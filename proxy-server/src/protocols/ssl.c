#include "../../include/proxy.h"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

/* SSL context structure */
typedef struct ssl_context {
    SSL_CTX *ctx;
    SSL *ssl;
    int is_server;
} ssl_context_t;

static SSL_CTX *g_server_ssl_ctx = NULL;
static pthread_mutex_t g_ssl_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Initialize OpenSSL library */
int ssl_init(void) {
    if (!g_config.enable_ssl) {
        return 0;
    }

    /* Initialize OpenSSL */
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    printf("[INFO] OpenSSL initialized: %s\n", SSLeay_version(SSLEAY_VERSION));
    return 0;
}

/* Create SSL context for server */
int ssl_create_server_context(const char *cert_file, const char *key_file) {
    const SSL_METHOD *method;

    pthread_mutex_lock(&g_ssl_mutex);

    /* Create SSL context */
    method = TLS_server_method();
    g_server_ssl_ctx = SSL_CTX_new(method);

    if (!g_server_ssl_ctx) {
        ERR_print_errors_fp(stderr);
        pthread_mutex_unlock(&g_ssl_mutex);
        return -1;
    }

    /* Set minimum TLS version to TLS 1.2 */
    SSL_CTX_set_min_proto_version(g_server_ssl_ctx, TLS1_2_VERSION);

    /* Load certificate */
    if (SSL_CTX_use_certificate_file(g_server_ssl_ctx, cert_file,
                                     SSL_FILETYPE_PEM) <= 0) {
        fprintf(stderr, "[ERROR] Failed to load certificate: %s\n", cert_file);
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(g_server_ssl_ctx);
        g_server_ssl_ctx = NULL;
        pthread_mutex_unlock(&g_ssl_mutex);
        return -1;
    }

    /* Load private key */
    if (SSL_CTX_use_PrivateKey_file(g_server_ssl_ctx, key_file,
                                    SSL_FILETYPE_PEM) <= 0) {
        fprintf(stderr, "[ERROR] Failed to load private key: %s\n", key_file);
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(g_server_ssl_ctx);
        g_server_ssl_ctx = NULL;
        pthread_mutex_unlock(&g_ssl_mutex);
        return -1;
    }

    /* Verify private key matches certificate */
    if (!SSL_CTX_check_private_key(g_server_ssl_ctx)) {
        fprintf(stderr, "[ERROR] Private key does not match certificate\n");
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(g_server_ssl_ctx);
        g_server_ssl_ctx = NULL;
        pthread_mutex_unlock(&g_ssl_mutex);
        return -1;
    }

    /* Set cipher suites */
    SSL_CTX_set_cipher_list(g_server_ssl_ctx,
        "ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-GCM-SHA256:"
        "ECDHE-RSA-AES256-SHA384:ECDHE-RSA-AES128-SHA256");

    pthread_mutex_unlock(&g_ssl_mutex);

    printf("[INFO] SSL server context created\n");
    printf("[INFO]   Certificate: %s\n", cert_file);
    printf("[INFO]   Private Key: %s\n", key_file);

    return 0;
}

/* Create SSL connection for client */
ssl_context_t* ssl_create_client_context(int sockfd) {
    const SSL_METHOD *method;
    SSL_CTX *ctx;
    SSL *ssl;

    /* Create client SSL context */
    method = TLS_client_method();
    ctx = SSL_CTX_new(method);

    if (!ctx) {
        ERR_print_errors_fp(stderr);
        return NULL;
    }

    /* Set minimum TLS version */
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);

    /* Set verification mode (don't verify for proxy use) */
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);

    /* Create SSL object */
    ssl = SSL_new(ctx);
    if (!ssl) {
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return NULL;
    }

    /* Attach to socket */
    SSL_set_fd(ssl, sockfd);

    /* Connect */
    if (SSL_connect(ssl) <= 0) {
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        return NULL;
    }

    /* Create context wrapper */
    ssl_context_t *ssl_ctx = malloc(sizeof(ssl_context_t));
    if (!ssl_ctx) {
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        return NULL;
    }

    ssl_ctx->ctx = ctx;
    ssl_ctx->ssl = ssl;
    ssl_ctx->is_server = 0;

    return ssl_ctx;
}

/* Accept SSL connection on server socket */
ssl_context_t* ssl_accept_connection(int sockfd) {
    if (!g_server_ssl_ctx) {
        return NULL;
    }

    pthread_mutex_lock(&g_ssl_mutex);

    /* Create SSL object */
    SSL *ssl = SSL_new(g_server_ssl_ctx);
    if (!ssl) {
        ERR_print_errors_fp(stderr);
        pthread_mutex_unlock(&g_ssl_mutex);
        return NULL;
    }

    pthread_mutex_unlock(&g_ssl_mutex);

    /* Attach to socket */
    SSL_set_fd(ssl, sockfd);

    /* Accept SSL connection */
    if (SSL_accept(ssl) <= 0) {
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        return NULL;
    }

    /* Create context wrapper */
    ssl_context_t *ssl_ctx = malloc(sizeof(ssl_context_t));
    if (!ssl_ctx) {
        SSL_free(ssl);
        return NULL;
    }

    ssl_ctx->ctx = NULL; /* Server uses global context */
    ssl_ctx->ssl = ssl;
    ssl_ctx->is_server = 1;

    return ssl_ctx;
}

/* SSL read */
ssize_t ssl_read(ssl_context_t *ssl_ctx, void *buf, size_t len) {
    if (!ssl_ctx || !ssl_ctx->ssl) {
        return -1;
    }

    int n = SSL_read(ssl_ctx->ssl, buf, len);
    if (n < 0) {
        int err = SSL_get_error(ssl_ctx->ssl, n);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            errno = EAGAIN;
            return -1;
        }
        ERR_print_errors_fp(stderr);
        return -1;
    }

    return n;
}

/* SSL write */
ssize_t ssl_write(ssl_context_t *ssl_ctx, const void *buf, size_t len) {
    if (!ssl_ctx || !ssl_ctx->ssl) {
        return -1;
    }

    int n = SSL_write(ssl_ctx->ssl, buf, len);
    if (n < 0) {
        int err = SSL_get_error(ssl_ctx->ssl, n);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            errno = EAGAIN;
            return -1;
        }
        ERR_print_errors_fp(stderr);
        return -1;
    }

    return n;
}

/* Get peer certificate info */
int ssl_get_peer_cert_info(ssl_context_t *ssl_ctx, char *buf, size_t buf_size) {
    if (!ssl_ctx || !ssl_ctx->ssl) {
        return -1;
    }

    X509 *cert = SSL_get_peer_certificate(ssl_ctx->ssl);
    if (!cert) {
        snprintf(buf, buf_size, "No certificate");
        return -1;
    }

    /* Get subject */
    X509_NAME *subject = X509_get_subject_name(cert);
    char subject_buf[256];
    X509_NAME_oneline(subject, subject_buf, sizeof(subject_buf));

    /* Get issuer */
    X509_NAME *issuer = X509_get_issuer_name(cert);
    char issuer_buf[256];
    X509_NAME_oneline(issuer, issuer_buf, sizeof(issuer_buf));

    snprintf(buf, buf_size, "Subject: %s, Issuer: %s", subject_buf, issuer_buf);

    X509_free(cert);
    return 0;
}

/* Get cipher info */
const char* ssl_get_cipher_info(ssl_context_t *ssl_ctx) {
    if (!ssl_ctx || !ssl_ctx->ssl) {
        return "None";
    }

    return SSL_get_cipher(ssl_ctx->ssl);
}

/* Shutdown SSL connection */
void ssl_shutdown(ssl_context_t *ssl_ctx) {
    if (!ssl_ctx) {
        return;
    }

    if (ssl_ctx->ssl) {
        SSL_shutdown(ssl_ctx->ssl);
        SSL_free(ssl_ctx->ssl);
    }

    if (ssl_ctx->ctx && !ssl_ctx->is_server) {
        SSL_CTX_free(ssl_ctx->ctx);
    }

    free(ssl_ctx);
}

/* Generate self-signed certificate */
int ssl_generate_self_signed_cert(const char *cert_file, const char *key_file) {
    EVP_PKEY *pkey = NULL;
    X509 *x509 = NULL;
    RSA *rsa = NULL;
    X509_NAME *name = NULL;
    FILE *fp = NULL;
    int ret = -1;

    /* Generate RSA key */
    rsa = RSA_new();
    BIGNUM *bn = BN_new();
    if (!BN_set_word(bn, RSA_F4)) {
        goto cleanup;
    }

    if (!RSA_generate_key_ex(rsa, 2048, bn, NULL)) {
        goto cleanup;
    }

    /* Create EVP_PKEY */
    pkey = EVP_PKEY_new();
    if (!EVP_PKEY_assign_RSA(pkey, rsa)) {
        goto cleanup;
    }
    rsa = NULL; /* pkey owns rsa now */

    /* Create X509 certificate */
    x509 = X509_new();
    if (!x509) {
        goto cleanup;
    }

    /* Set version */
    X509_set_version(x509, 2);

    /* Set serial number */
    ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);

    /* Set validity period */
    X509_gmtime_adj(X509_get_notBefore(x509), 0);
    X509_gmtime_adj(X509_get_notAfter(x509), 365L * 24 * 60 * 60); /* 1 year */

    /* Set public key */
    X509_set_pubkey(x509, pkey);

    /* Set subject name */
    name = X509_get_subject_name(x509);
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC,
                               (unsigned char *)"US", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC,
                               (unsigned char *)"ProxyMax", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               (unsigned char *)"localhost", -1, -1, 0);

    /* Set issuer name (self-signed) */
    X509_set_issuer_name(x509, name);

    /* Sign certificate */
    if (!X509_sign(x509, pkey, EVP_sha256())) {
        goto cleanup;
    }

    /* Write private key */
    fp = fopen(key_file, "wb");
    if (!fp) {
        goto cleanup;
    }

    if (!PEM_write_PrivateKey(fp, pkey, NULL, NULL, 0, NULL, NULL)) {
        goto cleanup;
    }

    fclose(fp);
    fp = NULL;

    /* Write certificate */
    fp = fopen(cert_file, "wb");
    if (!fp) {
        goto cleanup;
    }

    if (!PEM_write_X509(fp, x509)) {
        goto cleanup;
    }

    printf("[INFO] Generated self-signed certificate:\n");
    printf("[INFO]   Certificate: %s\n", cert_file);
    printf("[INFO]   Private Key: %s\n", key_file);

    ret = 0;

cleanup:
    if (fp) fclose(fp);
    if (x509) X509_free(x509);
    if (pkey) EVP_PKEY_free(pkey);
    if (rsa) RSA_free(rsa);
    if (bn) BN_free(bn);

    return ret;
}

/* Cleanup SSL */
void ssl_cleanup(void) {
    if (!g_config.enable_ssl) {
        return;
    }

    pthread_mutex_lock(&g_ssl_mutex);

    if (g_server_ssl_ctx) {
        SSL_CTX_free(g_server_ssl_ctx);
        g_server_ssl_ctx = NULL;
    }

    pthread_mutex_unlock(&g_ssl_mutex);

    EVP_cleanup();
    ERR_free_strings();

    printf("[INFO] SSL/TLS system cleaned up\n");
}
