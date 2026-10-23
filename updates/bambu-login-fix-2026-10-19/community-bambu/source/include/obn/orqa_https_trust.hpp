#pragma once

#include <curl/curl.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <memory>

namespace obn::tls {

// The Windows OpenSSL curl build has no system trust store. Reuse the
// Mozilla roots already compiled into this backend for cloud MQTT. Loading
// directly avoids a shared temporary CA file during concurrent HTTP requests.
inline CURLcode load_http_roots(CURL*, void* context, void* pem)
{
    const std::unique_ptr<BIO, decltype(&BIO_free)> input(
        BIO_new_mem_buf(pem, -1), BIO_free);
    if (!input) return CURLE_OUT_OF_MEMORY;
    X509_STORE* store = SSL_CTX_get_cert_store(static_cast<SSL_CTX*>(context));
    if (!store) return CURLE_SSL_CACERT_BADFILE;
    int loaded = 0;
    while (X509* cert = PEM_read_bio_X509(input.get(), nullptr, nullptr, nullptr)) {
        const std::unique_ptr<X509, decltype(&X509_free)> owned(cert, X509_free);
        if (X509_STORE_add_cert(store, cert) == 1) ++loaded;
    }
    // PEM_read_bio_X509 ends with a harmless no-more-PEM error.
    ERR_clear_error();
    return loaded > 0 ? CURLE_OK : CURLE_SSL_CACERT_BADFILE;
}

inline void configure_http_roots(CURL* curl, const char* pem)
{
    curl_easy_setopt(curl, CURLOPT_CAINFO, nullptr);
    curl_easy_setopt(curl, CURLOPT_CAPATH, nullptr);
    curl_easy_setopt(curl, CURLOPT_SSL_CTX_FUNCTION, load_http_roots);
    curl_easy_setopt(curl, CURLOPT_SSL_CTX_DATA, pem);
    // Certificate chain and hostname verification remain enabled.
}

} // namespace obn::tls
