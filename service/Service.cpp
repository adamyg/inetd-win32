#include <edidentifier.h>
__CIDENT_RCSID(Service_cpp,"$Id: Service.cpp,v 1.9 2022/03/25 17:04:05 cvsuser Exp $")

/* -*- mode: c; indent-width: 8; -*- */
/*
 * inetd service adapter
 *
 * Copyright (c) 2020 - 2022, Adam Young.
 * All rights reserved.
 *
 * This file is part of inetd-win32.
 *
 * The applications are free software: you can redistribute it
 * and/or modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 3.
 *
 * Redistributions of source code must retain the above copyright
 * notice, and must be distributed with the license document above.
 *
 * Redistributions in binary form must reproduce the above copyright
 * notice, and must include the license document above in
 * the documentation and/or other materials provided with the
 * distribution.
 *
 * This project is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * license for more details.
 * ==end==
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <time.h>
#include <fcntl.h>
#include <io.h>

#include <cstring>

#include "Service.h"                            // public header
#include "ServiceDiags.h"
#include "LoggerSyslog.h"
#include "Arguments.h"

#include "../libinetd/libinetd.h"
#include <buildinfo.h>


/////////////////////////////////////////////////////////////////////////////////////////
//  Pipe endpoint

#define OPENMODE        PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED
#define PIPEMODE        PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS
#define PIPEINSTANCES   PIPE_UNLIMITED_INSTANCES
#define PIPESIZE        (8 * 1024)

struct Service::PipeEndpoint {
    enum pipe_state { EP_CREATED, EP_CONNECT, EP_CONNECT_ERROR, EP_READY, EP_READING, EP_READ };

    PipeEndpoint(HANDLE _ioevent, HANDLE _handle, DWORD _size) :
            ioevent(_ioevent), handle(_handle), size(_size), state(EP_CREATED), avail(_size), cursor(buffer)
    {
    }

    void reset()
    {
        cursor = buffer, avail = size;
    }

    void pushed(DWORD bytes)
    {
        assert(bytes <= avail);
        avail -= bytes; cursor += bytes;
        cursor[0] = 0;
    }

    void popped(DWORD bytes)
    {
        const DWORD t_length = length();
        assert(bytes <= t_length);
        if (bytes == t_length) {                // empty?
            reset();
        } else {                                // remove leading bytes.
            (void) memmove(buffer, buffer + bytes, t_length - bytes);
            avail += bytes; cursor -= bytes;
        }
    }

    DWORD length() const
    {
        assert(avail <= size);
        return size - avail;
    }

    void CompletionSetup()
    {
        DWORD lasterr;
        if (PipeEndpoint::EP_CREATED == state || PipeEndpoint::EP_CONNECT_ERROR == state) {
            overlapped.hEvent = ioevent;
            if (! ::ConnectNamedPipe(handle, &overlapped)) {
                if ((lasterr = GetLastError()) == ERROR_PIPE_CONNECTED) {
                    state = PipeEndpoint::EP_READY;
                } else if (ERROR_IO_PENDING == lasterr) {
                    state = PipeEndpoint::EP_CONNECT;
                } else {
                    state = PipeEndpoint::EP_CONNECT_ERROR;
                    assert(false);
                }
            } else {
                state = PipeEndpoint::EP_CONNECT_ERROR;
                assert(false);
            }
        }

        if (PipeEndpoint::EP_READY == state) {
            overlapped.hEvent = ioevent;
            if (! ::ReadFile(handle, cursor, avail, NULL, &overlapped)) {
                assert(ERROR_IO_PENDING == (lasterr = GetLastError()));
                state = PipeEndpoint::EP_READING;
            } else {
                state = PipeEndpoint::EP_READING;
                ::SetEvent(ioevent);
            }
        }
    }

    bool CompletionResults(DWORD &dwRead) 
    {
        return (0 != ::GetOverlappedResult(handle, &overlapped, &dwRead, FALSE));
    }

    const HANDLE    ioevent;
    const HANDLE    handle;
    const DWORD     size;
    enum pipe_state state;
    OVERLAPPED      overlapped;
    DWORD           avail;                      // available bytes.
    char *          cursor;                     // current read cursor.
    char            buffer[1];                  // underlying buffer, of 'size' bytes.
        // ... data ....
};


DWORD
Service::NewPipeEndpoint(const char *pipe_name, PipeEndpoint *&result)
{
    HANDLE ioevent = ::CreateEventA(NULL, TRUE, TRUE, NULL);

    if (INVALID_HANDLE_VALUE != ioevent) {
        HANDLE handle = ::CreateNamedPipeA(pipe_name,
                            OPENMODE, PIPEMODE, PIPEINSTANCES, PIPESIZE, PIPESIZE, 0, NULL);

        if (INVALID_HANDLE_VALUE != handle) {
            struct PipeEndpoint *endpoint;
            if (NULL != (endpoint =
                    (struct PipeEndpoint *)calloc(sizeof(*endpoint) + PIPESIZE + 8 /*nul*/, 1))) {
                new (endpoint) PipeEndpoint(ioevent, handle, PIPESIZE);
                result = endpoint;
                return 0;
            }
            return ERROR_NOT_ENOUGH_MEMORY;
        }
        ::CloseHandle(ioevent);
    }
    return GetLastError();
}


/////////////////////////////////////////////////////////////////////////////////////////
//  Service framework

Service::Service(const char *svcname, bool console_mode) :
        CNTService(svcname, NTService::StdioDiagnosticsIO::Get()),
            options_(), config_(),
            logger_stop_event_(0), logger_thread_(0),
            server_stopped_event_(0), server_thread_(0)
{
    SetVersion(WININETD_VERSION_1, WININETD_VERSION_2, WININETD_VERSION_3);
    SetDescription(WININETD_PACKAGE_NAME);
    SetConsoleMode(console_mode);
}


Service::~Service() 
{
}


void
Service::Start(const struct Options &options)
{
    SetDiagnostics(ServiceDiags::Get(logger_));
    options_ = options;

    if (options_.conf.empty()) {                // system, otherwise default.
        if (! ConfigGet("conf", options_.conf)) {
            options_.conf.assign("./inetd.conf");
        }
    }
    options_.conf = ResolveRelative(options.conf.c_str());

    if (! CNTService::GetConsoleMode()) {
        options_.logger = true;                 // implied.
    }

    LoggerSyslog::attach(&logger_);             // attach logger to syslog

    CNTService::Start();
}


bool
Service::ConfigLogger()
{
    Logger::Profile profile;
    char szValue[1024];
    int ret;

    // TODO: logger section [logger.service|port]

    ConfigGet("logger/path", szValue, sizeof(szValue));
    ConfigGet("logger/age", szValue, sizeof(szValue));

    ret = ConfigGet("logger_path", szValue, sizeof(szValue));
    profile.base_path(ResolveRelative(ret ? szValue : "./logs/inetd_service.log"));

    if (ConfigGet("logger_age", szValue, sizeof(szValue)))
        profile.time_period(szValue);

    if (ConfigGet("logger_size", szValue, sizeof(szValue)))
        profile.size_limit(szValue);

    if (ConfigGet("logger_lines", szValue, sizeof(szValue)))
        profile.line_limit(szValue);

    if (ConfigGet("logger_purge", szValue, sizeof(szValue)))
        profile.purge_period(szValue);

    if (! logger_.start(profile)) {
        CNTService::LogError(true, "unable to initialise logger <%s>", profile.base_path());
        return false;
    }
    return true;
}


//virtual
bool
Service::OnInit()
{
    if (options_.delay_start) {
        ::Sleep(30 * 1000);                     // debugger/start delay.
    }

    if (! ConfigOpen() || ! ConfigLogger()) {
        return false;                           // fatal
    }

    return CNTService::OnInit();
}


//virtual
void
Service::OnStop()
{
    CNTService::OnStop();
}


//virtual
void
Service::ServiceRun()
{
    if (Initialise() >= 0) {
        HANDLE handles[2];

        handles[0] = StopEvent();
        handles[1] = server_stopped_event_;

        const DWORD dwWait =
                WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        switch (dwWait) {
        case WAIT_OBJECT_0 + 0:                 // service shutdown.
            diags().info("service shutdown");
            break;
        case WAIT_OBJECT_0 + 1:                 // inetd exit.
            diags().info("inetd exit");
            break;
        default:
            diags().fwarning("error waiting on stop event: %u", dwWait);
            break;
        }
        Shutdown();
    }
}


int
Service::Initialise()
{
    if (-1 == w32_sockinit()) {
        diags().error("winsock initialisation: %m");
        return -1;
    }

    FILE *stderr_stream = NULL, *stdout_stream = NULL;
    void *arguments[2] = {0};

    snprintf(pipe_name_, sizeof(pipe_name_)-1,  // stderr/stdout sink
        "\\\\.\\pipe\\inetd_service_stdlog.%u", (unsigned)(::GetCurrentProcessId() * ::GetTickCount()));

    if (options_.daemon_mode) {
        if (HWND console = GetConsoleWindow()) {
            ShowWindow(console, SW_HIDE /*SW_MINIMIZE*/);
          //FreeConsole();                      // detach from console.
                // unfortunately FreeConsole() unconditionally closes any associated handles, resulting in
                // invalid handle exceptions during freopen()'s; there is no portable work-around.
        }
    }

    if (options_.logger) {
        PipeEndpoint *endpoint = 0;

        if (unsigned ret = NewPipeEndpoint(pipe_name_, endpoint)) {
            diags().ferror("unable to open redirect pipe <%s>: %u", pipe_name_, ret);
            return -1;
        }

        arguments[0] = this;
        arguments[1] = endpoint;

        logger_stop_event_ = ::CreateEventA(NULL, TRUE, FALSE, NULL);
        logger_thread_ = ::CreateThread(NULL, 0, logger_thread, (void *)arguments, 0, NULL);

        if (NULL == logger_stop_event_ || NULL == logger_thread_ ||
                0 == ::WaitNamedPipeA(pipe_name_, 30 * 1000)) {
            diags().ferror("unable to create logger thread: %M");
            if (logger_stop_event_) {
                ::CloseHandle(logger_stop_event_);
            }
            return -1;
        }
        ::Sleep(200);

        if (NULL == (stderr_stream = freopen(pipe_name_, "wb", stderr)) ||
                -1 == setvbuf(stderr, NULL, _IOFBF, 1024)) {
            diags().ferror("unable to open redirect stderr <%s>: %m", pipe_name_);
            return -1;
        }

        if (::WaitNamedPipeA(pipe_name_, 30 * 1000)) {
            if (NULL == (stdout_stream = freopen(pipe_name_, "wb", stdout)) ||
                    -1 == setvbuf(stdout, NULL, _IOFBF, 1024)) {
                diags().ferror("unable to open redirect stdout <%s>: %m", pipe_name_);
                return -1;
            }
        }
    }

    server_stopped_event_ = ::CreateEventA(NULL, TRUE, FALSE, NULL);
    server_thread_ = ::CreateThread(NULL, 0, server_thread, (void *)this, 0, NULL);
    if (NULL == logger_stop_event_ || NULL == server_thread_) {
        diags().ferror("unable to create server thread: %M");
        return -1;
    }
    return 0;
}


void
Service::Shutdown()
{
    // shutdown inetd

    if (server_thread_) {
        inetd_signal_stop(1);
        if (WAIT_OBJECT_0 != ::WaitForSingleObject(server_stopped_event_, 30 * 1000)) {
            diags().warning("unable to shutdown inetd loop");
        }
        ::CloseHandle(server_thread_);
        server_thread_ = 0;
    }

    // shutdown stdout/stderr redirection

    if (logger_thread_) {
        ::SetEvent(logger_stop_event_);
        if (WAIT_OBJECT_0 != ::WaitForSingleObject(logger_thread_, 30 * 1000)) {
            diags().warning("unable to shutdown logger");
        }
        ::CloseHandle(logger_thread_);
        logger_thread_ = 0;
    }
}


DWORD WINAPI
Service::server_thread(LPVOID lpParam)
{
    Service *self = (Service *)lpParam;
    self->service_body();
    return 0;
}


DWORD WINAPI
Service::logger_thread(LPVOID lpParam)
{
    void **arguments = (void **) lpParam;
    Service *self = (Service *)arguments[0];

    self->logger_body((PipeEndpoint *)arguments[1]);
    return 0;
}


void
Service::service_body()
{
    std::vector<std::string> args;

    args.push_back(options_.arg0 ? options_.arg0 : ServiceName());

    // configuration

    if (! options_.ignore) {

        // build argument list

        if (! config_.empty()) {
            const SimpleConfig::elements_t *elements = 0;
            char section[256] = {0};
                                                // service specific.
            (void) snprintf(section, sizeof(section) - 1, "options.%s", ServiceName());
            if (0 == (elements = config_.GetSectionElements(section))) {

                if (0 == elements) {            // default.
                    elements = config_.GetSectionElements("options");
                }
            }

            if (elements) {
                char arg[1024] = {0};
                for (SimpleConfig::elements_t::const_iterator it(elements->begin()), end(elements->end()); it != end; ++it) {
                    const char *parameters = it->first.c_str();
                    if (! it->second.empty()) {
                        args.push_back(it->first);
                        parameters = it->second.c_str();
                    }
                    Arguments::split(args, parameters, true);
                }
            }

        } else {
            char options[1024];
            if (ConfigGet("options", options, sizeof(options))) {
                Arguments::emplace_split(args, options, true);
            }
        }
    }

    // command line

    for (const char **argv = options_.argv, **endv = argv + options_.argc;
                argv && argv < endv; ++argv) {
        args.push_back(*argv);
    }

    // inetd main

    Arguments arguments(args);
    const int ret = inetd_main(arguments.argc(), arguments.argv());

    if (ret) {
        diags().fwarning("inetd exited with : %d", ret);
    }

    fflush(stdout), fflush(stderr);

    // signal termination

    ::SetEvent(server_stopped_event_);
}


/////////////////////////////////////////////////////////////////////////////////////////
//  Diagnostics

void
Service::logger_body(PipeEndpoint *endpoint)
{
    HANDLE hStdout = INVALID_HANDLE_VALUE;
    PipeEndpoint *endpoints[8] = {0};
    HANDLE handles[1 + 8] = {0};
    bool terminate = false;
    DWORD inst = 0;

    handles[0] = logger_stop_event_;            // exit event

    endpoints[inst] = endpoint;                 // endpoint
    handles[++inst] = endpoint->ioevent;        // completion event

    if (options_.console_output) {
        hStdout = ::CreateFileA("CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, NULL);
    }

    for (;;) {
        if (endpoint) {
            endpoint->CompletionSetup();
            endpoint = 0;
        }

        const DWORD dwWait =                    // XXX: client limit 63
                ::WaitForMultipleObjects(inst + 1, handles, FALSE, terminate ? 100 : INFINITE);

#define WAIT_OBJECT_1       (WAIT_OBJECT_0 + 1)
#define WAIT_ABANDONED_1    (WAIT_ABANDONED_0 + 1)

        if (dwWait >= WAIT_OBJECT_1 && dwWait < (WAIT_OBJECT_1 + inst)) {
            const unsigned idx = dwWait - WAIT_OBJECT_1;
            DWORD dwRead = 0;

            endpoint = endpoints[idx];
            assert(handles[idx + 1] == endpoint->ioevent);

            if (endpoint->CompletionResults(dwRead)) {
                switch (endpoint->state) {
                case PipeEndpoint::EP_CONNECT:

                    // Connected

                    endpoint->state = PipeEndpoint::EP_READY;
                    endpoint->CompletionSetup();

                    // Create new endpoint

                    if (inst < _countof(endpoints)) {
                        if (0 == NewPipeEndpoint(pipe_name_, endpoint)) {
                            endpoints[inst] = endpoint;
                            handles[++inst] = endpoint->ioevent;
                        }
                    }
                    break;

                case PipeEndpoint::EP_READING:
                    if (dwRead) {
                        const char *scan = endpoint->cursor;
                        DWORD dwPopped = 0;     // note: first scan need only be against additional characters.

                        endpoint->pushed(dwRead);
                        for (const char *line = endpoint->buffer, *nl;
                                    NULL != (nl = strchr(scan, '\n')); scan = line = nl) {
                            unsigned sz = nl - line;

                            if (sz) {
                                unsigned t_sz = sz;

                                if ('\r' == line[t_sz-1]) --t_sz; // \r\n
                                if (t_sz) {
                                    ServiceDiags::LoggerAdapter::push(logger_, ServiceDiags::LoggerAdapter::LLNONE, line, t_sz);
                                    if (INVALID_HANDLE_VALUE != hStdout) {
                                        if (! ::WriteConsoleA(hStdout, line, sz + 1, NULL, NULL)) {
                                            ::CloseHandle(hStdout);
                                            hStdout = INVALID_HANDLE_VALUE;
                                                // TODO: move into logger, as console may block.
                                        }
                                    }
                                }
                            }

                            ++nl, ++sz;         // consume newline
                            if ('\r' == *nl) {  // \n\r
                                ++nl, ++sz;     // consume optional return
                            }

                            dwPopped += sz;
                        }

                        if (dwPopped) {         // pop consumed characters
                            endpoint->popped(dwPopped);
                        } else if (0 == endpoint->avail) {
                                                // otherwise on buffer full; flush
                            ServiceDiags::LoggerAdapter::push(logger_, ServiceDiags::LoggerAdapter::LLNONE, endpoint->buffer, endpoint->size);
                            if (INVALID_HANDLE_VALUE != hStdout) {
                                ::WriteConsoleA(hStdout, endpoint->buffer, endpoint->size, NULL, NULL);
                            }
                            endpoint->reset();
                        }

                        endpoint->state = PipeEndpoint::EP_READY;
                    }
                    break;

                default:
                    diags().ferror("unexpected endpoint state : %u", (unsigned)endpoint->state);
                    assert(false);
                    break;
                }

            } else {
                DWORD dwError = GetLastError();
                diags().ferror("unexpected io completion : %u", (unsigned)dwError);
                assert(false);
            }

        } else if (dwWait == WAIT_OBJECT_0) {
            ::ResetEvent(logger_stop_event_);
            terminate = true;                   // drain stream, then exit

        } else if (dwWait >= WAIT_ABANDONED_1 && dwWait < (WAIT_ABANDONED_1 + inst)) {
            const unsigned idx = dwWait - WAIT_ABANDONED_1;

            endpoint = endpoints[idx];
            assert(handles[idx + 1] == endpoint->ioevent);

        } else if (dwWait == WAIT_TIMEOUT) {
            if (terminate) {
                break;                          // exit
            }

        } else {
            // WAIT_IO_COMPLETION
            // WAIT_FAILED
            const DWORD dwError = GetLastError();
            diags().ferror("unexpected wait completion : %u", (unsigned)dwError);
            assert(false);
            break;                              // exit
        }
    }

    if (INVALID_HANDLE_VALUE != hStdout) {
        ::CloseHandle(hStdout);
    }
}


//virtual
void Service::ServiceTrace(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    diags().vinfo(fmt, ap);
    va_end(ap);
}


/////////////////////////////////////////////////////////////////////////////////////////
//  Configuration
//  Note: Move into NTServiceIni, allow a service to inherit.

std::string
Service::ResolveRelative(const char *path)
{
    // The working directory for services is always 'X:\WINDOWS\<SysWOW64>\system32',
    // hence resolve the path of a relative file against the application path not cwd.
    char t_szAppPath[_MAX_PATH] = {0};
    const char *basename = NULL;

    if ('.' == path[0] && ('/' == path[1] || '\\' == path[1])) {
        if (0 == _access(path, 0)) {
            return path;                        // exists within CWD.
        }
        basename = path + 2;                    // './xxxx' or '.\xxxx'

    } else if (NULL == strchr(path, '/') && NULL == strchr(path, '\\')) {
        basename = path;                        // xxxx
    }

    if (basename) {
        int len = (int)::GetModuleFileNameA(NULL, t_szAppPath, sizeof(t_szAppPath));

        const char *d1 = strrchr(t_szAppPath, '/'), *d2 = strrchr(t_szAppPath, '\\'),
            *d = (d1 > d2 ? d1 : d2);           // last delimiter
        len = (d ? (d - t_szAppPath) : len);

        snprintf(t_szAppPath + len, sizeof(t_szAppPath) - len, "\\%s", basename);
        t_szAppPath[sizeof(t_szAppPath) - 1] = 0;
        path = t_szAppPath;
    }

    return std::string(path);
}


//virtual
bool
Service::ConfigOpen(bool create /*= true*/)
{
    if (! config_.empty()) {
        return true;
    }

    if (! options_.conf.empty()) {
        std::string errmsg, path(ResolveRelative(options_.conf.c_str()));

        if (config_.Load(path, errmsg)) {       // success
            return true;
        }

        CNTService::LogError(true, "unable to open configuration <%s>: %s", path.c_str(), errmsg.c_str());
        diags().ferror("unable to open configuration <%s>: %s", path.c_str(), errmsg.c_str());
        return false;
    }
    return CNTService::ConfigOpen();
}


//virtual
void
Service::ConfigClose()
{
    config_.clear();
    CNTService::ConfigClose();
}


//virtual
bool
Service::ConfigSet(const char *csKey, const char *szValue)
{
    if (! config_.empty()) {
        const char *sep;
        if (NULL != (sep = strchr(csKey, '\\'))) { //TODO: sections
            assert(sep);
            return false;
        }
    }

    return CNTService::ConfigSet(csKey, szValue);
}


//virtual
bool
Service::ConfigSet(const char *csKey, DWORD dwValue)
{
    if (! config_.empty()) {
        const char *sep;
        if (NULL != (sep = strchr(csKey, '\\'))) {
            //TODO: sections
            assert(sep);
            return false;
        }
    }
    return CNTService::ConfigSet(csKey, dwValue);
}


int
Service::ConfigGet(const char *csKey, std::string &buffer, unsigned flags)
{
    if (! config_.empty()) {
        const char *sep;

        assert(csKey);
        if (!csKey || !*csKey)
            return false;

        const std::string *value;
        if (NULL != (sep = std::strpbrk(csKey, "\\/"))) {
            value = config_.GetValuePtr(SimpleConfig::string_view(csKey, sep - csKey), SimpleConfig::string_view(sep + 1));
        } else {
            value = config_.GetValuePtr("", csKey);
        }

        if (value) {
            buffer = *value;
            return true;
        }

        if (CFG_WARN & flags) {
            diags().fwarning("parameter <%s> does not exist", csKey);
        }

        return false;
    }

    char szValue[1024];
    if (CNTService::ConfigGet(csKey, szValue, sizeof(szValue), flags)) {
        buffer = szValue;
        return false;
    }

    return true;
}


//virtual
int
Service::ConfigGet(const char *csKey, char *szBuffer, size_t dwSize, unsigned flags)
{
    if (! config_.empty()) {
        const char *sep;

        assert(csKey);
        assert(szBuffer && dwSize);
        if (!csKey || !*csKey)
            return false;

        if (!szBuffer || !dwSize)
            return false;

        const std::string *value;
        if (NULL != (sep = std::strpbrk(csKey, "\\/"))) {
            value = config_.GetValuePtr(SimpleConfig::string_view(csKey, sep - csKey), SimpleConfig::string_view(sep + 1));
        } else {
            value = config_.GetValuePtr("", csKey);
        }

        if (value) {
            const size_t len = value->length();
            if (len < dwSize) {
                memcpy(szBuffer, value->c_str(), len + 1 /*null*/);
                dwSize = len;
                return true;
            }

            diags().fwarning("parameter <%s> too large", csKey);
            return false;
        }

        if (CFG_WARN & flags) {
            diags().fwarning("parameter <%s> does not exist", csKey);
        }

        return false;
    }

    return CNTService::ConfigGet(csKey, szBuffer, dwSize, flags);
}


//virtual
bool
Service::ConfigGet(const char *csKey, DWORD &dwValue, unsigned flags)
{
    if (! config_.empty()) {
        const char *sep;

        assert(csKey);
        if (!csKey || !*csKey)
            return false;

        const std::string *value;
        if (NULL != (sep = std::strpbrk(csKey, "\\/"))) {
            value = config_.GetValuePtr(SimpleConfig::string_view(csKey, sep - csKey), SimpleConfig::string_view(sep + 1));
        } else {
            value = config_.GetValuePtr("", csKey);
        }

        if (value) {
            char *end = 0;
            unsigned long ret = strtoul(value->c_str(), &end, 10);
            if (end && 0 == *end) {
                dwValue = (DWORD)ret;
                return true;
            }

            diags().fwarning("parameter <%s> should be a numeric", csKey);
            return false;
        }

        if (CFG_WARN & flags) {
            diags().fwarning("parameter <%s> does not exist", csKey);
        }

        return false;
    }

    return CNTService::ConfigGet(csKey, dwValue, flags);
}

//end