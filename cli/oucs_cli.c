/**
 * OUCS Engine - CLI Tools
 * oucs_cli.c
 *
 * Single entry-point CLI binary that implements all subcommands:
 *   oucs-pack      Pack audio files into a .oucs container
 *   oucs-extract   Extract a specific song from a .oucs file
 *   oucs-info      Print metadata and song list from a .oucs file
 *   oucs-merge     Merge multiple .oucs files into one
 *   oucs-split     Split a .oucs file into a subset
 *   oucs-dedup     Find and report duplicate songs
 *   oucs-history   Print version history of a .oucs file
 *   oucs-stream    Stream a song chunk-by-chunk (demo)
 *
 * Usage: oucs <subcommand> [args...]
 * Or symlink/alias each subcommand name to the binary.
 *
 * License: MIT
 */

#include "../include/oucs_format.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Plugin hooks from oucs_hooks.c */
void oucs_plugin_volume_monitor(const uint8_t *chunk, size_t size, void *userdata);
void oucs_plugin_pack_logger(const char *song_path, void *userdata);

/* ─────────────────────────────────────────────────────────────
   FORWARD DECLARATIONS (extern from engine)
───────────────────────────────────────────────────────────── */

extern int oucs_history_print(const char *oucs_path);
extern int oucs_dedup_scan(const char *oucs_path);
extern uint64_t oucs_dedup_estimate_savings(const char *oucs_path);

/* ─────────────────────────────────────────────────────────────
   USAGE HELPERS
───────────────────────────────────────────────────────────── */

static void print_banner(void) {
    printf("OUCS Engine v%s — Open Universal Container for Sound\n", oucs_version());
    printf("MIT License — https://github.com/oucs-engine/oucs\n\n");
}

static void usage_main(void) {
    print_banner();
    printf("Usage: oucs <subcommand> [options]\n\n");
    printf("Subcommands:\n");
    printf("  pack      Pack audio files into a .oucs container\n");
    printf("  extract   Extract a specific song by index or UUID\n");
    printf("  info      Print container metadata and song list\n");
    printf("  merge     Merge multiple .oucs files into one\n");
    printf("  split     Split: extract a range of songs to new file\n");
    printf("  dedup     Find duplicate songs in a .oucs file\n");
    printf("  history   Print version history\n");
    printf("  stream    Stream a song chunk-by-chunk (demo)\n");
    printf("\nRun 'oucs <subcommand> --help' for detailed usage.\n");
}

/* ─────────────────────────────────────────────────────────────
   oucs pack
───────────────────────────────────────────────────────────── */

static int cmd_pack(int argc, char **argv) {
    /* Usage: oucs pack <output.oucs> [options] <song1.mp3> [song2.mp3 ...] */
    if (argc < 2) {
        printf("Usage: oucs pack <output.oucs> [options] <song1.mp3> ...\n\n");
        printf("Options:\n");
        printf("  --name <name>           Container theme name\n");
        printf("  --desc <description>    Container description\n");
        printf("  --logo <image.png>      Container logo image\n");
        printf("  --logo-url <url>        Container logo external URL\n");
        printf("  --encrypt <password>    Encrypt all songs with password\n");
        printf("  --song-name <name>      Name for the NEXT song (can repeat)\n");
        printf("  --song-desc <desc>      Description for the NEXT song\n");
        printf("\nExample:\n");
        printf("  oucs pack playlist.oucs --name \"My Playlist\" song1.mp3 song2.mp3\n");
        return 1;
    }

    const char *output_path   = argv[0];
    const char *container_name = "Untitled Playlist";
    const char *container_desc = "";
    const char *logo_path      = NULL;
    const char *logo_url       = "";
    const char *encrypt_pass   = NULL;

    /* Per-song overrides (applied to next song in sequence) */
    char next_song_name[64]  = "";
    char next_song_desc[256] = "";

    OucsWriter *w = oucs_writer_create(output_path, 0);
    if (!w) { fprintf(stderr, "Error: cannot create writer for '%s'\n", output_path); return 1; }

    /* Register verbose pack logger */
    oucs_writer_register_hook(w, OUCS_HOOK_BEFORE_PACK,
                               (void *)oucs_plugin_pack_logger, NULL);

    int songs_added = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            container_name = argv[++i];
        } else if (strcmp(argv[i], "--desc") == 0 && i + 1 < argc) {
            container_desc = argv[++i];
        } else if (strcmp(argv[i], "--logo") == 0 && i + 1 < argc) {
            logo_path = argv[++i];
        } else if (strcmp(argv[i], "--logo-url") == 0 && i + 1 < argc) {
            logo_url = argv[++i];
        } else if (strcmp(argv[i], "--encrypt") == 0 && i + 1 < argc) {
            encrypt_pass = argv[++i];
        } else if (strcmp(argv[i], "--song-name") == 0 && i + 1 < argc) {
            strncpy(next_song_name, argv[++i], 63);
        } else if (strcmp(argv[i], "--song-desc") == 0 && i + 1 < argc) {
            strncpy(next_song_desc, argv[++i], 255);
        } else if (argv[i][0] != '-') {
            /* Audio file */
            const char *song_path = argv[i];
            const char *sname = next_song_name[0] ? next_song_name : NULL;
            const char *sdesc = next_song_desc[0] ? next_song_desc : NULL;

            int ret = oucs_writer_add_song(w, song_path, sname, sdesc, NULL);
            if (ret != OUCS_OK) {
                fprintf(stderr, "Error adding '%s': %s\n", song_path, oucs_strerror(ret));
            } else {
                if (encrypt_pass) oucs_writer_encrypt_song(w, encrypt_pass);
                songs_added++;
            }
            /* Reset per-song overrides */
            next_song_name[0] = '\0';
            next_song_desc[0] = '\0';
        }
    }

    if (songs_added == 0) {
        fprintf(stderr, "Error: no songs were added.\n");
        oucs_writer_free(w);
        return 1;
    }

    /* Set container metadata */
    OucsContainerMeta meta;
    memset(&meta, 0, sizeof(meta));
    strncpy(meta.theme_name,   container_name, OUCS_META_NAME_MAX - 1);
    strncpy(meta.description,  container_desc, OUCS_META_DESC_MAX - 1);
    strncpy(meta.logo_ext_url, logo_url,        OUCS_META_URL_MAX  - 1);
    meta.created_at = (uint64_t)time(NULL);

    /* Load logo if provided */
    if (logo_path) {
        FILE *lf = fopen(logo_path, "rb");
        if (lf) {
            fseek(lf, 0, SEEK_END);
            long lsz = ftell(lf); rewind(lf);
            meta.logo_bytes = (uint8_t *)malloc((size_t)lsz);
            if (meta.logo_bytes) {
                meta.logo_size = (uint32_t)fread(meta.logo_bytes, 1, (size_t)lsz, lf);
            }
            fclose(lf);
        } else {
            fprintf(stderr, "Warning: cannot open logo '%s'\n", logo_path);
        }
    }

    oucs_writer_set_container_meta(w, &meta);
    if (meta.logo_bytes) free(meta.logo_bytes);

    int ret = oucs_writer_finalize(w);
    oucs_writer_free(w);

    if (ret != OUCS_OK) {
        fprintf(stderr, "Error finalizing: %s\n", oucs_strerror(ret));
        return 1;
    }

    printf("\nDone! Packed %d song(s) into '%s'\n", songs_added, output_path);
    return 0;
}

/* ─────────────────────────────────────────────────────────────
   oucs extract
───────────────────────────────────────────────────────────── */

static int cmd_extract(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: oucs extract <input.oucs> <index|uuid> <output_file> [--password <pw>]\n");
        printf("Example: oucs extract playlist.oucs 0 song0.mp3\n");
        return 1;
    }

    const char *input_path  = argv[0];
    const char *index_or_id = argv[1];
    const char *output_path = argv[2];
    const char *password    = NULL;

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--password") == 0 && i + 1 < argc)
            password = argv[++i];
    }

    OucsReader *r = oucs_reader_open(input_path);
    if (!r) { fprintf(stderr, "Error: cannot open '%s'\n", input_path); return 1; }

    uint32_t idx = 0;
    /* Check if UUID or integer index */
    if (strlen(index_or_id) == 36 && index_or_id[8] == '-') {
        OucsUUID uuid;
        if (oucs_uuid_from_str(index_or_id, uuid) != OUCS_OK) {
            fprintf(stderr, "Error: invalid UUID format\n");
            oucs_reader_free(r); return 1;
        }
        if (oucs_reader_find_by_uuid(r, uuid, &idx) != OUCS_OK) {
            fprintf(stderr, "Error: song UUID not found\n");
            oucs_reader_free(r); return 1;
        }
    } else {
        idx = (uint32_t)atoi(index_or_id);
    }

    printf("Extracting song [%u] from '%s' → '%s'...\n", idx, input_path, output_path);

    int ret = oucs_reader_extract_song(r, idx, output_path, password);
    oucs_reader_free(r);

    if (ret != OUCS_OK) {
        fprintf(stderr, "Error: %s\n", oucs_strerror(ret));
        return 1;
    }
    printf("Done!\n");
    return 0;
}

/* ─────────────────────────────────────────────────────────────
   oucs info
───────────────────────────────────────────────────────────── */

static const char *key_names[] = {
    "C","C#","D","D#","E","F","F#","G","G#","A","A#","B","??"
};

static void print_mood(uint8_t mood) {
    if (!mood) { printf("(unknown)"); return; }
    if (mood & OUCS_MOOD_HAPPY)     printf("Happy ");
    if (mood & OUCS_MOOD_SAD)       printf("Sad ");
    if (mood & OUCS_MOOD_ENERGETIC) printf("Energetic ");
    if (mood & OUCS_MOOD_CALM)      printf("Calm ");
    if (mood & OUCS_MOOD_ROMANTIC)  printf("Romantic ");
    if (mood & OUCS_MOOD_ANGRY)     printf("Angry ");
}

static int cmd_info(int argc, char **argv) {
    if (argc < 1) {
        printf("Usage: oucs info <input.oucs>\n");
        return 1;
    }

    OucsReader *r = oucs_reader_open(argv[0]);
    if (!r) { fprintf(stderr, "Error: cannot open '%s'\n", argv[0]); return 1; }

    OucsContainerMeta meta;
    oucs_reader_get_container_meta(r, &meta);

    int song_count = oucs_reader_song_count(r);

    printf("╔══════════════════════════════════════════════════════╗\n");
    printf("║         OUCS Container Information                   ║\n");
    printf("╚══════════════════════════════════════════════════════╝\n\n");
    printf("  File       : %s\n", argv[0]);
    printf("  Theme      : %s\n", meta.theme_name[0]  ? meta.theme_name  : "(none)");
    printf("  Description: %s\n", meta.description[0] ? meta.description : "(none)");
    printf("  Songs      : %d\n", song_count);
    printf("  Logo       : %s (%u bytes)\n",
           meta.logo_ext_url[0] ? meta.logo_ext_url : "(embedded)",
           meta.logo_size);
    if (meta.created_at > 0) {
        time_t ts = (time_t)meta.created_at;
        printf("  Created    : %s", ctime(&ts));
    }
    printf("\n");
    oucs_container_meta_free(&meta);

    printf("  %-4s  %-30s  %-6s  %-5s  %-3s  %-3s  %s\n",
           "Idx", "Name", "Format", "Dur", "BPM", "Key", "Mood");
    printf("  ────  ──────────────────────────────  ──────  "
           "─────  ───  ───  ─────────\n");

    for (int i = 0; i < song_count; i++) {
        OucsIndexEntry e;
        oucs_reader_get_song_info(r, i, &e);

        char uuid_str[37]; oucs_uuid_to_str(e.uuid, uuid_str);
        char fmt[5]; memcpy(fmt, e.audio_format, 4); fmt[4] = '\0';

        uint32_t dur_sec = e.duration_ms / 1000;
        uint32_t dur_min = dur_sec / 60;
        dur_sec %= 60;

        uint8_t key_idx = (e.musical_key <= 11) ? e.musical_key : 12;
        char enc_mark = (e.encryption_flag == OUCS_ENC_AES256GCM) ? '*' : ' ';

        printf("  [%2d]%c %-30s  %-6s  %2u:%02u  %5.1f  %-3s  ",
               i, enc_mark,
               e.name[0] ? e.name : "(untitled)",
               fmt,
               dur_min, dur_sec,
               e.bpm > 0 ? e.bpm : 0.0f,
               key_names[key_idx]);
        print_mood(e.mood_flags);
        printf("\n");

        if (e.chapters_count > 0) {
            OucsChapters ch = {0};
            if (oucs_reader_get_chapters(r, i, &ch) == OUCS_OK) {
                for (uint32_t c = 0; c < ch.count; c++) {
                    printf("        Chapter %u: %s @ %u ms\n",
                           c, ch.entries[c].name ? ch.entries[c].name : "",
                           ch.entries[c].offset_ms);
                }
                oucs_chapters_free(&ch);
            }
        }

        if (e.lyrics_offset > 0) printf("        Lyrics: yes\n");
        if (e.waveform_offset > 0) printf("        Waveform: %llu samples\n",
                                           (unsigned long long)e.waveform_size);
        if (e.accessibility_offset > 0) printf("        Accessibility: yes\n");

        printf("        UUID: %s\n", uuid_str);
        printf("        Size: %llu bytes  CRC: 0x%08X\n",
               (unsigned long long)e.byte_size, e.crc32);
    }
    printf("\n  (*) = encrypted song\n");

    oucs_reader_free(r);
    return 0;
}

/* ─────────────────────────────────────────────────────────────
   oucs merge
───────────────────────────────────────────────────────────── */

static int cmd_merge(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: oucs merge <output.oucs> <input1.oucs> <input2.oucs> [...]\n");
        printf("Example: oucs merge combined.oucs a.oucs b.oucs c.oucs\n");
        return 1;
    }

    const char *output   = argv[0];
    const char **inputs  = (const char **)(argv + 1);
    uint32_t    in_count = (uint32_t)(argc - 1);

    printf("Merging %u file(s) → '%s'...\n", in_count, output);

    int ret = oucs_merge(inputs, in_count, output);
    if (ret != OUCS_OK) {
        fprintf(stderr, "Error: %s\n", oucs_strerror(ret));
        return 1;
    }
    printf("Done!\n");
    return 0;
}

/* ─────────────────────────────────────────────────────────────
   oucs split
───────────────────────────────────────────────────────────── */

static int cmd_split(int argc, char **argv) {
    if (argc < 4) {
        printf("Usage: oucs split <input.oucs> <from_idx> <to_idx> <output.oucs>\n");
        printf("Example: oucs split playlist.oucs 0 4 first5.oucs\n");
        return 1;
    }

    const char *input  = argv[0];
    uint32_t    from   = (uint32_t)atoi(argv[1]);
    uint32_t    to     = (uint32_t)atoi(argv[2]);
    const char *output = argv[3];

    printf("Splitting '%s' [%u..%u] → '%s'...\n", input, from, to, output);

    int ret = oucs_split(input, from, to, output);
    if (ret != OUCS_OK) {
        fprintf(stderr, "Error: %s\n", oucs_strerror(ret));
        return 1;
    }
    printf("Done!\n");
    return 0;
}

/* ─────────────────────────────────────────────────────────────
   oucs dedup
───────────────────────────────────────────────────────────── */

static int cmd_dedup(int argc, char **argv) {
    if (argc < 1) {
        printf("Usage: oucs dedup <input.oucs>\n");
        return 1;
    }

    uint64_t savings = oucs_dedup_estimate_savings(argv[0]);
    int pairs = oucs_dedup_scan(argv[0]);

    if (pairs > 0) {
        printf("\nEstimated space savings: %llu bytes (%.2f MB)\n",
               (unsigned long long)savings,
               (double)savings / (1024.0 * 1024.0));
    }
    return (pairs < 0) ? 1 : 0;
}

/* ─────────────────────────────────────────────────────────────
   oucs history
───────────────────────────────────────────────────────────── */

static int cmd_history(int argc, char **argv) {
    if (argc < 1) {
        printf("Usage: oucs history <input.oucs>\n");
        return 1;
    }
    return (oucs_history_print(argv[0]) == OUCS_OK) ? 0 : 1;
}

/* ─────────────────────────────────────────────────────────────
   oucs stream  (demo — prints chunk stats to stdout)
───────────────────────────────────────────────────────────── */

static int cmd_stream(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: oucs stream <input.oucs> <index> [--chunk-size <bytes>] [--password <pw>]\n");
        printf("Example: oucs stream playlist.oucs 0 --chunk-size 8192\n");
        return 1;
    }

    const char *input_path = argv[0];
    uint32_t    idx        = (uint32_t)atoi(argv[1]);
    size_t      chunk_size = OUCS_CHUNK_SIZE_DEFAULT;
    const char *password   = NULL;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--chunk-size") == 0 && i + 1 < argc)
            chunk_size = (size_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--password") == 0 && i + 1 < argc)
            password = argv[++i];
    }

    OucsReader *r = oucs_reader_open(input_path);
    if (!r) { fprintf(stderr, "Error: cannot open '%s'\n", input_path); return 1; }

    OucsIndexEntry e;
    if (oucs_reader_get_song_info(r, idx, &e) != OUCS_OK) {
        fprintf(stderr, "Error: song index %u not found\n", idx);
        oucs_reader_free(r); return 1;
    }

    printf("Streaming song [%u]: \"%s\"\n", idx, e.name[0] ? e.name : "(untitled)");
    printf("  Size: %llu bytes, Format: %.4s\n",
           (unsigned long long)e.byte_size, e.audio_format);
    printf("  Chunk size: %zu bytes\n\n", chunk_size);

    OucsStream *s = oucs_stream_open(r, idx, chunk_size, password);
    if (!s) { fprintf(stderr, "Error: cannot open stream\n"); oucs_reader_free(r); return 1; }

    /* Register volume monitor hook */
    oucs_stream_register_hook(s, OUCS_HOOK_CHUNK_READ,
                               (void *)oucs_plugin_volume_monitor, NULL);

    uint8_t *buf = (uint8_t *)malloc(chunk_size);
    if (!buf) { oucs_stream_free(s); oucs_reader_free(r); return 1; }

    uint64_t total_bytes = 0;
    size_t   bytes_read  = 0;
    uint32_t chunk_count = 0;

    while (1) {
        int ret = oucs_stream_read_chunk(s, buf, chunk_size, &bytes_read);
        if (ret != OUCS_OK) { fprintf(stderr, "\nStream error: %s\n", oucs_strerror(ret)); break; }
        if (bytes_read == 0) break;
        total_bytes += bytes_read;
        chunk_count++;
    }

    printf("\n\nStream complete: %u chunks, %llu bytes total\n",
           chunk_count, (unsigned long long)total_bytes);

    free(buf);
    oucs_stream_free(s);
    oucs_reader_free(r);
    return 0;
}

/* ─────────────────────────────────────────────────────────────
   MAIN
───────────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    if (argc < 2) { usage_main(); return 1; }

    const char *cmd = argv[1];
    int   sub_argc  = argc - 2;
    char **sub_argv = argv + 2;

    if (strcmp(cmd, "pack")    == 0) return cmd_pack(sub_argc, sub_argv);
    if (strcmp(cmd, "extract") == 0) return cmd_extract(sub_argc, sub_argv);
    if (strcmp(cmd, "info")    == 0) return cmd_info(sub_argc, sub_argv);
    if (strcmp(cmd, "merge")   == 0) return cmd_merge(sub_argc, sub_argv);
    if (strcmp(cmd, "split")   == 0) return cmd_split(sub_argc, sub_argv);
    if (strcmp(cmd, "dedup")   == 0) return cmd_dedup(sub_argc, sub_argv);
    if (strcmp(cmd, "history") == 0) return cmd_history(sub_argc, sub_argv);
    if (strcmp(cmd, "stream")  == 0) return cmd_stream(sub_argc, sub_argv);

    if (strcmp(cmd, "--version") == 0 || strcmp(cmd, "-v") == 0) {
        printf("oucs version %s\n", oucs_version()); return 0;
    }
    if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        usage_main(); return 0;
    }

    fprintf(stderr, "Unknown command: '%s'\n", cmd);
    usage_main();
    return 1;
}

/* end of oucs_cli.c */
