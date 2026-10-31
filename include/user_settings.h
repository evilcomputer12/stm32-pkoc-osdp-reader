#ifndef READER_WOLFCRYPT_USER_SETTINGS_H
#define READER_WOLFCRYPT_USER_SETTINGS_H
// Suppress Arduino auto-options: they force ECC and disable ASN.1 time checks.
#undef WOLFSSL_ARDUINO
// Based on wolfSSL's user_settings_min_ecc.h; runtime uses external ATECC
// verification, SHA-256 and AES-128. Software ECC/X.509 is a separate exercise.
#define WOLFCRYPT_ONLY
#define SINGLE_THREADED
#define NO_FILESYSTEM
#define NO_WRITEV
#define NO_DEV_RANDOM
#define NO_WOLFSSL_DIR
#define NO_ERROR_STRINGS
#define WOLFSSL_NO_PEM
#define NO_RSA
#define NO_DSA
#define NO_DH
#define NO_RC4
#define NO_DES3
#define NO_MD4
#define NO_MD5
#define NO_SHA
#define NO_SHA512
#define NO_SHA3
#define NO_PWDBASED
#define NO_PKCS12
#define NO_PSK
#define NO_SIG_WRAPPER
#define WOLFSSL_AES_SMALL_TABLES
#define WOLFSSL_AES_NO_UNROLL
#define NO_AES_192
#define NO_AES_256
#define NO_AES_CBC
#define HAVE_AES_ECB
#define WOLFSSL_AES_DIRECT
#define WOLFSSL_SHA256_SMALL

#ifndef READER_HOST_CRYPTO_TEST
#define CUSTOM_RAND_GENERATE_BLOCK reader_rng_block
#define CUSTOM_RAND_GENERATE_SEED reader_rng_block
#ifdef __cplusplus
extern "C" {
#endif
int reader_rng_block(unsigned char *out, unsigned int length);
#ifdef __cplusplus
}
#endif
#else
// Host uses the operating system CSPRNG. No deterministic production entropy.
#undef NO_DEV_RANDOM
#undef NO_FILESYSTEM // Host-only /dev/urandom access; MCU remains filesystem-free.
#endif

#ifdef READER_CRYPTO_EXERCISE
#define HAVE_ECC
#define ECC_USER_CURVES
#define HAVE_COMP_KEY
#define WOLFSSL_PUBLIC_MP
#define WOLFSSL_SP
#define WOLFSSL_SP_SMALL
#define WOLFSSL_SP_MATH
#define WOLFSSL_HAVE_SP_ECC
#define ECC_TIMING_RESISTANT
#define TFM_TIMING_RESISTANT
#define WOLFSSL_VALIDATE_ECC_IMPORT
#define WOLFSSL_SMALL_CERT_VERIFY
#if defined(READER_HOST_CRYPTO_TEST)
#include <time.h>
#define XTIME reader_test_time
#ifdef __cplusplus
extern "C" {
#endif
time_t reader_test_time(time_t *out);
#ifdef __cplusplus
}
#endif
#endif
// ASN.1 and certificate support intentionally remain enabled for the exercise.
#else
#define NO_BIG_INT
#define NO_ASN
#define NO_CERTS
#define NO_CODING
#endif
#endif
