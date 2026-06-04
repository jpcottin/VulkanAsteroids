#include "audio.h"
#include "common.h"
#include <oboe/Oboe.h>
#include <atomic>
#include <array>
#include <cmath>

// ── Impl ──────────────────────────────────────────────────────────────────────

struct AudioEngine::Impl : public oboe::AudioStreamDataCallback {

    static constexpr int   kSR     = 44100;
    static constexpr int   kVoices = 8;
    static constexpr float kTau    = 6.28318530f;

    enum class ST : uint8_t { LASER, EXPLOSION, HIT, CLEAR };

    struct Voice {
        ST    type   = ST::LASER;
        float t      = 0.0f;   // elapsed time [s]
        float phase  = 0.0f;   // oscillator phase [0,1) or LPF state
        bool  active = false;
    };

    std::shared_ptr<oboe::AudioStream> stream;
    std::array<Voice, kVoices>         voices{};

    // Bits: 0=LASER 1=EXPLOSION 2=HIT 3=CLEAR – written by game thread.
    std::atomic<uint32_t> pending{0};
    std::atomic<bool>     thrust{false};

    float thrustLPF = 0.0f;
    uint32_t rng = 0xDEADBEEFu;

    // ── helpers ──────────────────────────────────────────────────────────────

    float white() {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return static_cast<float>(rng & 0xFFFFu) / 32768.0f - 1.0f;
    }

    void activate(ST type) {
        for (auto& v : voices) {
            if (!v.active) { v = {type, 0.0f, 0.0f, true}; return; }
        }
        // All busy: steal the oldest (first slot).
        voices[0] = {type, 0.0f, 0.0f, true};
    }

    float genVoice(Voice& v) {
        const float dt = 1.0f / kSR;
        float s = 0.0f;

        switch (v.type) {
            case ST::LASER: {
                constexpr float dur = 0.12f;
                if (v.t > dur) { v.active = false; break; }
                float prog  = v.t / dur;
                float freq  = 1100.0f - 750.0f * prog;
                v.phase    += dt * freq;
                if (v.phase >= 1.0f) v.phase -= 1.0f;
                s = sinf(v.phase * kTau) * (1.0f - prog) * 0.42f;
                break;
            }
            case ST::EXPLOSION: {
                constexpr float dur = 0.32f;
                if (v.t > dur) { v.active = false; break; }
                float env  = expf(-v.t / 0.07f);
                // phase reused as LPF state
                v.phase    = v.phase * 0.60f + white() * 0.40f;
                s = v.phase * env * 0.62f;
                break;
            }
            case ST::HIT: {
                constexpr float dur = 0.20f;
                if (v.t > dur) { v.active = false; break; }
                float thud  = sinf(v.t * kTau * 90.0f) * expf(-v.t / 0.05f);
                float noise = white() * expf(-v.t / 0.025f);
                s = (thud * 0.6f + noise * 0.4f) * 0.50f;
                break;
            }
            case ST::CLEAR: {
                constexpr float dur     = 0.55f;
                constexpr float segDur  = dur / 3.0f;
                constexpr float freqs[3]= {523.25f, 659.25f, 783.99f};
                if (v.t > dur) { v.active = false; break; }
                int   ni   = static_cast<int>(v.t / segDur);
                if (ni > 2) ni = 2;
                float nt   = fmodf(v.t, segDur);
                float env  = sinf(nt / segDur * 3.14159265f);
                v.phase   += dt * freqs[ni];
                if (v.phase >= 1.0f) v.phase -= 1.0f;
                s = sinf(v.phase * kTau) * env * 0.45f;
                break;
            }
        }
        v.t += dt;
        return s;
    }

    // ── Oboe callback ─────────────────────────────────────────────────────────

    oboe::DataCallbackResult onAudioReady(
            oboe::AudioStream*, void* data, int32_t frames) override {

        float* out = static_cast<float*>(data);

        uint32_t mask = pending.exchange(0, std::memory_order_relaxed);
        if (mask & 1u) activate(ST::LASER);
        if (mask & 2u) activate(ST::EXPLOSION);
        if (mask & 4u) activate(ST::HIT);
        if (mask & 8u) activate(ST::CLEAR);

        const bool thr = thrust.load(std::memory_order_relaxed);

        for (int i = 0; i < frames; i++) {
            float s = 0.0f;

            for (auto& v : voices)
                if (v.active) s += genVoice(v);

            // Thrust rumble: filtered noise while held, decays silently when released.
            if (thr) {
                thrustLPF = thrustLPF * 0.82f + white() * 0.18f;
                s += thrustLPF * 0.35f;
            } else {
                thrustLPF *= 0.97f; // exponential decay, no fresh noise
            }

            // Hard limit
            out[i] = s >  1.0f ?  1.0f :
                     s < -1.0f ? -1.0f : s;
        }
        return oboe::DataCallbackResult::Continue;
    }

    // ── stream lifecycle ──────────────────────────────────────────────────────

    bool open() {
        oboe::AudioStreamBuilder builder;
        builder.setDirection(oboe::Direction::Output)
               ->setFormat(oboe::AudioFormat::Float)
               ->setChannelCount(oboe::ChannelCount::Mono)
               ->setSampleRate(kSR)
               ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
               ->setSharingMode(oboe::SharingMode::Exclusive)
               ->setDataCallback(this);

        oboe::Result r = builder.openStream(stream);
        if (r != oboe::Result::OK) {
            LOGW("Oboe openStream: %s", oboe::convertToText(r));
            return false;
        }
        stream->requestStart();
        return true;
    }

    void close() {
        if (stream) {
            stream->requestStop();
            stream->close();
            stream.reset();
        }
    }
};

// ── AudioEngine public API ────────────────────────────────────────────────────

AudioEngine::AudioEngine()  : impl_(new Impl) {}
AudioEngine::~AudioEngine() { shutdown(); delete impl_; }

bool AudioEngine::init()     { return impl_->open(); }
void AudioEngine::shutdown() { impl_->close(); }

void AudioEngine::triggerLaser()     { impl_->pending.fetch_or(1u, std::memory_order_relaxed); }
void AudioEngine::triggerExplosion() { impl_->pending.fetch_or(2u, std::memory_order_relaxed); }
void AudioEngine::triggerPlayerHit() { impl_->pending.fetch_or(4u, std::memory_order_relaxed); }
void AudioEngine::triggerLevelClear(){ impl_->pending.fetch_or(8u, std::memory_order_relaxed); }
void AudioEngine::setThrust(bool a)  { impl_->thrust.store(a,  std::memory_order_relaxed); }
