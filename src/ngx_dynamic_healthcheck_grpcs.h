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

    /* ---------- ALPN h2 ----------
     * Для gRPC over TLS бэкенд ожидает ALPN "h2"
     */

    virtual void set_alpn() {
#ifdef TLSEXT_TYPE_application_layer_protocol_negotiation
        SSL_set_alpn_protos(this->ssl_connection,
            (const unsigned char *) "\x02h2", 3);
#endif
    }

    virtual ngx_int_t
    on_send(ngx_dynamic_hc_local_node_t *state)
    {
        ngx_buf_t         *buf = state->buf;
        ngx_connection_t  *c = state->pc.connection;
        ngx_int_t           rc;
        int                 n;

        if (!this->ssl_handshake_done) {
            rc = this->on_ssl_handshake(state);
            if (rc == NGX_AGAIN) {
                return NGX_AGAIN;
            }
            if (rc == NGX_ERROR) {
                return NGX_ERROR;
            }
        }

        if (buf->last == buf->start) {
            /* is_tls=1 → :scheme https */
            if (ngx_dynamic_grpc_build_request(buf, &state->server, 1)
                != NGX_OK)
            {
                ngx_log_error(NGX_LOG_ERR, c->log, 0,
                              "[%V] %V: %V addr=%V, fd=%d grpcs build request"
                              " failed (buffer too small?)",
                              &this->module, &this->upstream,
                              &this->server, &this->name, c->fd);
                return NGX_ERROR;
            }
        }

        n = SSL_write(this->ssl_connection, buf->pos, buf->last - buf->pos);

        if (n > 0) {
            buf->pos += n;
            return buf->pos == buf->last ? NGX_OK : NGX_AGAIN;
        }

        int ssl_error = SSL_get_error(this->ssl_connection, n);
        if (ssl_error == SSL_ERROR_WANT_READ) {
            if (c->read->handler != c->write->handler) {
                c->read->handler = c->write->handler;
            }
            ngx_handle_read_event(c->read, 0);
            return NGX_AGAIN;
        }
        if (ssl_error == SSL_ERROR_WANT_WRITE) {
            ngx_handle_write_event(c->write, 0);
            return NGX_AGAIN;
        }

        return NGX_ERROR;
    }

    virtual ngx_int_t
    on_recv(ngx_dynamic_hc_local_node_t *state)
    {
        ngx_buf_t         *buf = state->buf;
        ngx_connection_t  *c = state->pc.connection;
        ngx_int_t           rc;
        int                 n;

        if (!this->ssl_handshake_done) {
            rc = this->on_ssl_handshake(state);
            if (rc == NGX_AGAIN) {
                return NGX_AGAIN;
            }
            if (rc == NGX_ERROR) {
                return NGX_ERROR;
            }
        }

        n = SSL_read(this->ssl_connection, buf->last, buf->end - buf->last);

        if (n > 0) {
            buf->last += n;

            rc = ngx_dynamic_grpc_response_ok(buf->start, buf->last);
            if (rc == NGX_OK) {
                return NGX_OK;
            }

            if (buf->last == buf->end) {
                return NGX_ERROR;
            }

            return NGX_AGAIN;
        }

        int ssl_error = SSL_get_error(this->ssl_connection, n);
        if (ssl_error == SSL_ERROR_WANT_READ) {
            ngx_handle_read_event(c->read, 0);
            return NGX_AGAIN;
        }
        if (ssl_error == SSL_ERROR_WANT_WRITE) {
            if (c->write->handler != c->read->handler) {
                c->write->handler = c->read->handler;
            }
            ngx_handle_write_event(c->write, 0);
            return NGX_AGAIN;
        }

        if (n == 0 || ssl_error == SSL_ERROR_ZERO_RETURN) {
            return NGX_ERROR;
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
