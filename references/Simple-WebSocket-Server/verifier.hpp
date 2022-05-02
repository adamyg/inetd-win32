#ifndef SIMPLE_WEB_VERIFIER_HPP
#define SIMPLE_WEB_VERIFIER_HPP

#ifdef ASIO_STANDALONE
#include <asio/ssl.hpp>
#else
#include <boost/asio/ssl.hpp>
#endif

namespace SimpleWeb {
  template <typename Implementation>
  class Verifier {
    Verifier& operator=(const Verifier &) = delete;

  public:
    Verifier(bool verify_peer, unsigned verify_depth, bool self_signed, Implementation implementation)
        : verify_peer_(verify_peer), verify_depth_(verify_depth), self_signed_(self_signed), implementation_(implementation) {
    }

    bool operator()(bool preverified, asio::ssl::verify_context& ctx) {
      X509_STORE_CTX *x509 = ctx.native_handle();
      X509* cert = X509_STORE_CTX_get_current_cert(x509);
      int err = X509_STORE_CTX_get_error(x509);
      int depth = X509_STORE_CTX_get_error_depth(x509);
      bool is_self_signed = false;

      if (preverified && verify_depth_) {
        /* OpenSSL manpage
         * Catch a too long certificate chain. The depth limit set using SSL_CTX_set_verify_depth()
         * is by purpose set to "limit+1" so that whenever the "depth>verify_depth" condition is met, we
         * have violated the limit and want to log this error condition.
         * We must do it here, because the CHAIN_TOO_LONG error would not be found explicitly; 
         * only errors introduced by cutting off the additional certificates would be logged.
         */
        if (depth > 0 && depth > (int)verify_depth_) {
          err = X509_V_ERR_CERT_CHAIN_TOO_LONG;
          X509_STORE_CTX_set_error(x509, err);
          preverified = false;
        }
      }

      if (self_signed_) {
        /* Mask self-signed error conditions */
        if (! preverified &&
                (err == X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT || err == X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN)) {
          is_self_signed = true;
          preverified = true;
        }
      }

      const bool verified = implementation_(preverified, ctx);

//    DIAGIO_STREAM_INFO()

      if (! verified || is_self_signed) {
        char buf[256];

        X509_NAME_oneline(X509_get_issuer_name(cert), buf, sizeof(buf));
//      DIAGIO_STREAM() << "Certificate: " << buf << DIAGIO_STREAM_FLUSH();
//      if (is_self_signed && verified)
//        DIAGIO_STREAM() << "Warning: Self-signed certificate" << DIAGIO_STREAM_FLUSH();
      }

      if (! verified) {
        if (err > 0) {
          if (depth > 0) {
//          DIAGIO_STREAM() << "Server verify error: "
//              << X509_verify_cert_error_string(err) << " (" << err << ") at depth " << depth << DIAGIO_STREAM_FLUSH();
          } else {
//          DIAGIO_STREAM() << "Server verify error: "
//              << X509_verify_cert_error_string(err) << " (" << err << ")" << DIAGIO_STREAM_FLUSH();
          }
        } else {
//        DIAGIO_STREAM() 
//           << "Server verify error: undefined" << DIAGIO_STREAM_FLUSH();
        }
//      if (! verify_peer_) //SSL_VERIFY_NONE
//          DIAGIO_STREAM() << "Warning: Report only, certificate status ignored" << DIAGIO_STREAM_FLUSH();
      }

//    DIAGIO_STREAM_END()
      return verified;
    }

  private:
    bool verify_peer_;
    unsigned verify_depth_;
    bool self_signed_;
    Implementation implementation_;
  };

  class basic_verification {
  public:
    bool operator()(bool preverified, asio::ssl::verify_context& ctx) {
      return preverified;
    }
  };

  template <typename Implementation>
  Verifier<Implementation>
  make_verifier(bool verify_peer, int verify_depth, bool self_signed, Implementation implementation) {
    return Verifier<Implementation>(verify_peer, verify_depth, self_signed, implementation);
  }

};  //namespace SimpleWeb

#endif  //SIMPLE_WEB_VERIFIER_HPP
