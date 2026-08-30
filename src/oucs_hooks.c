/**
 * OUCS Engine - Plugin / Hook System
 * oucs_hooks.c
 *
 * Implements the hook registration and dispatch system.
 * Developers can register callbacks to intercept events:
 *   - OUCS_HOOK_BEFORE_PACK
 *   - OUCS_HOOK_AFTER_PACK
 *   - OUCS_HOOK_CHUNK_READ
 *   - OUCS_HOOK_AFTER_EXTRACT
 *   - OUCS_HOOK_ON_ERROR
 *
 * License: MIT
 */

#include "oucs_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ─────────────────────────────────────────────────────────────
   GLOBAL HOOK REGISTRY
   Hooks registered globally apply to ALL writer/reader contexts.
───────────────────────────────────────────────────────────── */

static OucsHook  g_hooks[OUCS_MAX_HOOKS];
static uint32_t  g_hook_count = 0;

/**
 * Register a global hook that fires for all oucs operations.
 */
int oucs_hook_register_global(OucsHookType type, void *fn, void *userdata) {
    if (!fn) return OUCS_ERR_NULL_PARAM;
    if (g_hook_count >= OUCS_MAX_HOOKS) return OUCS_ERR_OVERFLOW;
    g_hooks[g_hook_count].type     = type;
    g_hooks[g_hook_count].fn       = fn;
    g_hooks[g_hook_count].userdata = userdata;
    g_hook_count++;
    return OUCS_OK;
}

/**
 * Unregister a global hook by function pointer.
 */
int oucs_hook_unregister_global(OucsHookType type, void *fn) {
    for (uint32_t i = 0; i < g_hook_count; i++) {
        if (g_hooks[i].type == type && g_hooks[i].fn == fn) {
            /* Shift remaining hooks left */
            memmove(&g_hooks[i], &g_hooks[i + 1],
                    (g_hook_count - i - 1) * sizeof(OucsHook));
            g_hook_count--;
            return OUCS_OK;
        }
    }
    return OUCS_ERR_NOT_FOUND;
}

/**
 * Clear all global hooks.
 */
void oucs_hook_clear_all(void) {
    g_hook_count = 0;
    memset(g_hooks, 0, sizeof(g_hooks));
}

/* ─────────────────────────────────────────────────────────────
   HOOK DISPATCH HELPERS
   Called internally by encoder/decoder/stream.
───────────────────────────────────────────────────────────── */

/**
 * Fire OUCS_HOOK_BEFORE_PACK hooks (writer-local + global).
 */
void oucs_hooks_fire_before_pack(const OucsHook *local_hooks, uint32_t local_count,
                                  const char *song_path) {
    /* Fire local hooks */
    for (uint32_t i = 0; i < local_count; i++) {
        if (local_hooks[i].type == OUCS_HOOK_BEFORE_PACK && local_hooks[i].fn)
            ((OucsHookBeforePack)local_hooks[i].fn)(song_path, local_hooks[i].userdata);
    }
    /* Fire global hooks */
    for (uint32_t i = 0; i < g_hook_count; i++) {
        if (g_hooks[i].type == OUCS_HOOK_BEFORE_PACK && g_hooks[i].fn)
            ((OucsHookBeforePack)g_hooks[i].fn)(song_path, g_hooks[i].userdata);
    }
}

/**
 * Fire OUCS_HOOK_AFTER_PACK hooks.
 */
void oucs_hooks_fire_after_pack(const OucsHook *local_hooks, uint32_t local_count,
                                 const OucsIndexEntry *entry) {
    for (uint32_t i = 0; i < local_count; i++) {
        if (local_hooks[i].type == OUCS_HOOK_AFTER_PACK && local_hooks[i].fn)
            ((OucsHookAfterPack)local_hooks[i].fn)(entry, local_hooks[i].userdata);
    }
    for (uint32_t i = 0; i < g_hook_count; i++) {
        if (g_hooks[i].type == OUCS_HOOK_AFTER_PACK && g_hooks[i].fn)
            ((OucsHookAfterPack)g_hooks[i].fn)(entry, g_hooks[i].userdata);
    }
}

/**
 * Fire OUCS_HOOK_CHUNK_READ hooks.
 * Called by oucs_stream_read_chunk() for each chunk.
 */
void oucs_hooks_fire_chunk_read(const OucsHook *local_hooks, uint32_t local_count,
                                 const uint8_t *chunk, size_t size) {
    for (uint32_t i = 0; i < local_count; i++) {
        if (local_hooks[i].type == OUCS_HOOK_CHUNK_READ && local_hooks[i].fn)
            ((OucsHookChunkRead)local_hooks[i].fn)(chunk, size, local_hooks[i].userdata);
    }
    for (uint32_t i = 0; i < g_hook_count; i++) {
        if (g_hooks[i].type == OUCS_HOOK_CHUNK_READ && g_hooks[i].fn)
            ((OucsHookChunkRead)g_hooks[i].fn)(chunk, size, g_hooks[i].userdata);
    }
}

/**
 * Fire OUCS_HOOK_AFTER_EXTRACT hooks.
 */
void oucs_hooks_fire_after_extract(const OucsHook *local_hooks, uint32_t local_count,
                                    const OucsIndexEntry *entry) {
    for (uint32_t i = 0; i < local_count; i++) {
        if (local_hooks[i].type == OUCS_HOOK_AFTER_EXTRACT && local_hooks[i].fn)
            ((OucsHookAfterExtract)local_hooks[i].fn)(entry, local_hooks[i].userdata);
    }
    for (uint32_t i = 0; i < g_hook_count; i++) {
        if (g_hooks[i].type == OUCS_HOOK_AFTER_EXTRACT && g_hooks[i].fn)
            ((OucsHookAfterExtract)g_hooks[i].fn)(entry, g_hooks[i].userdata);
    }
}

/**
 * Fire OUCS_HOOK_ON_ERROR hooks.
 */
void oucs_hooks_fire_error(const OucsHook *local_hooks, uint32_t local_count,
                            OucsError err, const char *msg) {
    for (uint32_t i = 0; i < local_count; i++) {
        if (local_hooks[i].type == OUCS_HOOK_ON_ERROR && local_hooks[i].fn)
            ((OucsHookOnError)local_hooks[i].fn)(err, msg, local_hooks[i].userdata);
    }
    for (uint32_t i = 0; i < g_hook_count; i++) {
        if (g_hooks[i].type == OUCS_HOOK_ON_ERROR && g_hooks[i].fn)
            ((OucsHookOnError)g_hooks[i].fn)(err, msg, g_hooks[i].userdata);
    }
}

/* ─────────────────────────────────────────────────────────────
   EXAMPLE PLUGINS (can be used directly by developers)
───────────────────────────────────────────────────────────── */

/**
 * Example plugin: prints per-chunk RMS volume level.
 * Register with: oucs_stream_register_hook(s, OUCS_HOOK_CHUNK_READ,
 *                                           oucs_plugin_volume_monitor, NULL);
 */
void oucs_plugin_volume_monitor(const uint8_t *chunk, size_t size, void *userdata) {
    (void)userdata;
    if (!chunk || size == 0) return;

    /* Compute RMS of signed 16-bit PCM samples */
    double sum = 0.0;
    size_t samples = size / 2;
    if (samples == 0) return;

    const int16_t *pcm = (const int16_t *)chunk;
    for (size_t i = 0; i < samples; i++) {
        double s = (double)pcm[i] / 32768.0;
        sum += s * s;
    }
    double rms = sqrt(sum / (double)samples);
    int db = (rms > 1e-10) ? (int)(20.0 * log10(rms)) : -96;

    /* ASCII VU meter */
    int level = (db + 60) * 40 / 60;
    if (level < 0) level = 0;
    if (level > 40) level = 40;

    printf("\r[VOLUME] %4d dBFS [", db);
    for (int i = 0; i < 40; i++) putchar(i < level ? '#' : ' ');
    printf("] ");
    fflush(stdout);
}

/**
 * Example plugin: logs each pack operation.
 */
void oucs_plugin_pack_logger(const char *song_path, void *userdata) {
    (void)userdata;
    printf("[PACK] Adding: %s\n", song_path ? song_path : "(memory)");
}

/**
 * Example plugin: logs errors.
 */
void oucs_plugin_error_logger(OucsError err, const char *msg, void *userdata) {
    (void)userdata;
    fprintf(stderr, "[ERROR] Code %d: %s\n", (int)err, msg ? msg : oucs_strerror(err));
}
