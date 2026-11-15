#include "web_service.h"
#include <LittleFS.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <lwip/sockets.h>
#include <psa/crypto.h>
#include <cstring>
#include <new>
#include "image_check.h"

#ifndef FW_VERSION
#define FW_VERSION "0.0"
#endif
namespace hw {
namespace {
class Lock {
public:
    explicit Lock(SemaphoreHandle_t mutex) : mutex_(mutex) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
    }
    ~Lock() { xSemaphoreGive(mutex_); }

private:
    SemaphoreHandle_t mutex_;
};
// Every byte a connection sends passes the header gate before the HTTP server parses it. A
// chunk that breaks the rules fails the receive, so the server closes the connection before
// any handler runs.
int guardedReceive(httpd_handle_t server, int fd, char *buffer, size_t len, int flags) {
    auto *gate = static_cast<webinterface::HeaderGate *>(httpd_sess_get_transport_ctx(server, fd));
    if (!gate) {
        return HTTPD_SOCK_ERR_FAIL;
    }
    int n = recv(fd, buffer, len, flags);
    if (n < 0) {
        return errno == EAGAIN || errno == EWOULDBLOCK ? HTTPD_SOCK_ERR_TIMEOUT
                                                       : HTTPD_SOCK_ERR_FAIL;
    }
    if (n > 0 && !(flags & MSG_PEEK) &&
        !gate->accept(reinterpret_cast<const uint8_t *>(buffer), size_t(n))) {
        return HTTPD_SOCK_ERR_FAIL;
    }
    return n;
}
esp_err_t openSocket(httpd_handle_t server, int fd) {
    auto *gate = new (std::nothrow) webinterface::HeaderGate;
    if (!gate) {
        return ESP_FAIL;
    }
    httpd_sess_set_transport_ctx(
        server, fd, gate, [](void *p) { delete static_cast<webinterface::HeaderGate *>(p); });
    // The server writes a response in several small pieces (status line, headers, body) and
    // WebSocket frames are small too. Without this, each piece after the first waits for the
    // browser's delayed ACK (about 50 ms on Windows).
    const int yes = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof yes);
    return httpd_sess_set_recv_override(server, fd, guardedReceive);
}
std::string header(httpd_req_t *req, const char *key, size_t max = 900) {
    size_t n = httpd_req_get_hdr_value_len(req, key);
    if (!n || n > max) {
        return "";
    }
    std::string value(n + 1, '\0');
    if (httpd_req_get_hdr_value_str(req, key, &value[0], value.size()) != ESP_OK) {
        return "";
    }
    value.resize(n);
    return value;
}
std::string query(httpd_req_t *req, const char *key) {
    char all[256] = {}, value[80] = {};
    if (httpd_req_get_url_query_str(req, all, sizeof all) != ESP_OK ||
        httpd_query_key_value(all, key, value, sizeof value) != ESP_OK) {
        return "";
    }
    return value;
}
void headers(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    // One request per connection (the header gate refuses a second one). The client closes its
    // end after this response and the server then frees the slot. The server is not asked to
    // close: a close queued from here names the slot, not the connection, so after a burst of
    // requests it closes whichever connection took the slot next.
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
    httpd_resp_set_hdr(req, "Content-Security-Policy",
                       "default-src 'self'; script-src 'self'; connect-src 'self'; "
                       "style-src 'self' 'unsafe-inline'; font-src 'self'; img-src 'self' data:; "
                       "frame-ancestors 'none'; base-uri 'none'; form-action 'self'");
}
esp_err_t reply(httpd_req_t *req, const char *status, const char *text) {
    headers(req);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_send(req, text, HTTPD_RESP_USE_STRLEN);
}
std::string hexBytes(const uint8_t *data, size_t len) {
    const char *digits = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out += digits[data[i] >> 4];
        out += digits[data[i] & 15];
    }
    return out;
}
// Filesystem uploads are first staged in the unused app slot. A record is a
// uint32 length, high bit set for all-FF runs, otherwise followed by literal data.
// Nothing selects this nonbootable staging data as an app. The live filesystem
// is untouched until the entire raw image's identity and SHA-256 are verified.
bool stage(const esp_partition_t *partition, size_t &offset, const uint8_t *data, size_t len) {
    bool blank = true;
    for (size_t i = 0; i < len; ++i) {
        blank = blank && data[i] == 0xff;
    }
    uint32_t record = uint32_t(len) | (blank ? 0x80000000u : 0);
    size_t needed = 4 + (blank ? 0 : len);
    if (offset + needed > partition->size ||
        esp_partition_write(partition, offset, &record, 4) != ESP_OK ||
        (!blank && esp_partition_write(partition, offset + 4, data, len) != ESP_OK)) {
        return false;
    }
    offset += needed;
    return true;
}
}  // namespace
bool WebCrypto::random(uint8_t *out, size_t len) {
    // Called only while the Wi-Fi radio is running.
    esp_fill_random(out, len);
    return true;
}
bool WebCrypto::md5(const std::string &text, char out[33]) {
    uint8_t bytes[16] = {};
    size_t length = 0;
    bool ok = psa_hash_compute(PSA_ALG_MD5, reinterpret_cast<const uint8_t *>(text.data()),
                               text.size(), bytes, sizeof bytes, &length) == PSA_SUCCESS;
    std::string encoded = hexBytes(bytes, sizeof bytes);
    memcpy(out, encoded.c_str(), 33);
    devicecfg::wipe(bytes, sizeof bytes);
    return ok;
}
WebService::WebService(devicecfg::Device &device, const netconsole::ConsoleLog &console)
    : device_(device),
      console_(console),
      auth_(crypto_),
      tickets_(auth_),
      mutex_(xSemaphoreCreateMutex()) {}
WebService::Client *WebService::find(uint32_t id) {
    for (auto &c : clients_) {
        if (c.active && c.id == id) {
            return &c;
        }
    }
    return nullptr;
}
void WebService::disconnect(uint32_t id) {
    Lock lock(mutex_);
    auto *c = find(id);
    if (c) {
        terminalOwner_.close(id);
        c->active = false;
        c->input.clear();
        c->output.clear();
        c->csrf.clear();
    }
}
void WebService::close(uint32_t id) {
    Lock lock(mutex_);
    auto *c = find(id);
    if (c) {
        c->close = true;
        c->input.clear();
        c->output.clear();
        terminalOwner_.close(id);
    }
}
void WebService::freeContext(void *value) {
    auto *ctx = static_cast<Context *>(value);
    ctx->self->disconnect(ctx->id);
    delete ctx;
}
void WebService::stop() {
    {
        Lock lock(mutex_);
        for (auto &c : clients_) {
            terminalOwner_.close(c.id);
            c.active = false;
            c.input.clear();
            c.output.clear();
            c.csrf.clear();
        }
        auth_.reset();
        tickets_.reset();
    }
    // Release a receive handler waiting for modem space before waiting for the
    // HTTP task itself. That task must never need progress from this loop to stop.
    if (server_) {
        httpd_stop(server_);
        server_ = nullptr;
    }
    Lock lock(mutex_);
    workQueued_ = false;
}
void WebService::poll(bool enabled, bool up, const char *name, const char *ip) {
    bool mark;
    {
        Lock lock(mutex_);
        mark = needUsed_;
        needUsed_ = false;
    }
    if (mark) {
        bool ok = device_.markUsed();
        Lock lock(mutex_);
        usedAck_ = ok;
    }
    bool changed = name_ != name || ip_ != ip;
    if (hash_ != device_.digestHash()) {
        Lock lock(mutex_);
        hash_ = device_.digestHash();
        auth_.reset();
        tickets_.reset();
        usedAck_ = false;
        // Send the requesting terminal's generated password before closing its
        // authenticated connection. New requests already require the new hash.
        for (auto &c : clients_)
            if (c.active) {
                c.close = true;
                terminalOwner_.close(c.id);
                c.input.clear();
            }
    }
    if (uploading()) {
        return;
    }
    if (!enabled || !up || !device_.ready() || changed) {
        stop();
        Lock lock(mutex_);
        name_ = name;
        ip_ = ip;
        hash_ = device_.digestHash();
        if (changed) {
            usedAck_ = false;
        }
    }
    if (!enabled || !up || !device_.ready() || server_) {
        return;
    }
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.core_id = 0;
    cfg.open_fn = openSocket;
    cfg.global_user_ctx = this;
    cfg.global_user_ctx_free_fn = [](void *) {};  // borrowed, static lifetime
    cfg.stack_size = 12288;
    cfg.max_req_hdr_len = webinterface::kMaxHeader;  // the header gate's limit
    cfg.max_open_sockets = 12;  // 3 browsers + a page load (about 8 connections)
    cfg.lru_purge_enable = true;
    cfg.recv_wait_timeout = 1;
    cfg.send_wait_timeout = 1;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    if (httpd_start(&server_, &cfg) != ESP_OK) {
        server_ = nullptr;
        return;
    }
    httpd_uri_t route = {};
    route.uri = "/ws";
    route.method = HTTP_GET;
    route.handler = websocket;
    route.user_ctx = this;
    route.is_websocket = true;
    route.handle_ws_control_frames = true;
    route.ws_pre_handshake_cb = upgrade;
    httpd_register_uri_handler(server_, &route);
    route = {};
    route.uri = "/api/update";
    route.method = HTTP_POST;
    route.handler = update;
    route.user_ctx = this;
    httpd_register_uri_handler(server_, &route);
    route.uri = "/*";
    route.method = HTTP_GET;
    route.handler = get;
    httpd_register_uri_handler(server_, &route);
}
bool WebService::host(httpd_req_t *req, bool origin) {
    std::string authority = header(req, "Host", 80);
    std::string source = header(req, "Origin", 90);
    Lock lock(mutex_);
    return webinterface::allowedHost(authority, name_, ip_) &&
           (!origin || webinterface::allowedOrigin(source, name_, ip_));
}
bool WebService::used() {
    {
        Lock lock(mutex_);
        if (usedAck_) {
            return true;
        }
        needUsed_ = true;
    }
    uint32_t start = millis();
    while (millis() - start < 2000) {
        {
            Lock lock(mutex_);
            if (usedAck_) {
                return true;
            }
        }
        delay(2);
    }
    return false;
}
bool WebService::authorized(httpd_req_t *req) {
    if (!host(req, false)) {
        reply(req, "403 Forbidden", "Invalid modem host.");
        return false;
    }
    std::string value = header(req, "Authorization");
    webinterface::Check result;
    std::string challenge;
    {
        Lock lock(mutex_);
        result = auth_.check(value, req->method == HTTP_POST ? "POST" : "GET", req->uri,
                             hash_.c_str(), millis());
        if (result != webinterface::Check::kOk) {
            challenge = auth_.challenge(millis(), result == webinterface::Check::kStale);
        }
    }
    if (result != webinterface::Check::kOk) {
        httpd_resp_set_hdr(req, "WWW-Authenticate", challenge.c_str());
        reply(req, "401 Unauthorized", "Log in as admin with the device password.");
        return false;
    }
    if (!used()) {
        reply(req, "503 Service Unavailable", "Unable to record authenticated use.");
        return false;
    }
    return true;
}
esp_err_t WebService::get(httpd_req_t *req) {
    return static_cast<WebService *>(req->user_ctx)->serve(req);
}
esp_err_t WebService::serve(httpd_req_t *req) {
    if (!authorized(req)) {
        return ESP_OK;
    }
    headers(req);
    std::string path = req->uri;
    path.resize(path.find('?') == std::string::npos ? path.size() : path.find('?'));
    if (path == "/api/boot") {
        httpd_resp_set_type(req, "text/plain; charset=utf-8");
        // Each read holds the log's lock only while copying. Never hold the spinlock
        // during a socket write, and stop at the captured end even if output continues.
        const uint64_t end = console_.end();
        uint64_t from = end > console_.capacity() ? end - console_.capacity() : 0;
        uint8_t buffer[512];
        while (from < end) {
            uint64_t skipped;
            size_t n = console_.read(from, buffer, sizeof buffer, skipped);
            const uint64_t start = from - n;
            if (!n || start >= end)
                break;
            if (n > end - start)
                n = static_cast<size_t>(end - start);
            esp_err_t result = httpd_resp_send_chunk(req, reinterpret_cast<char *>(buffer), n);
            devicecfg::wipe(buffer, sizeof buffer);
            if (result != ESP_OK)
                return result;
        }
        devicecfg::wipe(buffer, sizeof buffer);
        return httpd_resp_send_chunk(req, nullptr, 0);
    }
    if (path == "/api/session") {
        std::string ticket;
        {
            Lock lock(mutex_);
            ticket = tickets_.issue(millis());
        }
        httpd_resp_set_type(req, "application/json");
        std::string body = "{\"token\":\"" + ticket + "\"}";
        return httpd_resp_send(req, body.c_str(), body.size());
    }
    struct Asset {
        const char *path;
        const char *mime;
    };
    static const Asset assets[] = {{"/index.html", "text/html; charset=utf-8"},
                                   {"/usr-logo.png", "image/png"},
                                   {"/menubar.css", "text/css"},
                                   {"/menubar.js", "text/javascript"},
                                   {"/menu-view.js", "text/javascript"},
                                   {"/command-channel.js", "text/javascript"},
                                   {"/style.css", "text/css"},
                                   {"/app.js", "text/javascript"},
                                   {"/status.js", "text/javascript"},
                                   {"/demo.js", "text/javascript"},
                                   {"/upload.js", "text/javascript"},
                                   {"/upload_flow.js", "text/javascript"},
                                   {"/link.js", "text/javascript"},
                                   {"/geometry.js", "text/javascript"},
                                   {"/idle.js", "text/javascript"},
                                   {"/toast.js", "text/javascript"},
                                   {"/ui-data.js", "text/javascript"},
                                   {"/ui-commands.js", "text/javascript"},
                                   {"/xterm.mjs", "text/javascript"},
                                   {"/xterm.css", "text/css"},
                                   {"/ibm-plex-mono-400.woff2", "font/woff2"},
                                   {"/ibm-plex-mono-700.woff2", "font/woff2"}};
    if (path == "/") {
        path = "/index.html";
    }
    for (const auto &asset : assets) {
        if (path != asset.path) {
            continue;
        }
        if (uploading()) {
            return reply(req, "503 Service Unavailable", "Update in progress.");
        }
        File file = LittleFS.open(("/www" + path + ".gz").c_str(), "r");
        if (!file) {
            return reply(req, "503 Service Unavailable",
                         "Web files unavailable. Restore littlefs.bin using USB uploadfs.");
        }
        httpd_resp_set_type(req, asset.mime);
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
        char buffer[1024];
        while (file.available()) {
            size_t n = file.readBytes(buffer, sizeof buffer);
            if (httpd_resp_send_chunk(req, buffer, n) != ESP_OK) {
                return ESP_FAIL;
            }
        }
        return httpd_resp_send_chunk(req, nullptr, 0);
    }
    return reply(req, "404 Not Found", "Not found.");
}
esp_err_t WebService::websocket(httpd_req_t *req) {
    return static_cast<WebService *>(req->user_ctx)->ws(req);
}
// Runs before the HTTP server answers a WebSocket upgrade (the server then never calls the
// route's handler for that GET). A refused upgrade gets 403 and the connection closes.
esp_err_t WebService::upgrade(httpd_req_t *req) {
    auto *self = static_cast<WebService *>(req->user_ctx);
    const uint32_t id = self->prepareUpgrade(httpd_req_to_sockfd(req), header(req, "Host"),
                                             header(req, "Origin"), req->uri);
    if (id) {
        auto *context = new (std::nothrow) Context{self, id};
        if (context) {
            req->sess_ctx = context;  // the frame handler's link to the client; freed on close
            req->free_ctx = freeContext;
            return ESP_OK;
        }
        self->disconnect(id);
    }
    headers(req);
    httpd_resp_set_status(req, "403 Forbidden");
    httpd_resp_send(req, nullptr, 0);
    return ESP_FAIL;
}
esp_err_t WebService::ws(httpd_req_t *req) {
    auto *context = static_cast<Context *>(req->sess_ctx);
    if (!context) {
        return ESP_FAIL;
    }
    httpd_ws_frame_t frame = {};
    if (httpd_ws_recv_frame(req, &frame, 0) != ESP_OK ||
        frame.len > webinterface::kTerminalFrameBytes) {
        return ESP_FAIL;
    }
    uint8_t data[512];
    frame.payload = data;
    // Receive binary frames in bounded pieces. While the modem holds input off,
    // keep sending output here: queued HTTP work cannot run inside this handler.
    if (frame.type == HTTPD_WS_TYPE_BINARY || frame.type == HTTPD_WS_TYPE_CONTINUE) {
        {
            Lock lock(mutex_);
            auto *c = find(context->id);
            if (!c || (c->fragmented ? frame.type != HTTPD_WS_TYPE_CONTINUE
                                     : frame.type != HTTPD_WS_TYPE_BINARY))
                return ESP_FAIL;
            c->fragmented = !frame.final;
        }
        while (frame.left_len) {
            size_t n = frame.left_len < sizeof data ? frame.left_len : sizeof data;
            if (httpd_ws_recv_frame_part(req, &frame, n) != ESP_OK) {
                devicecfg::wipe(data, sizeof data);
                return ESP_FAIL;
            }
            size_t offset = 0;
            uint32_t progress = millis();
            while (offset < n) {
                {
                    Lock lock(mutex_);
                    auto *c = find(context->id);
                    if (!c) {
                        devicecfg::wipe(data, sizeof data);
                        return ESP_FAIL;
                    }
                    if (!terminalOwner_.owns(c->id)) {
                        offset = n;  // observers never queue commands
                    } else {
                        size_t used = c->input.append(data + offset, n - offset);
                        offset += used;
                        if (used)
                            progress = millis();
                    }
                }
                sendClients();
                if (offset < n) {
                    if (millis() - progress >= 30000) {
                        devicecfg::wipe(data, sizeof data);
                        return ESP_FAIL;
                    }
                    delay(1);
                }
            }
            devicecfg::wipe(data, sizeof data);
        }
        return ESP_OK;
    }
    if (frame.len > sizeof data)
        return ESP_FAIL;
    if (httpd_ws_recv_frame(req, &frame, sizeof data) != ESP_OK) {
        devicecfg::wipe(data, sizeof data);
        return ESP_FAIL;
    }
    if (frame.type == HTTPD_WS_TYPE_PING) {
        if (!frame.final || frame.len > 125) {
            return ESP_FAIL;
        }
        frame.type = HTTPD_WS_TYPE_PONG;
        return httpd_ws_send_frame(req, &frame);
    }
    if (frame.type == HTTPD_WS_TYPE_PONG) {
        return ESP_OK;
    }
    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        if (!frame.final || frame.len > 125) {
            return ESP_FAIL;
        }
        httpd_ws_send_frame(req, &frame);
        return ESP_FAIL;
    }
    if (frame.type == HTTPD_WS_TYPE_TEXT && frame.final && frame.len == 12 &&
        memcmp(data, "{\"t\":\"ping\"}", 12) == 0) {
        static const char pong[] = "{\"t\":\"pong\"}";
        frame.payload = reinterpret_cast<uint8_t *>(const_cast<char *>(pong));
        frame.len = sizeof pong - 1;
        return httpd_ws_send_frame(req, &frame);
    }
    Lock lock(mutex_);
    auto *c = find(context->id);
    if (c && !c->fragmented && frame.type == HTTPD_WS_TYPE_TEXT && frame.final) {
        std::string message(reinterpret_cast<const char *>(data), frame.len);
        if (message == "{\"t\":\"open\"}") {
            c->openRequested = true;
            return ESP_OK;
        }
        if (message == "{\"t\":\"hangup\"}") {
            terminalOwner_.requestHangup(c->id);
            return ESP_OK;
        }
        unsigned columns, rows;
        if (!webinterface::terminalSize(message, columns, rows))
            return ESP_FAIL;
        c->columns = columns;
        c->rows = rows;
        c->sizeChanged = true;
        return ESP_OK;
    }
    devicecfg::wipe(data, sizeof data);
    return ESP_FAIL;
}
uint32_t WebService::prepareUpgrade(int fd, const std::string &host, const std::string &origin,
                                    const std::string &target) {
    Lock lock(mutex_);
    const std::string prefix = "/ws?token=";
    if (!webinterface::allowedHost(host, name_, ip_) ||
        !webinterface::allowedOrigin(origin, name_, ip_) ||
        target.compare(0, prefix.size(), prefix) != 0 || target.size() != prefix.size() + 32) {
        return 0;
    }
    Client *available = nullptr;
    for (auto &c : clients_) {
        if (!c.active) {
            available = &c;
            break;
        }
    }
    if (!available || !tickets_.consume(target.substr(prefix.size()), millis())) {
        return 0;
    }
    char csrf[33];
    if (!auth_.token(csrf)) {
        return 0;
    }
    available->input.clear();
    available->output.clear();
    available->fragmented = available->close = false;
    available->openRequested = false;
    available->columns = 80;
    available->rows = 25;
    available->sizeChanged = true;
    for (auto &m : available->messages) {
        m.clear();
    }
    for (auto &v : available->sent) {
        v = 0;
    }
    available->fd = fd;
    available->id = nextId_++;
    available->active = true;
    available->csrf = csrf;
    available->messages[4] =
        "{\"t\":\"hello\",\"name\":" + webinterface::jsonString(name_.c_str()) +
        ",\"ver\":\"" FW_VERSION "\",\"csrf\":\"" + csrf + "\"}";
    return available->id;
}
uint32_t WebService::clientId(unsigned index) {
    Lock lock(mutex_);
    return index < kClients && clients_[index].active && !clients_[index].close ? clients_[index].id
                                                                                : 0;
}
bool WebService::connected(uint32_t id) {
    Lock lock(mutex_);
    auto *c = find(id);
    return c && !c->close;
}
size_t WebService::read(uint32_t id, uint8_t *data, size_t cap) {
    Lock lock(mutex_);
    auto *c = find(id);
    return c ? c->input.pop(data, cap) : 0;
}
bool WebService::takeSize(uint32_t id, unsigned &columns, unsigned &rows) {
    Lock lock(mutex_);
    auto *c = find(id);
    if (!c || !c->sizeChanged)
        return false;
    columns = c->columns;
    rows = c->rows;
    c->sizeChanged = false;
    return true;
}
bool WebService::takeOpen(uint32_t id) {
    Lock lock(mutex_);
    auto *c = find(id);
    if (!c || c->close || !c->openRequested)
        return false;
    c->openRequested = false;
    return true;
}
bool WebService::takeHangup(uint32_t id) {
    Lock lock(mutex_);
    auto *c = find(id);
    if (!c || c->close || !terminalOwner_.takeHangup(id))
        return false;
    c->input.clear();
    return true;
}
void WebService::terminal(uint32_t id, bool enabled) {
    Lock lock(mutex_);
    auto *c = find(id);
    if (!c)
        return;
    if (enabled)
        enabled = terminalOwner_.open(id, true);
    else
        terminalOwner_.close(id);
    c->input.clear();
    c->messages[5] = enabled ? "{\"t\":\"access\",\"edit\":true,\"state\":\"terminal\"}"
                             : "{\"t\":\"access\",\"edit\":false,\"state\":\"read-only\"}";
}
void WebService::discardInput(uint32_t id) {
    Lock lock(mutex_);
    auto *c = find(id);
    if (c) {
        c->input.clear();
    }
}
bool WebService::write(uint32_t id, const uint8_t *data, size_t len) {
    Lock lock(mutex_);
    auto *c = find(id);
    if (!c || c->close) {
        return false;
    }
    return c->output.push(data, len);
}
size_t WebService::space(uint32_t id) {
    Lock lock(mutex_);
    auto *c = find(id);
    return c && !c->close ? c->output.space() : 0;
}
void WebService::text(uint32_t id, const std::string &message) {
    Lock lock(mutex_);
    auto *c = find(id);
    if (c) {
        c->messages[5] = message;
    }
}
void WebService::commandState(uint32_t id, const std::string &message) {
    Lock lock(mutex_);
    auto *c = find(id);
    if (c) {
        c->messages[6] = message;
    }
}
void WebService::snapshot(unsigned type, const std::string &message, bool force) {
    Lock lock(mutex_);
    if (type < 4) {
        if (force || snapshots_[type] != message) {
            ++revision_[type];
        }
        snapshots_[type] = message;
    }
}
void WebService::sendWork(void *ctx) {
    auto &self = *static_cast<WebService *>(ctx);
    self.sendClients();
    Lock lock(self.mutex_);
    self.workQueued_ = false;
}
void WebService::sendClients() {
    auto &self = *this;
    for (unsigned i = 0; i < kClients; ++i) {
        int fd;
        uint32_t id;
        bool close;
        uint8_t bytes[512];
        size_t n;
        std::string messages[7];
        {
            Lock lock(self.mutex_);
            auto &c = self.clients_[i];
            if (!c.active) {
                continue;
            }
            fd = c.fd;
            id = c.id;
            close = c.close && c.output.size() == 0;
            n = c.output.pop(bytes, sizeof bytes);
            for (unsigned j = 0; j < 4; ++j) {
                if (c.sent[j] != self.revision_[j]) {
                    c.sent[j] = self.revision_[j];
                    c.messages[j] = self.snapshots_[j];
                }
            }
            for (unsigned j = 0; j < 6; ++j) {
                messages[j].swap(c.messages[j]);
            }
            if (c.output.size() == 0) {
                messages[6].swap(c.messages[6]);
            }
        }
        // hello precedes the initial snapshots and terminal bytes.
        for (unsigned j : {4u, 0u, 1u, 2u, 3u, 5u}) {
            if (messages[j].empty() || close) {
                continue;
            }
            httpd_ws_frame_t frame = {};
            frame.type = HTTPD_WS_TYPE_TEXT;
            frame.final = true;
            frame.payload = reinterpret_cast<uint8_t *>(&messages[j][0]);
            frame.len = messages[j].size();
            close = httpd_ws_send_frame_async(self.server_, fd, &frame) != ESP_OK;
        }
        if (n && !close) {
            httpd_ws_frame_t frame = {};
            frame.type = HTTPD_WS_TYPE_BINARY;
            frame.final = true;
            frame.payload = bytes;
            frame.len = n;
            close = httpd_ws_send_frame_async(self.server_, fd, &frame) != ESP_OK;
        }
        if (!messages[6].empty() && !close) {
            httpd_ws_frame_t frame = {};
            frame.type = HTTPD_WS_TYPE_TEXT;
            frame.final = true;
            frame.payload = reinterpret_cast<uint8_t *>(&messages[6][0]);
            frame.len = messages[6].size();
            close = httpd_ws_send_frame_async(self.server_, fd, &frame) != ESP_OK;
        }
        devicecfg::wipe(bytes, sizeof bytes);
        if (close) {
            self.disconnect(id);
            httpd_sess_trigger_close(self.server_, fd);
        }
    }
}
void WebService::pump() {
    if (!server_ || millis() - nextPump_ < 20) {
        return;
    }
    nextPump_ = millis();
    Lock lock(mutex_);
    if (!workQueued_ && httpd_queue_work(server_, sendWork, this) == ESP_OK) {
        workQueued_ = true;
    }
}
esp_err_t WebService::update(httpd_req_t *req) {
    return static_cast<WebService *>(req->user_ctx)->upload(req);
}
bool WebService::takeUploadRequest(uint32_t &client) {
    Lock lock(mutex_);
    return upload_.takeRequest(client);
}
bool WebService::allowUpload(bool allow) {
    Lock lock(mutex_);
    return upload_.allow(allow);
}
bool WebService::uploading() {
    Lock lock(mutex_);
    return upload_.uploading();
}
bool WebService::takeUploadDone(bool &ok, bool &filesystem) {
    Lock lock(mutex_);
    return upload_.takeDone(ok, filesystem);
}
bool WebService::takeUnmountRequest() {
    Lock lock(mutex_);
    return upload_.takeUnmount();
}
void WebService::unmounted() {
    Lock lock(mutex_);
    upload_.unmounted();
}
esp_err_t WebService::upload(httpd_req_t *req) {
    if (!authorized(req)) {
        return ESP_FAIL;  // close without reading an unauthenticated body
    }
    if (!host(req, true) || header(req, "Content-Type") != "application/octet-stream") {
        return reply(req, "403 Forbidden", "Invalid upload origin or content type.");
    }
    std::string target = query(req, "target"), version = query(req, "version");
    std::string expected = header(req, "X-Image-SHA256", 64);
    bool fs = target == "littlefs";
    bool hashValid = expected.size() == 64;
    for (char c : expected) {
        hashValid = hashValid && ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }
    int comparison = webinterface::compareVersions(version, FW_VERSION);
    if ((!fs && target != "firmware") || !hashValid || comparison == -2 || req->content_len < 24 ||
        req->content_len > (fs ? 0x7e0000u : 0x400000u) || (fs && req->content_len != 0x7e0000u)) {
        return reply(req, "400 Bad Request", "Invalid image metadata or length.");
    }
    if ((comparison < 0 && header(req, "X-Confirm-Downgrade") != expected) ||
        (fs && header(req, "X-Confirm-Filesystem") != expected)) {
        return reply(req, "409 Conflict",
                     "Confirm this image's downgrade or filesystem replacement.");
    }
    std::string csrf = header(req, "X-CSRF-Token", 32);
    {
        Lock lock(mutex_);
        Client *owner = nullptr;
        for (auto &c : clients_) {
            if (c.active && webinterface::equalSecret(c.csrf, csrf)) {
                owner = &c;
                break;
            }
        }
        if (!owner || !upload_.ask(owner->id)) {
            return reply(req, "403 Forbidden", "Invalid CSRF session or update already active.");
        }
    }
    // Wait up to 2 s for the modem loop. The final check and the give-up happen under
    // one lock, so a late allowUpload() finds nobody waiting and changes nothing.
    uint32_t start = millis();
    bool granted = false;
    for (;;) {
        bool timedOut = millis() - start >= 2000;
        {
            Lock lock(mutex_);
            if (upload_.answered()) {
                granted = upload_.granted();
                break;
            }
            if (timedOut) {
                upload_.giveUp();
                break;
            }
        }
        delay(2);
    }
    if (!granted) {
        return reply(req, "409 Conflict", "Modem busy. Exit other menus, calls and tests first.");
    }
    // All Update and partition writes are serialized by the modem's reservation.
    webinterface::ImageCheck check(fs, version, req->content_len);
    const esp_partition_t *staging = esp_ota_get_next_update_partition(nullptr);
    bool ok = fs ? staging && esp_partition_erase_range(staging, 0, staging->size) == ESP_OK
                 : Update.begin(req->content_len, U_FLASH);
    psa_hash_operation_t sha = PSA_HASH_OPERATION_INIT;
    ok = psa_hash_setup(&sha, PSA_ALG_SHA_256) == PSA_SUCCESS && ok;
    uint8_t buffer[1024];
    size_t received = 0, staged = 0;
    while (ok && received < req->content_len && millis() - start < 180000) {
        size_t wanted = req->content_len - received;
        if (wanted > sizeof buffer) {
            wanted = sizeof buffer;
        }
        int n = httpd_req_recv(req, reinterpret_cast<char *>(buffer), wanted);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;  // recv_wait_timeout is 1 s; a slow sender has the 180 s limit
        }
        if (n <= 0) {
            ok = false;
            break;
        }
        ok = check.write(buffer, size_t(n)) &&
             psa_hash_update(&sha, buffer, size_t(n)) == PSA_SUCCESS;
        if (ok) {
            ok = fs ? stage(staging, staged, buffer, size_t(n))
                    : Update.write(buffer, size_t(n)) == size_t(n);
        }
        received += size_t(n);
    }
    uint8_t digest[32] = {};
    size_t digestLength = 0;
    ok = psa_hash_finish(&sha, digest, sizeof digest, &digestLength) == PSA_SUCCESS &&
         check.complete() && ok &&
         webinterface::equalSecret(hexBytes(digest, sizeof digest), expected);
    psa_hash_abort(&sha);
    if (fs && ok) {
        {
            Lock lock(mutex_);
            upload_.askUnmount();
        }
        // Same rule as above: a late unmounted() is ignored, and a request the loop has
        // not taken yet is withdrawn, so it can't unmount LittleFS after this fails.
        uint32_t wait = millis();
        bool ready = false;
        for (;;) {
            bool timedOut = millis() - wait >= 2000;
            {
                Lock lock(mutex_);
                ready = upload_.unmountReady();
                if (ready || timedOut) {
                    if (!ready) {
                        upload_.giveUpUnmount();
                    }
                    break;
                }
            }
            delay(2);
        }
        ok = ready && Update.begin(req->content_len, U_SPIFFS);
        size_t offset = 0, written = 0;
        while (ok && offset < staged) {
            uint32_t record = 0;
            ok = esp_partition_read(staging, offset, &record, 4) == ESP_OK;
            offset += 4;
            size_t n = record & 0x7fffffffu;
            if (!n || n > sizeof buffer || written + n > req->content_len) {
                ok = false;
                break;
            }
            if (record & 0x80000000u) {
                memset(buffer, 0xff, n);
            } else {
                ok = esp_partition_read(staging, offset, buffer, n) == ESP_OK && ok;
                offset += n;
            }
            ok = ok && Update.write(buffer, n) == n;
            written += n;
            delay(1);
        }
        ok = ok && written == req->content_len;
    }
    ok = ok && Update.end(false);
    if (!ok) {
        Update.abort();
    }
    devicecfg::wipe(buffer, sizeof buffer);
    reply(req, ok ? "200 OK" : "400 Bad Request",
          ok ? "Update complete. Rebooting."
             : "Image rejected or update failed. USB recovery remains available.");
    {
        Lock lock(mutex_);
        upload_.finish(ok, fs);
    }
    return ok ? ESP_OK : ESP_FAIL;
}
}  // namespace hw
