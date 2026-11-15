// ESP-IDF HTTP/WebSocket adapter. The server task owns sockets; the modem loop
// owns commands. Bounded queues and snapshots are the only cross-core boundary.
#pragma once
#include <Arduino.h>
#include <esp_http_server.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <string>
#include "console_log.h"
#include "devicecfg.h"
#include "session_policy.h"
#include "upload_handshake.h"
#include "web_policy.h"

namespace hw {
class WebCrypto : public webinterface::Crypto {
public:
    bool random(uint8_t *out, size_t len) override;
    bool sha256(const uint8_t *data, size_t len, uint8_t out[32]) override;
};
class WebService {
public:
    static constexpr unsigned kClients = 3;
    WebService(devicecfg::Device &device, const netconsole::ConsoleLog &console);
    void poll(bool enabled, bool up, const char *name, const char *ip);
    // Next successful browser login for the console ("Login: web admin from ..."), if any.
    bool takeLogin(char *out, size_t cap);
    // Ends every browser session and closes their sockets (a reset is about to restart).
    void revokeSessions();
    uint32_t clientId(unsigned index);
    bool connected(uint32_t id);
    size_t read(uint32_t id, uint8_t *data, size_t cap);
    void discardInput(uint32_t id);
    bool takeSize(uint32_t id, unsigned &columns, unsigned &rows);
    bool takeOpen(uint32_t id);
    bool takeHangup(uint32_t id);
    void terminal(uint32_t id, bool enabled);
    bool write(uint32_t id, const uint8_t *data, size_t len);
    size_t space(uint32_t id);
    void text(uint32_t id, const std::string &message);
    // Command readiness follows all queued terminal output and has its own status slot.
    void commandState(uint32_t id, const std::string &message);
    void snapshot(unsigned type, const std::string &message, bool force = false);
    void pump();
    // Upload hand-shake, modem loop side (see webinterface::UploadHandshake).
    bool takeUploadRequest(uint32_t &client);
    // False if the server stopped waiting first: the caller then undoes its claim.
    bool allowUpload(bool allow);
    bool uploading();
    bool takeUploadDone(bool &ok, bool &filesystem);
    bool takeUnmountRequest();
    void unmounted();
    // Checks a WebSocket upgrade (Host, Origin, one-time ticket of a live session) and claims a
    // client slot bound to that session.
    // Returns the client's id, or 0 if the upgrade is refused.
    uint32_t prepareUpgrade(int fd, const std::string &host, const std::string &origin,
                            const std::string &target);
    void disconnect(uint32_t id);
    void close(uint32_t id);  // modem task: ask the HTTP task to close an unresponsive socket

private:
    struct Client {
        uint32_t id = 0;
        int fd = -1;
        bool active = false, fragmented = false, close = false;
        bool sizeChanged = false, openRequested = false;
        unsigned columns = 80, rows = 25;
        uint32_t session = 0;  // the login session the socket belongs to
        std::string csrf;
        webinterface::Queue<webinterface::kTerminalInputBytes> input;
        webinterface::Queue<webinterface::kTerminalOutputBytes> output;
        std::string messages[7];
        uint32_t sent[4] = {};
    };
    struct Context {
        WebService *self;
        uint32_t id;
    };
    // An admitted upload, handed from the HTTP task to the upload worker.
    struct Job {
        httpd_req_t *req;  // the asynchronous copy; completed by the worker
        uint32_t owner;
        bool filesystem;
        char version[12];
        char expected[65];
    };
    static esp_err_t get(httpd_req_t *req);
    static esp_err_t websocket(httpd_req_t *req);
    static esp_err_t upgrade(httpd_req_t *req);  // ws_pre_handshake_cb
    static esp_err_t update(httpd_req_t *req);
    static esp_err_t loginRoute(httpd_req_t *req);
    static esp_err_t logoutRoute(httpd_req_t *req);
    static void freeContext(void *ctx);
    static void uploadWorker(void *ctx);
    static void sendWork(void *ctx);
    void sendClients();  // HTTP task only; also drains output while input is held off
    esp_err_t serve(httpd_req_t *req);
    esp_err_t ws(httpd_req_t *req);
    esp_err_t upload(httpd_req_t *req);
    esp_err_t login(httpd_req_t *req);
    esp_err_t logout(httpd_req_t *req);
    void install(const Job &job);  // upload worker only
    // The request's live session serial (0 if none); kAmbiguous never counts as logged in.
    webinterface::CookieResult session(httpd_req_t *req, uint32_t &serial, std::string *token);
    bool host(httpd_req_t *req, bool origin);
    bool used();
    void revokeLocked(uint32_t serial);  // under mutex_
    void queueSend();
    Client *find(uint32_t id);
    void stop();
    devicecfg::Device &device_;
    const netconsole::ConsoleLog &console_;
    WebCrypto crypto_;
    webinterface::Tickets tickets_;
    webinterface::Sessions sessions_;       // under mutex_; kept across listener restarts
    webinterface::LoginThrottle throttle_;  // under mutex_
    webinterface::LoginEvents logins_;      // under mutex_
    SemaphoreHandle_t mutex_;
    QueueHandle_t jobs_ = nullptr;
    httpd_handle_t server_ = nullptr;
    std::string name_, ip_, hash_;  // hash_: the device-password hash (SHA-256 hex)
    uint32_t credentials_ = 0;      // changes with every new password
    bool needUsed_ = false, usedAck_ = false, workQueued_ = false;
    bool workerBusy_ = false;  // an admitted upload holds the worker until it replies
    Client clients_[kClients];
    webinterface::TerminalOwner terminalOwner_;
    uint32_t nextId_ = 1, nextPump_ = 0;
    std::string snapshots_[4];
    uint32_t revision_[4] = {};
    webinterface::UploadHandshake upload_;  // under mutex_
};
}  // namespace hw
