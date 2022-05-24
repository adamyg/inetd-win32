#include <edidentifier.h>
__CIDENT_RCSID(Service_cpp,"$Id: Service.cpp,v 1.12 2022/05/24 03:44:38 cvsuser Exp $")

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

#include <vector>
#include <algorithm>
#include <cstring>

#include "Service.h"                            // public header
#include "ServiceDiags.h"
#include "LoggerSyslog.h"
#include "Arguments.h"

#include <buildinfo.h>


/////////////////////////////////////////////////////////////////////////////////////////
//  Pipe endpoint

#define OPENMODE        PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED
#define PIPEMODE        PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS
#define PIPEINSTANCES   PIPE_UNLIMITED_INSTANCES
#define PIPESIZE        (8 * 1024)

struct Service::PipeEndpoint {
        enum pipe_state { EP_CREATED, EP_CONNECT, EP_CONNECT_ERROR, EP_READY, EP_READING, EP_EOF };

        PipeEndpoint(HANDLE _ioevent, HANDLE _handle, DWORD _size) :
                iopending(FALSE),
                ioevent(_ioevent), handle(_handle),
                size(_size), state(EP_CREATED),
                log_level_(ServiceDiags::Adapter::LLTRACE),
                avail(_size), cursor(buffer)
        {
                memset(&overlapped, 0, sizeof(overlapped));
        }

        void log_level(ServiceDiags::Adapter::loglevel ll)
        {
                log_level_ = ll;
        }

        ServiceDiags::Adapter::loglevel log_level() const
        {
                return log_level_;
        }

        void reset()
        {
                cursor = buffer, avail = size;
        }

        char *pushed(bool &eof)
        {
                char *ocursor = cursor;         // current read cursor.
                if (result || cursor != buffer) {
                        eof = true;
                        if (result) {           // new data.
                                assert(result <= avail);
                                avail -= result; cursor += result;
                                result = 0;
                                eof = false;
                        }
                        cursor[0] = 0;
                        return ocursor;
                }
                return NULL;
        }

        void pop(DWORD bytes)
        {
                const DWORD t_length = length();
                assert(bytes <= t_length);
                if (bytes == t_length) {        // empty?
                        reset();
                } else {                        // remove leading bytes.
                        (void) memmove(buffer, buffer + bytes, t_length - bytes);
                        avail += bytes; cursor -= bytes;
                }
        }

        DWORD length() const
        {
                assert(avail <= size);
                return size - avail;
        }

        DWORD data() const
        {
                return (cursor - buffer);
        }

        bool CompletionSetup()
        {
                assert(FALSE == iopending);

                switch (state) {
                case PipeEndpoint::EP_CREATED:
                case PipeEndpoint::EP_CONNECT_ERROR:
                        overlapped.hEvent = ioevent;
                        if (! ::ConnectNamedPipe(handle, &overlapped)) {
                                const DWORD err = GetLastError();
                                if (ERROR_PIPE_CONNECTED == err) {
                                        state = PipeEndpoint::EP_READY;
                                } else if (ERROR_IO_PENDING == err) {
                                        state = PipeEndpoint::EP_CONNECT;
                                        iopending = TRUE;
                                } else {
                                        state = PipeEndpoint::EP_CONNECT_ERROR;
                                        assert(false);
                                }
                        } else {
                                state = PipeEndpoint::EP_CONNECT_ERROR;
                                assert(false);
                        }
                        break;

                case PipeEndpoint::EP_READY:
                        assert(overlapped.hEvent == ioevent);
                        if (! ::ReadFile(handle, cursor, avail, &result, &overlapped)) {
                                const DWORD err = GetLastError();
                                if (ERROR_IO_PENDING == err) { // pending
                                        state = PipeEndpoint::EP_READING;
                                        iopending = TRUE;
                                } else if (ERROR_MORE_DATA == err) { // partial message
                                        state = PipeEndpoint::EP_READING;
                                        ::SetEvent(ioevent);
                                } else if (ERROR_BROKEN_PIPE == err || ERROR_HANDLE_EOF == err ||
                                                ERROR_OPERATION_ABORTED == err) {
                                        state = PipeEndpoint::EP_EOF;
                                        return false;
                                } else {
                                        assert(false);
                                        return false;
                                }
                        } else {
                                state = PipeEndpoint::EP_READING;
                                ::SetEvent(ioevent);
                        }
                        break;

                case PipeEndpoint::EP_EOF:
                        return false;

                default:
                        assert(false);
                        break;
                }
                return true;
        }

        int CompletionResults()
        {
                if (! iopending ||
                            ::GetOverlappedResult(handle, &overlapped, &result, FALSE)) {
                        iopending = FALSE;
                        return 0;
                }

                const DWORD err = GetLastError();
                if (ERROR_IO_INCOMPLETE == err) {
                        return -1;
                } else if (ERROR_BROKEN_PIPE == err || ERROR_HANDLE_EOF == err ||
                                ERROR_OPERATION_ABORTED == err) {
                        if (PipeEndpoint::EP_CONNECT == state) {
                                state = PipeEndpoint::EP_CONNECT_ERROR;
                        } else {
                                state = PipeEndpoint::EP_EOF;
                        }
                        iopending = FALSE;
                        return -1;
                }
                return (int)err; // error
        }

private:
        OVERLAPPED      overlapped;
        BOOL            iopending;
        DWORD           result;

public:
        const HANDLE    ioevent;
        const HANDLE    handle;
        const DWORD     size;
        enum pipe_state state;
        ServiceDiags::Adapter::loglevel log_level_;
        DWORD           avail;                  // available bytes.
        char *          cursor;                 // current read cursor.
        char            buffer[1];              // underlying buffer, of 'size' bytes.
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

Service::Service(const char *appname, const char *svcname, bool console_mode) :
        CNTService(svcname, NTService::StdioDiagnosticsIO::Get()),
            options_(),
            config_(),
            configopen_(false),
            logger_stop_event_(0),
            logger_thread_(0),
            server_stopped_event_(0),
            server_thread_(0)
{
        strncpy(appname_, appname, sizeof(appname_) - 1);
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

        if (options_.conf.empty()) {            // system, otherwise default.
                if (! ConfigGet("conf", options_.conf)) {
                        char defconf[MAX_PATH] ={0};

                        _snprintf(defconf, sizeof(defconf) - 1, "./%s.conf", appname_);
                        options_.conf.assign(defconf);
                }
        }
        options_.conf = ResolveRelative(options_.conf.c_str());

        if (! CNTService::GetConsoleMode()) {
                options_.logger = true;         // implied.
        }

        ServiceDiags::Syslog::attach(logger_);  // attach logger to syslog.

        CNTService::Start();
}


bool
Service::ConfigLogger()
{
        Logger::Profile profile;
        char szValue[1024], deflog[MAX_PATH] = {0};
        int ret;

        _snprintf(deflog, sizeof(deflog) - 1, "./logs/%s_service.log", appname_);
        ret = ConfigGet("logger_path", szValue, sizeof(szValue));
        profile.base_path(ResolveRelative(ret ? szValue : deflog));

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
                ::Sleep(30 * 1000);             // debugger/start delay.
        }

        if (! ConfigOpen() || ! ConfigLogger()) {
                return false;                   // fatal
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
                        case WAIT_OBJECT_0 + 0:
                        diags().info("service shutdown");
                        break;
                case WAIT_OBJECT_0 + 1:
                        diags().info("service exit");
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
        WSADATA wsaData = {0};
        if (WSAStartup(MAKEWORD(2, 2), &wsaData)) {
                diags().error("winsock initialisation: %m");
                return -1;
        }

        FILE *stderr_stream = NULL, *stdout_stream = NULL;
        void *arguments[2] = {0};

        snprintf(pipe_name_, sizeof(pipe_name_)-1, // stderr/stdout sink
                "\\\\.\\pipe\\%s_service_stdlog.%u", appname_, (unsigned)(::GetCurrentProcessId() * ::GetTickCount()));

        if (options_.daemon_mode) {
                if (HWND console = GetConsoleWindow()) {
                        ShowWindow(console, SW_HIDE /*SW_MINIMIZE*/);
                        //FreeConsole();        // detach from console.
                                // unfortunately FreeConsole() unconditionally closes any associated handles, resulting in
                                // invalid handle exceptions during freopen()'s; there is no portable work-around.
                }
        }

        if (options_.logger) {
                PipeEndpoint *endpoint = NULL;

                if (unsigned ret = NewPipeEndpoint(pipe_name_, endpoint)) {
                        diags().ferror("unable to open redirect pipe <%s>: %u", pipe_name_, ret);
                        return -1;
                }

                endpoint->log_level(ServiceDiags::Adapter::LLSTDERR);

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
        // shutdown service

        if (server_thread_) {

                if (options_.service_shutdown) {
                        options_.service_shutdown(1);
                }

                if (WAIT_OBJECT_0 != ::WaitForSingleObject(server_stopped_event_, 30 * 1000)) {
                        diags().warning("unable to shutdown service loop");
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

                        if (0 == elements) {    // default.
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

        // service main

        Arguments arguments(args);
        const int ret =
                (options_.service_main ? options_.service_main(arguments.argc(), (char **) arguments.argv()) : -1);

        if (ret) {
                diags().fwarning("%s exited with : %d", appname_, ret);
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
#define ENDPOINT_MAX  63
        std::vector<PipeEndpoint *> connections;
        PipeEndpoint *endpoints[ENDPOINT_MAX];
        HANDLE handles[64];
        unsigned pollidx = 0;
        bool terminate = false;

        connections.push_back(endpoint);

        if (options_.console_output) {
                hStdout = ::CreateFileA("CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, NULL);
        }

        for (;;) {
                // rearm

                if (endpoint) {
                        if (! endpoint->CompletionSetup()) {
                                std::vector<PipeEndpoint *>::iterator it = std::find(connections.begin(), connections.end(), endpoint);
                                if (it != connections.end()) {
                                        connections.erase(it);
                                        delete endpoint;
                                }
                        }
                        endpoint = NULL;
                }

                // build handles [round robin/limit handles]

                DWORD cnt = 0;

                handles[0] = logger_stop_event_;

                if (pollidx >= connections.size())
                        pollidx = 0; // reseed

                for (unsigned c = pollidx; c < connections.size() && cnt < ENDPOINT_MAX; ++c) {
                        endpoints[cnt] = connections[c];
                        handles[++cnt] = connections[c]->ioevent;
                }

                for (unsigned c = 0; c < pollidx && cnt < ENDPOINT_MAX; ++c) {
                        endpoints[cnt] = connections[c];
                        handles[++cnt] = connections[c]->ioevent;
                }

                assert(cnt == connections.size() || cnt == ENDPOINT_MAX);
                ++pollidx;

                // poll

                const DWORD dwWait =
                        ::WaitForMultipleObjects(cnt + 1, handles, FALSE, terminate ? 100 : INFINITE);

#define WAIT_OBJECT_1       (WAIT_OBJECT_0 + 1)
#define WAIT_ABANDONED_1    (WAIT_ABANDONED_0 + 1)

                if (dwWait >= WAIT_OBJECT_1 && dwWait < (WAIT_OBJECT_1 + cnt)) {
                        const unsigned idx = dwWait - WAIT_OBJECT_1;
                        bool eof = false;

                        endpoint = endpoints[idx];
                        assert(handles[idx + 1] == endpoint->ioevent);

                        const int res = endpoint->CompletionResults();
                        if (0 == res) {
                                switch (endpoint->state) {
                                case PipeEndpoint::EP_CONNECT:

                                        // Connected

                                        endpoint->state = PipeEndpoint::EP_READY;
                                        endpoint->CompletionSetup();

                                        // Create new endpoint

                                        if (0 == NewPipeEndpoint(pipe_name_, endpoint)) {
                                                connections.push_back(endpoint);
                                        }
                                        break;

                                case PipeEndpoint::EP_READING:
                                        if (const char *scan = endpoint->pushed(eof)) {
                                                DWORD dwPopped = 0; // note: first scan need only be against additional characters.

                                                for (const char *line = endpoint->buffer, *nl;
                                                        NULL != (nl = strchr(scan, '\n')); scan = line = nl) {
                                                        unsigned sz = nl - line;

                                                        if (sz) {
                                                                unsigned t_sz = sz;

                                                                if ('\r' == line[t_sz-1]) --t_sz; // \r\n
                                                                if (t_sz) {
                                                                        ServiceDiags::Adapter::push(logger_, endpoint->log_level(), line, t_sz);
                                                                        if (INVALID_HANDLE_VALUE != hStdout) {
                                                                                if (! ::WriteConsoleA(hStdout, line, sz + 1, NULL, NULL)) {
                                                                                        ::CloseHandle(hStdout);
                                                                                        hStdout = INVALID_HANDLE_VALUE;
                                                                                        // TODO: move into logger, as console may block.
                                                                                }
                                                                        }
                                                                }
                                                        }

                                                        ++nl, ++sz; // consume newline
                                                        if ('\r' == *nl) { // \n\r
                                                                ++nl, ++sz; // consume optional return
                                                        }

                                                        dwPopped += sz;
                                                }

                                                if (dwPopped) { // pop consumed characters
                                                        endpoint->pop(dwPopped);
                                                } else if (0 == endpoint->avail || (eof && endpoint->length())) { // otherwise on buffer full or eof; flush
                                                        ServiceDiags::Adapter::push(logger_, endpoint->log_level(), endpoint->buffer, endpoint->length());
                                                        if (INVALID_HANDLE_VALUE != hStdout) {
                                                                ::WriteConsoleA(hStdout, endpoint->buffer, endpoint->size, NULL, NULL);
                                                        }
                                                        endpoint->reset();
                                                }
                                        }
                                        endpoint->state = PipeEndpoint::EP_READY; //re-arm reader
                                        break;

                                default:
                                        diags().ferror("unexpected endpoint state : %u", (unsigned)endpoint->state);
                                        assert(false);
                                        break;
                                }

                        } else if (res > 0) {
                                diags().ferror("unexpected io completion : %d", res);
                                assert(false);
                        }

                } else if (dwWait == WAIT_OBJECT_0) {
                        ::ResetEvent(logger_stop_event_);
                        terminate = true;       // drain stream, then exit

                } else if (dwWait >= WAIT_ABANDONED_1 && dwWait < (WAIT_ABANDONED_1 + cnt)) {
                        const unsigned idx = dwWait - WAIT_ABANDONED_1;

                        endpoint = endpoints[idx];
                        assert(handles[idx + 1] == endpoint->ioevent);

                } else if (dwWait == WAIT_TIMEOUT) {
                        if (terminate) {
                                break;          // exit
                        }

                } else {
                        // WAIT_IO_COMPLETION
                        // WAIT_FAILED
                        const DWORD dwError = GetLastError();
                        diags().ferror("unexpected wait completion : %u", (unsigned)dwError);
                        assert(false);
                        break;                  // exit
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
                        return path;            // exists within CWD.
                }
                basename = path + 2;            // './xxxx' or '.\xxxx'

        } else if (NULL == strchr(path, '/') && NULL == strchr(path, '\\')) {
                basename = path;                // xxxx
        }

        if (basename) {
                int len = (int)::GetModuleFileNameA(NULL, t_szAppPath, sizeof(t_szAppPath));

                const char *d1 = strrchr(t_szAppPath, '/'), *d2 = strrchr(t_szAppPath, '\\'),
                *d = (d1 > d2 ? d1 : d2);       // last delimiter
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
        if (configopen_)                        // one-shot
                return true;

        if (! config_.empty()) {
                return true;
        }

        if (! options_.conf.empty()) {
                std::string errmsg, path(ResolveRelative(options_.conf.c_str()));

                if (config_.Load(path, errmsg)) {
                        configopen_ = true;     // success
                        return true;
                }

                if (options_.conf_required) {   // require?
                        CNTService::LogError(true, "unable to open configuration <%s>: %s", path.c_str(), errmsg.c_str());
                        diags().ferror("unable to open configuration <%s>: %s", path.c_str(), errmsg.c_str());
                        return false;
                }

                diags().fwarning("unable to open configuration <%s>: %s", path.c_str(), errmsg.c_str());
        }

        configopen_ = CNTService::ConfigOpen() || !options_.conf_required;

        return configopen_;
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
                return true;
        }

        return false;
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

