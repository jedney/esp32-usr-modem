// ESP-IDF HTTP/WebSocket adapter. The server task owns sockets; the modem loop
// owns commands. Bounded queues and snapshots are the only cross-core boundary.
#pragma once
#include <Arduino.h>
#include <esp_http_server.h>
#include <freertos/semphr.h>
#include <string>
#include "devicecfg.h"
#include "upload_handshake.h"
#include "web_policy.h"

namespace hw {
class WebCrypto : public webinterface::Crypto {
public:
    bool random(uint8_t *out, size_t len) override;
    bool md5(const std::string &text, char out[33]) override;
};
class WebService {
public:
    static constexpr unsigned kClients = 3;
    explicit WebService(devicecfg::Device &device);
    void poll(bool enabled, bool up, const char *name, const char *ip);
    uint32_t clientId(unsigned index);
    bool connected(uint32_t id);
    size_t read(uint32_t id, uint8_t *data, size_t cap);
    void discardInput(uint32_t id);
    bool takeSize(uint32_t id, unsigned &columns, unsigned &rows);
    bool write(uint32_t id, const uint8_t *data, size_t len);
    size_t space(uint32_t id);
    void text(uint32_t id, const std::string &message);
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
    // Checks a WebSocket upgrade (Host, Origin, one-time ticket) and claims a client slot.
    // Returns the client's id, or 0 if the upgrade is refused.
    uint32_t prepareUpgrade(int fd, const std::string &host, const std::string &origin,
                            const std::string &target);
    void disconnect(uint32_t id);

private:
    struct Client {
        uint32_t id = 0;
        int fd = -1;
        bool active = false, fragmented = false, close = false;
        bool sizeChanged = false;
        unsigned columns = 80, rows = 25;
        std::string csrf;
        webinterface::Queue<2048> input;
        webinterface::Queue<16384> output;
        std::string messages[6];
        uint32_t sent[4] = {};
    };
    struct Context {
        WebService *self;
        uint32_t id;
    };
    static esp_err_t get(httpd_req_t *req);
    static esp_err_t websocket(httpd_req_t *req);
    static esp_err_t upgrade(httpd_req_t *req);  // ws_pre_handshake_cb
    static esp_err_t update(httpd_req_t *req);
    static void freeContext(void *ctx);
    static void sendWork(void *ctx);
    esp_err_t serve(httpd_req_t *req);
    esp_err_t ws(httpd_req_t *req);
    esp_err_t upload(httpd_req_t *req);
    bool authorized(httpd_req_t *req);
    bool host(httpd_req_t *req, bool origin);
    bool used();
    Client *find(uint32_t id);
    void stop();
    devicecfg::Device &device_;
    WebCrypto crypto_;
    webinterface::Auth auth_;
    webinterface::Tickets tickets_;
    SemaphoreHandle_t mutex_;
    httpd_handle_t server_ = nullptr;
    std::string name_, ip_, hash_;
    bool needUsed_ = false, usedAck_ = false, workQueued_ = false;
    Client clients_[kClients];
    uint32_t nextId_ = 1, nextPump_ = 0;
    std::string snapshots_[4];
    uint32_t revision_[4] = {};
    webinterface::UploadHandshake upload_;  // under mutex_
};
}  // namespace hw
