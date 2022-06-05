#pragma once
#ifndef WS_SSLUTIL_H_INCLUDED
#define WS_SSLUTIL_H_INCLUDED
//  -*- mode: c; indent-width: 8; -*-
//
//  WebSocket SSL util functions
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

#undef X509_NAME
#undef X509_EXTENSIONS

#include <ctime>
#include <cctype>

#include <algorithm>
#include <vector>
#include <string>
#include <memory>

#include <openssl/ssl.h>
#include <openssl/evp.h>
#include <openssl/bio.h>

class sslutil {
public:
        class ScopedBIOStream {
                ScopedBIOStream(const ScopedBIOStream &) = delete;
                ScopedBIOStream& operator=(const ScopedBIOStream &) = delete;

        public:
                ScopedBIOStream() : bio_(BIO_new(BIO_s_mem())) {
                        buffer_[0] = 0;
                }
                ~ScopedBIOStream() {
                        if (bio_) BIO_free(bio_);
                }
                BIO *stream() {
                        return bio_;
                }
                int read(char *data, int datalen) {
                        int len = (bio_ ? BIO_pending(bio_) : 0);
                        if (datalen && len > 0) {
                                if (--datalen /*nul*/ > len) {
                                        len = BIO_read(bio_, data, datalen);
                                        if (datalen > 3) { // overflow marker
                                                memcpy(data + (datalen - 3), "...", 4);
                                        }
                                } else {
                                        len = BIO_read(bio_, data, datalen);
                                }
                                data[len] = 0;  // null terminate
                        }
                        return len;
                }
                const char *get() {
                        (void) read(buffer_, sizeof(buffer_));
                        return buffer_;
                }
                void reset() {
                        BIO_reset(bio_);
                }
        private:
                char buffer_[1024];
                BIO *bio_;
        };

public:
        // Certificate summary details
        //
        //  Connection: 2.1, cipher xxx xxx, 3 bit xxx
        //
        template <typename Output>
        static void summary(Output &output, const SSL *ssl, const char *prefix = "Connection: ")
        {
                X509 *peer = SSL_get_peer_certificate(ssl);
                const SSL_CIPHER *cipher = SSL_get_current_cipher(ssl);
                char message[1024];
                int len;

                // cipher information
                len = _snprintf(message, sizeof(message)-32, "%s%s, cipher %s %s",
                            (prefix ? prefix : ""), SSL_get_version(ssl),
                                (cipher ? SSL_CIPHER_get_version(cipher) : "-1"), SSL_CIPHER_get_name(cipher) /*(NONE) is null*/);
                if (len < 0) {
                        len = sizeof(message)-32; // overflow
                }

                // public key
                if (EVP_PKEY *pkey = (peer ? X509_get_pubkey(peer) : 0))  {
                        const int pkey_type = EVP_PKEY_base_id(pkey);
                        RSA *rsa = NULL;
                        DSA *dsa = NULL;

                        if (pkey_type == EVP_PKEY_RSA && (rsa = EVP_PKEY_get0_RSA(pkey)) != NULL) {
                                _snprintf(message + len, sizeof(message) - len, ", %d bit RSA", RSA_size(rsa));

                        } else if (pkey_type == EVP_PKEY_DSA && (dsa = EVP_PKEY_get0_DSA(pkey)) != NULL) {
                                _snprintf(message + len, sizeof(message) - len, ", %d bit DSA", DSA_size(dsa));

                        } else {
                                _snprintf(message + len, sizeof(message) - len, ", %d bits non-RSA/DSA", EVP_PKEY_bits(pkey));
                        }
                        EVP_PKEY_free(pkey);
                }

                message[sizeof(message)-1]=0;
                output(0, message);

                // peer certificate; if available
                if (peer)
                {
                        certificate(output, peer);
                        X509_free(peer);
                }
        }

private:
        // Certificate summary details
        //
        //  Issuer:  xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
        //  Subject: xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
        //  Active:  mmm dd hh:mm:ss yyyy tzz, Expires: mmm dd hh:mm:ss yyyy tzz
        //  Hash:    xx:xx:xx:x:xx:x:x:x:x:x:x:x:x:x:x:x:x:xx:xx:xx
        //
        template <typename Output>
        static void certificate(Output &output, const X509 *x509)
        {
                ScopedBIOStream biostream;
                BIO *bio = biostream.stream();

                if (nullptr == x509 || nullptr == bio)
                        return;

                // line 1
                BIO_puts(bio, "Issuer:  ");
                X509_NAME_print(bio, X509_get_issuer_name(x509), XN_FLAG_ONELINE);
                output(1, biostream.get());
                biostream.reset();

                // line 2
                BIO_puts(bio, "Subject: ");
                X509_NAME_print(bio, X509_get_subject_name(x509), XN_FLAG_ONELINE);
                output(2, biostream.get());
                biostream.reset();

                // line 3
                BIO_puts(bio, "Active:  ");
                ASN1_TIME_print(bio, X509_get0_notBefore(x509));
                BIO_puts(bio, ", Expires: ");
                ASN1_TIME_print(bio, X509_get0_notAfter(x509));
                output(3, biostream.get());
                biostream.reset();

                // line 4
                unsigned char sha1buffer[256];
                unsigned sha1size = sizeof(sha1buffer);

                X509_digest(x509, EVP_get_digestbyname("sha1"), sha1buffer, &sha1size);
                if (char *fingerprint = bin_to_hex(sha1buffer, sha1size, ':')) {
                        BIO_puts(bio, "Hash:    ");
                        BIO_puts(bio, fingerprint);
                        output(4, biostream.get());
                        free(fingerprint);
                }
        }

private:
        // binary to hexidecimal
        static char *bin_to_hex(const void *bin, unsigned length, char delimiter = ' ')
        {
                if (delimiter && length) {
                        char *buffer = (char *)::calloc((length * 3) + 1 /*nul*/, 1);
                        if (char *cursor = buffer) {
                                for (const unsigned char *it((const unsigned char *)bin), *end(it + length); it != end;) {
                                        unsigned char v = *it++;
                                        *cursor++ = ashex(v >> 4);
                                        *cursor++ = ashex(v & 0xf);
                                        *cursor++ = delimiter;
                                }
                                cursor[-1] = 0; // null terminate; replace trailing delimiter
                                return buffer;
                        }

                } else {
                        char *buffer = (char *)::calloc((length * 2) + 1 /*nul*/, 1);
                        if (char *cursor = buffer) {
                                for (const unsigned char *it((const unsigned char *)bin), *end(it + length); it != end;) {
                                        unsigned char v = *it++;
                                        *cursor++ = ashex(v >> 4);
                                        *cursor++ = ashex(v & 0xf);
                                }
                                *cursor = 0;    // null terminate
                                return buffer;
                        }
                }
                return nullptr;
        }

        static inline unsigned char
        ashex(unsigned char x)
        {
                return "0123456789abcdef"[x];
        }
};

#endif //WS_SSLUTIL_H_INCLUDED
