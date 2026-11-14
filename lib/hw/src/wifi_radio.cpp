// The S3's Wi-Fi, mDNS and NTP: see wifi_radio.h.
#include "wifi_radio.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_netif_sntp.h>
#include <esp_wifi.h>
#include <sys/time.h>

#include <cstdio>
#include <cstring>

namespace hw {

using netcfg::LinkState;
using netcfg::Reason;

namespace {

// Written by the Wi-Fi event task (core 0) and by loop() (core 1). The state and its reason
// change together under linkLock, and the event handler only makes the moves that make sense
// from the state loop() left: a late "got IP" from a connection that leave() or
// off() has already ended can no longer bring the link back up.
portMUX_TYPE linkLock = portMUX_INITIALIZER_UNLOCKED;
LinkState linkState = LinkState::kOff;  // guarded by linkLock
Reason linkReason = Reason::kNone;      // guarded by linkLock
// Written by the TCP/IP task, read by loop(). 64 bits are two stores on this CPU, so the
// lock stops a read from seeing half of an update.
portMUX_TYPE syncLock = portMUX_INITIALIZER_UNLOCKED;
int64_t syncedAt = 0;  // guarded by syncLock

void setLink(LinkState state, Reason reason) {
    portENTER_CRITICAL(&linkLock);
    linkState = state;
    linkReason = reason;
    portEXIT_CRITICAL(&linkLock);
}

// Our own scan limit; the first scan after the radio starts takes the longest. Arduino's
// own limit (60 s) is longer, so this one always decides.
constexpr uint32_t kScanLimitMs = 15000;
bool scanning = false;
uint32_t scanStartedAt = 0;

bool started = false;
bool mdnsRunning = false;
char appliedHost[netcfg::kHostMax + 1] = "";  // the name the STA interface started with
char ntpServer[netcfg::kNtpMax + 1] = "";     // lwIP keeps a pointer to this

Reason mapReason(uint8_t r, bool wasUp) {
    switch (r) {
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
            return wasUp ? Reason::kLost : Reason::kWrongPassword;
        case WIFI_REASON_NO_AP_FOUND:
            return wasUp ? Reason::kLost : Reason::kNotFound;
        case WIFI_REASON_AUTH_EXPIRE:  // IDF 6: also covers the old ASSOC_EXPIRE
        case WIFI_REASON_CONNECTION_FAIL:
            return wasUp ? Reason::kLost : Reason::kNoAnswer;
        default:
            return wasUp ? Reason::kLost : Reason::kOther;
    }
}

void onWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            portENTER_CRITICAL(&linkLock);
            if (linkState == LinkState::kConnecting || linkState == LinkState::kUp) {
                linkReason = Reason::kNone;
                linkState = LinkState::kUp;
            }
            portEXIT_CRITICAL(&linkLock);
            break;
        case ARDUINO_EVENT_WIFI_STA_LOST_IP:
            portENTER_CRITICAL(&linkLock);
            if (linkState == LinkState::kUp) {
                linkReason = Reason::kLost;
                linkState = LinkState::kDown;
            }
            portEXIT_CRITICAL(&linkLock);
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
            const uint8_t r = info.wifi_sta_disconnected.reason;
            if (r == WIFI_REASON_ASSOC_LEAVE) {
                break;  // our own leave() or a new connect()
            }
            portENTER_CRITICAL(&linkLock);
            if (linkState == LinkState::kConnecting || linkState == LinkState::kUp) {
                linkReason = mapReason(r, linkState == LinkState::kUp);
                linkState = LinkState::kDown;
            }
            portEXIT_CRITICAL(&linkLock);
            break;
        }
        default:
            break;
    }
}

void onTimeSync(struct timeval *tv) {
    portENTER_CRITICAL(&syncLock);
    syncedAt = tv->tv_sec;
    portEXIT_CRITICAL(&syncLock);
}

void start() {
    if (started) {
        return;
    }
    started = true;
    WiFi.persistent(false);  // no ESP-IDF copy of the network or password in flash
    Network.onEvent(onWifiEvent);
}

IPAddress toIp(uint32_t v) {
    return IPAddress(static_cast<uint8_t>(v >> 24), static_cast<uint8_t>(v >> 16),
                     static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v));
}

uint32_t fromIp(const IPAddress &a) {
    return static_cast<uint32_t>(a[0]) << 24 | static_cast<uint32_t>(a[1]) << 16 |
           static_cast<uint32_t>(a[2]) << 8 | static_cast<uint32_t>(a[3]);
}

netcfg::Security mapSecurity(wifi_auth_mode_t m) {
    switch (m) {
        case WIFI_AUTH_OPEN:
            return netcfg::Security::kOpen;
        case WIFI_AUTH_WEP:
            return netcfg::Security::kWep;
        case WIFI_AUTH_WPA_PSK:
            return netcfg::Security::kWpa;
        case WIFI_AUTH_WPA2_PSK:
        case WIFI_AUTH_WPA_WPA2_PSK:
            return netcfg::Security::kWpa2;
        case WIFI_AUTH_WPA3_PSK:
        case WIFI_AUTH_WPA2_WPA3_PSK:
            return netcfg::Security::kWpa3;
        case WIFI_AUTH_WPA2_ENTERPRISE:
            return netcfg::Security::kEnterprise;
        default:
            return netcfg::Security::kOther;
    }
}

void mdnsStop() {
    if (mdnsRunning) {
        MDNS.end();
        mdnsRunning = false;
    }
}

}  // namespace

void EspRadio::connect(const netcfg::Settings &s) {
    start();
    // The name is given to the STA interface when it starts, so restart it for a new one.
    if (strcmp(appliedHost, s.host) != 0 && WiFi.getMode() != WIFI_OFF) {
        mdnsStop();
        WiFi.mode(WIFI_OFF);
    }
    WiFi.setHostname(s.host);
    snprintf(appliedHost, sizeof appliedHost, "%s", s.host);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);          // no power-save delays on Telnet traffic
    WiFi.setAutoReconnect(false);  // netcfg::Net decides when to try again
    WiFi.disconnect(false, false);
    if (s.dhcp) {
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
    } else {
        WiFi.config(toIp(s.ip), toIp(s.gateway), toIp(s.mask), toIp(s.dns));
    }
    WiFi.setMinSecurity(s.pass[0] != '\0' ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN);
    setLink(LinkState::kConnecting, Reason::kNone);
    WiFi.begin(s.ssid, s.pass[0] != '\0' ? s.pass : nullptr);
}

void EspRadio::leave() {
    start();
    mdnsStop();
    setLink(LinkState::kDown, Reason::kNone);
    if (WiFi.getMode() != WIFI_OFF) {
        WiFi.disconnect(false, false);
    }
}

void EspRadio::off() {
    start();
    mdnsStop();
    setLink(LinkState::kOff, Reason::kNone);
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
}

netcfg::LinkInfo EspRadio::link() {
    netcfg::LinkInfo l = {};
    portENTER_CRITICAL(&linkLock);
    l.state = linkState;
    l.reason = linkReason;
    portEXIT_CRITICAL(&linkLock);
    if (l.state == LinkState::kUp) {
        l.rssi = static_cast<int8_t>(WiFi.RSSI());
        l.channel = static_cast<uint8_t>(WiFi.channel());
        l.ip = fromIp(WiFi.localIP());
        l.mask = fromIp(WiFi.subnetMask());
        l.gateway = fromIp(WiFi.gatewayIP());
        l.dns = fromIp(WiFi.dnsIP(0));
    }
    return l;
}

bool EspRadio::scanStart() {
    start();
    if (WiFi.getMode() == WIFI_OFF) {
        WiFi.mode(WIFI_STA);  // netcfg::Net turns it off again after the scan
    }
    if (scanning) {
        // A scan the caller gave up on (a cancelled AT*WIFISCAN) is still running: join it
        // rather than stop it. Stopping posts a "scan done" event which could arrive after
        // the new scan began and end it at once with a short list.
        scanStartedAt = millis();
        return true;
    }
    // Active scan in the background, 0-300 ms per channel, hidden networks left out.
    WiFi.setScanActiveMinTime(0);
    if (WiFi.scanNetworks(true, false, false, 300) != WIFI_SCAN_RUNNING) {
        scanning = false;
        return false;
    }
    scanning = true;
    scanStartedAt = millis();
    return true;
}

int EspRadio::scanResult(netcfg::ScanEntry *out, int max) {
    if (!scanning) {
        return -2;
    }
    const int16_t n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) {
        if (millis() - scanStartedAt < kScanLimitMs) {
            return -1;
        }
        esp_wifi_scan_stop();
        WiFi.scanDelete();
        scanning = false;
        return -2;
    }
    scanning = false;
    if (n < 0) {
        return -2;
    }
    int count = 0;
    for (int16_t i = 0; i < n && count < max; ++i) {
        netcfg::ScanEntry &e = out[count++];
        snprintf(e.ssid, sizeof e.ssid, "%s", WiFi.SSID(i).c_str());
        e.rssi = static_cast<int8_t>(WiFi.RSSI(i));
        e.channel = static_cast<uint8_t>(WiFi.channel(i));
        e.security = mapSecurity(WiFi.encryptionType(i));
    }
    WiFi.scanDelete();
    return count;
}

void EspRadio::startServices(const netcfg::Settings &s) {
    mdnsStop();
    mdnsRunning = MDNS.begin(s.host);
    snprintf(ntpServer, sizeof ntpServer, "%s", s.ntp);
    // The clock runs in UTC (the default time zone); netcfg::tz does the local time.
    esp_netif_sntp_deinit();  // a new join starts SNTP again with the current server
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(ntpServer);
    config.wait_for_sync = false;
    config.sync_cb = onTimeSync;
    esp_netif_sntp_init(&config);
}

int64_t EspRadio::utcNow() {
    const time_t t = time(nullptr);
    return t > 1600000000 ? static_cast<int64_t>(t) : 0;  // before 2020: not set yet
}

int64_t EspRadio::lastSync() {
    portENTER_CRITICAL(&syncLock);
    const int64_t at = syncedAt;
    portEXIT_CRITICAL(&syncLock);
    return at;
}

void EspRadio::mac(uint8_t out[6]) {
    WiFi.macAddress(out);
}

void wifiMac(uint8_t out[6]) {
    if (esp_read_mac(out, ESP_MAC_WIFI_STA) != ESP_OK) {
        memset(out, 0, 6);
    }
}

}  // namespace hw
