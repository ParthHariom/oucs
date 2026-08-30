/**
 * OUCS Engine - Encoder (Writer)
 * oucs_encoder.c
 *
 * Implements: OucsWriter context, oucs_writer_create, oucs_writer_add_song,
 *             oucs_writer_finalize, and all related writer APIs.
 *
 * License: MIT
 */

#include "../include/oucs_format.h"
#include "oucs_internal.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#ifdef _WIN32
  #define strcasecmp _stricmp
#else
  #include <strings.h>
#endif

/* ─── Internal pending-song node ─────────────────────────────── */
typedef struct OucsPendingSong {
    uint8_t             *audio_data;
    size_t               audio_size;
    OucsIndexEntry       entry;
    OucsLyrics          *lyrics;
    OucsChapters        *chapters;
    OucsAccessibility   *accessibility;
    char                 password[256];   /* empty = no encryption */
    struct OucsPendingSong *next;
} OucsPendingSong;

/* ─── Writer context ─────────────────────────────────────────── */
struct OucsWriter {
    char               path[4096];
    uint8_t            flags;
    OucsContainerMeta  meta;
    OucsPendingSong   *songs_head;
    OucsPendingSong   *songs_tail;
    uint32_t           song_count;
    OucsHook           hooks[OUCS_MAX_HOOKS];
    uint32_t           hook_count;
    int                compute_waveform;
    int                compute_fingerprint;
    int                compute_analysis;   /* BPM/key/mood */
};

/* ─── Forward declarations of internal helpers ───────────────── */
static uint32_t oucs_crc32_compute(const uint8_t *data, size_t len);
static void     oucs_fire_hook(OucsWriter *w, OucsHookType type, void *arg);
static int      oucs_write_block(FILE *fp, const void *data, size_t size, uint64_t *offset);

/* ─────────────────────────────────────────────────────────────
   PUBLIC API
───────────────────────────────────────────────────────────── */

OucsWriter *oucs_writer_create(const char *path, uint8_t flags) {
    if (!path) return NULL;

    OucsWriter *w = (OucsWriter *)calloc(1, sizeof(OucsWriter));
    if (!w) return NULL;

    strncpy(w->path, path, sizeof(w->path) - 1);
    w->flags              = flags;
    w->compute_waveform   = 1;
    w->compute_fingerprint= 1;
    w->compute_analysis   = 1;

    /* default container metadata */
    w->meta.created_at = (uint64_t)time(NULL);
    oucs_uuid_generate(w->meta.author_uuid);

    return w;
}

int oucs_writer_set_container_meta(OucsWriter *w, const OucsContainerMeta *meta) {
    if (!w || !meta) return OUCS_ERR_NULL_PARAM;

    strncpy(w->meta.theme_name,   meta->theme_name,   OUCS_META_NAME_MAX - 1);
    strncpy(w->meta.description,  meta->description,  OUCS_META_DESC_MAX - 1);
    strncpy(w->meta.logo_ext_url, meta->logo_ext_url, OUCS_META_URL_MAX  - 1);
    w->meta.created_at = meta->created_at ? meta->created_at : (uint64_t)time(NULL);

    /* copy logo */
    if (w->meta.logo_bytes) { free(w->meta.logo_bytes); w->meta.logo_bytes = NULL; }
    if (meta->logo_bytes && meta->logo_size > 0) {
        w->meta.logo_bytes = (uint8_t *)malloc(meta->logo_size);
        if (!w->meta.logo_bytes) return OUCS_ERR_NOMEM;
        memcpy(w->meta.logo_bytes, meta->logo_bytes, meta->logo_size);
        w->meta.logo_size = meta->logo_size;
    }
    return OUCS_OK;
}

int oucs_writer_add_song(OucsWriter *w, const char *audio_path,
                          const char *name, const char *description,
                          OucsIndexEntry *entry_out) {
    if (!w || !audio_path) return OUCS_ERR_NULL_PARAM;

    /* fire before-pack hook */
    oucs_fire_hook(w, OUCS_HOOK_BEFORE_PACK, (void *)audio_path);

    /* read file into memory */
    FILE *fp = fopen(audio_path, "rb");
    if (!fp) return OUCS_ERR_IO;

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    rewind(fp);
    if (sz <= 0) { fclose(fp); return OUCS_ERR_IO; }

    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) { fclose(fp); return OUCS_ERR_NOMEM; }
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        free(buf); fclose(fp); return OUCS_ERR_IO;
    }
    fclose(fp);

    /* detect format from extension */
    char fmt[5] = "MP3 ";
    const char *ext = strrchr(audio_path, '.');
    if (ext) {
        if      (strcasecmp(ext, ".flac") == 0) { fmt[0]='F';fmt[1]='L';fmt[2]='A';fmt[3]='C'; }
        else if (strcasecmp(ext, ".ogg")  == 0) { fmt[0]='O';fmt[1]='G';fmt[2]='G';fmt[3]=' '; }
        else if (strcasecmp(ext, ".wav")  == 0) { fmt[0]='W';fmt[1]='A';fmt[2]='V';fmt[3]=' '; }
        else if (strcasecmp(ext, ".aac")  == 0) { fmt[0]='A';fmt[1]='A';fmt[2]='C';fmt[3]=' '; }
        else if (strcasecmp(ext, ".opus") == 0) { fmt[0]='O';fmt[1]='P';fmt[2]='U';fmt[3]='S'; }
    }
    fmt[4] = '\0';

    return oucs_writer_add_song_mem(w, buf, (size_t)sz, fmt, name, description, entry_out);
}

int oucs_writer_add_song_mem(OucsWriter *w, const uint8_t *data, size_t size,
                              const char *audio_format, const char *name,
                              const char *description, OucsIndexEntry *entry_out) {
    if (!w || !data || size == 0) return OUCS_ERR_NULL_PARAM;

    OucsPendingSong *ps = (OucsPendingSong *)calloc(1, sizeof(OucsPendingSong));
    if (!ps) return OUCS_ERR_NOMEM;

    ps->audio_data = (uint8_t *)malloc(size);
    if (!ps->audio_data) { free(ps); return OUCS_ERR_NOMEM; }
    memcpy(ps->audio_data, data, size);
    ps->audio_size = size;

    /* populate index entry */
    OucsIndexEntry *e = &ps->entry;
    oucs_uuid_generate(e->uuid);
    if (name)        strncpy(e->name,        name,        OUCS_SONG_NAME_MAX - 1);
    if (description) strncpy(e->description, description, OUCS_SONG_DESC_MAX - 1);
    if (audio_format) memcpy(e->audio_format, audio_format, 4);
    else              memcpy(e->audio_format, "MP3 ", 4);
    e->byte_size        = (uint64_t)size;
    e->crc32            = oucs_crc32_compute(data, size);
    e->track_number     = (uint8_t)(w->song_count + 1);

    /* append to linked list */
    ps->next = NULL;
    if (!w->songs_head) {
        w->songs_head = w->songs_tail = ps;
    } else {
        w->songs_tail->next = ps;
        w->songs_tail       = ps;
    }
    w->song_count++;

    if (entry_out) memcpy(entry_out, e, sizeof(OucsIndexEntry));

    /* fire after-pack hook */
    oucs_fire_hook(w, OUCS_HOOK_AFTER_PACK, e);

    return OUCS_OK;
}

int oucs_writer_set_lyrics(OucsWriter *w, const OucsLyrics *lyrics) {
    if (!w || !lyrics || !w->songs_tail) return OUCS_ERR_NULL_PARAM;
    OucsLyrics *lc = (OucsLyrics *)malloc(sizeof(OucsLyrics));
    if (!lc) return OUCS_ERR_NOMEM;
    lc->count   = lyrics->count;
    lc->entries = (OucsLyricEntry *)malloc(lyrics->count * sizeof(OucsLyricEntry));
    if (!lc->entries) { free(lc); return OUCS_ERR_NOMEM; }
    for (uint32_t i = 0; i < lyrics->count; i++) {
        lc->entries[i].timestamp_ms = lyrics->entries[i].timestamp_ms;
        if (lyrics->entries[i].text) {
            lc->entries[i].text = strdup(lyrics->entries[i].text);
        } else {
            lc->entries[i].text = NULL;
        }
    }
    w->songs_tail->lyrics = lc;
    return OUCS_OK;
}

int oucs_writer_set_chapters(OucsWriter *w, const OucsChapters *chapters) {
    if (!w || !chapters || !w->songs_tail) return OUCS_ERR_NULL_PARAM;
    OucsChapters *cc = (OucsChapters *)malloc(sizeof(OucsChapters));
    if (!cc) return OUCS_ERR_NOMEM;
    cc->count   = chapters->count;
    cc->entries = (OucsChapter *)malloc(chapters->count * sizeof(OucsChapter));
    if (!cc->entries) { free(cc); return OUCS_ERR_NOMEM; }
    for (uint32_t i = 0; i < chapters->count; i++) {
        cc->entries[i].offset_ms = chapters->entries[i].offset_ms;
        if (chapters->entries[i].name) {
            cc->entries[i].name = strdup(chapters->entries[i].name);
        } else {
            cc->entries[i].name = NULL;
        }
    }
    w->songs_tail->chapters = cc;
    return OUCS_OK;
}

int oucs_writer_set_accessibility(OucsWriter *w, const OucsAccessibility *acc) {
    if (!w || !acc || !w->songs_tail) return OUCS_ERR_NULL_PARAM;
    OucsAccessibility *ac = (OucsAccessibility *)malloc(sizeof(OucsAccessibility));
    if (!ac) return OUCS_ERR_NOMEM;
    memcpy(ac->language, acc->language, 4);
    if (acc->audio_description) {
        ac->audio_description = strdup(acc->audio_description);
    } else {
        ac->audio_description = NULL;
    }
    if (acc->transcript) {
        ac->transcript = strdup(acc->transcript);
    } else {
        ac->transcript = NULL;
    }
    w->songs_tail->accessibility = ac;
    return OUCS_OK;
}

int oucs_writer_encrypt_song(OucsWriter *w, const char *password) {
    if (!w || !password || !w->songs_tail) return OUCS_ERR_NULL_PARAM;
    strncpy(w->songs_tail->password, password, 255);
    w->songs_tail->entry.encryption_flag = OUCS_ENC_AES256GCM;
    return OUCS_OK;
}

int oucs_writer_register_hook(OucsWriter *w, OucsHookType type, void *fn, void *userdata) {
    if (!w || !fn) return OUCS_ERR_NULL_PARAM;
    if (w->hook_count >= OUCS_MAX_HOOKS) return OUCS_ERR_OVERFLOW;
    w->hooks[w->hook_count].type     = type;
    w->hooks[w->hook_count].fn       = fn;
    w->hooks[w->hook_count].userdata = userdata;
    w->hook_count++;
    return OUCS_OK;
}

/* ─────────────────────────────────────────────────────────────
   FINALIZE — write the .oucs file
───────────────────────────────────────────────────────────── */

int oucs_writer_finalize(OucsWriter *w) {
    if (!w) return OUCS_ERR_NULL_PARAM;

    FILE *fp = fopen(w->path, "wb");
    if (!fp) return OUCS_ERR_IO;

    /* ── PHASE 1: collect all data offsets ─────────────────── */
    /* Layout:
       [Header 44 bytes]
       [Container meta block]
       [Index table: song_count * 512 bytes]  <- offsets TBD
       [Song units: audio + aux blocks]
    */

    uint64_t cur_offset = OUCS_HEADER_SIZE;

    /* ── Write placeholder header (will rewrite at end) ─────── */
    OucsFileHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, OUCS_MAGIC, 4);
    hdr.version_major            = OUCS_VERSION_MAJOR;
    hdr.version_minor            = OUCS_VERSION_MINOR;
    hdr.flags                    = w->flags;
    hdr.song_count               = w->song_count;
    if (fwrite(&hdr, 1, sizeof(hdr), fp) != sizeof(hdr)) goto io_err;
    cur_offset = sizeof(hdr);

    /* ── Container metadata block ──────────────────────────── */
    uint64_t meta_offset = cur_offset;
    {
        /* write theme_name */
        char name_buf[OUCS_META_NAME_MAX];
        memset(name_buf, 0, sizeof(name_buf));
        strncpy(name_buf, w->meta.theme_name, OUCS_META_NAME_MAX - 1);
        if (fwrite(name_buf, 1, OUCS_META_NAME_MAX, fp) != OUCS_META_NAME_MAX) goto io_err;
        cur_offset += OUCS_META_NAME_MAX;

        /* description */
        char desc_buf[OUCS_META_DESC_MAX];
        memset(desc_buf, 0, sizeof(desc_buf));
        strncpy(desc_buf, w->meta.description, OUCS_META_DESC_MAX - 1);
        if (fwrite(desc_buf, 1, OUCS_META_DESC_MAX, fp) != OUCS_META_DESC_MAX) goto io_err;
        cur_offset += OUCS_META_DESC_MAX;

        /* logo */
        uint32_t logo_sz = w->meta.logo_size;
        if (fwrite(&logo_sz, 4, 1, fp) != 1) goto io_err;
        cur_offset += 4;
        if (logo_sz > 0 && w->meta.logo_bytes) {
            if (fwrite(w->meta.logo_bytes, 1, logo_sz, fp) != logo_sz) goto io_err;
            cur_offset += logo_sz;
        }

        /* ext url */
        char url_buf[OUCS_META_URL_MAX];
        memset(url_buf, 0, sizeof(url_buf));
        strncpy(url_buf, w->meta.logo_ext_url, OUCS_META_URL_MAX - 1);
        if (fwrite(url_buf, 1, OUCS_META_URL_MAX, fp) != OUCS_META_URL_MAX) goto io_err;
        cur_offset += OUCS_META_URL_MAX;

        /* created_at */
        if (fwrite(&w->meta.created_at, 8, 1, fp) != 1) goto io_err;
        cur_offset += 8;

        /* author uuid + name (32 bytes total) */
        if (fwrite(w->meta.author_uuid, 1, OUCS_UUID_SIZE, fp) != OUCS_UUID_SIZE) goto io_err;
        char aname[16]; memset(aname, 0, 16);
        strncpy(aname, w->meta.author_name, 15);
        if (fwrite(aname, 1, 16, fp) != 16) goto io_err;
        cur_offset += OUCS_UUID_SIZE + 16;

        /* crc32 placeholder (0 for now — could compute over meta block) */
        uint32_t meta_crc = 0;
        if (fwrite(&meta_crc, 4, 1, fp) != 1) goto io_err;
        cur_offset += 4;
    }

    /* ── Index table placeholder ───────────────────────────── */
    /* Align to 512 bytes */
    while (cur_offset % 512 != 0) {
        uint8_t pad = 0;
        fwrite(&pad, 1, 1, fp);
        cur_offset++;
    }
    uint64_t index_offset = cur_offset;
    size_t index_table_bytes = (size_t)w->song_count * OUCS_INDEX_ENTRY_SIZE;

    /* Write zeroed index table as placeholder */
    {
        uint8_t *zero_index = (uint8_t *)calloc(1, index_table_bytes);
        if (!zero_index) goto mem_err;
        if (fwrite(zero_index, 1, index_table_bytes, fp) != index_table_bytes) {
            free(zero_index); goto io_err;
        }
        free(zero_index);
    }
    cur_offset += index_table_bytes;

    /* ── Write song units & collect final index entries ─────── */
    OucsIndexEntry *final_entries = (OucsIndexEntry *)calloc(w->song_count, OUCS_INDEX_ENTRY_SIZE);
    if (!final_entries) goto mem_err;

    uint32_t song_idx = 0;
    OucsPendingSong *ps = w->songs_head;

    while (ps) {
        /* align to 512 bytes */
        while (cur_offset % 512 != 0) {
            uint8_t pad = 0;
            fwrite(&pad, 1, 1, fp);
            cur_offset++;
        }

        OucsIndexEntry *fe = &final_entries[song_idx];
        memcpy(fe, &ps->entry, sizeof(OucsIndexEntry));
        fe->byte_offset = cur_offset;

        /* TODO: encryption (oucs_crypto.c wires in here) */
        /* For now write raw audio */
        if (fwrite(ps->audio_data, 1, ps->audio_size, fp) != ps->audio_size) {
            free(final_entries); goto io_err;
        }
        cur_offset += ps->audio_size;
        fe->byte_size = ps->audio_size;

        /* ── ECC block placeholder (oucs_ecc.c fills) ─────── */
        /* Written by oucs_ecc_compute_and_write() in oucs_ecc.c */
        fe->ecc_offset = 0;
        fe->ecc_size   = 0;

        /* ── Lyrics block ──────────────────────────────────── */
        if (ps->lyrics && ps->lyrics->count > 0) {
            fe->lyrics_offset = cur_offset;
            uint32_t lcount = ps->lyrics->count;
            fwrite(&lcount, 4, 1, fp); cur_offset += 4;
            for (uint32_t i = 0; i < lcount; i++) {
                fwrite(&ps->lyrics->entries[i].timestamp_ms, 4, 1, fp);
                cur_offset += 4;
                const char *txt = ps->lyrics->entries[i].text ? ps->lyrics->entries[i].text : "";
                uint16_t tlen = (uint16_t)strlen(txt);
                fwrite(&tlen, 2, 1, fp); cur_offset += 2;
                fwrite(txt, 1, tlen, fp); cur_offset += tlen;
            }
            fe->lyrics_size = (uint32_t)(cur_offset - fe->lyrics_offset);
        }

        /* ── Chapters block ────────────────────────────────── */
        if (ps->chapters && ps->chapters->count > 0) {
            fe->chapters_offset = cur_offset;
            uint32_t ccount = ps->chapters->count;
            fwrite(&ccount, 4, 1, fp); cur_offset += 4;
            for (uint32_t i = 0; i < ccount; i++) {
                fwrite(&ps->chapters->entries[i].offset_ms, 4, 1, fp);
                cur_offset += 4;
                const char *n = ps->chapters->entries[i].name ? ps->chapters->entries[i].name : "";
                uint16_t nlen = (uint16_t)strlen(n);
                fwrite(&nlen, 2, 1, fp); cur_offset += 2;
                fwrite(n, 1, nlen, fp); cur_offset += nlen;
            }
            fe->chapters_count = ccount;
        }

        /* ── Accessibility block ───────────────────────────── */
        if (ps->accessibility) {
            fe->accessibility_offset = cur_offset;
            OucsAccessibility *ac = ps->accessibility;
            fwrite(ac->language, 1, 4, fp); cur_offset += 4;
            const char *adesc = ac->audio_description ? ac->audio_description : "";
            uint32_t adlen = (uint32_t)strlen(adesc);
            fwrite(&adlen, 4, 1, fp); cur_offset += 4;
            fwrite(adesc, 1, adlen, fp); cur_offset += adlen;
            const char *tr = ac->transcript ? ac->transcript : "";
            uint32_t trlen = (uint32_t)strlen(tr);
            fwrite(&trlen, 4, 1, fp); cur_offset += 4;
            fwrite(tr, 1, trlen, fp); cur_offset += trlen;
        }

        song_idx++;
        ps = ps->next;
    }

    /* ── Rewrite index table with final offsets ─────────────── */
    fseek(fp, (long)index_offset, SEEK_SET);
    if (fwrite(final_entries, OUCS_INDEX_ENTRY_SIZE, w->song_count, fp) != w->song_count) {
        free(final_entries); goto io_err;
    }
    free(final_entries);

    /* ── Rewrite file header with real offsets ───────────────── */
    fseek(fp, 0, SEEK_SET);
    hdr.container_meta_offset    = meta_offset;
    hdr.index_table_offset       = index_offset;
    hdr.sync_manifest_offset     = 0;
    hdr.version_history_offset   = 0;
    if (fwrite(&hdr, 1, sizeof(hdr), fp) != sizeof(hdr)) goto io_err;

    fclose(fp);
    return OUCS_OK;

io_err:
    fclose(fp);
    return OUCS_ERR_IO;

mem_err:
    fclose(fp);
    return OUCS_ERR_NOMEM;
}

void oucs_writer_free(OucsWriter *w) {
    if (!w) return;
    OucsPendingSong *ps = w->songs_head;
    while (ps) {
        OucsPendingSong *next = ps->next;
        free(ps->audio_data);
        if (ps->lyrics) {
            oucs_lyrics_free(ps->lyrics);
            free(ps->lyrics);
        }
        if (ps->chapters) {
            oucs_chapters_free(ps->chapters);
            free(ps->chapters);
        }
        if (ps->accessibility) {
            oucs_accessibility_free(ps->accessibility);
            free(ps->accessibility);
        }
        free(ps);
        ps = next;
    }
    if (w->meta.logo_bytes) free(w->meta.logo_bytes);
    free(w);
}

/* ─────────────────────────────────────────────────────────────
   INTERNAL HELPERS
───────────────────────────────────────────────────────────── */

/* CRC-32 (ISO 3309 polynomial) */
static uint32_t oucs_crc32_compute(const uint8_t *data, size_t len) {
    static uint32_t table[256];
    static int table_ready = 0;
    if (!table_ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        table_ready = 1;
    }
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++)
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFF;
}

static void oucs_fire_hook(OucsWriter *w, OucsHookType type, void *arg) {
    for (uint32_t i = 0; i < w->hook_count; i++) {
        if (w->hooks[i].type != type) continue;
        switch (type) {
            case OUCS_HOOK_BEFORE_PACK:
                ((OucsHookBeforePack)w->hooks[i].fn)((const char *)arg, w->hooks[i].userdata);
                break;
            case OUCS_HOOK_AFTER_PACK:
                ((OucsHookAfterPack)w->hooks[i].fn)((const OucsIndexEntry *)arg, w->hooks[i].userdata);
                break;
            default: break;
        }
    }
}

static int oucs_write_block(FILE *fp, const void *data, size_t size, uint64_t *offset) {
    if (fwrite(data, 1, size, fp) != size) return OUCS_ERR_IO;
    *offset += size;
    return OUCS_OK;
}

/* ─────────────────────────────────────────────────────────────
   MERGE & SPLIT
───────────────────────────────────────────────────────────── */

int oucs_merge(const char **inputs, uint32_t input_count, const char *output) {
    if (!inputs || input_count == 0 || !output) return OUCS_ERR_NULL_PARAM;

    OucsWriter *w = oucs_writer_create(output, 0);
    if (!w) return OUCS_ERR_NOMEM;

    for (uint32_t f = 0; f < input_count; f++) {
        OucsReader *r = oucs_reader_open(inputs[f]);
        if (!r) { oucs_writer_free(w); return OUCS_ERR_IO; }

        int count = oucs_reader_song_count(r);
        for (int i = 0; i < count; i++) {
            OucsIndexEntry entry;
            oucs_reader_get_song_info(r, i, &entry);
            uint8_t *data = NULL;
            size_t   size = 0;
            int ret = oucs_reader_extract_song_mem(r, i, &data, &size, NULL);
            if (ret != OUCS_OK) { oucs_reader_free(r); oucs_writer_free(w); return ret; }

            char fmt[5]; memcpy(fmt, entry.audio_format, 4); fmt[4] = 0;
            oucs_writer_add_song_mem(w, data, size, fmt, entry.name, entry.description, NULL);
            free(data);
        }
        oucs_reader_free(r);
    }

    int ret = oucs_writer_finalize(w);
    oucs_writer_free(w);
    return ret;
}

int oucs_split(const char *input, uint32_t from_idx, uint32_t to_idx, const char *output) {
    if (!input || !output) return OUCS_ERR_NULL_PARAM;

    OucsReader *r = oucs_reader_open(input);
    if (!r) return OUCS_ERR_IO;

    int total = oucs_reader_song_count(r);
    if ((int)to_idx >= total) to_idx = (uint32_t)(total - 1);
    if (from_idx > to_idx) { oucs_reader_free(r); return OUCS_ERR_INVALID_ARG; }

    OucsWriter *w = oucs_writer_create(output, 0);
    if (!w) { oucs_reader_free(r); return OUCS_ERR_NOMEM; }

    for (uint32_t i = from_idx; i <= to_idx; i++) {
        OucsIndexEntry entry;
        oucs_reader_get_song_info(r, i, &entry);
        uint8_t *data = NULL;
        size_t   size = 0;
        int ret = oucs_reader_extract_song_mem(r, i, &data, &size, NULL);
        if (ret != OUCS_OK) { oucs_reader_free(r); oucs_writer_free(w); return ret; }

        char fmt[5]; memcpy(fmt, entry.audio_format, 4); fmt[4] = 0;
        oucs_writer_add_song_mem(w, data, size, fmt, entry.name, entry.description, NULL);
        free(data);
    }

    oucs_reader_free(r);
    int ret = oucs_writer_finalize(w);
    oucs_writer_free(w);
    return ret;
}

int oucs_append_song(const char *oucs_path, const char *audio_path,
                     const char *name, const char *description) {
    if (!oucs_path || !audio_path) return OUCS_ERR_NULL_PARAM;

    /* Read existing file */
    OucsReader *r = oucs_reader_open(oucs_path);
    if (!r) return OUCS_ERR_IO;

    int count = oucs_reader_song_count(r);
    OucsContainerMeta meta;
    oucs_reader_get_container_meta(r, &meta);

    /* Create a new writer with a temp path */
    char tmp_path[4096];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", oucs_path);
    OucsWriter *w = oucs_writer_create(tmp_path, 0);
    if (!w) { oucs_reader_free(r); return OUCS_ERR_NOMEM; }
    oucs_writer_set_container_meta(w, &meta);

    /* Copy existing songs */
    for (int i = 0; i < count; i++) {
        OucsIndexEntry entry;
        oucs_reader_get_song_info(r, i, &entry);
        uint8_t *data = NULL; size_t sz = 0;
        oucs_reader_extract_song_mem(r, i, &data, &sz, NULL);
        char fmt[5]; memcpy(fmt, entry.audio_format, 4); fmt[4] = 0;
        oucs_writer_add_song_mem(w, data, sz, fmt, entry.name, entry.description, NULL);
        free(data);
    }
    oucs_reader_free(r);
    oucs_container_meta_free(&meta);

    /* Add new song */
    oucs_writer_add_song(w, audio_path, name, description, NULL);

    int ret = oucs_writer_finalize(w);
    oucs_writer_free(w);

    if (ret == OUCS_OK) {
        remove(oucs_path);
        rename(tmp_path, oucs_path);
    }
    return ret;
}
