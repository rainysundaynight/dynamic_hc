# Changes from Original Module

This module is based on [ngx_dynamic_healthcheck](https://github.com/ZigzagAK/ngx_dynamic_healthcheck) with the following enhancements:

## Added Features

### HTTPS Health Checks

Added support for `type=https` health checks that:
1. Establish SSL/TLS connection with the upstream server
2. Perform HTTP request over the encrypted connection
3. Parse and validate HTTP response

### Implementation Details

1. **New File**: `src/ngx_dynamic_healthcheck_https.h`
   - Template class `ngx_dynamic_healthcheck_https` that extends `ngx_dynamic_healthcheck_http`
   - Handles SSL/TLS handshake before HTTP communication
   - Uses OpenSSL API for SSL operations

2. **Modified Files**:
   - `src/ngx_dynamic_healthcheck.h`: Added `ssl_ctx` field to configuration structure
   - `src/ngx_dynamic_healthcheck.cpp`: Added support for creating HTTPS check objects
   - `src/ngx_dynamic_healthcheck_config.cpp`: Added "https" as valid type
   - `src/ngx_http_dynamic_healthcheck.cpp`: 
     - Added SSL context initialization
     - Updated type checks to include "https"
     - Added ngx_ssl.h include

3. **Configuration**:
   - `config`: Added ngx_dynamic_healthcheck_https.cpp to build sources

## Usage

```nginx
upstream backend {
    zone backend 128k;
    server backend.example.com:443;
    
    check fall=2 rise=2 interval=10 timeout=10000 type=https;
    check_request_uri GET /health;
    check_response_codes 200;
}
```

## Technical Notes

- SSL context is initialized once per nginx configuration and shared across all upstreams
- Uses OpenSSL's SSL_CTX_new() with SSLv23_client_method()
- SSLv2 and SSLv3 are disabled for security
- Compression is disabled
- SNI (Server Name Indication) is supported

## Compatibility

- Fully compatible with original module's HTTP, TCP, and SSL check types
- All original API endpoints work unchanged
- Configuration syntax is identical, just add `type=https`

