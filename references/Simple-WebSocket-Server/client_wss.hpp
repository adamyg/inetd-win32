#ifndef SIMPLE_WEB_CLIENT_WSS_HPP
#define SIMPLE_WEB_CLIENT_WSS_HPP

#include <functional>

#include "client_ws.hpp"
#include "verifier.hpp" //APY

#ifdef ASIO_STANDALONE
#include <asio/ssl.hpp>
#else
#include <boost/asio/ssl.hpp>
#endif

namespace SimpleWeb {
  using WSS = asio::ssl::stream<asio::ip::tcp::socket>;

  template <>
  class SocketClient<WSS> : public SocketClientBase<WSS> {
  public:
    // Different SSL verification modes.
    enum verify_modes { //APY
      none = 0,
      basic,
      rfc2818
    };

  public:
    /**
     * Constructs a client object.
     *
     * @param server_port_path   Server resource given by host[:port][/path]
     */
    SocketClient(const std::string &server_port_path) //APY
        : SocketClientBase<WSS>::SocketClientBase(server_port_path, 443), context(asio::ssl::context::tlsv12),
          verify_mode_(rfc2818), verify_depth_(0), self_signed_(false) {
    }

    /**
     * Constructs a client object.
     *
     * @param server_port_path   Server resource given by host[:port][/path]
     * @param verify_certificate Set to true (default) to verify the server's certificate and hostname according to RFC 2818.
     * @param certification_file If non-empty, sends the given certification file to server. Requires private_key_file.
     * @param private_key_file   If non-empty, specifies the file containing the private key for certification_file. Requires certification_file.
     * @param verify_file        If non-empty, use this certificate authority file to perform verification.
     */
    SocketClient(const std::string &server_port_path, bool verify_certificate /*= true, APY*/,
                 const std::string &certification_file = std::string(), const std::string &private_key_file = std::string(),
                 const std::string &verify_file = std::string())
        : SocketClientBase<WSS>::SocketClientBase(server_port_path, 443),
          context(asio::ssl::context::tlsv12),
          verify_mode_(none), verify_depth_(0), self_signed_(false) {
      set_certification(certification_file, private_key_file);

      if(verify_certificate) {
        verify_mode_ = rfc2818;
      }

      set_verify_certificates(verify_file);
    };

    /*
     * Set transport ciphers.
     *
     * @param ciphers            Cipher list specification; see OpenSSL for details.
     */
    bool set_cipher_list(const std::string &ciphers) { //APY
     return (/*failure*/ 0 != SSL_CTX_set_cipher_list(context.native_handle(), ciphers.c_str()));
    }

    /**
     * Set the client certificate source.
     *
     * @param certification_file If non-empty, sends the given certification file to server. Requires private_key_file.
     * @param private_key_file   If non-empty, specifies the file containing the private key for certification_file. Requires certification_file.
     */
    bool set_certification(const std::string &certification_file, const std::string &private_key_file = std::string()) { //APY
      if(certification_file.size() > 0 && private_key_file.size() > 0) {
        context.use_certificate_chain_file(certification_file);
        context.use_private_key_file(private_key_file, asio::ssl::context::pem);
      }
      return true;
    }

    /**
     * Set server verification certificate source.
     *
     * @param verify_file        If non-empty, use this certificate authority file to perform verification.
     */
    bool set_verify_certificates(const std::string &verify_file) { //APY
      if (verify_file.size() > 0)
        context.load_verify_file(verify_file);
      else
        context.set_default_verify_paths();
      return true;
    }

    /**
     * Set server verification options.
     *
     * @param verify_mode        Verification mode; default rfc2818.
     */
    bool set_verify_options(enum verify_modes verify_host = rfc2818, unsigned verify_depth = 0, bool self_signed = false) { //APY
      verify_mode_  = verify_host;
      verify_depth_ = verify_depth;
      self_signed_  = self_signed;
      return true;
    }

    void reconnect(std::shared_ptr<Connection> &connection) {
      connection->cancel_timeout();
      connection->set_timeout();
      connection->close();
      connect_impl(connection);
    }

  protected:
    asio::ssl::context context;
    enum verify_modes verify_mode_;
    unsigned verify_depth_;
    bool self_signed_;

    void connect() override {
      LockGuard connection_lock(connection_mutex);

      if (none == verify_mode_) { //APY
        context.set_verify_callback(make_verifier(false, verify_depth_, self_signed_, basic_verification()));
        context.set_verify_mode(asio::ssl::verify_none);
      } else {
        if (rfc2818 == verify_mode_) {
          context.set_verify_callback(make_verifier(true, verify_depth_, self_signed_, asio::ssl::rfc2818_verification(host)));
        } else {
          context.set_verify_callback(make_verifier(true, verify_depth_, self_signed_, basic_verification()));
        }
        if (verify_depth_)
         context.set_verify_depth(verify_depth_ + 1 /*See: OpenSSL BUGS*/);
        context.set_verify_mode(asio::ssl::verify_peer);
      }

      auto connection = this->connection = std::shared_ptr<Connection>(new Connection(handler_runner, config.timeout_idle, *io_service, context));
      connection_lock.unlock();
      connect_impl(connection);
    }

    void connect_impl(std::shared_ptr<Connection> &connection) {
      std::pair<std::string, std::string> host_port;
      if(config.proxy_server.empty())
        host_port = {host, std::to_string(port)};
      else {
        auto proxy_host_port = parse_host_port(config.proxy_server, 8080);
        host_port = {proxy_host_port.first, std::to_string(proxy_host_port.second)};
      }

      auto resolver = std::make_shared<asio::ip::tcp::resolver>(*io_service);
      connection->set_timeout(config.timeout_request);
      async_resolve(*resolver, host_port, [this, connection, resolver](const error_code &ec, resolver_results results) {
        connection->cancel_timeout();
        auto lock = connection->handler_runner->continue_lock();
        if(!lock)
          return;
        if(!ec) {
          connection->set_timeout(this->config.timeout_request);
          asio::async_connect(connection->socket->lowest_layer(), results, 
              std::bind(&SocketClient::attempt_callback, this, connection, 
                        std::placeholders::_1 /*asio::placeholders::error*/, std::placeholders::_2 /*asio::placeholders::iterator*/), //APY
              [this, connection, resolver](const error_code &ec, async_connect_endpoint /*endpoint*/) {
            connection->cancel_timeout();
            auto lock = connection->handler_runner->continue_lock();
            if(!lock)
              return;
            if(!ec) {
              asio::ip::tcp::no_delay option(true);
              error_code ec;
              connection->socket->lowest_layer().set_option(option, ec);

              if(!this->config.proxy_server.empty()) {
                auto streambuf = std::make_shared<asio::streambuf>();
                std::ostream ostream(streambuf.get());
                auto host_port = this->host + ':' + std::to_string(this->port);
                ostream << "CONNECT " + host_port + " HTTP/1.1\r\n"
                        << "Host: " << host_port << "\r\n";
                if(!this->config.proxy_auth.empty())
                  ostream << "Proxy-Authorization: Basic " << Crypto::Base64::encode(this->config.proxy_auth) << "\r\n";
                ostream << "\r\n";
                connection->set_timeout(this->config.timeout_request);
                asio::async_write(connection->socket->next_layer(), *streambuf, [this, connection, streambuf](const error_code &ec, std::size_t /*bytes_transferred*/) {
                  connection->cancel_timeout();
                  auto lock = connection->handler_runner->continue_lock();
                  if(!lock)
                    return;
                  if(!ec) {
                    connection->set_timeout(this->config.timeout_request);
                    connection->in_message = std::shared_ptr<InMessage>(new InMessage());
                    asio::async_read_until(connection->socket->next_layer(), connection->in_message->streambuf, "\r\n\r\n", [this, connection](const error_code &ec, std::size_t /*bytes_transferred*/) {
                      connection->cancel_timeout();
                      auto lock = connection->handler_runner->continue_lock();
                      if(!lock)
                        return;
                      if(!ec) {
                        if(!ResponseMessage::parse(*connection->in_message, connection->http_version, connection->status_code, connection->header))
                          this->connection_error(connection, make_error_code::make_error_code(errc::protocol_error));
                        else {
                          if(connection->status_code.compare(0, 3, "200") != 0)
                            this->connection_error(connection, make_error_code::make_error_code(errc::permission_denied));
                          else
                            this->handshake(connection);
                        }
                      }
                      else
                        this->connection_error(connection, ec);
                    });
                  }
                  else
                    this->connection_error(connection, ec);
                });
              }
              else
                this->handshake(connection);
            }
            else
              this->connection_error(connection, ec);
          });
        }
        else
          this->connection_error(connection, ec);
      });
    }

    asio::ip::tcp::resolver::iterator attempt_callback(std::shared_ptr<Connection> &connection, const error_code &ec, asio::ip::tcp::resolver::iterator next) { //APY
      this->connection_attempt(connection, ec, next);
      return next;
    }

    void handshake(const std::shared_ptr<Connection> &connection) {
      SSL_set_tlsext_host_name(connection->socket->native_handle(), this->host.c_str());

      connection->set_timeout(this->config.timeout_request);
      connection->socket->async_handshake(asio::ssl::stream_base::client, [this, connection](const error_code &ec) {
        connection->cancel_timeout();
        auto lock = connection->handler_runner->continue_lock();
        if(!lock)
          return;
        if(!ec)
          upgrade(connection);
        else
          this->connection_error(connection, ec);
      });
    }
  };
} // namespace SimpleWeb

#endif /* SIMPLE_WEB_CLIENT_WSS_HPP */