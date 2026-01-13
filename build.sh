#!/bin/bash

# Build script for x5digital_dynamic_hc module with HTTPS support
# This script helps build nginx with this module

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODULE_DIR="$SCRIPT_DIR"
MODULE_NAME="x5digital_dynamic_hc"

echo "=========================================="
echo "Building x5digital_dynamic_hc module"
echo "with HTTPS support"
echo "=========================================="
echo "Module directory: $MODULE_DIR"
echo ""

# Check if nginx source is provided
if [ -z "$NGINX_SRC" ]; then
    echo "Error: NGINX_SRC environment variable is not set"
    echo ""
    echo "Usage:"
    echo "  1. Download nginx source:"
    echo "     cd /tmp"
    echo "     wget http://nginx.org/download/nginx-1.24.0.tar.gz"
    echo "     tar -xzf nginx-1.24.0.tar.gz"
    echo ""
    echo "  2. Set NGINX_SRC and build:"
    echo "     export NGINX_SRC=/tmp/nginx-1.24.0"
    echo "     cd $MODULE_DIR"
    echo "     ./build.sh"
    echo ""
    echo "Or specify nginx source directory:"
    echo "     NGINX_SRC=/path/to/nginx/source ./build.sh"
    exit 1
fi

if [ ! -d "$NGINX_SRC" ]; then
    echo "Error: NGINX_SRC directory does not exist: $NGINX_SRC"
    exit 1
fi

if [ ! -f "$NGINX_SRC/configure" ]; then
    echo "Error: $NGINX_SRC doesn't appear to be nginx source directory"
    echo "       (configure script not found)"
    exit 1
fi

cd "$NGINX_SRC"

echo "Nginx source: $NGINX_SRC"
echo ""

# Check if already configured
if [ -f "Makefile" ]; then
    echo "Warning: Nginx is already configured."
    read -p "Do you want to reconfigure? (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "Using existing configuration. Run 'make' to build."
        exit 0
    fi
    echo "Cleaning previous configuration..."
    make clean 2>/dev/null || true
fi

# Configure nginx with this module
echo "Configuring nginx with x5digital_dynamic_hc module..."
echo ""

./configure \
    --add-module="$MODULE_DIR" \
    --with-http_ssl_module \
    --prefix=/usr/local/nginx \
    --sbin-path=/usr/local/nginx/sbin/nginx \
    --conf-path=/usr/local/nginx/conf/nginx.conf \
    --error-log-path=/usr/local/nginx/logs/error.log \
    --pid-path=/usr/local/nginx/logs/nginx.pid \
    --lock-path=/usr/local/nginx/logs/nginx.lock \
    --http-log-path=/usr/local/nginx/logs/access.log \
    --with-http_realip_module \
    --with-http_stub_status_module \
    --with-http_gzip_static_module \
    --with-pcre \
    --with-file-aio \
    --with-http_v2_module

if [ $? -ne 0 ]; then
    echo ""
    echo "Error: Configuration failed!"
    echo "Make sure you have all required dependencies installed."
    exit 1
fi

echo ""
echo "Building nginx..."
make

if [ $? -ne 0 ]; then
    echo ""
    echo "Error: Build failed!"
    exit 1
fi

echo ""
echo "=========================================="
echo "Build completed successfully!"
echo "=========================================="
echo ""
echo "Nginx binary location: $NGINX_SRC/objs/nginx"
echo ""
echo "Next steps:"
echo ""
echo "1. Test the binary:"
echo "   $NGINX_SRC/objs/nginx -t"
echo ""
echo "2. Install nginx (requires root):"
echo "   cd $NGINX_SRC"
echo "   sudo make install"
echo ""
echo "3. Verify module is loaded:"
echo "   /usr/local/nginx/sbin/nginx -V 2>&1 | grep x5digital_dynamic_hc"
echo ""
echo "4. Add to your nginx.conf:"
echo "   upstream backend {"
echo "       zone backend 128k;"
echo "       server example.com:443;"
echo "       check fall=2 rise=2 interval=10 timeout=10000 type=https;"
echo "       check_request_uri GET /health;"
echo "       check_response_codes 200;"
echo "   }"
echo ""
echo "See INSTALL.md for detailed installation and usage instructions."

