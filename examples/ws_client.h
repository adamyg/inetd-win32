//  WebSocket Client,
//  extended https://gitlab.com/eidheim/Simple-WebSocket-Server

#if defined(USE_STANDALONE_ASIO)
#define ASIO_DISABLE_BOOST_BIND 1
#endif

#include <cassert>
#include <string>

#include <client_ws.hpp>                        // ws:// client
#if defined(HAVE_OPENSSL)
#pragma comment(lib, "libssl.lib")
#pragma comment(lib, "libcrypto.lib")
#include <client_wss.hpp>                       // wss:// client
#if defined(_WIN32) && (HAVE_LIBCERTSTORE)
#include <ssl_certificate.hpp>
#include <ssl_cacertificates.hpp>
#endif
#endif  //HAVE_OPENSSL

namespace ws {

/////////////////////////////////////////////////////////////////////////////////////////
//  Client interface
//

#ifdef USE_STANDALONE_ASIO
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

struct Diagnostics {
};


struct Configuration {
    Configuration() : reconnect_interval(0), reconnect_interval_max(0),
        verify_host(2 /*full*/), max_depth(0), self_signed(false) {
    }
    unsigned reconnect_interval;                //!< Reconnection interval; default=none.
    unsigned reconnect_interval_max;            //!< Uupper bounds; interval is extended until exceeded then capped at max; default=none.
    unsigned verify_host;                       //!< Verify remote host; default=yes.
    unsigned max_depth;                         //!< Max chain depth; 0=none,1=certificate,2=cert+hostname.
    bool self_signed;                           //!< Allow self-signed server certificates; default=false.
    std::string proxy_server;                   //!< optional proxy.
    std::string protocol;                       //!< Optional protocol; default=none
    std::string authorisation;                  //!< Optional auhorisation header: default=none
    std::string cipher_list;                    //!< Optional cipher list.
    std::string certification_file;             //!< Optional client certificate; default=none
    std::string private_key_file;               //!< Optional client private key; default=none
    std::string verify_certificates_file;       //!< Optional server certificate(s); default=none
};


class Client {
public:
    virtual bool set_io_service(std::shared_ptr<asio::io_service> &io_service) = 0;
    virtual bool start(const Configuration &cfg, const std::string &host, const std::string &path, std::function<void()> callback = nullptr) = 0;
    virtual bool start(const Configuration &cfg, const std::string &endpoint, std::function<void()> callback = nullptr) = 0;
    virtual void stop() = 0;

    template <template <class> class Specialisation>
    static std::shared_ptr<Client>
    factory(Diagnostics &diags, bool ssl) {
        if (ssl) {
#if defined(HAVE_OPENSSL)
            return std::make_shared<typename Specialisation<WSSClient>>(std::ref(diags));
#else
            return nullptr;
#endif
        }
        return std::make_shared<typename Specialisation<WSClient>>(std::ref(diags));
    }

    template <template <class> class Specialisation>
    static std::shared_ptr<Client>
    ssl_factory(Diagnostics &diags) {
#if defined(HAVE_OPENSSL)
        return std::make_shared<typename Specialisation<WSSClient>>(std::ref(diags));
#else
        return nullptr;
#endif
    }

    template <template <class> class Specialisation>
    static std::shared_ptr<Client>
    std_factory(Diagnostics &diags) {
        return std::make_shared<typename Specialisation<WSClient>>(std::ref(diags));
    }
};


/////////////////////////////////////////////////////////////////////////////////////////
//  Client implementation
//

template <typename Specialisation, typename Transport>
class ClientCommon : public Client {
    ClientCommon(const ClientCommon &) = delete;
    ClientCommon& operator=(const ClientCommon &) = delete;

public:
    typedef typename Transport SessionType;
    typedef typename Transport::Connection Connection;
    typedef typename Transport::InMessage InMessage;

private:
    class ClientDecorator : public SessionType {
    public:
        ClientDecorator(const Configuration &cfg, const std::string &endpoint)
                : SessionType(endpoint) {
            common(cfg);
            transport(cfg);
#if defined(_DEBUG)
            diagnostics();
#endif
        }

    private:
        /// common configuration
        void common(const Configuration &cfg) {
            config.proxy_server = cfg.proxy_server;
            config.protocol = cfg.protocol;
            if (! cfg.authorisation.empty()) {
                set_header("Authorization", cfg.authorisation.c_str());
            }
        }

        /// ws specific configuration
        template<class Q = Transport>
        typename std::enable_if<std::is_same<Q, WSClient>::value, void>::type
        transport(const Configuration &cfg) {
        }

        /// wws specific configuration
        template<class Q = Transport>
        typename std::enable_if<std::is_same<Q, WSSClient>::value, void>::type
        transport(const Configuration &cfg) {
#if defined(HAVE_OPENSSL)
#if defined(_WIN32) && (HAVE_LIBCERTSTORE)          // optional client certificate
            if (-1 == CertStore::ssl_certificate::load(context.native_handle(), cfg.certification_file.c_str()))
#endif
#endif
                set_certification(cfg.certification_file, cfg.private_key_file);

#if defined(HAVE_OPENSSL)
#if defined(_WIN32) && (HAVE_LIBCERTSTORE)          // optional server ca-certificates
            if (-1 == CertStore::ssl_cacertificates::load(context.native_handle(), cfg.verify_certificates_file.c_str() /*true - cache*/))
#endif
#endif
                set_verify_certificates(cfg.verify_certificates_file);

                                                    // server verify options
            set_verify_options(0 == cfg.verify_host ? Transport::none :
                (1 == cfg.verify_host ? Transport::basic : Transport::rfc2818), cfg.max_depth, cfg.self_signed);

            {   const char *ciphers = cfg.cipher_list.c_str();
	        if (0 == strcmp(ciphers, "default") || 0 == strcmp(ciphers, "secure")) {
		    ciphers = CIPHERS_DEFAULT;
                } else if (0 == strcmp(ciphers, "compat") || 0 == strcmp(ciphers, "legacy")) {
                    ciphers = CIPHERS_COMPAT;
                }
                set_cipher_list(ciphers);           // cipher list
            }
        }

    private:
        /// ws diagnostics
        template<class Q = Transport>
        typename std::enable_if<std::is_same<Q, WSClient>::value, bool>::type
        diagnostics() {
            return false;
        }

        /// wws specific configuration; SSL status hook.
        template<class Q = Transport>
        typename std::enable_if<std::is_same<Q, WSSClient>::value, bool>::type
        diagnostics() {
            if (SSL_CTX *ctx = context.native_handle()) {
                assert(ctx);
                if (! SSL_CTX_get_info_callback(ctx)) {
                    SSL_CTX_set_info_callback(ctx, ssl_ctx_info_cb);
                    return true;
                }
            }
            return false;
        }

#if defined(HAVE_OPENSSL)
        /// Connection run-time diagnositics.
        static void ssl_ctx_info_cb(const SSL *ssl, int where, int ret) {
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
#endif  //HAVE_OPENSSL
    };

public:
    ClientCommon(Diagnostics &diags) :
        reconnect_interval_(0), reconnect_interval_max_(0), connect_attempt_(0), started_(false) {
    }

    virtual ~ClientCommon() {
        stop();
    }

    bool set_io_service(std::shared_ptr<asio::io_service> &io_service) {
        assert(! started_);
        if (started_) return false;
        io_service_.swap(io_service);
        return true;
    }

    bool start(const Configuration &cfg, const std::string &host, const std::string &path, std::function<void()> callback = nullptr) {
        return start(cfg, host + "/" + path, callback);
    }

    bool start(const Configuration &cfg, const std::string &endpoint, std::function<void()> callback = nullptr) {
        assert(! started_);
        if (started_) return false;
        reconnect_interval_ = cfg.reconnect_interval;
        reconnect_interval_max_ = cfg.reconnect_interval_max;
        client_ = std::make_shared<ClientDecorator>(cfg, endpoint);
        if (client_) {
            bindings();
            client_->start(callback);
            started_ = true;
            return true;
        }
        return false;
    }

    void stop() {
    }

protected:
    void on_open(std::shared_ptr<Connection> &connection) {
        static_cast<Specialisation *>(this)->on_open(connection); //CRTP
    }

    void on_close(std::shared_ptr<Connection> &connection, bool success) {
        static_cast<Specialisation *>(this)->on_close(connection, success); //CRTP
    }

    void on_message(std::shared_ptr<Connection> &connection, std::shared_ptr<InMessage> &inmessage) {
        static_cast<Specialisation *>(this)->on_message(connection, inmessage); //CRTP
    }

private:
    void bindings() {
        using namespace std::placeholders;      // _1, _2 and _3

        if (io_service_) {                      // local io_service
            client_->io_service = io_service_;
        }
        client_->on_open    = std::bind(&ClientCommon::cb_onopen, this, _1);
        client_->on_message = std::bind(&ClientCommon::on_message, this, _1, _2);
        client_->on_close   = std::bind(&ClientCommon::cb_onclose, this, _1, _2, _3);
        client_->on_error   = std::bind(&ClientCommon::cb_onerror, this, _1, _2);
        client_->on_ping    = std::bind(&ClientCommon::cb_onping, this, _1);
        client_->on_pong    = std::bind(&ClientCommon::cb_onpong, this, _1);
    }

    void cb_onopen(std::shared_ptr<Connection> &connection) {
        connect_attempt_ = 0;
//TODO  connection_ = connection->weak_from_this();
        on_open(connection);
    }

    void cb_onclose(std::shared_ptr<Connection> &connection, int status, const std::string &reason) {
//      DIAGIO_STREAM_INFO()
//      DIAGIO_STREAM()
//          << label() << ": Closed connection with status code " << status << " [" << reason << "]";
//      DIAGIO_STREAM_END()
        connection_.reset();
        on_close(connection, true);
        if (started_ && reconnect_interval_) {
            reconnect_start(connection);
        }
    }

    void cb_onerror(std::shared_ptr<Connection> &connection, const SimpleWeb::error_code &ec) {
//      DIAGIO_STREAM_INFO()
        connection_.reset();
        on_close(connection, false);

        if (started_ && reconnect_interval_) {
            if (ec.value() == 335544539) {      // connection was closed unexpectedly.
//              DIAGIO_STREAM()
//                  << label() << ": Error: " << ec << " [connection was closed unexpectedly], error message: " << ec.message() << " -- retrying, attempt:" << reconnect_attempts_;
            } else {
//              DIAGIO_STREAM()
//                  << label() << ": Error: " << ec << ", error message: " << ec.message() << " -- retrying";
            }
            reconnect_start(connection);
        } else {
//          DIAGIO_STREAM()
//              << label() << ": Error: " << ec << ", error message: " << ec.message();
            if (reconnect_timer_) {
                reconnect_timer_->cancel();
                reconnect_timer_.reset();
            }
        }
//      DIAGIO_STREAM_END()
    }

    void cb_onping(std::shared_ptr<Connection> &connection) {
//      DIAGIO_STREAM_INFO()
//      DIAGIO_STREAM()
//          << label() << ": ping";
//      DIAGIO_STREAM_END()
    }

    void cb_onpong(std::shared_ptr<Connection> &connection) {
//      DIAGIO_STREAM_INFO()
//      DIAGIO_STREAM()
//          << label() << ": pong";
//      DIAGIO_STREAM_END()
    }

    void reconnect_start(std::shared_ptr<Connection> &connection) {
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

    void reconnect_start(std::shared_ptr<Connection> &connection, unsigned inseconds) {
        if (! reconnect_timer_) {
            reconnect_timer_ =
                std::shared_ptr<asio::steady_timer>(new(std::nothrow) asio::steady_timer(*client_->io_service.get()));
        }

        if (reconnect_timer_) {
            reconnect_timer_->expires_from_now(std::chrono::seconds(inseconds));
            reconnect_timer_->async_wait(std::bind(&ClientCommon::cb_reconnect_timer, this, connection, std::placeholders::_1 /*asio::placeholders::error*/));
        }
    }

    void cb_connect_timer(const error_code &ec) {
        if (ec != boost::asio::error::operation_aborted) {
            if (started_) {
                client_->start();
            }
        }
        connect_timer_.reset();
    }

    void cb_reconnect_timer(std::shared_ptr<Connection> &connection, const error_code &ec) {
//      DIAGIO_STREAM_INFO()
        if (ec != asio::error::operation_aborted) {
            if (started_) {
//              DIAGIO_STREAM()
//                  << label() << ": reconnecting, attempt:" << reconnect_attempts_;;
//              client_->reconnect(connection);
            }
        }
//      DIAGIO_STREAM_END()
    }

private:
    std::shared_ptr<ClientDecorator> client_;   //!< Client instance.
    std::weak_ptr<Connection> connection_;      //!< Active connection.
    std::shared_ptr<asio::steady_timer> connect_timer_;
    std::shared_ptr<asio::steady_timer> reconnect_timer_;
    std::shared_ptr<asio::io_context> io_service_;
    unsigned reconnect_interval_;               //!< reconnection initial interval.
    unsigned reconnect_interval_max_;           //!< Upper reconnection interval; optional
    unsigned connect_attempt_;                  //!< Attempt accumulator.
    bool started_;                              //!< Status.
};

};  //namespace ws

//end
