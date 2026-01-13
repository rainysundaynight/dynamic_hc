# x5digital_dynamic_hc - Dynamic Health Check Module for Nginx

Enhanced version of [ngx_dynamic_healthcheck](https://github.com/ZigzagAK/ngx_dynamic_healthcheck) with HTTPS health check support.

## Features

This module extends the original ngx_dynamic_healthcheck with the following capabilities:

- **HTTPS health checks**: Full SSL/TLS handshake support for HTTPS endpoints
- **HTTP health checks**: Standard HTTP health checks (from original module)
- **TCP health checks**: TCP connection checks (from original module)
- **SSL handshake checks**: SSL/TLS handshake verification (from original module)
- **Dynamic reconfiguration**: Update health check parameters without reloading nginx
- **Persistent configuration**: Save health check parameters to disk

## Differences from Original Module

The main addition is support for `type=https` which performs:
1. SSL/TLS handshake with the upstream server
2. HTTP request over the encrypted connection
3. HTTP response parsing and validation

### Key Fixes

- **Domain name preservation**: When using domain names in `server` directive, the module now:
  - Uses the original domain name for SNI (Server Name Indication) instead of resolved IP address
  - Uses the original domain name in HTTP `Host` header instead of resolved IP address
  - This fixes the issue where the original module would resolve domain to IP and send plain HTTP requests to HTTPS ports, resulting in 4xx errors

- **Proper HTTPS handling**: The module correctly establishes SSL/TLS connection before sending HTTP requests, ensuring that HTTPS endpoints are properly checked.

## Quick Start

### Installation

1. **Download nginx source:**
   ```bash
   cd /tmp
   wget http://nginx.org/download/nginx-1.24.0.tar.gz
   tar -xzf nginx-1.24.0.tar.gz
   ```

2. **Build nginx with the module:**
   ```bash
   export NGINX_SRC=/tmp/nginx-1.24.0
   cd /path/to/x5digital_dynamic_hc
   ./build.sh
   ```

3. **Install:**
   ```bash
   cd $NGINX_SRC
   sudo make install
   ```

4. **Verify installation:**
   ```bash
   /usr/local/nginx/sbin/nginx -V 2>&1 | grep x5digital_dynamic_hc
   ```

**For detailed installation instructions, see [INSTALL.md](INSTALL.md)**

### Basic Usage

Add to your `nginx.conf`:

```nginx
upstream backend {
    zone backend 128k;
    server example.com:443;
    
    check fall=2 rise=2 interval=10 timeout=10000 type=https;
    check_request_uri GET /health;
    check_response_codes 200;
}
```

## Installation (Detailed)

### Prerequisites

- Nginx source code (version 1.9.11 or higher)
- C++ compiler (C++11 or later)
- OpenSSL development libraries
- Make and standard build tools

### Build Options

You can build manually or use the provided script:

**Using build script:**
```bash
export NGINX_SRC=/path/to/nginx/source
./build.sh
```

**Manual build:**
```bash
cd /path/to/nginx/source
./configure --add-module=/path/to/x5digital_dynamic_hc --with-http_ssl_module [other options]
make
sudo make install
```

## Configuration

### Basic HTTPS Health Check

```nginx
http {
    upstream backend {
        zone backend 128k;
        
        server backend1.example.com:443;
        server backend2.example.com:443;
        
        check fall=2 rise=2 interval=10 timeout=10000 type=https;
        check_request_uri GET /health;
        check_response_codes 200 201;
    }
}
```

### HTTPS Health Check with Custom Headers

```nginx
http {
    upstream backend {
        zone backend 128k;
        
        server backend.example.com:443;
        
        check fall=2 rise=2 interval=10 timeout=10000 type=https;
        check_request_uri GET /api/health;
        check_request_headers Host:backend.example.com Authorization:Basic\ dXNlcjpwYXNz;
        check_response_codes 200;
        check_response_body ".*healthy.*";
    }
}
```

### Global HTTPS Health Check Configuration

```nginx
http {
    # Global HTTPS health check settings
    healthcheck fall=2 rise=2 interval=60 timeout=10000 type=https;
    healthcheck_request_uri GET /health;
    healthcheck_response_codes 200;
    
    upstream backend1 {
        zone backend1 128k;
        server backend1.example.com:443;
        # Inherits global HTTPS settings
    }
    
    upstream backend2 {
        zone backend2 128k;
        server backend2.example.com:443;
        # Override with custom settings
        check fall=3 rise=3 interval=30 timeout=5000 type=https;
        check_request_uri GET /custom/health;
    }
}
```

## Configuration Directives

### `check type=https`

Enables HTTPS health checks. Performs SSL/TLS handshake followed by HTTP request/response.

**Parameters:**
- `fall=N`: Number of consecutive failures before marking peer as down
- `rise=N`: Number of consecutive successes before marking peer as up
- `interval=SEC`: Interval between health checks (seconds)
- `timeout=MS`: Timeout for health check operations (milliseconds)
- `keepalive=N`: Maximum number of requests per connection
- `type=https`: Enable HTTPS health checks

### `check_request_uri METHOD URI`

Specify HTTP method and URI for HTTPS health check.

Example: `check_request_uri GET /health`

### `check_request_headers HEADER:VALUE ...`

Add custom HTTP headers to HTTPS health check requests.

Example: `check_request_headers Host:example.com Authorization:Basic\ token`

### `check_response_codes CODE ...`

List of acceptable HTTP response codes.

Example: `check_response_codes 200 201 204`

### `check_response_body REGEXP`

Regular expression to match in response body.

Example: `check_response_body ".*healthy.*"`

## API Endpoints

The module provides the same API endpoints as the original module:

- `GET /healthcheck/get` - Get health check configuration
- `GET /healthcheck/status` - Get current health check status
- `POST /healthcheck/update` - Update health check parameters

Example API usage:

```nginx
server {
    listen 8888;
    
    location = /healthcheck/get {
        healthcheck_get;
    }
    
    location = /healthcheck/status {
        healthcheck_status;
    }
    
    location = /healthcheck/update {
        healthcheck_update;
    }
}
```

### Update HTTPS Health Check via API

```bash
curl "http://localhost:8888/healthcheck/update?upstream=backend&type=https&fall=3&rise=2&interval=15&request_uri=GET%20/health&response_codes=200"
```

## SSL/TLS Configuration

The module automatically initializes SSL context for HTTPS health checks. The SSL context uses:
- TLS 1.0+ protocols (SSLv2 and SSLv3 disabled)
- Compression disabled
- Standard cipher suites

For custom SSL configuration, you may need to modify the SSL initialization code in `ngx_http_dynamic_healthcheck.cpp`.


## Troubleshooting

### SSL Handshake Failures

If you see SSL handshake errors, check:
1. Upstream server supports TLS
2. Certificate is valid
3. Firewall allows SSL connections
4. Port is correct (usually 443 for HTTPS)

### Connection Timeouts

Increase timeout value:
```nginx
check timeout=30000 type=https;  # 30 seconds
```

### Debug Logging

Enable debug logging in nginx:
```nginx
error_log /var/log/nginx/error.log debug;
```

## License

BSD-3-Clause (same as original module)

## Credits

Based on [ngx_dynamic_healthcheck](https://github.com/ZigzagAK/ngx_dynamic_healthcheck) by Aleksei Konovkin.

