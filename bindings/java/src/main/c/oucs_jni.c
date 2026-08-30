/**
 * OUCS JNI Bridge
 * oucs_jni.c
 *
 * Implements the JNI native methods for OucsWriter, OucsReader,
 * OucsStream, and OucsEngine Java classes.
 *
 * Build:
 *   gcc -shared -fPIC -I$JAVA_HOME/include -I$JAVA_HOME/include/linux \
 *       -I../../../../include oucs_jni.c -L../../../../build -loucs \
 *       -o liboucs_jni.so
 *
 * License: MIT
 */

#include <jni.h>
#include "../../../../include/oucs_format.h"
#include <stdlib.h>
#include <string.h>

/* ── Helpers ─────────────────────────────────────────────────── */

static void throw_oucs_exception(JNIEnv *env, int code, const char *context) {
    char msg[512];
    snprintf(msg, sizeof(msg), "%s: %s (code %d)",
             context ? context : "OUCS error",
             oucs_strerror((OucsError)code), code);
    jclass cls = (*env)->FindClass(env, "io/oucs/OucsException");
    if (cls) (*env)->ThrowNew(env, cls, msg);
}

/* ── OucsEngine ──────────────────────────────────────────────── */

JNIEXPORT jstring JNICALL
Java_io_oucs_OucsEngine_version(JNIEnv *env, jclass cls) {
    (void)cls;
    return (*env)->NewStringUTF(env, oucs_version());
}

JNIEXPORT jstring JNICALL
Java_io_oucs_OucsEngine_strerror(JNIEnv *env, jclass cls, jint code) {
    (void)cls;
    return (*env)->NewStringUTF(env, oucs_strerror((OucsError)code));
}

JNIEXPORT void JNICALL
Java_io_oucs_OucsEngine_nativeMerge(JNIEnv *env, jclass cls,
                                     jobjectArray inputs, jstring output) {
    (void)cls;
    jsize count = (*env)->GetArrayLength(env, inputs);
    const char **c_inputs = (const char **)malloc((size_t)count * sizeof(char*));
    if (!c_inputs) { throw_oucs_exception(env, OUCS_ERR_NOMEM, "merge"); return; }

    for (jsize i = 0; i < count; i++)
        c_inputs[i] = (*env)->GetStringUTFChars(env,
                           (jstring)(*env)->GetObjectArrayElement(env, inputs, i), NULL);

    const char *c_output = (*env)->GetStringUTFChars(env, output, NULL);
    int ret = oucs_merge(c_inputs, (uint32_t)count, c_output);

    for (jsize i = 0; i < count; i++)
        (*env)->ReleaseStringUTFChars(env, (jstring)(*env)->GetObjectArrayElement(env, inputs, i), c_inputs[i]);
    (*env)->ReleaseStringUTFChars(env, output, c_output);
    free(c_inputs);

    if (ret != OUCS_OK) throw_oucs_exception(env, ret, "merge");
}

JNIEXPORT void JNICALL
Java_io_oucs_OucsEngine_nativeSplit(JNIEnv *env, jclass cls,
                                     jstring input, jint from, jint to, jstring output) {
    (void)cls;
    const char *c_in  = (*env)->GetStringUTFChars(env, input, NULL);
    const char *c_out = (*env)->GetStringUTFChars(env, output, NULL);
    int ret = oucs_split(c_in, (uint32_t)from, (uint32_t)to, c_out);
    (*env)->ReleaseStringUTFChars(env, input, c_in);
    (*env)->ReleaseStringUTFChars(env, output, c_out);
    if (ret != OUCS_OK) throw_oucs_exception(env, ret, "split");
}

/* ── OucsWriter ──────────────────────────────────────────────── */

JNIEXPORT jlong JNICALL
Java_io_oucs_OucsWriter_nativeCreate(JNIEnv *env, jclass cls,
                                      jstring path, jint flags) {
    (void)cls;
    const char *c_path = (*env)->GetStringUTFChars(env, path, NULL);
    OucsWriter *w = oucs_writer_create(c_path, (uint8_t)flags);
    (*env)->ReleaseStringUTFChars(env, path, c_path);
    return (jlong)(uintptr_t)w;
}

JNIEXPORT void JNICALL
Java_io_oucs_OucsWriter_nativeSetMeta(JNIEnv *env, jclass cls, jlong ptr,
                                       jstring theme, jstring desc,
                                       jbyteArray logo, jstring logoUrl) {
    (void)cls;
    OucsWriter *w = (OucsWriter *)(uintptr_t)ptr;
    OucsContainerMeta meta;
    memset(&meta, 0, sizeof(meta));

    const char *c_theme = (*env)->GetStringUTFChars(env, theme, NULL);
    const char *c_desc  = (*env)->GetStringUTFChars(env, desc, NULL);
    const char *c_url   = (*env)->GetStringUTFChars(env, logoUrl, NULL);

    strncpy(meta.theme_name,   c_theme, OUCS_META_NAME_MAX - 1);
    strncpy(meta.description,  c_desc,  OUCS_META_DESC_MAX - 1);
    strncpy(meta.logo_ext_url, c_url,   OUCS_META_URL_MAX  - 1);

    (*env)->ReleaseStringUTFChars(env, theme,   c_theme);
    (*env)->ReleaseStringUTFChars(env, desc,    c_desc);
    (*env)->ReleaseStringUTFChars(env, logoUrl, c_url);

    if (logo != NULL) {
        jsize sz = (*env)->GetArrayLength(env, logo);
        meta.logo_bytes = (uint8_t *)malloc((size_t)sz);
        if (meta.logo_bytes) {
            (*env)->GetByteArrayRegion(env, logo, 0, sz, (jbyte *)meta.logo_bytes);
            meta.logo_size = (uint32_t)sz;
        }
    }

    oucs_writer_set_container_meta(w, &meta);
    if (meta.logo_bytes) free(meta.logo_bytes);
}

JNIEXPORT jint JNICALL
Java_io_oucs_OucsWriter_nativeAddSong(JNIEnv *env, jclass cls, jlong ptr,
                                       jstring filePath, jstring name, jstring desc) {
    (void)cls;
    OucsWriter *w      = (OucsWriter *)(uintptr_t)ptr;
    const char *c_path = (*env)->GetStringUTFChars(env, filePath, NULL);
    const char *c_name = (*env)->GetStringUTFChars(env, name, NULL);
    const char *c_desc = (*env)->GetStringUTFChars(env, desc, NULL);
    int ret = oucs_writer_add_song(w, c_path, c_name, c_desc, NULL);
    (*env)->ReleaseStringUTFChars(env, filePath, c_path);
    (*env)->ReleaseStringUTFChars(env, name,     c_name);
    (*env)->ReleaseStringUTFChars(env, desc,     c_desc);
    return ret;
}

JNIEXPORT jint JNICALL
Java_io_oucs_OucsWriter_nativeAddSongBytes(JNIEnv *env, jclass cls, jlong ptr,
                                            jbyteArray data, jstring fmt,
                                            jstring name, jstring desc) {
    (void)cls;
    OucsWriter *w  = (OucsWriter *)(uintptr_t)ptr;
    jsize sz       = (*env)->GetArrayLength(env, data);
    jbyte *raw     = (*env)->GetByteArrayElements(env, data, NULL);
    const char *c_fmt  = (*env)->GetStringUTFChars(env, fmt,  NULL);
    const char *c_name = (*env)->GetStringUTFChars(env, name, NULL);
    const char *c_desc = (*env)->GetStringUTFChars(env, desc, NULL);
    int ret = oucs_writer_add_song_mem(w, (uint8_t *)raw, (size_t)sz,
                                        c_fmt, c_name, c_desc, NULL);
    (*env)->ReleaseByteArrayElements(env, data, raw, JNI_ABORT);
    (*env)->ReleaseStringUTFChars(env, fmt,  c_fmt);
    (*env)->ReleaseStringUTFChars(env, name, c_name);
    (*env)->ReleaseStringUTFChars(env, desc, c_desc);
    return ret;
}

JNIEXPORT jint JNICALL
Java_io_oucs_OucsWriter_nativeEncryptSong(JNIEnv *env, jclass cls, jlong ptr, jstring pw) {
    (void)cls;
    const char *c_pw = (*env)->GetStringUTFChars(env, pw, NULL);
    int ret = oucs_writer_encrypt_song((OucsWriter *)(uintptr_t)ptr, c_pw);
    (*env)->ReleaseStringUTFChars(env, pw, c_pw);
    return ret;
}

JNIEXPORT jint JNICALL
Java_io_oucs_OucsWriter_nativeFinalize(JNIEnv *env, jclass cls, jlong ptr) {
    (void)env; (void)cls;
    return oucs_writer_finalize((OucsWriter *)(uintptr_t)ptr);
}

JNIEXPORT void JNICALL
Java_io_oucs_OucsWriter_nativeFree(JNIEnv *env, jclass cls, jlong ptr) {
    (void)env; (void)cls;
    oucs_writer_free((OucsWriter *)(uintptr_t)ptr);
}

/* ── OucsReader ──────────────────────────────────────────────── */

JNIEXPORT jlong JNICALL
Java_io_oucs_OucsReader_nativeOpen(JNIEnv *env, jclass cls, jstring path) {
    (void)cls;
    const char *c_path = (*env)->GetStringUTFChars(env, path, NULL);
    OucsReader *r = oucs_reader_open(c_path);
    (*env)->ReleaseStringUTFChars(env, path, c_path);
    return (jlong)(uintptr_t)r;
}

JNIEXPORT jlong JNICALL
Java_io_oucs_OucsReader_nativeOpenUrl(JNIEnv *env, jclass cls, jstring url) {
    (void)cls;
    const char *c_url = (*env)->GetStringUTFChars(env, url, NULL);
    OucsReader *r = oucs_reader_open_url(c_url);
    (*env)->ReleaseStringUTFChars(env, url, c_url);
    return (jlong)(uintptr_t)r;
}

JNIEXPORT jint JNICALL
Java_io_oucs_OucsReader_nativeSongCount(JNIEnv *env, jclass cls, jlong ptr) {
    (void)env; (void)cls;
    return oucs_reader_song_count((OucsReader *)(uintptr_t)ptr);
}

JNIEXPORT jbyteArray JNICALL
Java_io_oucs_OucsReader_nativeExtractSongBytes(JNIEnv *env, jclass cls, jlong ptr,
                                                jint index, jstring password) {
    (void)cls;
    const char *c_pw = password ? (*env)->GetStringUTFChars(env, password, NULL) : NULL;
    uint8_t *data = NULL; size_t size = 0;
    int ret = oucs_reader_extract_song_mem((OucsReader *)(uintptr_t)ptr,
                                            (uint32_t)index, &data, &size, c_pw);
    if (password && c_pw) (*env)->ReleaseStringUTFChars(env, password, c_pw);
    if (ret != OUCS_OK || !data) { throw_oucs_exception(env, ret, "extractSongBytes"); return NULL; }

    jbyteArray arr = (*env)->NewByteArray(env, (jsize)size);
    (*env)->SetByteArrayRegion(env, arr, 0, (jsize)size, (jbyte *)data);
    free(data);
    return arr;
}

JNIEXPORT void JNICALL
Java_io_oucs_OucsReader_nativeFree(JNIEnv *env, jclass cls, jlong ptr) {
    (void)env; (void)cls;
    oucs_reader_free((OucsReader *)(uintptr_t)ptr);
}

/* ── OucsStream ──────────────────────────────────────────────── */

JNIEXPORT jlong JNICALL
Java_io_oucs_OucsStream_nativeOpen(JNIEnv *env, jclass cls, jlong readerPtr,
                                    jint songIndex, jint chunkSize, jstring password) {
    (void)cls;
    const char *c_pw = password ? (*env)->GetStringUTFChars(env, password, NULL) : NULL;
    OucsStream *s = oucs_stream_open((OucsReader *)(uintptr_t)readerPtr,
                                      (uint32_t)songIndex, (size_t)chunkSize, c_pw);
    if (password && c_pw) (*env)->ReleaseStringUTFChars(env, password, c_pw);
    return (jlong)(uintptr_t)s;
}

JNIEXPORT jbyteArray JNICALL
Java_io_oucs_OucsStream_nativeReadChunk(JNIEnv *env, jclass cls, jlong ptr, jint chunkSize) {
    (void)cls;
    uint8_t *buf = (uint8_t *)malloc((size_t)chunkSize);
    if (!buf) return NULL;
    size_t bytes_read = 0;
    int ret = oucs_stream_read_chunk((OucsStream *)(uintptr_t)ptr,
                                      buf, (size_t)chunkSize, &bytes_read);
    if (ret != OUCS_OK || bytes_read == 0) { free(buf); return NULL; }
    jbyteArray arr = (*env)->NewByteArray(env, (jsize)bytes_read);
    (*env)->SetByteArrayRegion(env, arr, 0, (jsize)bytes_read, (jbyte *)buf);
    free(buf);
    return arr;
}

JNIEXPORT jint JNICALL
Java_io_oucs_OucsStream_nativeSeek(JNIEnv *env, jclass cls, jlong ptr, jlong offset) {
    (void)env; (void)cls;
    return oucs_stream_seek((OucsStream *)(uintptr_t)ptr, (uint64_t)offset);
}

JNIEXPORT jint JNICALL
Java_io_oucs_OucsStream_nativeSeekChapter(JNIEnv *env, jclass cls, jlong ptr, jint idx) {
    (void)env; (void)cls;
    return oucs_stream_seek_chapter((OucsStream *)(uintptr_t)ptr, (uint32_t)idx);
}

JNIEXPORT jlong JNICALL
Java_io_oucs_OucsStream_nativeTell(JNIEnv *env, jclass cls, jlong ptr) {
    (void)env; (void)cls;
    return (jlong)oucs_stream_tell((OucsStream *)(uintptr_t)ptr);
}

JNIEXPORT void JNICALL
Java_io_oucs_OucsStream_nativeFree(JNIEnv *env, jclass cls, jlong ptr) {
    (void)env; (void)cls;
    oucs_stream_free((OucsStream *)(uintptr_t)ptr);
}
