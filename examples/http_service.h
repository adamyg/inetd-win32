#pragma once
#ifndef HTTP_SERVICE_H_INCLUDED
#define HTTP_SERVICE_H_INCLUDED
//
//  HTTP Service,
//  extended https://gitlab.com/eidheim/Simple-Web-Server
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

#if !defined(ASIO_STANDALONE)
#define ASIO_STANDALONE 1
#endif

#include <server_http.hpp>

class http_service : public SimpleWeb::Server<SimpleWeb::HTTP> {
public:
        http_service() :
                SimpleWeb::Server<SimpleWeb::HTTP>() {
        }

        void start(SOCKET socket) {
                if (! io_service) {
                        io_service = std::make_shared<asio::io_context>();
                        internal_io_service = true;
                }

                after_bind();
                accept(socket);

#if (TODO)
                if (internal_io_service) {
                        // If thread_pool_size>1, start m_io_service.run() in (thread_pool_size-1) threads for thread-pooling
                        threads.clear();
                        for (std::size_t c = 1; c < config.thread_pool_size; c++) {
                                threads.emplace_back([this]() { this->io_service->run(); });
                        }

                        // Main thread
                        if (config.thread_pool_size > 0) {
                                io_service->run();
                        }

                        // Wait for the rest of the threads, if any, to finish as well
                        for(auto &t : threads)
                                t.join();
                }
#endif

                io_service->run();
        }

#if (TODO)
        void join() {
                for(auto &t : threads) {
                    t.join();
                }
        }
#endif

        void accept(SOCKET socket) {
                auto connection = create_connection(*io_service);

                connection->socket->assign(asio::ip::tcp::v4(), socket);
                auto session = std::make_shared<Session>(config.max_request_streambuf_size, connection);

                asio::ip::tcp::no_delay option(true);
                asio::error_code ec;
                session->connection->socket->set_option(option, ec);

                if (! ec) {
                        this->read(session);
                } else if(this->on_error) {
                        this->on_error(session->request, ec);
                }
        }
};

#endif //HTTP_SERVICE_H_INCLUDED
//end
