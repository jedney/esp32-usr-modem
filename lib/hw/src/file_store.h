#pragma once
#include <LittleFS.h>
#include <sys/stat.h>
#include <cstdio>
#include "modemcfg.h"
namespace hw {
// True if a LittleFS file exists. LittleFS.exists() and open() print a core error
// on the console for a missing file; stat() on the mounted path does not.
inline bool fileExists(const char *path) {
    char full[96];
    struct stat info;
    return snprintf(full, sizeof full, "/littlefs%s", path) < int(sizeof full) &&
           stat(full, &info) == 0;
}
// LittleFS rename commits one complete blob atomically, retaining the old file if
// a write fails. Used for history so a full phonebook cannot exhaust the small NVS.
class FileStore : public modemcfg::Store {
public:
    FileStore(const char *path, const char *temporary, const char *backup)
        : path_(path), temporary_(temporary), backup_(backup) {}
    size_t load(uint8_t *out, size_t cap) override {
        // The file, then the temporary file (the newest blob if power was lost between the two
        // renames of save()), then the backup. Each must be complete and CRC-valid.
        modemcfg::Slot used = modemcfg::Slot::kFile;
        size_t n = modemcfg::recoverBlob(readSlot, this, out, cap, &used);
        primaryGood_ = n != 0 && used == modemcfg::Slot::kFile;
        if (n && used == modemcfg::Slot::kTemporary) {
            // Make the recovered blob the file again, so the next save() cannot overwrite the
            // only complete copy of it before its own commit.
            primaryGood_ = LittleFS.rename(temporary_, path_);
        }
        if (n) {
            return n;
        }
        // A nonempty sentinel distinguishes damaged storage from an empty history.
        if (fileExists(path_) || fileExists(backup_)) {
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
        if (primaryGood_ && fileExists(path_) && !LittleFS.rename(path_, backup_)) {
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
            return true;  // not mounted: there is nothing to erase
        }
        bool ok = !fileExists(path_) || LittleFS.remove(path_);
        ok = (!fileExists(temporary_) || LittleFS.remove(temporary_)) && ok;
        ok = (!fileExists(backup_) || LittleFS.remove(backup_)) && ok;
        primaryGood_ = false;
        return ok;
    }

private:
    static size_t readSlot(void *context, modemcfg::Slot slot, uint8_t *out, size_t cap) {
        auto *self = static_cast<FileStore *>(context);
        return self->read(slot == modemcfg::Slot::kFile        ? self->path_
                          : slot == modemcfg::Slot::kTemporary ? self->temporary_
                                                               : self->backup_,
                          out, cap);
    }
    size_t read(const char *path, uint8_t *out, size_t cap) {
        if (!fileExists(path)) {
            return 0;
        }
        File file = LittleFS.open(path, "r");
        if (!file) {
            return 0;
        }
        size_t n = file.size();
        if (n < 4 || n > cap || file.read(out, n) != n) {
            return 0;
        }
        return modemcfg::crcSealed(out, n) ? n : 0;
    }
    const char *path_, *temporary_, *backup_;
    bool primaryGood_ = false;
};
}  // namespace hw
