// Incremental X/Y/ZMODEM adaptation; provenance and original notices in credits/zimodem.
#pragma once
#include "byte_queue.h"

namespace clienttest {
enum class Protocol : uint8_t { Xsum, Xcrc, X1k, Y, Z };
enum class Outcome : uint8_t { Running, Success, Cancelled, Timeout, Error };
struct Statistics {
    uint32_t bytes = 0, wire = 0, retries = 0, errors = 0, files = 0, elapsed = 0;
    uint32_t crc = 0xffffffffu;
    Outcome outcome = Outcome::Running;
};
class Source {
public:
    virtual ~Source() = default;
    virtual unsigned count() const = 0;
    virtual const char *name(unsigned file) const = 0;
    virtual uint32_t size(unsigned file) const = 0;
    virtual size_t read(unsigned file, uint32_t offset, uint8_t *out, size_t cap) = 0;
};
uint16_t crc16(const uint8_t *data, size_t len, uint16_t crc = 0);
uint32_t crc32(const uint8_t *data, size_t len, uint32_t crc = 0xffffffffu);
class Transfer {
public:
    static constexpr uint32_t kLimit = 16 * 1024 * 1024;
    bool start(Protocol protocol, bool sending, Source *source, uint32_t now);
    void poll(uint32_t now);
    size_t write(const uint8_t *data, size_t len);
    size_t read(uint8_t *out, size_t cap);
    size_t space() const { return running() ? rx_.space() : 0; }
    bool running() const { return stats_.outcome == Outcome::Running; }
    bool pending() const { return tx_.size() != 0; }
    const Statistics &statistics() const { return stats_; }
    void cancel(Outcome reason = Outcome::Cancelled);

private:
    void byte(uint8_t b);
    void pump();
    void finish(Outcome outcome);
    void retry();
    void put(uint8_t b);
    void xbyte(uint8_t b);
    void xsend(bool metadata = false, bool last = false);
    void xframe();
    void xnext();
    void zbyte(uint8_t b);
    void zdecoded(uint8_t b);
    void zheader(uint8_t type, uint32_t value = 0);
    void zpacket(const uint8_t *data, size_t len, uint8_t end);
    void zescape(uint8_t b);
    void zhandle();
    void zdata();
    void zsendfile();
    void zsenddata();
    bool metadata(size_t len);
    void accepted(const uint8_t *data, size_t len);
    void zacknowledge(uint32_t end);
    Protocol protocol_ = Protocol::Xcrc;
    bool sending_ = false, crc_ = true;
    Source *source_ = nullptr;
    Statistics stats_;
    calls::ByteQueue<4096> tx_;
    calls::ByteQueue<4096> rx_;
    uint8_t buffer_[2048] = {}, header_[9] = {};
    size_t used_ = 0, need_ = 0, blockSize_ = 128, payload_ = 0;
    uint8_t sequence_ = 1, state_ = 0, can_ = 0;
    unsigned file_ = 0, attempts_ = 0;
    uint32_t now_ = 0, start_ = 0, activity_ = 0, offset_ = 0, fileSize_ = 0;
    bool knownSize_ = false, metadata_ = false, final_ = false;
    bool afterEot_ = false, lastEofValid_ = false;
    uint8_t zparse_ = 0, zformat_ = 0, ztype_ = 0, zend_ = 0, zcrcBytes_ = 0, ztrailer_ = 0;
    bool zescaped_ = false, z32_ = false, zreceiving_ = false;
    int nibble_ = -1;
    uint32_t zposition_ = 0;
    uint32_t highwater_ = 0;
    uint32_t lastEof_ = 0;
};
}  // namespace clienttest
