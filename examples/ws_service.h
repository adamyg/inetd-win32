#pragma once
#ifndef WS_SERVICE_H_INCLUDED
#define WS_SERVICE_H_INCLUDED
//
//  WS Service,
//  extended https://gitlab.com/eidheim/Simple-WebSocket-Server
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

#include <server_ws.hpp>

class ws_service : public SimpleWeb::SocketServer<SimpleWeb::WS> {
public:
    using Transport = SimpleWeb::WS;

public:
    ws_service() : SimpleWeb::SocketServer<SimpleWeb::WS>()
    {
    }

    // Alternative client
    void client(asio::detail::socket_type socket, bool wait = false, const std::function<void(unsigned short /*port*/)> &callback = nullptr)
    {
        std::unique_lock<std::mutex> lock(start_stop_mutex);

        if(!io_service) {
            io_service = std::make_shared<asio::io_context>();
            internal_io_service = true;
        }

        after_bind();
        accept(socket);

        if(internal_io_service && io_service->stopped())
            SimpleWeb::restart(*io_service);

        if(callback)
            post(*io_service, [callback] {
                callback(0);
            });

        if(internal_io_service) {
            // If thread_pool_size>1, start m_io_service.run() in (thread_pool_size-1) threads for thread-pooling
            threads.clear();
            for(std::size_t c = 1; c < config.thread_pool_size; c++) {
                threads.emplace_back([this]() {
                    this->io_service->run();
                });
            }

            lock.unlock();

            // Main thread
            if(wait || config.thread_pool_size > 0)
                io_service->run();

            lock.lock();

            // Wait for the rest of the threads, if any, to finish as well
            for(auto &t : threads)
                t.join();
        }
    }

    void stop() noexcept
    {
        std::lock_guard<std::mutex> lock(start_stop_mutex);

        close_connections();

        if(internal_io_service)
            io_service->stop();
    }
};

#endif //WS_SERVICE_H_INCLUDED