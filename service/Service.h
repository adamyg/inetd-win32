#pragma once
#ifndef SERVICE_H_INCLUDED
#define SERVICE_H_INCLUDED
/* -*- mode: c; indent-width: 8; -*- */
/*
 * Service adapter
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

#if defined(_MSC_VER)
#include <msvc_system_error.hpp>
#endif
#include "../libNTService/NTService.h"

#include "SimpleConfig.h"
#include "Logger.h"

#include "w32support.h"

class Service : public CNTService {
        BOOST_DELETED_FUNCTION(Service(const Service &))
        BOOST_DELETED_FUNCTION(Service& operator=(const Service &))

public:
        struct Options {
                Options() : argc(0), argv(NULL), arg0(NULL), /*port(0),*/
                        ignore(false), daemon_mode(false), delay_start(false),
                        console_output(false), logger(true) {
                }
                int argc;
                const char **argv;
                const char *arg0;
                bool ignore;
                bool daemon_mode;
                bool delay_start;
                bool console_output;
                bool logger;
//              unsigned port;
                std::string conf;
        };

public:
        Service(const char *svcname, bool console_mode = true);
        virtual ~Service();

        void Start(const Options &options);

protected:
        static  std::string ResolveRelative(const char *path);
        virtual bool ConfigOpen(bool create = true);
        virtual void ConfigClose();
        virtual bool ConfigSet(const char *csKey, const char *szValue);
        virtual bool ConfigSet(const char *csKey, DWORD dwValue);
        virtual int  ConfigGet(const char *csKey, char *szBuffer, size_t dwSize, unsigned flags = 0);
        virtual bool ConfigGet(const char *csKey, DWORD &dwValue, unsigned flags = 0);
                int  ConfigGet(const char *csKey, std::string &value, unsigned flags = 0);

protected:
        int Initialise();
        void Shutdown();
        void AttachLogger();
        bool ConfigLogger();
        virtual void ServiceRun();
        virtual void ServiceTrace(const char *fmt, ...);
        virtual bool OnInit();
        virtual void OnStop();

private:
        struct PipeEndpoint;

        static DWORD WINAPI server_thread(LPVOID lpParam);
        static DWORD WINAPI logger_thread(LPVOID lpParam);

        void service_body();
        static DWORD NewPipeEndpoint(const char *pipe_name, PipeEndpoint *&endpoint);
        void logger_body(PipeEndpoint *endpoint);

private:
        struct Options options_;
        SimpleConfig config_;
        Logger logger_;

        char   pipe_name_[256];
        HANDLE logger_stop_event_;
        HANDLE logger_thread_;
        HANDLE server_stopped_event_;
        HANDLE server_thread_;
};

#endif  //SERVICE_H_INCLUDED

//end

