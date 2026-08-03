/*
 * Copyright (C) 2025
 * Enhanced version with HTTPS support
 */

#ifndef NGX_DYNAMIC_HEALTHCHECK_HTTPS_H
#define NGX_DYNAMIC_HEALTHCHECK_HTTPS_H


#include "ngx_dynamic_healthcheck_http.h"

extern "C" {
#include <openssl/ssl.h>
#include <openssl/err.h>
}


template <class PeersT, class PeerT> class ngx_dynamic_healthcheck_https :
    public ngx_dynamic_healthcheck_http<PeersT, PeerT>
{
protected:
    SSL                   *ssl_connection;
    SSL_CTX               *ssl_ctx;
    ngx_flag_t            ssl_handshake_done;

    /* ---------- ALPN http/1.1 ----------
     * Для обычного HTTPS healthcheck. gRPCS переопределяет на h2.
     */

    virtual void set_alpn() {
#ifdef TLSEXT_TYPE_application_layer_protocol_negotiation
        SSL_set_alpn_protos(ssl_connection,
            (const unsigned char *) "\x08http/1.1", 9);
#endif
    }

    virtual ngx_int_t
    on_ssl_handshake(ngx_dynamic_hc_local_node_t *state)
    {
        ngx_connection_t  *c = state->pc.connection;
        ngx_int_t           rc;
        ngx_str_t           hostname;
        u_char             *hostname_str;
        u_char             *p;
        ngx_flag_t          is_domain;

        if (ssl_handshake_done) {
            return NGX_OK;
        }

        if (ssl_connection == NULL) {
            if (ssl_ctx == NULL) {
                void *ctx_ptr = this->event->conf->ssl_ctx;
                if (ctx_ptr == NULL) {
                    ngx_log_error(NGX_LOG_ERR, c->log, 0,
                                  "[%V] %V: %V addr=%V, fd=%d SSL context"
                                  " not initialized",
                                  &this->module, &this->upstream,
                                  &this->server, &this->name, c->fd);
                    return NGX_ERROR;
                }
                ssl_ctx = (SSL_CTX *) ctx_ptr;
            }

            ssl_connection = SSL_new(ssl_ctx);
            if (ssl_connection == NULL) {
                ngx_log_error(NGX_LOG_ERR, c->log, 0,
                              "[%V] %V: %V addr=%V, fd=%d SSL_new() failed",
                              &this->module, &this->upstream,
                              &this->server, &this->name, c->fd);
                return NGX_ERROR;
            }

            if (SSL_set_fd(ssl_connection, c->fd) == 0) {
                ngx_log_error(NGX_LOG_ERR, c->log, 0,
                              "[%V] %V: %V addr=%V, fd=%d SSL_set_fd() failed",
                              &this->module, &this->upstream,
                              &this->server, &this->name, c->fd);
                SSL_free(ssl_connection);
                ssl_connection = NULL;
                return NGX_ERROR;
            }

            SSL_set_connect_state(ssl_connection);
            set_alpn();

            /* ---------- SNI ----------
             * Домен из server (не IP из name), IPv6-safe через get_host().
             */

            if (state->server.len > 0) {
                hostname = get_host(&state->server);

                if (hostname.len > 0 && hostname.data[0] == '[') {
                    hostname.data++;
                    hostname.len--;
                    if (hostname.len > 0
                        && hostname.data[hostname.len - 1] == ']')
                    {
                        hostname.len--;
                    }
                }

                is_domain = 0;
                for (p = hostname.data; p < hostname.data + hostname.len; p++) {
                    if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')) {
                        is_domain = 1;
                        break;
                    }
                }

                if (is_domain) {
                    hostname_str = (u_char *) ngx_pnalloc(c->pool,
                                                          hostname.len + 1);
                    if (hostname_str != NULL) {
                        ngx_memcpy(hostname_str, hostname.data, hostname.len);
                        hostname_str[hostname.len] = '\0';
                        SSL_set_tlsext_host_name(ssl_connection,
                                                 (char *) hostname_str);
                    }
                }
            }
        }

        rc = SSL_do_handshake(ssl_connection);

        if (rc == 1) {
            ssl_handshake_done = 1;
            return NGX_OK;
        }

        int ssl_error = SSL_get_error(ssl_connection, rc);

        if (ssl_error == SSL_ERROR_WANT_READ) {
            if (c->read->handler != c->write->handler) {
                c->read->handler = c->write->handler;
            }
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

        {
            unsigned long err = ERR_get_error();
            char err_buf[256];
            ERR_error_string_n(err, err_buf, sizeof(err_buf));
            ngx_log_error(NGX_LOG_ERR, c->log, 0,
                          "[%V] %V: %V addr=%V, fd=%d SSL handshake failed, "
                          "error=%d, %s",
                          &this->module, &this->upstream,
                          &this->server, &this->name, c->fd,
                          ssl_error, err_buf);
        }

        return NGX_ERROR;
    }

    virtual ngx_int_t
    on_send(ngx_dynamic_hc_local_node_t *state)
    {
        ngx_connection_t  *c = state->pc.connection;
        ngx_buf_t         *buf = state->buf;
        ngx_int_t           rc;
        int                 n;

        if (!ssl_handshake_done) {
            rc = on_ssl_handshake(state);
            if (rc == NGX_AGAIN) {
                return NGX_AGAIN;
            }
            if (rc == NGX_ERROR) {
                return NGX_ERROR;
            }
        }

        /* Без URI — достаточно успешного TLS handshake (как type=ssl) */
        if (this->shared->request_uri.len == 0) {
            return NGX_OK;
        }

        if (buf->last == buf->start) {
            if (this->helper.make_request(this->shared, state) == NGX_ERROR) {
                return NGX_ERROR;
            }
        }

        n = SSL_write(ssl_connection, buf->pos, buf->last - buf->pos);

        if (n > 0) {
            buf->pos += n;
            return buf->pos == buf->last ? NGX_OK : NGX_AGAIN;
        }

        int ssl_error = SSL_get_error(ssl_connection, n);

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

        ngx_log_error(NGX_LOG_ERR, c->log, 0,
                      "[%V] %V: %V addr=%V, fd=%d SSL_write() failed, error=%d",
                      &this->module, &this->upstream,
                      &this->server, &this->name, c->fd, ssl_error);
        return NGX_ERROR;
    }

    virtual ngx_int_t
    on_recv(ngx_dynamic_hc_local_node_t *state)
    {
        ngx_int_t  rc;

        if (!ssl_handshake_done) {
            rc = on_ssl_handshake(state);
            if (rc == NGX_AGAIN) {
                return NGX_AGAIN;
            }
            if (rc == NGX_ERROR) {
                return NGX_ERROR;
            }
        }

        if (this->shared->request_uri.len == 0) {
            return NGX_OK;
        }

        this->helper.ssl_conn_ptr = (void *) ssl_connection;
        return this->helper.receive(this->shared, state);
    }

public:

    ngx_dynamic_healthcheck_https(PeersT *peers,
        ngx_dynamic_healthcheck_event_t *event, ngx_dynamic_hc_state_node_t s)
        : ngx_dynamic_healthcheck_http<PeersT, PeerT>(peers, event, s),
          ssl_connection(NULL), ssl_ctx(NULL), ssl_handshake_done(0)
    {
        if (event->conf->ssl_ctx != NULL) {
            ssl_ctx = (SSL_CTX *) event->conf->ssl_ctx;
        }
    }

    virtual ~ngx_dynamic_healthcheck_https()
    {
        /* ---------- Cleanup SSL ----------
         * Без SSL_shutdown: на non-blocking fd и при keepalive reuse
         * close_notify ломает следующий цикл. Достаточно SSL_free.
         */

        if (ssl_connection != NULL) {
            SSL_free(ssl_connection);
            ssl_connection = NULL;
        }
    }
};


#endif /* NGX_DYNAMIC_HEALTHCHECK_HTTPS_H */
