# Быстрый старт

## Установка за 5 минут

### 1. Скачайте исходники nginx

```bash
cd /tmp
wget http://nginx.org/download/nginx-1.24.0.tar.gz
tar -xzf nginx-1.24.0.tar.gz
```

### 2. Соберите nginx с модулем

```bash
# Укажите путь к модулю и исходникам nginx
export NGINX_SRC=/tmp/nginx-1.24.0
cd /path/to/x5digital_dynamic_hc

# Запустите скрипт сборки
./build.sh
```

### 3. Установите nginx

```bash
cd $NGINX_SRC
sudo make install
```

### 4. Проверьте установку

```bash
/usr/local/nginx/sbin/nginx -V 2>&1 | grep x5digital_dynamic_hc
```

Если видите `ngx_http_x5digital_dynamic_hc_module` - модуль установлен!

## Использование

### Минимальная конфигурация

Добавьте в `/usr/local/nginx/conf/nginx.conf`:

```nginx
http {
    upstream backend {
        zone backend 128k;
        server example.com:443;
        
        check fall=2 rise=2 interval=10 timeout=10000 type=https;
        check_request_uri GET /health;
        check_response_codes 200;
    }
    
    server {
        listen 80;
        location / {
            proxy_pass http://backend;
        }
    }
}
```

### Запуск

```bash
# Проверьте конфигурацию
/usr/local/nginx/sbin/nginx -t

# Запустите
/usr/local/nginx/sbin/nginx
```

## Примеры

### Проверка API endpoint

```nginx
upstream api {
    zone api 128k;
    server api.example.com:443;
    
    check type=https fall=2 rise=2 interval=10 timeout=5000;
    check_request_uri GET /api/v1/health;
    check_response_codes 200;
}
```

### С кастомными заголовками

```nginx
upstream secure {
    zone secure 128k;
    server secure.example.com:443;
    
    check type=https fall=2 rise=2 interval=15 timeout=10000;
    check_request_uri GET /health;
    check_request_headers Host:secure.example.com Authorization:Basic\ token;
    check_response_codes 200;
}
```

## API для мониторинга

Добавьте в конфигурацию:

```nginx
server {
    listen 8888;
    
    location = /healthcheck/status {
        healthcheck_status;
    }
}
```

Проверка статуса:
```bash
curl http://localhost:8888/healthcheck/status
```

## Дополнительная информация

- Полная документация: [INSTALL.md](INSTALL.md)
- Описание модуля: [README.md](README.md)

