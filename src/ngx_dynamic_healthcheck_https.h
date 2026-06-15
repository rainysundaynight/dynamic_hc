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

    virtual void set_alpn() {
#ifdef TLSEXT_TYPE_application_layer_protocol_negotiation
        SSL_set_alpn_protos(ssl_connection, (const unsigned char *) "\x08http/1.1", 9);
#endif
    }

    virtual ngx_int_t
    on_ssl_handshake(ngx_dynamic_hc_local_node_t *state)
    {
        ngx_connection_t  *c = state->pc.connection;
        ngx_int_t           rc;

        if (ssl_handshake_done) {
            return NGX_OK;
        }

        // Initialize SSL connection if not already done
        if (ssl_connection == NULL) {
            // Get SSL context from event configuration
            if (ssl_ctx == NULL) {
                void *ctx_ptr = this->event->conf->ssl_ctx;
                if (ctx_ptr == NULL) {
                    ngx_log_error(NGX_LOG_ERR, c->log, 0,
                                  "[%V] %V: %V addr=%V, fd=%d SSL context not initialized",
                                  &this->module, &this->upstream,
                                  &this->server, &this->name, c->fd);
                    return NGX_ERROR;
                }
                ssl_ctx = (SSL_CTX *)ctx_ptr;
            }

            // Create SSL connection
            ssl_connection = SSL_new(ssl_ctx);
            if (ssl_connection == NULL) {
                ngx_log_error(NGX_LOG_ERR, c->log, 0,
                              "[%V] %V: %V addr=%V, fd=%d SSL_new() failed",
                              &this->module, &this->upstream,
                              &this->server, &this->name, c->fd);
                return NGX_ERROR;
            }

            // Set socket file descriptor
            if (SSL_set_fd(ssl_connection, c->fd) == 0) {
                ngx_log_error(NGX_LOG_ERR, c->log, 0,
                              "[%V] %V: %V addr=%V, fd=%d SSL_set_fd() failed",
                              &this->module, &this->upstream,
                              &this->server, &this->name, c->fd);
                SSL_free(ssl_connection);
                ssl_connection = NULL;
                return NGX_ERROR;
            }

            // Set SSL mode for client connection
            SSL_set_connect_state(ssl_connection);
            
            // Allow subclasses to set ALPN
            set_alpn();
            
            // Set SNI using original server name from configuration
            // Use state->server instead of state->name to preserve domain name
            // (state->name may contain IP address after DNS resolution)
            if (state->server.len > 0) {
                ngx_str_t hostname = state->server;
                
                // Remove port if present, safely handling IPv6 literals
                u_char *colon = (u_char *) ngx_strrchr(hostname.data, ':');
                u_char *bracket = (u_char *) ngx_strchr(hostname.data, ']');
                if (colon != NULL) {
                    if (bracket == NULL || colon > bracket) {
                        hostname.len = colon - hostname.data;
                    }
                }
                
                // If IPv6 literal, strip brackets for SNI checks
                if (hostname.len > 0 && hostname.data[0] == '[') {
                    hostname.data++;
                    hostname.len--;
                    if (hostname.len > 0 && hostname.data[hostname.len-1] == ']') {
                        hostname.len--;
                    }
                }
                
                // Only set SNI if it looks like a domain name (not IP address)
                // Check if it contains at least one letter (domains have letters, IPs don't)
                u_char *p;
                ngx_flag_t is_domain = 0;
                for (p = hostname.data; p < hostname.data + hostname.len; p++) {
                    if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')) {
                        is_domain = 1;
                        break;
                    }
                }
                
                if (is_domain) {
                    // Ensure null termination for SSL_set_tlsext_host_name
                    u_char *hostname_str = (u_char *) ngx_pnalloc(c->pool, hostname.len + 1);
                    if (hostname_str != NULL) {
                        ngx_memcpy(hostname_str, hostname.data, hostname.len);
                        hostname_str[hostname.len] = '\0';
                        SSL_set_tlsext_host_name(ssl_connection, (char *) hostname_str);
                        
                        ngx_log_error(NGX_LOG_DEBUG, c->log, 0,
                                      "[%V] %V: %V addr=%V, fd=%d SNI set to: %s",
                                      &this->module, &this->upstream,
                                      &this->server, &this->name, c->fd, hostname_str);
                    }
                }
            }
        }

        ngx_log_error(NGX_LOG_DEBUG, c->log, 0,
                      "[%V] %V: %V addr=%V, fd=%d performing SSL handshake",
                      &this->module, &this->upstream,
                      &this->server, &this->name, c->fd);

        // Perform SSL handshake
        rc = SSL_do_handshake(ssl_connection);
        
        if (rc == 1) {
            // Handshake completed successfully
            ssl_handshake_done = 1;
            ngx_log_error(NGX_LOG_DEBUG, c->log, 0,
                          "[%V] %V: %V addr=%V, fd=%d SSL handshake completed",
                          &this->module, &this->upstream,
                          &this->server, &this->name, c->fd);
            return NGX_OK;
        }

        int ssl_error = SSL_get_error(ssl_connection, rc);
        
        if (ssl_error == SSL_ERROR_WANT_READ) {
            if (c->read->handler != c->write->handler) {
                c->read->handler = c->write->handler;
            }
            ngx_handle_read_event(c->read, 0);
            return NGX_AGAIN;
        } else if (ssl_error == SSL_ERROR_WANT_WRITE) {
            if (c->write->handler != c->read->handler) {
                c->write->handler = c->read->handler;
            }
            ngx_handle_write_event(c->write, 0);
            return NGX_AGAIN;
        }

        // Handshake failed
        unsigned long err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));
        ngx_log_error(NGX_LOG_ERR, c->log, 0,
                      "[%V] %V: %V addr=%V, fd=%d SSL handshake failed, "
                      "error=%d, %s",
                      &this->module, &this->upstream,
                      &this->server, &this->name, c->fd, ssl_error, err_buf);
        return NGX_ERROR;
    }

    virtual ngx_int_t
    on_send(ngx_dynamic_hc_local_node_t *state)
    {
        ngx_connection_t  *c = state->pc.connection;
        ngx_buf_t         *buf = state->buf;
        ngx_int_t           rc;
        int                 n;

        // If SSL handshake is not done, continue handshake
        if (!ssl_handshake_done) {
            rc = on_ssl_handshake(state);
            if (rc == NGX_AGAIN) {
                return NGX_AGAIN;
            }
            if (rc == NGX_ERROR) {
                return NGX_ERROR;
            }
        }

        // SSL handshake is done, proceed with HTTP request
        if (buf->last == buf->start) {
            if (this->helper.make_request(this->shared, state) == NGX_ERROR) {
                return NGX_ERROR;
            }
        }

        // Send data through SSL
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
        } else if (ssl_error == SSL_ERROR_WANT_WRITE) {
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
        ngx_int_t           rc;

        // If SSL handshake is not done, continue handshake
        if (!ssl_handshake_done) {
            rc = on_ssl_handshake(state);
            if (rc == NGX_AGAIN) {
                return NGX_AGAIN;
            }
            if (rc == NGX_ERROR) {
                return NGX_ERROR;
            }
        }

        // SSL handshake is done, receive HTTP response through SSL
        // Set SSL connection pointer in helper so receive_data can use SSL_read
        this->helper.ssl_conn_ptr = (void *)ssl_connection;
        
        // Call helper.receive which will use SSL_read via receive_data
        rc = this->helper.receive(this->shared, state);
        return rc;
    }

public:

    ngx_dynamic_healthcheck_https(PeersT *peers,
        ngx_dynamic_healthcheck_event_t *event, ngx_dynamic_hc_state_node_t s)
        : ngx_dynamic_healthcheck_http<PeersT, PeerT>(peers, event, s),
          ssl_connection(NULL), ssl_ctx(NULL), ssl_handshake_done(0)
    {
        // Get SSL_CTX from configuration
        if (event->conf->ssl_ctx != NULL) {
            ssl_ctx = (SSL_CTX *)event->conf->ssl_ctx;
        }
    }


    virtual ~ngx_dynamic_healthcheck_https()
    {
        if (ssl_connection != NULL) {
            SSL_shutdown(ssl_connection);
            SSL_free(ssl_connection);
            ssl_connection = NULL;
        }
    }
};

#endif /* NGX_DYNAMIC_HEALTHCHECK_HTTPS_H */