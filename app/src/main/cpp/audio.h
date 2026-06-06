#pragma once
#include <functional>

// Pimpl facade — callers need no Oboe headers.
class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    bool init();
    void shutdown();

    void triggerLaser();
    void triggerExplosion();
    void triggerPlayerHit();
    void triggerLevelClear();
    void triggerPowerUp();
    void setThrust(bool active);
    void setMusicEnabled(bool enabled);

private:
    struct Impl;
    Impl* impl_ = nullptr;
};
