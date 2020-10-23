//
//  HTTP Service example,
//  utilising https://gitlab.com/eidheim/Simple-Web-Server
//

#if defined(_MSC_VER)
#define _SCL_SECURE_NO_WARNINGS 1
#define __STDC_LIMIT_MACROS 1
#pragma warning(disable:4244) // 'xxx': conversion from 'xxx' to 'xxx', possible loss of data
#pragma warning(disable:4503) // 'xxx': decorated name length exceeded, name was truncated)
#endif

#include "../libinetd/ServiceGetOpt.h"
#include "../libinetd/SocketShare.h"
#include "http_service.h"

#define PROGNAME    "http_service"

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
    http_service server;

    //
    //  GET-example for the path /info
    //  Responds with request-information
    server.resource["^/info$"]["GET"] = [](std::shared_ptr<http_service::Response> response, std::shared_ptr<http_service::Request> request) {
        std::stringstream stream;

        stream << "<h1>Request from " << request->remote_endpoint().address().to_string() << ":" << request->remote_endpoint().port() << "</h1>";
        stream << request->method << " " << request->path << " HTTP/" << request->http_version;
        stream << "<h2>Query Fields</h2>";
        auto query_fields = request->parse_query_string();
        for(auto &field : query_fields) {
            stream << field.first << ": " << field.second << "<br>";
        }
        stream << "<h2>Header Fields</h2>";
        for(auto &field : request->header) {
            stream << field.first << ": " << field.second << "<br>";
        }
        response->write(stream);
    };

    //
    //  Default GET
    server.default_resource["GET"] = [](std::shared_ptr<http_service::Response> response, std::shared_ptr<http_service::Request> request) {
        response->write(SimpleWeb::StatusCode::client_error_not_found,
            "<!DOCTYPE html>\n"
            "<html><head><style>\n"
            "*{\n"
                " transition: all 0.6s;\n"
            "}\n"
            "html {\n"
                " height: 100%;\n"
            "}\n"
            "body{\n"
                " font-family: 'Lato', sans-serif; color: #888; margin: 0;\n"
            "}\n"
            "#main{\n"
                " display: table; width: 100%; height: 100vh; text-align: center;\n"
            "}\n"
            ".fof{\n"
                " display: table-cell; vertical-align: middle;\n"
            "}\n"
            ".fof h1{\n"
                " font-size: 50px; display: inline-block; padding-right: 12px; animation: type .5s alternate infinite;\n"
            "}\n"
            "@keyframes type{\n"
                " from{box-shadow: inset -3px 0px 0px #888;}\n"
                " to{box-shadow: inset -3px 0px 0px transparent;}\n"
            "}\n"
            "</style></head>\n"
            "<body>"
                "<div id=\"main\">"
                    "<div class=\"fof\">"
                        "<h1>Error 404</h1>"
                    "</div>"
                "</div>"
            "</body>\n"
            "</html>\n"
            );
    };

    std::cerr << socket << ": Connection open" << std::endl;
    server.start(socket);
    std::cerr << socket << ": Connection complete" << std::endl;

    return 0;
}

//end
