# x5digital_dynamic_hc - Модуль динамических проверок здоровья для Nginx

Улучшенная версия модуля [ngx_dynamic_healthcheck](https://github.com/ZigzagAK/ngx_dynamic_healthcheck) с поддержкой HTTPS проверок здоровья и динамическим управлением upstream серверами.

## Содержание

- [Описание](#описание)
- [Возможности](#возможности)
- [Отличия от оригинального модуля](#отличия-от-оригинального-модуля)
- [Установка](#установка)
- [Быстрый старт](#быстрый-старт)
- [Конфигурация](#конфигурация)
- [API для управления](#api-для-управления)
- [Динамическое управление upstream](#динамическое-управление-upstream)
- [Сохранение конфигурации](#сохранение-конфигурации)
- [Решение проблем](#решение-проблем)
- [Лицензия](#лицензия)

## Описание

Модуль `x5digital_dynamic_hc` расширяет функциональность Nginx, добавляя возможность:

- **Динамических проверок здоровья** серверов в upstream без перезагрузки конфигурации
- **Поддержки различных типов проверок**: HTTP, HTTPS, TCP, SSL handshake
- **Динамического управления** списком серверов в upstream через API
- **Автоматического сохранения** конфигурации и списка серверов на диск

Модуль работает в режиме реального времени и позволяет изменять параметры проверок здоровья и управлять серверами без остановки Nginx.

## Возможности

### Типы проверок здоровья

1. **HTTPS проверки** (`type=https`)
   - Полное SSL/TLS handshake с upstream сервером
   - HTTP запросы по зашифрованному соединению
   - Проверка HTTP ответов и кодов состояния
   - Поддержка SNI (Server Name Indication)
   - Правильная обработка доменных имен

2. **HTTP проверки** (`type=http`)
   - Стандартные HTTP запросы
   - Проверка кодов ответа
   - Проверка содержимого ответа (regexp)

3. **TCP проверки** (`type=tcp`)
   - Проверка доступности порта
   - Установка TCP соединения

4. **SSL handshake проверки** (`type=ssl`)
   - Проверка SSL/TLS handshake без HTTP запросов
   - Валидация сертификата

### Динамическое управление

- **Изменение параметров проверок** без перезагрузки Nginx
- **Добавление/удаление серверов** в upstream через API
- **Обновление параметров серверов** (weight, max_fails, fail_timeout и т.д.)
- **Управление состоянием серверов** (up/down, backup)

### Персистентность

- **Сохранение параметров проверок** на диск
- **Сохранение списка серверов** в файл peers
- **Автоматическая загрузка** при старте Nginx
- **Восстановление состояния** после перезагрузки

## Отличия от оригинального модуля

### Основные улучшения

1. **Поддержка HTTPS** (`type=https`)
   - Полная реализация SSL/TLS соединений
   - Правильная обработка доменных имен в SNI и Host заголовке
   - Исправлена проблема, когда модуль отправлял HTTP запросы на HTTPS порты

2. **Динамическое управление upstream**
   - Интеграция функциональности из `ngx_dynamic_upstream`
   - Удобный JSON API вместо неудобных query параметров
   - Автоматическая интеграция с системой проверок здоровья

3. **Сохранение peers в файл**
   - Автоматическое сохранение списка серверов
   - Загрузка при старте Nginx
   - Формат файла совместим с оригинальным модулем

### Исправленные проблемы

- **Сохранение доменных имен**: Модуль теперь использует оригинальное доменное имя для SNI и Host заголовка вместо разрешенного IP адреса
- **Правильная обработка HTTPS**: Модуль корректно устанавливает SSL/TLS соединение перед отправкой HTTP запросов
- **Логирование**: Исправлена проблема с засорением error.log при создании директорий

## Интеграция модулей

Проект объединяет два модуля в единое решение:

1. **Модуль проверки здоровья** (`ngx_dynamic_healthcheck`) - проверяет доступность серверов
2. **Модуль динамического управления upstream** (`ngx_dynamic_upstream`) - управляет списком серверов через API

### Как модули работают вместе

Модули **полностью интегрированы** и работают автоматически:

#### Автоматическая интеграция при добавлении серверов

Когда вы добавляете сервер в upstream через API динамического управления, модуль **автоматически**:

1. ✅ Добавляет сервер в upstream блок
2. ✅ Проверяет, настроен ли healthcheck для этого upstream
3. ✅ Если healthcheck настроен - автоматически инициализирует состояние проверки здоровья для нового сервера
4. ✅ Начинает проверять здоровье сервера с параметрами из конфигурации upstream

**Пример:**

```nginx
upstream backend {
    zone backend 128k;
    
    # Настройка healthcheck
    check fall=2 rise=2 interval=10 timeout=10000 type=https;
    check_request_uri GET /health;
    check_response_codes 200;
    
    # Серверы можно добавить через API - они автоматически попадут под проверку здоровья!
}
```

Добавление сервера через API:
```bash
curl -X POST http://localhost:8888/dynamic_upstream \
  -H "Content-Type: application/json" \
  -d '{
    "upstream": "backend",
    "action": "add",
    "server": "192.168.1.100:443"
  }'
```

После этого:
- Сервер добавлен в upstream ✅
- Healthcheck автоматически начал проверять этот сервер ✅
- Используются параметры проверки из конфигурации (`type=https`, `fall=2`, `rise=2` и т.д.) ✅

#### Синхронизация состояния

Модули синхронизируют состояние серверов:

- Когда healthcheck обнаруживает, что сервер недоступен, он устанавливает флаг `peer->down = 1`
- Модуль динамического upstream видит это состояние и учитывает его при балансировке
- При обновлении сервера через API состояние синхронизируется с healthcheck

#### Требования для интеграции

Для работы интеграции необходимо:

1. **Директива `zone`** - обязательна для динамического управления:
   ```nginx
   upstream backend {
       zone backend 128k;  # Обязательно!
   }
   ```

2. **Директива `check`** - для включения проверок здоровья:
   ```nginx
   check type=https;  # Настройте параметры проверки
   ```

3. **Оба модуля включены** - они объединены в один проект и работают вместе

#### Преимущества интеграции

- 🚀 **Автоматизация**: Не нужно настраивать healthcheck отдельно для каждого динамически добавленного сервера
- 🔄 **Синхронизация**: Состояние серверов синхронизировано между модулями
- ⚡ **Производительность**: Проверки здоровья начинаются сразу после добавления сервера
- 🛡️ **Надежность**: Серверы автоматически исключаются из балансировки при проблемах со здоровьем

## Установка

### Требования

- Исходный код Nginx (версия 1.9.11 или выше)
- Компилятор C++ (C++11 или новее)
- Библиотеки разработки OpenSSL
- Make и стандартные инструменты сборки

### Быстрая установка

1. **Скачайте исходный код Nginx:**
   ```bash
   cd /tmp
   wget http://nginx.org/download/nginx-1.24.0.tar.gz
   tar -xzf nginx-1.24.0.tar.gz
   ```

2. **Соберите Nginx с модулем:**
   ```bash
   export NGINX_SRC=/tmp/nginx-1.24.0
   cd /path/to/dynamic-hc
   ./build.sh
   ```

3. **Установите:**
   ```bash
   cd $NGINX_SRC
   sudo make install
   ```

4. **Проверьте установку:**
   ```bash
   /usr/local/nginx/sbin/nginx -V 2>&1 | grep x5digital_dynamic_hc
   ```

### Ручная сборка

Если вы хотите собрать модуль вручную:

```bash
cd /path/to/nginx/source
./configure \
    --add-module=/path/to/dynamic-hc \
    --with-http_ssl_module \
    [другие опции]
make
sudo make install
```

**Важно**: Обязательно включите `--with-http_ssl_module` для поддержки HTTPS проверок.

## Быстрый старт

### Базовая конфигурация HTTPS проверки

Добавьте в ваш `nginx.conf`:

```nginx
http {
    upstream backend {
        zone backend 128k;
        server backend1.example.com:443;
        server backend2.example.com:443;
        
        # HTTPS проверка здоровья
        check fall=2 rise=2 interval=10 timeout=10000 type=https;
        check_request_uri GET /health;
        check_response_codes 200;
    }
    
    server {
        listen 80;
        server_name example.com;
        
        location / {
            proxy_pass http://backend;
        }
    }
    
    # API для управления
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
        
        location = /dynamic_upstream {
            dynamic_upstream;
        }
    }
}
```

После этого модуль автоматически начнет проверять здоровье серверов в upstream `backend`.

## Конфигурация

### Директивы модуля

#### `check` - Основная директива проверки здоровья

Включает проверку здоровья для upstream блока.

**Синтаксис:**
```nginx
check [fall=N] [rise=N] [interval=SEC] [timeout=MS] [keepalive=N] type=TYPE;
```

**Параметры:**
- `fall=N` - Количество последовательных неудач перед пометкой сервера как недоступного (по умолчанию: 1)
- `rise=N` - Количество последовательных успехов перед пометкой сервера как доступного (по умолчанию: 1)
- `interval=SEC` - Интервал между проверками в секундах (по умолчанию: 10)
- `timeout=MS` - Таймаут для операций проверки в миллисекундах (по умолчанию: 1000)
- `keepalive=N` - Максимальное количество запросов на одно соединение (по умолчанию: 1)
- `type=TYPE` - Тип проверки: `http`, `https`, `tcp`, `ssl` (обязательный параметр)

**Примеры:**

```nginx
# HTTPS проверка
check fall=3 rise=2 interval=15 timeout=5000 type=https;

# HTTP проверка
check fall=2 rise=2 interval=10 timeout=3000 type=http;

# TCP проверка
check fall=2 rise=2 interval=5 timeout=2000 type=tcp;

# SSL handshake проверка
check fall=2 rise=2 interval=30 timeout=5000 type=ssl;
```

#### `check_request_uri` - URI для HTTP/HTTPS проверок

Указывает HTTP метод и URI для проверки.

**Синтаксис:**
```nginx
check_request_uri METHOD URI;
```

**Примеры:**

```nginx
check_request_uri GET /health;
check_request_uri POST /api/health/check;
check_request_uri GET /api/v1/status;
```

#### `check_request_headers` - Заголовки запроса

Добавляет пользовательские HTTP заголовки к запросам проверки.

**Синтаксис:**
```nginx
check_request_headers HEADER:VALUE [HEADER:VALUE ...];
```

**Примеры:**

```nginx
# Один заголовок
check_request_headers Host:example.com;

# Несколько заголовков
check_request_headers Host:example.com Authorization:Basic\ dXNlcjpwYXNz;

# Заголовок с пробелами (используйте обратный слэш для экранирования)
check_request_headers X-Custom-Header:value\ with\ spaces;
```

#### `check_response_codes` - Коды ответа

Список допустимых HTTP кодов ответа.

**Синтаксис:**
```nginx
check_response_codes CODE [CODE ...];
```

**Примеры:**

```nginx
# Один код
check_response_codes 200;

# Несколько кодов
check_response_codes 200 201 204;

# Диапазон кодов
check_response_codes 200 201 202 204;
```

#### `check_response_body` - Проверка тела ответа

Регулярное выражение для проверки содержимого ответа.

**Синтаксис:**
```nginx
check_response_body REGEXP;
```

**Примеры:**

```nginx
# Проверка наличия слова "healthy"
check_response_body ".*healthy.*";

# Проверка JSON ответа
check_response_body "\"status\":\"ok\"";

# Сложное регулярное выражение
check_response_body "status.*(ok|healthy|ready)";
```

#### `check_disable_host` - Отключение хостов

Временно отключает проверку для указанных хостов.

**Синтаксис:**
```nginx
check_disable_host HOST [HOST ...];
```

**Пример:**

```nginx
check_disable_host 192.168.1.100:443 192.168.1.101:443;
```

#### `check_persistent` - Сохранение конфигурации

Указывает путь для сохранения параметров проверки здоровья.

**Синтаксис:**
```nginx
check_persistent PATH;
```

**Пример:**

```nginx
check_persistent /var/lib/nginx/healthcheck/backend;
```

#### `dynamic_upstream_state_file` - Файл для сохранения серверов

Указывает путь к файлу для сохранения списка серверов в upstream.

**Синтаксис:**
```nginx
dynamic_upstream_state_file PATH;
```

**Пример:**

```nginx
dynamic_upstream_state_file /var/lib/nginx/peers/backend.peers;
```

### Глобальная конфигурация

Вы можете задать глобальные настройки проверок здоровья, которые будут применяться ко всем upstream блокам:

```nginx
http {
    # Глобальные настройки
    healthcheck fall=2 rise=2 interval=60 timeout=10000 type=https;
    healthcheck_request_uri GET /health;
    healthcheck_response_codes 200;
    
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
    }
}
```

### Примеры конфигураций

#### HTTPS проверка с аутентификацией

```nginx
upstream secure_backend {
    zone secure_backend 128k;
    server api.example.com:443;
    
    check fall=3 rise=2 interval=15 timeout=10000 type=https;
    check_request_uri GET /api/health;
    check_request_headers Host:api.example.com Authorization:Bearer\ token123;
    check_response_codes 200;
    check_response_body "\"status\":\"ok\"";
}
```

#### HTTP проверка с проверкой содержимого

```nginx
upstream web_backend {
    zone web_backend 128k;
    server web1.example.com:80;
    server web2.example.com:80;
    
    check fall=2 rise=2 interval=10 timeout=5000 type=http;
    check_request_uri GET /health;
    check_response_codes 200 201;
    check_response_body ".*healthy.*";
}
```

#### TCP проверка

```nginx
upstream db_backend {
    zone db_backend 128k;
    server db1.example.com:5432;
    server db2.example.com:5432;
    
    check fall=2 rise=2 interval=5 timeout=2000 type=tcp;
}
```

#### SSL handshake проверка

```nginx
upstream ssl_backend {
    zone ssl_backend 128k;
    server ssl.example.com:443;
    
    check fall=2 rise=2 interval=30 timeout=5000 type=ssl;
}
```

## API для управления

Модуль предоставляет REST API для управления проверками здоровья и upstream серверами.

### Настройка API endpoints

Добавьте в конфигурацию Nginx:

```nginx
server {
    listen 8888;
    
    # Получение конфигурации проверки здоровья
    location = /healthcheck/get {
        healthcheck_get;
    }
    
    # Получение статуса проверки здоровья
    location = /healthcheck/status {
        healthcheck_status;
    }
    
    # Обновление параметров проверки здоровья
    location = /healthcheck/update {
        healthcheck_update;
    }
    
    # Управление upstream серверами
    location = /dynamic_upstream {
        dynamic_upstream;
    }
}
```

### Получение конфигурации

**Запрос:**
```bash
curl "http://localhost:8888/healthcheck/get?upstream=backend"
```

**Ответ:**
```
type:https
fall:2
rise:2
timeout:10000
interval:10
keepalive:1
request_uri:GET /health
response_codes:200
```

### Получение статуса

**Запрос:**
```bash
curl "http://localhost:8888/healthcheck/status?upstream=backend"
```

**Ответ:**
```
192.168.1.100:443 up fall=0 rise=2
192.168.1.101:443 down fall=3 rise=0
```

### Обновление параметров

**Запрос:**
```bash
curl "http://localhost:8888/healthcheck/update?upstream=backend&type=https&fall=3&rise=2&interval=15&request_uri=GET%20/health&response_codes=200"
```

**Ответ:**
```
ok
```

**Параметры запроса:**
- `upstream=NAME` - Имя upstream блока (обязательно)
- `type=TYPE` - Тип проверки: `http`, `https`, `tcp`, `ssl`
- `fall=N` - Количество неудач
- `rise=N` - Количество успехов
- `interval=SEC` - Интервал в секундах
- `timeout=MS` - Таймаут в миллисекундах
- `request_uri=METHOD URI` - URI для проверки
- `response_codes=CODE [CODE ...]` - Коды ответа
- `request_headers=HEADER:VALUE [HEADER:VALUE ...]` - Заголовки
- `response_body=REGEXP` - Регулярное выражение для тела ответа

## Динамическое управление upstream

Модуль позволяет динамически добавлять, удалять и обновлять серверы в upstream блоках без перезагрузки Nginx.

### JSON API (Рекомендуется)

JSON API более удобен и читаем, чем query параметры.

#### Добавление сервера

**Запрос:**
```bash
curl -X POST http://localhost:8888/dynamic_upstream \
  -H "Content-Type: application/json" \
  -d '{
    "upstream": "backend",
    "action": "add",
    "server": "192.168.1.100:443",
    "weight": 10,
    "max_fails": 3,
    "fail_timeout": 5000,
    "max_conns": 100,
    "backup": false,
    "down": false
  }'
```

**Ответ:**
```json
{"status":"ok"}
```

#### Удаление сервера

**Запрос:**
```bash
curl -X DELETE http://localhost:8888/dynamic_upstream \
  -H "Content-Type: application/json" \
  -d '{
    "upstream": "backend",
    "action": "remove",
    "server": "192.168.1.100:443"
  }'
```

**Ответ:**
```json
{"status":"ok"}
```

#### Обновление параметров сервера

**Запрос:**
```bash
curl -X PUT http://localhost:8888/dynamic_upstream \
  -H "Content-Type: application/json" \
  -d '{
    "upstream": "backend",
    "action": "update",
    "server": "192.168.1.100:443",
    "weight": 20,
    "max_fails": 5,
    "fail_timeout": 10000
  }'
```

**Ответ:**
```json
{"status":"ok"}
```

#### Пометить сервер как недоступный

**Запрос:**
```bash
curl -X PUT http://localhost:8888/dynamic_upstream \
  -H "Content-Type: application/json" \
  -d '{
    "upstream": "backend",
    "action": "update",
    "server": "192.168.1.100:443",
    "down": true
  }'
```

#### Пометить сервер как резервный

**Запрос:**
```bash
curl -X PUT http://localhost:8888/dynamic_upstream \
  -H "Content-Type: application/json" \
  -d '{
    "upstream": "backend",
    "action": "update",
    "server": "192.168.1.100:443",
    "backup": true
  }'
```

#### Получение списка серверов

**Запрос:**
```bash
curl "http://localhost:8888/dynamic_upstream?upstream=backend"
```

**Ответ:**
```
server 192.168.1.100:443 weight=10 max_fails=3 fail_timeout=5000 max_conns=100;
server 192.168.1.101:443 weight=10 max_fails=3 fail_timeout=5000 backup;
server 192.168.1.102:443 weight=5 max_fails=5 fail_timeout=10000 down;
```

### Query Parameters API (Устаревший, но поддерживается)

Для обратной совместимости модуль поддерживает старый формат с query параметрами:

#### Добавление сервера

```bash
curl "http://localhost:8888/dynamic_upstream?upstream=backend&add=&server=192.168.1.100:443&weight=10&max_fails=3&fail_timeout=5000&max_conns=100"
```

#### Удаление сервера

```bash
curl "http://localhost:8888/dynamic_upstream?upstream=backend&remove=&server=192.168.1.100:443"
```

#### Обновление сервера

```bash
curl "http://localhost:8888/dynamic_upstream?upstream=backend&server=192.168.1.100:443&weight=20&max_fails=5"
```

#### Параметры query API

- `upstream=NAME` - Имя upstream блока (обязательно)
- `server=ADDRESS` - Адрес сервера (host:port)
- `add=` - Добавить сервер
- `remove=` - Удалить сервер
- `weight=N` - Вес сервера
- `max_fails=N` - Максимальное количество неудач
- `fail_timeout=MS` - Таймаут неудачи в миллисекундах
- `max_conns=N` - Максимальное количество соединений
- `backup=` - Пометить как резервный сервер
- `down=` - Пометить как недоступный
- `up=` - Пометить как доступный

### Важные замечания

1. **Требуется zone**: Upstream блок должен иметь директиву `zone` для динамического управления:
   ```nginx
   upstream backend {
       zone backend 128k;  # Обязательно!
   }
   ```

2. **Интеграция с проверками здоровья**: Серверы, добавленные через API, автоматически интегрируются с системой проверок здоровья, если она настроена для upstream.

3. **DNS разрешение**: Модуль автоматически разрешает доменные имена в IP адреса при добавлении серверов.

4. **Потокобезопасность**: Все операции выполняются с использованием блокировок для обеспечения потокобезопасности.

## Сохранение конфигурации

Модуль поддерживает автоматическое сохранение конфигурации проверок здоровья и списка серверов на диск.

### Сохранение параметров проверок здоровья

Используйте директиву `check_persistent` для сохранения параметров проверок:

```nginx
upstream backend {
    zone backend 128k;
    server backend.example.com:443;
    
    check type=https;
    check_persistent /var/lib/nginx/healthcheck/backend;
}
```

**Как это работает:**

1. При изменении параметров через API они автоматически сохраняются в файл
2. При старте Nginx параметры загружаются из файла
3. Директория создается автоматически, если не существует

**Формат файла:**
```
type:https
fall:2
rise:2
timeout:10000
interval:10
keepalive:1
request_uri:GET /health
response_codes:200
```

### Сохранение списка серверов (peers)

Используйте директиву `dynamic_upstream_state_file` для сохранения списка серверов:

```nginx
upstream backend {
    zone backend 128k;
    
    # Файл для сохранения списка серверов
    dynamic_upstream_state_file /var/lib/nginx/peers/backend.peers;
    
    check type=https;
}
```

**Как это работает:**

1. **Автоматическое сохранение**: При добавлении, удалении или обновлении серверов через API список автоматически сохраняется в файл

2. **Автоматическая загрузка**: При старте Nginx worker процессы автоматически загружают серверы из файла (если он существует) и добавляют их в upstream

3. **Формат файла**: Простой текстовый формат, похожий на конфигурацию Nginx:
   ```
   server 192.168.1.100:443 weight=10 max_fails=3 fail_timeout=5000 max_conns=100;
   server 192.168.1.101:443 weight=10 max_fails=3 fail_timeout=5000 backup;
   server 192.168.1.102:443 weight=5 max_fails=5 fail_timeout=10000 down;
   ```

4. **Создание директорий**: Модуль автоматически создает путь к директории, если она не существует (аналогично `check_persistent`)

5. **Разрешение путей**: Если в nginx.conf установлен `working_directory`, путь считается относительно этой директории. Иначе - абсолютный путь

**Пример рабочего процесса:**

1. Запустите Nginx с пустым upstream:
   ```nginx
   upstream backend {
       zone backend 128k;
       dynamic_upstream_state_file /var/lib/nginx/peers/backend.peers;
       check type=https;
   }
   ```

2. Добавьте серверы через API:
   ```bash
   curl -X POST http://localhost:8888/dynamic_upstream \
     -H "Content-Type: application/json" \
     -d '{"upstream": "backend", "action": "add", "server": "192.168.1.100:443"}'
   ```

3. Серверы автоматически сохраняются в `/var/lib/nginx/peers/backend.peers`

4. После перезагрузки Nginx серверы автоматически загружаются из файла и добавляются обратно в upstream

**Важно**: Серверы, добавленные через API, автоматически интегрируются с системой проверок здоровья, если она настроена для upstream.

## Решение проблем

### Проблемы с SSL handshake

Если вы видите ошибки SSL handshake, проверьте:

1. **Поддержка TLS**: Убедитесь, что upstream сервер поддерживает TLS
2. **Валидность сертификата**: Проверьте, что сертификат действителен
3. **Файрвол**: Убедитесь, что файрвол разрешает SSL соединения
4. **Порт**: Проверьте, что порт правильный (обычно 443 для HTTPS)

**Диагностика:**
```bash
# Проверка SSL соединения вручную
openssl s_client -connect backend.example.com:443 -servername backend.example.com

# Проверка с помощью curl
curl -v https://backend.example.com/health
```

### Таймауты соединения

Если возникают таймауты, увеличьте значение timeout:

```nginx
check timeout=30000 type=https;  # 30 секунд
```

Также проверьте:
- Доступность сети до upstream сервера
- Правильность порта
- Настройки файрвола

### Проблемы с DNS разрешением

Если модуль не может разрешить доменное имя:

1. Проверьте DNS настройки системы
2. Убедитесь, что доменное имя доступно из Nginx сервера
3. Используйте IP адреса напрямую, если DNS не работает

### Проблемы с сохранением файлов

Если файлы не сохраняются:

1. **Права доступа**: Убедитесь, что Nginx имеет права на запись в директорию:
   ```bash
   sudo chown -R nginx:nginx /var/lib/nginx/
   sudo chmod -R 755 /var/lib/nginx/
   ```

2. **Директория существует**: Модуль создает директорию автоматически, но проверьте права

3. **Путь**: Убедитесь, что путь указан правильно (абсолютный или относительно `working_directory`)

### Отладка

Включите отладочное логирование в Nginx:

```nginx
error_log /var/log/nginx/error.log debug;
```

Это поможет увидеть детальную информацию о работе модуля.

### Частые ошибки

1. **"upstream not found"**
   - Проверьте правильность имени upstream
   - Убедитесь, что upstream блок существует в конфигурации

2. **"upstream does not have a zone directive"**
   - Добавьте директиву `zone` в upstream блок
   - Без zone динамическое управление невозможно

3. **"server already exists"**
   - Сервер с таким адресом уже существует в upstream
   - Используйте `update` вместо `add` для изменения параметров

4. **"can't create directory"**
   - Проверьте права доступа
   - Убедитесь, что родительская директория существует

## Лицензия

BSD-3-Clause (та же, что и у оригинального модуля)

## Благодарности

Модуль основан на [ngx_dynamic_healthcheck](https://github.com/ZigzagAK/ngx_dynamic_healthcheck) от Aleksei Konovkin.

## Поддержка

При возникновении проблем:

1. Проверьте логи Nginx: `/var/log/nginx/error.log`
2. Включите отладочное логирование для детальной информации
3. Убедитесь, что все требования выполнены
4. Проверьте конфигурацию на синтаксические ошибки: `nginx -t`

---

**Версия документации**: 1.0  
**Последнее обновление**: 2024
