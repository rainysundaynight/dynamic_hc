/*
 * Dynamic Upstream Management Implementation
 * Extends ngx_dynamic_healthcheck with dynamic server addition/removal
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE  /* For getline */
#endif

#include "ngx_dynamic_upstream.h"
#include "ngx_dynamic_healthcheck_api.h"
#include "ngx_dynamic_healthcheck_config.h"
#include "ngx_dynamic_healthcheck_state.h"

extern "C" {
#include <ngx_http_upstream.h>
#include <ngx_stream_upstream.h>
#include <ngx_inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
}

extern ngx_module_t ngx_http_x5digital_dynamic_hc_module;

extern ngx_str_t NGX_DH_MODULE_HTTP;
extern ngx_str_t NGX_DH_MODULE_STREAM;

/* Helper function to find upstream by name */
static ngx_http_upstream_srv_conf_t *
find_http_upstream(ngx_str_t *upstream_name)
{
    ngx_http_upstream_main_conf_t  *umcf;
    ngx_http_upstream_srv_conf_t  **uscfp;
    ngx_uint_t                      i;

    umcf = (ngx_http_upstream_main_conf_t *)
        ngx_http_cycle_get_module_main_conf(ngx_cycle,
                                            ngx_http_upstream_module);
    if (umcf == NULL || umcf->upstreams.nelts == 0) {
        return NULL;
    }

    uscfp = (ngx_http_upstream_srv_conf_t **) umcf->upstreams.elts;

    for (i = 0; i < umcf->upstreams.nelts; i++) {
        if (uscfp[i] == NULL || uscfp[i]->host.len == 0) {
            continue;
        }
        if (uscfp[i]->host.len == upstream_name->len &&
            ngx_memcmp(uscfp[i]->host.data, upstream_name->data,
                      upstream_name->len) == 0) {
            return uscfp[i];
        }
    }

    return NULL;
}

/* Helper function to parse server address (host:port) */
static ngx_int_t
parse_server_address(ngx_str_t *server_str, ngx_str_t *host, ngx_int_t *port)
{
    u_char  *colon;
    size_t   host_len;

    colon = (u_char *) ngx_strchr(server_str->data, ':');
    if (colon == NULL) {
        /* No port specified, use default */
        *host = *server_str;
        *port = 80;  /* Default HTTP port */
        return NGX_OK;
    }

    host_len = colon - server_str->data;
    host->data = server_str->data;
    host->len = host_len;

    *port = ngx_atoi(colon + 1, server_str->len - host_len - 1);
    if (*port == NGX_ERROR || *port < 1 || *port > 65535) {
        return NGX_ERROR;
    }

    return NGX_OK;
}

/* Helper function to resolve address */
static ngx_int_t
resolve_address(ngx_str_t *host, ngx_int_t port, ngx_addr_t *addr, 
                ngx_pool_t *pool, ngx_log_t *log)
{
    u_char      *p;
    ngx_str_t    addr_str;
    u_char       buf[NGX_SOCKADDR_STRLEN];
    ngx_int_t    rc;
    struct addrinfo hints, *res, *rp;
    char         *hostname;
    int           err;

    /* Initialize addr structure */
    ngx_memzero(addr, sizeof(ngx_addr_t));

    /* Check if it's an IP address */
    p = host->data;
    while (p < host->data + host->len) {
        if ((*p >= '0' && *p <= '9') || *p == '.' || *p == ':') {
            p++;
        } else {
            break;
        }
    }

    if (p == host->data + host->len) {
        /* Looks like IP address, parse directly */
        addr_str.len = ngx_snprintf(buf, NGX_SOCKADDR_STRLEN, "%V:%d", host, port) - buf;
        addr_str.data = buf;
        
        rc = ngx_parse_addr_port(pool, addr, addr_str.data, addr_str.len);
        if (rc == NGX_OK && addr->sockaddr != NULL && addr->socklen > 0) {
            return NGX_OK;
        }
    }

    /* Domain name - use getaddrinfo for DNS resolution */
    hostname = (char *) ngx_palloc(pool, host->len + 1);
    if (hostname == NULL) {
        return NGX_ERROR;
    }
    ngx_memcpy(hostname, host->data, host->len);
    hostname[host->len] = '\0';
    
    /* Resolve hostname only, then set port manually */
    ngx_memzero(&hints, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    
    err = getaddrinfo(hostname, NULL, &hints, &res); /* NULL for service to avoid service lookup */
    if (err != 0) {
        ngx_log_error(NGX_LOG_ERR, log, 0,
                      "DNS resolution for %V failed: %s", host, gai_strerror(err));
        return NGX_ERROR;
    }
    
    /* Use first result and set port manually */
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        if (rp->ai_family == AF_INET) {
            addr->socklen = sizeof(struct sockaddr_in);
            addr->sockaddr = (struct sockaddr *) ngx_palloc(pool, addr->socklen);
            if (addr->sockaddr == NULL) {
                freeaddrinfo(res);
                return NGX_ERROR;
            }
            ngx_memcpy(addr->sockaddr, rp->ai_addr, rp->ai_addrlen);
            ((struct sockaddr_in *)addr->sockaddr)->sin_port = htons(port);
            freeaddrinfo(res);
            return NGX_OK;
        } else if (rp->ai_family == AF_INET6) {
            addr->socklen = sizeof(struct sockaddr_in6);
            addr->sockaddr = (struct sockaddr *) ngx_palloc(pool, addr->socklen);
            if (addr->sockaddr == NULL) {
                freeaddrinfo(res);
                return NGX_ERROR;
            }
            ngx_memcpy(addr->sockaddr, rp->ai_addr, rp->ai_addrlen);
            ((struct sockaddr_in6 *)addr->sockaddr)->sin6_port = htons(port);
            freeaddrinfo(res);
            return NGX_OK;
        }
    }
    
    freeaddrinfo(res);
    ngx_log_error(NGX_LOG_ERR, log, 0,
                  "DNS resolution for %V failed: no valid address found", host);
    return NGX_ERROR;
}

/* Add server to HTTP upstream */
ngx_int_t
ngx_dynamic_upstream_add_server(ngx_str_t *upstream_name,
                                 ngx_dynamic_upstream_server_t *server,
                                 ngx_log_t *log)
{
    ngx_http_upstream_srv_conf_t      *uscf;
    ngx_http_upstream_rr_peers_t      *peers;
    ngx_http_upstream_rr_peer_t       *peer, *new_peer, *last_peer;
    ngx_str_t                          host;
    ngx_int_t                          port;
    ngx_addr_t                         addr;
    ngx_pool_t                        *pool;
    ngx_slab_pool_t                   *slab;
    ngx_uint_t                         weight, max_fails, max_conns;
    ngx_msec_t                         fail_timeout;
    ngx_uint_t                         socklen;
    u_char                            *p;

    if (upstream_name == NULL || server == NULL || log == NULL) {
        return NGX_ERROR;
    }

    /* Find upstream */
    uscf = find_http_upstream(upstream_name);
    if (uscf == NULL) {
        ngx_log_error(NGX_LOG_ERR, log, 0,
                      "upstream \"%V\" not found", upstream_name);
        return NGX_ERROR;
    }

    /* Check if upstream has zone (required for dynamic updates) */
    if (uscf->shm_zone == NULL) {
        ngx_log_error(NGX_LOG_ERR, log, 0,
                      "upstream \"%V\" does not have a zone directive",
                      upstream_name);
        return NGX_ERROR;
    }

    /* Get shared memory slab */
    slab = (ngx_slab_pool_t *) uscf->shm_zone->shm.addr;
    if (slab == NULL) {
        ngx_log_error(NGX_LOG_ERR, log, 0,
                      "upstream \"%V\" shared memory not initialized",
                      upstream_name);
        return NGX_ERROR;
    }

    /* Parse server address */
    if (parse_server_address(&server->server, &host, &port) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, log, 0,
                      "invalid server address: %V", &server->server);
        return NGX_ERROR;
    }

    /* Create temporary pool for address parsing */
    pool = ngx_create_pool(1024, log);
    if (pool == NULL) {
        return NGX_ERROR;
    }

    /* Initialize addr structure */
    ngx_memzero(&addr, sizeof(ngx_addr_t));

    /* Resolve address */
    if (resolve_address(&host, port, &addr, pool, log) != NGX_OK) {
        ngx_destroy_pool(pool);
        ngx_log_error(NGX_LOG_ERR, log, 0,
                      "failed to resolve address for %V:%d", &host, port);
        return NGX_ERROR;
    }

    /* Verify that address was resolved */
    if (addr.sockaddr == NULL || addr.socklen == 0) {
        ngx_destroy_pool(pool);
        ngx_log_error(NGX_LOG_ERR, log, 0,
                      "address resolution returned invalid result for %V:%d", &host, port);
        return NGX_ERROR;
    }

    /* Get peers structure */
    /* Note: uscf->peer.data should be initialized by Nginx upstream module */
    /* If it's NULL, upstream has no servers and we cannot add dynamically */
    peers = (ngx_http_upstream_rr_peers_t *) uscf->peer.data;
    if (peers == NULL) {
        ngx_destroy_pool(pool);
        ngx_log_error(NGX_LOG_ERR, log, 0,
                      "upstream \"%V\" peers not initialized. "
                      "Upstream must have at least one server in configuration.",
                      upstream_name);
        return NGX_ERROR;
    }

    /* Check if server already exists - use read lock first */
    ngx_rwlock_rlock(&peers->rwlock);
    
    for (peer = peers->peer; peer != NULL; peer = peer->next) {
        if (peer->server.len == server->server.len &&
            ngx_memcmp(peer->server.data, server->server.data,
                      server->server.len) == 0) {
            ngx_rwlock_unlock(&peers->rwlock);
            ngx_destroy_pool(pool);
            ngx_log_error(NGX_LOG_WARN, log, 0,
                          "server %V already exists in upstream %V",
                          &server->server, upstream_name);
            return NGX_DECLINED;
        }
    }
    
    ngx_rwlock_unlock(&peers->rwlock);

    /* Allocate new peer in shared memory - lock slab mutex for allocation */
    ngx_shmtx_lock(&slab->mutex);
    
    new_peer = (ngx_http_upstream_rr_peer_t *) ngx_slab_calloc_locked(slab, sizeof(ngx_http_upstream_rr_peer_t));
    if (new_peer == NULL) {
        ngx_shmtx_unlock(&slab->mutex);
        ngx_destroy_pool(pool);
        ngx_log_error(NGX_LOG_ERR, log, 0,
                      "failed to allocate peer in shared memory");
        return NGX_ERROR;
    }
    
    /* Zero-initialize the peer structure */
    ngx_memzero(new_peer, sizeof(ngx_http_upstream_rr_peer_t));

    /* Allocate server string in shared memory */
    new_peer->server.data = (u_char *) ngx_slab_alloc_locked(slab, server->server.len);
    if (new_peer->server.data == NULL) {
        ngx_slab_free_locked(slab, new_peer);
        ngx_shmtx_unlock(&slab->mutex);
        ngx_destroy_pool(pool);
        ngx_log_error(NGX_LOG_ERR, log, 0,
                      "failed to allocate server string in shared memory");
        return NGX_ERROR;
    }
    ngx_memcpy(new_peer->server.data, server->server.data, server->server.len);
    new_peer->server.len = server->server.len;

    /* Allocate name string (IP:port) */
    p = (u_char *) ngx_slab_alloc_locked(slab, NGX_SOCKADDR_STRLEN);
    if (p == NULL) {
        ngx_slab_free_locked(slab, new_peer->server.data);
        ngx_slab_free_locked(slab, new_peer);
        ngx_shmtx_unlock(&slab->mutex);
        ngx_destroy_pool(pool);
        return NGX_ERROR;
    }
    new_peer->name.len = ngx_sock_ntop(addr.sockaddr, addr.socklen, p, NGX_SOCKADDR_STRLEN, 0);
    new_peer->name.data = p;

    /* Copy sockaddr - MUST be done before destroying pool! */
    socklen = addr.socklen;
    new_peer->sockaddr = (struct sockaddr *) ngx_slab_alloc_locked(slab, socklen);
    if (new_peer->sockaddr == NULL) {
        ngx_slab_free_locked(slab, new_peer->name.data);
        ngx_slab_free_locked(slab, new_peer->server.data);
        ngx_slab_free_locked(slab, new_peer);
        ngx_shmtx_unlock(&slab->mutex);
        ngx_destroy_pool(pool);
        return NGX_ERROR;
    }
    /* Copy sockaddr data BEFORE destroying pool */
    ngx_memcpy(new_peer->sockaddr, addr.sockaddr, socklen);
    new_peer->socklen = socklen;
    
    /* Now we can safely destroy pool - all data is copied to shared memory */

    /* Set peer parameters */
    weight = (server->weight >= 0) ? server->weight : 1;
    max_fails = (server->max_fails >= 0) ? (ngx_uint_t)server->max_fails : 1;
    /* fail_timeout: -1 means use default from upstream config, >0 means set value */
    if (server->fail_timeout == -1) {
        /* Use default fail_timeout from upstream configuration */
        fail_timeout = peers->fail_timeout > 0 ? peers->fail_timeout : 10000;
    } else if (server->fail_timeout > 0) {
        fail_timeout = server->fail_timeout;
    } else {
        /* Default value if not specified */
        fail_timeout = 10000;
    }
    max_conns = (server->max_conns >= 0) ? (ngx_uint_t)server->max_conns : 0;

    new_peer->weight = weight;
    new_peer->max_fails = max_fails;
    new_peer->fail_timeout = fail_timeout;
    new_peer->max_conns = max_conns;
    new_peer->down = (server->down > 0) ? 1 : 0;
    /* Note: backup field may not exist in all Nginx versions */
    /* Setting backup is handled by Nginx upstream configuration */
    new_peer->current_weight = 0;
    new_peer->effective_weight = weight;
    new_peer->fails = 0;
    new_peer->accessed = 0;
    new_peer->checked = 0;
    new_peer->conns = 0;
    new_peer->max_conns = max_conns;
    
    /* Initialize lock - rwlock is already initialized in shared memory */
    /* Note: ngx_rwlock_init is a macro that may not be available in all Nginx versions */
    /* The lock structure is zero-initialized by ngx_slab_calloc_locked */

    new_peer->next = NULL;

    /* Unlock slab mutex before acquiring write lock on peers */
    ngx_shmtx_unlock(&slab->mutex);
    
    /* Now lock peers for writing to add to list */
    ngx_rwlock_wlock(&peers->rwlock);
    
    /* Double-check that server doesn't exist (another worker might have added it) */
    for (peer = peers->peer; peer != NULL; peer = peer->next) {
        if (peer->server.len == server->server.len &&
            ngx_memcmp(peer->server.data, server->server.data,
                      server->server.len) == 0) {
            ngx_rwlock_unlock(&peers->rwlock);
            /* Free allocated peer from shared memory */
            ngx_shmtx_lock(&slab->mutex);
            if (new_peer->sockaddr) ngx_slab_free_locked(slab, new_peer->sockaddr);
            if (new_peer->name.data) ngx_slab_free_locked(slab, new_peer->name.data);
            if (new_peer->server.data) ngx_slab_free_locked(slab, new_peer->server.data);
            ngx_slab_free_locked(slab, new_peer);
            ngx_shmtx_unlock(&slab->mutex);
            ngx_destroy_pool(pool);
            ngx_log_error(NGX_LOG_WARN, log, 0,
                          "server %V already exists in upstream %V (race condition)",
                          &server->server, upstream_name);
            return NGX_DECLINED;
        }
    }

    /* Add to list */
    if (peers->peer == NULL) {
        peers->peer = new_peer;
    } else {
        last_peer = peers->peer;
        while (last_peer->next != NULL) {
            last_peer = last_peer->next;
        }
        last_peer->next = new_peer;
    }

    /* Update counters */
    peers->number++;
    if (!new_peer->down) {
        peers->tries++;
    }
    peers->total_weight += weight;

    ngx_rwlock_unlock(&peers->rwlock);
    ngx_destroy_pool(pool);

    /* Integrate with health check system if configured */
    /* Get health check configuration for this upstream */
    ngx_dynamic_healthcheck_conf_t *hc_conf = NULL;
    if (uscf->srv_conf != NULL) {
        hc_conf = (ngx_dynamic_healthcheck_conf_t *)
            ngx_http_conf_upstream_srv_conf(uscf,
                ngx_http_x5digital_dynamic_hc_module);
    }

    if (hc_conf != NULL && hc_conf->shared != NULL && 
        hc_conf->shared->type.len > 0) {
        /* Health check is configured - initialize state for new peer */
        ngx_dynamic_hc_state_node_t state;
        ngx_pool_t *hc_pool = ngx_create_pool(1024, log);
        
        if (hc_pool != NULL) {
            state = ngx_dynamic_healthcheck_state_get(&hc_conf->peers,
                        &new_peer->server, &new_peer->name,
                        new_peer->sockaddr, new_peer->socklen,
                        hc_conf->shared->buffer_size);
            
            if (state.local != NULL) {
                state.local->module = hc_conf->config.module;
                state.local->upstream = hc_conf->config.upstream;
                state.shared->down = new_peer->down;
            }
            
            ngx_destroy_pool(hc_pool);
        }
    }

    ngx_log_error(NGX_LOG_NOTICE, log, 0,
                  "added server %V to upstream %V (weight=%ui, max_fails=%ui, "
                  "fail_timeout=%T, max_conns=%ui, down=%d)",
                  &server->server, upstream_name, weight, max_fails,
                  fail_timeout, max_conns, new_peer->down);

    /* Save peers to file if configured */
    ngx_dynamic_upstream_save_peers(upstream_name, log);

    return NGX_OK;
}

/* Remove server from HTTP upstream */
ngx_int_t
ngx_dynamic_upstream_remove_server(ngx_str_t *upstream_name,
                                    ngx_str_t *server_addr,
                                    ngx_log_t *log)
{
    ngx_http_upstream_srv_conf_t  *uscf;
    ngx_http_upstream_rr_peers_t  *peers;
    ngx_http_upstream_rr_peer_t   *peer, *prev;

    if (upstream_name == NULL || server_addr == NULL || log == NULL) {
        return NGX_ERROR;
    }

    uscf = find_http_upstream(upstream_name);
    if (uscf == NULL) {
        ngx_log_error(NGX_LOG_ERR, log, 0,
                      "upstream \"%V\" not found", upstream_name);
        return NGX_ERROR;
    }

    peers = (ngx_http_upstream_rr_peers_t *) uscf->peer.data;
    if (peers == NULL) {
        return NGX_ERROR;
    }

    ngx_rwlock_wlock(&peers->rwlock);

    prev = NULL;
    for (peer = peers->peer; peer != NULL; prev = peer, peer = peer->next) {
        if (peer->server.len == server_addr->len &&
            ngx_memcmp(peer->server.data, server_addr->data,
                      server_addr->len) == 0) {
            /* Found server, remove it */
            if (prev == NULL) {
                peers->peer = peer->next;
            } else {
                prev->next = peer->next;
            }

            if (!peer->down) {
                peers->tries--;
            }
            peers->number--;

            ngx_rwlock_unlock(&peers->rwlock);

            ngx_log_error(NGX_LOG_NOTICE, log, 0,
                          "removed server %V from upstream %V",
                          server_addr, upstream_name);

            /* Save peers to file if configured */
            ngx_dynamic_upstream_save_peers(upstream_name, log);

            /* TODO: Free peer structure properly */
            return NGX_OK;
        }
    }

    ngx_rwlock_unlock(&peers->rwlock);

    ngx_log_error(NGX_LOG_WARN, log, 0,
                  "server %V not found in upstream %V",
                  server_addr, upstream_name);

    return NGX_DECLINED;
}

/* Update server parameters */
ngx_int_t
ngx_dynamic_upstream_update_server(ngx_str_t *upstream_name,
                                    ngx_str_t *server_addr,
                                    ngx_dynamic_upstream_server_t *server,
                                    ngx_log_t *log)
{
    ngx_http_upstream_srv_conf_t  *uscf;
    ngx_http_upstream_rr_peers_t  *peers;
    ngx_http_upstream_rr_peer_t   *peer;

    if (upstream_name == NULL || server_addr == NULL || 
        server == NULL || log == NULL) {
        return NGX_ERROR;
    }

    uscf = find_http_upstream(upstream_name);
    if (uscf == NULL) {
        return NGX_ERROR;
    }

    peers = (ngx_http_upstream_rr_peers_t *) uscf->peer.data;
    if (peers == NULL) {
        return NGX_ERROR;
    }

    ngx_rwlock_wlock(&peers->rwlock);

    for (peer = peers->peer; peer != NULL; peer = peer->next) {
        if (peer->server.len == server_addr->len &&
            ngx_memcmp(peer->server.data, server_addr->data,
                      server_addr->len) == 0) {
            /* Update parameters */
            if (server->weight >= 0) {
                peer->weight = server->weight;
            }
            if (server->max_fails >= 0) {
                peer->max_fails = server->max_fails;
            }
            /* fail_timeout: -1 means use default from upstream config, >0 means set value */
            if (server->fail_timeout == -1) {
                /* Use default fail_timeout from upstream configuration */
                peer->fail_timeout = peers->fail_timeout > 0 ? peers->fail_timeout : 10000;
            } else if (server->fail_timeout > 0) {
                peer->fail_timeout = (ngx_msec_t)server->fail_timeout;
            }
            if (server->max_conns >= 0) {
                peer->max_conns = server->max_conns;
            }
            if (server->down >= 0) {
                if (peer->down != (ngx_uint_t) server->down) {
                    peer->down = server->down;
                    peers->tries += server->down ? -1 : 1;
                }
            }

            ngx_rwlock_unlock(&peers->rwlock);

            ngx_log_error(NGX_LOG_NOTICE, log, 0,
                          "updated server %V in upstream %V",
                          server_addr, upstream_name);

            /* Save peers to file if configured */
            ngx_dynamic_upstream_save_peers(upstream_name, log);

            return NGX_OK;
        }
    }

    ngx_rwlock_unlock(&peers->rwlock);

    return NGX_DECLINED;
}

/* List all servers in upstream */
ngx_int_t
ngx_dynamic_upstream_list_servers(ngx_str_t *upstream_name,
                                   ngx_str_t *output,
                                   ngx_pool_t *pool,
                                   ngx_log_t *log)
{
    ngx_http_upstream_srv_conf_t  *uscf;
    ngx_http_upstream_rr_peers_t  *peers;
    ngx_http_upstream_rr_peer_t   *peer;
    u_char                        *p;

    if (upstream_name == NULL || output == NULL || pool == NULL || log == NULL) {
        return NGX_ERROR;
    }

    uscf = find_http_upstream(upstream_name);
    if (uscf == NULL) {
        return NGX_ERROR;
    }

    peers = (ngx_http_upstream_rr_peers_t *) uscf->peer.data;
    if (peers == NULL) {
        return NGX_ERROR;
    }

    /* Estimate buffer size */
    size_t buf_size = 1024;
    for (peer = peers->peer; peer != NULL; peer = peer->next) {
        buf_size += peer->server.len + 200; /* Extra space for parameters */
    }

    output->data = (u_char *) ngx_pcalloc(pool, buf_size);
    if (output->data == NULL) {
        return NGX_ERROR;
    }

    p = output->data;
    ngx_rwlock_rlock(&peers->rwlock);

    for (peer = peers->peer; peer != NULL; peer = peer->next) {
        p = ngx_snprintf(p, output->data + buf_size - p,
                        "server %V", &peer->server);

        if (peer->weight != 1) {
            p = ngx_snprintf(p, output->data + buf_size - p,
                            " weight=%ui", peer->weight);
        }
        if (peer->max_fails != 1) {
            p = ngx_snprintf(p, output->data + buf_size - p,
                            " max_fails=%ui", peer->max_fails);
        }
        if (peer->fail_timeout != 10000) {
            p = ngx_snprintf(p, output->data + buf_size - p,
                            " fail_timeout=%T", peer->fail_timeout);
        }
        if (peer->max_conns != 0) {
            p = ngx_snprintf(p, output->data + buf_size - p,
                            " max_conns=%ui", peer->max_conns);
        }
        if (peer->down) {
            p = ngx_snprintf(p, output->data + buf_size - p, " down");
        }

        p = ngx_snprintf(p, output->data + buf_size - p, ";\n");
    }

    ngx_rwlock_unlock(&peers->rwlock);

    output->len = p - output->data;

    return NGX_OK;
}

/* Helper function to get upstream configuration */
static ngx_dynamic_healthcheck_conf_t *
get_upstream_hc_conf(ngx_str_t *upstream_name)
{
    ngx_http_upstream_srv_conf_t *uscf = find_http_upstream(upstream_name);
    if (uscf == NULL) {
        return NULL;
    }
    
    extern ngx_module_t ngx_http_x5digital_dynamic_hc_module;
    if (uscf->srv_conf == NULL) {
        return NULL;
    }
    
    return (ngx_dynamic_healthcheck_conf_t *)
        ngx_http_conf_upstream_srv_conf(uscf, ngx_http_x5digital_dynamic_hc_module);
}

/* Save upstream peers to file */
ngx_int_t
ngx_dynamic_upstream_save_peers(ngx_str_t *upstream_name, ngx_log_t *log)
{
    ngx_http_upstream_srv_conf_t  *uscf;
    ngx_http_upstream_rr_peers_t  *peers;
    ngx_http_upstream_rr_peer_t   *peer;
    ngx_dynamic_healthcheck_conf_t *hc_conf;
    ngx_str_t                       state_file;
    FILE                           *f = NULL;
    ngx_core_conf_t                *ccf;
    u_char                         *path_data;
    size_t                          path_size = 10240;
    ngx_file_info_t                 fi;
    static time_t                   last_error_log = 0;
    time_t                          now;

    if (upstream_name == NULL || log == NULL) {
        return NGX_ERROR;
    }

    uscf = find_http_upstream(upstream_name);
    if (uscf == NULL) {
        return NGX_ERROR;
    }

    hc_conf = get_upstream_hc_conf(upstream_name);
    if (hc_conf == NULL || hc_conf->config.upstream_state_file.len == 0) {
        /* No state file configured */
        return NGX_OK;
    }

    state_file = hc_conf->config.upstream_state_file;

    peers = (ngx_http_upstream_rr_peers_t *) uscf->peer.data;
    if (peers == NULL) {
        return NGX_ERROR;
    }

    ccf = (ngx_core_conf_t *) ngx_get_conf(ngx_cycle->conf_ctx, ngx_core_module);
    
    path_data = (u_char *) ngx_alloc(path_size, log);
    if (path_data == NULL) {
        return NGX_ERROR;
    }

    /* Build full path */
    if (ccf->working_directory.len != 0) {
        ngx_snprintf(path_data, path_size, "%V/%V",
                     &ccf->working_directory, &state_file);
    } else {
        ngx_snprintf(path_data, path_size, "%V", &state_file);
    }

    /* Create directory if needed */
    u_char *dir_end = path_data + path_size - 1;
    while (dir_end > path_data && *dir_end != '/') {
        dir_end--;
    }
    if (dir_end > path_data && *dir_end == '/') {
        *dir_end = '\0';
        if (ngx_file_info(path_data, &fi) == NGX_FILE_ERROR) {
            ngx_log_error(NGX_LOG_DEBUG, log, 0,
                          "creating directory for peers file: %s", path_data);
            if (ngx_create_full_path(path_data, ngx_dir_access(NGX_FILE_OWNER_ACCESS))
                    != NGX_OK) {
                /* Check again if directory was created by another worker */
                if (ngx_file_info(path_data, &fi) == NGX_FILE_ERROR) {
                    now = ngx_time();
                    if (now - last_error_log >= 60) {
                        ngx_log_error(NGX_LOG_ERR, log, errno,
                                      "can't create directory for peers file: %s (errno: %d)",
                                      path_data, errno);
                        last_error_log = now;
                    }
                    ngx_free(path_data);
                    return NGX_ERROR;
                } else {
                    ngx_log_error(NGX_LOG_INFO, log, 0,
                                  "directory created by another worker: %s", path_data);
                }
            } else {
                ngx_log_error(NGX_LOG_INFO, log, 0,
                              "created directory for peers file: %s", path_data);
            }
        }
        *dir_end = '/';
    }

    f = fopen((const char *) path_data, "w");
    if (f == NULL) {
        ngx_log_error(NGX_LOG_ERR, log, errno,
                      "can't open peers file for writing: %s (errno: %d, check permissions)",
                      path_data, errno);
        ngx_free(path_data);
        return NGX_ERROR;
    }
    
    ngx_log_error(NGX_LOG_DEBUG, log, 0,
                  "opened peers file for writing: %s", path_data);

    ngx_rwlock_rlock(&peers->rwlock);

    for (peer = peers->peer; peer != NULL; peer = peer->next) {
        /* Format: server ADDRESS weight=W max_fails=M fail_timeout=T max_conns=C backup down; */
        fprintf(f, "server %.*s", (int)peer->server.len, peer->server.data);
        
        /* Always save weight, max_fails, fail_timeout, max_conns for consistency */
        fprintf(f, " weight=%u", (unsigned int)peer->weight);
        fprintf(f, " max_fails=%u", (unsigned int)peer->max_fails);
        fprintf(f, " fail_timeout=%lu", (unsigned long)peer->fail_timeout);
        fprintf(f, " max_conns=%u", (unsigned int)peer->max_conns);
        
        /* Note: backup field may not exist in all Nginx versions */
        if (peer->down) {
            fprintf(f, " down");
        }
        
        fprintf(f, ";\n");
    }

    ngx_rwlock_unlock(&peers->rwlock);

    if (fflush(f) != 0) {
        ngx_log_error(NGX_LOG_WARN, log, errno,
                      "failed to flush peers file: %s", path_data);
    }
    
    if (fclose(f) != 0) {
        ngx_log_error(NGX_LOG_WARN, log, errno,
                      "failed to close peers file: %s", path_data);
        ngx_free(path_data);
        return NGX_ERROR;
    }
    
    ngx_log_error(NGX_LOG_INFO, log, 0,
                  "saved peers for upstream %V to file %s", upstream_name, path_data);
    
    ngx_free(path_data);

    return NGX_OK;
}

/* Load upstream peers from file */
ngx_int_t
ngx_dynamic_upstream_load_peers(ngx_str_t *upstream_name, ngx_log_t *log)
{
    ngx_dynamic_healthcheck_conf_t *hc_conf;
    ngx_str_t                       state_file;
    FILE                           *f = NULL;
    ngx_core_conf_t                *ccf;
    u_char                         *path_data;
    size_t                          path_size = 10240;
    char                           *line = NULL;
    size_t                          line_size = 0;
    ssize_t                         read;
    ngx_dynamic_upstream_server_t   server;
    ngx_int_t                       rc = NGX_OK;

    if (upstream_name == NULL || log == NULL) {
        return NGX_ERROR;
    }

    hc_conf = get_upstream_hc_conf(upstream_name);
    if (hc_conf == NULL || hc_conf->config.upstream_state_file.len == 0) {
        /* No state file configured */
        return NGX_OK;
    }

    state_file = hc_conf->config.upstream_state_file;

    ccf = (ngx_core_conf_t *) ngx_get_conf(ngx_cycle->conf_ctx, ngx_core_module);
    
    path_data = (u_char *) ngx_alloc(path_size, log);
    if (path_data == NULL) {
        return NGX_ERROR;
    }

    /* Build full path */
    if (ccf->working_directory.len != 0) {
        ngx_snprintf(path_data, path_size, "%V/%V",
                     &ccf->working_directory, &state_file);
    } else {
        ngx_snprintf(path_data, path_size, "%V", &state_file);
    }

    f = fopen((const char *) path_data, "r");
    if (f == NULL) {
        /* File doesn't exist, that's OK */
        ngx_free(path_data);
        return NGX_OK;
    }

    ngx_log_error(NGX_LOG_INFO, log, 0,
                  "loading peers for upstream %V from file", upstream_name);

    while ((read = getline(&line, &line_size, f)) != -1) {
        u_char *p, *end;
        ngx_str_t server_str;
        size_t param_len;
        
        /* Skip empty lines and comments */
        p = (u_char *)line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\n' || *p == '\r' || *p == '#' || *p == '\0') {
            continue;
        }

        /* Parse "server ADDRESS [params];" */
        if (ngx_strncmp(p, (u_char *)"server ", 7) != 0) {
            continue;
        }
        p += 7;
        
        /* Skip whitespace */
        while (*p == ' ' || *p == '\t') p++;
        
        /* Find end of address (space, semicolon, or newline) */
        end = p;
        while (*end != ' ' && *end != '\t' && *end != ';' && 
               *end != '\n' && *end != '\r' && *end != '\0') {
            end++;
        }
        
        if (end == p) {
            continue; /* Empty address */
        }

        server_str.data = p;
        server_str.len = end - p;

        ngx_memzero(&server, sizeof(ngx_dynamic_upstream_server_t));
        server.server = server_str;
        server.weight = -1;
        server.max_fails = -1;
        server.fail_timeout = -1;
        server.max_conns = -1;
        server.backup = -1;
        server.down = -1;

        /* Parse parameters */
        p = end;
        while (*p != ';' && *p != '\n' && *p != '\r' && *p != '\0') {
            while (*p == ' ' || *p == '\t') p++;
            
            if (ngx_strncmp(p, (u_char *)"weight=", 7) == 0) {
                p += 7;
                param_len = ngx_strlen(p);
                server.weight = ngx_atoi(p, param_len);
            } else if (ngx_strncmp(p, (u_char *)"max_fails=", 10) == 0) {
                p += 10;
                param_len = ngx_strlen(p);
                server.max_fails = ngx_atoi(p, param_len);
            } else if (ngx_strncmp(p, (u_char *)"fail_timeout=", 14) == 0) {
                p += 14;
                param_len = ngx_strlen(p);
                server.fail_timeout = ngx_atoi(p, param_len);
            } else if (ngx_strncmp(p, (u_char *)"max_conns=", 10) == 0) {
                p += 10;
                param_len = ngx_strlen(p);
                server.max_conns = ngx_atoi(p, param_len);
            } else if (ngx_strncmp(p, (u_char *)"backup", 6) == 0) {
                server.backup = 1;
                p += 6;
            } else if (ngx_strncmp(p, (u_char *)"down", 4) == 0) {
                server.down = 1;
                p += 4;
            } else {
                /* Skip unknown parameter */
                while (*p != ' ' && *p != '\t' && *p != ';' && 
                       *p != '\n' && *p != '\r' && *p != '\0') {
                    p++;
                }
            }
        }

        /* Add server */
        if (ngx_dynamic_upstream_add_server(upstream_name, &server, log) != NGX_OK) {
            ngx_log_error(NGX_LOG_WARN, log, 0,
                          "failed to add server %V from peers file",
                          &server_str);
            rc = NGX_ERROR;
        }
    }

    if (line != NULL) {
        free(line);
    }
    fclose(f);
    ngx_free(path_data);

    ngx_log_error(NGX_LOG_INFO, log, 0,
                  "loaded peers for upstream %V from file", upstream_name);

    return rc;
}

