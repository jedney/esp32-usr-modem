// PC stand-ins for the SSH client tests: FakeCrypto (deterministic, not secure: XOR key
// streams and checksum "MACs") and FakeServer, a scripted SSH server that speaks the real
// wire protocol with the same FakeCrypto rules, so both ends derive the same keys.
#pragma once
#include <algorithm>
#include <cstring>
#include <deque>
#include <string>
#include <vector>
#include "ssh_crypto.h"
#include "ssh_transport.h"
#include "ssh_wire.h"

namespace sshfake {
inline void fnv(uint64_t &h, const uint8_t *data, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        h = (h ^ data[i]) * 0x100000001b3ull;
    }
}
inline void digest(const std::vector<uint8_t> &data, uint8_t salt, uint8_t *out, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        uint64_t h = 0xcbf29ce484222325ull;
        const uint8_t lane[2] = {salt, uint8_t(i)};
        fnv(h, lane, 2);
        fnv(h, data.data(), data.size());
        out[i] = uint8_t(h >> 24);
    }
}

class FakeCrypto : public ssh::Crypto {
public:
    struct Slot {
        ssh::Cipher cipher = ssh::Cipher::kNone;
        std::vector<uint8_t> key, iv, macKey;
        uint64_t position = 0;
    };
    uint8_t seed = 1;
    unsigned busyPolls = 0;  // agreeState() reports kBusy this many times per step
    bool failAgree = false;
    std::vector<uint8_t> hashing;
    ssh::Exchange kind = ssh::Exchange::kX25519;
    std::vector<uint8_t> mine, secret;
    ssh::Work state = ssh::Work::kIdle;
    unsigned busyLeft = 0;
    Slot slots[2];
    unsigned agreements = 0, forgets = 0;

    void random(uint8_t *out, size_t size) override {
        for (size_t i = 0; i < size; ++i) {
            out[i] = seed++;
        }
    }
    bool hashBegin() override {
        hashing.clear();
        return true;
    }
    bool hashUpdate(const uint8_t *data, size_t size) override {
        hashing.insert(hashing.end(), data, data + size);
        return true;
    }
    bool hashFinish(uint8_t out[ssh::kHashSize]) override {
        digest(hashing, 0x48, out, ssh::kHashSize);
        hashing.clear();
        return true;
    }
    bool agreeBegin(ssh::Exchange k) override {
        kind = k;
        ++agreements;
        mine.assign(k == ssh::Exchange::kX25519 ? 32 : 65, 0);
        random(mine.data(), mine.size());
        if (k == ssh::Exchange::kP256) {
            mine[0] = 4;
        }
        return step();
    }
    bool agreeFinish(const uint8_t *peer, size_t size) override {
        secret.assign(32, 0);
        const size_t skip = kind == ssh::Exchange::kP256 ? 1 : 0;
        if (size != mine.size()) {
            return false;
        }
        for (size_t i = 0; i < 32; ++i) {
            secret[i] = mine[skip + i] ^ peer[skip + i];
        }
        return step();
    }
    ssh::Work agreeState() override {
        if (state == ssh::Work::kBusy && busyLeft-- == 0) {
            state = failAgree ? ssh::Work::kFailed : ssh::Work::kDone;
        }
        return state;
    }
    size_t agreePublic(uint8_t *out, size_t capacity) override {
        if (capacity < mine.size()) {
            return 0;
        }
        memcpy(out, mine.data(), mine.size());
        return mine.size();
    }
    size_t agreeSecret(uint8_t *out, size_t capacity) override {
        if (capacity < secret.size()) {
            return 0;
        }
        const size_t n = secret.size();
        memcpy(out, secret.data(), n);
        secret.assign(n, 0);
        return n;
    }
    void agreeEnd() override {
        state = ssh::Work::kIdle;
        secret.clear();
    }
    bool keys(unsigned slot, ssh::Cipher cipher, const uint8_t *key, const uint8_t *iv,
              const uint8_t *macKey) override {
        Slot &s = slots[slot];
        const bool small = cipher == ssh::Cipher::kAes128Ctr || cipher == ssh::Cipher::kAes128Gcm;
        const bool gcm = cipher == ssh::Cipher::kAes128Gcm || cipher == ssh::Cipher::kAes256Gcm;
        s.cipher = cipher;
        s.key.assign(key, key + (small ? 16 : 32));
        s.iv.assign(iv, iv + (gcm ? 12 : 16));
        s.macKey.clear();
        if (macKey) {
            s.macKey.assign(macKey, macKey + 32);
        }
        s.position = 0;
        return true;
    }
    bool ctr(unsigned slot, uint8_t *data, size_t size) override {
        if (size % 16) {
            return false;  // whole blocks only, as on the modem
        }
        Slot &s = slots[slot];
        for (size_t i = 0; i < size; ++i, ++s.position) {
            data[i] ^= s.key[s.position % s.key.size()] ^ s.iv[s.position % 16] ^
                       uint8_t(s.position) ^ uint8_t(s.position >> 8) ^ 0x5a;
        }
        return true;
    }
    bool gcm(unsigned slot, bool seal, const uint8_t nonce[ssh::kNonceSize], const uint8_t *aad,
             size_t aadSize, uint8_t *data, size_t size, uint8_t tag[ssh::kTagSize]) override {
        Slot &s = slots[slot];
        auto check = [&](uint8_t *out) {
            std::vector<uint8_t> all(s.key);
            all.insert(all.end(), nonce, nonce + ssh::kNonceSize);
            all.insert(all.end(), aad, aad + aadSize);
            all.insert(all.end(), data, data + size);
            digest(all, 0x47, out, ssh::kTagSize);
        };
        uint8_t expected[ssh::kTagSize];
        if (!seal) {
            check(expected);
            if (memcmp(expected, tag, ssh::kTagSize) != 0) {
                return false;
            }
        }
        for (size_t i = 0; i < size; ++i) {
            data[i] ^= s.key[i % s.key.size()] ^ nonce[i % ssh::kNonceSize] ^ uint8_t(i) ^ 0xa5;
        }
        if (seal) {
            check(tag);
        }
        return true;
    }
    bool mac(unsigned slot, const ssh::Slice *parts, size_t count,
             uint8_t out[ssh::kMacSize]) override {
        std::vector<uint8_t> all(slots[slot].macKey);
        for (size_t i = 0; i < count; ++i) {
            all.insert(all.end(), parts[i].data, parts[i].data + parts[i].size);
        }
        digest(all, 0x4d, out, ssh::kMacSize);
        return true;
    }
    void forget(unsigned slot) override {
        slots[slot] = Slot{};
        ++forgets;
    }

private:
    bool step() {
        state = ssh::Work::kBusy;
        busyLeft = busyPolls;
        return true;
    }
};

// A scripted SSH server. The key exchange runs by itself; session-layer messages from the
// client are queued in `messages` and the test answers with send().
class FakeServer {
public:
    std::string preamble;  // lines sent before the version line
    std::string version = "SSH-2.0-OpenSSH_9.2p1 Fake";
    std::string kexList = "curve25519-sha256,ecdh-sha2-nistp256,kex-strict-s-v00@openssh.com";
    std::string hostKeyList = "rsa-sha2-512,ssh-ed25519";
    std::string cipherList = "aes128-ctr,aes256-gcm@openssh.com";
    std::string macList = "hmac-sha2-256,hmac-sha2-256-etm@openssh.com";
    std::string compression = "none";
    bool truncateSignature = false;
    bool sendNewKeysEarly = true;  // NEWKEYS straight after the reply, as OpenSSH does
    FakeCrypto crypto;
    std::vector<uint8_t> out;  // bytes for the client
    std::deque<std::vector<uint8_t>> messages;
    std::string clientVersion, chosenKex, chosenCipher[2], chosenMac[2];
    bool strict = false, established = false, broken = false;
    uint32_t txSeq = 0, rxSeq = 0;
    unsigned kexDone = 0, newKeysSent = 0;
    std::vector<uint32_t> rxSeqAtMessage;  // client sequence number of each queued message

    bool ignoreFirst = false;  // an IGNORE message before the KEXINIT

    FakeServer() { crypto.seed = 101; }
    void start() {
        append(preamble);
        append(version + "\r\n");
        if (ignoreFirst) {
            seal({ssh::kIgnore, 0, 0, 0, 0});
        }
        sendKexInit();
    }
    void sendKexInit() {
        std::vector<uint8_t> p(1024);
        ssh::Writer w(p.data(), p.size());
        uint8_t cookie[16] = {};
        w.byte(ssh::kKexInit).raw(cookie, 16);
        w.string(kexList.c_str()).string(hostKeyList.c_str());
        w.string(cipherList.c_str()).string(cipherList.c_str());
        w.string(macList.c_str()).string(macList.c_str());
        w.string(compression.c_str()).string(compression.c_str()).string("").string("");
        w.boolean(false).u32(0);
        serverKexInit.assign(p.data(), p.data() + w.size());
        sentKexInit = true;
        seal(serverKexInit);
    }
    void send(const std::vector<uint8_t> &payload) { seal(payload); }
    // Takes bytes from the client.
    void feed(const uint8_t *data, size_t size) {
        in.insert(in.end(), data, data + size);
        if (clientVersion.empty()) {
            auto it = std::find(in.begin(), in.end(), '\n');
            if (it == in.end()) {
                return;
            }
            clientVersion.assign(in.begin(), it);
            if (!clientVersion.empty() && clientVersion.back() == '\r') {
                clientVersion.pop_back();
            }
            in.erase(in.begin(), it + 1);
        }
        std::vector<uint8_t> payload;
        while (!broken && open(payload)) {
            handle(payload);
        }
    }

private:
    std::vector<uint8_t> in, serverKexInit, clientKexInit, hostKey, serverPublic, clientPublic;
    uint8_t sessionId[32] = {};
    bool haveSession = false, sentKexInit = false;
    struct Dir {
        ssh::Cipher cipher = ssh::Cipher::kNone;
        bool etm = false;
        uint8_t nonce[12] = {};
    } tx, rx, nextTx, nextRx;
    std::vector<uint8_t> keyTx[3], keyRx[3];  // iv, key, mac

    void append(const std::string &s) { out.insert(out.end(), s.begin(), s.end()); }
    static bool gcmCipher(ssh::Cipher c) {
        return c == ssh::Cipher::kAes128Gcm || c == ssh::Cipher::kAes256Gcm;
    }
    static void bump(uint8_t *nonce) {
        for (size_t i = 12; i-- > 4;) {
            if (++nonce[i]) {
                break;
            }
        }
    }
    void seal(const std::vector<uint8_t> &payload) {
        const bool enc = tx.cipher != ssh::Cipher::kNone;
        const bool gcm = gcmCipher(tx.cipher);
        const bool clear = enc && (gcm || tx.etm);
        const size_t block = enc ? 16 : 8;
        size_t pad = block - ((clear ? 1 + payload.size() : 5 + payload.size()) % block);
        if (pad < 4) {
            pad += block;
        }
        const size_t len = 1 + payload.size() + pad;
        std::vector<uint8_t> p(4 + len, 0);
        ssh::putU32(p.data(), uint32_t(len));
        p[4] = uint8_t(pad);
        memcpy(p.data() + 5, payload.data(), payload.size());
        uint8_t seq[4];
        ssh::putU32(seq, txSeq++);
        if (gcm) {
            uint8_t tag[16];
            crypto.gcm(0, true, tx.nonce, p.data(), 4, p.data() + 4, len, tag);
            bump(tx.nonce);
            p.insert(p.end(), tag, tag + 16);
        } else if (enc) {
            uint8_t mac[32];
            if (tx.etm) {
                crypto.ctr(0, p.data() + 4, len);
                const ssh::Slice parts[] = {{seq, 4}, {p.data(), p.size()}};
                crypto.mac(0, parts, 2, mac);
            } else {
                const ssh::Slice parts[] = {{seq, 4}, {p.data(), p.size()}};
                crypto.mac(0, parts, 2, mac);
                crypto.ctr(0, p.data(), p.size());
            }
            p.insert(p.end(), mac, mac + 32);
        }
        out.insert(out.end(), p.begin(), p.end());
    }
    bool open(std::vector<uint8_t> &payload) {
        const bool enc = rx.cipher != ssh::Cipher::kNone;
        const bool gcm = gcmCipher(rx.cipher);
        const bool clear = enc && (gcm || rx.etm);
        if (in.size() < (enc && !clear ? 16u : 4u)) {
            return false;
        }
        uint32_t len;
        std::vector<uint8_t> head(in.begin(), in.begin() + 4);
        if (enc && !clear) {
            // Decrypt a copy of the first block to read the length; the stream position is
            // restored so the whole packet is decrypted once below.
            std::vector<uint8_t> first(in.begin(), in.begin() + 16);
            const uint64_t at = crypto.slots[1].position;
            crypto.ctr(1, first.data(), 16);
            crypto.slots[1].position = at;
            len = ssh::getU32(first.data());
        } else {
            len = ssh::getU32(head.data());
        }
        const size_t macSize = !enc ? 0 : gcm ? 16 : 32;
        if (in.size() < 4 + len + macSize) {
            return false;
        }
        std::vector<uint8_t> p(in.begin(), in.begin() + 4 + len);
        std::vector<uint8_t> mac(in.begin() + 4 + len, in.begin() + 4 + len + macSize);
        in.erase(in.begin(), in.begin() + 4 + len + macSize);
        uint8_t seq[4];
        const uint32_t number = rxSeq++;
        ssh::putU32(seq, number);
        if (gcm) {
            if (!crypto.gcm(1, false, rx.nonce, p.data(), 4, p.data() + 4, len, mac.data())) {
                broken = true;
                return false;
            }
            bump(rx.nonce);
        } else if (enc) {
            uint8_t expect[32];
            if (rx.etm) {
                const ssh::Slice parts[] = {{seq, 4}, {p.data(), p.size()}};
                crypto.mac(1, parts, 2, expect);
                crypto.ctr(1, p.data() + 4, len);
            } else {
                crypto.ctr(1, p.data(), p.size());
                const ssh::Slice parts[] = {{seq, 4}, {p.data(), p.size()}};
                crypto.mac(1, parts, 2, expect);
            }
            if (memcmp(expect, mac.data(), 32) != 0) {
                broken = true;
                return false;
            }
        }
        const uint8_t pad = p[4];
        payload.assign(p.begin() + 5, p.end() - pad);
        lastSeq = number;
        return true;
    }
    uint32_t lastSeq = 0;
    static std::string pick(const uint8_t *client, size_t size, const std::string &server) {
        size_t start = 0;
        for (size_t i = 0; i <= size; ++i) {
            if (i == size || client[i] == ',') {
                std::string name(reinterpret_cast<const char *>(client) + start, i - start);
                if (ssh::listHas(reinterpret_cast<const uint8_t *>(server.data()), server.size(),
                                 name.c_str())) {
                    return name;
                }
                start = i + 1;
            }
        }
        return "";
    }
    static ssh::Cipher cipherOf(const std::string &name) {
        if (name == "aes128-gcm@openssh.com") {
            return ssh::Cipher::kAes128Gcm;
        }
        if (name == "aes256-gcm@openssh.com") {
            return ssh::Cipher::kAes256Gcm;
        }
        return name == "aes128-ctr" ? ssh::Cipher::kAes128Ctr : ssh::Cipher::kAes256Ctr;
    }
    void handle(const std::vector<uint8_t> &payload) {
        ssh::Reader r(payload.data() + 1, payload.size() - 1);
        switch (payload[0]) {
            case ssh::kKexInit: {
                clientKexInit = payload;
                const uint8_t *lists[10] = {};
                size_t sizes[10] = {};
                r.skip(16);
                for (int i = 0; i < 10; ++i) {
                    r.string(lists[i], sizes[i]);
                }
                chosenKex = pick(lists[0], sizes[0], kexList);
                for (int d = 0; d < 2; ++d) {
                    chosenCipher[d] = pick(lists[2 + d], sizes[2 + d], cipherList);
                    chosenMac[d] = pick(lists[4 + d], sizes[4 + d], macList);
                }
                if (!haveSession &&
                    ssh::listHas(lists[0], sizes[0], "kex-strict-c-v00@openssh.com") &&
                    kexList.find("kex-strict-s-v00@openssh.com") != std::string::npos) {
                    strict = true;
                }
                if (!sentKexInit) {
                    sendKexInit();
                }
                return;
            }
            case ssh::kKexEcdhInit: {
                const uint8_t *q;
                size_t qn;
                r.string(q, qn);
                clientPublic.assign(q, q + qn);
                const ssh::Exchange kind = chosenKex == "ecdh-sha2-nistp256"
                                               ? ssh::Exchange::kP256
                                               : ssh::Exchange::kX25519;
                crypto.agreeBegin(kind);
                crypto.agreeState();
                serverPublic.assign(65, 0);
                serverPublic.resize(crypto.agreePublic(serverPublic.data(), 65));
                crypto.agreeFinish(clientPublic.data(), clientPublic.size());
                crypto.agreeState();
                uint8_t secret[32];
                crypto.agreeSecret(secret, 32);
                uint8_t k[40];
                ssh::Writer kw(k, sizeof k);
                kw.mpint(secret, 32);
                std::vector<uint8_t> blob(200);
                ssh::Writer hk(blob.data(), blob.size());
                uint8_t keyBytes[32];
                memset(keyBytes, 0x33, 32);
                hk.string("ssh-ed25519").string(keyBytes, 32);
                hostKey.assign(blob.data(), blob.data() + hk.size());
                // exchange hash, as the client makes it
                crypto.hashBegin();
                auto str = [&](const void *d, size_t n) {
                    uint8_t l[4];
                    ssh::putU32(l, uint32_t(n));
                    crypto.hashUpdate(l, 4);
                    crypto.hashUpdate(static_cast<const uint8_t *>(d), n);
                };
                str(clientVersion.data(), clientVersion.size());
                str(version.data(), version.size());
                str(clientKexInit.data(), clientKexInit.size());
                str(serverKexInit.data(), serverKexInit.size());
                str(hostKey.data(), hostKey.size());
                str(clientPublic.data(), clientPublic.size());
                str(serverPublic.data(), serverPublic.size());
                crypto.hashUpdate(k, kw.size());
                uint8_t h[32];
                crypto.hashFinish(h);
                if (!haveSession) {
                    memcpy(sessionId, h, 32);
                    haveSession = true;
                }
                auto derive = [&](char letter, size_t n) {
                    uint8_t d[32];
                    const uint8_t c = uint8_t(letter);
                    crypto.hashBegin();
                    crypto.hashUpdate(k, kw.size());
                    crypto.hashUpdate(h, 32);
                    crypto.hashUpdate(&c, 1);
                    crypto.hashUpdate(sessionId, 32);
                    crypto.hashFinish(d);
                    return std::vector<uint8_t>(d, d + n);
                };
                const ssh::Cipher cs = cipherOf(chosenCipher[0]), sc = cipherOf(chosenCipher[1]);
                auto keySize = [](ssh::Cipher c) {
                    return c == ssh::Cipher::kAes128Ctr || c == ssh::Cipher::kAes128Gcm ? 16u : 32u;
                };
                keyRx[0] = derive('A', gcmCipher(cs) ? 12 : 16);
                keyTx[0] = derive('B', gcmCipher(sc) ? 12 : 16);
                keyRx[1] = derive('C', keySize(cs));
                keyTx[1] = derive('D', keySize(sc));
                keyRx[2] = derive('E', 32);
                keyTx[2] = derive('F', 32);
                nextRx.cipher = cs;
                nextRx.etm = chosenMac[0] == "hmac-sha2-256-etm@openssh.com";
                nextTx.cipher = sc;
                nextTx.etm = chosenMac[1] == "hmac-sha2-256-etm@openssh.com";
                std::vector<uint8_t> reply(600);
                ssh::Writer w(reply.data(), reply.size());
                uint8_t sigBytes[64];
                memset(sigBytes, 0x44, 64);
                std::vector<uint8_t> sig(120);
                ssh::Writer sw(sig.data(), sig.size());
                sw.string("ssh-ed25519");
                if (!truncateSignature) {
                    sw.string(sigBytes, 64);
                }
                w.byte(ssh::kKexEcdhReply)
                    .string(hostKey.data(), hostKey.size())
                    .string(serverPublic.data(), serverPublic.size())
                    .string(sig.data(), sw.size());
                reply.resize(w.size());
                seal(reply);
                crypto.agreeEnd();
                if (sendNewKeysEarly) {
                    sendNewKeys();
                }
                return;
            }
            case ssh::kNewKeys:
                if (!sendNewKeysEarly && newKeysSent == kexDone) {
                    sendNewKeys();
                }
                rx = nextRx;
                crypto.keys(1, rx.cipher, keyRx[1].data(), keyRx[0].data(),
                            gcmCipher(rx.cipher) ? nullptr : keyRx[2].data());
                if (gcmCipher(rx.cipher)) {
                    memcpy(rx.nonce, keyRx[0].data(), 12);
                }
                if (strict) {
                    rxSeq = 0;
                }
                established = true;
                sentKexInit = false;
                ++kexDone;
                return;
            default:
                messages.push_back(payload);
                rxSeqAtMessage.push_back(lastSeq);
                if (payload[0] == ssh::kServiceRequest) {
                    std::vector<uint8_t> accept(17);
                    ssh::Writer(accept.data(), accept.size())
                        .byte(ssh::kServiceAccept)
                        .string("ssh-userauth");
                    seal(accept);
                }
        }
    }
    void sendNewKeys() {
        seal({ssh::kNewKeys});
        ++newKeysSent;
        tx = nextTx;
        crypto.keys(0, tx.cipher, keyTx[1].data(), keyTx[0].data(),
                    gcmCipher(tx.cipher) ? nullptr : keyTx[2].data());
        if (gcmCipher(tx.cipher)) {
            memcpy(tx.nonce, keyTx[0].data(), 12);
        }
        if (strict) {
            txSeq = 0;
        }
    }

public:
    // Server-initiated re-key: a fresh KEXINIT.
    void rekey() { sendKexInit(); }
};
}  // namespace sshfake
