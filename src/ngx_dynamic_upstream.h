/*
 * Dynamic Upstream Management for Nginx
 * Extends ngx_dynamic_healthcheck with dynamic server addition/removal
 */

#ifndef NGX_DYNAMIC_UPSTREAM_H
#define NGX_DYNAMIC_UPSTREAM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_stream.h>

#ifdef __cplusplus
}
#endif

#include "ngx_dynamic_healthcheck.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Server parameters for dynamic upstream management */
typedef struct {
    ngx_str_t   server;         /* server address (host:port or IP:port) */
    ngx_int_t   weight;         /* server weight */
    ngx_int_t   max_fails;      /* max failures */
    ngx_msec_t  fail_timeout;   /* fail timeout */
    ngx_int_t   max_conns;      /* max connections */
    ngx_flag_t  backup;         /* backup server flag */
    ngx_flag_t  down;           /* down flag */
    ngx_flag_t  resolve;        /* resolve DNS in background */
} ngx_dynamic_upstream_server_t;

/* Add server to upstream */
ngx_int_t
ngx_dynamic_upstream_add_server(ngx_str_t *upstream_name, 
                                 ngx_dynamic_upstream_server_t *server,
                                 ngx_log_t *log);

/* Remove server from upstream */
ngx_int_t
ngx_dynamic_upstream_remove_server(ngx_str_t *upstream_name,
                                    ngx_str_t *server_addr,
                                    ngx_log_t *log);

/* Update server parameters */
ngx_int_t
ngx_dynamic_upstream_update_server(ngx_str_t *upstream_name,
                                    ngx_str_t *server_addr,
                                    ngx_dynamic_upstream_server_t *server,
                                    ngx_log_t *log);

/* List all servers in upstream */
ngx_int_t
ngx_dynamic_upstream_list_servers(ngx_str_t *upstream_name,
                                   ngx_str_t *output,
                                   ngx_pool_t *pool,
                                   ngx_log_t *log);

/* Save upstream peers to file */
ngx_int_t
ngx_dynamic_upstream_save_peers(ngx_str_t *upstream_name,
                                 ngx_log_t *log);

/* Load upstream peers from file */
ngx_int_t
ngx_dynamic_upstream_load_peers(ngx_str_t *upstream_name,
                                 ngx_log_t *log);

#ifdef __cplusplus
}
#endif

#endif /* NGX_DYNAMIC_UPSTREAM_H */

