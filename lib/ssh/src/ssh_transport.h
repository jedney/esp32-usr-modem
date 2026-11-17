// SSH transport layer (RFC 4253) for the modem's basic SSH client: version exchange,
// binary packets, key exchange (curve25519-sha256 or ecdh-sha2-nistp256) and re-keying.
//
// Hobby use only. The server's host key and its signature are read so that the exchange
// hash can be made, and are never checked: a man-in-the-middle cannot be detected.
//
// No Arduino dependencies: unit-tested on the PC.
#pragma once
#include "ssh_crypto.h"
#include "ssh_wire.h"

namespace ssh {
enum Message : uint8_t {
    kDisconnect = 1,
    kIgnore = 2,
    kUnimplemented = 3,
    kDebug = 4,
    kServiceRequest = 5,
    kServiceAccept = 6,
    kExtInfo = 7,
    kKexInit = 20,
    kNewKeys = 21,
    kKexEcdhInit = 30,
    kKexEcdhReply = 31,
    kUserauthRequest = 50,
    kUserauthFailure = 51,
    kUserauthSuccess = 52,
    kUserauthBanner = 53,
    kUserauthPasswdChangereq = 60,
    kGlobalRequest = 80,
    kRequestSuccess = 81,
    kRequestFailure = 82,
    kChannelOpen = 90,
    kChannelOpenConfirmation = 91,
    kChannelOpenFailure = 92,
    kChannelWindowAdjust = 93,
    kChannelData = 94,
    kChannelExtendedData = 95,
    kChannelEof = 96,
    kChannelClose = 97,
    kChannelRequest = 98,
    kChannelSuccess = 99,
    kChannelFailure = 100,
};

// The algorithm names this client offers, in order of preference.
extern const char *const kKexNames[];
extern const size_t kKexCount;
extern const char *const kHostKeyNames[];
extern const size_t kHostKeyCount;
extern const char *const kCipherNames[];
extern const size_t kCipherCount;
extern const char *const kMacNames[];
extern const size_t kMacCount;

class Transport {
public:
    static constexpr size_t kMaxPacket = 35000;             // RFC 4253 6.1
    static constexpr size_t kInSize = kMaxPacket + 4 + 64;  // length field and MAC or tag
    static constexpr size_t kOutSize = 16384;
    static constexpr size_t kMaxKexInit = 4096;
    static constexpr size_t kMaxHostKey = 2048;
    static constexpr uint64_t kRekeyBytes = uint64_t(1) << 30;

    explicit Transport(Crypto &crypto) : crypto_(crypto) {}
    // Sends the identification line and the first KEXINIT.
    void begin();
    // Destroys the keys; the connection is over.
    void end();

    // The network side. receive() takes at most inSpace() bytes.
    size_t inSpace() const;
    size_t receive(const uint8_t *data, size_t size);
    const uint8_t *outData() const { return out_; }
    size_t outSize() const { return outLen_; }
    void sent(size_t size);

    // Runs the key exchange: call often.
    void poll();
    // The next message for the session layer, valid until done().
    bool next(const uint8_t *&payload, size_t &size);
    void done();
    // Session-layer messages may be sent once the first key exchange is over and while no
    // re-key is running. send() returns false when the message must wait.
    bool canSend(size_t size) const;
    bool send(const uint8_t *payload, size_t size);

    bool established() const { return established_; }
    bool failed() const { return failed_; }
    const char *reason() const { return reason_; }
    void fail(const char *reason);

private:
    enum class Kex : uint8_t { kIdle, kWaitInit, kMakeKey, kWaitReply, kMakeSecret, kWaitNewKeys };
    struct Keys {
        Cipher cipher = Cipher::kNone;
        bool etm = false, mac = false;
        uint8_t key[32] = {}, iv[16] = {}, macKey[32] = {};
    };
    bool readVersion();
    bool openPacket(const uint8_t *&payload, size_t &size);
    void consume();
    bool seal(const uint8_t *payload, size_t size);
    bool sendKexInit();
    bool transportMessage(const uint8_t *payload, size_t size);
    bool serverKexInit(const uint8_t *payload, size_t size);
    bool kexReply(const uint8_t *payload, size_t size);
    bool finishKex();
    bool derive(char letter, const uint8_t *k, size_t kSize, const uint8_t *hash, uint8_t *out,
                size_t size);
    void install(unsigned slot, Keys &keys);
    void wipeKex();

    Crypto &crypto_;
    uint8_t in_[kInSize] = {};
    size_t inStart_ = 0, inEnd_ = 0, packetSize_ = 0;
    bool firstBlock_ = false;  // the first block of a CTR+HMAC packet is already decrypted
    uint8_t out_[kOutSize] = {};
    size_t outLen_ = 0;

    char serverVersion_[256] = {};
    size_t versionLines_ = 0;
    bool versionDone_ = false;
    uint8_t clientKexInit_[512] = {}, serverKexInit_[kMaxKexInit] = {};
    size_t clientKexInitSize_ = 0, serverKexInitSize_ = 0;
    uint8_t hostKey_[kMaxHostKey] = {};
    size_t hostKeySize_ = 0;
    uint8_t clientPublic_[65] = {}, serverPublic_[65] = {};
    size_t clientPublicSize_ = 0, serverPublicSize_ = 0;
    uint8_t sessionId_[kHashSize] = {};
    bool haveSessionId_ = false;
    Exchange exchange_ = Exchange::kX25519;
    Keys next_[2], now_[2];
    uint8_t txNonce_[kNonceSize] = {}, rxNonce_[kNonceSize] = {};

    Kex kex_ = Kex::kIdle;
    bool sentKexInit_ = false, ignoreNext_ = false, strict_ = false;
    bool established_ = false, failed_ = false, held_ = false;
    uint32_t txSeq_ = 0, rxSeq_ = 0;
    uint64_t bytes_ = 0;  // since the last key exchange
    const char *reason_ = "";
};
}  // namespace ssh
