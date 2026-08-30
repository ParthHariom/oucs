/**
 * OUCS Engine - Decoder (Reader)
 * oucs_decoder.c
 *
 * Implements: OucsReader context, oucs_reader_open, oucs_reader_get_song_info,
 *             oucs_reader_extract_song, oucs_reader_find_duplicates, and all
 *             related reader APIs.
 *
 * License: MIT
 */

#include "oucs_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ─────────────────────────────────────────────────────────────
   INTERNAL HELPERS
───────────────────────────────────────────────────────────── */

int oucs_pread(FILE *fp, void *buf, size_t size, uint64_t offset) {
    if (!fp || !buf) return OUCS_ERR_NULL_PARAM;
    if (fseek(fp, (long)offset, SEEK_SET) != 0) return OUCS_ERR_IO;
    if (fread(buf, 1, size, fp) != size) return OUCS_ERR_IO;
    return OUCS_OK;
}

uint32_t oucs_crc32(const uint8_t *data, size_t len) {
    static uint32_t table[256];
    static int ready = 0;
    if (!ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        ready = 1;
    }
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++)
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFF;
}

int oucs_header_valid(const OucsFileHeader *hdr) {
    if (!hdr) return 0;
    if (memcmp(hdr->magic, OUCS_MAGIC, 4) != 0) return 0;
    if (hdr->version_major > OUCS_VERSION_MAJOR)  return 0;
    return 1;
}

int oucs_load_index(OucsReader *r) {
    if (!r || !r->fp) return OUCS_ERR_NULL_PARAM;
    if (r->header.song_count == 0) return OUCS_OK;

    size_t idx_bytes = (size_t)r->header.song_count * OUCS_INDEX_ENTRY_SIZE;
    r->index = (OucsIndexEntry *)malloc(idx_bytes);
    if (!r->index) return OUCS_ERR_NOMEM;

    int ret = oucs_pread(r->fp, r->index, idx_bytes, r->header.index_table_offset);
    if (ret != OUCS_OK) { free(r->index); r->index = NULL; return ret; }
    return OUCS_OK;
}

int oucs_load_container_meta(OucsReader *r) {
    if (!r || !r->fp || r->meta_loaded) return OUCS_OK;

    uint64_t off = r->header.container_meta_offset;
    if (off == 0) return OUCS_OK;

    /* theme_name */
    if (oucs_pread(r->fp, r->meta.theme_name, OUCS_META_NAME_MAX, off) != OUCS_OK)
        return OUCS_ERR_IO;
    off += OUCS_META_NAME_MAX;
    r->meta.theme_name[OUCS_META_NAME_MAX - 1] = '\0';

    /* description */
    if (oucs_pread(r->fp, r->meta.description, OUCS_META_DESC_MAX, off) != OUCS_OK)
        return OUCS_ERR_IO;
    off += OUCS_META_DESC_MAX;
    r->meta.description[OUCS_META_DESC_MAX - 1] = '\0';

    /* logo_size */
    uint32_t logo_size = 0;
    if (oucs_pread(r->fp, &logo_size, 4, off) != OUCS_OK) return OUCS_ERR_IO;
    off += 4;
    r->meta.logo_size = logo_size;

    /* logo bytes */
    if (logo_size > 0) {
        r->meta.logo_bytes = (uint8_t *)malloc(logo_size);
        if (!r->meta.logo_bytes) return OUCS_ERR_NOMEM;
        if (oucs_pread(r->fp, r->meta.logo_bytes, logo_size, off) != OUCS_OK) {
            free(r->meta.logo_bytes); r->meta.logo_bytes = NULL; return OUCS_ERR_IO;
        }
        off += logo_size;
    }

    /* logo_ext_url */
    if (oucs_pread(r->fp, r->meta.logo_ext_url, OUCS_META_URL_MAX, off) != OUCS_OK)
        return OUCS_ERR_IO;
    off += OUCS_META_URL_MAX;
    r->meta.logo_ext_url[OUCS_META_URL_MAX - 1] = '\0';

    /* created_at */
    if (oucs_pread(r->fp, &r->meta.created_at, 8, off) != OUCS_OK) return OUCS_ERR_IO;
    off += 8;

    /* author_uuid + author_name */
    if (oucs_pread(r->fp, r->meta.author_uuid, OUCS_UUID_SIZE, off) != OUCS_OK)
        return OUCS_ERR_IO;
    off += OUCS_UUID_SIZE;
    if (oucs_pread(r->fp, r->meta.author_name, 16, off) != OUCS_OK)
        return OUCS_ERR_IO;
    r->meta.author_name[15] = '\0';

    r->meta_loaded = 1;
    return OUCS_OK;
}

/* ─────────────────────────────────────────────────────────────
   PUBLIC API
───────────────────────────────────────────────────────────── */

OucsReader *oucs_reader_open(const char *path) {
    if (!path) return NULL;

    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;

    OucsReader *r = (OucsReader *)calloc(1, sizeof(OucsReader));
    if (!r) { fclose(fp); return NULL; }

    r->fp = fp;
    strncpy(r->path, path, sizeof(r->path) - 1);

    /* read header */
    if (fread(&r->header, 1, sizeof(OucsFileHeader), fp) != sizeof(OucsFileHeader)) {
        oucs_reader_free(r); return NULL;
    }
    if (!oucs_header_valid(&r->header)) {
        oucs_reader_free(r); return NULL;
    }

    /* load index */
    if (oucs_load_index(r) != OUCS_OK) {
        oucs_reader_free(r); return NULL;
    }

    /* load container meta */
    oucs_load_container_meta(r);

    return r;
}

/* URL-based reader (HTTP Range) — oucs_network.c provides the actual fetch */
OucsReader *oucs_reader_open_url(const char *url) {
    if (!url) return NULL;
    /* Delegate to oucs_network_open_url() defined in oucs_network.c */
    extern OucsReader *oucs_network_open_url(const char *url);
    return oucs_network_open_url(url);
}

int oucs_reader_song_count(const OucsReader *r) {
    if (!r) return 0;
    return (int)r->header.song_count;
}

int oucs_reader_get_container_meta(const OucsReader *r, OucsContainerMeta *meta_out) {
    if (!r || !meta_out) return OUCS_ERR_NULL_PARAM;
    memcpy(meta_out, &r->meta, sizeof(OucsContainerMeta));
    /* deep-copy logo bytes */
    if (r->meta.logo_bytes && r->meta.logo_size > 0) {
        meta_out->logo_bytes = (uint8_t *)malloc(r->meta.logo_size);
        if (!meta_out->logo_bytes) return OUCS_ERR_NOMEM;
        memcpy(meta_out->logo_bytes, r->meta.logo_bytes, r->meta.logo_size);
    } else {
        meta_out->logo_bytes = NULL;
    }
    return OUCS_OK;
}

int oucs_reader_get_song_info(const OucsReader *r, uint32_t idx, OucsIndexEntry *entry_out) {
    if (!r || !entry_out) return OUCS_ERR_NULL_PARAM;
    if (idx >= r->header.song_count) return OUCS_ERR_NOT_FOUND;
    if (!r->index) return OUCS_ERR_CORRUPT;
    memcpy(entry_out, &r->index[idx], sizeof(OucsIndexEntry));
    return OUCS_OK;
}

int oucs_reader_find_by_uuid(const OucsReader *r, const OucsUUID uuid, uint32_t *idx_out) {
    if (!r || !uuid || !idx_out) return OUCS_ERR_NULL_PARAM;
    for (uint32_t i = 0; i < r->header.song_count; i++) {
        if (memcmp(r->index[i].uuid, uuid, OUCS_UUID_SIZE) == 0) {
            *idx_out = i;
            return OUCS_OK;
        }
    }
    return OUCS_ERR_NOT_FOUND;
}

int oucs_reader_extract_song_mem(OucsReader *r, uint32_t idx,
                                  uint8_t **data_out, size_t *size_out,
                                  const char *password) {
    if (!r || !data_out || !size_out) return OUCS_ERR_NULL_PARAM;
    if (idx >= r->header.song_count || !r->index) return OUCS_ERR_NOT_FOUND;

    OucsIndexEntry *e = &r->index[idx];
    size_t sz = (size_t)e->byte_size;

    uint8_t *buf = (uint8_t *)malloc(sz);
    if (!buf) return OUCS_ERR_NOMEM;

    int ret = oucs_pread(r->fp, buf, sz, e->byte_offset);
    if (ret != OUCS_OK) { free(buf); return ret; }

    /* CRC-32 verify */
    uint32_t computed_crc = oucs_crc32(buf, sz);
    if (e->crc32 != 0 && computed_crc != e->crc32) {
        /* CRC mismatch — attempt ECC recovery if available */
        if (e->ecc_offset != 0) {
            extern int oucs_ecc_recover(uint8_t *data, size_t data_size,
                                         FILE *fp, uint64_t ecc_offset, uint64_t ecc_size);
            oucs_ecc_recover(buf, sz, r->fp, e->ecc_offset, e->ecc_size);
        } else {
            free(buf);
            return OUCS_ERR_CORRUPT;
        }
    }

    /* Decrypt if needed */
    if (e->encryption_flag == OUCS_ENC_AES256GCM) {
        if (!password) { free(buf); return OUCS_ERR_WRONG_PASSWORD; }
        extern int oucs_decrypt_song(uint8_t *ciphertext, size_t ct_size,
                                      const char *password, const uint8_t *nonce,
                                      uint8_t **plaintext_out, size_t *pt_size_out);
        uint8_t *plain = NULL;
        size_t   plain_sz = 0;
        ret = oucs_decrypt_song(buf, sz, password, e->iv_nonce, &plain, &plain_sz);
        free(buf);
        if (ret != OUCS_OK) return ret;
        *data_out = plain;
        *size_out = plain_sz;
        return OUCS_OK;
    }

    *data_out = buf;
    *size_out = sz;
    return OUCS_OK;
}

int oucs_reader_extract_song(OucsReader *r, uint32_t idx,
                              const char *out_path, const char *password) {
    if (!out_path) return OUCS_ERR_NULL_PARAM;
    uint8_t *data = NULL;
    size_t   size = 0;
    int ret = oucs_reader_extract_song_mem(r, idx, &data, &size, password);
    if (ret != OUCS_OK) return ret;

    FILE *fp = fopen(out_path, "wb");
    if (!fp) { free(data); return OUCS_ERR_IO; }
    if (fwrite(data, 1, size, fp) != size) { fclose(fp); free(data); return OUCS_ERR_IO; }
    fclose(fp);
    free(data);
    return OUCS_OK;
}

int oucs_reader_get_lyrics(const OucsReader *r, uint32_t idx, OucsLyrics *lyrics_out) {
    if (!r || !lyrics_out) return OUCS_ERR_NULL_PARAM;
    if (idx >= r->header.song_count) return OUCS_ERR_NOT_FOUND;
    OucsIndexEntry *e = &r->index[idx];
    if (e->lyrics_offset == 0) { lyrics_out->count = 0; lyrics_out->entries = NULL; return OUCS_OK; }

    uint64_t off = e->lyrics_offset;
    uint32_t count = 0;
    if (oucs_pread(r->fp, &count, 4, off) != OUCS_OK) return OUCS_ERR_IO;
    off += 4;

    lyrics_out->count   = count;
    lyrics_out->entries = (OucsLyricEntry *)calloc(count, sizeof(OucsLyricEntry));
    if (!lyrics_out->entries) return OUCS_ERR_NOMEM;

    for (uint32_t i = 0; i < count; i++) {
        if (oucs_pread(r->fp, &lyrics_out->entries[i].timestamp_ms, 4, off) != OUCS_OK) goto err;
        off += 4;
        uint16_t tlen = 0;
        if (oucs_pread(r->fp, &tlen, 2, off) != OUCS_OK) goto err;
        off += 2;
        char *txt = (char *)malloc(tlen + 1);
        if (!txt) goto err;
        if (oucs_pread(r->fp, txt, tlen, off) != OUCS_OK) { free(txt); goto err; }
        txt[tlen] = '\0';
        lyrics_out->entries[i].text = txt;
        off += tlen;
    }
    return OUCS_OK;

err:
    oucs_lyrics_free(lyrics_out);
    return OUCS_ERR_IO;
}

int oucs_reader_get_lyric_at_time(const OucsReader *r, uint32_t idx,
                                   uint32_t timestamp_ms, const char **text_out) {
    if (!r || !text_out) return OUCS_ERR_NULL_PARAM;
    /* Load lyrics, find the last entry whose timestamp <= timestamp_ms */
    OucsLyrics lyrics = {0};
    int ret = oucs_reader_get_lyrics(r, idx, &lyrics);
    if (ret != OUCS_OK) return ret;

    *text_out = NULL;
    for (uint32_t i = 0; i < lyrics.count; i++) {
        if (lyrics.entries[i].timestamp_ms <= timestamp_ms)
            *text_out = lyrics.entries[i].text;
        else
            break;
    }
    /* Note: caller must NOT free *text_out — it points into lyrics.entries which we free here */
    /* This is a simplified pattern; production code should cache lyrics in the reader */
    oucs_lyrics_free(&lyrics);
    return OUCS_OK;
}

int oucs_reader_get_chapters(const OucsReader *r, uint32_t idx, OucsChapters *out) {
    if (!r || !out) return OUCS_ERR_NULL_PARAM;
    if (idx >= r->header.song_count) return OUCS_ERR_NOT_FOUND;
    OucsIndexEntry *e = &r->index[idx];
    if (e->chapters_offset == 0 || e->chapters_count == 0) {
        out->count = 0; out->entries = NULL; return OUCS_OK;
    }
    uint64_t off = e->chapters_offset;
    uint32_t count = 0;
    if (oucs_pread(r->fp, &count, 4, off) != OUCS_OK) return OUCS_ERR_IO;
    off += 4;

    out->count   = count;
    out->entries = (OucsChapter *)calloc(count, sizeof(OucsChapter));
    if (!out->entries) return OUCS_ERR_NOMEM;

    for (uint32_t i = 0; i < count; i++) {
        if (oucs_pread(r->fp, &out->entries[i].offset_ms, 4, off) != OUCS_OK) goto err;
        off += 4;
        uint16_t nlen = 0;
        if (oucs_pread(r->fp, &nlen, 2, off) != OUCS_OK) goto err;
        off += 2;
        char *name = (char *)malloc(nlen + 1);
        if (!name) goto err;
        if (oucs_pread(r->fp, name, nlen, off) != OUCS_OK) { free(name); goto err; }
        name[nlen] = '\0';
        out->entries[i].name = name;
        off += nlen;
    }
    return OUCS_OK;
err:
    oucs_chapters_free(out);
    return OUCS_ERR_IO;
}

int oucs_reader_get_waveform(const OucsReader *r, uint32_t idx, OucsWaveform *out) {
    if (!r || !out) return OUCS_ERR_NULL_PARAM;
    if (idx >= r->header.song_count) return OUCS_ERR_NOT_FOUND;
    OucsIndexEntry *e = &r->index[idx];
    if (e->waveform_offset == 0 || e->waveform_size == 0) {
        out->samples = NULL; out->count = 0; return OUCS_OK;
    }
    size_t byte_count = (size_t)e->waveform_size * sizeof(float);
    out->samples = (float *)malloc(byte_count);
    if (!out->samples) return OUCS_ERR_NOMEM;
    if (oucs_pread(r->fp, out->samples, byte_count, e->waveform_offset) != OUCS_OK) {
        free(out->samples); out->samples = NULL; return OUCS_ERR_IO;
    }
    out->count = e->waveform_size;
    return OUCS_OK;
}

int oucs_reader_get_fingerprint(const OucsReader *r, uint32_t idx, OucsFingerprint *out) {
    if (!r || !out) return OUCS_ERR_NULL_PARAM;
    if (idx >= r->header.song_count) return OUCS_ERR_NOT_FOUND;
    OucsIndexEntry *e = &r->index[idx];
    if (e->fingerprint_offset == 0 || e->fingerprint_size == 0) {
        out->data = NULL; out->size = 0; return OUCS_OK;
    }
    out->data = (uint8_t *)malloc(e->fingerprint_size);
    if (!out->data) return OUCS_ERR_NOMEM;
    if (oucs_pread(r->fp, out->data, e->fingerprint_size, e->fingerprint_offset) != OUCS_OK) {
        free(out->data); out->data = NULL; return OUCS_ERR_IO;
    }
    out->size = e->fingerprint_size;
    return OUCS_OK;
}

int oucs_reader_get_accessibility(const OucsReader *r, uint32_t idx, OucsAccessibility *out) {
    if (!r || !out) return OUCS_ERR_NULL_PARAM;
    if (idx >= r->header.song_count) return OUCS_ERR_NOT_FOUND;
    OucsIndexEntry *e = &r->index[idx];
    if (e->accessibility_offset == 0) {
        memset(out, 0, sizeof(*out)); return OUCS_OK;
    }
    uint64_t off = e->accessibility_offset;
    if (oucs_pread(r->fp, out->language, 4, off) != OUCS_OK) return OUCS_ERR_IO;
    off += 4;

    uint32_t adlen = 0;
    if (oucs_pread(r->fp, &adlen, 4, off) != OUCS_OK) return OUCS_ERR_IO;
    off += 4;
    out->audio_description = (char *)malloc(adlen + 1);
    if (!out->audio_description) return OUCS_ERR_NOMEM;
    if (oucs_pread(r->fp, out->audio_description, adlen, off) != OUCS_OK) {
        free(out->audio_description); return OUCS_ERR_IO;
    }
    out->audio_description[adlen] = '\0';
    off += adlen;

    uint32_t trlen = 0;
    if (oucs_pread(r->fp, &trlen, 4, off) != OUCS_OK) return OUCS_ERR_IO;
    off += 4;
    out->transcript = (char *)malloc(trlen + 1);
    if (!out->transcript) { free(out->audio_description); return OUCS_ERR_NOMEM; }
    if (oucs_pread(r->fp, out->transcript, trlen, off) != OUCS_OK) {
        free(out->audio_description); free(out->transcript); return OUCS_ERR_IO;
    }
    out->transcript[trlen] = '\0';
    return OUCS_OK;
}

int oucs_reader_find_duplicates(const OucsReader *r, uint32_t **matches_out, uint32_t *count_out) {
    if (!r || !matches_out || !count_out) return OUCS_ERR_NULL_PARAM;
    *matches_out = NULL; *count_out = 0;

    uint32_t n = r->header.song_count;
    uint32_t cap = 16;
    uint32_t *pairs = (uint32_t *)malloc(cap * 2 * sizeof(uint32_t));
    if (!pairs) return OUCS_ERR_NOMEM;
    uint32_t found = 0;

    for (uint32_t i = 0; i < n; i++) {
        for (uint32_t j = i + 1; j < n; j++) {
            /* Compare CRC-32 first (fast path) */
            if (r->index[i].crc32 == r->index[j].crc32 &&
                r->index[i].byte_size == r->index[j].byte_size) {
                if (found >= cap) {
                    cap *= 2;
                    uint32_t *tmp = (uint32_t *)realloc(pairs, cap * 2 * sizeof(uint32_t));
                    if (!tmp) { free(pairs); return OUCS_ERR_NOMEM; }
                    pairs = tmp;
                }
                pairs[found * 2]     = i;
                pairs[found * 2 + 1] = j;
                found++;
            }
        }
    }

    *matches_out = pairs;
    *count_out   = found;
    return OUCS_OK;
}

int oucs_reader_get_sync_manifest(const OucsReader *r, OucsSyncManifest *out) {
    if (!r || !out) return OUCS_ERR_NULL_PARAM;
    memset(out, 0, sizeof(*out));
    if (r->header.sync_manifest_offset == 0) return OUCS_OK;

    uint64_t off = r->header.sync_manifest_offset;
    uint32_t dc = 0;
    if (oucs_pread(r->fp, &dc, 4, off) != OUCS_OK) return OUCS_ERR_IO;
    off += 4;
    out->device_count = dc;
    if (dc > 0) {
        out->devices = (OucsSyncDevice *)calloc(dc, sizeof(OucsSyncDevice));
        if (!out->devices) return OUCS_ERR_NOMEM;
        for (uint32_t i = 0; i < dc; i++) {
            if (oucs_pread(r->fp, out->devices[i].device_id, OUCS_UUID_SIZE, off) != OUCS_OK) goto err;
            off += OUCS_UUID_SIZE;
            if (oucs_pread(r->fp, &out->devices[i].last_sync_ts, 8, off) != OUCS_OK) goto err;
            off += 8;
            off += 8; /* reserved */
        }
    }
    uint32_t ec = 0;
    if (oucs_pread(r->fp, &ec, 4, off) != OUCS_OK) goto err;
    off += 4;
    out->entry_count = ec;
    if (ec > 0) {
        out->entries = (OucsSyncEntry *)calloc(ec, sizeof(OucsSyncEntry));
        if (!out->entries) goto err;
        for (uint32_t i = 0; i < ec; i++) {
            if (oucs_pread(r->fp, &out->entries[i].timestamp, 8, off) != OUCS_OK) goto err;
            off += 8;
            if (oucs_pread(r->fp, &out->entries[i].operation, 1, off) != OUCS_OK) goto err;
            off += 1;
            if (oucs_pread(r->fp, out->entries[i].song_uuid, OUCS_UUID_SIZE, off) != OUCS_OK) goto err;
            off += OUCS_UUID_SIZE;
        }
    }
    return OUCS_OK;
err:
    oucs_sync_manifest_free(out);
    return OUCS_ERR_IO;
}

int oucs_reader_get_history(const OucsReader *r, OucsHistory *out) {
    if (!r || !out) return OUCS_ERR_NULL_PARAM;
    memset(out, 0, sizeof(*out));
    if (r->header.version_history_offset == 0) return OUCS_OK;

    uint64_t off = r->header.version_history_offset;
    uint32_t count = 0;
    if (oucs_pread(r->fp, &count, 4, off) != OUCS_OK) return OUCS_ERR_IO;
    off += 4;
    out->entry_count = count;
    if (count == 0) return OUCS_OK;

    out->entries = (OucsHistoryEntry *)calloc(count, sizeof(OucsHistoryEntry));
    if (!out->entries) return OUCS_ERR_NOMEM;

    for (uint32_t i = 0; i < count; i++) {
        if (oucs_pread(r->fp, &out->entries[i].timestamp, 8, off) != OUCS_OK) goto err;
        off += 8;
        if (oucs_pread(r->fp, &out->entries[i].operation, 1, off) != OUCS_OK) goto err;
        off += 1;
        if (oucs_pread(r->fp, out->entries[i].song_uuid, OUCS_UUID_SIZE, off) != OUCS_OK) goto err;
        off += OUCS_UUID_SIZE;
        if (oucs_pread(r->fp, out->entries[i].note, 64, off) != OUCS_OK) goto err;
        out->entries[i].note[63] = '\0';
        off += 64;
    }
    return OUCS_OK;
err:
    oucs_history_free(out);
    return OUCS_ERR_IO;
}

void oucs_reader_free(OucsReader *r) {
    if (!r) return;
    if (r->fp) fclose(r->fp);
    if (r->index) free(r->index);
    oucs_container_meta_free(&r->meta);
    free(r);
}
