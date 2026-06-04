#pragma once

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
    void setThrust(bool active);

private:
    struct Impl;
    Impl* impl_ = nullptr;
};
