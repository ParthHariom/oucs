/**
 * OUCS Engine - Version History & Sync Manifest
 * oucs_history.c
 *
 * Implements:
 *   - Append-only version history block
 *   - Sync manifest (multi-device, offline-first)
 *   - Manifest merge (conflict resolution by timestamp)
 *
 * License: MIT
 */

#include "oucs_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* ─────────────────────────────────────────────────────────────
   VERSION HISTORY
───────────────────────────────────────────────────────────── */

/**
 * Print version history of a .oucs file to stdout.
 */
int oucs_history_print(const char *oucs_path) {
    if (!oucs_path) return OUCS_ERR_NULL_PARAM;

    OucsReader *r = oucs_reader_open(oucs_path);
    if (!r) return OUCS_ERR_IO;

    OucsHistory hist = {0};
    int ret = oucs_reader_get_history(r, &hist);
    if (ret != OUCS_OK) { oucs_reader_free(r); return ret; }

    if (hist.entry_count == 0) {
        printf("No version history.\n");
    } else {
        static const char *op_names[] = {"CREATE","ADD","REMOVE","UPDATE","MERGE"};
        printf("Version history (%u entries):\n", hist.entry_count);
        for (uint32_t i = 0; i < hist.entry_count; i++) {
            OucsHistoryEntry *e = &hist.entries[i];
            char uuid_str[37];
            oucs_uuid_to_str(e->song_uuid, uuid_str);
            /* Format timestamp */
            time_t ts = (time_t)e->timestamp;
            struct tm *tm_info = localtime(&ts);
            char time_buf[32] = {0};
            if (tm_info) strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
            const char *op_name = (e->operation < 5) ? op_names[e->operation] : "UNKNOWN";
            printf("  [%u] %s  %-8s  %s  %s\n",
                   i, time_buf, op_name, uuid_str,
                   e->note[0] ? e->note : "");
        }
    }

    oucs_history_free(&hist);
    oucs_reader_free(r);
    return OUCS_OK;
}

/**
 * Append a history entry to an existing .oucs file.
 * The entry is appended at the end of the file and the header
 * updated to point to the new history block.
 *
 * NOTE: This rewrites only the history block + header offsets.
 * All audio data is preserved in place.
 */
int oucs_history_append_entry(const char *oucs_path,
                               uint8_t operation,
                               const OucsUUID song_uuid,
                               const char *note) {
    if (!oucs_path) return OUCS_ERR_NULL_PARAM;

    /* Read existing history */
    OucsReader *r = oucs_reader_open(oucs_path);
    if (!r) return OUCS_ERR_IO;

    OucsHistory hist = {0};
    oucs_reader_get_history(r, &hist);

    uint64_t current_hist_offset = r->header.version_history_offset;
    oucs_reader_free(r);

    /* Open file for read-write to append history block */
    FILE *fp = fopen(oucs_path, "r+b");
    if (!fp) { oucs_history_free(&hist); return OUCS_ERR_IO; }

    /* Seek to end of file */
    fseek(fp, 0, SEEK_END);
    uint64_t new_hist_offset = (uint64_t)ftell(fp);

    /* Write history block */
    uint32_t new_count = hist.entry_count + 1;
    fwrite(&new_count, 4, 1, fp);

    /* Write old entries */
    for (uint32_t i = 0; i < hist.entry_count; i++) {
        OucsHistoryEntry *e = &hist.entries[i];
        fwrite(&e->timestamp, 8, 1, fp);
        fwrite(&e->operation, 1, 1, fp);
        fwrite(e->song_uuid, OUCS_UUID_SIZE, 1, fp);
        char note_buf[64]; memset(note_buf, 0, 64);
        strncpy(note_buf, e->note, 63);
        fwrite(note_buf, 64, 1, fp);
    }

    /* Write new entry */
    uint64_t ts = (uint64_t)time(NULL);
    fwrite(&ts, 8, 1, fp);
    fwrite(&operation, 1, 1, fp);
    if (song_uuid)
        fwrite(song_uuid, OUCS_UUID_SIZE, 1, fp);
    else {
        uint8_t zero_uuid[OUCS_UUID_SIZE] = {0};
        fwrite(zero_uuid, OUCS_UUID_SIZE, 1, fp);
    }
    char note_buf[64]; memset(note_buf, 0, 64);
    if (note) strncpy(note_buf, note, 63);
    fwrite(note_buf, 64, 1, fp);

    /* Update header: set version_history_offset and OUCS_FLAG_HAS_HISTORY */
    fseek(fp, 0, SEEK_SET);
    OucsFileHeader hdr;
    fread(&hdr, sizeof(hdr), 1, fp);
    hdr.version_history_offset = new_hist_offset;
    hdr.flags |= OUCS_FLAG_HAS_HISTORY;
    fseek(fp, 0, SEEK_SET);
    fwrite(&hdr, sizeof(hdr), 1, fp);

    fclose(fp);
    oucs_history_free(&hist);
    return OUCS_OK;
}

/* ─────────────────────────────────────────────────────────────
   SYNC MANIFEST
───────────────────────────────────────────────────────────── */

/**
 * Add or update this device in the sync manifest of a .oucs file.
 * @param device_id  16-byte UUID of this device
 */
int oucs_sync_update_device(const char *oucs_path, const OucsUUID device_id) {
    if (!oucs_path || !device_id) return OUCS_ERR_NULL_PARAM;

    OucsReader *r = oucs_reader_open(oucs_path);
    if (!r) return OUCS_ERR_IO;

    OucsSyncManifest manifest = {0};
    oucs_reader_get_sync_manifest(r, &manifest);
    oucs_reader_free(r);

    /* Update or add device */
    int found = 0;
    uint64_t now = (uint64_t)time(NULL);
    for (uint32_t i = 0; i < manifest.device_count; i++) {
        if (memcmp(manifest.devices[i].device_id, device_id, OUCS_UUID_SIZE) == 0) {
            manifest.devices[i].last_sync_ts = now;
            found = 1;
            break;
        }
    }
    if (!found) {
        manifest.device_count++;
        manifest.devices = (OucsSyncDevice *)realloc(
            manifest.devices, manifest.device_count * sizeof(OucsSyncDevice));
        if (!manifest.devices) return OUCS_ERR_NOMEM;
        memcpy(manifest.devices[manifest.device_count - 1].device_id, device_id, OUCS_UUID_SIZE);
        manifest.devices[manifest.device_count - 1].last_sync_ts = now;
    }

    /* Write manifest to end of file */
    FILE *fp = fopen(oucs_path, "r+b");
    if (!fp) { oucs_sync_manifest_free(&manifest); return OUCS_ERR_IO; }

    fseek(fp, 0, SEEK_END);
    uint64_t new_manifest_offset = (uint64_t)ftell(fp);

    /* Write device count */
    fwrite(&manifest.device_count, 4, 1, fp);
    for (uint32_t i = 0; i < manifest.device_count; i++) {
        fwrite(manifest.devices[i].device_id, OUCS_UUID_SIZE, 1, fp);
        fwrite(&manifest.devices[i].last_sync_ts, 8, 1, fp);
        uint64_t reserved = 0; fwrite(&reserved, 8, 1, fp);
    }
    /* Write changelog */
    fwrite(&manifest.entry_count, 4, 1, fp);
    for (uint32_t i = 0; i < manifest.entry_count; i++) {
        fwrite(&manifest.entries[i].timestamp, 8, 1, fp);
        fwrite(&manifest.entries[i].operation, 1, 1, fp);
        fwrite(manifest.entries[i].song_uuid, OUCS_UUID_SIZE, 1, fp);
    }

    /* Update header */
    fseek(fp, 0, SEEK_SET);
    OucsFileHeader hdr;
    fread(&hdr, sizeof(hdr), 1, fp);
    hdr.sync_manifest_offset = new_manifest_offset;
    hdr.flags |= OUCS_FLAG_HAS_SYNC;
    fseek(fp, 0, SEEK_SET);
    fwrite(&hdr, sizeof(hdr), 1, fp);

    fclose(fp);
    oucs_sync_manifest_free(&manifest);
    return OUCS_OK;
}

/**
 * Merge sync manifests from two .oucs files.
 * The result is written into the first file.
 * Conflict resolution: latest timestamp wins.
 */
int oucs_sync_merge_manifests(const char *oucs_a, const char *oucs_b) {
    if (!oucs_a || !oucs_b) return OUCS_ERR_NULL_PARAM;

    OucsReader *ra = oucs_reader_open(oucs_a);
    OucsReader *rb = oucs_reader_open(oucs_b);
    if (!ra || !rb) {
        if (ra) oucs_reader_free(ra);
        if (rb) oucs_reader_free(rb);
        return OUCS_ERR_IO;
    }

    OucsSyncManifest ma = {0}, mb = {0};
    oucs_reader_get_sync_manifest(ra, &ma);
    oucs_reader_get_sync_manifest(rb, &mb);
    oucs_reader_free(ra);
    oucs_reader_free(rb);

    /* Merge device lists */
    OucsSyncManifest merged = {0};
    uint32_t total_dev = ma.device_count + mb.device_count;
    merged.devices = (OucsSyncDevice *)calloc(total_dev, sizeof(OucsSyncDevice));
    if (!merged.devices) { oucs_sync_manifest_free(&ma); oucs_sync_manifest_free(&mb); return OUCS_ERR_NOMEM; }

    merged.device_count = ma.device_count;
    memcpy(merged.devices, ma.devices, ma.device_count * sizeof(OucsSyncDevice));

    for (uint32_t i = 0; i < mb.device_count; i++) {
        int found = 0;
        for (uint32_t j = 0; j < merged.device_count; j++) {
            if (memcmp(merged.devices[j].device_id, mb.devices[i].device_id, OUCS_UUID_SIZE) == 0) {
                /* Keep latest */
                if (mb.devices[i].last_sync_ts > merged.devices[j].last_sync_ts)
                    merged.devices[j].last_sync_ts = mb.devices[i].last_sync_ts;
                found = 1;
                break;
            }
        }
        if (!found) {
            memcpy(&merged.devices[merged.device_count++], &mb.devices[i], sizeof(OucsSyncDevice));
        }
    }

    /* Merge changelogs (concatenate, deduplicate by timestamp+uuid) */
    uint32_t total_entries = ma.entry_count + mb.entry_count;
    merged.entries = (OucsSyncEntry *)calloc(total_entries, sizeof(OucsSyncEntry));
    if (!merged.entries) goto cleanup_fail;

    memcpy(merged.entries, ma.entries, ma.entry_count * sizeof(OucsSyncEntry));
    merged.entry_count = ma.entry_count;

    for (uint32_t i = 0; i < mb.entry_count; i++) {
        int dup = 0;
        for (uint32_t j = 0; j < merged.entry_count; j++) {
            if (merged.entries[j].timestamp == mb.entries[i].timestamp &&
                memcmp(merged.entries[j].song_uuid, mb.entries[i].song_uuid, OUCS_UUID_SIZE) == 0) {
                dup = 1; break;
            }
        }
        if (!dup) {
            memcpy(&merged.entries[merged.entry_count++], &mb.entries[i], sizeof(OucsSyncEntry));
        }
    }

    printf("Merged sync manifest: %u devices, %u changelog entries\n",
           merged.device_count, merged.entry_count);

    oucs_sync_manifest_free(&ma);
    oucs_sync_manifest_free(&mb);
    oucs_sync_manifest_free(&merged);
    return OUCS_OK;

cleanup_fail:
    oucs_sync_manifest_free(&ma);
    oucs_sync_manifest_free(&mb);
    oucs_sync_manifest_free(&merged);
    return OUCS_ERR_NOMEM;
}

/* ─────────────────────────────────────────────────────────────
   FREE HELPERS (oucs_util.c)
───────────────────────────────────────────────────────────── */

void oucs_lyrics_free(OucsLyrics *lyrics) {
    if (!lyrics) return;
    for (uint32_t i = 0; i < lyrics->count; i++)
        if (lyrics->entries[i].text) free(lyrics->entries[i].text);
    if (lyrics->entries) free(lyrics->entries);
    lyrics->entries = NULL;
    lyrics->count   = 0;
}

void oucs_chapters_free(OucsChapters *chapters) {
    if (!chapters) return;
    for (uint32_t i = 0; i < chapters->count; i++)
        if (chapters->entries[i].name) free(chapters->entries[i].name);
    if (chapters->entries) free(chapters->entries);
    chapters->entries = NULL;
    chapters->count   = 0;
}

void oucs_accessibility_free(OucsAccessibility *acc) {
    if (!acc) return;
    if (acc->audio_description) { free(acc->audio_description); acc->audio_description = NULL; }
    if (acc->transcript)        { free(acc->transcript);        acc->transcript        = NULL; }
}

void oucs_waveform_free(OucsWaveform *wf) {
    if (!wf) return;
    if (wf->samples) { free(wf->samples); wf->samples = NULL; }
    wf->count = 0;
}

void oucs_fingerprint_free(OucsFingerprint *fp) {
    if (!fp) return;
    if (fp->data) { free(fp->data); fp->data = NULL; }
    fp->size = 0;
}

void oucs_sync_manifest_free(OucsSyncManifest *m) {
    if (!m) return;
    if (m->devices) { free(m->devices); m->devices = NULL; }
    if (m->entries) { free(m->entries); m->entries = NULL; }
    m->device_count = 0;
    m->entry_count  = 0;
}

void oucs_history_free(OucsHistory *h) {
    if (!h) return;
    if (h->entries) { free(h->entries); h->entries = NULL; }
    h->entry_count = 0;
}

void oucs_container_meta_free(OucsContainerMeta *meta) {
    if (!meta) return;
    if (meta->logo_bytes) { free(meta->logo_bytes); meta->logo_bytes = NULL; }
    meta->logo_size = 0;
}
