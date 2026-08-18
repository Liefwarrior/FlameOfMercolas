// See mixer.hpp. Float DSP, client-side only, never included by the sim.

#include "granadad/audio/mixer.hpp"

#include <algorithm>
#include <cmath>

namespace granadad::audio {

namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr float kTwoPi = 2.0F * kPi;

/// Per-sample phase increment for a swell LFO at `hz`.
[[nodiscard]] constexpr float phaseStep(float hz) noexcept {
    return kTwoPi * hz / static_cast<float>(kSampleRate);
}

[[nodiscard]] float clamp01(float v) noexcept {
    return std::clamp(v, 0.0F, 1.0F);
}

/// Per-sample ramp increment that reaches `target` from `current` in rampSec.
[[nodiscard]] float rampStep(float current, float target, float rampSec) noexcept {
    if (rampSec <= 0.0F) {
        return target - current;  // one sample: effectively a snap
    }
    return (target - current) / (rampSec * static_cast<float>(kSampleRate));
}

}  // namespace

Mixer::Mixer() {
    busGain_.fill(1.0F);
    voices_.reserve(kMaxVoices);
}

float Mixer::noise() noexcept {
    // xorshift32 — cheap, stateful, and seeded by the CLIENT (wall clock or a
    // test constant), never by anything the sim owns.
    noiseState_ ^= noiseState_ << 13;
    noiseState_ ^= noiseState_ >> 17;
    noiseState_ ^= noiseState_ << 5;
    return static_cast<float>(static_cast<std::int32_t>(noiseState_)) *
           (1.0F / 2147483648.0F);
}

float Mixer::procSample(std::size_t layer) noexcept {
    Proc& p = proc_[layer];
    switch (static_cast<ProcLayer>(layer)) {
        case ProcLayer::WaterLap: {
            // Brown-ish noise (leaky random walk), low-passed, with a slow
            // two-sine swell at incommensurate rates so the lapping never
            // settles into a loop the ear can catch.
            p.brown = p.brown * 0.997F + 0.015F * noise();
            p.lp += 0.030F * (p.brown - p.lp);
            p.phaseA += phaseStep(0.13F);
            p.phaseB += phaseStep(0.047F);
            if (p.phaseA > kTwoPi) { p.phaseA -= kTwoPi; }
            if (p.phaseB > kTwoPi) { p.phaseB -= kTwoPi; }
            const float swell =
                0.60F + 0.40F * std::sin(p.phaseA) * std::sin(p.phaseB);
            return p.lp * swell * 5.0F;
        }
        case ProcLayer::Wind: {
            // Low-passed white noise under a slower gust swell.
            p.lp += 0.020F * (noise() - p.lp);
            p.phaseA += phaseStep(0.050F);
            p.phaseB += phaseStep(0.021F);
            if (p.phaseA > kTwoPi) { p.phaseA -= kTwoPi; }
            if (p.phaseB > kTwoPi) { p.phaseB -= kTwoPi; }
            const float gust =
                0.55F + 0.45F * std::sin(p.phaseA) * std::sin(p.phaseB);
            return p.lp * gust * 6.0F;
        }
    }
    return 0.0F;
}

Mixer::VoiceId Mixer::play(std::shared_ptr<const Sample> sample, Bus bus,
                           float gain, float pan, float pitch, bool loop) {
    if (!sample || sample->mono.empty()) {
        return kNoVoice;
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    if (voices_.size() >= kMaxVoices) {
        // Steal the oldest non-loop voice; bed layers are not sacrificed to a
        // flurry of footsteps.
        const auto victim = std::find_if(voices_.begin(), voices_.end(),
                                         [](const Voice& v) { return !v.loop; });
        if (victim != voices_.end()) {
            voices_.erase(victim);
        } else {
            return kNoVoice;
        }
    }
    Voice v;
    v.sample = std::move(sample);
    v.step = static_cast<double>(std::clamp(pitch, 0.25F, 4.0F));
    v.bus = bus;
    v.loop = loop;
    v.id = nextId_++;
    if (nextId_ == kNoVoice) {
        ++nextId_;
    }
    const float clampedGain = std::clamp(gain, 0.0F, 4.0F);
    if (loop) {
        // Loops fade in — they are beds, and a bed that pops on is a defect.
        v.gain = 0.0F;
        v.targetGain = clampedGain;
        v.gainStep = rampStep(0.0F, clampedGain, 0.05F);
    } else {
        v.gain = clampedGain;
        v.targetGain = clampedGain;
    }
    // Constant-power pan.
    const float angle = (std::clamp(pan, -1.0F, 1.0F) + 1.0F) * 0.25F * kPi;
    v.panL = std::cos(angle);
    v.panR = std::sin(angle);
    voices_.push_back(std::move(v));
    return voices_.back().id;
}

void Mixer::stop(VoiceId voice, float fadeSec) {
    if (voice == kNoVoice) {
        return;
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    for (Voice& v : voices_) {
        if (v.id == voice) {
            v.stopping = true;
            v.targetGain = 0.0F;
            v.gainStep = rampStep(v.gain, 0.0F, fadeSec);
            return;
        }
    }
}

void Mixer::setVoiceGain(VoiceId voice, float gain, float rampSec) {
    if (voice == kNoVoice) {
        return;
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    for (Voice& v : voices_) {
        if (v.id == voice) {
            v.targetGain = std::clamp(gain, 0.0F, 4.0F);
            v.gainStep = rampStep(v.gain, v.targetGain, rampSec);
            return;
        }
    }
}

void Mixer::setBusGain(Bus bus, float gain) {
    const std::lock_guard<std::mutex> lock(mutex_);
    busGain_[busIndex(bus)] = std::clamp(gain, 0.0F, 2.0F);
}

float Mixer::busGain(Bus bus) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return busGain_[busIndex(bus)];
}

void Mixer::setProceduralGain(ProcLayer layer, float target, float rampSec) {
    const std::lock_guard<std::mutex> lock(mutex_);
    Proc& p = proc_[static_cast<std::size_t>(layer)];
    p.target = clamp01(target);
    p.step = rampStep(p.gain, p.target, rampSec);
}

float Mixer::proceduralGain(ProcLayer layer) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return proc_[static_cast<std::size_t>(layer)].gain;
}

int Mixer::activeVoices() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(voices_.size());
}

void Mixer::setNoiseSeed(std::uint32_t seed) {
    const std::lock_guard<std::mutex> lock(mutex_);
    noiseState_ = seed == 0 ? 0x9E3779B9U : seed;
}

void Mixer::render(float* out, int frames) {
    if (frames <= 0) {
        return;
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t sampleCount = static_cast<std::size_t>(frames) * 2U;
    std::fill(out, out + sampleCount, 0.0F);

    const float ambientBus = busGain_[busIndex(Bus::Ambient)];
    const float master = busGain_[busIndex(Bus::Master)];

    // Voices.
    for (Voice& v : voices_) {
        const std::vector<float>& mono = v.sample->mono;
        const std::size_t n = mono.size();
        const float bus = busGain_[busIndex(v.bus)];
        for (int f = 0; f < frames; ++f) {
            if (v.gainStep != 0.0F) {
                v.gain += v.gainStep;
                const bool arrived = (v.gainStep > 0.0F) ? (v.gain >= v.targetGain)
                                                         : (v.gain <= v.targetGain);
                if (arrived) {
                    v.gain = v.targetGain;
                    v.gainStep = 0.0F;
                }
            }
            const std::size_t i0 = static_cast<std::size_t>(v.pos);
            if (i0 >= n) {
                break;  // finished mid-block (non-loop)
            }
            const float frac = static_cast<float>(v.pos - static_cast<double>(i0));
            const std::size_t i1 = (i0 + 1 < n) ? i0 + 1 : (v.loop ? 0 : i0);
            const float s =
                (mono[i0] * (1.0F - frac) + mono[i1] * frac) * v.gain * bus;
            out[static_cast<std::size_t>(f) * 2U] += s * v.panL;
            out[static_cast<std::size_t>(f) * 2U + 1U] += s * v.panR;
            v.pos += v.step;
            if (v.pos >= static_cast<double>(n)) {
                if (v.loop) {
                    v.pos -= static_cast<double>(n);
                } else {
                    v.pos = static_cast<double>(n);  // marks it finished
                    break;
                }
            }
        }
    }

    // Reap: finished one-shots, and stopped voices that reached silence.
    voices_.erase(
        std::remove_if(voices_.begin(), voices_.end(),
                       [](const Voice& v) {
                           if (v.stopping && v.gain <= 0.0F) {
                               return true;
                           }
                           return !v.loop &&
                                  v.pos >= static_cast<double>(v.sample->mono.size());
                       }),
        voices_.end());

    // Procedural ambience layers.
    for (std::size_t layer = 0; layer < kProcLayerCount; ++layer) {
        Proc& p = proc_[layer];
        if (p.gain <= 0.0F && p.target <= 0.0F && p.step == 0.0F) {
            continue;
        }
        for (int f = 0; f < frames; ++f) {
            if (p.step != 0.0F) {
                p.gain += p.step;
                const bool arrived = (p.step > 0.0F) ? (p.gain >= p.target)
                                                     : (p.gain <= p.target);
                if (arrived) {
                    p.gain = p.target;
                    p.step = 0.0F;
                }
            }
            const float s = procSample(layer) * p.gain * ambientBus;
            out[static_cast<std::size_t>(f) * 2U] += s;
            out[static_cast<std::size_t>(f) * 2U + 1U] += s;
        }
    }

    // Master + soft clip. tanh saturates a brawl's pile-up instead of wrapping.
    for (std::size_t i = 0; i < sampleCount; ++i) {
        out[i] = std::tanh(out[i] * master);
    }
}

}  // namespace granadad::audio
