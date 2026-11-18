/*
   Copyright 2018-2026 Bo Zimmerman

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/
// Changed for USR Modem: incremental X/Y state machines, bounded queues, injected
// source, discard-only receives, deadlines; adapted from proto_xmodem at a534e512.
// ZMODEM framing/CRC adaptation from proto_zmodem (Synchronet):
// (C) Mattheij Computer Service 1994; rights transferred to Stephen Hurd, 2015.
// Original ZMODEM notices and transfer-of-rights text: https://github.com/bozimmerman/Zimodem.
#include "transfer.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace clienttest {
uint16_t crc16(const uint8_t *data, size_t len, uint16_t crc) {
    while (len--) {
        crc ^= uint16_t(*data++) << 8;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1;
    }
    return crc;
}
uint32_t crc32(const uint8_t *data, size_t len, uint32_t crc) {
    while (len--) {
        crc ^= *data++;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    return crc;
}
bool Transfer::start(Protocol protocol, bool sending, Source *source, uint32_t now) {
    if (sending && (!source || !source->count() || source->count() > 16))
        return false;
    if (sending) {
        if (protocol < Protocol::Y && source->count() != 1)
            return false;
        uint64_t total = 0;
        for (unsigned i = 0; i < source->count(); ++i)
            if (source->size(i) > kLimit || strlen(source->name(i)) > 12)
                return false;
            else
                total += source->size(i);
        if (total > kLimit)
            return false;
    }
    protocol_ = protocol;
    sending_ = sending;
    source_ = source;
    stats_ = {};
    tx_.clear();
    rx_.clear();
    now_ = start_ = activity_ = now;
    offset_ = file_ = used_ = need_ = attempts_ = can_ = state_ = 0;
    sequence_ = protocol == Protocol::Y ? 0 : 1;
    crc_ = protocol != Protocol::Xsum;
    blockSize_ = protocol == Protocol::X1k || protocol == Protocol::Y ? 1024 : 128;
    knownSize_ = metadata_ = final_ = zescaped_ = zreceiving_ = false;
    afterEot_ = lastEofValid_ = false;
    finAt_ = 0;
    zparse_ = zformat_ = ztype_ = zend_ = zcrcBytes_ = ztrailer_ = 0;
    nibble_ = -1;
    fileSize_ = 0;
    highwater_ = 0;
    if (protocol_ == Protocol::Z)
        zheader(sending ? 0 : 1, sending ? 0 : 0x23000400u);  // 1 KiB, duplex/overlap, CRC32
    else if (!sending)
        put(crc_ ? 'C' : 21);
    return true;
}
void Transfer::put(uint8_t b) {
    if (!tx_.put(b))
        finish(Outcome::Error);
}
size_t Transfer::read(uint8_t *out, size_t cap) {
    size_t n = tx_.read(out, cap);
    stats_.wire += n;
    if (n)
        activity_ = now_;
    return n;
}
size_t Transfer::write(const uint8_t *data, size_t len) {
    size_t n = 0;
    while (n < len && running() && rx_.put(data[n]))
        ++n;
    // A byte alone does not count as activity: noise could then hold the transfer open
    // until the 30 minute limit. The protocol code calls alive() for real progress.
    pump();
    return n;
}
void Transfer::pump() {
    uint8_t b = 0;
    while (running() && !tx_.size() && rx_.read(&b, 1))
        byte(b);
}
void Transfer::alive() {
    activity_ = now_;
}
void Transfer::byte(uint8_t b) {
    // CAN within a block/subpacket is data. Cancellation requires consecutive CAN
    // outside payload; ZDLE escaping has its own decoder.
    if (protocol_ != Protocol::Z) {
        if (!used_ && b == 24) {
            if (++can_ >= 2)
                cancel();
            return;
        }
        can_ = 0;
        xbyte(b);
    } else
        zbyte(b);
}
void Transfer::finish(Outcome outcome) {
    stats_.outcome = outcome;
    stats_.elapsed = now_ - start_;
}
void Transfer::cancel(Outcome reason) {
    tx_.clear();
    for (unsigned i = 0; i < 8; ++i)
        put(24);
    finish(reason);
}
void Transfer::poll(uint32_t now) {
    if (static_cast<int32_t>(now - start_) < 0)
        return;
    now_ = now;
    stats_.elapsed = now - start_;
    if (!running())
        return;
    pump();
    if (now - start_ >= 30 * 60 * 1000u) {
        cancel(Outcome::Timeout);
        return;
    }
    // The sender's "OO" after ZFIN is optional: the files are in, so stop after a short wait.
    if (protocol_ == Protocol::Z && !sending_ && state_ == 5 && now - finAt_ >= kFinWaitMs) {
        finish(Outcome::Success);
        return;
    }
    if (now - activity_ >= 5000) {
        activity_ = now;
        retry();
    }
}
void Transfer::retry() {
    ++stats_.retries;
    if (++attempts_ > 10) {
        cancel(Outcome::Timeout);
        return;
    }
    if (tx_.size())
        return;  // blocked physical output also has a bounded retry budget
    used_ = need_ = 0;
    if (protocol_ == Protocol::Z) {
        zparse_ = 0;
        zescaped_ = false;
        if (sending_) {
            if (!state_)
                zheader(0);
            else if (state_ == 1)
                zsendfile();
            else if (state_ == 4)
                zheader(8);
            else
                zsenddata();
        } else {
            zreceiving_ = false;
            zheader(state_ ? 9 : 1, state_ ? offset_ : 0x23000400u);
        }
    } else if (!sending_)
        put(sequence_ == 0 || (!offset_ && crc_) ? 'C' : 21);
    else if (state_ == 2)
        put(4);
    else if (state_ == 1)
        xsend(metadata_, final_);
}
void Transfer::accepted(const uint8_t *data, size_t len) {
    stats_.crc = crc32(data, len, stats_.crc);
    stats_.bytes += len;
    attempts_ = 0;
}
bool Transfer::metadata(size_t len) {
    const auto *end = static_cast<const uint8_t *>(memchr(buffer_, 0, len));
    if (!end || end - buffer_ > 128)
        return false;
    if (end == buffer_) {
        final_ = true;
        return true;
    }
    // Ignore all peer names; no received file is ever opened or created.
    const uint8_t *p = end + 1;
    uint32_t size = 0;
    bool digit = false;
    while (p < buffer_ + len && *p >= '0' && *p <= '9') {
        digit = true;
        if (size > kLimit / 10)
            return false;
        size = size * 10 + *p++ - '0';
        if (size > kLimit)
            return false;
    }
    if (!digit || p == buffer_ + len || (*p && *p != ' '))
        return false;
    fileSize_ = size;
    knownSize_ = true;
    offset_ = 0;
    if (stats_.files >= 16)
        return false;
    return true;
}
void Transfer::xsend(bool meta, bool last) {
    metadata_ = meta;
    final_ = last;
    size_t size = meta ? 128 : blockSize_;
    memset(buffer_, meta ? 0 : 26, size);
    payload_ = 0;
    if (meta && !last) {
        snprintf(reinterpret_cast<char *>(buffer_), size, "%s", source_->name(file_));
        size_t pos = strlen(reinterpret_cast<char *>(buffer_)) + 1;
        snprintf(reinterpret_cast<char *>(buffer_) + pos, size - pos, "%lu",
                 static_cast<unsigned long>(source_->size(file_)));
    } else if (!meta) {
        payload_ = std::min<size_t>(size, source_->size(file_) - offset_);
        if (source_->read(file_, offset_, buffer_, payload_) != payload_) {
            cancel(Outcome::Error);
            return;
        }
    }
    put(size == 1024 ? 2 : 1);
    put(sequence_);
    put(255 - sequence_);
    uint8_t sum = 0;
    for (size_t i = 0; i < size; ++i) {
        put(buffer_[i]);
        sum += buffer_[i];
    }
    if (crc_) {
        uint16_t crc = crc16(buffer_, size);
        put(crc >> 8);
        put(crc);
    } else
        put(sum);
    state_ = 1;
}
void Transfer::xnext() {
    if (offset_ >= source_->size(file_)) {
        put(4);
        state_ = 2;
    } else
        xsend();
}
void Transfer::xbyte(uint8_t b) {
#ifdef CLIENTTEST_TRACE
    if (!used_)
        fprintf(stderr, "X %s state=%u byte=%02X seq=%u offset=%u\n", sending_ ? "send" : "recv",
                state_, b, sequence_, offset_);
#endif
    if (sending_) {
        if ((state_ == 0 && (b == 'C' || b == 21)) || (state_ == 3 && b == 'C') ||
            (state_ == 4 && b == 'C') || ((state_ == 1 || state_ == 2) && (b == 6 || b == 21)))
            alive();
        if (state_ == 0 && (b == 'C' || b == 21)) {
            crc_ = b == 'C';
            xsend(protocol_ == Protocol::Y);
        } else if (state_ == 3 && b == 'C') {
            sequence_ = 1;
            xnext();
        } else if (b == 6 && state_ == 1) {
            attempts_ = 0;
            if (metadata_) {
                if (final_)
                    finish(Outcome::Success);
                else
                    state_ = 3;
            } else {
                accepted(buffer_, payload_);
                offset_ += payload_;
                ++sequence_;
                xnext();
            }
        } else if (state_ == 2 && b == 6) {
            ++stats_.files;
            if (protocol_ != Protocol::Y)
                finish(Outcome::Success);
            else {
                ++file_;
                offset_ = 0;
                highwater_ = 0;
                sequence_ = 0;
                state_ = 4;
            }
        } else if (state_ == 4 && b == 'C')
            xsend(true, file_ >= source_->count());
        else if (b == 21 && (state_ == 1 || state_ == 2))
            retry();
        return;
    }
    if (!used_) {
        if (b == 4 || b == 1 || b == 2)
            alive();
        if (b == 4) {
            if (protocol_ == Protocol::Y && afterEot_) {
                put(6);
                put('C');
                return;
            }
            if (knownSize_ && offset_ != fileSize_) {
                cancel(Outcome::Error);
                return;
            }
            put(6);
            ++stats_.files;
            afterEot_ = true;
            if (protocol_ == Protocol::Y) {
                sequence_ = 0;
                knownSize_ = false;
                offset_ = 0;
                put('C');
            } else
                finish(Outcome::Success);
        } else if (b == 1 || b == 2) {
            blockSize_ = b == 1 ? 128 : 1024;
            need_ = 2 + blockSize_ + (crc_ ? 2 : 1);
            used_ = 1;  // sentinel: buffer begins with sequence and its complement
        }
        return;
    }
    alive();  // inside a block: slow links must not be retried
    buffer_[used_++ - 1] = b;
    if (used_ - 1 == need_)
        xframe();
}
void Transfer::xframe() {
    used_ = 0;
    uint8_t seq = buffer_[0];
    bool valid = uint8_t(seq + buffer_[1]) == 255;
    const uint8_t *data = buffer_ + 2;
    if (crc_)
        valid &= crc16(data, blockSize_) ==
                 uint16_t(buffer_[blockSize_ + 2] << 8 | buffer_[blockSize_ + 3]);
    else {
        uint8_t sum = 0;
        for (size_t i = 0; i < blockSize_; ++i)
            sum += data[i];
        valid &= sum == buffer_[blockSize_ + 2];
    }
    if (!valid || (seq != sequence_ && seq != uint8_t(sequence_ - 1))) {
        ++stats_.errors;
        retry();
        return;
    }
    if (seq == uint8_t(sequence_ - 1)) {
        put(6);
        if (protocol_ == Protocol::Y && seq == 0)
            put('C');
        return;
    }
    if (protocol_ == Protocol::Y && seq == 0 && !knownSize_) {
        afterEot_ = false;
        memmove(buffer_, data, blockSize_);
        if (!metadata(blockSize_)) {
            cancel(Outcome::Error);
            return;
        }
        put(6);
        if (final_)
            finish(Outcome::Success);
        else {
            ++sequence_;
            put('C');
        }
        return;
    }
    size_t len = knownSize_ ? std::min<uint32_t>(blockSize_, fileSize_ - offset_) : blockSize_;
    if (offset_ + len > kLimit || stats_.bytes + len > kLimit) {
        cancel(Outcome::Error);
        return;
    }
    accepted(data, len);
    offset_ += len;
    ++sequence_;
    put(6);
}

// ZMODEM: send CRC16 hex headers and CRC16 data; accept binary16/binary32/hex
// headers and data. Advertise a 1 KiB receive buffer, no CRC32 requirement.
void Transfer::zescape(uint8_t b) {
    if ((b & 0x60) == 0 || b == 0x7f || b == 0xff) {
        put(24);
        put(b == 0x7f ? 'l' : b == 0xff ? 'm' : b ^ 0x40);
    } else
        put(b);
}
void Transfer::zheader(uint8_t type, uint32_t value) {
    uint8_t data[7] = {
        type, uint8_t(value), uint8_t(value >> 8), uint8_t(value >> 16), uint8_t(value >> 24), 0,
        0};
    uint16_t crc = crc16(data, 5);
    data[5] = crc >> 8;
    data[6] = crc;
    put('*');
    put('*');
    put(24);
    put('B');
    const char *hex = "0123456789abcdef";
    for (uint8_t b : data) {
        put(hex[b >> 4]);
        put(hex[b & 15]);
    }
    put(13);
    put(10);
    if (type != 3 && type != 8)
        put(17);
}
void Transfer::zpacket(const uint8_t *data, size_t len, uint8_t end) {
    for (size_t i = 0; i < len; ++i)
        zescape(data[i]);
    put(24);
    put(end);
    uint16_t crc = crc16(&end, 1, crc16(data, len));
    zescape(crc >> 8);
    zescape(crc);
    if (end == 'k')
        put(17);
}
void Transfer::zsendfile() {
    zheader(4, 0x01000000u);  // ZCBIN
    memset(buffer_, 0, sizeof buffer_);
    snprintf(reinterpret_cast<char *>(buffer_), 128, "%s", source_->name(file_));
    size_t pos = strlen(reinterpret_cast<char *>(buffer_)) + 1;
    snprintf(reinterpret_cast<char *>(buffer_) + pos, 128 - pos, "%lu 0 0 0",
             static_cast<unsigned long>(source_->size(file_)));
    zpacket(buffer_, pos + strlen(reinterpret_cast<char *>(buffer_) + pos) + 1, 'k');
    state_ = 1;
}
void Transfer::zsenddata() {
    if (offset_ == source_->size(file_)) {
        zheader(11, offset_);
        state_ = 3;
        return;
    }
    payload_ = std::min<uint32_t>(1024, source_->size(file_) - offset_);
    if (source_->read(file_, offset_, buffer_, payload_) != payload_) {
        cancel(Outcome::Error);
        return;
    }
    zheader(10, offset_);
    zpacket(buffer_, payload_, 'k');  // bounded stop-and-wait, interoperable ZCRCW
    state_ = 2;
}
void Transfer::zbyte(uint8_t b) {
    if (ztrailer_) {
        if (ztrailer_ == 2 && b == 13) {
            ztrailer_ = 1;
            return;
        }
        ztrailer_ = 0;
        if (b == 10 || b == 0x8a)
            return;
    }
    if (b == 24) {
        if (++can_ >= 5) {
            cancel();
            return;
        }
    } else
        can_ = 0;
    if (state_ == 5 && !sending_) {
        if (b == 'O') {
            alive();
            if (++used_ == 2)
                finish(Outcome::Success);
        }
        return;
    }
    if (!zreceiving_ && zparse_ < 3) {
        if (zparse_ == 0) {
            if (b == '*') {
                zparse_ = 1;
                alive();
            }
        } else if (zparse_ == 1) {
            if (b == 24) {
                zparse_ = 2;
                alive();
            } else if (b != '*')
                zparse_ = 0;
        } else {
            if (b == 'A' || b == 'B' || b == 'C') {
                alive();
                zformat_ = b;
                z32_ = b == 'C';
                used_ = 0;
                nibble_ = -1;
                zescaped_ = false;
                zparse_ = 3;
            } else
                zparse_ = 0;
        }
        return;
    }
    if (!zreceiving_ && zformat_ == 'B') {
        int n = b >= '0' && b <= '9' ? b - '0' : b >= 'a' && b <= 'f' ? b - 'a' + 10 : -1;
        if (n < 0) {
            zparse_ = 0;
            return;
        }
        alive();
        if (nibble_ < 0) {
            nibble_ = n;
            return;
        }
        b = uint8_t(nibble_ * 16 + n);
        nibble_ = -1;
        zdecoded(b);
        return;
    }
    if (zescaped_) {
        zescaped_ = false;
        if (zreceiving_ && !zend_ && b >= 'h' && b <= 'k') {
            zend_ = b;
            zcrcBytes_ = z32_ ? 4 : 2;
            need_ = used_ + zcrcBytes_;
            return;
        }
        b = b == 'l' ? 0x7f : b == 'm' ? 0xff : b ^ 0x40;
    } else {
        if (b == 17 || b == 19 || b == 0x91 || b == 0x93)
            return;
        if (b == 24) {
            zescaped_ = true;
            return;
        }
    }
    alive();
    zdecoded(b);
}
void Transfer::zdecoded(uint8_t b) {
    if (zreceiving_) {
        if (used_ >= sizeof buffer_) {
            ++stats_.errors;
            zreceiving_ = false;
            zparse_ = 0;
            zheader(9, offset_);
            return;
        }
        buffer_[used_++] = b;
        if (zend_ && used_ == need_)
            zdata();
        return;
    }
    header_[used_++] = b;
    size_t len = z32_ ? 9 : 7;
    if (used_ != len)
        return;
    bool valid = z32_ ? crc32(header_, len) == 0xdebb20e3u : crc16(header_, len) == 0;
    zparse_ = 0;
    used_ = 0;
    if (!valid) {
        ++stats_.errors;
        zheader(6);
        return;
    }
    ztype_ = header_[0];
    zposition_ = uint32_t(header_[1]) | uint32_t(header_[2]) << 8 | uint32_t(header_[3]) << 16 |
                 uint32_t(header_[4]) << 24;
    zhandle();
}
void Transfer::zhandle() {
#ifdef CLIENTTEST_TRACE
    fprintf(stderr, "Z %s state=%u type=%u pos=%u offset=%u\n", sending_ ? "send" : "recv", state_,
            ztype_, zposition_, offset_);
#endif
    if (ztype_ == 7 || ztype_ == 12 || ztype_ == 16) {
        cancel(Outcome::Error);
        return;
    }
    if (sending_) {
        if (ztype_ == 1 && (state_ == 0 || state_ == 3)) {
            attempts_ = 0;
            if (state_ == 3) {
                ++stats_.files;
                ++file_;
                offset_ = 0;
            }
            if (file_ >= source_->count()) {
                state_ = 4;
                zheader(8);
            } else
                zsendfile();
        } else if (ztype_ == 9) {
            if (zposition_ > source_->size(file_)) {
                cancel(Outcome::Error);
                return;
            }
            if (zposition_ != offset_)
                ++stats_.retries;
            if (state_ == 2 && zposition_ == offset_ + payload_)
                zacknowledge(zposition_);
            // Successful bytes are counted only once after acknowledged data.
            if (state_ == 1)
                highwater_ = zposition_;
            offset_ = zposition_;
            zsenddata();
        } else if (ztype_ == 3 && state_ == 2 && zposition_ == offset_ + payload_) {
            zacknowledge(zposition_);
            offset_ += payload_;
            zsenddata();
        } else if (ztype_ == 8 && state_ == 4) {
            put('O');
            put('O');
            finish(Outcome::Success);
        } else if (ztype_ == 6)
            retry();
        return;
    }
    if (ztype_ == 0)
        zheader(1, 0x23000400u);
    else if (ztype_ == 4 || ztype_ == 2 || ztype_ == 10) {
        if (ztype_ == 10 && (!state_ || zposition_ != offset_)) {
            zheader(9, offset_);
            return;
        }
        zreceiving_ = true;
        ztrailer_ = zformat_ == 'B' ? 2 : 0;
        zend_ = 0;
        used_ = need_ = 0;
    } else if (ztype_ == 11) {
        if (!state_ && lastEofValid_ && zposition_ == lastEof_) {
            zheader(1, 0x23000400u);
            return;
        }
        if (state_ && zposition_ == offset_ && (!knownSize_ || fileSize_ == offset_)) {
            lastEof_ = offset_;
            lastEofValid_ = true;
            ++stats_.files;
            state_ = 0;
            offset_ = 0;
            zheader(1, 0x23000400u);
        } else
            zheader(9, offset_);
    } else if (ztype_ == 8) {
        zheader(8);
        state_ = 5;
        used_ = 0;
        finAt_ = now_;
    } else
        zheader(6);  // no remote commands or unsupported extensions
}
void Transfer::zacknowledge(uint32_t end) {
    if (end > highwater_) {
        size_t skip = highwater_ > offset_ ? highwater_ - offset_ : 0;
        accepted(buffer_ + skip, payload_ - skip);
        highwater_ = end;
    }
}
void Transfer::zdata() {
    size_t len = used_ - zcrcBytes_;
    bool valid;
    if (z32_) {
        uint32_t crc = crc32(&zend_, 1, crc32(buffer_, len));
        valid = crc32(buffer_ + len, 4, crc) == 0xdebb20e3u;
    } else {
        uint16_t crc = crc16(&zend_, 1, crc16(buffer_, len));
        valid = crc16(buffer_ + len, 2, crc) == 0;
    }
    uint8_t end = zend_;
    zreceiving_ = end == 'i' || end == 'j';
    used_ = need_ = zend_ = 0;
    if (!valid) {
        ++stats_.errors;
        zreceiving_ = false;
        zparse_ = 0;
        retry();
        return;
    }
    if (ztype_ == 4) {
        if (!metadata(len) || final_) {
            cancel(Outcome::Error);
            return;
        }
        state_ = 1;
        zheader(9, 0);
    } else if (ztype_ == 2)
        zheader(3);
    else if (ztype_ == 10) {
        if (offset_ + len > kLimit || stats_.bytes + len > kLimit ||
            (knownSize_ && offset_ + len > fileSize_)) {
            cancel(Outcome::Error);
            return;
        }
        accepted(buffer_, len);
        offset_ += len;
        if (end == 'j' || end == 'k')
            zheader(3, offset_);
    }
    if (!zreceiving_)
        zparse_ = 0;
}
}  // namespace clienttest
