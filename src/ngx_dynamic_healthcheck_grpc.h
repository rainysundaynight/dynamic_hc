/*
 * Copyright (C) 2025
 * gRPC healthcheck support
 */

#ifndef NGX_DYNAMIC_HEALTHCHECK_GRPC_H
#define NGX_DYNAMIC_HEALTHCHECK_GRPC_H

#include "ngx_dynamic_healthcheck_tcp.h"

// Hardcoded HTTP/2 gRPC health check request for grpc.health.v1.Health/Check
static const u_char grpc_health_check_req[] = {
    // PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n
    0x50, 0x52, 0x49, 0x20, 0x2a, 0x20, 0x48, 0x54, 0x54, 0x50, 0x2f, 0x32, 0x2e, 0x30, 0x0d, 0x0a, 0x0d, 0x0a, 0x53, 0x4d, 0x0d, 0x0a, 0x0d, 0x0a,
    // SETTINGS frame (empty)
    0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
    // HEADERS frame (END_HEADERS)
    0x00, 0x00, 0x39, 0x01, 0x04, 0x00, 0x00, 0x00, 0x01,
    // HPACK encoded headers: :method POST, :scheme http, :path /grpc.health.v1.Health/Check, :authority localhost, content-type application/grpc, te trailers
    0x83, 0x86, 0x44, 0x95, 0x62, 0x6b, 0x2b, 0x22, 0xf3, 0x94, 0x74, 0x26, 0x75, 0xfb, 0x85, 0x7c,
    0x65, 0x1d, 0x09, 0x9d, 0x8b, 0xd3, 0x94, 0x9d, 0x7f, 0x41, 0x86, 0xa0, 0xe4, 0x1d, 0x13, 0x9d,
    0x09, 0x5f, 0x8b, 0x1d, 0x75, 0xd0, 0x62, 0x0d, 0x26, 0x3d, 0x4c, 0x4d, 0x65, 0x64, 0x40, 0x82,
    0x49, 0x7f, 0x86, 0x4d, 0x83, 0x35, 0x05, 0xb1, 0x1f,
    // DATA frame (END_STREAM), 5 bytes: 00 00 00 00 00
    0x00, 0x00, 0x05, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00
};

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
            buf->last = ngx_cpymem(buf->start, grpc_health_check_req, sizeof(grpc_health_check_req));
        }

        size = c->send(c, buf->pos, buf->last - buf->pos);

        if (size == NGX_ERROR) return NGX_ERROR;
        if (size == NGX_AGAIN) return NGX_AGAIN;

        buf->pos += size;

        return buf->pos == buf->last ? NGX_OK : NGX_AGAIN;
    }

    virtual ngx_int_t
    on_recv(ngx_dynamic_hc_local_node_t *state)
    {
        ngx_buf_t         *buf = state->buf;
        ngx_connection_t  *c = state->pc.connection;
        ssize_t            size;

        size = c->recv(c, buf->last, buf->end - buf->last);

        if (size == NGX_ERROR) return NGX_ERROR;
        if (size == NGX_AGAIN) return NGX_AGAIN;

        buf->last += size;

        // Search for gRPC SERVING response: 00 00 00 00 02 08 01
        u_char expected[] = {0x00, 0x00, 0x00, 0x00, 0x02, 0x08, 0x01};
        
        u_char *p;
        if (buf->last - buf->start >= (ngx_int_t)sizeof(expected)) {
            for (p = buf->start; p <= buf->last - sizeof(expected); p++) {
                if (ngx_memcmp(p, expected, sizeof(expected)) == 0) {
                    return NGX_OK;
                }
            }
        }

        if (c->read->eof) return NGX_ERROR;

        return NGX_AGAIN;
    }

public:
    ngx_dynamic_healthcheck_grpc(PeersT *peers,
        ngx_dynamic_healthcheck_event_t *event, ngx_dynamic_hc_state_node_t s)
        : ngx_dynamic_healthcheck_tcp<PeersT, PeerT>(peers, event, s)
    {}
};

#endif /* NGX_DYNAMIC_HEALTHCHECK_GRPC_H */
