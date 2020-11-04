//
//  WSS Service, 
//  extended https://gitlab.com/eidheim/Simple-WebSocket-Server

#define  USE_STANDALONE_ASIO 1
#include <server_wss.hpp>

#if defined(_WIN32) && (CrytoAPI_OPENSSL)
#include "ssl_utilises.hpp"
#include "ssl_certificate.hpp"
#include "ssl_cacertificates.hpp"
#include "ssl_verify.hpp"
#endif

#pragma comment(lib, "libssl.lib")
#pragma comment(lib, "libcrypto.lib")

class wss_service : public SimpleWeb::SocketServer<SimpleWeb::WSS> {
public:
    /**
     * Constructs a server object.
     *
     * @param certification_file If non-empty, sends the given certification file to client.
     * @param private_key_file   Specifies the file containing the private key for certification_file.
     * @param verify_file        If non-empty, use this certificate authority file to perform verification of client's certificate and hostname according to RFC 2818.
     */
    wss_service(const std::string &certification_file, const std::string &private_key_file, const std::string &verify_file = std::string()) : 
         SimpleWeb::SocketServer<SimpleWeb::WSS>("", "", "") {
      set_certificates(certification_file, private_key_file, verify_file);
    }

    /*
     * Set server certificate and optional client certificate authority.
     *
     * @param certification_file If non-empty, sends the given certification file to client.
     * @param private_key_file   Specifies the file containing the private key for certification_file.
     * @param verify_file        If non-empty, use this certificate authority file to perform verification of client's certificate and hostname according to RFC 2818.
     */
    void set_certificates(const std::string &certification_file, const std::string& private_key_file, const std::string& vertify_file = std::string()) {
#if defined(_WIN32) && (CrytoAPI_OPENSSL)
      if (-1 == SimpleWeb::ssl_certificate::load(context.native_handle(), certification_file.c_str()))
#endif                                                                    
      {
        context.use_certificate_chain_file(certification_file); //certificate chain from a file (see: SSL_CTX_use_certificate_chain_file)
        context.use_private_key_file(private_key_file, asio::ssl::context::pem /*or context_base::asn1*/); //private key from a file (see: SSL_CTX_use_PrivateKey_file)
      }

      if (! vertify_file.empty()) {
#if defined(_WIN32) && (CrytoAPI_OPENSSL)
        if (-1 == SimpleWeb::ssl_cacertificates::load(context.native_handle(), vertify_file.c_str()))
#endif
        {
          context.load_verify_file(vertify_file);
        }
        context.set_verify_mode(asio::ssl::verify_peer | asio::ssl::verify_fail_if_no_peer_cert | asio::ssl::verify_client_once);
        set_session_id_context = true;
      }
    }

    /*
     * Set transport ciphers.
     *
     * @param ciphers            Cipher list specification; see OpenSSL for details.
     */
    bool set_cipher_list(const std::string &ciphers) {
     if (! ciphers.empty()) {
       return set_cipher_list(ciphers.c_str());
     }
     return false;
    }

    /*
     * Set transport ciphers.
     *
     * @param ciphers            Cipher list specification; see OpenSSL for details.
     */
    bool set_cipher_list(const char *ciphers) {
      if (ciphers && *ciphers) {
        return (/*failure*/ 0 != SSL_CTX_set_cipher_list(context.native_handle(), ciphers));
      }
      return false;
    }

    /*
     *  Enable SSL diagnostics
     */
    bool set_diagnostics() {
      SSL_CTX *ctx = context.native_handle();

      assert(ctx);
      if (SSL_CTX_get_info_callback(ctx)) {
        return false;
      }
      SSL_CTX_set_info_callback(ctx, ssl_ctx_info_cb);
      return true;
    }

    /*
     * Start a direct client connection.
     *
     * @param ciphers            Cipher list specification; see OpenSSL for details.
     *
     * TODO: Allow concurrent start() and multiple client() calls.
     */
    void client(asio::detail::socket_type socket, bool wait = false, const std::function<void(unsigned short /*port*/)> &callback = nullptr) {
      std::unique_lock<std::mutex> lock(start_stop_mutex);
//    const bool running = running_++;  

      if(!io_service) {
        io_service = std::make_shared<asio::io_context>();
        internal_io_service = true;
      }

      accept(socket);

      if(/*!running &&*/ internal_io_service && io_service->stopped())
        SimpleWeb::restart(*io_service);

      if(callback)
        post(*io_service, [callback] {
          callback(0);
        });

      if(/*!running &&*/ internal_io_service) {
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

//    running_ = 0;
      close_connections();

      if(internal_io_service)
        io_service->stop();
    }

private:
    /*
     * Connection run-time diagnositics.
     */
    static void ssl_ctx_info_cb(const SSL *ssl, int where, int ret) {
      char message[1024];
      const char *str;
      int w;

//    wpa_printf(MSG_DEBUG, "SSL: (where=0x%x ret=0x%x)", where, ret);
      w = where & ~SSL_ST_MASK;
      if (w & SSL_ST_CONNECT) str = "SSL_connect";
      else if (w & SSL_ST_ACCEPT) str = "SSL_accept";
      else str = "undefined";

      if (where & SSL_CB_LOOP) {
        sprintf_s(message, sizeof(message), 
            "SSL: %s:%s", str, SSL_state_string_long(ssl));
        std::cout << message << '\n';

      } else if (where & SSL_CB_ALERT) {
        sprintf_s(message, sizeof(message), 
            "SSL: SSL3 alert: %s:%s:%s",
                where & SSL_CB_READ ? "read (remote end reported an error)" : "write (local SSL3 detected an error)",
                SSL_alert_type_string_long(ret), SSL_alert_desc_string_long(ret));
        std::cout << message << '\n';

      } else if (where & SSL_CB_EXIT && ret <= 0) {
        sprintf_s(message, sizeof(message), "SSL: %s:%s in %s",
                str, ret == 0 ? "failed" : "error", SSL_state_string_long(ssl));
        std::cout << message << '\n';
      }
    }
};

//end
