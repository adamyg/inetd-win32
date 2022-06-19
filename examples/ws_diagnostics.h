#pragma once
#ifndef WS_DIAGNOSTICS_H_INCLUDED
#define WS_DIAGNOSTICS_H_INCLUDED
//
//  WebSocket diagnostics
//
//  Copyright (c) 2020 - 2022, Adam Young.
//
//  The applications are free software: you can redistribute it
//  and/or modify it under the terms of the GNU General Public License as
//  published by the Free Software Foundation, version 3.
//
//  Redistributions of source code must retain the above copyright
//  notice, and must be distributed with the license document above.
//
//  Redistributions in binary form must reproduce the above copyright
//  notice, and must include the license document above in
//  the documentation and/or other materials provided with the
//  distribution.
//
//  This project is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
//  license for more details.
//  ==end==
//

#include <cstdarg>
#include <strstream>
#include <iostream>
#include <cassert>

namespace ws {

class Diagnostics {
public:
        static const unsigned BUFFERSZ = 1024;

        class StreamInstance {
        private:
                struct Stream {
                        Stream(unsigned level)
                                : level_(level), text_(message_, sizeof(message_) - 2 /*nl+nul*/)
                        {
                        }

                        ~Stream()
                        {
                                flush_impl();
                        }

                        std::ostream &get()
                        {
                                return text_;
                        }

                        void flush()
                        {
                                if (!flush_impl()) return;
                                text_.clear();
                                text_.seekp(0, std::ios::beg);
                        }

                private:
                        bool flush_impl()
                        {
                                size_t length = (size_t)text_.tellp();
                                if (0 == length) return false;

                                char *msg = message_;
                                if ('\n' != msg[length - 1])
                                    msg[length++] = '\n';
                                msg[length] = 0;

                                Diagnostics::message(msg);
                                return true;
                        }

                private:
                        const unsigned level_;
                        std::ostrstream text_;
                        char message_[BUFFERSZ];
                };

        private:
                StreamInstance(const StreamInstance &rhs) = delete;
                StreamInstance& operator=(const StreamInstance &rhs) = delete;

        public:
                StreamInstance(unsigned level)
                        : stream_(new Stream(level)) {
                }

                StreamInstance(StreamInstance &&rhs) {
                        stream_ = rhs.stream_, rhs.stream_ = nullptr;
                }

                ~StreamInstance() {
                       delete(stream_);
                }

                template <typename T>
                friend std::ostream& operator<<(const StreamInstance &rhs, const T &value) {
                        assert(rhs.stream_);
                        return rhs.stream_->get() << value;
                }

                friend std::ostream& operator<<(const StreamInstance &rhs, const char *value) {
                        assert(rhs.stream_);
                        return rhs.stream_->get() << value;
                }

        private:
                mutable Stream *stream_;
        };

        friend struct Stream;

        StreamInstance stream(unsigned level = 0)
        {
                return StreamInstance(level);
        }

        void ferror(const char *fmt, ...)
        {
                va_list ap;
                va_start(ap, fmt);
                vmessage(fmt, ap);
                va_end(ap);
        }

        void fwarning(const char *fmt, ...)
        {
                va_list ap;
                va_start(ap, fmt);
                vmessage(fmt, ap);
                va_end(ap);
        }

        void finfo(const char *fmt, ...)
        {
                va_list ap;
                va_start(ap, fmt);
                vmessage(fmt, ap);
                va_end(ap);
        }

        void fdebug(const char *fmt, ...)
        {
                va_list ap;
                va_start(ap, fmt);
                vmessage(fmt, ap);
                va_end(ap);
        }

        void vmessage(const char *fmt, va_list ap)
        {
                char msg[BUFFERSZ];
                int length;

                if ((length = vsnprintf(msg, sizeof(msg) - 2, fmt, ap)) < 0 || length > (sizeof(msg) - 2))
                        length = sizeof(msg) - 2;

                if ('\n' != msg[length - 1])
                        msg[length++] = '\n';
                msg[length] = 0;

                message(msg);
        }

        static void message(const char *msg)
        {
                std::cout << msg;
#if defined(_DEBUG)
                ::OutputDebugStringA(msg);
#endif
        }
};

};  //namespace ws

#endif //WS_DIAGNOSTICS_H_INCLUDED
