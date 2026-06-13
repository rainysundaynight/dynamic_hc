/*
 * Copyright (C) 2025
 * gRPC over HTTPS (TLS) healthcheck support
 */

#ifndef NGX_DYNAMIC_HEALTHCHECK_GRPCS_H
#define NGX_DYNAMIC_HEALTHCHECK_GRPCS_H

#include "ngx_dynamic_healthcheck_https.h"
#include "ngx_dynamic_healthcheck_grpc.h"

template <class PeersT, class PeerT> class ngx_dynamic_healthcheck_grpcs :
    public ngx_dynamic_healthcheck_https<PeersT, PeerT>
{
protected:
    virtual ngx_int_t
    on_send(ngx_dynamic_hc_local_node_t *state)
    {
        ngx_buf_t         *buf = state->buf;
        ngx_int_t           rc;
        int                 n;

        if (!this->ssl_handshake_done) {
            rc = this->on_ssl_handshake(state);
            if (rc == NGX_AGAIN) return NGX_AGAIN;
            if (rc == NGX_ERROR) return NGX_ERROR;
        }

        if (buf->last == buf->start) {
            buf->last = ngx_cpymem(buf->start, grpc_health_check_req, sizeof(grpc_health_check_req));
        }

        n = SSL_write(this->ssl_connection, buf->pos, buf->last - buf->pos);
        
        if (n > 0) {
            buf->pos += n;
            return buf->pos == buf->last ? NGX_OK : NGX_AGAIN;
        }

        int ssl_error = SSL_get_error(this->ssl_connection, n);
        if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE) {
            return NGX_AGAIN;
        }

        return NGX_ERROR;
    }

    virtual ngx_int_t
    on_recv(ngx_dynamic_hc_local_node_t *state)
    {
        ngx_buf_t         *buf = state->buf;
        ngx_int_t           rc;
        int                 n;

        if (!this->ssl_handshake_done) {
            rc = this->on_ssl_handshake(state);
            if (rc == NGX_AGAIN) return NGX_AGAIN;
            if (rc == NGX_ERROR) return NGX_ERROR;
        }

        n = SSL_read(this->ssl_connection, buf->last, buf->end - buf->last);

        if (n > 0) {
            buf->last += n;

            // Search for gRPC SERVING response: 00 00 00 00 02 08 01
            u_char expected[] = {0x00, 0x00, 0x00, 0x00, 0x02, 0x08, 0x01};
            u_char *p;
            for (p = buf->start; p <= buf->last - sizeof(expected); p++) {
                if (ngx_memcmp(p, expected, sizeof(expected)) == 0) {
                    return NGX_OK;
                }
            }

            return NGX_AGAIN;
        }

        int ssl_error = SSL_get_error(this->ssl_connection, n);
        if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE) {
            return NGX_AGAIN;
        }

        if (n == 0 || ssl_error == SSL_ERROR_ZERO_RETURN) {
            return NGX_ERROR; // EOF
        }

        return NGX_ERROR;
    }

public:
    ngx_dynamic_healthcheck_grpcs(PeersT *peers,
        ngx_dynamic_healthcheck_event_t *event, ngx_dynamic_hc_state_node_t s)
        : ngx_dynamic_healthcheck_https<PeersT, PeerT>(peers, event, s)
    {}
};

#endif /* NGX_DYNAMIC_HEALTHCHECK_GRPCS_H */
