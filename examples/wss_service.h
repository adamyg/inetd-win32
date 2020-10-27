//
//  WSS Service, 
//  extended https://gitlab.com/eidheim/Simple-WebSocket-Server

#define  USE_STANDALONE_ASIO 1
#include <server_wss.hpp>

class wss_service : public SimpleWeb::SocketServer<SimpleWeb::WSS> {
public:
    wss_service() : 
      SimpleWeb::SocketServer<SimpleWeb::WSS>() {
    }

    // Alternative client
    void client(asio::detail::socket_type socket, bool wait = false, const std::function<void(unsigned short /*port*/)> &callback = nullptr) {
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

    void stop() noexcept {
      std::lock_guard<std::mutex> lock(start_stop_mutex);

      close_connections();

      if(internal_io_service)
        io_service->stop();
    }
};

//end
