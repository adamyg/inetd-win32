#ifndef SIMPLE_WEB_CRYPTO_WIN32_HPP
#define SIMPLE_WEB_CRYPTO_WIN32_HPP

#include <cassert>
#include <iomanip>
#include <istream>
#include <sstream>
#include <string>

#pragma comment (lib, "Crypt32.lib")

namespace SimpleWeb {
  class Crypto {
    const static std::size_t buffer_size = 131072;

    struct ScopedProvider {
      ScopedProvider() : handle(0) { }
      ~ScopedProvider() {
        if (handle) ::CryptReleaseContext(handle, 0);
      }
      HCRYPTPROV handle;
    } provider;

    struct ScopedHash {
      ScopedHash() : handle(0) { }
      ~ScopedHash() {
        if (handle) ::CryptDestroyHash(handle);
      }
      HCRYPTHASH handle; 
    } hash;

  public:
    class Base64 {
    public:
      /// Returns Base64 encoded string from input string.
      static std::string encode(const std::string &msg) {
        DWORD length = (msg.length() * 2) + 4;
        std::string base64(length, '\0');

        if (::CryptBinaryToStringA((const BYTE *)msg.data(), msg.length(),
                    CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF|CRYPT_STRING_NOCR, (char *)base64.data(), &length)) {
          base64.resize(length);
        } else {
          base64.clear();
        }
        return base64;
      }

      /// Returns Base64 decoded string from base64 input.
      static std::string decode(const std::string &base64) {
        DWORD length = (6 * base64.size()) / 8;
        std::string ascii(length, '\0');

        if (::CryptStringToBinaryA((const char *)base64.data(), base64.length(),
                    CRYPT_STRING_BASE64, (BYTE *)ascii.data(), &length, nullptr, nullptr)) {
          ascii.resize(length);
        } else {
          ascii.clear();
        }
        return ascii;
      }
    };

    static std::string sha1(const std::string &s) {
#define SHA1Length      20
      ScopedProvider provider;
      ScopedHash hash;

      if (::CryptAcquireContextA(&provider.handle, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT) &&
          ::CryptCreateHash(provider.handle, CALG_SHA1, 0, 0, &hash.handle) &&
            ::CryptHashData(hash.handle, reinterpret_cast<CONST BYTE*>(s.data()), static_cast<DWORD>(s.length()), 0)) {

        DWORD hash_len = 0, buffer_size = sizeof hash_len;
        if (::CryptGetHashParam(hash.handle, HP_HASHSIZE, reinterpret_cast<unsigned char*>(&hash_len), &buffer_size, 0) && 
                hash_len == SHA1Length) {
          BYTE result[SHA1Length];
          if (::CryptGetHashParam(hash.handle, HP_HASHVAL, result, &hash_len, 0) && 
                hash_len == SHA1Length) {
            return std::string((const char *)result, SHA1Length);
          }
        }
      }
      return std::string(SHA1Length, '\0');
    }

    /// Returns hex string from bytes in input string.
    static std::string to_hex_string(const std::string &input) noexcept {
      std::stringstream hex_stream;
      hex_stream << std::hex << std::internal << std::setfill('0');
      for(auto &byte : input)
        hex_stream << std::setw(2) << static_cast<int>(static_cast<unsigned char>(byte));
      return hex_stream.str();
    }

#if defined(_DEBUG)
    static void unit_tests() {
      const struct base64_test { const char *first; const char *second; } base64_string_tests[] = {
        {"", ""},
        {"f", "Zg=="},
        {"fo", "Zm8="},
        {"foo", "Zm9v"},
        {"foob", "Zm9vYg=="},
        {"fooba", "Zm9vYmE="},
        {"foobar", "Zm9vYmFy"},
        {"The itsy bitsy spider climbed up the waterspout.\r\nDown came the rain\r\nand washed the spider out.\r\nOut came the sun\r\nand dried up all the rain\r\nand the itsy bitsy spider climbed up the spout again.",
         "VGhlIGl0c3kgYml0c3kgc3BpZGVyIGNsaW1iZWQgdXAgdGhlIHdhdGVyc3BvdXQuDQpEb3duIGNhbWUgdGhlIHJhaW4NCmFuZCB3YXNoZWQgdGhlIHNwaWRlciBvdXQuDQpPdXQgY2FtZSB0aGUgc3VuDQphbmQgZHJpZWQgdXAgYWxsIHRoZSByYWluDQphbmQgdGhlIGl0c3kgYml0c3kgc3BpZGVyIGNsaW1iZWQgdXAgdGhlIHNwb3V0IGFnYWluLg=="}};

      const struct sha1_test { const char *first; const char *second; } sha1_string_tests[] = {
        {"", "da39a3ee5e6b4b0d3255bfef95601890afd80709"},
        {"The quick brown fox jumps over the lazy dog", "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12"}};

      for (auto *it(base64_string_tests), *end(it + _countof(base64_string_tests)); it != end; ++it) {
        assert(Crypto::Base64::encode(it->first)  == it->second);
        assert(Crypto::Base64::decode(it->second) == it->first);
      }

      for (auto *it(sha1_string_tests), *end(it + _countof(sha1_string_tests)); it != end; ++it) {
        assert(Crypto::to_hex_string(Crypto::sha1(it->first)) == it->second);
        std::string ss(it->first);
        assert(Crypto::to_hex_string(Crypto::sha1(ss)) == it->second);
      }
    }
#endif //_DEBUG

  }; // namespace Crypto
}; // namespace SimpleWeb

#endif /* SIMPLE_WEB_CRYPTO_WIN32_HPP */
