//  -*- mode: c; indent-width: 8; -*-
//
//  WS/WSS client example,
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
#define __STDC_LIMIT_MACROS 1
#pragma warning(disable:4244) // 'xxx': conversion from 'xxx' to 'xxx', possible loss of data
#pragma warning(disable:4503) // 'xxx': decorated name length exceeded, name was truncated)
#pragma warning(disable:4309) // 'static_cast': truncation of constant value
#endif

#include <iostream>
#include <utility>

#include "../libinetd/ServiceGetOpt.h"

#include "ws_client.h"                          // client implementation

#define PROGNAME    "ws_client"

static void         usage(const inetd::Getopt &options, const char *fmt, ...); /*no-return*/

static const char *short_options = "h:u:s";
static struct inetd::Getopt::Option long_options[] = {
        { "host",       inetd::Getopt::argument_required,   NULL,   'h'  },
        { "url",        inetd::Getopt::argument_required,   NULL,   'u'  },
        { "ssl",        inetd::Getopt::argument_none,       NULL,   's'  },
#if defined(HAVE_OPENSSL)
        { "proxy",      inetd::Getopt::argument_required,   NULL,   'p'  },
        { "cert",       inetd::Getopt::argument_required,   NULL,   'c'  },
        { "key",        inetd::Getopt::argument_required,   NULL,   'k'  },
        { "cacerts",    inetd::Getopt::argument_required,   NULL,   'a'  },
        { "ciphers",    inetd::Getopt::argument_required,   NULL,   'l'  },
        { "chaindepth", inetd::Getopt::argument_required,   NULL,   'd'  },
        { "selfsigned", inetd::Getopt::argument_none,       NULL,   't'  },
#endif
        { "usage",      inetd::Getopt::argument_none,       NULL,   1100 },
        { NULL }
};

static std::string host, path;
static int ssl = -1;

namespace {                                     // client specialisation (CRTP style)
        template <typename Transport>
        class ExampleSpecialisation {
        public:
            typedef typename Transport::Connection Connection;
            typedef typename Transport::InMessage InMessage;

            void on_open(std::shared_ptr<Connection> &connection) {
            }

            void on_close(std::shared_ptr<Connection> &connection, bool success) {
            }

            void on_message(std::shared_ptr<Connection> &connection, std::shared_ptr<InMessage> &inmessage) {
            }
        };
};


int
main(int argc, const char **argv)
{
        inetd::Getopt options(short_options, long_options, argv[0]);
        ws::Configuration cfg;                  // run-time configuration
        std::string msg;

        while (-1 != options.shift(argc, argv, msg)) {
                switch (options.optret()) {
                case 'h': {         // hostname, [ws:|wss:]/]host[:port]
                            const char *t_host = options.optarg();

                            if (! host.empty()) {
                                    usage(options, "multiple hosts specified <%s> and <%s>", host.c_str(), t_host);
                            }
                                                // embedded endpoint
                            if (char *sep = const_cast<char *>(strstr(t_host, "://"))) {
                                    if (0 == std::strncmp(t_host, "ws://", 5)) {
                                            ssl = false, host = t_host + 5;
#if defined(HAVE_OPENSSL)
                                    } else if (0 == strncmp(t_host, "wss://", 6)) {
                                            ssl = true,  host = t_host + 6;
#endif
                                    } else {
                                            sep[3] = 0;
                                            usage(options, "unknown endpoint <%s>", t_host);
                                    }
                            } else {
                                    host = t_host;
                            }

                            if (host.empty()) {
                                    usage(options, "empty hostname");
                            }
                    }
                    break;
                case 'u': {         // url/path, [[ws:|wss:]//host[:port]/]<path>
                        const char *t_path = options.optarg();

                        if (! path.empty()) {   // existing?
                                usage(options, "multiple paths specified <%s> and <%s>", path.c_str(), t_path);
                        }
                                                // optional embedded endpoint
                        if (char *sep = const_cast<char *>(strstr(t_path, "://"))) {
                                if (0 == std::strncmp(t_path, "ws://", 5)) {
                                        ssl = false, t_path += 5;
#if defined(HAVE_OPENSSL)
                                } else if (0 == strncmp(t_path, "wss://", 6)) {
                                        ssl = true, t_path += 6;
#endif
                                } else {
                                        sep[3] = 0;
                                        usage(options, "unknown endpoint <%s>", t_path);
                                }

                                if (nullptr == (sep = const_cast<char *>(strchr(t_path, '/'))) || t_path == sep) {
                                        usage(options, "missing hostname component");
                                }
                                *sep = 0;       // terminate hostname

                                if (! host.empty()) {
                                        usage(options, "multiple hosts specified <%s> and <%s>", host.c_str(), t_path);
                                }

                                host = t_path;
                                path = sep + 1;

                        } else {
                                path = t_path;
                        }

                        if (path.empty()) {
                                usage(options, "empty path component");
                        }
                    }
                    break;
                case 's':           // ssl/secure
                        if (-1 != ssl) {
                                usage(options, "ssl mode already selected");
                        }
                        ssl = true;
                        break;
                case 'p':           // proxy
                        cfg.proxy_server = options.optarg();
                        break;
                case 'c':           // cert
                        cfg.certification_file = options.optarg();
                        break;
                case 'k':           // key
                        cfg.private_key_file = options.optarg();
                        break;
                case 'a':           // cacerts
                        cfg.verify_certificates_file = options.optarg();
                        break;
                case 'l':           // cipher-list
                        cfg.cipher_list = options.optarg();
                        break;
                case 'd':           // chain-depth
                        cfg.max_depth = (unsigned)strtoul(options.optarg(), NULL, 10);
                        break;
                case 't':           // self signed (test)
                        cfg.self_signed = true;
                        break;
                case 1100:          // usage
                case '?':
                        usage(options, nullptr);
                default:            // error
                        usage(options, "%s", msg.c_str());
                }
        }

        argv += options.optind();
        if (0 != (argc -= options.optind())) {
                usage(options, "unexpected arguments %s ...", argv[0]);
        }

        ws::Diagnostics diags;
        std::shared_ptr<ws::Client> client(
                ws::client_factory<ExampleSpecialisation>(1 == ssl, std::ref(diags)));
        if (client) {
                client->start(cfg, host, path);
        }
        return 0;
}


static void
usage(const inetd::Getopt &options, const char *fmt, ...)
{
        if (fmt) {
                va_list ap;
                va_start(ap, fmt);
                vfprintf(stderr, fmt, ap), fputs("\n\n", stderr);
                va_end(ap);
        }

        fprintf(stderr,
                "Usage: %s [options]\n\n", options.progname());
        fprintf(stderr,
                "options:\n"
                "   -h,--host <hostname>        Hostname ([ws:|wss:]/]host[:port]).\n"
                "   -u,--url <path>             Remote resource url ([[ws:|wss:]//host[:port]/]<path>).\n"
#if defined(HAVE_OPENSSL)
                "   -s,--ssl                    Explicit SSL mode; unless embedded within host/url.\n"
                "   -p,--proxy <server>         Proxy server.\n"
                "   -c,--cert <cert>            Client certificate.\n"
                "   -k,--key <cert>             Private private key, dependent on certificate type.\n"
                "   -a,--cacerts <certs>        Verification certificate(s).\n"
        //      "   -l,--ciphers <list>         Cipher list; default=openssl default openssl.\n"
                "   -d,--chaindepth <depth>     Certificate chain depth; default=2.\n"
                "   -t,--selfsign               Permit self-signed certificates, use for testing only; default=no.\n"
#if defined(_DEBUG)
                "   -w,--warnonly               Verify warnings only, allow insecure certificates.\n"
#endif  //_DEBUG
#endif  //HAVE_OPENSSL
                );
        exit(3);
}

//end
