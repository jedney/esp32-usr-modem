#include "call_audio.h"
#include <new>
#include "hw.h"
#include "volume.h"

namespace hw {
bool CallAudio::start(calls::SoundStage stage, const char *digits, bool pulse, uint32_t speed,
                      const modemcfg::Profile &profile) {
    stop();
    failed_ = false;
    using calls::SoundStage;
    switch (stage) {
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
            char path[48];
            snprintf(path, sizeof path, "/sounds/handshake-%u.wav",
                     unsigned(sounds::handshakeSpeed(speed)));
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
    float level = volume::potLevel(potRaw(), kPotMax);
    sent_ += audioStreamWrite(samples_ + sent_, count_ - sent_,
                              volume::speakerGain(level, profile.get(modemcfg::kL)));
    if (eof_ && sent_ == count_) {
        draining_ = true;
        since_ = now;
    }
}
}  // namespace hw
