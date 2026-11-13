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
struct Transport {
    webinterface::HeaderGate gate;
    WebService *self;
    uint32_t id = 0;
    size_t received = 0, pendingAt = 0;
    std::string pending;
};
int guardedReceive(httpd_handle_t server, int fd, char *buffer, size_t len, int flags) {
    auto *transport = static_cast<Transport *>(httpd_sess_get_transport_ctx(server, fd));
    if (!transport) {
        return HTTPD_SOCK_ERR_FAIL;
    }
    while (!transport->gate.complete()) {
        uint8_t raw[512];
        int n = recv(fd, raw, sizeof raw, flags & ~MSG_PEEK);
        if (n <= 0) {
            return n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) ? HTTPD_SOCK_ERR_TIMEOUT
                                                                      : HTTPD_SOCK_ERR_FAIL;
        }
        size_t before = transport->received;
        transport->received += size_t(n);
        if (!transport->gate.accept(raw, size_t(n))) {
            return HTTPD_SOCK_ERR_FAIL;
        }
        if (!transport->gate.complete()) {
            continue;
        }
        if (transport->gate.websocket()) {
            transport->id = transport->self->prepareUpgrade(fd, transport->gate);
            if (!transport->id) {
                static const char denied[] =
                    "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                send(fd, denied, sizeof denied - 1, 0);
                return HTTPD_SOCK_ERR_FAIL;
            }
        }
        // IDF 4.4 has a fixed 1 KiB parser header buffer. Browser uploads can
        // exceed it through User-Agent/Accept/Sec-Fetch headers. Validate the
        // complete bounded raw header first, then retain only protocol fields.
        transport->pending = transport->gate.filtered();
        size_t body = transport->gate.headerLength() - before;
        transport->pending.append(reinterpret_cast<const char *>(raw + body), size_t(n) - body);
        devicecfg::wipe(raw, sizeof raw);
    }
    if (transport->pendingAt < transport->pending.size()) {
        size_t n = transport->pending.size() - transport->pendingAt;
        if (n > len) {
            n = len;
        }
        memcpy(buffer, transport->pending.data() + transport->pendingAt, n);
        if (!(flags & MSG_PEEK)) {
            transport->pendingAt += n;
            if (transport->pendingAt == transport->pending.size()) {
                devicecfg::wipe(&transport->pending[0], transport->pending.size());
                transport->pending.clear();
                transport->pendingAt = 0;
            }
        }
        return int(n);
    }
    int n = recv(fd, buffer, len, flags);
    if (n < 0) {
        return errno == EAGAIN || errno == EWOULDBLOCK ? HTTPD_SOCK_ERR_TIMEOUT
                                                       : HTTPD_SOCK_ERR_FAIL;
    }
    if (n > 0 && !(flags & MSG_PEEK) &&
        !transport->gate.accept(reinterpret_cast<const uint8_t *>(buffer), size_t(n))) {
        return HTTPD_SOCK_ERR_FAIL;
    }
    return n;
}
esp_err_t openSocket(httpd_handle_t server, int fd) {
    auto *transport = new (std::nothrow) Transport;
    if (!transport) {
        return ESP_FAIL;
    }
    transport->self = static_cast<WebService *>(httpd_get_global_user_ctx(server));
    httpd_sess_set_transport_ctx(server, fd, transport, [](void *p) {
        auto *ctx = static_cast<Transport *>(p);
        ctx->self->disconnect(ctx->id);
        delete ctx;
    });
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
WebService::WebService(devicecfg::Device &device)
    : device_(device), auth_(crypto_), tickets_(auth_), mutex_(xSemaphoreCreateMutex()) {}
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
        c->active = false;
        c->input.clear();
        c->output.clear();
        c->csrf.clear();
    }
}
void WebService::freeContext(void *value) {
    auto *ctx = static_cast<Context *>(value);
    ctx->self->disconnect(ctx->id);
    delete ctx;
}
void WebService::stop() {
    if (server_) {
        httpd_stop(server_);
        server_ = nullptr;
    }
    Lock lock(mutex_);
    for (auto &c : clients_) {
        c.active = false;
        c.input.clear();
        c.output.clear();
        c.csrf.clear();
    }
    auth_.reset();
    tickets_.reset();
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
    bool changed = hash_ != device_.digestHash() || name_ != name || ip_ != ip;
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
    cfg.max_open_sockets = 6;
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
                                   {"/style.css", "text/css"},
                                   {"/app.js", "text/javascript"},
                                   {"/status.js", "text/javascript"},
                                   {"/demo.js", "text/javascript"},
                                   {"/upload.js", "text/javascript"},
                                   {"/upload_flow.js", "text/javascript"},
                                   {"/link.js", "text/javascript"},
                                   {"/geometry.js", "text/javascript"},
                                   {"/ui-data.js", "text/javascript"},
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
esp_err_t WebService::ws(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        auto *transport = static_cast<Transport *>(
            httpd_sess_get_transport_ctx(req->handle, httpd_req_to_sockfd(req)));
        if (!transport || !transport->id) {
            return ESP_FAIL;
        }
        req->sess_ctx = new Context{this, transport->id};
        req->free_ctx = freeContext;
        return ESP_OK;
    }
    auto *context = static_cast<Context *>(req->sess_ctx);
    if (!context) {
        return ESP_FAIL;
    }
    httpd_ws_frame_t frame = {};
    if (httpd_ws_recv_frame(req, &frame, 0) != ESP_OK || frame.len > 1024) {
        return ESP_FAIL;
    }
    uint8_t data[1024];
    frame.payload = data;
    if (httpd_ws_recv_frame(req, &frame, sizeof data) != ESP_OK) {
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
        unsigned columns, rows;
        if (!webinterface::terminalSize(
                std::string(reinterpret_cast<const char *>(data), frame.len), columns, rows))
            return ESP_FAIL;
        c->columns = columns;
        c->rows = rows;
        c->sizeChanged = true;
        return ESP_OK;
    }
    if (!c || (c->fragmented ? frame.type != HTTPD_WS_TYPE_CONTINUE
                             : frame.type != HTTPD_WS_TYPE_BINARY)) {
        return ESP_FAIL;
    }
    c->fragmented = !frame.final;
    bool ok = c->input.push(data, frame.len);
    devicecfg::wipe(data, sizeof data);
    return ok ? ESP_OK : ESP_FAIL;
}
uint32_t WebService::prepareUpgrade(int fd, const webinterface::HeaderGate &gate) {
    Lock lock(mutex_);
    const std::string prefix = "/ws?token=";
    if (!webinterface::allowedHost(gate.host(), name_, ip_) ||
        !webinterface::allowedOrigin(gate.origin(), name_, ip_) ||
        gate.target().compare(0, prefix.size(), prefix) != 0 ||
        gate.target().size() != prefix.size() + 32) {
        return 0;
    }
    Client *available = nullptr;
    for (auto &c : clients_) {
        if (!c.active) {
            available = &c;
            break;
        }
    }
    if (!available || !tickets_.consume(gate.target().substr(prefix.size()), millis())) {
        return 0;
    }
    char csrf[33];
    if (!auth_.token(csrf)) {
        return 0;
    }
    available->input.clear();
    available->output.clear();
    available->fragmented = available->close = false;
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
        ",\"ver\":\"" FW_VERSION "\",\"size\":true,\"csrf\":\"" + csrf + "\"}";
    return available->id;
}
uint32_t WebService::clientId(unsigned index) {
    Lock lock(mutex_);
    return index < kClients && clients_[index].active ? clients_[index].id : 0;
}
bool WebService::connected(uint32_t id) {
    Lock lock(mutex_);
    return find(id) != nullptr;
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
    if (!c) {
        return false;
    }
    if (!c->output.push(data, len)) {
        c->close = true;
        return false;
    }
    return true;
}
size_t WebService::space(uint32_t id) {
    Lock lock(mutex_);
    auto *c = find(id);
    return c ? c->output.space() : 0;
}
void WebService::text(uint32_t id, const std::string &message) {
    Lock lock(mutex_);
    auto *c = find(id);
    if (c) {
        c->messages[5] = message;
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
    for (unsigned i = 0; i < kClients; ++i) {
        int fd;
        uint32_t id;
        bool close;
        uint8_t bytes[512];
        size_t n;
        std::string messages[6];
        {
            Lock lock(self.mutex_);
            auto &c = self.clients_[i];
            if (!c.active) {
                continue;
            }
            fd = c.fd;
            id = c.id;
            close = c.close;
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
        if (close) {
            self.disconnect(id);
            httpd_sess_trigger_close(self.server_, fd);
        }
    }
    Lock lock(self.mutex_);
    self.workQueued_ = false;
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
