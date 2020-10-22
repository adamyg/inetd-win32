[![Build status](https://ci.appveyor.com/api/projects/status/6fq5on94pp3i87kj?svg=true&passingText=MSVC%20Passing&failingText=MSVC%20Failing&pendingText=MSVC%20Pending)](https://ci.appveyor.com/project/adamyg/inetd-win32-msvc/)

# inetd-win32

inetd for windows - inetd is a super-server daemon on many Unix systems that provides Internet services. For each configured service, it listens for requests from connecting clients. Requests are served by spawning a process which runs the appropriate executable, but simple services such as echo are served internally.

This service implements a similar framework under windows.

# Interface

Unlike UNIX, a process cannot simply pass socket handles by associating them with standard input/output of a child process. The method available utilises the `WSADuplicateSocket` function to enable socket sharing between processes. A source process calls `WSADuplicateSocket` to obtain a `WSAPROTOCOL_INFO` structure. We then use a pipe for interprocess communications to pass the contents of this structure to a target process, which in turn uses it in a call to `WSASocket` to obtain a descriptor for the duplicated socket.

In order to execute within the inetd framework applications needs to bind to a named interface and import sockets from the parent. 
Sample code below utilises the `inetd::SocketShare::GetSocket` function to recieve a socket from its parent.

```c++

#include <libinetd/GetOpt.h>
#include <libinetd/SocketShare.h>

static const char *short_options = "i:";
static struct inetd::GetOpt::Option long_options[] = {
    { "usage", inetd::GetOpt::argument_none, NULL, 1000 },
    { NULL }
};

extern void usage(const char *fmt = NULL, ...); /*no-return*/
extern int  process(SOCKET socket);

int
main(int argc, const char **argv)
{
    inetd::GetOpt options(short_options, long_options, argv[0]);
    const char *basename = NULL;

    while (-1 != options.shift(argc, argv)) {
        switch (options.optret()) {
        case 'i':
            basename = options.optarg();
            break;
        case 1000:
            usage();
        default:
            return 3;
        }
    }

    if (NULL == basename) {
        usage("missing interface specification");
    }

    argv += options.optind();
    if (0 != (argc -= options.optind())) {
        usage("unexpected arguments");
    }

    SOCKET socket = inetd::SocketShare::GetSocket(basename);
    if (INVALID_SOCKET != socket) {
        return process(socket);
    }
    std::abort();
}

```
