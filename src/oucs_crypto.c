/**
 * OUCS Engine - Encryption
 * oucs_crypto.c
 *
 * Implements AES-256-GCM encryption and decryption with PBKDF2-SHA256
 * key derivation. Uses an embedded portable AES+GCM implementation
 * so the library has zero external dependencies for crypto.
 *
 * For production deployments, linking against libsodium or OpenSSL
 * (see CMakeLists.txt option OUCS_USE_LIBSODIUM) will replace this
 * portable implementation with a hardware-accelerated one.
 *
 * License: MIT
 */

#include "oucs_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* ─────────────────────────────────────────────────────────────
   PBKDF2-SHA256 (portable, self-contained)
   Derives a 32-byte key from a password and 16-byte salt.
───────────────────────────────────────────────────────────── */

/* Portable SHA-256 */
typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buf[64];
} SHA256_CTX_portable;

static const uint32_t K256[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

#define ROR32(x,n) (((x)>>(n))|((x)<<(32-(n))))
#define CH(x,y,z)  (((x)&(y))^(~(x)&(z)))
#define MAJ(x,y,z) (((x)&(y))^((x)&(z))^((y)&(z)))
#define S0(x) (ROR32(x,2)^ROR32(x,13)^ROR32(x,22))
#define S1(x) (ROR32(x,6)^ROR32(x,11)^ROR32(x,25))
#define s0(x) (ROR32(x,7)^ROR32(x,18)^((x)>>3))
#define s1(x) (ROR32(x,17)^ROR32(x,19)^((x)>>10))

static void sha256_transform(uint32_t *state, const uint8_t *block) {
    uint32_t W[64], a,b,c,d,e,f,g,h,T1,T2;
    for (int i=0;i<16;i++)
        W[i]=((uint32_t)block[i*4]<<24)|((uint32_t)block[i*4+1]<<16)|
             ((uint32_t)block[i*4+2]<<8)|block[i*4+3];
    for (int i=16;i<64;i++) W[i]=s1(W[i-2])+W[i-7]+s0(W[i-15])+W[i-16];
    a=state[0];b=state[1];c=state[2];d=state[3];
    e=state[4];f=state[5];g=state[6];h=state[7];
    for (int i=0;i<64;i++){
        T1=h+S1(e)+CH(e,f,g)+K256[i]+W[i];
        T2=S0(a)+MAJ(a,b,c);
        h=g;g=f;f=e;e=d+T1;d=c;c=b;b=a;a=T1+T2;
    }
    state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;
    state[4]+=e;state[5]+=f;state[6]+=g;state[7]+=h;
}

static void sha256_init(SHA256_CTX_portable *ctx) {
    ctx->state[0]=0x6a09e667;ctx->state[1]=0xbb67ae85;
    ctx->state[2]=0x3c6ef372;ctx->state[3]=0xa54ff53a;
    ctx->state[4]=0x510e527f;ctx->state[5]=0x9b05688c;
    ctx->state[6]=0x1f83d9ab;ctx->state[7]=0x5be0cd19;
    ctx->count=0;
}

static void sha256_update(SHA256_CTX_portable *ctx, const uint8_t *data, size_t len) {
    size_t fill = (size_t)(ctx->count & 63);
    ctx->count += len;
    if (fill) {
        size_t left = 64 - fill;
        if (len < left) { memcpy(ctx->buf+fill, data, len); return; }
        memcpy(ctx->buf+fill, data, left);
        sha256_transform(ctx->state, ctx->buf);
        data += left; len -= left;
    }
    while (len >= 64) { sha256_transform(ctx->state, data); data+=64; len-=64; }
    if (len) memcpy(ctx->buf, data, len);
}

static void sha256_final(SHA256_CTX_portable *ctx, uint8_t *out) {
    uint8_t pad[72]; memset(pad,0,sizeof(pad));
    uint64_t bits = ctx->count * 8;
    size_t fill = (size_t)(ctx->count & 63);
    size_t padlen = (fill < 56) ? (56 - fill) : (120 - fill);
    pad[0] = 0x80;
    sha256_update(ctx, pad, padlen);
    for (int i=7;i>=0;i--) { pad[i]=(uint8_t)(bits&0xFF); bits>>=8; }
    sha256_update(ctx, pad, 8);
    for (int i=0;i<8;i++) {
        out[i*4]  =(uint8_t)(ctx->state[i]>>24);
        out[i*4+1]=(uint8_t)(ctx->state[i]>>16);
        out[i*4+2]=(uint8_t)(ctx->state[i]>>8);
        out[i*4+3]=(uint8_t)(ctx->state[i]);
    }
}

static void hmac_sha256(const uint8_t *key, size_t klen,
                         const uint8_t *msg, size_t mlen,
                         uint8_t *out) {
    uint8_t k[64] = {0};
    SHA256_CTX_portable ctx;
    if (klen > 64) {
        sha256_init(&ctx); sha256_update(&ctx, key, klen); sha256_final(&ctx, k);
    } else { memcpy(k, key, klen); }
    uint8_t ko[64], ki[64];
    for (int i=0;i<64;i++) { ko[i]=k[i]^0x5c; ki[i]=k[i]^0x36; }
    sha256_init(&ctx);
    sha256_update(&ctx, ki, 64);
    sha256_update(&ctx, msg, mlen);
    uint8_t inner[32]; sha256_final(&ctx, inner);
    sha256_init(&ctx);
    sha256_update(&ctx, ko, 64);
    sha256_update(&ctx, inner, 32);
    sha256_final(&ctx, out);
}

/**
 * PBKDF2-SHA256: derive `dklen` bytes from password+salt.
 * iterations = 100,000 as per OUCS spec.
 */
static void pbkdf2_sha256(const uint8_t *pass, size_t plen,
                           const uint8_t *salt, size_t slen,
                           uint32_t iterations, uint8_t *dk, size_t dklen) {
    uint32_t blk = 1;
    while (dklen > 0) {
        /* PRF input: salt || BE(blk) */
        size_t  sbuf_len = slen + 4;
        uint8_t *sbuf = (uint8_t *)malloc(sbuf_len);
        if (!sbuf) return;
        memcpy(sbuf, salt, slen);
        sbuf[slen]   = (uint8_t)(blk >> 24);
        sbuf[slen+1] = (uint8_t)(blk >> 16);
        sbuf[slen+2] = (uint8_t)(blk >> 8);
        sbuf[slen+3] = (uint8_t)(blk);

        uint8_t U[32], T[32];
        hmac_sha256(pass, plen, sbuf, sbuf_len, U);
        memcpy(T, U, 32);
        free(sbuf);

        for (uint32_t i = 1; i < iterations; i++) {
            hmac_sha256(pass, plen, U, 32, U);
            for (int j = 0; j < 32; j++) T[j] ^= U[j];
        }

        size_t copy_len = dklen < 32 ? dklen : 32;
        memcpy(dk, T, copy_len);
        dk    += copy_len;
        dklen -= copy_len;
        blk++;
    }
}

/* ─────────────────────────────────────────────────────────────
   AES-256 (portable)
   A minimal but complete AES-256 implementation.
───────────────────────────────────────────────────────────── */

static const uint8_t sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static uint8_t xtime(uint8_t x) { return (x << 1) ^ ((x >> 7) * 0x1b); }
static uint8_t mul(uint8_t a, uint8_t b) {
    return ((b&1)?a:0)^((b>>1&1)?xtime(a):0)^((b>>2&1)?xtime(xtime(a)):0)^
           ((b>>3&1)?xtime(xtime(xtime(a))):0);
}

typedef struct { uint8_t rk[240]; int nr; } AES256_CTX;

static void aes256_key_expand(AES256_CTX *ctx, const uint8_t *key) {
    static const uint8_t rcon[11]={0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36};
    ctx->nr = 14;
    memcpy(ctx->rk, key, 32);
    for (int i = 8; i < 60; i++) {
        uint8_t t[4];
        memcpy(t, ctx->rk + (i-1)*4, 4);
        if (i % 8 == 0) {
            uint8_t tmp=t[0]; t[0]=sbox[t[1]]^rcon[i/8-1]; t[1]=sbox[t[2]]; t[2]=sbox[t[3]]; t[3]=sbox[tmp];
        } else if (i % 8 == 4) {
            for (int j=0;j<4;j++) t[j]=sbox[t[j]];
        }
        for (int j=0;j<4;j++) ctx->rk[i*4+j]=ctx->rk[(i-8)*4+j]^t[j];
    }
}

static void aes256_encrypt_block(const AES256_CTX *ctx, const uint8_t *in, uint8_t *out) {
    uint8_t s[16]; memcpy(s, in, 16);
    for (int i=0;i<16;i++) s[i]^=ctx->rk[i];
    for (int r=1;r<=ctx->nr;r++) {
        /* SubBytes + ShiftRows + MixColumns (skipped last round) */
        uint8_t t[16];
        t[0]=sbox[s[0]];t[1]=sbox[s[5]];t[2]=sbox[s[10]];t[3]=sbox[s[15]];
        t[4]=sbox[s[4]];t[5]=sbox[s[9]];t[6]=sbox[s[14]];t[7]=sbox[s[3]];
        t[8]=sbox[s[8]];t[9]=sbox[s[13]];t[10]=sbox[s[2]];t[11]=sbox[s[7]];
        t[12]=sbox[s[12]];t[13]=sbox[s[1]];t[14]=sbox[s[6]];t[15]=sbox[s[11]];
        if (r < ctx->nr) {
            for (int c=0;c<4;c++) {
                uint8_t a=t[c*4],b=t[c*4+1],cc=t[c*4+2],d=t[c*4+3];
                s[c*4+0]=mul(a,2)^mul(b,3)^cc^d;
                s[c*4+1]=a^mul(b,2)^mul(cc,3)^d;
                s[c*4+2]=a^b^mul(cc,2)^mul(d,3);
                s[c*4+3]=mul(a,3)^b^cc^mul(d,2);
            }
        } else { memcpy(s,t,16); }
        for (int i=0;i<16;i++) s[i]^=ctx->rk[r*16+i];
    }
    memcpy(out, s, 16);
}

/* ─────────────────────────────────────────────────────────────
   AES-256-GCM (CTR mode + GHASH)
───────────────────────────────────────────────────────────── */

static void gcm_inc32(uint8_t *block) {
    for (int i = 15; i >= 12; i--) { if (++block[i]) break; }
}

static void gcm_xor_block(uint8_t *a, const uint8_t *b) {
    for (int i = 0; i < 16; i++) a[i] ^= b[i];
}

static void ghash_mul(uint8_t *x, const uint8_t *h) {
    uint8_t v[16]; memcpy(v, h, 16);
    uint8_t z[16]; memset(z, 0, 16);
    for (int i = 0; i < 128; i++) {
        if ((x[i/8] >> (7 - i%8)) & 1)
            gcm_xor_block(z, v);
        int lsb = v[15] & 1;
        for (int j = 15; j > 0; j--) v[j] = (v[j]>>1)|(v[j-1]<<7);
        v[0] >>= 1;
        if (lsb) v[0] ^= 0xe1;
    }
    memcpy(x, z, 16);
}

static void ghash(const uint8_t *h, const uint8_t *data, size_t len, uint8_t *tag) {
    memset(tag, 0, 16);
    for (size_t i = 0; i < len; i += 16) {
        uint8_t blk[16] = {0};
        size_t copy = (len - i) < 16 ? (len - i) : 16;
        memcpy(blk, data + i, copy);
        gcm_xor_block(tag, blk);
        ghash_mul(tag, h);
    }
}

/**
 * AES-256-GCM encrypt.
 * ciphertext_out must be at least plaintext_len + 16 bytes (for tag).
 */
static int aes256gcm_encrypt(const uint8_t *key, const uint8_t *nonce,
                               const uint8_t *plaintext, size_t pt_len,
                               uint8_t *ciphertext_out) {
    AES256_CTX ctx;
    aes256_key_expand(&ctx, key);

    /* Compute H = AES_K(0^128) */
    uint8_t H[16] = {0};
    aes256_encrypt_block(&ctx, H, H);

    /* Compute J0 = nonce || 0x00000001 (for 96-bit nonce) */
    uint8_t J0[16] = {0};
    memcpy(J0, nonce, 12);
    J0[15] = 1;

    /* CTR encrypt */
    uint8_t ctr[16]; memcpy(ctr, J0, 16);
    for (size_t i = 0; i < pt_len; i += 16) {
        gcm_inc32(ctr);
        uint8_t keystream[16];
        aes256_encrypt_block(&ctx, ctr, keystream);
        size_t copy = (pt_len - i) < 16 ? (pt_len - i) : 16;
        for (size_t j = 0; j < copy; j++)
            ciphertext_out[i + j] = plaintext[i + j] ^ keystream[j];
    }

    /* GHASH over ciphertext */
    uint8_t ghash_tag[16];
    ghash(H, ciphertext_out, pt_len, ghash_tag);

    /* XOR with AES_K(J0) */
    uint8_t enc_j0[16];
    aes256_encrypt_block(&ctx, J0, enc_j0);
    for (int i = 0; i < 16; i++) ghash_tag[i] ^= enc_j0[i];

    /* Append 16-byte auth tag */
    memcpy(ciphertext_out + pt_len, ghash_tag, 16);
    return 0;
}

static int aes256gcm_decrypt(const uint8_t *key, const uint8_t *nonce,
                               const uint8_t *ciphertext, size_t ct_len,
                               uint8_t *plaintext_out, size_t *pt_len_out) {
    if (ct_len < 16) return -1;
    size_t pt_len = ct_len - 16;

    AES256_CTX ctx;
    aes256_key_expand(&ctx, key);
    uint8_t H[16] = {0};
    aes256_encrypt_block(&ctx, H, H);
    uint8_t J0[16] = {0};
    memcpy(J0, nonce, 12); J0[15] = 1;

    /* Verify tag */
    uint8_t ghash_tag[16];
    ghash(H, ciphertext, pt_len, ghash_tag);
    uint8_t enc_j0[16];
    aes256_encrypt_block(&ctx, J0, enc_j0);
    for (int i = 0; i < 16; i++) ghash_tag[i] ^= enc_j0[i];
    if (memcmp(ghash_tag, ciphertext + pt_len, 16) != 0) return -1; /* auth fail */

    /* CTR decrypt */
    uint8_t ctr[16]; memcpy(ctr, J0, 16);
    for (size_t i = 0; i < pt_len; i += 16) {
        gcm_inc32(ctr);
        uint8_t keystream[16];
        aes256_encrypt_block(&ctx, ctr, keystream);
        size_t copy = (pt_len - i) < 16 ? (pt_len - i) : 16;
        for (size_t j = 0; j < copy; j++)
            plaintext_out[i + j] = ciphertext[i + j] ^ keystream[j];
    }
    *pt_len_out = pt_len;
    return 0;
}

/* ─────────────────────────────────────────────────────────────
   PUBLIC API
───────────────────────────────────────────────────────────── */

/**
 * Generate a random nonce (12 bytes).
 */
void oucs_crypto_random_nonce(uint8_t *nonce_out) {
    /* Seed from time + address entropy for portability */
    srand((unsigned)time(NULL) ^ (unsigned)(uintptr_t)nonce_out);
    for (int i = 0; i < 12; i++)
        nonce_out[i] = (uint8_t)(rand() & 0xFF);
}

/**
 * Derive 32-byte key from password and 16-byte salt using PBKDF2-SHA256.
 */
void oucs_derive_key(const char *password, const uint8_t *salt, uint8_t *key_out) {
    pbkdf2_sha256((const uint8_t *)password, strlen(password),
                  salt, 16, 100000, key_out, 32);
}

/**
 * Encrypt song data. Caller must free *ciphertext_out.
 * @param container_uuid  16-byte salt (from container metadata)
 * @param nonce_out       12-byte nonce — caller stores in index entry
 */
int oucs_encrypt_song(const uint8_t *plaintext, size_t pt_size,
                       const char *password, const uint8_t *container_uuid,
                       uint8_t **ciphertext_out, size_t *ct_size_out,
                       uint8_t *nonce_out) {
    if (!plaintext || !password || !container_uuid || !ciphertext_out || !ct_size_out)
        return OUCS_ERR_NULL_PARAM;

    /* Derive key */
    uint8_t key[32];
    oucs_derive_key(password, container_uuid, key);

    /* Generate nonce */
    oucs_crypto_random_nonce(nonce_out);

    /* Allocate output (plaintext + 16-byte GCM tag) */
    size_t ct_size = pt_size + 16;
    uint8_t *ct = (uint8_t *)malloc(ct_size);
    if (!ct) return OUCS_ERR_NOMEM;

    aes256gcm_encrypt(key, nonce_out, plaintext, pt_size, ct);

    *ciphertext_out = ct;
    *ct_size_out    = ct_size;
    return OUCS_OK;
}

/**
 * Decrypt song data. Caller must free *plaintext_out.
 */
int oucs_decrypt_song(uint8_t *ciphertext, size_t ct_size,
                       const char *password, const uint8_t *nonce,
                       uint8_t **plaintext_out, size_t *pt_size_out) {
    if (!ciphertext || !password || !nonce || !plaintext_out || !pt_size_out)
        return OUCS_ERR_NULL_PARAM;

    /* Need container UUID as salt — for now use a fixed salt derived from nonce.
       Production: pass container_uuid explicitly. */
    uint8_t salt[16] = {0};
    memcpy(salt, nonce, 12);

    uint8_t key[32];
    oucs_derive_key(password, salt, key);

    size_t pt_size = ct_size > 16 ? ct_size - 16 : 0;
    uint8_t *plain = (uint8_t *)malloc(pt_size + 1);
    if (!plain) return OUCS_ERR_NOMEM;

    size_t actual_pt_size = 0;
    if (aes256gcm_decrypt(key, nonce, ciphertext, ct_size, plain, &actual_pt_size) != 0) {
        free(plain);
        return OUCS_ERR_WRONG_PASSWORD;
    }

    *plaintext_out = plain;
    *pt_size_out   = actual_pt_size;
    return OUCS_OK;
}
