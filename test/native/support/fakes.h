// Fakes shared by the PC unit tests (TG-16). One of each, so a suite cannot drift from the
// others, and every one can fail on demand so the failure paths are testable.
//
//   fakes::MemoryStore    modemcfg::Store held in memory      failSave, failLoad, failErase,
//                                                              failNextSaves
//   fakes::TextSink       atcmd::Sink that collects the text   take(), text
//   fakes::RecordingSound calls::CallSound                     fail (start refuses), broken
//                                                              (failed() reports a fault)
//
// Include it from a suite as "../support/fakes.h". Each fake is behind its own macro, so a
// suite only pulls in the library headers it needs: define FAKES_STORE, FAKES_SINK and/or
// FAKES_SOUND before the include.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#ifdef FAKES_STORE
#include "modemcfg.h"
namespace fakes {
class MemoryStore : public modemcfg::Store {
public:
    std::vector<uint8_t> bytes;
    bool failSave = false;       // save() refuses
    bool failLoad = false;       // load() reports an unreadable store (kLoadError)
    bool failErase = false;      // erase() refuses
    unsigned failNextSaves = 0;  // the next n save() calls refuse, then saving works again
    unsigned loads = 0, saves = 0, erases = 0;
    size_t load(uint8_t *out, size_t cap) override {
        ++loads;
        if (failLoad) {
            return modemcfg::kLoadError;
        }
        const size_t n = std::min(bytes.size(), cap);
        if (n) {
            memcpy(out, bytes.data(), n);
        }
        return n;
    }
    bool save(const uint8_t *in, size_t n) override {
        if (failSave || failNextSaves) {
            if (failNextSaves) {
                --failNextSaves;
            }
            return false;
        }
        bytes.assign(in, in + n);
        ++saves;
        return true;
    }
    bool erase() override {
        ++erases;
        if (failErase) {
            return false;
        }
        bytes.clear();
        return true;
    }
    bool contains(const char *text) const {
        return std::string(bytes.begin(), bytes.end()).find(text) != std::string::npos;
    }
};
}  // namespace fakes
#endif

#ifdef FAKES_SINK
#include "atcmd.h"
namespace fakes {
class TextSink : public atcmd::Sink {
public:
    std::string text;
    void write(const char *data, size_t n) override { text.append(data, n); }
    std::string take() {
        std::string result = text;
        text.clear();
        return result;
    }
};
}  // namespace fakes
#endif

#ifdef FAKES_SOUND
#include "outgoing.h"
namespace fakes {
class RecordingSound : public calls::CallSound {
public:
    std::vector<calls::SoundStage> stages;
    std::string digits;
    bool pulse = false;
    bool active = false;
    bool fail = false;    // start() refuses: the sound cannot be played
    bool broken = false;  // failed() reports a fault while a sound runs
    unsigned starts = 0, stops = 0;
    bool start(calls::SoundStage stage, const char *d, bool p, uint32_t,
               const modemcfg::Profile &) override {
        ++starts;
        stages.push_back(stage);
        digits = d ? d : "";
        pulse = p;
        active = !fail;
        return !fail;
    }
    bool running() const override { return active; }
    bool failed() const override { return broken; }
    void stop() override {
        ++stops;
        active = false;
    }
    void poll(uint32_t, const modemcfg::Profile &) override {}
};
}  // namespace fakes
#endif
