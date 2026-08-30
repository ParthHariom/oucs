/**
 * OUCS Engine - Utilities
 * oucs_util.c
 *
 * UUID generation, error strings, version string, and other helpers.
 *
 * License: MIT
 */

#include "oucs_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* ─────────────────────────────────────────────────────────────
   UUID v4 GENERATION
───────────────────────────────────────────────────────────── */

void oucs_uuid_generate(OucsUUID out) {
    /* Simple random UUID using stdlib rand().
       For production, prefer /dev/urandom or platform CSPRNG. */
    static int seeded = 0;
    if (!seeded) { srand((unsigned)time(NULL)); seeded = 1; }

    for (int i = 0; i < OUCS_UUID_SIZE; i++)
        out[i] = (uint8_t)(rand() & 0xFF);

    /* Set version 4 (random) */
    out[6] = (out[6] & 0x0F) | 0x40;
    /* Set variant bits */
    out[8] = (out[8] & 0x3F) | 0x80;
}

void oucs_uuid_to_str(const OucsUUID uuid, char *buf) {
    /* Format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx */
    snprintf(buf, 37,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
        "%02x%02x%02x%02x%02x%02x",
        uuid[0], uuid[1], uuid[2],  uuid[3],
        uuid[4], uuid[5],
        uuid[6], uuid[7],
        uuid[8], uuid[9],
        uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);
}

int oucs_uuid_from_str(const char *str, OucsUUID out) {
    if (!str || strlen(str) < 36) return OUCS_ERR_INVALID_ARG;
    unsigned int v[16];
    int r = sscanf(str,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
        "%02x%02x%02x%02x%02x%02x",
        &v[0],&v[1],&v[2],&v[3],&v[4],&v[5],&v[6],&v[7],
        &v[8],&v[9],&v[10],&v[11],&v[12],&v[13],&v[14],&v[15]);
    if (r != 16) return OUCS_ERR_INVALID_ARG;
    for (int i = 0; i < 16; i++) out[i] = (uint8_t)v[i];
    return OUCS_OK;
}

/* ─────────────────────────────────────────────────────────────
   ERROR STRINGS
───────────────────────────────────────────────────────────── */

const char *oucs_strerror(OucsError err) {
    switch (err) {
        case OUCS_OK:                 return "Success";
        case OUCS_ERR_NULL_PARAM:     return "Null parameter";
        case OUCS_ERR_IO:             return "I/O error";
        case OUCS_ERR_INVALID_MAGIC:  return "Invalid magic bytes (not a .oucs file)";
        case OUCS_ERR_VERSION:        return "Unsupported file version";
        case OUCS_ERR_CORRUPT:        return "File is corrupted";
        case OUCS_ERR_NOT_FOUND:      return "Song not found";
        case OUCS_ERR_NOMEM:          return "Out of memory";
        case OUCS_ERR_CRYPTO:         return "Crypto error";
        case OUCS_ERR_WRONG_PASSWORD: return "Wrong password";
        case OUCS_ERR_ECC_FAIL:       return "ECC recovery failed (too many errors)";
        case OUCS_ERR_INVALID_ARG:    return "Invalid argument";
        case OUCS_ERR_OVERFLOW:       return "Buffer overflow";
        case OUCS_ERR_ALREADY_EXISTS: return "Already exists";
        case OUCS_ERR_NETWORK:        return "Network error";
        case OUCS_ERR_UNSUPPORTED:    return "Unsupported operation";
        default:                      return "Unknown error";
    }
}

const char *oucs_version(void) {
    return OUCS_VERSION_STRING;
}

/* ─────────────────────────────────────────────────────────────
   I/O HELPERS
───────────────────────────────────────────────────────────── */

int oucs_write_u32(FILE *fp, uint32_t val) {
    uint8_t b[4] = {
        (uint8_t)(val),
        (uint8_t)(val >> 8),
        (uint8_t)(val >> 16),
        (uint8_t)(val >> 24)
    };
    return (fwrite(b, 1, 4, fp) == 4) ? OUCS_OK : OUCS_ERR_IO;
}

int oucs_write_u64(FILE *fp, uint64_t val) {
    uint8_t b[8] = {
        (uint8_t)(val),       (uint8_t)(val >> 8),
        (uint8_t)(val >> 16), (uint8_t)(val >> 24),
        (uint8_t)(val >> 32), (uint8_t)(val >> 40),
        (uint8_t)(val >> 48), (uint8_t)(val >> 56)
    };
    return (fwrite(b, 1, 8, fp) == 8) ? OUCS_OK : OUCS_ERR_IO;
}

uint32_t oucs_read_u32(FILE *fp) {
    uint8_t b[4] = {0};
    if (fread(b, 1, 4, fp) != 4) return 0;
    return (uint32_t)b[0] | ((uint32_t)b[1]<<8) | ((uint32_t)b[2]<<16) | ((uint32_t)b[3]<<24);
}

uint64_t oucs_read_u64(FILE *fp) {
    uint8_t b[8] = {0};
    if (fread(b, 1, 8, fp) != 8) return 0;
    return (uint64_t)b[0] | ((uint64_t)b[1]<<8) | ((uint64_t)b[2]<<16) | ((uint64_t)b[3]<<24) |
           ((uint64_t)b[4]<<32) | ((uint64_t)b[5]<<40) | ((uint64_t)b[6]<<48) | ((uint64_t)b[7]<<56);
}
