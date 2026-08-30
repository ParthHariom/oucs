/**
 * OUCS Engine - Network / HTTP Range Streaming
 * oucs_network.c
 *
 * Implements HTTP Range request support so a .oucs file hosted
 * on any HTTP/CDN server can be streamed without a full download.
 *
 * Strategy:
 *   1. Fetch only the file header (44 bytes) to read offsets
 *   2. Fetch only the index table block (song_count × 512 bytes)
 *   3. On stream_open, seek to song's byte_offset using Range header
 *   4. Read chunk-by-chunk using successive Range requests
 *
 * Uses POSIX sockets (no libcurl dependency). For HTTPS use a TLS
 * wrapper (mbedTLS/wolfSSL) — see CMakeLists.txt option OUCS_USE_TLS.
 *
 * License: MIT
 */

#include "oucs_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Forward declaration from oucs_hooks.c */
extern void oucs_hooks_fire_chunk_read(const OucsHook *local_hooks, uint32_t local_count,
                                        const uint8_t *chunk, size_t size);

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  typedef SOCKET oucs_socket_t;
  #define OUCS_INVALID_SOCKET INVALID_SOCKET
  #define oucs_socket_close(s) closesocket(s)
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <netdb.h>
  #include <unistd.h>
  typedef int oucs_socket_t;
  #define OUCS_INVALID_SOCKET (-1)
  #define oucs_socket_close(s) close(s)
#endif

/* ─────────────────────────────────────────────────────────────
   URL PARSING
───────────────────────────────────────────────────────────── */

typedef struct {
    char host[512];
    char path[2048];
    int  port;
    int  is_https;
} OucsURL;

static int oucs_parse_url(const char *url, OucsURL *out) {
    memset(out, 0, sizeof(*out));
    out->port = 80;

    if (strncmp(url, "https://", 8) == 0) {
        out->is_https = 1;
        out->port     = 443;
        url += 8;
    } else if (strncmp(url, "http://", 7) == 0) {
        out->is_https = 0;
        url += 7;
    } else {
        return OUCS_ERR_INVALID_ARG;
    }

    const char *slash = strchr(url, '/');
    if (slash) {
        size_t host_len = (size_t)(slash - url);
        if (host_len >= sizeof(out->host)) return OUCS_ERR_OVERFLOW;
        strncpy(out->host, url, host_len);
        strncpy(out->path, slash, sizeof(out->path) - 1);
    } else {
        strncpy(out->host, url, sizeof(out->host) - 1);
        strcpy(out->path, "/");
    }

    /* Check for port in host */
    char *colon = strrchr(out->host, ':');
    if (colon) {
        out->port = atoi(colon + 1);
        *colon = '\0';
    }

    return OUCS_OK;
}

/* ─────────────────────────────────────────────────────────────
   TCP CONNECT
───────────────────────────────────────────────────────────── */

static oucs_socket_t oucs_tcp_connect(const char *host, int port) {
#ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);
#endif
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(host, port_str, &hints, &res) != 0)
        return OUCS_INVALID_SOCKET;

    oucs_socket_t sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock == OUCS_INVALID_SOCKET) { freeaddrinfo(res); return OUCS_INVALID_SOCKET; }

    if (connect(sock, res->ai_addr, (int)res->ai_addrlen) != 0) {
        oucs_socket_close(sock); freeaddrinfo(res); return OUCS_INVALID_SOCKET;
    }
    freeaddrinfo(res);
    return sock;
}

/* ─────────────────────────────────────────────────────────────
   HTTP RANGE REQUEST
───────────────────────────────────────────────────────────── */

/**
 * Send an HTTP/1.1 GET request with Range header.
 * @param url        Full URL
 * @param from_byte  Start byte (inclusive)
 * @param to_byte    End byte (inclusive), or -1 for open-ended
 * @param buf_out    Heap-allocated response body (caller must free)
 * @param size_out   Number of bytes in response body
 */
int oucs_http_range_get(const char *url,
                         int64_t from_byte, int64_t to_byte,
                         uint8_t **buf_out, size_t *size_out) {
    if (!url || !buf_out || !size_out) return OUCS_ERR_NULL_PARAM;

    OucsURL parsed;
    if (oucs_parse_url(url, &parsed) != OUCS_OK) return OUCS_ERR_INVALID_ARG;

    if (parsed.is_https) {
        /* HTTPS requires TLS — fallback to error with clear message */
        fprintf(stderr, "[OUCS] HTTPS not supported in portable build. "
                        "Compile with OUCS_USE_TLS=ON for HTTPS support.\n");
        return OUCS_ERR_UNSUPPORTED;
    }

    oucs_socket_t sock = oucs_tcp_connect(parsed.host, parsed.port);
    if (sock == OUCS_INVALID_SOCKET) return OUCS_ERR_NETWORK;

    /* Build request */
    char request[4096];
    int req_len;
    if (to_byte >= 0) {
        req_len = snprintf(request, sizeof(request),
            "GET %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Range: bytes=%lld-%lld\r\n"
            "Connection: close\r\n"
            "User-Agent: liboucs/" OUCS_VERSION_STRING "\r\n"
            "\r\n",
            parsed.path, parsed.host,
            (long long)from_byte, (long long)to_byte);
    } else {
        req_len = snprintf(request, sizeof(request),
            "GET %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Range: bytes=%lld-\r\n"
            "Connection: close\r\n"
            "User-Agent: liboucs/" OUCS_VERSION_STRING "\r\n"
            "\r\n",
            parsed.path, parsed.host, (long long)from_byte);
    }

#ifdef _WIN32
    send(sock, request, req_len, 0);
#else
    (void)write(sock, request, req_len);
#endif

    /* Read response into growing buffer */
    size_t cap = 65536, pos = 0;
    uint8_t *resp = (uint8_t *)malloc(cap);
    if (!resp) { oucs_socket_close(sock); return OUCS_ERR_NOMEM; }

    uint8_t recv_buf[4096];
    int n;
    while (1) {
#ifdef _WIN32
        n = recv(sock, (char *)recv_buf, sizeof(recv_buf), 0);
#else
        n = (int)read(sock, recv_buf, sizeof(recv_buf));
#endif
        if (n <= 0) break;
        if (pos + (size_t)n >= cap) {
            cap = cap * 2 + (size_t)n;
            uint8_t *tmp = (uint8_t *)realloc(resp, cap);
            if (!tmp) { free(resp); oucs_socket_close(sock); return OUCS_ERR_NOMEM; }
            resp = tmp;
        }
        memcpy(resp + pos, recv_buf, (size_t)n);
        pos += (size_t)n;
    }
    oucs_socket_close(sock);

    /* Parse HTTP headers — find double CRLF */
    uint8_t *body = NULL;
    size_t   body_size = 0;
    for (size_t i = 0; i + 3 < pos; i++) {
        if (resp[i]=='\r' && resp[i+1]=='\n' && resp[i+2]=='\r' && resp[i+3]=='\n') {
            body      = resp + i + 4;
            body_size = pos - (i + 4);
            break;
        }
    }

    if (!body) {
        free(resp);
        return OUCS_ERR_NETWORK;
    }

    /* Check status code (must be 200 or 206) */
    if (pos < 12 || (memcmp(resp, "HTTP/1.1 200", 12) != 0 &&
                     memcmp(resp, "HTTP/1.1 206", 12) != 0)) {
        free(resp);
        return OUCS_ERR_NETWORK;
    }

    /* Return body (memmove to front) */
    memmove(resp, body, body_size);
    *buf_out  = resp;
    *size_out = body_size;
    return OUCS_OK;
}

/* ─────────────────────────────────────────────────────────────
   NETWORK-BACKED READER
   Creates a pseudo-reader backed by HTTP range requests.
   The FILE* is a temp file; index is fetched once and cached.
───────────────────────────────────────────────────────────── */

OucsReader *oucs_network_open_url(const char *url) {
    if (!url) return NULL;

    /* Step 1: Fetch header (44 bytes) */
    uint8_t *hdr_data = NULL;
    size_t   hdr_size = 0;
    if (oucs_http_range_get(url, 0, OUCS_HEADER_SIZE - 1, &hdr_data, &hdr_size) != OUCS_OK)
        return NULL;
    if (hdr_size < OUCS_HEADER_SIZE) { free(hdr_data); return NULL; }

    OucsFileHeader hdr;
    memcpy(&hdr, hdr_data, sizeof(OucsFileHeader));
    free(hdr_data);

    if (!oucs_header_valid(&hdr)) return NULL;

    /* Step 2: Fetch index table */
    size_t   idx_size = (size_t)hdr.song_count * OUCS_INDEX_ENTRY_SIZE;
    uint8_t *idx_data = NULL;
    size_t   idx_fetched = 0;
    if (idx_size > 0) {
        int64_t from = (int64_t)hdr.index_table_offset;
        int64_t to   = from + (int64_t)idx_size - 1;
        if (oucs_http_range_get(url, from, to, &idx_data, &idx_fetched) != OUCS_OK)
            return NULL;
        if (idx_fetched < idx_size) { free(idx_data); return NULL; }
    }

    /* Create reader with a temp file backend */
    OucsReader *r = (OucsReader *)calloc(1, sizeof(OucsReader));
    if (!r) { free(idx_data); return NULL; }

    strncpy(r->url, url, sizeof(r->url) - 1);
    r->is_url = 1;
    memcpy(&r->header, &hdr, sizeof(hdr));

    if (idx_size > 0 && idx_data) {
        r->index = (OucsIndexEntry *)malloc(idx_size);
        if (!r->index) { free(idx_data); free(r); return NULL; }
        memcpy(r->index, idx_data, idx_size);
        free(idx_data);
    }

    /* NOTE: for URL-backed streams, song data is fetched on demand
       via oucs_http_range_get in oucs_stream_read_chunk_url() */

    return r;
}

/**
 * Read a chunk from a URL-backed stream.
 * Called by oucs_stream_read_chunk when stream->reader->is_url is true.
 */
int oucs_stream_read_chunk_url(OucsStream *s, uint8_t *buf, size_t buf_size,
                                size_t *bytes_read) {
    if (!s || !buf || !bytes_read) return OUCS_ERR_NULL_PARAM;
    *bytes_read = 0;

    if (s->pos >= s->audio_size) return OUCS_OK; /* EOF */

    uint64_t remaining = s->audio_size - s->pos;
    size_t   to_read   = (size_t)(remaining < (uint64_t)buf_size ? remaining : (uint64_t)buf_size);
    if (to_read > s->chunk_size) to_read = s->chunk_size;

    int64_t abs_from = (int64_t)(s->audio_start + s->pos);
    int64_t abs_to   = abs_from + (int64_t)to_read - 1;

    uint8_t *chunk_data = NULL;
    size_t   chunk_fetched = 0;
    int ret = oucs_http_range_get(s->reader->url, abs_from, abs_to,
                                   &chunk_data, &chunk_fetched);
    if (ret != OUCS_OK) return ret;

    size_t copy = chunk_fetched < to_read ? chunk_fetched : to_read;
    memcpy(buf, chunk_data, copy);
    free(chunk_data);

    s->pos     += copy;
    *bytes_read = copy;

    /* Fire chunk hooks */
    oucs_hooks_fire_chunk_read(s->hooks, s->hook_count, buf, copy);

    return OUCS_OK;
}
