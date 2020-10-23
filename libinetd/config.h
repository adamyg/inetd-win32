/* -*- mode: c; indent-width: 8; -*- */
/*
 * Extended configuration - xinetd style
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

/*
 *
 *  defaults
 *  {
 *      <attribute> = <value> <value> ...
 *      ...
 *  }
 *
 *  service <service_name>
 *  {
 *      <attribute> <assign_op> <value> <value> ...
 *      ...
 *  }
 *
 */

namespace inetd {

///////////////////////////////////////////////////////////////////////////////
//

class ConfigDefaults {
        ConfigDefaults(const ConfigDefaults &) = delete;
        ConfigDefaults& operator=(const ConfigDefaults &) = delete;

public:
        ConfigDefaults() :
                toomany(0), maxchild(0), maxcpm(0), maxperip(0) {
        }

        template <typename Iterator>
        void load(Iterator it, Iterator end,
                        Compare compare = Comparision()) {
                while (it != end) {
                        if (Comparsion(it.first, "toomany")) {
                        } else if (Comparsion(it.first, "maxchild")) {
                        } else if (Comparsion(it.first, "maxcpm")) {
                        } else if (Comparsion(it.first, "maxperip")) {
                        }
                }
        }

private:
        struct Comparision {
                bool operator()(const std::string &lhs, const char *rhs) {
                        return (0 == strcmp(lhs.c_str(), rhs));
                };
                bool operator()(const char *lhs, const char *rhs) {
                        return (0 == strcmp(lhs, rhs));
                };
        }

public:
        int toomany; 
        int maxchild;
        int maxcpm;  
        int maxperip;
};


///////////////////////////////////////////////////////////////////////////////
//

class ConfigServer {
        ConfigServer(const ConfigServer &) = delete;
        ConfigServer& operator=(const ConfigServer &) = delete;

public:
        template <typename Iterator>
        void load(const ConfigDefaults &defaults, Iterator it, Iterator end) {
                while (it != end) {
                        ++it;
                }
        }

public:
};


}; //namespace inetd

/*end*/


