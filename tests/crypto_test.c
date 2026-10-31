#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "wolfssl/wolfcrypt/settings.h"
#include "wolfssl/wolfcrypt/hash.h"
#include "wolfssl/wolfcrypt/ecc.h"
#include "wolfssl/wolfcrypt/asn.h"
#include "wolfssl/wolfcrypt/aes.h"
#include "crypto_fixtures.h"

// Test-only clock: 2026-11-01T00:00:00Z. Production has no fake clock.
time_t reader_test_time(time_t *out) { time_t t = 1793491200; if(out) *out=t; return t; }
static int valid_point(const unsigned char *p, unsigned n) {
    ecc_key key; int status;
    if ((n!=65 || p[0]!=4) && (n!=33 || (p[0]!=2 && p[0]!=3))) return 0;
    assert(wc_ecc_init(&key)==0);
    status=wc_ecc_import_x963_ex(p,n,&key,ECC_SECP256R1);
    if(!status) status=wc_ecc_check_key(&key);
    wc_ecc_free(&key); return status==0;
}
// Strict DER contract: accept canonical encoding only, with no trailing bytes.
static int raw_from_der(const unsigned char *der, unsigned n, unsigned char raw[64]) {
    unsigned char r[32],s[32],canonical[80]; word32 rn=32,sn=32,cn=sizeof(canonical);
    if(wc_ecc_sig_to_rs(der,n,r,&rn,s,&sn)!=0 || rn>32 || sn>32) return 0;
    memset(raw,0,64); memcpy(raw+32-rn,r,rn); memcpy(raw+64-sn,s,sn);
    if(wc_ecc_rs_raw_to_sig(raw,32,raw+32,32,canonical,&cn)!=0) return 0;
    return cn==n && memcmp(der,canonical,n)==0;
}
static int verify(const unsigned char *pub, const unsigned char *hash, const unsigned char *raw) {
    ecc_key key; unsigned char der[80]; word32 n=sizeof(der); int verified=0;
    if(!valid_point(pub,65)) return 0;
    assert(wc_ecc_init(&key)==0);
    int rc=wc_ecc_import_x963_ex(pub,65,&key,ECC_SECP256R1);
    if(!rc) rc=wc_ecc_rs_raw_to_sig(raw,32,raw+32,32,der,&n);
    if(!rc) rc=wc_ecc_verify_hash(der,n,hash,32,&verified,&key);
    wc_ecc_free(&key); return !rc && verified==1;
}
static int dates(const DecodedCert *c) {
    // These generated fixtures use short-form DER UTC time fields.
    if(c->beforeDateLen<2 || c->afterDateLen<2 || c->beforeDate[1]>=128 || c->afterDate[1]>=128) return 0;
    return wc_ValidateDateWithTime(c->beforeDate+2,c->beforeDate[0],ASN_BEFORE,reader_test_time(NULL),c->beforeDate[1]) &&
           wc_ValidateDateWithTime(c->afterDate+2,c->afterDate[0],ASN_AFTER,reader_test_time(NULL),c->afterDate[1]);
}
static int trusted_certificate(const unsigned char *der, unsigned n, const unsigned char *issuer) {
    DecodedCert c; wc_InitDecodedCert(&c,der,n,NULL);
    int ok=wc_ParseCert(&c,CERT_TYPE,NO_VERIFY,NULL)==0;
    // Explicit test trust store contains exactly this issuer key. This is
    // pinned-issuer validation, not general chain/path/revocation validation.
    if(ok) {
        ecc_key issuer_key;byte issuer_der[128];word32 issuer_size;
        assert(wc_ecc_init(&issuer_key)==0);
        int status=wc_ecc_import_x963_ex(issuer,65,&issuer_key,ECC_SECP256R1);
        int encoded=status ? status : wc_EccPublicKeyToDer(&issuer_key,issuer_der,sizeof(issuer_der),1);
        issuer_size=encoded>0 ? (word32)encoded : 0;
        wc_ecc_free(&issuer_key);
        int sig_status=issuer_size ? wc_CheckCertSigPubKey(der,n,NULL,issuer_der,issuer_size,ECDSAk) : -1;
        int date_status=dates(&c);
        ok=memcmp(issuer,test_pub_a,65)==0 && date_status && sig_status==0;
    }
    wc_FreeDecodedCert(&c); return ok;
}
int main(void) {
    byte out[256], bad[65], raw[64]; word32 n;
    WC_RNG rng; int rng_status=wc_InitRng(&rng);
    if(rng_status) fprintf(stderr,"Host RNG initialization error=%d\n",rng_status);
    assert(rng_status==0);
    assert(wc_Sha256Hash((const byte*)"abc",3,out)==0 && !memcmp(out,test_digest,32));
    // NIST FIPS 197 AES-128 known answer, encryption and decryption.
    const byte key[16]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    const byte plain[16]={0,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
    const byte cipher[16]={0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a};
    Aes aes; assert(wc_AesInit(&aes,NULL,INVALID_DEVID)==0);
    assert(wc_AesSetKey(&aes,key,16,NULL,AES_ENCRYPTION)==0 && wc_AesEcbEncrypt(&aes,out,plain,16)==0 && !memcmp(out,cipher,16));
    assert(wc_AesSetKey(&aes,key,16,NULL,AES_DECRYPTION)==0 && wc_AesEcbDecrypt(&aes,out,cipher,16)==0 && !memcmp(out,plain,16));
    wc_AesFree(&aes);

    ecc_key a,b,imported; byte private_a[32]={0}; private_a[31]=1;
    assert(wc_ecc_init(&a)==0 && wc_ecc_init(&b)==0 && wc_ecc_init(&imported)==0);
    assert(wc_ecc_import_private_key_ex(private_a,32,test_pub_a,65,&a,ECC_SECP256R1)==0);
    assert(wc_ecc_import_x963_ex(test_pub_b,65,&b,ECC_SECP256R1)==0);
    assert(wc_ecc_set_rng(&a,&rng)==0);
    n=33; assert(wc_ecc_export_x963_ex(&a,out,&n,1)==0 && n==33);
    assert(valid_point(out,n));
    assert(wc_ecc_import_x963_ex(out,n,&imported,ECC_SECP256R1)==0);
    n=65; assert(wc_ecc_export_x963(&imported,out,&n)==0 && n==65 && !memcmp(out,test_pub_a,65));
    assert(!valid_point(out,64)); out[0]=5; assert(!valid_point(out,65));
    memset(out,0,65);out[0]=4; assert(!valid_point(out,65));
    assert(verify(test_pub_a,test_digest,test_signature_raw));
    memcpy(bad,test_digest,32);bad[0]^=1; assert(!verify(test_pub_a,bad,test_signature_raw));
    memcpy(raw,test_signature_raw,64);raw[5]^=1; assert(!verify(test_pub_a,test_digest,raw));
    assert(!verify(test_pub_b,test_digest,test_signature_raw));
    n=32; assert(wc_ecc_shared_secret(&a,&b,out,&n)==0 && n==32 && !memcmp(out,test_ecdh,32));
    assert(raw_from_der(test_signature_der,sizeof(test_signature_der),raw) && !memcmp(raw,test_signature_raw,64));
    memcpy(out,test_signature_der,sizeof(test_signature_der));out[sizeof(test_signature_der)]=0;
    assert(!raw_from_der(out,sizeof(test_signature_der)+1,raw));
    out[0]=0x31;assert(!raw_from_der(out,sizeof(test_signature_der),raw));
    memset(raw,0,64);raw[31]=1;raw[63]=2;n=sizeof(out);
    assert(wc_ecc_rs_raw_to_sig(raw,32,raw+32,32,out,&n)==0);
    memset(raw,255,64);assert(raw_from_der(out,n,raw) && raw[0]==0 && raw[31]==1 && raw[32]==0 && raw[63]==2);

    n=sizeof(out);assert(wc_ecc_sign_hash(test_digest,32,out,&n,&rng,&a)==0);
    int verified=0;assert(wc_ecc_verify_hash(out,n,test_digest,32,&verified,&a)==0 && verified==1);
    printf("SIGNATURE_DER=");for(unsigned i=0;i<n;i++) printf("%02x",out[i]);puts("");
    DecodedCert c;wc_InitDecodedCert(&c,test_certificate,sizeof(test_certificate),NULL);
    assert(wc_ParseCert(&c,CERT_TYPE,NO_VERIFY,NULL)==0);
    byte public_der[128];word32 der_size=sizeof(public_der),index=0;
    assert(wc_GetPubKeyDerFromCert(&c,public_der,&der_size)==0);
    wc_ecc_free(&imported);assert(wc_ecc_init(&imported)==0);
    assert(wc_EccPublicKeyDecode(public_der,&index,&imported,der_size)==0 && index==der_size);
    n=65;assert(wc_ecc_export_x963(&imported,out,&n)==0 && n==65 && !memcmp(out,test_pub_b,65));
    wc_FreeDecodedCert(&c);
    assert(trusted_certificate(test_certificate,sizeof(test_certificate),test_pub_a));
    assert(!trusted_certificate(test_certificate,sizeof(test_certificate),test_pub_b));
    int wrong_issuer_size=wc_EccPublicKeyToDer(&b,public_der,sizeof(public_der),1);
    assert(wrong_issuer_size>0 && wc_CheckCertSigPubKey(test_certificate,sizeof(test_certificate),NULL,public_der,wrong_issuer_size,ECDSAk)!=0);
    byte tampered[sizeof(test_certificate)];memcpy(tampered,test_certificate,sizeof(tampered));tampered[sizeof(tampered)-1]^=1;
    assert(!trusted_certificate(tampered,sizeof(tampered),test_pub_a));
    assert(!trusted_certificate(test_future_certificate,sizeof(test_future_certificate),test_pub_a));
    wc_ecc_free(&a);wc_ecc_free(&b);wc_ecc_free(&imported);wc_FreeRng(&rng);
    puts("PASS: SHA256, AES128, P256 compressed/invalid points, ECDSA +/- cases, ECDH, strict DER, X509 extraction/signature/pinned issuer/time");
    return 0;
}
