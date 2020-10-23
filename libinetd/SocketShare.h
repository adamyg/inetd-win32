#pragma once
/* -*- mode: c; indent-width: 8; -*- */
/*
 * Process socket sharing
 * windows inetd service.
 *
 * Copyright (c) 2020, Adam Young.
 * All rights reserved.
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

#include <cstdio>
#include <cassert>

#include "Windowstd.h"
#include "ScopedHandle.h"
#include "ScopedProcessId.h"

#pragma comment(lib, "Rpcrt4.lib")              // UUID

namespace inetd {
class SocketShare {
    SocketShare(const SocketShare &) = delete;
    SocketShare& operator=(const SocketShare &) = delete;

private:
    struct ServerProfile {
        ServerProfile() {
            GenerateUniqueName(basename, sizeof(basename));
        }

        ~ServerProfile() {
        }

        char basename[MAX_PATH];
        ScopedProcessId child;
        ScopedHandle hParentEvent;
        ScopedHandle hChildEvent;
        ScopedHandle hPipe;
    };

    struct ClientProfile {
        ClientProfile(const char *name) {
            strncpy_s(basename, name, sizeof(basename));
        }

        ~ClientProfile() {
        }

        char basename[MAX_PATH];
        ScopedHandle hParentEvent;
        ScopedHandle hChildEvent;
        ScopedHandle hFile;
    };

    struct Names {
#define PARENT_EVENT_SPEC   "Local\\%s-parent"
#define CHILD_EVENT_SPEC    "Local\\%s-child"
#define PIPE_NAME_SPEC      "\\\\.\\pipe\\%s"

        Names(const char *basename) {
            sprintf_s(parentEvent, sizeof(parentEvent), PARENT_EVENT_SPEC, basename);
            sprintf_s(childEvent, sizeof(childEvent), CHILD_EVENT_SPEC, basename);
            sprintf_s(pipe, sizeof(pipe), PIPE_NAME_SPEC, basename);
        }

        char parentEvent[MAX_PATH];
        char childEvent[MAX_PATH];
        char pipe[MAX_PATH];
    };

public:
    class Server {
        Server(const Server &) = delete;
        Server& operator=(const Server &) = delete;
    public:
        Server(HANDLE job_handle, const char *progname, const char **argv = 0) :
                profile_(), job_handle_(job_handle), progname_(progname), argv_(argv) {
        }
        Server(const char *progname, const char **argv = 0) :
                profile_(), job_handle_(nullptr), progname_(progname), argv_(argv) {
        }
        bool publish(SOCKET socket) {
            if (! profile_.hPipe.IsValid()) {
                return PushSocket(profile_, socket, job_handle_, progname_, argv_);
            }
            return WriteSocket(profile_, socket);
        }
        const ScopedProcessId &child() const {
            return profile_.child;
        }
        HANDLE process_handle() const {
            profile_.child.process_handle();
        }
        int pid() const {
            return profile_.child.pid();
        }
    private:
        ServerProfile profile_;
        HANDLE job_handle_;
        const char *progname_;
        const char **argv_;
    };

    class Client {
        Client(const Client &) = delete;
        Client& operator=(const Client &) = delete;
    public:
        Client(const char *basename) :
                profile_(basename) {
        }
        bool
        wait(DWORD timeout = INFINITE) {
            return WaitSocket(profile_, timeout);
        }
        SOCKET get(DWORD dwFlags = WSA_FLAG_OVERLAPPED) {
            if (! profile_.hFile.IsValid()) {
                return GetSocket(profile_, dwFlags);
            }
            return ReadSocket(profile_, dwFlags);
        }
    private:
        ClientProfile profile_;
    };

public:
    static bool
    PushSocket(SOCKET socket,
            const char *progname, const char **argv = 0)
    {
        ServerProfile profile;
        return PushSocket(profile, socket, nullptr, progname, argv);
    }

    static bool
    PushSocket(SOCKET socket, HANDLE job_handle,
            const char *progname, const char **argv = 0)
    {
        ServerProfile profile;
        return PushSocket(profile, socket, job_handle, progname, argv);
    }

    static SOCKET
    GetSocket(const char *basename, DWORD dwFlags = 0 /*or WSA_FLAG_OVERLAPPED*/)
    {
        ClientProfile profile(basename);
        return GetSocket(profile, dwFlags);
    }

private:
    static bool
    PushSocket(ServerProfile &profile, SOCKET socket,
            HANDLE job_handle, const char *progname, const char **argv)
    {
        const Names names(profile.basename);
        char t_progname[_MAX_PATH] = {0};
        STARTUPINFO siStartInfo = {0};
        char cmdline[4 * 1024];                 // 4k limit

        assert(! profile.hPipe.IsValid());

        // Create an event to signal the child that the protocol info is set.

        if (! profile.hParentEvent.IsValid()) {
            profile.hParentEvent.Set(::CreateEventA(NULL, TRUE, FALSE, names.parentEvent));
            if (! profile.hParentEvent.IsValid()) {
                fprintf(stderr, "CreateEvent(%s) failed: %u\n", names.parentEvent, (unsigned) ::GetLastError());
                return false;
            }
        }

        // Create an event to for the child to signal the parent that the protocol info can be released

        if (! profile.hChildEvent.IsValid()) {
            profile.hChildEvent.Set(::CreateEventA(NULL, TRUE, FALSE, names.childEvent));
            if (! profile.hChildEvent.IsValid()) {
                fprintf(stderr, "CreateEvent(%s) failed: %u\n", names.childEvent, (unsigned) ::GetLastError());
                return false;
            }
        }

        // Process name.

        const char *dot = strrchr(progname, '.');
        if (NULL == dot || 0 != _stricmp(dot, ".exe")) {
            sprintf_s(t_progname, sizeof(t_progname), "%s.exe", progname);
            progname = t_progname;
        }

        // Child process command line options.

        ::GetStartupInfo(&siStartInfo);
        if (argv && argv[0]) {                  // publish interface label plus optional arguments
            char *cursor = cmdline,
                *end = (cmdline + sizeof(cmdline)) - 1 /*nul*/;

            cursor += sprintf_s(cmdline, sizeof(cmdline),
                        "\"%s\" -i \"%s\"", progname, profile.basename);
            while (argv[0]) {
                const int ret =
                        sprintf_s(cursor, (end - cursor) + 1 /*nul*/, " \"%s\"", argv[0]);
                if (ret < 0 || (cursor += ret) >= end) {
                    fprintf(stderr, "CommandLine() overflowed\n");
                    return false;
                }
                ++argv;
            }

        } else {
            sprintf_s(cmdline, sizeof(cmdline), // publish interface label
                "\"%s\" -i \"%s\"", progname, profile.basename);
        }

        // Create interface pipe.

        profile.hPipe.Set(::CreateNamedPipeA(
                    names.pipe,
                    PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                    PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                                // message and blocking mode
                    PIPE_UNLIMITED_INSTANCES,   // max. instances
                    4096, 4096,                 // output/input buffer size
                    NMPWAIT_USE_DEFAULT_WAIT,   // client time-out
                    NULL));                     // default security attribute

        DWORD creationFlags = 0;

        if (job_handle)
            creationFlags |= CREATE_SUSPENDED;

        if (! profile.hPipe.IsValid()) {
            fprintf(stderr, "CreatePipe(%s) failed: %u\n", names.pipe, (unsigned) ::GetLastError());

        } else if (FALSE == ::CreateProcessA(
                                progname,       // Module name
                                cmdline,        // Command line
                                NULL,           // Process handle not inheritable
                                NULL,           // Thread handle not inheritable
                                FALSE,          // Handle inheritance to FALSE
                                creationFlags,  // CreationFlags
                                NULL,           // Use parent's environment block
                                NULL,           // Use parent's starting directory
                                &siStartInfo,
                                profile.child)) {
            fprintf(stderr, "CreateProcess(%s) failed: %u\n", progname, (unsigned) ::GetLastError());
            profile.hPipe.Close();

        } else {

            // Associated job

            if (job_handle) {
                if (! ::AssignProcessToJobObject(job_handle, profile.child.process_handle())) {
                    fprintf(stderr, "AssignProcessJob(%s) failed: %u\n", progname, (unsigned) ::GetLastError());
                }
            }

            if (CREATE_SUSPENDED & creationFlags) {
                if (! ::ResumeThread(profile.child.process_thread())) {
                    fprintf(stderr, "ResumeProcess(%s) failed: %u\n", progname, (unsigned) ::GetLastError());
                    return false;
                }
            }

            // Wait for the pipe

            OVERLAPPED ol = {0, 0, 0, 0, NULL};
            DWORD ret = -1;
            bool ready = false;

            ol.hEvent = ::CreateEvent(NULL, TRUE, FALSE, NULL);
            if (::ConnectNamedPipe(profile.hPipe, &ol)) {
                ready = true;
            } else {
                ret = ::GetLastError();
                switch (ret) {
                case ERROR_PIPE_CONNECTED:
                    ready = true;
                    break;
                case ERROR_IO_PENDING:
                    if (WAIT_OBJECT_0 == ::WaitForSingleObject(ol.hEvent, 5 * 1000 /*5 seconds*/)) {
                        DWORD dwIgnore = 0;
                        if (::GetOverlappedResult(profile.hPipe, &ol, &dwIgnore, FALSE)) {
                            ready = true;
                        }
                    } else {
                        ret = ::GetLastError();
                        ::CancelIo(profile.hPipe);
                    }
                    break;
                default:
                    break;
                }
            }
            ::CloseHandle(ol.hEvent);

            if (ready) {
                return WriteSocket(profile, socket);
            }

            fprintf(stderr, "ConnectNamedPipe(%s) failed: %u\n", names.pipe, (unsigned) ret);
        }
        return false;
    }

    static bool
    WriteSocket(ServerProfile &profile, SOCKET socket) {

        WSAPROTOCOL_INFO pi = {0};
        DWORD dwBytes = 0;

        // Clone

        if (SOCKET_ERROR == ::WSADuplicateSocketA(socket, profile.child.process_id(), &pi)) {
            fprintf(stderr, "WSADuplicateSocket() failed: %u\n", (unsigned) ::WSAGetLastError());

        // Write

        } else if (::WriteFile(profile.hPipe, &socket, sizeof(socket), &dwBytes, NULL) &&
                        ::WriteFile(profile.hPipe, &pi, sizeof(pi), &dwBytes, NULL)) {

            // Signal write complete

            if (! ::ResetEvent(profile.hChildEvent)) {
                fprintf(stderr, "ResetEvent(child) failed: %u\n", (unsigned) ::GetLastError());
            }

            if (! ::SetEvent(profile.hParentEvent)) {
                fprintf(stderr, "SetEvent(parent) failed: %u\n", (unsigned) ::GetLastError());
            }

            // Wait for client

            if (WAIT_OBJECT_0 == ::WaitForSingleObject(profile.hChildEvent, 2 * 1000)) {
                return true;
            }

            fprintf(stderr, "WaitEvent(child) failed: %u\n", (unsigned) ::GetLastError());

        } else {
            fprintf(stderr, "WriteSocket() failed: %u\n", (unsigned) ::GetLastError());
        }

        return false;
    }

    static SOCKET
    GetSocket(ClientProfile &profile, DWORD dwFlags)
    {
        const Names names(profile.basename);

        assert(! profile.hFile.IsValid());

        // Events

        if (! profile.hParentEvent.IsValid()) {
            profile.hParentEvent.Set(::OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, names.parentEvent));
            if (! profile.hParentEvent.IsValid()) {
                fprintf(stderr, "OpenEvent(%s) failed: %u\n", names.parentEvent, (unsigned) ::GetLastError());
                return INVALID_SOCKET;
            }
        }

        if (! profile.hChildEvent.IsValid()) {
            profile.hChildEvent.Set(::OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, names.childEvent));
            if (! profile.hChildEvent.IsValid()) {
                fprintf(stderr, "OpenEvent(%s) failed: %u\n", names.childEvent, (unsigned) ::GetLastError());
                return INVALID_SOCKET;
            }
        }

        // Pipe

        while (true) {

            // Open

            profile.hFile.Set(::CreateFileA(
                        names.pipe,             // pipe name
                        GENERIC_READ | GENERIC_WRITE,
                        0,                      // no sharing
                        NULL,                   // default security attributes
                        OPEN_EXISTING,          // opens existing pipe
                        0,                      // default attributes
                        NULL));                 // no template file

            if (profile.hFile.IsValid()) {

                // Read mode

                DWORD dwMode = PIPE_READMODE_BYTE;
                ::SetNamedPipeHandleState(profile.hFile, &dwMode, NULL, NULL);

                return ReadSocket(profile, dwFlags);
            }

            // Wait for server
            const DWORD ret = ::GetLastError();
            if (ERROR_PIPE_BUSY != ret) {
                fprintf(stderr, "OpenPipe(%s) failed: %u\n", names.pipe, (unsigned) ret);
                break;

            } else if (! ::WaitNamedPipeA(names.pipe, 2 * 1000)) {
                fprintf(stderr, "WaitPipe(%s) failed: %u\n", names.pipe, (unsigned) GetLastError());
                break;
            }
        }

        if (profile.hChildEvent.IsValid()) {    // unblock parent
            if (! ::SetEvent(profile.hChildEvent)) {
                fprintf(stderr, "SetEvent(child) failed: %u\n", (unsigned) GetLastError());
            }
        }

        return INVALID_SOCKET;
    }

    static bool
    WaitSocket(ClientProfile &profile, DWORD timeout = INFINITE) {

        // Opne event resource

        if (! profile.hParentEvent.IsValid()) {
            const Names names(profile.basename);

            profile.hParentEvent.Set(::OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, names.parentEvent));
            if (! profile.hParentEvent.IsValid()) {
                fprintf(stderr, "OpenEvent(%s) failed: %u\n", names.parentEvent, (unsigned) ::GetLastError());
                return false;
            }
        }

        // Wait for the parent to signal that the protocol info ready to be accessed

        if (WAIT_OBJECT_0 != ::WaitForSingleObject(profile.hParentEvent, timeout)) {
            return false;
        }
        return true;
    }

    static SOCKET
    ReadSocket(ClientProfile &profile, DWORD dwFlags) {

        // Wait for the parent to signal that the protocol info ready to be accessed

        if (WAIT_FAILED == ::WaitForSingleObject(profile.hParentEvent, 2 * 1000)) {
            fprintf(stderr, "WaitEvent(parent) failed: %d\n", (unsigned) ::GetLastError());
            return INVALID_SOCKET;
        }

        // Read socket description

        WSAPROTOCOL_INFO pi = {0};
        SOCKET socket = INVALID_SOCKET;
        DWORD dwBytes = 0;

        if (! ::ResetEvent(profile.hParentEvent)) {
            fprintf(stderr, "ResetEvent(parent) failed: %u\n",  (unsigned) ::GetLastError());
        }

        if (::ReadFile(profile.hFile, &socket, sizeof(socket), &dwBytes, NULL) &&
                    dwBytes == sizeof(socket) &&
            ::ReadFile(profile.hFile, &pi, sizeof(pi), &dwBytes, NULL) &&
                    dwBytes == sizeof(pi)) {

            // Clone socket

            if (INVALID_SOCKET == (socket =
                    ::WSASocket(AF_INET, SOCK_STREAM, 0, &pi, 0, dwFlags))) {
                DWORD ret = (unsigned) ::WSAGetLastError();

                if (WSANOTINITIALISED == ret) {
                    WSADATA wsaData = {0};

                    if (::WSAStartup(MAKEWORD(2, 2), &wsaData) ||
                            INVALID_SOCKET == (socket =
                                ::WSASocket(AF_INET, SOCK_STREAM, 0, &pi, 0, dwFlags))) {
                        ret = (unsigned) ::WSAGetLastError();
                    }
                }

                if (INVALID_SOCKET == socket) {
                    fprintf(stderr, "WSASocket() failed: %u\n", (unsigned) ret);
                }
            }

            // Disable socket inheritance; enabled by default, mirror unix defaults.

            ::SetHandleInformation((HANDLE) socket, HANDLE_FLAG_INHERIT, 0);

            // Signal parent we are done

            if (! ::SetEvent(profile.hChildEvent)) {
                fprintf(stderr, "SetEvent(child) failed: %u\n",  (unsigned) ::GetLastError());
            }

        } else {
            fprintf(stderr, "ReadSocket() failed: %u\n", (unsigned) ::GetLastError());
        }

        return socket;
    }

public:
    static bool
    GenerateUniqueName(char *buf, size_t buflen)
    {
        UUID uuid;
        char *str;

        (void) UuidCreate(&uuid);
        (void) UuidToStringA(&uuid, (RPC_CSTR*)&str);
        sprintf_s(buf, buflen, "%08x-%s-%08x",
            (unsigned)GetCurrentProcessId(), str, (unsigned)GetTickCount());
        RpcStringFreeA((RPC_CSTR *)&str);
        return true;
    }
};

};  //namespace inetd

//end
