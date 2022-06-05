//  -*- mode: c; indent-width: 8; -*-
//
//  WS/WSS Service example,
//  utilising https://gitlab.com/eidheim/Simple-WebSocket-Server
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

#if defined(_MSC_VER)
#define _SCL_SECURE_NO_WARNINGS 1
#define _CRT_SECURE_NO_WARNINGS 1
#define __STDC_LIMIT_MACROS 1
#pragma warning(disable:4244) // 'xxx': conversion from 'xxx' to 'xxx', possible loss of data
#pragma warning(disable:4503) // 'xxx': decorated name length exceeded, name was truncated)
#pragma warning(disable:4309) // 'static_cast': truncation of constant value
#endif

#include "../libinetd/ServiceGetOpt.h"
#include "../libinetd/SocketShare.h"

#if defined(HAVE_OPENSSL)
#include "wss_service.h"
#endif
#include "ws_service.h"
#include "ws_sslutil.h"

#define PROGNAME    "ws_service"

static const char *short_options = "i:";
static struct inetd::Getopt::Option long_options[] = {
        { "usage",      inetd::Getopt::argument_none,       NULL,   1100 },

        { "ip4",        inetd::Getopt::argument_none,       NULL,   1001 },
        { "ip6",        inetd::Getopt::argument_none,       NULL,   1002 },
//      { "nowait",     inetd::Getopt::argument_none,       NULL,   1003 },
//      { "wait",       inetd::Getopt::argument_none,       NULL,   1004 },
//      { "multi",      inetd::Getopt::argument_none,       NULL,   1005 },

#if defined(HAVE_OPENSSL)
        { "ssl",        inetd::Getopt::argument_none,       NULL,   1010 },
        { "cert",       inetd::Getopt::argument_required,   NULL,   1011 },
        { "privkey",    inetd::Getopt::argument_required,   NULL,   1012 },
        { "cacerts",    inetd::Getopt::argument_required,   NULL,   1013 },
        { "ciphers",    inetd::Getopt::argument_required,   NULL,   1014 },
#endif

        { NULL }
};

static bool         ssl = false;
static std::string  cert, privkey, cacerts, ciphers;

static void         usage(const char *fmt = NULL, ...); /*no-return*/
static int          process(SOCKET socket);

int
main(int argc, const char **argv)
{
        inetd::Getopt options(short_options, long_options, argv[0]);
        const char *basename = NULL;
        std::string msg;
        bool ip6 = false;

#if defined(_DEBUG) && defined(_WIN32)
        SimpleWeb::Crypto::unit_tests();
#endif

        ssl  = false;
//      cert = "cert://service,user,local/localhost";
//      cert = "cert://test.wininetd.dev";
        cert = "cert://test2.wininetd.dev";
//      cert = "hash://03:74:62:3a:2c:ac:ef:52:ee:dc:0e:11:42:16:83:ff:e8:d5:ad:05";
//      cert = "hash://0374623a2cacef52eedc0e11421683ffe8d5ad05";

//      cacerts = "store://CA";

        ciphers = "ECDH+AESGCM:DH+AESGCM:ECDH+AES256:DH+AES256:ECDH+AES128:DH+AES:RSA+AESGCM:RSA+AES:!aNULL:!MD5:!DSS";

        while (-1 != options.shift(argc, argv, msg)) {
                switch (options.optret()) {
                case 'i':   // interface
                        basename = options.optarg();
                        break;
                case 1001:  // --ip4
                        ip6 = false;
                        break;
                case 1002:  // --ip6
                        ip6 = true;
                        break;

                case 1010:  // ssl
                        ssl = true;
                        break;
                case 1011:  // certificate
                        cert = options.optarg();
                        break;
                case 1012:  // private key
                        privkey = options.optarg();
                        break;
                case 1013:  // ca certificates
                        cacerts = options.optarg();
                        break;
                case 1014:  // cipher list
                        ciphers = options.optarg();
                        break;

                case 1100:  // usage
                        usage();
                default:    // error
                        usage("%s", msg.c_str());
                }
        }

        if (NULL == basename) {
                usage("missing interface specification");
        }

        argv += options.optind();
        if (0 != (argc -= options.optind())) {
               usage("unexpected arguments %s ...", argv[0]);
        }

        SOCKET socket = inetd::SocketShare::GetSocket(basename, WSA_FLAG_OVERLAPPED /*asio*/);
        if (INVALID_SOCKET != socket) {
                return process(socket);
        }
}


void
usage(const char *fmt, ...)
{
        if (fmt) {
                va_list ap;
                va_start(ap, fmt);
                vfprintf(stderr, fmt, ap), fputs("\n\n", stderr);
                va_end(ap);
        }

        fprintf(stderr,
                "Usage: %s [options] -i interface\n\n", PROGNAME);
        fprintf(stderr,
                "options:\n"
                "   -i <interface>              Parent interface.\n"
                "   --ip[46]                    Interface type; default ip4.\n"
//              "   --[no]wait                  nowait/wait mode; default nowait.\n"
//              "   --multi                     Multisocket mode.\n"
#if defined(HAVE_OPENSSL)
                "   --ssl                       SSL mode.\n"
                "   --cert <cert>               Server certificate.\n"
                "   --privkey <key>             Private key, if required.\n"
                "   --cacerts <certs>           Client verification certificates.\n"
                "   --ciphers <list>            Cipher list.\n"
#endif
                );
        exit(3);
}


namespace {
        template <typename Type>
        typename std::enable_if<std::is_same<typename Type::Transport, SimpleWeb::WS>::value, void>::type
        peer_summary(std::shared_ptr<typename Type::Connection> &connection)
        {
        }

        template <typename Type>
        typename std::enable_if<std::is_same<typename Type::Transport, SimpleWeb::WSS>::value, void>::type
        peer_summary(std::shared_ptr<typename Type::Connection> &connection)
        {
                struct Output {
                        void operator()(unsigned line, const char *msg) {
                                std::cout << msg << '\n';
                        }
                };

                Output output;
                sslutil::summary(output, connection->get_socket().native_handle());
        }

        template <typename Type>
        int Service(Type &server, SOCKET socket)
        {
                //
                //  echo endpoint
                auto &echo = server.endpoint["^/echo/?$"];

                echo.on_open = [](std::shared_ptr<typename Type::Connection> connection) {
                        std::cout << "Server: Opened connection " << connection.get() << std::endl;
                };

                // See RFC 6455 7.4.1. for status codes
                echo.on_close = [](std::shared_ptr<typename Type::Connection> connection, int status, const std::string /*reason*/) {
                        std::cout << "Server: Closed connection " << connection.get() << " with status code " << status << std::endl;
                };

                // Can modify handshake response headers here if needed
                echo.on_handshake = [](std::shared_ptr<typename Type::Connection> connection, SimpleWeb::CaseInsensitiveMultimap &response_header) {

                    // [RFC6455]
                    //  The request MAY include a header field with the name |Sec-WebSocket-Protocol|.
                    //  If present, this value indicates one or more comma-separated subprotocol the client wishes to
                    //  speak, ordered by preference.  The elements that comprise this value MUST be non-empty strings
                    //  with characters in the range U+0021 to U+007E not including separator characters as defined
                    //  in [RFC2616] and MUST all be unique strings. The ABNF for the value of this header field is 1
                    //  #token, where the definitions of constructs and rules are as given in [RFC2616].
                    //
                    //  Sec-WebSocket-Protocol: subprotocols
                    //      A comma-separated list of subprotocol names, in the order of preference.
                    //      The subprotocols may be selected from the IANA WebSocket Subprotocol Name Registry
                    //      or may be a custom name jointly understood by the client and the server.
                    //
                    // [RFC6455]
                    //  The request MAY include a header field with the name |Sec-WebSocket-Extensions|.
                    //  If present, this value indicates the protocol-level extension(s) the client wishes to speak.
                    //  The interpretation and format of this header field is described in Section 9.1.
                    //
                    //  Sec-WebSocket-Extensions: extensions
                    //      A comma-separated list of extensions to request (or agree to support).
                    //      These should be selected from the IANA WebSocket Extension Name Registry.
                    //      Extensions which take parameters do so by using semicolon delineation.
                    //
                    peer_summary<Type>(connection);

                    std::string protocols;
                    auto protocol_it = connection->header.equal_range("Sec-WebSocket-Protocol");
                    for(auto it(protocol_it.first); it != protocol_it.second; it++) {
                            if (it->second.length()) {
                                    if (protocols.length()) protocols += ", ";
                                    protocols += it->second;
                            }
                    }

                    std::string extensions;
                    auto extensions_it = connection->header.equal_range("Sec-WebSocket-Extensions");
                    for(auto it(extensions_it.first); it != extensions_it.second; it++) {
                            if (it->second.length()) {
                                    if (extensions.length()) extensions += ", ";
                                    extensions += it->second;
                            }
                    }

                    return SimpleWeb::StatusCode::information_switching_protocols; // Upgrade to websocket
                };

                // See http://www.boost.org/doc/libs/1_55_0/doc/html/boost_asio/reference.html, Error Codes for error code meanings
                echo.on_error = [](std::shared_ptr<typename Type::Connection> connection, const SimpleWeb::error_code ec) {
                        std::cout << "Server: Error in connection " << connection.get() << ". "
                                << "Error: " << ec << ", error message: " << ec.message() << std::endl;
                };

                echo.on_message = [](std::shared_ptr<typename Type::Connection> connection, std::shared_ptr<typename Type::InMessage> in_message) {
                        auto out_message = in_message->string();

                        std::cout << "Server: Message received: \"" << out_message << "\" from " << connection.get() << std::endl;
                        std::cout << "Server: Sending message \"" << out_message << "\" to " << connection.get() << std::endl;

                        // connection->send is an asynchronous function
                        connection->send(out_message, [](const SimpleWeb::error_code &ec) {
                                if(ec) {
                                        std::cout << "Server: Error sending message. " <<
                                            // See http://www.boost.org/doc/libs/1_55_0/doc/html/boost_asio/reference.html, Error Codes for error code meanings
                                            "Error: " << ec << ", error message: " << ec.message() << std::endl;
                                }
                        });

                        // Alternatively use streams:
                        // auto out_message = make_shared<WsServer::OutMessage>();
                        // *out_message << in_message->string();
                        // connection->send(out_message);
                };

                std::cerr << socket << ": Connection open" << std::endl;
                server.client(socket, true);
                std::cerr << socket << ": Connection complete" << std::endl;

                return 0;
        }
};


static int
process(SOCKET socket)
{
#if defined(HAVE_OPENSSL)
       if (ssl) {
                wss_service service(cert, privkey, cacerts);

                service.set_cipher_list(ciphers);
#if defined(_DEBUG)
                service.set_diagnostics();
#endif
                return Service(service, socket);
        }
#endif /*HAVE_OPENSSL*/

        ws_service service;
        return Service(service, socket);
}

//end
