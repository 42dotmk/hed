#include "jsonrpc/jsonrpc.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* --- reader --------------------------------------------------------- */

void jrpc_reader_init(JrpcReader *r) {
    memset(r, 0, sizeof(*r));
    r->content_length = -1;
}

void jrpc_reader_free(JrpcReader *r) {
    free(r->buf);
    memset(r, 0, sizeof(*r));
    r->content_length = -1;
}

int jrpc_reader_feed(JrpcReader *r, const char *data, size_t n) {
    if (n == 0)
        return 1;
    if (r->len + n + 1 > r->cap) {
        size_t nc = r->cap ? r->cap * 2 : 8192;
        while (nc < r->len + n + 1)
            nc *= 2;
        char *nb = realloc(r->buf, nc);
        if (!nb)
            return 0;
        r->buf = nb;
        r->cap = nc;
    }
    memcpy(r->buf + r->len, data, n);
    r->len += n;
    r->buf[r->len] = '\0';
    return 1;
}

char *jrpc_reader_next(JrpcReader *r, size_t *out_len) {
    if (!r->buf)
        return NULL;
    if (r->content_length < 0) {
        char *hend = strstr(r->buf, "\r\n\r\n");
        if (!hend)
            return NULL;
        char *cl = strstr(r->buf, "Content-Length:");
        if (!cl || cl > hend) {
            /* Malformed header block: drop what we have. */
            r->len = 0;
            r->buf[0] = '\0';
            return NULL;
        }
        r->content_length = atol(cl + 15);
        size_t hlen = (size_t)(hend - r->buf) + 4;
        memmove(r->buf, r->buf + hlen, r->len - hlen + 1);
        r->len -= hlen;
    }
    if (r->len < (size_t)r->content_length)
        return NULL;

    size_t blen = (size_t)r->content_length;
    char *body = malloc(blen + 1);
    if (!body)
        return NULL;
    memcpy(body, r->buf, blen);
    body[blen] = '\0';
    memmove(r->buf, r->buf + blen, r->len - blen + 1);
    r->len -= blen;
    r->content_length = -1;
    if (out_len)
        *out_len = blen;
    return body;
}

/* --- envelopes ------------------------------------------------------ */

static cJSON *jrpc_base(void) {
    cJSON *m = cJSON_CreateObject();
    if (m)
        cJSON_AddStringToObject(m, "jsonrpc", "2.0");
    return m;
}

cJSON *jrpc_request(const char *method, cJSON *params, int id) {
    cJSON *m = jrpc_base();
    if (!m)
        return NULL;
    cJSON_AddNumberToObject(m, "id", id);
    cJSON_AddStringToObject(m, "method", method);
    if (params)
        cJSON_AddItemToObject(m, "params", params);
    return m;
}

cJSON *jrpc_notification(const char *method, cJSON *params) {
    cJSON *m = jrpc_base();
    if (!m)
        return NULL;
    cJSON_AddStringToObject(m, "method", method);
    if (params)
        cJSON_AddItemToObject(m, "params", params);
    return m;
}

cJSON *jrpc_response(const cJSON *req_id, cJSON *result) {
    cJSON *m = jrpc_base();
    if (!m)
        return NULL;
    cJSON_AddItemToObject(
        m, "id", req_id ? cJSON_Duplicate(req_id, 1) : cJSON_CreateNull());
    /* "result" is required on success; default to an empty object. */
    cJSON_AddItemToObject(m, "result", result ? result : cJSON_CreateObject());
    return m;
}

cJSON *jrpc_error(const cJSON *req_id, int code, const char *message) {
    cJSON *m = jrpc_base();
    if (!m)
        return NULL;
    cJSON_AddItemToObject(
        m, "id", req_id ? cJSON_Duplicate(req_id, 1) : cJSON_CreateNull());
    cJSON *err = cJSON_CreateObject();
    cJSON_AddNumberToObject(err, "code", code);
    cJSON_AddStringToObject(err, "message", message ? message : "");
    cJSON_AddItemToObject(m, "error", err);
    return m;
}

/* --- send ----------------------------------------------------------- */

static int write_all(int fd, const char *buf, size_t len) {
    while (len > 0) {
        ssize_t n = write(fd, buf, len);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd p = {.fd = fd, .events = POLLOUT};
                poll(&p, 1, 1000);
                continue;
            }
            return -1;
        }
        buf += n;
        len -= (size_t)n;
    }
    return 0;
}

int jrpc_send(int fd, cJSON *msg) {
    if (!msg)
        return -1;
    if (fd < 0) {
        cJSON_Delete(msg);
        return -1;
    }
    char *s = cJSON_PrintUnformatted(msg);
    cJSON_Delete(msg);
    if (!s)
        return -1;

    char header[64];
    size_t clen = strlen(s);
    int hlen =
        snprintf(header, sizeof(header), "Content-Length: %zu\r\n\r\n", clen);
    int rc = 0;
    if (write_all(fd, header, (size_t)hlen) < 0 || write_all(fd, s, clen) < 0)
        rc = -1;
    free(s);
    return rc;
}
