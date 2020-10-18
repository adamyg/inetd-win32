/*
 * argument handing
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

#include <vector>
#include <cstdlib>
#include <cstdio>


class Arguments {

public:
    Arguments(const std::vector<std::string> &args, bool clone = false) :
            argc_(0), argv_(NULL), cloned_(0) {
        argv_ = (const char **)calloc(args.size() + 1, sizeof(char *));
        if (argv_) {
            size_t v = 0;

            if (clone) {
                size_t length = 0;

                for (std::vector<std::string>::const_iterator it(args.begin()), end(args.end());
                            it != end; ++it) {
                    length += it->length() + 1;
                }

                if (length && NULL != (cloned_ = (char *)malloc(length))) {
                    char *cursor = cloned_;
                    for (std::vector<std::string>::const_iterator it(args.begin()), end(args.end());
                                it != end; ++it) {
                        const std::string &val = *it;
                        (void) memcpy(cursor, val.c_str(), val.length() + 1 /*nul*/);
                        argv_[v++] = cursor;
                        cursor += val.length() + 1;
                    }
                }
            } else {
                for (std::vector<std::string>::const_iterator it(args.begin()), end(args.end());
                            it != end; ++it) {
                    argv_[v++] = it->c_str();
                }
            }
            argv_[v] = NULL;
            argc_ = v;
        }
    }

    ~Arguments() {
        if (argv_) {
            if (cloned_) ::free(cloned_);
            ::free(argv_);
        }
    }

    int argc() const {
        return argc_;
    }

    const char **argv() const {
        return argv_;
    }

public:
    static void
    split(std::vector<std::string> &argv, const char *cmd, bool escapes = true) {
        if (char *t_cmd = ::_strdup(cmd)) {
            emplace_split(argv, t_cmd, escapes);
            ::free(t_cmd);
        }
    }

    static void
    emplace_split(std::vector<std::string> &argv, char *cmd, bool escapes = true) {
        char *start, *end;

        if (cmd == NULL) return;
        for (;;) {
            // skip over blanks
            while (*cmd == ' '|| *cmd == '\t' || *cmd == '\n') {
                ++cmd;                          /* white-space */
            }

            if (!*cmd) break;

            // next argument
            if ('\"' == *cmd || '\'' == *cmd) {
                const char quote = *cmd++;      /* quoted argument */

                start = end = cmd;
                for (;;) {
                    const char ch = *cmd;

                    if (!ch || ch == '\n' || ch == quote) {
                        break;
                    }
                    if (escapes && ch == '\\') {
                        if ('\"' == cmd[1] || '\'' == cmd[1] || '\\' == cmd[1]) {
                            ++cmd;
                        }
                    }
                    *end++ = *cmd++;
                }

            } else {
                start = end = cmd;
                for (;;) {
                    const char ch = *cmd;

                    if (!ch || ch == '\n' || ch == ' ' || ch == '\t') {
                        break;
                    }
                    if (escapes && ch == '\\') {
                        if ('\"' == cmd[1] || '\'' == cmd[1] || '\\' == cmd[1]) {
                            ++cmd;
                        }
                    }
                    *end++ = *cmd++;
                }
            }

            // result
            argv.push_back(std::string(start, end - start));
            if (!*cmd) break;
            *end = 0;
            ++cmd;
        }
    }

private:
    int argc_;
    const char **argv_;
    char *cloned_;
};

//end
