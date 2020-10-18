/* -*- mode: c; indent-width: 8; -*- */
/*
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

#include <Windows.h>
#include <iostream>

int
main()
{
    SC_HANDLE schSCManager = 0, schService = 0;
    SERVICE_STATUS controlParms = {0};
    DWORD retStatus = (DWORD)-1;

    schSCManager = ::OpenSCManager(NULL, NULL, GENERIC_READ);
    if (NULL != schSCManager) {
        schService = ::OpenService(schSCManager, "inetd_service",
                            SERVICE_USER_DEFINED_CONTROL | SERVICE_QUERY_STATUS);

        if (NULL != schService) {
            std::cout << "connected to Service" << std::endl;
            retStatus = ::ControlService(schService, 141, &controlParms);
            if (retStatus) {                    // service return code
                std::cout << "For command 141, return code from service was " << controlParms.dwWin32ExitCode << std::endl;
            } else {
                std::cout << "Sending command 141 failed" << std::endl;
            }
            ::CloseServiceHandle(schService);

        } else {
            std::cout << "could not connect to Service" << std::endl;
        }
        ::CloseServiceHandle(schSCManager);

    } else {
        std::cout << "could not open service manager" << std::endl;
    }
    return 0;
}

//end