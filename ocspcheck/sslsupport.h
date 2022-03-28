/*
//  ocspcheck -- CertStore support
*/

#if defined(__cplusplus)
extern "C" {
#endif

extern int certstore_cacertificates(X509_STORE *store, const char *file);
extern STACK_OF(X509) *certstore_certificate_chain(const char *file, int *count);

#if defined(__cplusplus)
}
#endif

//end
