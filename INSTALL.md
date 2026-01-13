# Руководство по установке и использованию

## Требования

- Nginx исходный код (версия 1.9.11 или выше)
- Компилятор C++ (поддержка C++11)
- OpenSSL библиотеки для разработки
- Make и другие стандартные инструменты сборки

## Установка

### Шаг 1: Получение исходного кода Nginx

```bash
# Скачайте исходный код nginx
cd /tmp
wget http://nginx.org/download/nginx-1.24.0.tar.gz
tar -xzf nginx-1.24.0.tar.gz
cd nginx-1.24.0
```

Или используйте другую версию nginx, которая у вас уже есть.

### Шаг 2: Клонирование модуля

```bash
# Если модуль еще не скачан
git clone <repository-url> /path/to/dynamic-hc
cd /path/to/dynamic-hc
```

### Шаг 3: Конфигурация и сборка Nginx

```bash
# Перейдите в директорию с исходниками nginx
cd /tmp/nginx-1.24.0  # или ваш путь

# Настройте nginx с модулем
./configure \
    --add-module=/path/to/x5digital_dynamic_hc \
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

# Соберите nginx
make

# Установите (требуются права root)
sudo make install
```

**Важно:** Замените `/path/to/x5digital_dynamic_hc` на реальный путь к модулю.

### Шаг 4: Проверка установки

```bash
# Проверьте, что модуль загружен
/usr/local/nginx/sbin/nginx -V 2>&1 | grep dynamic_healthcheck
```

Должна быть строка с `ngx_http_x5digital_dynamic_hc_module`.

## Конфигурация

### Базовая конфигурация HTTPS health check

Добавьте в ваш `nginx.conf`:

```nginx
http {
    # Глобальные настройки health check (опционально)
    healthcheck fall=2 rise=2 interval=60 timeout=10000 type=https;
    healthcheck_request_uri GET /health;
    healthcheck_response_codes 200;
    
    upstream backend {
        zone backend 128k;  # Обязательно нужна директива zone
        
        # Укажите домены или IP адреса
        server backend1.example.com:443;
        server backend2.example.com:443;
        
        # Настройки health check для этого upstream
        check fall=2 rise=2 interval=10 timeout=10000 type=https;
        check_request_uri GET /health;
        check_response_codes 200;
        check_response_body ".*healthy.*";  # Опционально: проверка содержимого ответа
    }
    
    server {
        listen 80;
        server_name example.com;
        
        location / {
            proxy_pass http://backend;
            proxy_set_header Host $host;
            proxy_set_header X-Real-IP $remote_addr;
        }
    }
    
    # API для управления health checks (опционально)
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
}
```

### Примеры конфигурации

#### Пример 1: Простой HTTPS health check

```nginx
upstream api {
    zone api 128k;
    server api.example.com:443;
    
    check fall=3 rise=2 interval=15 timeout=5000 type=https;
    check_request_uri GET /api/health;
    check_response_codes 200 201;
}
```

#### Пример 2: HTTPS с кастомными заголовками

```nginx
upstream secure_backend {
    zone secure_backend 128k;
    server secure.example.com:443;
    
    check fall=2 rise=2 interval=10 timeout=10000 type=https;
    check_request_uri GET /health;
    check_request_headers Host:secure.example.com Authorization:Basic\ dXNlcjpwYXNz;
    check_response_codes 200;
    check_response_body ".*status.*ok.*";
}
```

#### Пример 3: Несколько upstream с разными настройками

```nginx
http {
    # Глобальные настройки по умолчанию
    healthcheck fall=2 rise=2 interval=60 timeout=10000 type=https;
    
    upstream backend1 {
        zone backend1 128k;
        server backend1.example.com:443;
        # Использует глобальные настройки
    }
    
    upstream backend2 {
        zone backend2 128k;
        server backend2.example.com:443;
        
        # Переопределяет глобальные настройки
        check fall=3 rise=3 interval=30 timeout=5000 type=https;
        check_request_uri GET /custom/health;
        check_response_codes 200 204;
    }
}
```

## Использование

### Запуск Nginx

```bash
# Проверьте конфигурацию
/usr/local/nginx/sbin/nginx -t

# Запустите nginx
/usr/local/nginx/sbin/nginx

# Или если nginx уже запущен, перезагрузите конфигурацию
/usr/local/nginx/sbin/nginx -s reload
```

### Проверка статуса health checks через API

```bash
# Получить конфигурацию всех health checks
curl http://localhost:8888/healthcheck/get

# Получить текущий статус
curl http://localhost:8888/healthcheck/status

# Получить статус конкретного upstream
curl "http://localhost:8888/healthcheck/status?upstream=backend"

# Получить статус stream upstream
curl "http://localhost:8888/healthcheck/status?stream=1"
```

### Динамическое обновление параметров

```bash
# Обновить параметры health check
curl "http://localhost:8888/healthcheck/update?upstream=backend&fall=3&rise=2&interval=20&type=https&request_uri=GET%20/health&response_codes=200"

# Включить/выключить health check для upstream
curl "http://localhost:8888/healthcheck/update?upstream=backend&off=1"  # выключить
curl "http://localhost:8888/healthcheck/update?upstream=backend&off=0"  # включить

# Отключить конкретный хост
curl "http://localhost:8888/healthcheck/update?upstream=backend&disable_host=backend1.example.com:443"

# Включить хост обратно
curl "http://localhost:8888/healthcheck/update?upstream=backend&enable_host=backend1.example.com:443"
```

## Параметры директивы `check`

- `type=https` - тип проверки (https, http, tcp, ssl)
- `fall=N` - количество последовательных неудач перед пометкой как down (по умолчанию: 1)
- `rise=N` - количество последовательных успехов перед пометкой как up (по умолчанию: 1)
- `interval=SEC` - интервал между проверками в секундах (по умолчанию: 10)
- `timeout=MS` - таймаут операции проверки в миллисекундах (по умолчанию: 1000)
- `keepalive=N` - максимальное количество запросов на одно соединение (по умолчанию: 1)
- `port=PORT` - порт для проверки (если отличается от порта в server)
- `passive` - пассивный режим (проверки не выполняются при успешных ответах)

## Директивы для HTTPS проверок

- `check_request_uri METHOD URI` - метод и URI для запроса (например: `GET /health`)
- `check_request_headers HEADER:VALUE ...` - дополнительные HTTP заголовки
- `check_request_body BODY` - тело запроса (опционально)
- `check_response_codes CODE ...` - допустимые коды ответа (например: `200 201 204`)
- `check_response_body REGEXP` - регулярное выражение для проверки тела ответа

## Отладка

### Включение debug логов

```nginx
error_log /var/log/nginx/error.log debug;
```

### Проверка логов

```bash
# Просмотр логов ошибок
tail -f /usr/local/nginx/logs/error.log

# Фильтрация логов по health check
grep "x5digital_dynamic_hc" /usr/local/nginx/logs/error.log
```

### Типичные проблемы

1. **SSL handshake failed**
   - Проверьте, что сервер поддерживает TLS
   - Убедитесь, что порт правильный (обычно 443)
   - Проверьте сертификат сервера

2. **Connection timeout**
   - Увеличьте `timeout` параметр
   - Проверьте сетевую доступность сервера

3. **4xx ошибки**
   - Убедитесь, что используется правильный URI
   - Проверьте заголовки запроса
   - Убедитесь, что используется HTTPS, а не HTTP

4. **Модуль не загружается**
   - Проверьте, что nginx собран с `--with-http_ssl_module`
   - Убедитесь, что путь к модулю правильный в configure

## Примеры использования

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

### Проверка с аутентификацией

```nginx
upstream secure_api {
    zone secure_api 128k;
    server secure-api.example.com:443;
    
    check type=https fall=2 rise=2 interval=15 timeout=10000;
    check_request_uri GET /health;
    check_request_headers Host:secure-api.example.com Authorization:Basic\ base64encodedcredentials;
    check_response_codes 200;
}
```

### Проверка с проверкой содержимого ответа

```nginx
upstream service {
    zone service 128k;
    server service.example.com:443;
    
    check type=https fall=3 rise=2 interval=20 timeout=10000;
    check_request_uri GET /status;
    check_response_codes 200;
    check_response_body ".*\"status\":\"ok\".*";
}
```

## Дополнительная информация

- Подробная документация: см. `README.md`
- Изменения от оригинального модуля: см. `CHANGES.md`
- Исходный модуль: https://github.com/ZigzagAK/ngx_dynamic_healthcheck

