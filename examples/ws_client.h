#pragma once
#ifndef WS_CLIENT_H_INCLUDED
#define WS_CLIENT_H_INCLUDED
//  -*- mode: c; indent-width: 8; -*-
//
//  WebSocket Client,
//  extended https://gitlab.com/eidheim/Simple-WebSocket-Server
//
//  Copyright (c) 2020 - 2022, Adam Young.
//
//  The applications are free software: you can redistribute it
//  and/or modify it under the terms of the GNU General Public License as
//  published by the Free Software Foundation, version 3.
//
//  Redistributions of source code must retain the above copyright
//  notice, and must be distributed with the license document above.
//
//  Redistributions in binary form must reproduce the above copyright
//  notice, and must include the license document above in
//  the documentation and/or other materials provided with the
//  distribution.
//
//  This project is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
//  license for more details.
//  ==end==
//

#if defined(ASIO_STANDALONE)
#define ASIO_DISABLE_BOOST_BIND 1
#endif

#undef bind
#include <cstdarg>
#include <cassert>
#include <string>
#include <functional>
#include <memory>

#include <client_ws.hpp>                        // ws:// client
#if defined(HAVE_OPENSSL)
#include <client_wss.hpp>                       // wss:// client
#if defined(HAVE_LIBCERTSTORE)
#include <ssl_certificate.hpp>
#include <ssl_cacertificates.hpp>
#endif
#if !defined(__MINGW32__)
#pragma comment(lib, "libssl.lib")
#pragma comment(lib, "libcrypto.lib")
#endif
#else
#undef HAVE_LIBCERTSTORE
#endif  //HAVE_OPENSSL

#include "ws_diagnostics.h"

namespace ws {


/////////////////////////////////////////////////////////////////////////////////////////
//  Client interface
//

#ifdef ASIO_STANDALONE
namespace error = asio::error;
using error_code = std::error_code;
using errc = std::errc;
using system_error = std::system_error;
namespace make_error_code = std;
#else
namespace asio = boost::asio;
namespace error = asio::error;
using error_code = boost::system::error_code;
namespace errc = boost::system::errc;
using system_error = boost::system::system_error;
namespace make_error_code = boost::system::errc;
#endif

// note: mirror libtls
#define CIPHERS_COMPAT      "ALL:!aNULL:!eNULL"
#define CIPHERS_DEFAULT     "TLSv1.2+AEAD+ECDHE:TLSv1.2+AEAD+DHE"

using WSClient = SimpleWeb::SocketClient<SimpleWeb::WS>;
#if defined(HAVE_OPENSSL)
using WSSClient = SimpleWeb::SocketClient<SimpleWeb::WSS>;
#endif


/////////////////////////////////////////////////////////////////////////////////////////
//  Configuration
//

struct Configuration {
        Configuration() : reconnect_interval(0), reconnect_interval_max(0),
                verify_host(2 /*full*/), max_depth(0), self_signed(false) {
        }

        unsigned reconnect_interval;            //!< Reconnection interval; default=none.
        unsigned reconnect_interval_max;        //!< Uupper bounds; interval is extended until exceeded then capped at max; default=none.
        unsigned verify_host;                   //!< Verify remote host; default=yes.
        unsigned max_depth;                     //!< Max chain depth; 0=none,1=certificate,2=cert+hostname.
        bool self_signed;                       //!< Allow self-signed server certificates; default=false.
        std::string proxy_server;               //!< optional proxy.
        std::string protocol;                   //!< Optional protocol; default=none
        std::string authorisation;              //!< Optional auhorisation header: default=none
        std::string cipher_list;                //!< Optional cipher list.
        std::string certification_file;         //!< Optional client certificate; default=none
        std::string private_key_file;           //!< Optional client private key; default=none
        std::string verify_certificates_file;   //!< Optional server certificate(s); default=none
};


/////////////////////////////////////////////////////////////////////////////////////////
//  Client
//

class Client {
public:
        virtual bool set_io_service(std::shared_ptr<asio::io_service> &io_service) = 0;
        virtual bool start(const Configuration &cfg, const std::string &host, const std::string &path, std::function<void()> callback = nullptr) = 0;
        virtual bool start(const Configuration &cfg, const std::string &endpoint, std::function<void()> callback = nullptr) = 0;
        virtual void stop() = 0;
};


/////////////////////////////////////////////////////////////////////////////////////////
//  Client implementation
//

template <template <class> class Specialisation, typename Transport>
class ClientCommon
                : public Specialisation<Transport>, public Client {
        ClientCommon(const ClientCommon &) = delete;
        ClientCommon& operator=(const ClientCommon &) = delete;

public:
        typedef typename Transport::Connection Connection;
        typedef typename Transport::InMessage InMessage;
        typedef Specialisation<Transport> ISpecialisation;

private:
#if defined(HAVE_LIBCERTSTORE)
        struct DiagnosticsCertSink : public CertStore::ICertSink {
                DiagnosticsCertSink(Diagnostics &diags) : diags_(diags) {
                }

                virtual void message(const char *msg) {
                        diags_.message(msg);
                }

                Diagnostics &diags_;
        };
#endif

        class ClientDecorator : public Transport {
        public:
            ClientDecorator(const Configuration &cfg, Diagnostics &diags, const std::string &endpoint)
                    : Transport(endpoint), diags_(diags)
            {
                    common(cfg);
                    transport(cfg);
#if defined(_DEBUG)
                    diagnostics();
#endif
            }

    private:
            /// common configuration
            void common(const Configuration &cfg)
            {   
                    Transport::config.proxy_server = cfg.proxy_server;
                    Transport::config.protocol = cfg.protocol;
                    if (! cfg.authorisation.empty()) {
                            Transport::set_header("Authorization", cfg.authorisation.c_str());
                    }
            }

            /// ws specific configuration
            template<class Q = Transport>
            typename std::enable_if<std::is_same<Q, WSClient>::value, void>::type
            transport(const Configuration &cfg)
            {
            }
                                      
#if defined(HAVE_OPENSSL)
            /// wws specific configuration
            template<class Q = Transport>
            typename std::enable_if<std::is_same<Q, WSSClient>::value, void>::type
            transport(const Configuration &cfg)
            {
#if defined(HAVE_LIBCERTSTORE)
                    DiagnosticsCertSink sink(diags_);
#endif

#if defined(HAVE_LIBCERTSTORE)                  // optional client certificate
                    auto native_handle = Transport::context.native_handle();
                    if (-1 == CertStore::ssl_certificate::load(sink, native_handle, cfg.certification_file.c_str()))
#endif //HAVE_LIBCERTSTORE
                            Transport::set_certification(cfg.certification_file, cfg.private_key_file);

#if defined(HAVE_LIBCERTSTORE)                  // optional server ca-certificates
                    if (-1 == CertStore::ssl_cacertificates::load(sink, native_handle, cfg.verify_certificates_file.c_str() /*true - cache*/))
#endif //HAVE_LIBCERTSTORE
                            Transport::set_verify_certificates(cfg.verify_certificates_file);

                                                // server verify options
                    Transport::set_verify_options(0 == cfg.verify_host ? Transport::none :
                        (1 == cfg.verify_host ? Transport::basic : Transport::rfc2818), cfg.max_depth, cfg.self_signed);

                    {       const char *ciphers = cfg.cipher_list.c_str();
                            if (0 == strcmp(ciphers, "default") || 0 == strcmp(ciphers, "secure")) {
                                    ciphers = CIPHERS_DEFAULT;
                            } else if (0 == strcmp(ciphers, "compat") || 0 == strcmp(ciphers, "legacy")) {
                                    ciphers = CIPHERS_COMPAT;
                            }
                            Transport::set_cipher_list(ciphers);
                    }
            }
#endif //HAVE_OPENSSL

    private:
            /// ws diagnostics
            template<class Q = Transport>
            typename std::enable_if<std::is_same<Q, WSClient>::value, bool>::type
            diagnostics()
            {
                    return false;
            }

#if defined(HAVE_OPENSSL)
            /// wss specific configuration; SSL status hook.
            template<class Q = Transport>
            typename std::enable_if<std::is_same<Q, WSSClient>::value, bool>::type
            diagnostics()
            {
                    if (SSL_CTX *ctx = Transport::context.native_handle()) {
                            assert(ctx);
                            if (! SSL_CTX_get_info_callback(ctx)) {
                                    SSL_CTX_set_info_callback(ctx, ssl_ctx_info_cb);
                                    return true;
                            }
                    }
                    return false;
            }
#endif //HAVE_OPENSSL

#if defined(HAVE_OPENSSL)
            /// Connection run-time diagnositics.
            static void ssl_ctx_info_cb(const SSL *ssl, int where, int ret)
            {
                    char message[1024];
                    const char *str;
                    int w;

                    w = where & ~SSL_ST_MASK;
                    if (w & SSL_ST_CONNECT) str = "SSL_connect";
                    else if (w & SSL_ST_ACCEPT) str = "SSL_accept";
                    else str = "undefined";

                    if (SSL_CB_LOOP & where) {
                            sprintf_s(message, sizeof(message),
                                "SSL: %s:%s", str, SSL_state_string_long(ssl));
                            std::cout << message << '\n';

                    } else if (SSL_CB_ALERT & where) {
                            sprintf_s(message, sizeof(message),
                                "SSL: SSL3 alert: %s:%s:%s",
                                    where & SSL_CB_READ ? "read (remote end reported an error)" : "write (local SSL3 detected an error)",
                                    SSL_alert_type_string_long(ret), SSL_alert_desc_string_long(ret));
                            std::cout << message << '\n';

                    } else if ((SSL_CB_EXIT & where) && ret <= 0) {
                            sprintf_s(message, sizeof(message), "SSL: %s:%s in %s",
                                    str, ret == 0 ? "failed" : "error", SSL_state_string_long(ssl));
                            std::cout << message << '\n';
                    }
            }
#endif //HAVE_OPENSSL

        private:
                Diagnostics &diags_;
        };

public:
        ClientCommon(Diagnostics &diags)
                : diags_(diags), reconnect_interval_(0), reconnect_interval_max_(0), connect_attempt_(0), started_(false)
        {
        }

        virtual ~ClientCommon()
        {
                stop();
        }

        bool set_io_service(std::shared_ptr<asio::io_service> &io_service)
        {
                assert(! started_);
                if (started_) return false;
                io_service_.swap(io_service);
                return true;
        }

        bool start(const Configuration &cfg, const std::string &host, const std::string &path, std::function<void()> callback = nullptr)
        {
                return start(cfg, host + "/" + path, callback);
        }

        bool start(const Configuration &cfg, const std::string &endpoint, std::function<void()> callback = nullptr)
        {
                assert(! started_);
                if (started_) return false;
                reconnect_interval_ = cfg.reconnect_interval;
                reconnect_interval_max_ = cfg.reconnect_interval_max;
                client_ = std::make_shared<ClientDecorator>(cfg, diags_, endpoint);
                if (client_) {
                        bindings();
                        client_->start(callback);
                        started_ = true;
                        return true;
                }
                return false;
        }

        void stop()
        {
        }

private:
        void bindings()
        {
                using namespace std::placeholders; // _1, _2 and _3

                if (io_service_) { // local io_service
                        client_->io_service = io_service_;
                }

//              client_->on_attempt = std::bind(&ClientCommon::cb_onattempt, this, _1, _2, _3);
//              client_->on_open    = std::bind(&ClientCommon::cb_onopen, this, _1);
//              client_->on_message = std::bind(&ClientCommon::on_message, this, _1, _2);
//              client_->on_close   = std::bind(&ClientCommon::cb_onclose, this, _1, _2, _3);
//              client_->on_error   = std::bind(&ClientCommon::cb_onerror, this, _1, _2);
//              client_->on_ping    = std::bind(&ClientCommon::cb_onping, this, _1);
//              client_->on_pong    = std::bind(&ClientCommon::cb_onpong, this, _1);

                client_->on_attempt = [&](std::shared_ptr<Connection> connection, const error_code ec, asio::ip::tcp::resolver::iterator next) {
                                            this->cb_onattempt(connection, ec, next);
                                        };
                client_->on_open    = [&](std::shared_ptr<Connection> connection) {
                                            this->cb_onopen(connection);
                                        };
                client_->on_message = [&](std::shared_ptr<Connection> connection, std::shared_ptr<InMessage> in_message) {
                                            this->on_message(connection, in_message);
                                        };
                client_->on_close   = [&](std::shared_ptr<Connection> connection, int status, const std::string &reason) {
                                            this->cb_onclose(connection, status, reason);
                                        };
                client_->on_error   = [&](std::shared_ptr<Connection> connection, const SimpleWeb::error_code &ec) {
                                            this->cb_onerror(connection, ec);
                                        };
                client_->on_ping    = [&](std::shared_ptr<Connection> connection) {
                                            this->cb_onping(connection);
                                        };
                client_->on_pong    = [&](std::shared_ptr<Connection> connection) {
                                            this->cb_onpong(connection);
                                        };
        }

protected:
        const char *label() const
        {
                return "ws";
        }

        void on_open(std::shared_ptr<Connection> &connection)
        {
                static_cast<ISpecialisation *>(this)->on_open(connection); //CRTP
        }

        void on_close(std::shared_ptr<Connection> &connection, bool success)
        {
                static_cast<ISpecialisation *>(this)->on_close(connection, success); //CRTP
        }

        void on_message(std::shared_ptr<Connection> &connection, std::shared_ptr<InMessage> &inmessage)
        {
                static_cast<ISpecialisation *>(this)->on_message(connection, inmessage); //CRTP
        }

private:
        void cb_onattempt(std::shared_ptr<Connection>, const error_code &ec, asio::ip::tcp::resolver::iterator next)
        {
                if (ec) {
                        diags_.stream()
                            << label() << ": Connection error: " << ec.message();
                }

                diags_.stream()
                    << label() << ": Trying <" << next->endpoint() << "> ...";
        }

        void cb_onopen(std::shared_ptr<Connection> &connection)
        {
                connect_attempt_ = 0;
                on_open(connection);
        }

        void cb_onclose(std::shared_ptr<Connection> &connection, int status, const std::string &reason)
        {
                diags_.stream()
                        << label() << ": Closed connection with status code " << status << " [" << reason << "]";

                on_close(connection, true);
                if (started_ && reconnect_interval_) {
                        reconnect_start(connection);
                }
        }

        void cb_onerror(std::shared_ptr<Connection> connection, const SimpleWeb::error_code &ec)
        {
                on_close(connection, false);
                if (started_ && reconnect_interval_) {
                        if (ec.value() == 335544539) {
                                // connection was closed unexpectedly.
                                diags_.stream()
                                    << label() << ": Error: " << ec << " [connection was closed unexpectedly], error message: " 
                                    << ec.message() << " -- retrying, attempt:" << connect_attempt_;

                        } else {
                                diags_.stream()
                                    << label() << ": Error: " << ec << ", error message: " << ec.message() << " -- retrying";
                        }
                        reconnect_start(connection);

                } else {
                        diags_.stream()
                            << label() << ": Error: " << ec << ", error message: " << ec.message();
                        if (reconnect_timer_) {
                                reconnect_timer_->cancel();
                                reconnect_timer_.reset();
                        }
                }
        }

        void cb_onping(std::shared_ptr<Connection> &connection)
        {
                diags_.stream()
                    << label() << ": ping";
        }

        void cb_onpong(std::shared_ptr<Connection> &connection)
        {
                diags_.stream()
                    << label() << ": pong";
        }

        void reconnect_start(std::shared_ptr<Connection> &connection)
        {
                assert(reconnect_interval_);
                if (reconnect_interval_) {
                        unsigned next =
                                (1 == ++connect_attempt_ ? reconnect_interval_ :
                                    (unsigned)(reconnect_interval_ + ((float)reconnect_interval_ * 0.01 * connect_attempt_)));
                        if (reconnect_interval_max_) {
                                next = std::min(next, reconnect_interval_max_);
                        }
                        reconnect_start(connection, next);
                }
        }

        void reconnect_start(std::shared_ptr<Connection> &connection, unsigned inseconds)
        {
                if (! reconnect_timer_) {
                        reconnect_timer_ =
                            std::shared_ptr<asio::steady_timer>(new(std::nothrow) asio::steady_timer(*client_->io_service.get()));
                }

                if (reconnect_timer_) {
                        reconnect_timer_->expires_from_now(std::chrono::seconds(inseconds));
                        reconnect_timer_->async_wait(std::bind(&ClientCommon::cb_reconnect_timer, this, connection, std::placeholders::_1 /*asio::placeholders::error*/));
                }
        }

        void cb_connect_timer(const error_code &ec)
        {
                if (ec != asio::error::operation_aborted) {
                        if (started_) {
                                client_->start();
                        }
                }
                connect_timer_.reset();
        }

        void cb_reconnect_timer(std::shared_ptr<Connection> &connection, const error_code &ec)
        {
                if (ec != asio::error::operation_aborted) {
                        if (started_) {
                                diags_.stream()
                                    << label() << ": reconnecting, attempt:" << connect_attempt_;
                                client_->reconnect(connection);
                                ++connect_attempt_;
                        }
                }
        }

private:
        Diagnostics &diags_;
        std::shared_ptr<ClientDecorator> client_; //!< Client instance.
        std::shared_ptr<asio::steady_timer> connect_timer_;
        std::shared_ptr<asio::steady_timer> reconnect_timer_;
        std::shared_ptr<asio::io_context> io_service_;
        unsigned reconnect_interval_;           //!< reconnection initial interval.
        unsigned reconnect_interval_max_;       //!< Upper reconnection interval; optional
        unsigned connect_attempt_;              //!< Attempt accumulator.
        bool started_;                          //!< Status.
};   


//          template<typename... Args>
//          ExampleSpecialisation(Args&&... args)
//              : ws::ClientCommon<ExampleSpecialisation<Transport>,Transport>(std::forward<Args>(args)...) {
//          }

template <template <class> class Specialisation>
static std::shared_ptr<Client>
client_factory(bool ssl, Diagnostics &diags)
{
        if (ssl) {
#if defined(HAVE_OPENSSL)
                return std::make_shared< ClientCommon<Specialisation, WSSClient> >(std::ref(diags));
#else
                return nullptr;
#endif        
        }
        return std::make_shared< ClientCommon<Specialisation, WSClient> >(std::ref(diags));
}

}; //namespace ws

#endif //WS_CLIENT_H_INCLUDED

//end
