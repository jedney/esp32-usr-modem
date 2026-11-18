#include "ssh_transport.h"
#include <cstring>

#ifndef FW_VERSION
#define FW_VERSION "0.0"
#endif

namespace ssh {
const char *const kKexNames[] = {"curve25519-sha256", "curve25519-sha256@libssh.org",
                                 "ecdh-sha2-nistp256"};
const size_t kKexCount = sizeof kKexNames / sizeof kKexNames[0];
// Names only: the key is trusted without a check, so any type will do.
const char *const kHostKeyNames[] = {"ssh-ed25519",
                                     "ecdsa-sha2-nistp256",
                                     "ecdsa-sha2-nistp384",
                                     "ecdsa-sha2-nistp521",
                                     "rsa-sha2-512",
                                     "rsa-sha2-256",
                                     "ssh-rsa"};
const size_t kHostKeyCount = sizeof kHostKeyNames / sizeof kHostKeyNames[0];
const char *const kCipherNames[] = {"aes128-gcm@openssh.com", "aes256-gcm@openssh.com",
                                    "aes128-ctr", "aes256-ctr"};
const size_t kCipherCount = sizeof kCipherNames / sizeof kCipherNames[0];
const char *const kMacNames[] = {"hmac-sha2-256-etm@openssh.com", "hmac-sha2-256"};
const size_t kMacCount = sizeof kMacNames / sizeof kMacNames[0];

namespace {
const char kVersion[] = "SSH-2.0-USRModem_" FW_VERSION;
const char kStrictClient[] = "kex-strict-c-v00@openssh.com";
const char kStrictServer[] = "kex-strict-s-v00@openssh.com";
constexpr Cipher kCiphers[] = {Cipher::kAes128Gcm, Cipher::kAes256Gcm, Cipher::kAes128Ctr,
                               Cipher::kAes256Ctr};

bool gcm(Cipher c) {
    return c == Cipher::kAes128Gcm || c == Cipher::kAes256Gcm;
}
void nameList(Writer &w, const char *const *names, size_t count, const char *extra) {
    size_t size = 0;
    for (size_t i = 0; i < count; ++i) {
        size += strlen(names[i]) + (i ? 1 : 0);
    }
    if (extra) {
        size += strlen(extra) + 1;
    }
    w.u32(uint32_t(size));
    for (size_t i = 0; i < count; ++i) {
        if (i) {
            w.byte(',');
        }
        w.raw(names[i], strlen(names[i]));
    }
    if (extra) {
        w.byte(',').raw(extra, strlen(extra));
    }
}
bool same(const uint8_t *a, const uint8_t *b, size_t size) {
    uint8_t diff = 0;
    for (size_t i = 0; i < size; ++i) {
        diff |= a[i] ^ b[i];
    }
    return diff == 0;
}
void nextNonce(uint8_t nonce[kNonceSize]) {
    for (size_t i = kNonceSize; i-- > 4;) {
        if (++nonce[i]) {
            break;
        }
    }
}
}  // namespace

void Transport::begin() {
    inStart_ = inEnd_ = packetSize_ = outLen_ = 0;
    firstBlock_ = false;
    serverVersion_[0] = 0;
    versionLines_ = 0;
    versionDone_ = false;
    clientKexInitSize_ = serverKexInitSize_ = hostKeySize_ = 0;
    clientPublicSize_ = serverPublicSize_ = 0;
    haveSessionId_ = false;
    for (unsigned slot = 0; slot < 2; ++slot) {
        now_[slot] = Keys{};
        next_[slot] = Keys{};
        crypto_.forget(slot);
    }
    kex_ = Kex::kIdle;
    sentKexInit_ = ignoreNext_ = strict_ = false;
    established_ = failed_ = held_ = false;
    txSeq_ = rxSeq_ = 0;
    bytes_ = 0;
    reason_ = "";
    memcpy(out_, kVersion, sizeof kVersion - 1);
    memcpy(out_ + sizeof kVersion - 1, "\r\n", 2);
    outLen_ = sizeof kVersion + 1;
    if (sendKexInit()) {
        kex_ = Kex::kWaitInit;
    }
}
void Transport::end() {
    crypto_.agreeEnd();
    crypto_.forget(0);
    crypto_.forget(1);
    wipeKex();
    wipe(sessionId_, sizeof sessionId_);
    held_ = false;
    if (!failed_) {
        failed_ = true;
        reason_ = "";
    }
}
void Transport::fail(const char *reason) {
    if (!failed_) {
        failed_ = true;
        reason_ = reason;
    }
}
void Transport::wipeKex() {
    wipe(next_, sizeof next_);
}

size_t Transport::inSpace() const {
    if (failed_) {
        return 0;
    }
    return held_ ? kInSize - inEnd_ : kInSize - (inEnd_ - inStart_);
}
size_t Transport::receive(const uint8_t *data, size_t size) {
    if (failed_) {
        return 0;
    }
    if (!held_ && inStart_ && kInSize - inEnd_ < size) {
        memmove(in_, in_ + inStart_, inEnd_ - inStart_);
        inEnd_ -= inStart_;
        inStart_ = 0;
    }
    size_t n = size < kInSize - inEnd_ ? size : kInSize - inEnd_;
    memcpy(in_ + inEnd_, data, n);
    inEnd_ += n;
    return n;
}
void Transport::sent(size_t size) {
    if (size > outLen_) {
        size = outLen_;
    }
    memmove(out_, out_ + size, outLen_ - size);
    outLen_ -= size;
}

bool Transport::readVersion() {
    for (;;) {
        const uint8_t *start = in_ + inStart_;
        const size_t avail = inEnd_ - inStart_;
        const void *found = memchr(start, '\n', avail);
        if (!found) {
            if (avail >= 1024) {
                fail("SSH: no SSH greeting");
            }
            return false;
        }
        const size_t lineSize = static_cast<const uint8_t *>(found) - start;
        size_t textSize = lineSize;
        if (textSize && start[textSize - 1] == '\r') {
            --textSize;
        }
        inStart_ += lineSize + 1;
        if (textSize >= 4 && memcmp(start, "SSH-", 4) == 0) {
            const bool v2 = (textSize >= 8 && memcmp(start, "SSH-2.0-", 8) == 0) ||
                            (textSize >= 9 && memcmp(start, "SSH-1.99-", 9) == 0);
            if (!v2 || textSize >= sizeof serverVersion_) {
                fail("SSH: unsupported SSH version");
                return false;
            }
            memcpy(serverVersion_, start, textSize);
            serverVersion_[textSize] = 0;
            versionDone_ = true;
            return true;
        }
        if (++versionLines_ > 50) {
            fail("SSH: no SSH greeting");
            return false;
        }
    }
}

bool Transport::seal(const uint8_t *payload, size_t size) {
    const Keys &k = now_[0];
    const bool encrypted = k.cipher != Cipher::kNone;
    const bool clearLength = encrypted && (gcm(k.cipher) || k.etm);
    const size_t block = encrypted ? 16 : 8;
    size_t pad = block - ((clearLength ? 1 + size : 5 + size) % block);
    if (pad < 4) {
        pad += block;
    }
    const size_t len = 1 + size + pad;
    const size_t macSize = !encrypted ? 0 : gcm(k.cipher) ? kTagSize : kMacSize;
    const size_t total = 4 + len + macSize;
    if (kOutSize - outLen_ < total || len > kMaxPacket) {
        return false;
    }
    uint8_t *p = out_ + outLen_;
    putU32(p, uint32_t(len));
    p[4] = uint8_t(pad);
    memcpy(p + 5, payload, size);
    crypto_.random(p + 5 + size, pad);
    bool ok = true;
    if (gcm(k.cipher)) {
        ok = crypto_.gcm(0, true, txNonce_, p, 4, p + 4, len, p + 4 + len);
        nextNonce(txNonce_);
    } else if (encrypted) {
        uint8_t seq[4];
        putU32(seq, txSeq_);
        if (k.etm) {
            ok = crypto_.ctr(0, p + 4, len);
            const Slice parts[] = {{seq, 4}, {p, 4 + len}};
            ok = ok && crypto_.mac(0, parts, 2, p + 4 + len);
        } else {
            const Slice parts[] = {{seq, 4}, {p, 4 + len}};
            ok = crypto_.mac(0, parts, 2, p + 4 + len) && crypto_.ctr(0, p, 4 + len);
        }
    }
    if (!ok) {
        wipe(p, total);
        fail("SSH: encryption failed");
        return false;
    }
    ++txSeq_;
    outLen_ += total;
    bytes_ += total;
    return true;
}

bool Transport::openPacket(const uint8_t *&payload, size_t &size) {
    uint8_t *p = in_ + inStart_;
    const size_t avail = inEnd_ - inStart_;
    const Keys &k = now_[1];
    const bool encrypted = k.cipher != Cipher::kNone;
    const bool clearLength = encrypted && (gcm(k.cipher) || k.etm);
    if (encrypted && !clearLength) {
        if (avail < 16) {
            return false;
        }
        if (!firstBlock_) {
            if (!crypto_.ctr(1, p, 16)) {
                fail("SSH: decryption failed");
                return false;
            }
            firstBlock_ = true;
        }
    } else if (avail < 4) {
        return false;
    }
    const uint32_t len = getU32(p);
    const size_t block = encrypted ? 16 : 8;
    if (len < 5 || len > kMaxPacket || (clearLength ? len : len + 4) % block != 0) {
        fail("SSH: bad packet");
        return false;
    }
    const size_t macSize = !encrypted ? 0 : gcm(k.cipher) ? kTagSize : kMacSize;
    const size_t total = 4 + len + macSize;
    if (avail < total) {
        return false;
    }
    uint8_t seq[4];
    putU32(seq, rxSeq_);
    if (gcm(k.cipher)) {
        const bool ok = crypto_.gcm(1, false, rxNonce_, p, 4, p + 4, len, p + 4 + len);
        nextNonce(rxNonce_);
        if (!ok) {
            fail("SSH: damaged packet");
            return false;
        }
    } else if (encrypted) {
        uint8_t mac[kMacSize];
        const Slice parts[] = {{seq, 4}, {p, 4 + len}};
        if (k.etm) {
            if (!crypto_.mac(1, parts, 2, mac) || !same(mac, p + 4 + len, kMacSize) ||
                !crypto_.ctr(1, p + 4, len)) {
                fail("SSH: damaged packet");
                return false;
            }
        } else {
            firstBlock_ = false;
            if (!crypto_.ctr(1, p + 16, 4 + len - 16) || !crypto_.mac(1, parts, 2, mac) ||
                !same(mac, p + 4 + len, kMacSize)) {
                fail("SSH: damaged packet");
                return false;
            }
        }
    }
    const uint8_t pad = p[4];
    if (pad < 4 || size_t(pad) + 1 >= len) {
        fail("SSH: bad packet");
        return false;
    }
    payload = p + 5;
    size = len - pad - 1;
    packetSize_ = total;
    ++rxSeq_;
    bytes_ += total;
    return true;
}
void Transport::consume() {
    inStart_ += packetSize_;
    packetSize_ = 0;
    if (inStart_ == inEnd_) {
        inStart_ = inEnd_ = 0;
    }
}

bool Transport::sendKexInit() {
    Writer w(clientKexInit_, sizeof clientKexInit_);
    uint8_t cookie[16];
    crypto_.random(cookie, sizeof cookie);
    w.byte(kKexInit).raw(cookie, sizeof cookie);
    nameList(w, kKexNames, kKexCount, kStrictClient);
    nameList(w, kHostKeyNames, kHostKeyCount, nullptr);
    nameList(w, kCipherNames, kCipherCount, nullptr);
    nameList(w, kCipherNames, kCipherCount, nullptr);
    nameList(w, kMacNames, kMacCount, nullptr);
    nameList(w, kMacNames, kMacCount, nullptr);
    w.string("none").string("none").string("").string("").boolean(false).u32(0);
    if (!w.ok() || !seal(clientKexInit_, w.size())) {
        return false;
    }
    clientKexInitSize_ = w.size();
    sentKexInit_ = true;
    return true;
}

bool Transport::serverKexInit(const uint8_t *payload, size_t size) {
    if (kex_ != Kex::kIdle && kex_ != Kex::kWaitInit) {
        fail("SSH: unexpected key exchange");
        return false;
    }
    if (size > kMaxKexInit) {
        fail("SSH: key exchange offer too long");
        return false;
    }
    Reader r(payload, size);
    uint8_t type = 0;
    const uint8_t *lists[10] = {};
    size_t sizes[10] = {};
    r.byte(type);
    r.skip(16);
    for (size_t i = 0; i < 10; ++i) {
        r.string(lists[i], sizes[i]);
    }
    bool follows = false;
    uint32_t reserved = 0;
    r.boolean(follows);
    r.u32(reserved);
    if (!r.ok()) {
        fail("SSH: bad key exchange offer");
        return false;
    }
    memcpy(serverKexInit_, payload, size);
    serverKexInitSize_ = size;
    if (!haveSessionId_ && listHas(lists[0], sizes[0], kStrictServer)) {
        strict_ = true;
        if (rxSeq_ != 1) {  // the server's KEXINIT must be its first packet
            fail("SSH: unexpected message before key exchange");
            return false;
        }
    }
    const int kex = choose(kKexNames, kKexCount, lists[0], sizes[0]);
    const int host = choose(kHostKeyNames, kHostKeyCount, lists[1], sizes[1]);
    const int cipher[2] = {choose(kCipherNames, kCipherCount, lists[2], sizes[2]),
                           choose(kCipherNames, kCipherCount, lists[3], sizes[3])};
    const int mac[2] = {choose(kMacNames, kMacCount, lists[4], sizes[4]),
                        choose(kMacNames, kMacCount, lists[5], sizes[5])};
    if (kex < 0) {
        fail("SSH: no common key exchange");
        return false;
    }
    if (host < 0) {
        fail("SSH: no common host key type");
        return false;
    }
    if (cipher[0] < 0 || cipher[1] < 0) {
        fail("SSH: no common cipher");
        return false;
    }
    for (unsigned slot = 0; slot < 2; ++slot) {
        Keys &keys = next_[slot];
        keys.cipher = kCiphers[cipher[slot]];
        keys.mac = !gcm(keys.cipher);
        if (keys.mac && mac[slot] < 0) {
            fail("SSH: no common MAC");
            return false;
        }
        keys.etm = keys.mac && mac[slot] == 0;
    }
    if (!listHas(lists[6], sizes[6], "none") || !listHas(lists[7], sizes[7], "none")) {
        fail("SSH: no common compression");
        return false;
    }
    if (follows) {
        const uint8_t *name;
        size_t length;
        bool right = firstName(lists[0], sizes[0], name, length) &&
                     length == strlen(kKexNames[kex]) && memcmp(name, kKexNames[kex], length) == 0;
        right = right && firstName(lists[1], sizes[1], name, length) &&
                length == strlen(kHostKeyNames[host]) &&
                memcmp(name, kHostKeyNames[host], length) == 0;
        ignoreNext_ = !right;
    }
    exchange_ = kex < 2 ? Exchange::kX25519 : Exchange::kP256;
    if (!sentKexInit_ && !sendKexInit()) {
        fail("SSH: output full");
        return false;
    }
    if (!crypto_.agreeBegin(exchange_)) {
        fail("SSH: key exchange failed");
        return false;
    }
    kex_ = Kex::kMakeKey;
    return true;
}

bool Transport::kexReply(const uint8_t *payload, size_t size) {
    if (kex_ != Kex::kWaitReply) {
        fail("SSH: unexpected key exchange message");
        return false;
    }
    Reader r(payload + 1, size - 1);
    const uint8_t *hostKey, *serverPublic, *signature;
    size_t hostKeySize, serverPublicSize, signatureSize;
    r.string(hostKey, hostKeySize);
    r.string(serverPublic, serverPublicSize);
    r.string(signature, signatureSize);
    // Shape only: the host key and the signature are not checked (hobby use).
    Reader key(hostKey, r.ok() ? hostKeySize : 0);
    Reader sig(signature, r.ok() ? signatureSize : 0);
    key.skipString();
    sig.skipString();
    sig.skipString();
    const size_t expected = exchange_ == Exchange::kX25519 ? 32 : 65;
    if (!r.ok() || !key.ok() || !sig.ok() || hostKeySize > kMaxHostKey ||
        serverPublicSize != expected || (exchange_ == Exchange::kP256 && serverPublic[0] != 4)) {
        fail("SSH: bad key exchange reply");
        return false;
    }
    memcpy(hostKey_, hostKey, hostKeySize);
    hostKeySize_ = hostKeySize;
    memcpy(serverPublic_, serverPublic, serverPublicSize);
    serverPublicSize_ = serverPublicSize;
    if (!crypto_.agreeFinish(serverPublic_, serverPublicSize_)) {
        fail("SSH: key exchange failed");
        return false;
    }
    kex_ = Kex::kMakeSecret;
    return true;
}

bool Transport::derive(char letter, const uint8_t *k, size_t kSize, const uint8_t *hash,
                       uint8_t *out, size_t size) {
    // RFC 4253 7.2. Every key and IV here is at most one SHA-256 output long.
    uint8_t digest[kHashSize];
    const uint8_t c = uint8_t(letter);
    const bool ok = crypto_.hashBegin() && crypto_.hashUpdate(k, kSize) &&
                    crypto_.hashUpdate(hash, kHashSize) && crypto_.hashUpdate(&c, 1) &&
                    crypto_.hashUpdate(sessionId_, kHashSize) && crypto_.hashFinish(digest);
    memcpy(out, digest, size);
    wipe(digest, sizeof digest);
    return ok;
}

bool Transport::finishKex() {
    uint8_t secret[66];
    const size_t secretSize = crypto_.agreeSecret(secret, sizeof secret);
    crypto_.agreeEnd();
    uint8_t any = 0;
    for (size_t i = 0; i < secretSize; ++i) {
        any |= secret[i];
    }
    if (secretSize != 32 || !any) {
        wipe(secret, sizeof secret);
        fail("SSH: key exchange failed");
        return false;
    }
    uint8_t k[40];
    Writer kw(k, sizeof k);
    kw.mpint(secret, secretSize);
    wipe(secret, sizeof secret);
    const size_t kSize = kw.size();

    uint8_t hash[kHashSize];
    uint8_t length[4];
    bool ok = crypto_.hashBegin();
    auto string = [&](const void *data, size_t size) {
        putU32(length, uint32_t(size));
        ok = ok && crypto_.hashUpdate(length, 4) &&
             crypto_.hashUpdate(static_cast<const uint8_t *>(data), size);
    };
    string(kVersion, sizeof kVersion - 1);
    string(serverVersion_, strlen(serverVersion_));
    string(clientKexInit_, clientKexInitSize_);
    string(serverKexInit_, serverKexInitSize_);
    string(hostKey_, hostKeySize_);
    string(clientPublic_, clientPublicSize_);
    string(serverPublic_, serverPublicSize_);
    ok = ok && crypto_.hashUpdate(k, kSize) && crypto_.hashFinish(hash);
    if (ok && !haveSessionId_) {
        memcpy(sessionId_, hash, kHashSize);
        haveSessionId_ = true;
    }
    static const char kLetters[2][3] = {{'A', 'C', 'E'}, {'B', 'D', 'F'}};
    for (unsigned slot = 0; slot < 2 && ok; ++slot) {
        // derive() writes the new keys into it
        // cppcheck-suppress constVariableReference
        Keys &keys = next_[slot];
        const bool small = keys.cipher == Cipher::kAes128Ctr || keys.cipher == Cipher::kAes128Gcm;
        ok = derive(kLetters[slot][0], k, kSize, hash, keys.iv, gcm(keys.cipher) ? 12 : 16) &&
             derive(kLetters[slot][1], k, kSize, hash, keys.key, small ? 16 : 32) &&
             (!keys.mac || derive(kLetters[slot][2], k, kSize, hash, keys.macKey, kMacSize));
    }
    wipe(k, sizeof k);
    wipe(hash, sizeof hash);
    const uint8_t newKeys = kNewKeys;
    if (!ok) {
        fail("SSH: key exchange failed");
        return false;
    }
    if (!seal(&newKeys, 1)) {
        fail("SSH: output full");
        return false;
    }
    install(0, next_[0]);
    if (strict_) {
        txSeq_ = 0;
    }
    kex_ = Kex::kWaitNewKeys;
    return true;
}

void Transport::install(unsigned slot, Keys &keys) {
    if (!crypto_.keys(slot, keys.cipher, keys.key, keys.iv, keys.mac ? keys.macKey : nullptr)) {
        fail("SSH: key exchange failed");
    }
    if (gcm(keys.cipher)) {
        memcpy(slot ? rxNonce_ : txNonce_, keys.iv, kNonceSize);
    }
    now_[slot].cipher = keys.cipher;
    now_[slot].etm = keys.etm;
    now_[slot].mac = keys.mac;
    wipe(&keys, sizeof keys);
}

bool Transport::transportMessage(const uint8_t *payload, size_t size) {
    const uint8_t type = payload[0];
    switch (type) {
        case kDisconnect:
            fail("SSH: disconnected by the server");
            return true;
        case kIgnore:
        case kDebug:
        case kUnimplemented:
            if (strict_ && !established_) {
                fail("SSH: unexpected message during key exchange");
            }
            return true;
        case kExtInfo:
            return true;
        case kKexInit:
            serverKexInit(payload, size);
            return true;
        case kKexEcdhReply:
            kexReply(payload, size);
            return true;
        case kNewKeys:
            if (kex_ != Kex::kWaitNewKeys) {
                fail("SSH: unexpected key exchange message");
                return true;
            }
            install(1, next_[1]);
            if (strict_) {
                rxSeq_ = 0;
            }
            kex_ = Kex::kIdle;
            sentKexInit_ = false;
            established_ = true;
            bytes_ = 0;
            return true;
        default:
            break;
    }
    if (type >= kKexInit && type < kUserauthRequest) {
        fail("SSH: unexpected key exchange message");
        return true;
    }
    if (type < kKexInit && type != kServiceRequest && type != kServiceAccept) {
        if (strict_ && !established_) {
            fail("SSH: unexpected message during key exchange");
            return true;
        }
        uint8_t reply[5];
        Writer(reply, sizeof reply).byte(kUnimplemented).u32(rxSeq_ - 1);
        seal(reply, sizeof reply);
        return true;
    }
    return false;
}

void Transport::poll() {
    if (failed_) {
        return;
    }
    if (kex_ == Kex::kMakeKey || kex_ == Kex::kMakeSecret) {
        const Work state = crypto_.agreeState();
        if (state == Work::kBusy || state == Work::kIdle) {
            return;
        }
        if (state == Work::kFailed) {
            fail("SSH: key exchange failed");
            return;
        }
        if (kex_ == Kex::kMakeSecret) {
            finishKex();
            return;
        }
        clientPublicSize_ = crypto_.agreePublic(clientPublic_, sizeof clientPublic_);
        if (clientPublicSize_ != (exchange_ == Exchange::kX25519 ? 32u : 65u)) {
            fail("SSH: key exchange failed");
            return;
        }
        uint8_t init[1 + 4 + 65];
        Writer w(init, sizeof init);
        w.byte(kKexEcdhInit).string(clientPublic_, clientPublicSize_);
        if (seal(init, w.size())) {
            kex_ = Kex::kWaitReply;
        }
        return;
    }
    if (established_ && kex_ == Kex::kIdle && bytes_ >= kRekeyBytes && sendKexInit()) {
        kex_ = Kex::kWaitInit;
    }
}

bool Transport::next(const uint8_t *&payload, size_t &size) {
    while (!held_ && !failed_) {
        if (!versionDone_) {
            if (!readVersion()) {
                return false;
            }
            continue;
        }
        if (kex_ == Kex::kMakeSecret) {
            return false;  // later packets may need the keys being made
        }
        if (!openPacket(payload, size)) {
            return false;
        }
        if (size == 0) {
            fail("SSH: bad packet");
            return false;
        }
        const uint8_t type = payload[0];
        if (ignoreNext_ && type >= kKexEcdhInit && type < kUserauthRequest) {
            ignoreNext_ = false;
            consume();
            continue;
        }
        if (transportMessage(payload, size)) {
            consume();
            continue;
        }
        if (!established_) {
            fail("SSH: unexpected message before key exchange");
            return false;
        }
        held_ = true;
        return true;
    }
    return false;
}
void Transport::done() {
    if (held_) {
        held_ = false;
        consume();
    }
}
bool Transport::canSend(size_t size) const {
    return established_ && !failed_ && kex_ == Kex::kIdle && size + 80 <= kOutSize - outLen_;
}
bool Transport::send(const uint8_t *payload, size_t size) {
    return canSend(size) && seal(payload, size);
}
}  // namespace ssh
