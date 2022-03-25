//
//  WSS Service, 
//  extended https://gitlab.com/eidheim/Simple-WebSocket-Server

#include <server_wss.hpp>

#include <openssl/ec.h>
#include <openssl/ssl.h>

#if defined(_WIN32) && (HAVE_LIBCERTSTORE)
#include "ssl_certificate.hpp"
#include "ssl_cacertificates.hpp"
#include "ssl_ticket.hpp"
#endif

// note: mirror libtls
#define CIPHERS_COMPAT      "ALL:!aNULL:!eNULL"
#define CIPHERS_DEFAULT     "TLSv1.2+AEAD+ECDHE:TLSv1.2+AEAD+DHE"

#pragma comment(lib, "libssl.lib")
#pragma comment(lib, "libcrypto.lib")

class wss_service : 
        public SimpleWeb::SocketServer<SimpleWeb::WSS> {
private:
    struct ticket_key {
      time_t time;
      unsigned char key_name[16];
      unsigned char aes_key[32];
      unsigned char hmac_key[16];
    };

public:
    /**
     * Constructs a server object.
     *
     * @param certification_file If non-empty, sends the given certification file to client.
     * @param private_key_file   Specifies the file containing the private key for certification_file.
     * @param verify_file        If non-empty, use this certificate authority file to perform verification of client's certificate and hostname according to RFC 2818.
     */
    wss_service(const std::string &certification_file, const std::string &private_key_file, const std::string &verify_file = std::string()) : 
         SimpleWeb::SocketServer<SimpleWeb::WSS>() {
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
#if defined(_WIN32) && (HAVE_LIBCERTSTORE)
      if (-1 == CertStore::ssl_certificate::load(context.native_handle(), certification_file.c_str()))
#endif                                                                    
      {
        context.use_certificate_chain_file(certification_file); //certificate chain from a file (see: SSL_CTX_use_certificate_chain_file)
        context.use_private_key_file(private_key_file, asio::ssl::context::pem /*or context_base::asn1*/); //private key from a file (see: SSL_CTX_use_PrivateKey_file)
      }

      if (! vertify_file.empty()) {
#if defined(_WIN32) && (HAVE_LIBCERTSTORE)
        if (-1 == CertStore::ssl_cacertificates::load(context.native_handle(), vertify_file.c_str()))
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
        if (0 == strcmp(ciphers, "default") || 0 == strcmp(ciphers, "secure")) {
          ciphers = CIPHERS_DEFAULT;
        } else if (0 == strcmp(ciphers, "compat") || 0 == strcmp(ciphers, "legacy")) {
          ciphers = CIPHERS_COMPAT;
        }
        return (/*failure*/ 0 != SSL_CTX_set_cipher_list(context.native_handle(), ciphers));
            //Note: SSL_set_cipher_list() sets the list of ciphers (TLSv1.2 and below).
      }
      return false;
    }

    /*
     * Set transport ciphers.
     *
     * @param ciphers            Cipher suites specification; see OpenSSL for details.
     */
    bool set_cipher_suites(const std::string &ciphers) {
     if (! ciphers.empty()) {
       return set_cipher_suites(ciphers.c_str());
     }
     return false;
    }

    /*
     * Set transport ciphers.
     *
     * @param ciphers            Cipher suites specification; see OpenSSL for details.
     */
    bool set_cipher_suites(const char *ciphers) {
      if (ciphers && *ciphers) {
        return (/*failure*/ 0 != SSL_CTX_set_ciphersuites(context.native_handle(), ciphers));
            //Note: SSL_CTX_set_ciphersuites() is used to configure the available TLSv1.3 ciphersuites for ctx. 
            //  This is a simple colon (":") separated list of TLSv1.3 ciphersuite names in order of preference.
      }
      return false;
    }

    /*
     * Enable SSL diagnostics.
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
     * Set DH parameters from RFC 5114 and RFC 3526.
     */
    bool set_dh_auto(int param = -1) {
      SSL_CTX *ctx = context.native_handle();

      assert(ctx);
      if (-1 == param) {
        SSL_CTX_set_dh_auto(ctx, 1);
      } else if (1024 == param) {
        SSL_CTX_set_dh_auto(ctx, 2);
      } else {
        return false;
      }
      return true;   
    }

    /*
     * Set curve.
     */
    bool set_curve(int param = -1) {
      SSL_CTX *ctx = context.native_handle();

      assert(ctx);
      if (-1 == param) {
        SSL_CTX_set_ecdh_auto(ctx, 1);          //deprecated and have no effect.
        return true;
      } else if (param != NID_undef) {
        EC_KEY *ecdh_key;
        if (NULL != (ecdh_key = EC_KEY_new_by_curve_name(param))) {
          SSL_CTX_set_options(ctx, SSL_OP_SINGLE_ECDH_USE);
          SSL_CTX_set_tmp_ecdh(ctx, ecdh_key);
          EC_KEY_free(ecdh_key);
          return true;
        }
      }
      return false;
    }

    /*
     * Set session timeout and enable tickets.
     */
    bool set_session_timeout(int lifetime = -1) {
      SSL_CTX *ctx = context.native_handle();

      assert(ctx);
      if (lifetime > 0) {
        ticket_.bind(ctx, lifetime);
      }
      return false;
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

#if (XXX)
    void start() {
        if (SSL_CTX_set_tlsext_servername_callback(*ssl_ctx, tls_servername_cb) != 1) {
            tls_set_error(ctx, "failed to set servername callback");
            goto err;
        }
        if (SSL_CTX_set_tlsext_servername_arg(*ssl_ctx, ctx) != 1) {
            tls_set_error(ctx, "failed to set servername callback arg");
            goto err;
        }
    }
#endif

    void stop() noexcept {
      std::lock_guard<std::mutex> lock(start_stop_mutex);

//    running_ = 0;
      close_connections();

      if(internal_io_service)
        io_service->stop();
    }

private:
    CertStore::ssl_ticket ticket_;

#if (XXX)
    static int servername_cb(SSL *ssl, int *al, void *arg) {
	struct tls *ctx = (struct tls *)arg;
	struct tls_sni_ctx *sni_ctx;
	union tls_addr addrbuf;
	struct tls *conn_ctx;
	const char *name;
	int match;

	if ((conn_ctx = SSL_get_app_data(ssl)) == NULL)
		goto err;

	if ((name = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name)) == NULL) {
		/*
		 * The servername callback gets called even when there is no
		 * TLS servername extension provided by the client. Sigh!
		 */
		return (SSL_TLSEXT_ERR_NOACK);
	}

	/* Per RFC 6066 section 3: ensure that name is not an IP literal. */
	if (inet_pton(AF_INET, name, &addrbuf) == 1 ||
            inet_pton(AF_INET6, name, &addrbuf) == 1)
		goto err;

	free((char *)conn_ctx->servername);
	if ((conn_ctx->servername = strdup(name)) == NULL)
		goto err;

	/* Find appropriate SSL context for requested servername. */
	for (sni_ctx = ctx->sni_ctx; sni_ctx != NULL; sni_ctx = sni_ctx->next) {
		if (tls_check_name(ctx, sni_ctx->ssl_cert, name,
		    &match) == -1)
			goto err;
		if (match) {
			SSL_set_SSL_CTX(conn_ctx->ssl_conn, sni_ctx->ssl_ctx);
			return (SSL_TLSEXT_ERR_OK);
		}
	}

	/* No match, use the existing context/certificate. */
	return (SSL_TLSEXT_ERR_OK);

 err:
	/*
	 * There is no way to tell libssl that an internal failure occurred.
	 * The only option we have is to return a fatal alert.
	 */
	*al = TLS1_AD_INTERNAL_ERROR;
	return (SSL_TLSEXT_ERR_ALERT_FATAL);
    }
#endif

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
