#pragma once
#include <LittleFS.h>
#include "modemcfg.h"
namespace hw {
// LittleFS rename commits one complete blob atomically, retaining the old file if
// a write fails. Used for history so a full phonebook cannot exhaust the small NVS.
class FileStore : public modemcfg::Store {
public:
    FileStore(const char *path, const char *temporary, const char *backup)
        : path_(path), temporary_(temporary), backup_(backup) {}
    size_t load(uint8_t *out, size_t cap) override {
        size_t n = read(path_, out, cap);
        primaryGood_ = n != 0;
        if (n) {
            return n;
        }
        n = read(backup_, out, cap);
        if (n) {
            return n;
        }
        // A nonempty sentinel distinguishes damaged storage from an empty history.
        if (LittleFS.exists(path_) || LittleFS.exists(backup_)) {
            out[0] = 0;
            return 1;
        }
        return 0;
    }
    bool save(const uint8_t *data, size_t len) override {
        File file = LittleFS.open(temporary_, "w");
        if (!file) {
            return false;
        }
        bool ok = file.write(data, len) == len;
        file.flush();
        file.close();
        if (!ok) {
            LittleFS.remove(temporary_);
            return false;
        }
        if (primaryGood_ && LittleFS.exists(path_) && !LittleFS.rename(path_, backup_)) {
            return false;
        }
        primaryGood_ = false;
        bool committed = LittleFS.rename(temporary_, path_);
        primaryGood_ = committed;
        return committed;
    }
    bool erase() override {
        bool ready = bool(LittleFS.open("/"));
        if (!ready) {
            return false;
        }
        bool ok = !LittleFS.exists(path_) || LittleFS.remove(path_);
        ok = (!LittleFS.exists(temporary_) || LittleFS.remove(temporary_)) && ok;
        ok = (!LittleFS.exists(backup_) || LittleFS.remove(backup_)) && ok;
        primaryGood_ = false;
        return ok;
    }

private:
    size_t read(const char *path, uint8_t *out, size_t cap) {
        File file = LittleFS.open(path, "r");
        if (!file) {
            return 0;
        }
        size_t n = file.size();
        if (n < 4 || n > cap || file.read(out, n) != n) {
            return 0;
        }
        uint32_t expected = 0;
        for (unsigned i = 0; i < 4; ++i) {
            expected |= uint32_t(out[n - 4 + i]) << (8 * i);
        }
        return modemcfg::crc32(out, n - 4) == expected ? n : 0;
    }
    const char *path_, *temporary_, *backup_;
    bool primaryGood_ = false;
};
}  // namespace hw
