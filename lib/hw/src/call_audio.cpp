#include "call_audio.h"
#include <new>
#include "catalog.h"
#include "hw.h"
#include "volume.h"

namespace hw {
bool CallAudio::preview(const char *name, const modemcfg::Profile &profile) {
    stop();
    if (!strcmp(name, "DTMF") || !strcmp(name, "PULSE")) {
        return start(calls::SoundStage::kDigits, "1234567890*#", !strcmp(name, "PULSE"), 14400,
                     profile);
    }
    if (!strcmp(name, "SWEEP")) {
        source_.reset(new (std::nothrow) tones::SweepSource(200, 4000, 4000));
    } else if (!strcmp(name, "DIAL") || !strcmp(name, "BUSY") || !strcmp(name, "RING")) {
        const auto &spec = !strcmp(name, "DIAL")   ? tones::kUkDialTone
                           : !strcmp(name, "BUSY") ? tones::kUkBusyTone
                                                   : tones::kUkRingTone;
        source_.reset(new (std::nothrow) tones::ToneSource(spec, 3000));
        sourceGain_ = volume::callToneGain();
    } else {
        char path[100];
        uint32_t speed;
        if (hwtest::parseUint(name, 300, 56000, &speed) && calls::validSpeed(speed)) {
            snprintf(path, sizeof path, "%s", sounds::handshakeAsset(speed).path);
        } else {
            if (strncmp(name, "/sounds/", 8) || strstr(name, "..") || strchr(name, '\\') ||
                strlen(name) >= sizeof path) {
                return false;
            }
            strcpy(path, name);
        }
        if (!reader_.open(path)) {
            return false;
        }
        auto *wav = new (std::nothrow) sounds::WavSource(reader_);
        if (!wav) {
            reader_.close();
            return false;
        }
        source_.reset(wav);
        wav_ = wav;
        if (!wav->begin()) {
            stop();
            return false;
        }
    }
    if (!source_ || !audioStreamBegin()) {
        stop();
        return false;
    }
    active_ = true;
    since_ = millis();
    return true;
}
bool CallAudio::start(calls::SoundStage stage, const char *digits, bool pulse, uint32_t speed,
                      const modemcfg::Profile &profile) {
    stop();
    failed_ = false;
    using calls::SoundStage;
    if (stage != SoundStage::kHandshake && !(stage == SoundStage::kDigits && pulse)) {
        sourceGain_ = volume::callToneGain();
    }
    switch (stage) {
        case SoundStage::kRing:
            source_.reset(new (std::nothrow) tones::ToneSource(tones::kUkRingTone, 0));
            break;
        case SoundStage::kDial:
            if (profile.s[6] == 0) {
                return true;
            }
            source_.reset(new (std::nothrow)
                              tones::ToneSource(tones::kUkDialTone, uint32_t(profile.s[6]) * 1000));
            break;
        case SoundStage::kDigits:
            if (pulse) {
                source_.reset(new (std::nothrow) tones::PulseSource(digits));
            } else {
                source_.reset(new (std::nothrow) tones::DtmfSource(digits, profile.s[11], 100));
            }
            break;
        case SoundStage::kBusy:
            source_.reset(new (std::nothrow) tones::ToneSource(tones::kUkBusyTone, 1500));
            break;
        case SoundStage::kCarrier:
            source_.reset(new (std::nothrow) tones::ToneSource(1800, 0, 0));
            break;
        case SoundStage::kHandshake: {
            if (!reader_.open(sounds::handshakeAsset(speed).path)) {
                return false;
            }
            auto *wav = new (std::nothrow) sounds::WavSource(reader_);
            if (!wav) {
                reader_.close();
                return false;
            }
            source_.reset(wav);
            wav_ = wav;
            if (!wav->begin()) {
                stop();
                return false;
            }
            break;
        }
    }
    if (!source_ || !audioStreamBegin()) {
        stop();
        return false;
    }
    active_ = true;
    since_ = millis();
    return true;
}
void CallAudio::stop() {
    if (active_) {
        audioStreamEnd();
    }
    source_.reset();
    reader_.close();
    wav_ = nullptr;
    active_ = draining_ = eof_ = false;
    failed_ = false;
    potRead_ = false;
    sourceGain_ = 1.0f;
    count_ = sent_ = 0;
}
void CallAudio::poll(uint32_t now, const modemcfg::Profile &profile) {
    if (!active_) {
        return;
    }
    if (draining_) {
        if (now - since_ >= 120) {
            stop();
        }
        return;
    }
    if (now - since_ < 20) {
        return;
    }  // amplifier startup, without blocking serial
    if (sent_ == count_) {
        count_ = source_->fill(samples_, 256);
        sent_ = 0;
        eof_ = count_ < 256;
        if (wav_ && wav_->failed()) {
            stop();
            failed_ = true;
            return;
        }
    }
    if (!potRead_ || now - potAt_ >= 50) {
        potLevel_ = volume::potLevel(potRaw(), kPotMax);
        potAt_ = now;
        potRead_ = true;
    }
    sent_ +=
        audioStreamWrite(samples_ + sent_, count_ - sent_,
                         sourceGain_ * volume::speakerGain(potLevel_, profile.get(modemcfg::kL)));
    if (eof_ && sent_ == count_) {
        draining_ = true;
        since_ = now;
    }
}
}  // namespace hw
