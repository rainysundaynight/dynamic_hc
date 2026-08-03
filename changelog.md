# Changelog x5digital_dynamic_hc

## Проблема (Nginx 1.30.1 + gRPC 502)

После апгрейда Nginx и включения `type=grpc`/`type=grpcs` peer'ы уходили в `down`, клиенты получали `502` с `upstream_*_time=0` (нет живых upstream).

## Корневые причины

1. **`peer->down` стал битовой маской** (FAILED=1, DRAINING=8). Модуль писал `0/1` целиком и ломал `peers->tries` → `no_live_upstreams`.
2. **`type=grpcs` без `SSL_CTX`** — контекст создавался только для `https`.
3. **Захардкоженный gRPC-запрос**: `:authority localhost`, `:scheme http` даже для TLS.
4. **Host/SNI**: обрезка порта ломала hostname без `:` и IPv6.
5. **TLS keepalive reuse** несовместим с `SSL_free` / HTTP/2 preface.
6. **WANT_READ/WRITE** без перевешивания handlers на EPOLLET → ложные таймауты.

## Как пофиксили

| Область | Фикс |
|---|---|
| `peer->down` / `tries` | бит `FAILED` (`&= ~1` / `\|= 1`), `ngx_atomic_fetch_add` |
| `grpcs` SSL | `SSL_CTX` для `https` и `grpcs` |
| gRPC request | динамический HPACK: `:authority`=hostname, `:scheme` http/https |
| Host / SNI / get_host | IPv6-safe, не режем строку без порта |
| keepalive | принудительно `1` для https/grpc/grpcs |
| HTTPS без URI | достаточно успешного TLS handshake |
| SSL I/O | handlers на WANT_READ/WRITE; `SSL_free` без `SSL_shutdown` |
| gRPC response | только полный `SERVING` frame `00 00 00 00 02 08 01` |
| API disable_host | битовые операции, не затирает draining |

## Рабочие type=

- `http` — HTTP/1.x + `check_request_uri` / `check_response_codes`
- `https` — TLS + HTTP (или только handshake без URI)
- `grpc` — h2c Health/Check
- `grpcs` — TLS+ALPN h2 + Health/Check
