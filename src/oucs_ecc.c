/**
 * OUCS Engine - Error Correction Code (ECC)
 * oucs_ecc.c
 *
 * Implements Reed-Solomon(255,223) style error correction using
 * a GF(2^8) finite field. Each 223-byte data block gets 32 parity
 * bytes. Up to 16 byte-errors per block can be corrected.
 *
 * Also provides CRC-32 computation.
 *
 * License: MIT
 */

#include "oucs_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ─────────────────────────────────────────────────────────────
   GF(2^8) ARITHMETIC  (primitive polynomial x^8+x^4+x^3+x^2+1)
───────────────────────────────────────────────────────────── */

#define GF_SIZE   256
#define GF_POLY   0x11D   /* x^8+x^4+x^3+x^2+1 */
#define RS_NROOTS 32      /* parity bytes */
#define RS_NN     255     /* total block size */
#define RS_KK     (RS_NN - RS_NROOTS)  /* 223 data bytes */
#define RS_IPRIM  1       /* primitive element α = 1 in GF(256) uses base α */

static uint8_t gf_exp[512];  /* antilog table */
static uint8_t gf_log[256];  /* log table */
static int     gf_init_done = 0;

static void gf_init(void) {
    if (gf_init_done) return;
    int x = 1;
    for (int i = 0; i < 255; i++) {
        gf_exp[i] = (uint8_t)x;
        gf_log[x] = (uint8_t)i;
        x <<= 1;
        if (x & 0x100) x ^= GF_POLY;
    }
    for (int i = 255; i < 512; i++) gf_exp[i] = gf_exp[i - 255];
    gf_log[0] = 0; /* undefined but set to 0 to avoid UB */
    gf_init_done = 1;
}

static inline uint8_t gf_mul(uint8_t a, uint8_t b) {
    if (a == 0 || b == 0) return 0;
    return gf_exp[gf_log[a] + gf_log[b]];
}

static inline uint8_t gf_pow(uint8_t a, int n) {
    if (a == 0) return 0;
    return gf_exp[(gf_log[a] * n) % 255];
}

static inline uint8_t gf_inv(uint8_t a) {
    if (a == 0) return 0;
    return gf_exp[255 - gf_log[a]];
}

/* ─────────────────────────────────────────────────────────────
   RS GENERATOR POLYNOMIAL
───────────────────────────────────────────────────────────── */

static uint8_t rs_gen[RS_NROOTS + 1];
static int     rs_gen_done = 0;

static void rs_gen_init(void) {
    if (rs_gen_done) return;
    gf_init();
    rs_gen[0] = 1;
    for (int i = 0; i < RS_NROOTS; i++) {
        rs_gen[i + 1] = 1;
        for (int j = i; j > 0; j--)
            rs_gen[j] = rs_gen[j - 1] ^ gf_mul(rs_gen[j], gf_exp[i]);
        rs_gen[0] = gf_mul(rs_gen[0], gf_exp[i]);
    }
    rs_gen_done = 1;
}

/* ─────────────────────────────────────────────────────────────
   ENCODE: compute 32 parity bytes for 223 data bytes
───────────────────────────────────────────────────────────── */

static void rs_encode_block(const uint8_t *data, uint8_t *parity) {
    rs_gen_init();
    memset(parity, 0, RS_NROOTS);
    for (int i = 0; i < RS_KK; i++) {
        uint8_t feedback = data[i] ^ parity[0];
        if (feedback != 0) {
            for (int j = 1; j < RS_NROOTS; j++)
                parity[j - 1] = parity[j] ^ gf_mul(feedback, rs_gen[RS_NROOTS - j]);
            parity[RS_NROOTS - 1] = gf_mul(feedback, rs_gen[0]);
        } else {
            memmove(parity, parity + 1, RS_NROOTS - 1);
            parity[RS_NROOTS - 1] = 0;
        }
    }
}

/* ─────────────────────────────────────────────────────────────
   DECODE: attempt to correct errors in a block
   Returns 0 on success, -1 if uncorrectable
───────────────────────────────────────────────────────────── */

static int rs_decode_block(uint8_t *block, int block_len) {
    gf_init();
    /* Compute syndromes */
    uint8_t s[RS_NROOTS];
    int     no_error = 1;
    for (int i = 0; i < RS_NROOTS; i++) {
        uint8_t tmp = 0;
        for (int j = 0; j < block_len; j++)
            tmp = gf_mul(tmp, gf_exp[i]) ^ block[j];
        s[i] = tmp;
        if (tmp) no_error = 0;
    }
    if (no_error) return 0;

    /* Berlekamp-Massey to find error locator polynomial */
    uint8_t sigma[RS_NROOTS / 2 + 2] = {1};
    uint8_t B[RS_NROOTS / 2 + 2]     = {1};
    int L = 0, m = 1;
    uint8_t b = 1;

    for (int n = 0; n < RS_NROOTS; n++) {
        uint8_t d = s[n];
        for (int i = 1; i <= L; i++)
            d ^= gf_mul(sigma[i], s[n - i]);
        if (d == 0) { m++; continue; }
        uint8_t T[RS_NROOTS / 2 + 2] = {0};
        memcpy(T, sigma, sizeof(T));
        uint8_t coeff = gf_mul(d, gf_inv(b));
        for (int i = m; i <= RS_NROOTS / 2; i++)
            sigma[i] ^= gf_mul(coeff, B[i - m]);
        if (2 * L <= n) {
            L = n + 1 - L;
            memcpy(B, T, sizeof(B));
            b = d; m = 1;
        } else { m++; }
    }

    /* Chien search — find error locations */
    uint8_t roots[RS_NROOTS / 2];
    uint8_t locs[RS_NROOTS / 2];
    int     errs = 0;
    for (int i = 1; i < GF_SIZE; i++) {
        uint8_t val = 1;
        for (int j = 1; j <= L; j++)
            val ^= gf_mul(sigma[j], gf_exp[(j * i) % 255]);
        if (val == 0) {
            if (errs >= RS_NROOTS / 2) return -1;
            roots[errs] = (uint8_t)i;
            locs[errs]  = (uint8_t)(255 - i);
            errs++;
        }
    }
    if (errs != L) return -1;

    /* Forney algorithm — compute error magnitudes */
    for (int i = 0; i < errs; i++) {
        uint8_t Xi = gf_exp[locs[i]];
        uint8_t num = 0, den = 1;
        /* Omega (error evaluator) */
        for (int j = 0; j < RS_NROOTS; j++) {
            uint8_t tmp = s[j];
            for (int k = 1; k <= L; k++)
                tmp ^= gf_mul(sigma[k], (j >= k) ? s[j - k] : 0);
            num ^= gf_mul(tmp, gf_exp[(j * locs[i]) % 255]);
        }
        /* Formal derivative of sigma at Xi */
        for (int j = 1; j <= L; j += 2)
            den ^= gf_mul(sigma[j], gf_exp[((j - 1) * locs[i]) % 255]);
        if (den == 0) return -1;

        uint8_t magnitude = gf_mul(Xi, gf_mul(num, gf_inv(den)));
        int pos = block_len - 1 - locs[i];
        if (pos < 0 || pos >= block_len) return -1;
        block[pos] ^= magnitude;
    }
    return 0;
}

/* ─────────────────────────────────────────────────────────────
   PUBLIC API
───────────────────────────────────────────────────────────── */

/**
 * Compute ECC for audio data and write parity to *ecc_out.
 * Caller must free *ecc_out.
 * @param ecc_size_out  Number of parity bytes written
 */
int oucs_ecc_encode(const uint8_t *data, size_t data_size,
                    uint8_t **ecc_out, size_t *ecc_size_out) {
    if (!data || !ecc_out || !ecc_size_out) return OUCS_ERR_NULL_PARAM;

    rs_gen_init();
    size_t num_blocks = (data_size + RS_KK - 1) / RS_KK;
    size_t ecc_total  = num_blocks * RS_NROOTS;

    uint8_t *ecc = (uint8_t *)calloc(1, ecc_total);
    if (!ecc) return OUCS_ERR_NOMEM;

    for (size_t b = 0; b < num_blocks; b++) {
        size_t  block_start = b * RS_KK;
        size_t  block_len   = (data_size - block_start) < RS_KK
                              ? (data_size - block_start) : RS_KK;
        /* Pad last block to RS_KK if needed */
        uint8_t block[RS_KK];
        memset(block, 0, RS_KK);
        memcpy(block, data + block_start, block_len);

        rs_encode_block(block, ecc + b * RS_NROOTS);
    }

    *ecc_out      = ecc;
    *ecc_size_out = ecc_total;
    return OUCS_OK;
}

/**
 * Attempt to recover data using stored parity.
 * Modifies data in-place. Returns OUCS_OK or OUCS_ERR_ECC_FAIL.
 */
int oucs_ecc_recover(uint8_t *data, size_t data_size,
                     FILE *fp, uint64_t ecc_offset, uint64_t ecc_size) {
    if (!data || !fp) return OUCS_ERR_NULL_PARAM;
    if (ecc_offset == 0 || ecc_size == 0) return OUCS_ERR_UNSUPPORTED;

    uint8_t *ecc = (uint8_t *)malloc((size_t)ecc_size);
    if (!ecc) return OUCS_ERR_NOMEM;

    if (oucs_pread(fp, ecc, (size_t)ecc_size, ecc_offset) != OUCS_OK) {
        free(ecc); return OUCS_ERR_IO;
    }

    size_t num_blocks = (data_size + RS_KK - 1) / RS_KK;
    int any_fail = 0;

    for (size_t b = 0; b < num_blocks; b++) {
        size_t  block_start = b * RS_KK;
        size_t  block_len   = (data_size - block_start) < RS_KK
                              ? (data_size - block_start) : RS_KK;

        /* Build full RS word (data + parity) */
        uint8_t word[RS_NN];
        memset(word, 0, RS_NN);
        memcpy(word, data + block_start, block_len);
        memcpy(word + RS_KK, ecc + b * RS_NROOTS, RS_NROOTS);

        if (rs_decode_block(word, RS_NN) != 0) {
            any_fail = 1;
        } else {
            memcpy(data + block_start, word, block_len);
        }
    }

    free(ecc);
    return any_fail ? OUCS_ERR_ECC_FAIL : OUCS_OK;
}

/**
 * CRC-32 wrapper (same table as encoder).
 */
uint32_t oucs_crc32_pub(const uint8_t *data, size_t len) {
    extern uint32_t oucs_crc32(const uint8_t *, size_t);
    return oucs_crc32(data, len);
}
