//
//  WS Service, 
//  extended https://gitlab.com/eidheim/Simple-WebSocket-Server

#define  USE_STANDALONE_ASIO 1
#include <server_ws.hpp>

class ws_service : public SimpleWeb::SocketServer<SimpleWeb::WS> {
public:
    ws_service() : 
            SimpleWeb::SocketServer<SimpleWeb::WS>() {  
    }

    void start(SOCKET socket) {
//      std::lock_guard<std::mutex> lock(start_stop_mutex);
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

    void stop() noexcept {
//    std::lock_guard<std::mutex> lock(start_stop_mutex);
      if(internal_io_service)
        io_service->stop();
    }

    void accept(SOCKET socket) {
        std::shared_ptr<Connection> connection(new_connection());

        connection->get_socket().assign(asio::ip::tcp::v4(), socket);

        asio::ip::tcp::no_delay option(true);
        asio::error_code ec;
        connection->get_socket().set_option(option, ec);

        if (! ec) {
            read_handshake(connection);
        }
    }
};

//end
