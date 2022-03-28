//
//  ocspcheck -- CertStore support
//

#if defined(_WIN32)
#if !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <Winsock2.h>
#include <Windows.h>
#undef X509_NAME                                // namespace issue
#endif

#include <openssl/ssl.h>

#if defined(_WIN32) && (HAVE_LIBCERTSTORE)
#if (OPENSSL_VERSION_NUMBER > 0x10100000L)      // build requirement
#define ENABLE_LIBCERTSTORE
#include <ssl_cacertificates.hpp>
#include <ssl_certificate_chain.hpp>
#include <ssl_sink.hpp>
#endif //OpenSSL 1.1.0+
#endif //_WIN32

#include "sslsupport.h"

#if defined(ENABLE_LIBCERTSTORE)
namespace {
    struct CertStoreSink : public CertStore::ICertSink {
        virtual void message(const char *msg) {
            std::cout << msg << std::endl;
        }
    };
}
#endif


extern "C" int
certstore_cacertificates(X509_STORE *store, const char *file)
{
#if defined(ENABLE_LIBCERTSTORE)
    CertStoreSink sink;
    return CertStore::ssl_cacertificates::load(sink, store, file);

#else
    return -1;      //not supported/error
#endif
}


extern "C" STACK_OF(X509) *
certstore_certificate_chain(const char *file, int *count)
{
#if defined(ENABLE_LIBCERTSTORE)
    STACK_OF(X509) *stack = NULL;

    if (NULL != (stack = sk_X509_new_null())) {
        const int result = CertStore::ssl_certificate_chain::load(stack, file);
        if (result >= 0) {
            *count = result;
            return stack;
        }
        sk_X509_pop_free(stack, X509_free);
    }
#endif

    return NULL;    //not supported/error
}

//end