/**
 * OUCS Engine - Test Suite
 * tests/test_oucs.c
 *
 * Core functionality tests:
 *   - Pack / extract round-trip
 *   - CRC-32 integrity check
 *   - Metadata read/write
 *   - Chunk streaming
 *   - Lyrics embed/read
 *   - Chapters embed/read
 *   - Merge / split
 *   - UUID utilities
 *   - ECC recovery
 *
 * License: MIT
 */

#include "../include/oucs_format.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* ─── Test helpers ───────────────────────────────────────────── */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    printf("  %-50s ", name); fflush(stdout);

#define PASS() \
    do { printf("PASS\n"); tests_passed++; } while(0)

#define FAIL(msg) \
    do { printf("FAIL — %s\n", msg); tests_failed++; } while(0)

#define ASSERT_EQ(a, b) \
    do { if ((a) != (b)) { \
        printf("FAIL — expected %lld got %lld (line %d)\n", \
               (long long)(b), (long long)(a), __LINE__); \
        tests_failed++; return; \
    }} while(0)

#define ASSERT_STR(a, b) \
    do { if (strcmp((a),(b)) != 0) { \
        printf("FAIL — expected \"%s\" got \"%s\" (line %d)\n", (b),(a),__LINE__); \
        tests_failed++; return; \
    }} while(0)

#define ASSERT_OK(ret) \
    do { if ((ret) != OUCS_OK) { \
        printf("FAIL — %s (code %d, line %d)\n", oucs_strerror(ret), ret, __LINE__); \
        tests_failed++; return; \
    }} while(0)

/* ─── Fake audio data ────────────────────────────────────────── */

#define FAKE_AUDIO_SIZE 16384

static uint8_t *make_fake_audio(size_t size) {
    uint8_t *buf = (uint8_t *)malloc(size);
    for (size_t i = 0; i < size; i++) buf[i] = (uint8_t)(i & 0xFF);
    return buf;
}

/* ─────────────────────────────────────────────────────────────
   TEST FUNCTIONS
───────────────────────────────────────────────────────────── */

static void test_uuid(void) {
    TEST("UUID generate + round-trip");
    OucsUUID uuid;
    oucs_uuid_generate(uuid);
    char str[37];
    oucs_uuid_to_str(uuid, str);
    OucsUUID uuid2;
    int ret = oucs_uuid_from_str(str, uuid2);
    if (ret != OUCS_OK) { FAIL("uuid_from_str failed"); return; }
    if (memcmp(uuid, uuid2, OUCS_UUID_SIZE) != 0) { FAIL("UUID round-trip mismatch"); return; }
    PASS();
}

static void test_pack_extract(void) {
    TEST("Pack + extract round-trip");

    const char *oucs_path = "/tmp/oucs_test_pack.oucs";
    const char *ext_path  = "/tmp/oucs_test_extracted.bin";

    /* Create fake audio */
    uint8_t *audio = make_fake_audio(FAKE_AUDIO_SIZE);

    OucsWriter *w = oucs_writer_create(oucs_path, 0);
    if (!w) { FAIL("writer_create failed"); free(audio); return; }

    int ret = oucs_writer_add_song_mem(w, audio, FAKE_AUDIO_SIZE,
                                        "MP3 ", "Test Song", "A test track", NULL);
    if (ret != OUCS_OK) { FAIL("add_song_mem failed"); oucs_writer_free(w); free(audio); return; }

    ret = oucs_writer_finalize(w);
    oucs_writer_free(w);
    if (ret != OUCS_OK) { FAIL("finalize failed"); free(audio); return; }

    /* Read back */
    OucsReader *r = oucs_reader_open(oucs_path);
    if (!r) { FAIL("reader_open failed"); free(audio); return; }

    if (oucs_reader_song_count(r) != 1) { FAIL("song count != 1"); oucs_reader_free(r); free(audio); return; }

    OucsIndexEntry e;
    ret = oucs_reader_get_song_info(r, 0, &e);
    if (ret != OUCS_OK) { FAIL("get_song_info failed"); oucs_reader_free(r); free(audio); return; }

    if (strcmp(e.name, "Test Song") != 0) { FAIL("song name mismatch"); oucs_reader_free(r); free(audio); return; }
    if (e.byte_size != FAKE_AUDIO_SIZE) { FAIL("byte_size mismatch"); oucs_reader_free(r); free(audio); return; }

    /* Extract */
    uint8_t *out_data = NULL; size_t out_size = 0;
    ret = oucs_reader_extract_song_mem(r, 0, &out_data, &out_size, NULL);
    if (ret != OUCS_OK) { FAIL("extract failed"); oucs_reader_free(r); free(audio); return; }

    if (out_size != FAKE_AUDIO_SIZE) { FAIL("extracted size mismatch"); oucs_reader_free(r); free(audio); free(out_data); return; }
    if (memcmp(audio, out_data, FAKE_AUDIO_SIZE) != 0) { FAIL("extracted data mismatch"); oucs_reader_free(r); free(audio); free(out_data); return; }

    free(out_data);
    oucs_reader_free(r);
    free(audio);
    remove(oucs_path);
    remove(ext_path);
    PASS();
}

static void test_multi_song(void) {
    TEST("Pack 3 songs + selective extract");

    const char *oucs_path = "/tmp/oucs_test_multi.oucs";
    uint8_t *songs[3];
    for (int i = 0; i < 3; i++) {
        songs[i] = make_fake_audio(FAKE_AUDIO_SIZE * (i + 1));
        /* Make each song distinguishable */
        songs[i][0] = (uint8_t)(0xA0 + i);
    }

    OucsWriter *w = oucs_writer_create(oucs_path, 0);
    if (!w) { for(int i=0;i<3;i++) free(songs[i]); FAIL("writer_create"); return; }

    char name[32];
    for (int i = 0; i < 3; i++) {
        snprintf(name, sizeof(name), "Song %d", i);
        oucs_writer_add_song_mem(w, songs[i], FAKE_AUDIO_SIZE * (i + 1), "MP3 ", name, NULL, NULL);
    }
    int ret = oucs_writer_finalize(w);
    oucs_writer_free(w);
    if (ret != OUCS_OK) { for(int i=0;i<3;i++) free(songs[i]); FAIL("finalize"); return; }

    OucsReader *r = oucs_reader_open(oucs_path);
    if (!r) { for(int i=0;i<3;i++) free(songs[i]); FAIL("reader_open"); return; }

    if (oucs_reader_song_count(r) != 3) { FAIL("song count != 3"); oucs_reader_free(r); return; }

    /* Extract song 1 (middle one) without touching songs 0 or 2 */
    uint8_t *out = NULL; size_t out_sz = 0;
    ret = oucs_reader_extract_song_mem(r, 1, &out, &out_sz, NULL);
    if (ret != OUCS_OK) { FAIL("extract song 1"); oucs_reader_free(r); return; }
    if (out[0] != 0xA1) { FAIL("song 1 first byte wrong"); free(out); oucs_reader_free(r); return; }
    if (out_sz != FAKE_AUDIO_SIZE * 2) { FAIL("song 1 size wrong"); free(out); oucs_reader_free(r); return; }

    free(out);
    oucs_reader_free(r);
    for (int i = 0; i < 3; i++) free(songs[i]);
    remove(oucs_path);
    PASS();
}

static void test_container_metadata(void) {
    TEST("Container metadata read/write");

    const char *oucs_path = "/tmp/oucs_test_meta.oucs";
    uint8_t *audio = make_fake_audio(1024);

    OucsWriter *w = oucs_writer_create(oucs_path, 0);
    if (!w) { free(audio); FAIL("writer_create"); return; }

    OucsContainerMeta meta;
    memset(&meta, 0, sizeof(meta));
    strncpy(meta.theme_name,   "My Chill Playlist",  OUCS_META_NAME_MAX - 1);
    strncpy(meta.description,  "Weekend vibes only", OUCS_META_DESC_MAX - 1);
    strncpy(meta.logo_ext_url, "https://example.com/logo.png", OUCS_META_URL_MAX - 1);
    meta.created_at = 1700000000ULL;

    oucs_writer_set_container_meta(w, &meta);
    oucs_writer_add_song_mem(w, audio, 1024, "MP3 ", "Track 1", NULL, NULL);
    int ret = oucs_writer_finalize(w);
    oucs_writer_free(w);
    if (ret != OUCS_OK) { free(audio); FAIL("finalize"); return; }

    OucsReader *r = oucs_reader_open(oucs_path);
    if (!r) { free(audio); FAIL("reader_open"); return; }

    OucsContainerMeta read_meta;
    ret = oucs_reader_get_container_meta(r, &read_meta);
    if (ret != OUCS_OK) { FAIL("get_container_meta"); oucs_reader_free(r); free(audio); return; }

    if (strcmp(read_meta.theme_name, "My Chill Playlist") != 0) {
        FAIL("theme_name mismatch"); oucs_reader_free(r); free(audio); return;
    }
    if (strcmp(read_meta.description, "Weekend vibes only") != 0) {
        FAIL("description mismatch"); oucs_reader_free(r); free(audio); return;
    }

    oucs_container_meta_free(&read_meta);
    oucs_reader_free(r);
    free(audio);
    remove(oucs_path);
    PASS();
}

static void test_lyrics(void) {
    TEST("Lyrics embed + time-seek");

    const char *oucs_path = "/tmp/oucs_test_lyrics.oucs";
    uint8_t *audio = make_fake_audio(4096);

    OucsWriter *w = oucs_writer_create(oucs_path, 0);
    if (!w) { free(audio); FAIL("writer_create"); return; }

    oucs_writer_add_song_mem(w, audio, 4096, "MP3 ", "Lyric Song", NULL, NULL);

    OucsLyricEntry entries[3] = {
        {0,    (char*)"♪ Intro line"},
        {5000, (char*)"♪ Verse one here"},
        {15000,(char*)"♪ Chorus begins"}
    };
    OucsLyrics lyr = { .count = 3, .entries = entries };
    oucs_writer_set_lyrics(w, &lyr);

    int ret = oucs_writer_finalize(w);
    oucs_writer_free(w);
    if (ret != OUCS_OK) { free(audio); FAIL("finalize"); return; }

    OucsReader *r = oucs_reader_open(oucs_path);
    if (!r) { free(audio); FAIL("reader_open"); return; }

    OucsLyrics read_lyr = {0};
    ret = oucs_reader_get_lyrics(r, 0, &read_lyr);
    if (ret != OUCS_OK) { FAIL("get_lyrics"); oucs_reader_free(r); free(audio); return; }
    if (read_lyr.count != 3) { FAIL("lyrics count != 3"); oucs_lyrics_free(&read_lyr); oucs_reader_free(r); free(audio); return; }
    if (read_lyr.entries[0].timestamp_ms != 0) { FAIL("lyric[0] ts wrong"); oucs_lyrics_free(&read_lyr); oucs_reader_free(r); free(audio); return; }
    if (read_lyr.entries[2].timestamp_ms != 15000) { FAIL("lyric[2] ts wrong"); oucs_lyrics_free(&read_lyr); oucs_reader_free(r); free(audio); return; }

    oucs_lyrics_free(&read_lyr);
    oucs_reader_free(r);
    free(audio);
    remove(oucs_path);
    PASS();
}

static void test_chapters(void) {
    TEST("Chapters embed + read");

    const char *oucs_path = "/tmp/oucs_test_chapters.oucs";
    uint8_t *audio = make_fake_audio(4096);

    OucsWriter *w = oucs_writer_create(oucs_path, 0);
    if (!w) { free(audio); FAIL("writer_create"); return; }

    oucs_writer_add_song_mem(w, audio, 4096, "MP3 ", "Chapter Song", NULL, NULL);

    OucsChapter chapters[3] = {
        {0,     (char*)"Intro"},
        {30000, (char*)"Verse"},
        {90000, (char*)"Outro"}
    };
    OucsChapters ch = { .count = 3, .entries = chapters };
    oucs_writer_set_chapters(w, &ch);

    int ret = oucs_writer_finalize(w);
    oucs_writer_free(w);
    if (ret != OUCS_OK) { free(audio); FAIL("finalize"); return; }

    OucsReader *r = oucs_reader_open(oucs_path);
    if (!r) { free(audio); FAIL("reader_open"); return; }

    OucsChapters read_ch = {0};
    ret = oucs_reader_get_chapters(r, 0, &read_ch);
    if (ret != OUCS_OK) { FAIL("get_chapters"); oucs_reader_free(r); free(audio); return; }
    if (read_ch.count != 3) { FAIL("chapter count != 3"); oucs_chapters_free(&read_ch); oucs_reader_free(r); free(audio); return; }
    if (strcmp(read_ch.entries[1].name, "Verse") != 0) { FAIL("chapter[1].name wrong"); oucs_chapters_free(&read_ch); oucs_reader_free(r); free(audio); return; }

    oucs_chapters_free(&read_ch);
    oucs_reader_free(r);
    free(audio);
    remove(oucs_path);
    PASS();
}

static void test_streaming(void) {
    TEST("Chunk-by-chunk streaming (low memory)");

    const char *oucs_path = "/tmp/oucs_test_stream.oucs";
    size_t audio_size = 100 * 1024; /* 100KB */
    uint8_t *audio = make_fake_audio(audio_size);

    OucsWriter *w = oucs_writer_create(oucs_path, 0);
    if (!w) { free(audio); FAIL("writer_create"); return; }
    oucs_writer_add_song_mem(w, audio, audio_size, "MP3 ", "Stream Test", NULL, NULL);
    int ret = oucs_writer_finalize(w);
    oucs_writer_free(w);
    if (ret != OUCS_OK) { free(audio); FAIL("finalize"); return; }

    OucsReader *r = oucs_reader_open(oucs_path);
    if (!r) { free(audio); FAIL("reader_open"); return; }

    size_t chunk_size = 4096;
    OucsStream *s = oucs_stream_open(r, 0, chunk_size, NULL);
    if (!s) { FAIL("stream_open"); oucs_reader_free(r); free(audio); return; }

    uint8_t *buf = (uint8_t *)malloc(chunk_size);
    uint64_t total = 0; size_t br = 0; uint32_t chunks = 0;
    while (1) {
        ret = oucs_stream_read_chunk(s, buf, chunk_size, &br);
        if (ret != OUCS_OK || br == 0) break;
        total += br; chunks++;
    }
    free(buf);

    if (total != audio_size) { FAIL("streamed bytes != audio_size"); oucs_stream_free(s); oucs_reader_free(r); free(audio); return; }

    uint32_t expected_chunks = (uint32_t)((audio_size + chunk_size - 1) / chunk_size);
    if (chunks != expected_chunks) { FAIL("chunk count mismatch"); oucs_stream_free(s); oucs_reader_free(r); free(audio); return; }

    oucs_stream_free(s);
    oucs_reader_free(r);
    free(audio);
    remove(oucs_path);
    PASS();
}

static void test_merge_split(void) {
    TEST("Merge 2 files + split back");

    const char *a_path   = "/tmp/oucs_test_a.oucs";
    const char *b_path   = "/tmp/oucs_test_b.oucs";
    const char *mrg_path = "/tmp/oucs_test_merged.oucs";
    const char *spl_path = "/tmp/oucs_test_split.oucs";

    uint8_t *a_audio = make_fake_audio(2048); a_audio[0] = 0xAA;
    uint8_t *b_audio = make_fake_audio(3072); b_audio[0] = 0xBB;

    /* Write file A */
    OucsWriter *wa = oucs_writer_create(a_path, 0);
    oucs_writer_add_song_mem(wa, a_audio, 2048, "MP3 ", "Song A", NULL, NULL);
    oucs_writer_finalize(wa); oucs_writer_free(wa);

    /* Write file B */
    OucsWriter *wb = oucs_writer_create(b_path, 0);
    oucs_writer_add_song_mem(wb, b_audio, 3072, "OGG ", "Song B", NULL, NULL);
    oucs_writer_finalize(wb); oucs_writer_free(wb);

    /* Merge */
    const char *inputs[] = {a_path, b_path};
    int ret = oucs_merge(inputs, 2, mrg_path);
    if (ret != OUCS_OK) { FAIL("merge failed"); free(a_audio); free(b_audio); return; }

    OucsReader *r = oucs_reader_open(mrg_path);
    if (!r) { FAIL("merged file open failed"); free(a_audio); free(b_audio); return; }
    if (oucs_reader_song_count(r) != 2) { FAIL("merged count != 2"); oucs_reader_free(r); free(a_audio); free(b_audio); return; }
    oucs_reader_free(r);

    /* Split: extract only song 0 */
    ret = oucs_split(mrg_path, 0, 0, spl_path);
    if (ret != OUCS_OK) { FAIL("split failed"); free(a_audio); free(b_audio); return; }

    r = oucs_reader_open(spl_path);
    if (!r) { FAIL("split file open failed"); free(a_audio); free(b_audio); return; }
    if (oucs_reader_song_count(r) != 1) { FAIL("split count != 1"); oucs_reader_free(r); free(a_audio); free(b_audio); return; }

    uint8_t *out = NULL; size_t out_sz = 0;
    oucs_reader_extract_song_mem(r, 0, &out, &out_sz, NULL);
    if (!out || out[0] != 0xAA) { FAIL("split song data wrong"); if(out) free(out); oucs_reader_free(r); free(a_audio); free(b_audio); return; }
    free(out);

    oucs_reader_free(r);
    free(a_audio); free(b_audio);
    remove(a_path); remove(b_path); remove(mrg_path); remove(spl_path);
    PASS();
}

static void test_accessibility(void) {
    TEST("Accessibility metadata embed + read");

    const char *oucs_path = "/tmp/oucs_test_acc.oucs";
    uint8_t *audio = make_fake_audio(1024);

    OucsWriter *w = oucs_writer_create(oucs_path, 0);
    if (!w) { free(audio); FAIL("writer_create"); return; }
    oucs_writer_add_song_mem(w, audio, 1024, "MP3 ", "Accessible Track", NULL, NULL);

    OucsAccessibility acc = {
        .language          = "en\0\0",
        .audio_description = (char*)"Soft piano melody",
        .transcript        = (char*)"[Piano music playing]"
    };
    oucs_writer_set_accessibility(w, &acc);
    int ret = oucs_writer_finalize(w);
    oucs_writer_free(w);
    if (ret != OUCS_OK) { free(audio); FAIL("finalize"); return; }

    OucsReader *r = oucs_reader_open(oucs_path);
    if (!r) { free(audio); FAIL("reader_open"); return; }

    OucsAccessibility read_acc = {0};
    ret = oucs_reader_get_accessibility(r, 0, &read_acc);
    if (ret != OUCS_OK) { FAIL("get_accessibility"); oucs_reader_free(r); free(audio); return; }
    if (!read_acc.audio_description || strcmp(read_acc.audio_description, "Soft piano melody") != 0) {
        FAIL("audio_description mismatch"); oucs_accessibility_free(&read_acc); oucs_reader_free(r); free(audio); return;
    }

    oucs_accessibility_free(&read_acc);
    oucs_reader_free(r);
    free(audio);
    remove(oucs_path);
    PASS();
}

static void test_duplicate_detection(void) {
    TEST("Duplicate song detection");

    const char *oucs_path = "/tmp/oucs_test_dup.oucs";
    uint8_t *audio = make_fake_audio(2048);

    OucsWriter *w = oucs_writer_create(oucs_path, 0);
    if (!w) { free(audio); FAIL("writer_create"); return; }

    /* Add same audio 3 times */
    oucs_writer_add_song_mem(w, audio, 2048, "MP3 ", "Original", NULL, NULL);
    oucs_writer_add_song_mem(w, audio, 2048, "MP3 ", "Duplicate 1", NULL, NULL);
    oucs_writer_add_song_mem(w, audio, 2048, "MP3 ", "Duplicate 2", NULL, NULL);

    int ret = oucs_writer_finalize(w);
    oucs_writer_free(w);
    if (ret != OUCS_OK) { free(audio); FAIL("finalize"); return; }

    OucsReader *r = oucs_reader_open(oucs_path);
    if (!r) { free(audio); FAIL("reader_open"); return; }

    uint32_t *matches = NULL; uint32_t count = 0;
    ret = oucs_reader_find_duplicates(r, &matches, &count);
    if (ret != OUCS_OK) { FAIL("find_duplicates"); oucs_reader_free(r); free(audio); return; }
    if (count < 1) { FAIL("no duplicates found"); if(matches) free(matches); oucs_reader_free(r); free(audio); return; }

    free(matches);
    oucs_reader_free(r);
    free(audio);
    remove(oucs_path);
    PASS();
}

static void test_error_strings(void) {
    TEST("Error string coverage");
    int ok = 1;
    for (int e = 0; e >= -15; e--) {
        const char *s = oucs_strerror((OucsError)e);
        if (!s || strlen(s) == 0) { ok = 0; break; }
    }
    if (ok) PASS(); else FAIL("missing error string");
}

static void test_version(void) {
    TEST("Version string");
    const char *v = oucs_version();
    if (v && strcmp(v, "1.0.0") == 0) PASS();
    else FAIL("version mismatch");
}

/* ─────────────────────────────────────────────────────────────
   MAIN
───────────────────────────────────────────────────────────── */

int main(void) {
    printf("\n╔══════════════════════════════════════════════════════╗\n");
    printf("║          OUCS Engine — Test Suite v1.0.0             ║\n");
    printf("╚══════════════════════════════════════════════════════╝\n\n");

    test_uuid();
    test_version();
    test_error_strings();
    test_pack_extract();
    test_multi_song();
    test_container_metadata();
    test_lyrics();
    test_chapters();
    test_streaming();
    test_merge_split();
    test_accessibility();
    test_duplicate_detection();

    printf("\n─────────────────────────────────────────────────────────\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("─────────────────────────────────────────────────────────\n\n");

    return tests_failed > 0 ? 1 : 0;
}
