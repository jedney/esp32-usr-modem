// Console output kept in RAM: the newest bytes written to the maintenance console since
// boot, in a ring. The network console replays it after log-in and then reads the live
// output from it, each client at its own place, so a slow client never holds up a writer:
// when it falls a whole ring behind, the oldest bytes are gone and it is told how many.
//
// Writers can be on other tasks (ESP-IDF log output), so every call takes the Lock, if
// one is given. No Arduino dependencies.
#pragma once

#include <cstddef>
#include <cstdint>

namespace netconsole {

class ConsoleLog {
public:
    class Lock {
    public:
        virtual ~Lock() = default;
        virtual void lock() = 0;
        virtual void unlock() = 0;
    };

    // `storage` is kept; without it nothing is kept and nothing can be read.
    void begin(uint8_t *storage, size_t size, Lock *lock = nullptr);
    void append(const void *data, size_t len);
    // Bytes ever written, and the oldest of them still held (positions count from boot).
    uint64_t end() const;
    uint64_t start() const;
    size_t capacity() const { return size_; }
    // Copies up to `cap` bytes from position `from` and moves `from` past them. If `from`
    // is older than start(), it moves to start() first and `skipped` says by how much.
    size_t read(uint64_t &from, uint8_t *out, size_t cap, uint64_t &skipped) const;

private:
    class Hold {
    public:
        explicit Hold(Lock *lock) : lock_(lock) {
            if (lock_) {
                lock_->lock();
            }
        }
        ~Hold() {
            if (lock_) {
                lock_->unlock();
            }
        }
        Hold(const Hold &) = delete;
        Hold &operator=(const Hold &) = delete;

    private:
        Lock *lock_;
    };
    uint64_t startLocked() const { return total_ > size_ ? total_ - size_ : 0; }
    uint8_t *bytes_ = nullptr;
    size_t size_ = 0;
    uint64_t total_ = 0;
    Lock *lock_ = nullptr;
};

}  // namespace netconsole
