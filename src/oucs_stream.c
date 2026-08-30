/**
 * OUCS Engine - Streaming API
 * oucs_stream.c
 *
 * Implements chunk-by-chunk low-memory streaming of a single song
 * from a .oucs container. Only the current chunk is ever in memory.
 *
 * License: MIT
 */

#include "oucs_internal.h"
#include <stdlib.h>
#include <string.h>

/* ─────────────────────────────────────────────────────────────
   OPEN / CLOSE
───────────────────────────────────────────────────────────── */

OucsStream *oucs_stream_open(OucsReader *r, uint32_t idx,
                              size_t chunk_size, const char *password) {
    if (!r) return NULL;
    if (idx >= r->header.song_count) return NULL;
    if (!r->index) return NULL;

    OucsStream *s = (OucsStream *)calloc(1, sizeof(OucsStream));
    if (!s) return NULL;

    s->reader      = r;
    s->song_idx    = idx;
    s->chunk_size  = (chunk_size > 0) ? chunk_size : OUCS_CHUNK_SIZE_DEFAULT;
    s->pos         = 0;
    s->chapters_loaded = 0;

    OucsIndexEntry *e = &r->index[idx];
    s->audio_start = e->byte_offset;
    s->audio_size  = e->byte_size;

    if (password) strncpy(s->password, password, 255);

    return s;
}

void oucs_stream_free(OucsStream *s) {
    if (!s) return;
    if (s->chapters_loaded) {
        oucs_chapters_free(&s->chapters);
    }
    free(s);
}

/* ─────────────────────────────────────────────────────────────
   READ CHUNK
───────────────────────────────────────────────────────────── */

int oucs_stream_read_chunk(OucsStream *s, uint8_t *buf, size_t buf_size, size_t *bytes_read) {
    if (!s || !buf || !bytes_read) return OUCS_ERR_NULL_PARAM;
    *bytes_read = 0;

    if (s->pos >= s->audio_size) {
        return OUCS_OK; /* EOF */
    }

    uint64_t remaining = s->audio_size - s->pos;
    size_t   to_read   = (size_t)(remaining < (uint64_t)buf_size ? remaining : (uint64_t)buf_size);
    to_read = (to_read < s->chunk_size) ? to_read : s->chunk_size;

    uint64_t abs_offset = s->audio_start + s->pos;
    int ret = oucs_pread(s->reader->fp, buf, to_read, abs_offset);
    if (ret != OUCS_OK) return ret;

    s->pos     += to_read;
    *bytes_read = to_read;

    /* Fire chunk-read hooks */
    for (uint32_t i = 0; i < s->hook_count; i++) {
        if (s->hooks[i].type == OUCS_HOOK_CHUNK_READ) {
            ((OucsHookChunkRead)s->hooks[i].fn)(buf, to_read, s->hooks[i].userdata);
        }
    }

    return OUCS_OK;
}

/* ─────────────────────────────────────────────────────────────
   SEEK
───────────────────────────────────────────────────────────── */

int oucs_stream_seek(OucsStream *s, uint64_t byte_offset) {
    if (!s) return OUCS_ERR_NULL_PARAM;
    if (byte_offset > s->audio_size) return OUCS_ERR_INVALID_ARG;
    s->pos = byte_offset;
    return OUCS_OK;
}

int oucs_stream_seek_chapter(OucsStream *s, uint32_t chapter_idx) {
    if (!s) return OUCS_ERR_NULL_PARAM;

    /* Load chapters if not already loaded */
    if (!s->chapters_loaded) {
        int ret = oucs_reader_get_chapters(s->reader, s->song_idx, &s->chapters);
        if (ret != OUCS_OK) return ret;
        s->chapters_loaded = 1;
    }

    if (chapter_idx >= s->chapters.count) return OUCS_ERR_NOT_FOUND;

    /* Convert millisecond offset to approximate byte offset.
       We use a linear mapping: pos = (ms / duration_ms) * audio_size.
       This is approximate; an exact mapping requires audio format parsing. */
    OucsIndexEntry *e = &s->reader->index[s->song_idx];
    uint32_t chapter_ms  = s->chapters.entries[chapter_idx].offset_ms;
    uint32_t duration_ms = e->duration_ms;

    if (duration_ms > 0) {
        s->pos = (uint64_t)((double)chapter_ms / (double)duration_ms * (double)s->audio_size);
    } else {
        s->pos = 0;
    }
    return OUCS_OK;
}

uint64_t oucs_stream_tell(const OucsStream *s) {
    if (!s) return 0;
    return s->pos;
}

int oucs_stream_register_hook(OucsStream *s, OucsHookType type, void *fn, void *userdata) {
    if (!s || !fn) return OUCS_ERR_NULL_PARAM;
    if (s->hook_count >= OUCS_MAX_HOOKS) return OUCS_ERR_OVERFLOW;
    s->hooks[s->hook_count].type     = type;
    s->hooks[s->hook_count].fn       = fn;
    s->hooks[s->hook_count].userdata = userdata;
    s->hook_count++;
    return OUCS_OK;
}
