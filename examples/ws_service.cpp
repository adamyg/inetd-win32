//
//  WS Service example,
//  utilising https://gitlab.com/eidheim/Simple-WebSocket-Server
//

#if defined(_MSC_VER)
#define _SCL_SECURE_NO_WARNINGS 1
#define __STDC_LIMIT_MACROS 1
#pragma warning(disable:4244) // 'xxx': conversion from 'xxx' to 'xxx', possible loss of data
#pragma warning(disable:4503) // 'xxx': decorated name length exceeded, name was truncated)
#endif

#include "../libinetd/ServiceGetOpt.h"
#include "../libinetd/SocketShare.h"
#include "ws_service.h"

#define PROGNAME    "ws_service"

static const char *short_options = "i:";
static struct inetd::Getopt::Option long_options[] = {
    { "usage",      inetd::Getopt::argument_none,   NULL,   1000 },
    { "ip4",        inetd::Getopt::argument_none,   NULL,   1001 },
    { "ip6",        inetd::Getopt::argument_none,   NULL,   1002 },
//  { "nowait",     inetd::Getopt::argument_none,   NULL,   1003 },
//  { "wait",       inetd::Getopt::argument_none,   NULL,   1004 },
//  { "multi",      inetd::Getopt::argument_none,   NULL,   1005 },
//  { "ssl",        inetd::Getopt::argument_none,   NULL,   1006 },
    { NULL }
};

extern void usage(const char *fmt = NULL, ...); /*no-return*/
extern int  process(SOCKET socket);

int
main(int argc, const char **argv)
{
    inetd::Getopt options(short_options, long_options, argv[0]);
    const char *basename = NULL;
    std::string msg;
    bool ip6 = false;

    while (-1 != options.shift(argc, argv, msg)) {
        switch (options.optret()) {
        case 'i':   // interface
            basename = options.optarg();
            break;
        case 1002:  // --ip4
            ip6 = false;
            break;
        case 1001:  // --ip6
            ip6 = true;
            break;
        case 1000:  // usage
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
        usage("unexpected arguments");
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
        "   -i <interface>      Parent interface.\n"
        "   --ip[46]            Interface type; default ip4.\n"
//      "   --[no]wait          nowait/wait mode; default nowait.\n"
//      "   --multi             Multisocket mode.\n"
//      "   --ssl               ssl mode.\n"
//      "   --cert <cert>       server certificate.\n"
        );
    exit(3);
}


static int
process(SOCKET socket)
{
    ws_service server;

    //
    //  echo endpoint
    auto &echo = server.endpoint["^/echo/?$"];

    echo.on_open = [](std::shared_ptr<ws_service::Connection> connection) {
        std::cout << "Server: Opened connection " << connection.get() << std::endl;
    };

    // See RFC 6455 7.4.1. for status codes
    echo.on_close = [](std::shared_ptr<ws_service::Connection> connection, int status, const std::string & /*reason*/) {
        std::cout << "Server: Closed connection " << connection.get() << " with status code " << status << std::endl;
    };

    // Can modify handshake response headers here if needed
    echo.on_handshake = [](std::shared_ptr<ws_service::Connection> /*connection*/, SimpleWeb::CaseInsensitiveMultimap & /*response_header*/) {
        return SimpleWeb::StatusCode::information_switching_protocols; // Upgrade to websocket
    };

    // See http://www.boost.org/doc/libs/1_55_0/doc/html/boost_asio/reference.html, Error Codes for error code meanings
    echo.on_error = [](std::shared_ptr<ws_service::Connection> connection, const SimpleWeb::error_code &ec) {
        std::cout << "Server: Error in connection " << connection.get() << ". "
             << "Error: " << ec << ", error message: " << ec.message() << std::endl;
    };

    echo.on_message = [](std::shared_ptr<ws_service::Connection> connection, std::shared_ptr<ws_service::InMessage> in_message) {
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
    server.start(socket);
    std::cerr << socket << ": Connection complete" << std::endl;

    return 0;
}

//end
