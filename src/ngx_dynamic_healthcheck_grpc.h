/*
 * Copyright (C) 2025
 * gRPC healthcheck support
 */

#ifndef NGX_DYNAMIC_HEALTHCHECK_GRPC_H
#define NGX_DYNAMIC_HEALTHCHECK_GRPC_H


#include "ngx_dynamic_healthcheck_tcp.h"


/* ---------- gRPC Health/Check path ----------
 * Путь стандартного gRPC Health Checking Protocol
 */

static const u_char  grpc_hc_path[] = "/grpc.health.v1.Health/Check";


/* ---------- Извлечение :authority из server ----------
 * Убирает порт, учитывает IPv6 [addr]:port, возвращает hostname для HPACK
 */

static ngx_inline ngx_str_t
ngx_dynamic_grpc_authority(ngx_str_t *server)
{
    ngx_str_t  host;

    if (server->len > 5 && ngx_strncmp(server->data, (u_char *) "unix:", 5) == 0)
    {
        host.data = (u_char *) "localhost";
        host.len = sizeof("localhost") - 1;
        return host;
    }

    host = get_host(server);

    if (host.len > 0 && host.data[0] == '[') {
        host.data++;
        host.len--;
        if (host.len > 0 && host.data[host.len - 1] == ']') {
            host.len--;
        }
    }

    if (host.len == 0) {
        host.data = (u_char *) "localhost";
        host.len = sizeof("localhost") - 1;
    }

    return host;
}


/* ---------- HPACK: строка без Huffman ----------
 * Пишет length-prefixed literal (bit H=0) в буфер
 */

static ngx_inline u_char *
ngx_dynamic_grpc_hpack_string(u_char *p, u_char *end, ngx_str_t *s)
{
    if (s->len > 127 || p + 1 + s->len > end) {
        return NULL;
    }

    *p++ = (u_char) s->len;
    p = ngx_cpymem(p, s->data, s->len);
    return p;
}


/* ---------- Сборка HTTP/2 gRPC Health/Check запроса ----------
 * Connection preface + SETTINGS + HEADERS + DATA(empty HealthCheckRequest).
 * :scheme = http|https, :authority = hostname peer'а (не localhost).
 */

static ngx_inline ngx_int_t
ngx_dynamic_grpc_build_request(ngx_buf_t *buf, ngx_str_t *server,
    ngx_flag_t is_tls)
{
    u_char     *p, *headers_len, *headers_start, *end;
    ngx_str_t   authority, path, ctype, te, te_name;
    size_t      hlen;

    authority = ngx_dynamic_grpc_authority(server);
    path.data = (u_char *) grpc_hc_path;
    path.len = sizeof(grpc_hc_path) - 1;
    ctype.data = (u_char *) "application/grpc";
    ctype.len = sizeof("application/grpc") - 1;
    te.data = (u_char *) "trailers";
    te.len = sizeof("trailers") - 1;
    te_name.data = (u_char *) "te";
    te_name.len = 2;

    end = buf->end;
    p = buf->start;

    /* PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n */
    if (p + 24 > end) {
        return NGX_ERROR;
    }
    p = ngx_cpymem(p, (u_char *) "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n", 24);

    /* Empty SETTINGS */
    if (p + 9 > end) {
        return NGX_ERROR;
    }
    p = ngx_cpymem(p, (u_char *) "\x00\x00\x00\x04\x00\x00\x00\x00\x00", 9);

    /* HEADERS frame header (length filled later), END_HEADERS, stream 1 */
    if (p + 9 > end) {
        return NGX_ERROR;
    }
    headers_len = p;
    p = ngx_cpymem(p, (u_char *) "\x00\x00\x00\x01\x04\x00\x00\x00\x01", 9);
    headers_start = p;

    /* :method POST (indexed 3) */
    if (p + 1 > end) {
        return NGX_ERROR;
    }
    *p++ = 0x83;

    /* :scheme http(6) / https(7) */
    if (p + 1 > end) {
        return NGX_ERROR;
    }
    *p++ = is_tls ? 0x87 : 0x86;

    /* :path — Literal without indexing, name index 4 */
    if (p + 1 > end) {
        return NGX_ERROR;
    }
    *p++ = 0x04;
    p = ngx_dynamic_grpc_hpack_string(p, end, &path);
    if (p == NULL) {
        return NGX_ERROR;
    }

    /* :authority — Literal without indexing, name index 1 */
    if (p + 1 > end) {
        return NGX_ERROR;
    }
    *p++ = 0x01;
    p = ngx_dynamic_grpc_hpack_string(p, end, &authority);
    if (p == NULL) {
        return NGX_ERROR;
    }

    /* content-type — Literal without indexing, name index 31 */
    if (p + 2 > end) {
        return NGX_ERROR;
    }
    *p++ = 0x0f;
    *p++ = 0x10; /* 15 + 16 = 31 */
    p = ngx_dynamic_grpc_hpack_string(p, end, &ctype);
    if (p == NULL) {
        return NGX_ERROR;
    }

    /* te: trailers — Literal without indexing, new name */
    if (p + 1 > end) {
        return NGX_ERROR;
    }
    *p++ = 0x00;
    p = ngx_dynamic_grpc_hpack_string(p, end, &te_name);
    if (p == NULL) {
        return NGX_ERROR;
    }
    p = ngx_dynamic_grpc_hpack_string(p, end, &te);
    if (p == NULL) {
        return NGX_ERROR;
    }

    hlen = (size_t) (p - headers_start);
    if (hlen > 0xffffff) {
        return NGX_ERROR;
    }
    headers_len[0] = (u_char) ((hlen >> 16) & 0xff);
    headers_len[1] = (u_char) ((hlen >> 8) & 0xff);
    headers_len[2] = (u_char) (hlen & 0xff);

    /* DATA END_STREAM, stream 1: empty protobuf HealthCheckRequest */
    if (p + 14 > end) {
        return NGX_ERROR;
    }
    p = ngx_cpymem(p,
        (u_char *) "\x00\x00\x05\x00\x01\x00\x00\x00\x01"
                   "\x00\x00\x00\x00\x00", 14);

    buf->last = p;
    buf->pos = buf->start;
    return NGX_OK;
}


/* ---------- Проверка ответа Health/Check ----------
 * Ищем полный gRPC DATA: flags(0) + len(2) + protobuf SERVING (08 01).
 * Голый 08 01 не используем — ложные срабатывания на HTTP/2 frames.
 */

static ngx_inline ngx_int_t
ngx_dynamic_grpc_response_ok(u_char *start, u_char *last)
{
    static u_char  serving[] = { 0x00, 0x00, 0x00, 0x00, 0x02, 0x08, 0x01 };
    u_char        *p;
    size_t         n;

    n = (size_t) (last - start);
    if (n < sizeof(serving)) {
        return NGX_AGAIN;
    }

    for (p = start; p + sizeof(serving) <= last; p++) {
        if (ngx_memcmp(p, serving, sizeof(serving)) == 0) {
            return NGX_OK;
        }
    }

    return NGX_AGAIN;
}


template <class PeersT, class PeerT> class ngx_dynamic_healthcheck_grpc :
    public ngx_dynamic_healthcheck_tcp<PeersT, PeerT>
{
protected:

    virtual ngx_int_t
    on_send(ngx_dynamic_hc_local_node_t *state)
    {
        ngx_buf_t         *buf = state->buf;
        ngx_connection_t  *c = state->pc.connection;
        ssize_t            size;

        if (buf->last == buf->start) {
            if (ngx_dynamic_grpc_build_request(buf, &state->server, 0)
                != NGX_OK)
            {
                ngx_log_error(NGX_LOG_ERR, c->log, 0,
                              "[%V] %V: %V addr=%V, fd=%d grpc build request"
                              " failed (buffer too small?)",
                              &this->module, &this->upstream,
                              &this->server, &this->name, c->fd);
                return NGX_ERROR;
            }
        }

        size = c->send(c, buf->pos, buf->last - buf->pos);

        if (size == NGX_ERROR) {
            return NGX_ERROR;
        }
        if (size == NGX_AGAIN) {
            return NGX_AGAIN;
        }

        buf->pos += size;

        return buf->pos == buf->last ? NGX_OK : NGX_AGAIN;
    }

    virtual ngx_int_t
    on_recv(ngx_dynamic_hc_local_node_t *state)
    {
        ngx_buf_t         *buf = state->buf;
        ngx_connection_t  *c = state->pc.connection;
        ssize_t            size;
        ngx_int_t          rc;

        size = c->recv(c, buf->last, buf->end - buf->last);

        if (size == NGX_ERROR) {
            return NGX_ERROR;
        }
        if (size == NGX_AGAIN) {
            return NGX_AGAIN;
        }

        buf->last += size;

        rc = ngx_dynamic_grpc_response_ok(buf->start, buf->last);
        if (rc == NGX_OK) {
            return NGX_OK;
        }

        if (buf->last == buf->end) {
            return NGX_ERROR;
        }

        if (c->read->eof) {
            return NGX_ERROR;
        }

        return NGX_AGAIN;
    }

public:

    ngx_dynamic_healthcheck_grpc(PeersT *peers,
        ngx_dynamic_healthcheck_event_t *event, ngx_dynamic_hc_state_node_t s)
        : ngx_dynamic_healthcheck_tcp<PeersT, PeerT>(peers, event, s)
    {}
};


#endif /* NGX_DYNAMIC_HEALTHCHECK_GRPC_H */
