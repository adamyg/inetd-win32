/*
 * CNTService - Classic window services framework (tweaked).
 * Service Control
 *
 * Copyright (c) 2020, Adam Young.
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

#include <string>

#include "NTServiceIO.h"

class CNTServiceControl {
    CNTServiceControl(const class CNTServiceControl &) /*=delete*/;
    CNTServiceControl& operator=(const CNTServiceControl &) /*=delete*/;

public:
    CNTServiceControl(const char *svcname, NTService::IDiagnostics &diags = NTService::StdioDiagnosticsIO::Get());
    ~ CNTServiceControl();

    int  ExecuteCommand(int argc, const char * const *argv, unsigned filter = 0);
    void Start();
    void UpdateDacl();
    void Stop();

private:
    NTService::IDiagnostics &diags_;
    std::string svcName_;
};

//end
