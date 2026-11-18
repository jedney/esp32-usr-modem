#include "web_service.h"
#include <LittleFS.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <lwip/sockets.h>
#include <psa/crypto.h>
#include <cstring>
#include <new>
#include "hw.h"
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
// Monotonic uptime for the session deadline: it never wraps and ignores the wall clock.
uint64_t uptimeMs() {
    return static_cast<uint64_t>(esp_timer_get_time()) / 1000;
}
// The login page sends its form with Referrer-Policy same-origin: under no-referrer, browsers
// send "Origin: null" with a form POST, and the login's Origin check would refuse it.
void headers(httpd_req_t *req, bool sameOriginReferrer = false) {
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    // One request per connection (the header gate refuses a second one). The client closes its
    // end after this response and the server then frees the slot. The server is not asked to
    // close: a close queued from here names the slot, not the connection, so after a burst of
    // requests it closes whichever connection took the slot next.
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(req, "Referrer-Policy", sameOriginReferrer ? "same-origin" : "no-referrer");
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
// The session cookie: host-only, never readable by scripts, never sent cross-site. No Secure
// flag because the modem serves plain HTTP. An empty token expires the cookie.
std::string sessionCookie(const std::string &token) {
    return std::string(webinterface::kSessionCookie) + "=" + token +
           "; Path=/; HttpOnly; SameSite=Strict; Max-Age=" + (token.empty() ? "0" : "43200");
}
esp_err_t redirect(httpd_req_t *req, const char *location, const std::string &cookie = "") {
    headers(req);
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", location);
    if (!cookie.empty()) {
        httpd_resp_set_hdr(req, "Set-Cookie", cookie.c_str());
    }
    return httpd_resp_send(req, nullptr, 0);
}
// With lwIP IPv6 on, the server's socket is IPv6 and IPv4 clients come back as ::ffff:a.b.c.d.
void peerAddress(httpd_req_t *req, char out[16]) {
    out[0] = 0;
    sockaddr_storage peer = {};
    socklen_t len = sizeof peer;
    if (lwip_getpeername(httpd_req_to_sockfd(req), reinterpret_cast<sockaddr *>(&peer), &len) !=
        0) {
        return;
    }
    if (peer.ss_family == AF_INET) {
        const auto &v4 = reinterpret_cast<const sockaddr_in &>(peer);
        webinterface::formatPeer(reinterpret_cast<const uint8_t *>(&v4.sin_addr), 4, out);
    } else if (peer.ss_family == AF_INET6) {
        const auto &v6 = reinterpret_cast<const sockaddr_in6 &>(peer);
        webinterface::formatPeer(reinterpret_cast<const uint8_t *>(&v6.sin6_addr), 16, out);
    }
}
// The media type alone: "application/x-www-form-urlencoded; charset=UTF-8" is still a form.
std::string mediaType(const std::string &value) {
    std::string type = value.substr(0, value.find(';'));
    while (!type.empty() && type.back() == ' ') {
        type.pop_back();
    }
    return type;
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
bool WebCrypto::sha256(const uint8_t *data, size_t len, uint8_t out[32]) {
    size_t length = 0;
    return psa_hash_compute(PSA_ALG_SHA_256, data, len, out, 32, &length) == PSA_SUCCESS &&
           length == 32;
}
WebService::WebService(devicecfg::Device &device, const netconsole::ConsoleLog &console)
    : device_(device),
      console_(console),
      tickets_(crypto_),
      sessions_(crypto_),
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
            c.session = 0;
        }
        // Sessions outlive the listener: Wi-Fi reconnects and AT*WEB=OFF/ON keep them.
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
    if (hash_ != device_.otaHash()) {
        Lock lock(mutex_);
        hash_ = device_.otaHash();
        ++credentials_;  // a login checked against the old password is not committed
        sessions_.revokeAll();
        tickets_.reset();
        usedAck_ = false;
        // Send the requesting terminal's generated password before closing its
        // authenticated connection. New requests already require the new password.
        for (auto &c : clients_)
            if (c.active) {
                c.close = true;
                terminalOwner_.close(c.id);
                c.input.clear();
            }
    }
    {
        // A session ends exactly 12 hours after its login, whatever the browser does.
        uint32_t ended[webinterface::kSessionSlots];
        Lock lock(mutex_);
        size_t n = sessions_.expire(uptimeMs(), ended);
        for (size_t i = 0; i < n; ++i) {
            revokeLocked(ended[i]);
        }
    }
    if (uploading()) {
        pump();  // closes revoked sockets: the loop's own pump() waits while an upload runs
        return;
    }
    {
        Lock lock(mutex_);
        if (workerBusy_) {
            return;  // never stop the server under an upload's asynchronous request
        }
    }
    if (!enabled || !up || !device_.ready() || changed) {
        stop();
        Lock lock(mutex_);
        name_ = name;
        ip_ = ip;
        hash_ = device_.otaHash();
        if (changed) {
            usedAck_ = false;
        }
    }
    if (!enabled || !up || !device_.ready() || server_) {
        return;
    }
    if (!jobs_) {
        // One upload worker for the life of the firmware: an admitted upload streams there,
        // so the HTTP task keeps serving logout, login and the terminal meanwhile.
        jobs_ = xQueueCreate(1, sizeof(Job));
        if (!jobs_ || xTaskCreatePinnedToCore(uploadWorker, "webupload", 12288, this,
                                              tskIDLE_PRIORITY + 5, nullptr, 0) != pdPASS) {
            return;
        }
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
    cfg.max_uri_handlers = 5;
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
    route.uri = "/api/login";
    route.handler = loginRoute;
    httpd_register_uri_handler(server_, &route);
    route.uri = "/api/logout";
    route.handler = logoutRoute;
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
esp_err_t WebService::get(httpd_req_t *req) {
    return static_cast<WebService *>(req->user_ctx)->serve(req);
}
webinterface::CookieResult WebService::session(httpd_req_t *req, uint32_t &serial,
                                               std::string *token) {
    serial = 0;
    std::string value;
    webinterface::CookieResult result =
        webinterface::parseSessionCookie(header(req, "Cookie", 1024), value);
    if (result == webinterface::CookieResult::kOk) {
        Lock lock(mutex_);
        serial = sessions_.find(value, uptimeMs());
    }
    if (token) {
        *token = value;
    }
    return result;
}
esp_err_t WebService::serve(httpd_req_t *req) {
    if (!host(req, false)) {
        return reply(req, "403 Forbidden", "Invalid modem host.");
    }
    std::string path = req->uri;
    path.resize(path.find('?') == std::string::npos ? path.size() : path.find('?'));
    if (path == "/") {
        path = "/index.html";
    }
    struct Asset {
        const char *path;
        const char *mime;
        bool open;  // served before login: the login page and what it shows
    };
    static const Asset assets[] = {{"/index.html", "text/html; charset=utf-8", false},
                                   {"/login.html", "text/html; charset=utf-8", true},
                                   {"/login.js", "text/javascript", true},
                                   {"/usr-logo.png", "image/png", true},
                                   {"/menubar.css", "text/css", true},
                                   {"/menubar.js", "text/javascript", false},
                                   {"/menu-view.js", "text/javascript", false},
                                   {"/command-channel.js", "text/javascript", false},
                                   {"/style.css", "text/css", true},
                                   {"/app.js", "text/javascript", false},
                                   {"/status.js", "text/javascript", false},
                                   {"/demo.js", "text/javascript", false},
                                   {"/upload.js", "text/javascript", false},
                                   {"/upload_flow.js", "text/javascript", false},
                                   {"/link.js", "text/javascript", false},
                                   {"/geometry.js", "text/javascript", false},
                                   {"/idle.js", "text/javascript", false},
                                   {"/toast.js", "text/javascript", false},
                                   {"/ui-data.js", "text/javascript", false},
                                   {"/ui-commands.js", "text/javascript", false},
                                   {"/xterm.mjs", "text/javascript", false},
                                   {"/xterm.css", "text/css", false},
                                   {"/ibm-plex-mono-400.woff2", "font/woff2", true},
                                   {"/ibm-plex-mono-700.woff2", "font/woff2", true}};
    uint32_t serial;
    const webinterface::CookieResult cookie = session(req, serial, nullptr);
    const Asset *asset = nullptr;
    if (path == "/login") {
        if (serial) {
            return redirect(req, "/");
        }
        asset = &assets[1];
    }
    for (const auto &a : assets) {
        if (!asset && path == a.path) {
            asset = &a;
        }
    }
    if (!serial && !(asset && asset->open)) {
        if (path == "/index.html") {
            return redirect(req, "/login");
        }
        return cookie == webinterface::CookieResult::kAmbiguous
                   ? reply(req, "400 Bad Request", "Ambiguous session cookie. Log in again.")
                   : reply(req, "401 Unauthorized", "Log in as admin with the device password.");
    }
    if (path == "/api/auth") {
        std::string body;
        {
            Lock lock(mutex_);
            body = "{\"user\":\"" + std::string(webinterface::kAccount) + "\",\"expires\":" +
                   std::to_string(sessions_.remainingSeconds(serial, uptimeMs())) + ",\"csrf\":\"" +
                   sessions_.csrf(serial) + "\"}";
        }
        headers(req);
        httpd_resp_set_type(req, "application/json");
        esp_err_t result = httpd_resp_send(req, body.c_str(), body.size());
        devicecfg::wipe(body.data(), body.size());
        return result;
    }
    if (path == "/api/boot") {
        headers(req);
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
            ticket = tickets_.issue(serial, millis());
        }
        headers(req);
        httpd_resp_set_type(req, "application/json");
        std::string body = "{\"token\":\"" + ticket + "\"}";
        return httpd_resp_send(req, body.c_str(), body.size());
    }
    if (!asset) {
        return reply(req, "404 Not Found", "Not found.");
    }
    if (uploading()) {
        return reply(req, "503 Service Unavailable", "Update in progress.");
    }
    File file = LittleFS.open(("/www" + std::string(asset->path) + ".gz").c_str(), "r");
    if (!file) {
        return reply(req, "503 Service Unavailable",
                     "Web files unavailable. Restore littlefs.bin using USB uploadfs.");
    }
    headers(req, asset == &assets[1]);
    httpd_resp_set_type(req, asset->mime);
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
esp_err_t WebService::loginRoute(httpd_req_t *req) {
    return static_cast<WebService *>(req->user_ctx)->login(req);
}
esp_err_t WebService::logoutRoute(httpd_req_t *req) {
    return static_cast<WebService *>(req->user_ctx)->logout(req);
}
// A form POST. Every outcome goes back to a page: the terminal after a login, else the login
// page with a fixed error code. Nothing submitted is echoed, logged or kept.
esp_err_t WebService::login(httpd_req_t *req) {
    if (!host(req, true)) {
        return reply(req, "403 Forbidden", "Invalid modem host or origin.");
    }
    if (mediaType(header(req, "Content-Type")) != "application/x-www-form-urlencoded" ||
        req->content_len > webinterface::kLoginFormBytes) {
        reply(req, "400 Bad Request", "Invalid login form.");
        return ESP_FAIL;  // close without reading the body
    }
    std::string body(req->content_len, '\0');
    size_t received = 0;
    const uint32_t start = millis();
    while (received < body.size() && millis() - start < 5000) {
        int n = httpd_req_recv(req, &body[received], body.size() - received);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (n <= 0) {
            break;
        }
        received += size_t(n);
    }
    char user[webinterface::kLoginFieldBytes + 1], password[webinterface::kLoginFieldBytes + 1];
    const bool parsed =
        received == body.size() && webinterface::parseLoginForm(body, user, password);
    devicecfg::wipe(body.data(), body.size());
    if (!parsed) {
        return redirect(req, "/login?error=invalid");
    }
    std::string hash;
    uint32_t credentials;
    bool allowed;
    {
        Lock lock(mutex_);
        allowed = throttle_.begin(millis());
        hash = hash_;
        credentials = credentials_;
    }
    bool correct = allowed && webinterface::verifyPassword(crypto_, user, password, hash.c_str());
    devicecfg::wipe(user, sizeof user);
    devicecfg::wipe(password, sizeof password);
    devicecfg::wipe(hash.data(), hash.size());
    if (!allowed) {
        return redirect(req, "/login?error=wait");
    }
    {
        Lock lock(mutex_);
        throttle_.finish(correct, millis());
    }
    if (!correct) {
        return redirect(req, "/login?error=invalid");
    }
    // The boot-banner password is erased (on the modem loop) before the login counts.
    if (!used()) {
        return redirect(req, "/login?error=unavailable");
    }
    uint32_t ignored;
    std::string previous;
    session(req, ignored, &previous);
    char peer[16];
    peerAddress(req, peer);
    std::string token;
    webinterface::Sessions::Result result = webinterface::Sessions::Result::kFailed;
    {
        Lock lock(mutex_);
        if (credentials == credentials_) {  // the password did not change meanwhile
            uint32_t serial, replaced;
            result = sessions_.create(previous, uptimeMs(), token, serial, replaced);
            if (result == webinterface::Sessions::Result::kOk) {
                if (replaced) {
                    revokeLocked(replaced);
                }
                logins_.push(webinterface::LoginKind::kWeb, peer);
            }
        } else {
            correct = false;
        }
    }
    devicecfg::wipe(previous.data(), previous.size());
    if (!correct) {
        return redirect(req, "/login?error=invalid");
    }
    if (result == webinterface::Sessions::Result::kFull) {
        return redirect(req, "/login?error=busy");
    }
    if (result != webinterface::Sessions::Result::kOk) {
        return redirect(req, "/login?error=unavailable");
    }
    const std::string cookie = sessionCookie(token);
    devicecfg::wipe(token.data(), token.size());
    esp_err_t sent = redirect(req, "/", cookie);
    return sent;
}
// Ends the browser's session in every tab: its tickets, its sockets and its input. Repeating
// it is harmless. A live session needs its CSRF token, so another site cannot log anyone out.
esp_err_t WebService::logout(httpd_req_t *req) {
    if (!host(req, true)) {
        return reply(req, "403 Forbidden", "Invalid modem host or origin.");
    }
    uint32_t serial;
    session(req, serial, nullptr);
    if (serial) {
        const std::string csrf = header(req, "X-CSRF-Token", 32);
        {
            Lock lock(mutex_);
            if (!sessions_.csrfMatches(serial, csrf, uptimeMs())) {
                serial = 0;
            } else {
                revokeLocked(serial);
            }
        }
        if (!serial) {
            return reply(req, "403 Forbidden", "Invalid CSRF token.");
        }
        sendClients();  // close the session's sockets now, from this task
    }
    // The server keeps a pointer to each header value until the response is sent.
    const std::string expired = sessionCookie("");
    headers(req);
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_set_hdr(req, "Set-Cookie", expired.c_str());
    return httpd_resp_send(req, nullptr, 0);
}
void WebService::revokeLocked(uint32_t serial) {
    sessions_.revoke(serial);
    tickets_.revoke(serial);
    for (auto &c : clients_) {
        if (c.active && c.session == serial) {
            terminalOwner_.close(c.id);
            c.input.clear();
            c.output.clear();  // nothing more reaches a logged-out browser
            for (auto &m : c.messages) {
                m.clear();
            }
            c.close = true;
        }
    }
}
void WebService::revokeSessions() {
    {
        Lock lock(mutex_);
        sessions_.revokeAll();
        tickets_.reset();
        for (auto &c : clients_) {
            if (c.active) {
                terminalOwner_.close(c.id);
                c.input.clear();
                c.close = true;
            }
        }
    }
    queueSend();
}
bool WebService::takeLogin(char *out, size_t cap) {
    Lock lock(mutex_);
    return logins_.take(out, cap);
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
                    if (!terminalOwner_.owns(c->id) || c->close ||
                        !sessions_.valid(c->session, uptimeMs())) {
                        offset = n;  // observers and ended sessions never queue commands
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
    if (c && (c->close || !sessions_.valid(c->session, uptimeMs()))) {
        devicecfg::wipe(data, sizeof data);
        return ESP_OK;  // the socket is closing: ignore what it still sends
    }
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
    if (!available) {
        return 0;
    }
    const uint32_t session = tickets_.consume(target.substr(prefix.size()), millis());
    char csrf[33];
    if (!session || !sessions_.valid(session, uptimeMs()) ||
        !webinterface::makeToken(crypto_, csrf)) {
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
    available->session = session;
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
    queueSend();
}
void WebService::queueSend() {
    Lock lock(mutex_);
    if (server_ && !workQueued_ && httpd_queue_work(server_, sendWork, this) == ESP_OK) {
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
// Admission runs on the HTTP task: the session, Host, Origin, metadata and the CSRF token of
// one of that session's terminals. An admitted upload then belongs to the upload worker, which
// replies and completes the request; logging out or expiring after admission never stops it.
esp_err_t WebService::upload(httpd_req_t *req) {
    uint32_t serial;
    session(req, serial, nullptr);
    if (!serial) {
        reply(req, "401 Unauthorized", "Log in as admin with the device password.");
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
    Job job = {};
    job.filesystem = fs;
    snprintf(job.version, sizeof job.version, "%s", version.c_str());
    snprintf(job.expected, sizeof job.expected, "%s", expected.c_str());
    std::string csrf = header(req, "X-CSRF-Token", 32);
    {
        Lock lock(mutex_);
        for (auto &c : clients_) {
            if (c.active && !c.close && c.session == serial &&
                webinterface::equalSecret(c.csrf, csrf)) {
                job.owner = c.id;
                break;
            }
        }
        if (!job.owner || workerBusy_ || upload_.uploading()) {
            return reply(req, "403 Forbidden", "Invalid CSRF session or update already active.");
        }
        workerBusy_ = true;
    }
    if (httpd_req_async_handler_begin(req, &job.req) != ESP_OK ||
        xQueueSend(jobs_, &job, 0) != pdTRUE) {
        if (job.req) {
            httpd_req_async_handler_complete(job.req);
        }
        Lock lock(mutex_);
        workerBusy_ = false;
        reply(req, "503 Service Unavailable", "Update worker unavailable. Try again.");
        return ESP_FAIL;
    }
    return ESP_OK;
}
void WebService::uploadWorker(void *ctx) {
    auto *self = static_cast<WebService *>(ctx);
    Job job;
    for (;;) {
        if (xQueueReceive(self->jobs_, &job, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        self->install(job);
        // The request is finished on every path, success or failure, before the worker is
        // free again; only then may the server stop.
        httpd_req_async_handler_complete(job.req);
        Lock lock(self->mutex_);
        self->workerBusy_ = false;
    }
}
void WebService::install(const Job &job) {
    httpd_req_t *req = job.req;
    const bool fs = job.filesystem;
    const std::string version = job.version, expected = job.expected;
    {
        Lock lock(mutex_);
        if (!upload_.ask(job.owner)) {
            reply(req, "403 Forbidden", "Invalid CSRF session or update already active.");
            return;
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
        reply(req, "409 Conflict", "Modem busy. Exit other menus, calls and tests first.");
        return;
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
    // The modem loop does not refresh the panel during an upload, so the LEDs show the
    // progress. A LittleFS image fills them in two halves: receive, then copy.
    const size_t progressTotal = fs ? 2 * req->content_len : req->content_len;
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
        hw::ledsProgress(received, progressTotal);
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
                ok = esp_partition_read(staging, offset, buffer, n) == ESP_OK;
                offset += n;
            }
            ok = ok && Update.write(buffer, n) == n;
            written += n;
            hw::ledsProgress(received + written, progressTotal);
            delay(1);
        }
        ok = ok && written == req->content_len;
    }
    ok = ok && Update.end(false);
    if (!ok) {
        Update.abort();
        hw::ledsSet(0);  // the modem loop restores the panel
    }
    devicecfg::wipe(buffer, sizeof buffer);
    reply(req, ok ? "200 OK" : "400 Bad Request",
          ok ? "Update complete. Rebooting."
             : "Image rejected or update failed. USB recovery remains available.");
    Lock lock(mutex_);
    upload_.finish(ok, fs);
}
}  // namespace hw
