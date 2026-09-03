#include "game.h"
#include "audio.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

// ---- level tuning (1..10, very easy -> hard) ----
static const int kMinLevel = 1, kMaxLevel = 10;
static int clampLevel(int L) { return std::clamp(L, kMinLevel, kMaxLevel); }
static float levelFall(int L)   { return 0.50f + 0.08f * (float)(clampLevel(L) - 1); }
static float levelSpawn(int L)  { return 1.00f - 0.07f * (float)(clampLevel(L) - 1); }
static int   levelGoal(int L)   {
    int c = clampLevel(L);
    int goal = 10 + 2 * c;            // gentle base ramp (levels 1..5)
    if (c > 5) goal += 3 * (c - 5);   // steeper climb for the late game so 8/9 don't end so fast
    return goal;
}

static const float kStarSpeed  = 0.18f;
static const float kGravity    = 0.90f;   // ship falls at this acceleration
static const float kThrustAcc  = 2.50f;   // upward acceleration when thrust held
static const float kMaxShipVy  = 1.60f;
static const float kBulletSpeed = 2.80f;
static const float kFireCooldown = 0.22f;
static const float kBulletLife  = 2.20f;

// Per-frame decay factors are expressed as rates so they don't depend on the
// refresh rate: factor^(60 fps) == exp(rate) per second.
static const float kParticleDragRate = -7.67f;   // 0.88 per 60 Hz frame
static const float kShakeDecayRate   = -14.9f;   // 0.78 per 60 Hz frame

// Touch zones as fractions of the viewport. Bottom-left corner is THRUST,
// bottom-right is FIRE; the rest of the left/right halves move the ship.
// render() draws the zone quads from the same numbers.
static const float kThrustZoneW = 0.30f;   // x < 30% of width
static const float kFireZoneX   = 0.70f;   // x > 70% of width
static const float kZoneTopY    = 0.72f;   // y > 72% of height

// Remove entries whose `alive` flag has been cleared.
template <class V>
static void eraseDead(V& v) {
    v.erase(std::remove_if(v.begin(), v.end(),
                           [](const typename V::value_type& e) { return !e.alive; }),
            v.end());
}

// Write `buf` to `path` atomically: a crash between truncate and write can't
// leave a half-written file behind.
static void writeFileAtomic(const char* path, const void* buf, size_t len) {
    char tmp[600];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE* f = fopen(tmp, "wb");
    if (!f) return;
    bool ok = fwrite(buf, len, 1, f) == 1;
    ok = (fclose(f) == 0) && ok;
    if (!ok || rename(tmp, path) != 0) remove(tmp);
}

// Zero the screen-shake offset for the lifetime of the guard so HUD and
// overlay elements are drawn shake-free, then restore it.
struct ShakeOff {
    float &x, &y, sx, sy;
    ShakeOff(float& x_, float& y_) : x(x_), y(y_), sx(x_), sy(y_) { x = 0.0f; y = 0.0f; }
    ~ShakeOff() { x = sx; y = sy; }
};

// 7-segment masks for digits 0..9 (bit a=1,b=2,c=4,d=8,e=16,f=32,g=64).
static const int kDigitSeg[10] = {63, 6, 91, 79, 102, 109, 125, 7, 127, 111};


Game::Game() {}

float Game::frand() {
    uint32_t x = rng_;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    rng_ = x;
    return float(x & 0xFFFFFF) / float(0x1000000);
}
float Game::frange(float a, float b) { return a + (b - a) * frand(); }

void Game::setViewport(int w, int h) {
    if (w <= 0 || h <= 0) return;
    vwf_ = (float)w; vhf_ = (float)h;
    asp_ = vwf_ / vhf_;
    if (stars_.empty()) {
        for (int i = 0; i < 60; i++) {
            Star s;
            s.x = frange(-1.0f, 1.0f);
            s.y = frange(-1.0f, 1.0f);
            s.size = frange(0.004f, 0.011f);
            stars_.push_back(s);
        }
        for (int i = 0; i < 20; i++) {
            Star s;
            s.x = frange(-1.0f, 1.0f);
            s.y = frange(-1.0f, 1.0f);
            s.size = frange(0.007f, 0.016f);
            starsNear_.push_back(s);
        }
    }
    clampShipX();
}

void Game::clampShipX() {
    float lim = asp_ - shipScale_;
    shipX_ = std::clamp(shipX_, -lim, lim);
}

void Game::toWorld(float px, float py, float& wx, float& wy) const {
    wy = 2.0f * py / vhf_ - 1.0f;
    wx = (2.0f * px / vwf_ - 1.0f) * asp_;
}

// --- input zone helpers ---

bool Game::inThrustZone(const Pointer& p) const {
    return p.x < vwf_ * kThrustZoneW && p.y > vhf_ * kZoneTopY;
}
bool Game::inFireZone(const Pointer& p) const {
    return p.x > vwf_ * kFireZoneX && p.y > vhf_ * kZoneTopY;
}

bool Game::leftHeld() const {
    if (autoRunActive_ && aiLeft_) return true;
    return std::any_of(std::begin(pointers_), std::end(pointers_), [this](const Pointer& p) {
        return p.active && p.x < vwf_ * 0.5f && !inThrustZone(p);
    });
}
bool Game::rightHeld() const {
    if (autoRunActive_ && aiRight_) return true;
    return std::any_of(std::begin(pointers_), std::end(pointers_), [this](const Pointer& p) {
        return p.active && p.x >= vwf_ * 0.5f && !inFireZone(p) &&
               !isGearTap(p.x, p.y);  // gear area excluded from movement
    });
}
bool Game::thrustHeld() const {
    if (autoRunActive_ && aiThrust_) return true;
    return std::any_of(std::begin(pointers_), std::end(pointers_), [this](const Pointer& p) {
        return p.active && inThrustZone(p);
    });
}
bool Game::fireHeld() const {
    if (autoRunActive_ && aiFire_) return true;
    return std::any_of(std::begin(pointers_), std::end(pointers_), [this](const Pointer& p) {
        return p.active && inFireZone(p);
    });
}

void Game::onPointerDown(int id, float x, float y) {
    if (id >= 0 && id < kMaxPointers) {
        pointers_[id].active = true;
        pointers_[id].x = x;
        pointers_[id].y = y;
    }
    if (!tapPending_) { tapX_ = x; tapY_ = y; }  // first finger wins
    tapPending_ = true;
}
void Game::onPointerMove(int id, float x, float y) {
    if (id >= 0 && id < kMaxPointers && pointers_[id].active) {
        pointers_[id].x = x;
        pointers_[id].y = y;
    }
}
void Game::onPointerUp(int id) {
    if (id >= 0 && id < kMaxPointers) pointers_[id].active = false;
}
void Game::onPointersCancel() {
    for (auto& p : pointers_) p.active = false;
}

void Game::onAppPause() {
    onPointersCancel();
    tapPending_ = false;
    if (audio_) audio_->setThrust(false);
    // Park a human's round on the settings overlay so it doesn't resume the
    // instant the shade closes. Auto-run keeps its round: the main loop stops
    // calling update() while inactive, and an unattended run (CI replay
    // scripts) has nobody to tap BACK.
    if ((state_ == PLAYING || state_ == LEVEL_CLEAR) && !autoRunActive_) {
        prevState_ = state_;
        state_ = SETTINGS;
    }
}

// ── High score persistence ────────────────────────────────────────────────────

static const uint32_t kHsMagic = 0x41535452u; // "ASTR"

void Game::setDataPath(const char* path) {
    if (!path || path[0] == '\0') return;
    snprintf(dataPath_,    sizeof(dataPath_),    "%s/highscores.bin", path);
    snprintf(settingsPath_, sizeof(settingsPath_), "%s/settings.bin",   path);
    loadHighScores();
    loadSettings();
}

void Game::loadHighScores() {
    if (dataPath_[0] == '\0') return;
    FILE* f = fopen(dataPath_, "rb");
    if (!f) return;
    struct { uint32_t magic, count; struct { int64_t score; int32_t level; } e[kMaxScores]; } buf{};
    if (fread(&buf, sizeof(buf), 1, f) == 1 && buf.magic == kHsMagic) {
        int n = (int)buf.count < kMaxScores ? (int)buf.count : kMaxScores;
        for (int i = 0; i < n; i++) {
            highScores_[i].score = (long)buf.e[i].score;
            highScores_[i].level = buf.e[i].level;
        }
    }
    fclose(f);
}

void Game::saveHighScores() {
    if (dataPath_[0] == '\0') return;
    struct { uint32_t magic, count; struct { int64_t score; int32_t level; } e[kMaxScores]; } buf{};
    buf.magic = kHsMagic;
    buf.count = kMaxScores;
    for (int i = 0; i < kMaxScores; i++) {
        buf.e[i].score = (int64_t)highScores_[i].score;
        buf.e[i].level = highScores_[i].level;
    }
    writeFileAtomic(dataPath_, &buf, sizeof(buf));
}

static const uint32_t kSettingsMagic = 0x53455454u;  // "SETT"

// Gear icon world position — single source of truth for both rendering and hit-testing.
static constexpr float kGearOffsetX = 0.062f;   // distance from right edge
static constexpr float kGearWY      = -0.82f;   // y in world space (top area)

// Settings row y-positions — shared between drawSettingsScreen() and the tap handler.
static constexpr float kSettingSoundY   = -0.15f;
static constexpr float kSettingAutoRunY =  0.10f;
static constexpr float kSettingBackY    =  0.52f;

void Game::loadSettings() {
    if (settingsPath_[0] == '\0') return;
    FILE* f = fopen(settingsPath_, "rb");
    if (!f) return;
    struct { uint32_t magic; int32_t soundOn; int32_t autoRun; } buf = {};
    size_t n = fread(&buf, 1, sizeof(buf), f);
    fclose(f);
    if (n >= 8 && buf.magic == kSettingsMagic) {
        soundEnabled_  = (buf.soundOn != 0);
        if (n >= 12) autoRunActive_ = (buf.autoRun != 0);
    }
}

void Game::saveSettings() {
    if (settingsPath_[0] == '\0') return;
    struct { uint32_t magic; int32_t soundOn; int32_t autoRun; } buf = {
        kSettingsMagic, soundEnabled_ ? 1 : 0, autoRunActive_ ? 1 : 0
    };
    writeFileAtomic(settingsPath_, &buf, sizeof(buf));
}

bool Game::isGearTap(float px, float py) const {
    float wx, wy;
    toWorld(px, py, wx, wy);
    float dx = wx - (asp_ - kGearOffsetX), dy = wy - kGearWY;
    return dx*dx + dy*dy < 0.0081f;  // 0.09 world units radius
}

void Game::spawnDebris(float ax, float ay, float ar, float cr, float cg, float cb) {
    // Expanding flash ring
    Explosion e;
    e.x = ax; e.y = ay; e.radius = ar;
    e.t = 0.0f; e.maxLife = 0.22f;
    e.cr = cr; e.cg = cg; e.cb = cb;
    e.alive = true;
    explosions_.push_back(e);

    // 10 debris fragments flying outward
    for (int i = 0; i < 10; i++) {
        float angle = frange(0.0f, 6.2832f);
        float speed = frange(0.5f, 1.6f);
        Particle p;
        p.x = ax + cosf(angle) * ar * 0.4f;
        p.y = ay + sinf(angle) * ar * 0.4f;
        p.vx = cosf(angle) * speed;
        p.vy = sinf(angle) * speed;
        p.rot  = frange(0.0f, 6.2832f);
        p.spin = frange(-5.0f, 5.0f);
        p.t       = 0.0f;
        p.maxLife = frange(0.25f, 0.55f);
        p.size    = ar * frange(0.25f, 0.55f);
        p.r = cr + frange(-0.08f, 0.08f);
        p.g = cg + frange(-0.08f, 0.08f);
        p.b = cb + frange(-0.08f, 0.08f);
        p.alive = true;
        particles_.push_back(p);
    }
}

void Game::checkHighScore() {
    if (score_ <= 0) return;
    int pos = kMaxScores;
    for (int i = 0; i < kMaxScores; i++) {
        if (score_ > highScores_[i].score) { pos = i; break; }
    }
    if (pos == kMaxScores) return;
    for (int i = kMaxScores - 1; i > pos; i--) highScores_[i] = highScores_[i - 1];
    highScores_[pos] = {score_, level_};
    newHighScore_     = true;
    newHighScoreRank_ = pos;
    saveHighScores();
}

void Game::startGame() {
    score_ = 0;
    lives_ = 5;
    newHighScore_     = false;
    newHighScoreRank_ = -1;
    comboCount_ = 0; comboTimer_ = 0.0f; comboDisplayTimer_ = 0.0f;
    // Reset power-ups fully on new game (they only carry across levels, not sessions).
    shieldActive_ = false;    shieldTimer_     = 0.0f;
    spreadActive_ = false;    spreadTimer_     = 0.0f;
    speedBoostActive_ = false; speedBoostTimer_ = 0.0f;
    if (audio_) audio_->setMusicEnabled(soundEnabled_);
    startLevel(1);
}

void Game::startLevel(int level) {
    level_ = clampLevel(level);
    fallSpeed_ = levelFall(level_);
    spawnInterval_ = levelSpawn(level_);
    goal_ = levelGoal(level_);
    dodgedThisLevel_ = 0;
    spawnTimer_ = 0.5f;
    shipX_ = 0.0f;
    shipY_ = 0.80f;
    shipVy_ = 0.0f;
    shipTilt_ = 0.0f;
    fireCooldown_ = 0.0f;
    invuln_ = 1.2f;
    asteroids_.clear();
    bullets_.clear();
    particles_.clear();
    explosions_.clear();
    powerUps_.clear();
    bossActive_ = false;
    boss_ = {};
    // Power-up state intentionally NOT reset — bonuses carry across levels.
    powerUpSpawnTimer_ = 12.0f;
    shakeAmt_ = 0.0f; shakeX_ = 0.0f; shakeY_ = 0.0f;
    if (level_ == 10) spawnBoss();
    state_ = PLAYING;
}

// Randomize the purely visual rock attributes: noise seed, silhouette variant,
// and squash. Shared by fresh spawns and split children.
void Game::randomizeRockVisuals(Asteroid& a) {
    static const int kShapes[] = {
        SHAPE_ASTEROID, SHAPE_ASTEROID_2, SHAPE_ASTEROID_3, SHAPE_ASTEROID_4
    };
    const int n = (int)(sizeof(kShapes) / sizeof(kShapes[0]));
    a.seed   = frange(0.0f, 64.0f);
    a.squash = frange(0.88f, 1.12f);
    a.shape  = kShapes[(int)(frand() * n) % n];  // frand() < 1, so index <= n-1
}

void Game::spawnAsteroid(bool ambient) {
    Asteroid a;
    a.r = frange(0.05f, 0.11f);
    randomizeRockVisuals(a);
    a.x = frange(-asp_ + a.r, asp_ - a.r);
    a.y = -1.15f - a.r;
    a.vx = 0.0f;
    a.vy = (ambient ? frange(0.30f, 0.55f) : fallSpeed_ * frange(0.85f, 1.15f));
    a.spin = frange(-2.0f, 2.0f);
    a.rot = frange(0.0f, 6.28f);
    float tint = frange(-0.08f, 0.08f);
    a.cr = 0.62f + tint;
    a.cg = 0.55f + tint * 0.6f;
    a.cb = 0.48f + tint * 0.4f;
    // gen / type / hp keep their Asteroid defaults (0 / AT_NORMAL / 1).

    // Higher levels introduce fast and armored variants.
    if (!ambient && level_ >= 7 && frand() < 0.18f + 0.02f * (float)(level_ - 7)) {
        a.type = AT_ARMORED;
        a.hp   = 2;
        a.r   *= 1.15f;
        a.vy  *= 0.80f;
        a.cr   = 0.45f + tint; a.cg = 0.22f + tint * 0.3f; a.cb = 0.18f + tint * 0.2f;
    } else if (!ambient && level_ >= 5 && frand() < 0.18f + 0.03f * (float)(level_ - 5)) {
        a.type = AT_FAST;
        a.hp   = 1;
        a.r   *= 0.72f;
        a.vy  *= 1.65f;
        a.vx   = frange(-0.25f, 0.25f);
        a.spin *= 2.0f;
        a.cr   = 0.85f + tint; a.cg = 0.82f + tint; a.cb = 0.72f + tint;
    }

    a.alive = true;
    asteroids_.push_back(a);
}

void Game::spawnBoss() {
    boss_.x    = 0.0f;
    boss_.y    = -1.35f;
    boss_.vy   = 0.10f;
    boss_.t    = 0.0f;
    boss_.rot  = 0.0f;
    boss_.spin = 0.25f;
    boss_.r    = 0.22f;
    boss_.hp   = boss_.maxHp = 12;
    boss_.alive = true;
    bossActive_ = true;
    // Boss replaces normal asteroid spawning at level 10.
    spawnInterval_ = 999.0f;
}

void Game::splitAsteroid(const Asteroid& a) {
    for (int k = 0; k < 2; k++) {
        Asteroid child;
        child.r    = a.r * 0.55f;
        child.x    = a.x + (k == 0 ? -1.0f : 1.0f) * a.r * 0.45f;
        child.y    = a.y;
        child.vx   = (k == 0 ? -1.0f : 1.0f) * frange(0.18f, 0.40f);
        child.vy   = a.vy * frange(0.85f, 1.25f);
        child.spin = frange(-3.5f, 3.5f);
        child.rot  = frange(0.0f, 6.28f);
        child.cr = a.cr; child.cg = a.cg; child.cb = a.cb;
        randomizeRockVisuals(child);
        child.gen  = a.gen + 1;
        // type / hp keep their defaults: split children are never armored,
        // so the visual promise matches the HP.
        child.alive = true;
        asteroids_.push_back(child);
    }
}

void Game::spawnPowerUp(float x, float y) {
    PowerUp p;
    p.x    = x;
    p.y    = y;
    p.vy   = 0.28f;
    p.rot  = 0.0f;
    p.spin = frange(-2.5f, 2.5f);
    p.type  = (PowerUpType)((int)(frand() * 3.0f));
    p.alive = true;
    powerUps_.push_back(p);
}

void Game::update(float dt) {
    if (dt > 0.05f) dt = 0.05f;   // clamp huge hitches
    animTime_ += dt;

    // Particles and explosions animate in all states.
    const float drag = expf(kParticleDragRate * dt);
    for (auto& p : particles_) {
        if (!p.alive) continue;
        p.x  += p.vx * dt; p.y  += p.vy * dt;
        p.vx *= drag;      p.vy *= drag;
        p.rot += p.spin * dt;
        p.t   += dt;
        if (p.t >= p.maxLife) p.alive = false;
    }
    eraseDead(particles_);

    for (auto& e : explosions_) {
        if (!e.alive) continue;
        e.t += dt;
        if (e.t >= e.maxLife) e.alive = false;
    }
    eraseDead(explosions_);

    // Stars scroll in every state for a sense of motion.
    for (auto& s : stars_) {
        s.y += kStarSpeed * dt;
        if (s.y > 1.05f) { s.y = -1.05f; s.x = frange(-1.0f, 1.0f); }
    }
    for (auto& s : starsNear_) {
        s.y += kStarSpeed * 3.2f * dt;
        if (s.y > 1.05f) { s.y = -1.05f; s.x = frange(-1.0f, 1.0f); }
    }

    // Screen shake decay.
    if (shakeAmt_ > 0.0f) {
        shakeX_ = frange(-1.0f, 1.0f) * shakeAmt_ * 0.040f;
        shakeY_ = frange(-1.0f, 1.0f) * shakeAmt_ * 0.040f;
        shakeAmt_ *= expf(kShakeDecayRate * dt);
        if (shakeAmt_ < 0.01f) { shakeAmt_ = 0.0f; shakeX_ = 0.0f; shakeY_ = 0.0f; }
    }

    bool tapped = tapPending_;
    tapPending_ = false;

    // Gear tap opens Settings from any state except Settings itself.
    if (tapped && state_ != SETTINGS && isGearTap(tapX_, tapY_)) {
        prevState_ = state_;
        if (audio_) audio_->setThrust(false);
        state_ = SETTINGS;
        tapped = false;
    }

    switch (state_) {
        case TITLE: {
            if (tapped) { startGame(); break; }
            spawnTimer_ -= dt;
            if (spawnTimer_ <= 0.0f) { spawnAsteroid(true); spawnTimer_ = 0.8f; }
            for (auto& a : asteroids_) {
                a.y += a.vy * dt; a.rot += a.spin * dt;
                if (a.y - a.r > 1.1f) a.alive = false;
            }
            eraseDead(asteroids_);
            break;
        }
        case PLAYING: {
            // Reset AI flags; updateAutoRun sets them when autopilot is on.
            aiLeft_ = aiRight_ = aiThrust_ = aiFire_ = false;
            if (autoRunActive_) updateAutoRun(dt);

            // Audio: sync thrust sound each frame
            if (audio_) audio_->setThrust(soundEnabled_ && thrustHeld());

            // Combo decay
            if (comboTimer_ > 0.0f) { comboTimer_ -= dt; if (comboTimer_ <= 0.0f) comboCount_ = 0; }
            if (comboDisplayTimer_ > 0.0f) comboDisplayTimer_ -= dt;

            // One frame of gameplay, in order. A step may flip the state to
            // GAME_OVER (fatal hit); the remaining gameplay steps are skipped
            // so nothing scores, spawns, or wins after the high score has
            // already been recorded.
            updatePowerUps(dt);
            updateBossFight(dt);
            if (state_ == PLAYING) updateShipMotion(dt);
            if (state_ == PLAYING) updateAsteroids(dt);
            if (state_ == PLAYING) updateBullets(dt);
            removeDeadEntities();
            checkLevelClear();
            break;
        }
        case LEVEL_CLEAR: {
            stateTimer_ += dt;
            if (stateTimer_ >= 1.8f) {
                if (level_ >= 10) { state_ = WIN; stateTimer_ = 0.0f; checkHighScore(); }
                else startLevel(level_ + 1);
            }
            break;
        }
        case GAME_OVER:
        case WIN: {
            stateTimer_ += dt;
            for (auto& a : asteroids_) {
                a.y += a.vy * dt; a.rot += a.spin * dt;
                if (a.y - a.r > 1.1f) a.alive = false;
            }
            eraseDead(asteroids_);
            if (state_ == GAME_OVER && asteroids_.size() < 4) {
                spawnTimer_ -= dt;
                if (spawnTimer_ <= 0.0f) { spawnAsteroid(true); spawnTimer_ = 0.9f; }
            }
            if (tapped && stateTimer_ > 0.6f) {
                state_ = TITLE;
                asteroids_.clear();
                if (audio_) audio_->setMusicEnabled(false);
            }
            break;
        }
        case SETTINGS: {
            if (tapped) {
                float wx, wy;
                toWorld(tapX_, tapY_, wx, wy);
                const float kHitH = 0.12f;
                // Rows respond only across the label-to-toggle span, not the
                // full screen width (avoids accidental edge taps).
                const float kHitW = asp_ * 0.66f;
                if (fabsf(wy - kSettingSoundY) < kHitH && fabsf(wx) < kHitW) {
                    soundEnabled_ = !soundEnabled_;
                    saveSettings();
                    if (!soundEnabled_) {
                        if (audio_) audio_->setMusicEnabled(false);
                    } else if (prevState_ == PLAYING || prevState_ == LEVEL_CLEAR) {
                        if (audio_) audio_->setMusicEnabled(true);
                    }
                } else if (fabsf(wy - kSettingAutoRunY) < kHitH && fabsf(wx) < kHitW) {
                    autoRunActive_ = !autoRunActive_;
                    saveSettings();
                } else if (fabsf(wy - kSettingBackY) < kHitH && fabsf(wx) < 0.15f) {
                    onPointersCancel();
                    state_ = prevState_;
                }
            }
            break;
        }
    }
}

// ---- PLAYING-state update steps ----

// Shield absorbs the hit; otherwise lose a life, flash, shake, and possibly end
// the game. Shared by asteroid and boss collisions.
void Game::applyShipHit() {
    if (shieldActive_) {
        shieldActive_ = false; shieldTimer_ = 0.0f;
        shakeAmt_ = 0.5f;
        return;
    }
    lives_--;
    invuln_ = 1.5f;
    shakeAmt_ = 1.0f;
    comboCount_ = 0; comboTimer_ = 0.0f;
    if (audio_ && soundEnabled_) audio_->triggerPlayerHit();
    if (haptic_) haptic_();
    Explosion flash;
    flash.x = shipX_; flash.y = shipY_; flash.radius = shipScale_;
    flash.t = 0.0f; flash.maxLife = 0.18f;
    flash.cr = 0.45f; flash.cg = 0.9f; flash.cb = 1.0f;
    flash.alive = true;
    explosions_.push_back(flash);
    if (lives_ <= 0) {
        if (audio_) audio_->setThrust(false);
        state_ = GAME_OVER; stateTimer_ = 0.0f;
        checkHighScore();
    }
}

void Game::updatePowerUps(float dt) {
    // Active-effect timers
    if (shieldActive_)     { shieldTimer_     -= dt; if (shieldTimer_     <= 0) shieldActive_     = false; }
    if (spreadActive_)     { spreadTimer_     -= dt; if (spreadTimer_     <= 0) spreadActive_     = false; }
    if (speedBoostActive_) { speedBoostTimer_ -= dt; if (speedBoostTimer_ <= 0) speedBoostActive_ = false; }

    // Ambient spawn
    powerUpSpawnTimer_ -= dt;
    if (powerUpSpawnTimer_ <= 0.0f) {
        spawnPowerUp(frange(-asp_ * 0.7f, asp_ * 0.7f), -1.1f);
        powerUpSpawnTimer_ = frange(12.0f, 18.0f);
    }

    // Movement + collection
    float puR = 0.045f;
    for (auto& pu : powerUps_) {
        if (!pu.alive) continue;
        pu.y   += pu.vy * dt;
        pu.rot += pu.spin * dt;
        if (pu.y > 1.1f) { pu.alive = false; continue; }
        float dx = pu.x - shipX_, dy = pu.y - shipY_;
        if (dx*dx + dy*dy < (puR + shipR_) * (puR + shipR_)) {
            pu.alive = false;
            score_ += 25 * level_;
            if (pu.type == PU_SHIELD)      { shieldActive_ = true;     shieldTimer_     = kPowerUpDuration; }
            else if (pu.type == PU_SPREAD)  { spreadActive_ = true;     spreadTimer_     = kPowerUpDuration; }
            else                            { speedBoostActive_ = true; speedBoostTimer_ = kPowerUpDuration; }
            if (audio_ && soundEnabled_) audio_->triggerPowerUp();
        }
    }
    eraseDead(powerUps_);
}

void Game::updateBossFight(float dt) {
    if (!bossActive_ || !boss_.alive) return;
    boss_.t   += dt;
    boss_.y   += boss_.vy * dt;
    boss_.rot += boss_.spin * dt;
    boss_.x    = sinf(boss_.t * 0.70f) * asp_ * 0.62f;
    if (invuln_ <= 0.0f) {
        float bdx = boss_.x - shipX_, bdy = boss_.y - shipY_;
        if (bdx*bdx + bdy*bdy < (boss_.r + shipR_) * (boss_.r + shipR_))
            applyShipHit();
    }
    // Boss wraps back from bottom to top so it stays on screen permanently
    if (boss_.y - boss_.r > 1.1f) boss_.y = -1.1f - boss_.r;
}

void Game::updateShipMotion(float dt) {
    // Horizontal movement + tilt
    float dir = (rightHeld() ? 1.0f : 0.0f) - (leftHeld() ? 1.0f : 0.0f);
    float effectiveSpeed = shipSpeed_ * (speedBoostActive_ ? 1.65f : 1.0f);
    shipX_ += dir * effectiveSpeed * dt;
    clampShipX();
    // Smoothly lean into direction of travel (±20°)
    float tiltTarget = dir * 0.35f;
    shipTilt_ += (tiltTarget - shipTilt_) * 9.0f * dt;

    // Vertical thrust / gravity
    float acc = thrustHeld() ? -kThrustAcc : kGravity;
    shipVy_ += acc * dt;
    if (shipVy_ >  kMaxShipVy) shipVy_ =  kMaxShipVy;
    if (shipVy_ < -kMaxShipVy) shipVy_ = -kMaxShipVy;
    shipY_ += shipVy_ * dt;
    if (shipY_ > 0.92f) { shipY_ = 0.92f; if (shipVy_ > 0) shipVy_ = 0; }
    if (shipY_ < 0.12f) { shipY_ = 0.12f; if (shipVy_ < 0) shipVy_ = 0; }

    if (invuln_ > 0.0f) invuln_ -= dt;
}

void Game::updateAsteroids(float dt) {
    spawnTimer_ -= dt;
    if (spawnTimer_ <= 0.0f) {
        spawnAsteroid(false);
        spawnTimer_ = spawnInterval_ * frange(0.8f, 1.2f);
    }

    // Movement, ship collision, and dodge scoring
    for (auto& a : asteroids_) {
        a.x += a.vx * dt;
        a.y += a.vy * dt;
        a.rot += a.spin * dt;
        if (invuln_ <= 0.0f) {
            float dx = a.x - shipX_, dy = a.y - shipY_;
            float rad = a.r + shipR_;
            if (dx * dx + dy * dy < rad * rad) {
                a.alive = false;
                spawnDebris(a.x, a.y, a.r, a.cr, a.cg, a.cb);
                applyShipHit();
                if (state_ != PLAYING) break;  // fatal: no dodge points this frame
            }
        }
        if (a.alive && (a.y - a.r > 1.05f ||
                       a.x - a.r > asp_ + 0.05f || a.x + a.r < -asp_ - 0.05f)) {
            a.alive = false;
            if (a.y - a.r > 1.05f) {  // only award dodge score for bottom exit
                dodgedThisLevel_++;
                score_ += 5 * level_;
            }
        }
    }
}

void Game::updateBullets(float dt) {
    // Fire
    if (fireCooldown_ > 0.0f) fireCooldown_ -= dt;
    if (fireHeld() && fireCooldown_ <= 0.0f) {
        auto fireBullet = [&](float ox, float vx) {
            Bullet b;
            b.x = shipX_ + ox;
            b.y = shipY_ - shipScale_ * 1.1f;
            b.vx = vx;
            b.vy = -kBulletSpeed;
            b.life = kBulletLife;
            b.alive = true;
            bullets_.push_back(b);
        };
        fireBullet(0.0f, 0.0f);
        if (spreadActive_) {
            fireBullet(-shipScale_ * 0.55f, -0.28f);
            fireBullet( shipScale_ * 0.55f,  0.28f);
        }
        fireCooldown_ = kFireCooldown;
        if (audio_ && soundEnabled_) audio_->triggerLaser();
    }

    for (auto& b : bullets_) {
        if (!b.alive) continue;
        b.x += b.vx * dt;
        b.y += b.vy * dt;
        b.life -= dt;
        if (b.x < -asp_ - 0.1f || b.x > asp_ + 0.1f || b.y < -1.15f || b.life <= 0.0f) {
            b.alive = false; continue;
        }

        // Bullet vs boss
        if (bossActive_ && boss_.alive) {
            float bdx = b.x - boss_.x, bdy = b.y - boss_.y;
            if (bdx*bdx + bdy*bdy < (boss_.r + 0.012f) * (boss_.r + 0.012f)) {
                b.alive = false;
                boss_.hp--;
                // Partial-hit flash
                Explosion hitFlash;
                hitFlash.x = boss_.x; hitFlash.y = boss_.y;
                hitFlash.radius = boss_.r * 0.4f;
                hitFlash.t = 0.0f; hitFlash.maxLife = 0.12f;
                hitFlash.cr = 1.0f; hitFlash.cg = 0.6f; hitFlash.cb = 0.1f;
                hitFlash.alive = true;
                explosions_.push_back(hitFlash);
                if (boss_.hp <= 0) {
                    boss_.alive = false;
                    if (audio_ && soundEnabled_) audio_->triggerExplosion();
                    spawnDebris(boss_.x, boss_.y, boss_.r * 1.5f, 0.9f, 0.55f, 0.15f);
                    spawnDebris(boss_.x, boss_.y, boss_.r,        0.8f, 0.40f, 0.10f);
                    score_ += 50 * level_;
                    if (audio_) audio_->setThrust(false);
                    state_ = WIN; stateTimer_ = 0.0f;
                    checkHighScore();
                    return;   // the table is written: nothing else scores this frame
                }
                continue;
            }
        }

        // Bullet vs asteroids
        for (auto& a : asteroids_) {
            if (!a.alive) continue;
            float dx = b.x - a.x, dy = b.y - a.y;
            if (dx * dx + dy * dy < (a.r + 0.012f) * (a.r + 0.012f)) {
                b.alive = false;
                a.hp--;
                if (a.hp > 0) {
                    // Armored hit — flash but don't destroy yet
                    Explosion e;
                    e.x = a.x; e.y = a.y; e.radius = a.r * 0.35f;
                    e.t = 0.0f; e.maxLife = 0.10f;
                    e.cr = 1.0f; e.cg = 0.7f; e.cb = 0.2f; e.alive = true;
                    explosions_.push_back(e);
                    break;
                }
                a.alive = false;
                // Update combo
                if (comboTimer_ > 0.0f) {
                    comboCount_ = comboCount_ < 4 ? comboCount_ + 1 : 4;
                } else {
                    comboCount_ = 1;
                }
                comboTimer_        = 1.8f;
                comboDisplayTimer_ = 0.9f;
                long baseScore = 10L * level_;
                score_ += baseScore * comboCount_;
                dodgedThisLevel_++;
                if (audio_ && soundEnabled_) audio_->triggerExplosion();
                // Copy before push_backs: splitAsteroid/spawnPowerUp may reallocate
                // asteroids_, invalidating the 'a' reference.
                Asteroid dead = a;
                spawnDebris(dead.x, dead.y, dead.r, dead.cr, dead.cg, dead.cb);
                if (dead.gen < 2) splitAsteroid(dead);
                if (frand() < 0.15f) spawnPowerUp(dead.x, dead.y);
                break;
            }
        }
    }
}

void Game::removeDeadEntities() {
    eraseDead(asteroids_);
    eraseDead(bullets_);
}

void Game::checkLevelClear() {
    // Level clear: level 10 is cleared only by defeating the boss.
    if (state_ == PLAYING && dodgedThisLevel_ >= goal_ && level_ < 10) {
        score_ += 100 * level_;
        if (audio_) audio_->setThrust(false);
        state_ = LEVEL_CLEAR;
        if (audio_ && soundEnabled_) audio_->triggerLevelClear();
        stateTimer_ = 0.0f;
        asteroids_.clear();
        bullets_.clear();
    }
}

// ---- drawing helpers ----
void Game::emit(std::vector<DrawCmd>& out, int shape, float wx, float wy,
                float sx, float sy, float rot, float r, float g, float b, float a,
                float style, float seed) const {
    float c = cosf(rot), s = sinf(rot);
    DrawCmd d;
    d.mtx[0] = sx * c / asp_;
    d.mtx[1] = -sy * s / asp_;
    d.mtx[2] = sx * s;
    d.mtx[3] = sy * c;
    d.tx = wx / asp_ + shakeX_;
    d.ty = wy + shakeY_;
    d.color[0] = r; d.color[1] = g; d.color[2] = b; d.color[3] = a;
    d.style = style;
    d.seed = seed;
    d.shape = shape;
    out.push_back(d);
}

void Game::drawDigit(std::vector<DrawCmd>& out, int dgt, float cx, float cy,
                     float h, float r, float g, float b, float a) const {
    if (dgt < 0 || dgt > 9) return;
    int mask = kDigitSeg[dgt];
    float w = h * 0.60f;
    float t = h * 0.16f;
    float ht = t * 0.5f, hw = w * 0.5f, q = h * 0.25f;
    float hSegX = hw - ht, hSegY = ht;     // horizontal segment half-extents
    float vSegX = ht, vSegY = q - ht;      // vertical segment half-extents
    auto seg = [&](float x, float y, float ex, float ey) {
        emit(out, SHAPE_QUAD, x, y, ex, ey, 0.0f, r, g, b, a);
    };
    if (mask & 1)  seg(cx, cy - h * 0.5f + ht, hSegX, hSegY);   // a top
    if (mask & 2)  seg(cx + hw - ht, cy - q, vSegX, vSegY);     // b top-right
    if (mask & 4)  seg(cx + hw - ht, cy + q, vSegX, vSegY);     // c bottom-right
    if (mask & 8)  seg(cx, cy + h * 0.5f - ht, hSegX, hSegY);   // d bottom
    if (mask & 16) seg(cx - hw + ht, cy + q, vSegX, vSegY);     // e bottom-left
    if (mask & 32) seg(cx - hw + ht, cy - q, vSegX, vSegY);     // f top-left
    if (mask & 64) seg(cx, cy, hSegX, hSegY);                   // g middle
}

int Game::numDigits(long v) {
    if (v <= 0) return 1;
    int n = 0;
    while (v > 0) { n++; v /= 10; }
    return n;
}

void Game::drawNumber(std::vector<DrawCmd>& out, long value, float firstCx, float cy,
                      float h, float r, float g, float b, float a) const {
    if (value < 0) value = 0;
    int n = numDigits(value);
    float w = h * 0.60f;
    float step = w * 1.45f;
    int digits[20];   // enough for any 64-bit value
    long tmp = value;
    int count = 0;
    if (value == 0) { digits[count++] = 0; }
    while (tmp > 0 && count < 20) { digits[count++] = (int)(tmp % 10); tmp /= 10; }
    // digits[] is reversed; draw most-significant first.
    for (int i = 0; i < n; i++) {
        int dgt = digits[n - 1 - i];
        drawDigit(out, dgt, firstCx + (float)i * step, cy, h, r, g, b, a);
    }
}

void Game::drawLetter(std::vector<DrawCmd>& out, char ch, float cx, float cy,
                      float h, float r, float g, float b, float a) const {
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
    if (ch >= '0' && ch <= '9') { drawDigit(out, ch - '0', cx, cy, h, r, g, b, a); return; }
    if (ch < 'A' || ch > 'Z') return;

    // Stroke font: each letter built from line segments.
    // Coords are in letter-local space: x in [-1,1], y in [-1,1] (top=-1, bot=+1).
    float hw = h * 0.38f;  // half-width
    float hh = h * 0.50f;  // half-height
    float th = h * 0.07f;  // stroke thickness

    auto stroke = [&](float x1, float y1, float x2, float y2) {
        float wx1 = cx + x1 * hw,  wy1 = cy + y1 * hh;
        float wx2 = cx + x2 * hw,  wy2 = cy + y2 * hh;
        float dx = wx2 - wx1, dy = wy2 - wy1;
        float len = sqrtf(dx*dx + dy*dy);
        if (len < 0.001f) return;
        float rot = atan2f(wx1 - wx2, wy2 - wy1);
        emit(out, SHAPE_QUAD, (wx1+wx2)*0.5f, (wy1+wy2)*0.5f, th, len*0.5f, rot, r, g, b, a);
    };

    switch (ch) {
        case 'A': stroke(-1,1,0,-1); stroke(1,1,0,-1); stroke(-0.5f,0.1f,0.5f,0.1f); break;
        case 'B': stroke(-1,-1,-1,1); stroke(-1,-1,0.6f,-1); stroke(0.6f,-1,1,-0.5f); stroke(1,-0.5f,0.6f,0); stroke(0.6f,0,-1,0); stroke(-1,0,0.8f,0); stroke(0.8f,0,1,0.5f); stroke(1,0.5f,0.8f,1); stroke(0.8f,1,-1,1); break;
        case 'C': stroke(1,-0.7f,0,-1); stroke(0,-1,-1,-0.3f); stroke(-1,-0.3f,-1,0.3f); stroke(-1,0.3f,0,1); stroke(0,1,1,0.7f); break;
        case 'D': stroke(-1,-1,-1,1); stroke(-1,-1,0.3f,-1); stroke(0.3f,-1,1,-0.4f); stroke(1,-0.4f,1,0.4f); stroke(1,0.4f,0.3f,1); stroke(0.3f,1,-1,1); break;
        case 'E': stroke(-1,-1,-1,1); stroke(-1,-1,1,-1); stroke(-1,0,0.6f,0); stroke(-1,1,1,1); break;
        case 'F': stroke(-1,-1,-1,1); stroke(-1,-1,1,-1); stroke(-1,0,0.6f,0); break;
        case 'G': stroke(1,-0.7f,0,-1); stroke(0,-1,-1,-0.3f); stroke(-1,-0.3f,-1,0.3f); stroke(-1,0.3f,0,1); stroke(0,1,1,0.7f); stroke(1,0.7f,1,0); stroke(1,0,0.2f,0); break;
        case 'H': stroke(-1,-1,-1,1); stroke(1,-1,1,1); stroke(-1,0,1,0); break;
        case 'I': stroke(-0.5f,-1,0.5f,-1); stroke(0,-1,0,1); stroke(-0.5f,1,0.5f,1); break;
        case 'J': stroke(0.5f,-1,0.5f,0.6f); stroke(0.5f,0.6f,0,1); stroke(0,1,-0.5f,0.7f); break;
        case 'K': stroke(-1,-1,-1,1); stroke(-1,0,1,-1); stroke(-1,0,1,1); break;
        case 'L': stroke(-1,-1,-1,1); stroke(-1,1,1,1); break;
        case 'M': stroke(-1,1,-1,-1); stroke(-1,-1,0,0.3f); stroke(0,0.3f,1,-1); stroke(1,-1,1,1); break;
        case 'N': stroke(-1,1,-1,-1); stroke(-1,-1,1,1); stroke(1,1,1,-1); break;
        case 'O': stroke(-1,-1,1,-1); stroke(1,-1,1,1); stroke(1,1,-1,1); stroke(-1,1,-1,-1); break;
        case 'P': stroke(-1,-1,-1,1); stroke(-1,-1,0.7f,-1); stroke(0.7f,-1,1,-0.5f); stroke(1,-0.5f,0.7f,0); stroke(0.7f,0,-1,0); break;
        case 'Q': stroke(-1,-1,1,-1); stroke(1,-1,1,1); stroke(1,1,-1,1); stroke(-1,1,-1,-1); stroke(0.2f,0.4f,1,1); break;
        case 'R': stroke(-1,-1,-1,1); stroke(-1,-1,0.7f,-1); stroke(0.7f,-1,1,-0.5f); stroke(1,-0.5f,0.7f,0); stroke(0.7f,0,-1,0); stroke(-0.1f,0,1,1); break;
        case 'S': stroke(1,-0.8f,-1,-1); stroke(-1,-1,-1,0); stroke(-1,0,1,0); stroke(1,0,1,1); stroke(1,1,-1,0.8f); break;
        case 'T': stroke(-1,-1,1,-1); stroke(0,-1,0,1); break;
        case 'U': stroke(-1,-1,-1,0.7f); stroke(-1,0.7f,0,1); stroke(0,1,1,0.7f); stroke(1,0.7f,1,-1); break;
        case 'V': stroke(-1,-1,0,1); stroke(1,-1,0,1); break;
        case 'W': stroke(-1,-1,-0.5f,1); stroke(-0.5f,1,0,0); stroke(0,0,0.5f,1); stroke(0.5f,1,1,-1); break;
        case 'X': stroke(-1,-1,1,1); stroke(1,-1,-1,1); break;
        case 'Y': stroke(-1,-1,0,0); stroke(1,-1,0,0); stroke(0,0,0,1); break;
        case 'Z': stroke(-1,-1,1,-1); stroke(1,-1,-1,1); stroke(-1,1,1,1); break;
        default: break;
    }
}

void Game::drawText(std::vector<DrawCmd>& out, const char* text, float cx, float cy,
                    float h, float r, float g, float b, float a) const {
    int total = 0;
    for (const char* p = text; *p; p++) total++;
    if (total == 0) return;
    float step = h * 0.95f;  // stroke font: wider spacing than 7-seg digits
    float startX = cx - (float)(total - 1) * step * 0.5f;
    for (int i = 0; text[i]; i++) {
        if (text[i] != ' ')
            drawLetter(out, text[i], startX + (float)i * step, cy, h, r, g, b, a);
    }
}

void Game::drawPowerUpHUD(std::vector<DrawCmd>& out) const {
    // Left-side HUD: stacked power-up indicators with timer bars.
    // Each row: small diamond icon + horizontal timer bar.
    struct PU { bool active; float timer; float r, g, b; } pus[3] = {
        { shieldActive_,    shieldTimer_,     0.30f, 0.55f, 1.00f },
        { spreadActive_,    spreadTimer_,     0.25f, 1.00f, 0.40f },
        { speedBoostActive_,speedBoostTimer_, 1.00f, 0.85f, 0.10f },
    };
    float iconX = -asp_ + 0.055f;
    float barX0 = -asp_ + 0.095f;
    float barMaxW = 0.16f;
    for (int i = 0; i < 3; i++) {
        if (!pus[i].active) continue;
        float y  = -0.70f + (float)i * 0.11f;
        float progress = pus[i].timer / kPowerUpDuration;
        float pulse = (pus[i].timer < 2.0f) ? (0.5f + 0.5f * sinf(animTime_ * 18.0f)) : 1.0f;
        // Icon diamond
        emit(out, SHAPE_QUAD, iconX, y, 0.020f, 0.020f, 0.785f,
             pus[i].r, pus[i].g, pus[i].b, pulse);
        // Timer bar background
        emit(out, SHAPE_QUAD, barX0 + barMaxW * 0.5f, y, barMaxW * 0.5f, 0.006f, 0.0f,
             pus[i].r * 0.3f, pus[i].g * 0.3f, pus[i].b * 0.3f, 0.5f);
        // Timer bar fill
        float fillW = barMaxW * progress;
        if (fillW > 0.002f)
            emit(out, SHAPE_QUAD, barX0 + fillW * 0.5f, y, fillW * 0.5f, 0.006f, 0.0f,
                 pus[i].r, pus[i].g, pus[i].b, 0.85f);
    }
}

void Game::drawComboIndicator(std::vector<DrawCmd>& out) const {
    if (comboDisplayTimer_ <= 0.0f || comboCount_ < 2) return;
    float alpha = comboDisplayTimer_ / 0.9f;
    float h  = 0.075f + 0.015f * (1.0f - alpha); // slightly grows then fades
    float step = h * 0.95f;
    float cx = 0.0f; // center of screen
    // Draw "x" then digit, e.g. "x3"
    drawLetter(out, 'X', cx - step * 0.5f, 0.62f, h, 1.0f, 0.85f, 0.10f, alpha);
    drawDigit(out, comboCount_, cx + step * 0.5f, 0.62f, h * 1.2f, 1.0f, 0.85f, 0.10f, alpha);
}

void Game::drawBossHealthBar(std::vector<DrawCmd>& out) const {
    if (!bossActive_ || !boss_.alive) return;
    float progress = (float)boss_.hp / (float)boss_.maxHp;
    float barW = asp_ * 0.80f;
    float y    = -0.84f;
    // Background
    emit(out, SHAPE_QUAD, 0.0f, y, barW, 0.012f, 0.0f, 0.25f, 0.05f, 0.05f, 0.7f);
    // Fill (red → orange → yellow based on health)
    float fr = 1.0f, fg = 0.15f + 0.70f * progress, fb = 0.0f;
    emit(out, SHAPE_QUAD, -barW + barW * progress, y, barW * progress, 0.012f, 0.0f, fr, fg, fb, 0.9f);
    // "BOSS" label using stroke letters
    drawText(out, "BOSS", 0.0f, y - 0.045f, 0.038f, fr, fg, fb, 0.85f);
}

void Game::updateAutoRun(float dt) {
    const float kPreferredY   = 0.38f;
    const float kEvadeRadius  = 0.22f;
    const float kSteerDeadband = 0.03f;  // don't jitter around an aligned x

    // Find best shoot target: asteroid above ship with shortest approach angle
    float bestTargetX  = 0.0f;
    float bestScore    = 1e9f;
    bool  hasTarget    = false;
    for (auto& a : asteroids_) {
        if (!a.alive || a.y >= shipY_) continue;
        float distX   = fabsf(a.x - shipX_);
        float urgency = (a.y + 1.2f) + a.vy * 0.4f;
        float score   = distX / (urgency + 0.01f);
        if (score < bestScore) { bestScore = score; bestTargetX = a.x; hasTarget = true; }
    }
    // Boss: predict 0.4 s ahead for lead aiming
    if (bossActive_ && boss_.alive) {
        float predictX = sinf((boss_.t + 0.40f) * 0.70f) * asp_ * 0.62f;
        float score    = fabsf(predictX - shipX_) / 3.0f;
        if (score < bestScore) { bestScore = score; bestTargetX = predictX; hasTarget = true; }
    }

    // Find closest threat for evasion (compare squared distances, sqrt only once)
    float closestDistSq = 1e18f, closestX = shipX_;
    for (auto& a : asteroids_) {
        if (!a.alive) continue;
        float dx = a.x - shipX_, dy = a.y - shipY_;
        float dsq = dx*dx + dy*dy;
        if (dsq < closestDistSq) { closestDistSq = dsq; closestX = a.x; }
    }
    const float closestDist = sqrtf(closestDistSq);

    // Horizontal: evade > align-to-target > return-to-center
    if (closestDist < kEvadeRadius) {
        if (closestX >= shipX_) aiLeft_ = true; else aiRight_ = true;
    } else if (hasTarget) {
        float dx = bestTargetX - shipX_;
        if (fabsf(dx) > kSteerDeadband) { if (dx > 0) aiRight_ = true; else aiLeft_ = true; }
    } else {
        if      (shipX_ >  0.05f) aiLeft_  = true;
        else if (shipX_ < -0.05f) aiRight_ = true;
    }
    // Power-up collection: steer to intercept the most quickly reachable
    // power-up — but only when the path to it is clear. Evasion always wins;
    // collection outranks target alignment.
    const float kCollectMargin = 0.16f;  // safety buffer around the steering corridor
    const PowerUp* collect = nullptr;
    if (closestDist > kEvadeRadius) {
        float bestT = 1e9f;
        for (auto& pu : powerUps_) {
            if (!pu.alive) continue;
            float tFall = (shipY_ - pu.y) / pu.vy;  // time until it falls to our altitude
            if (tFall < -0.3f) continue;            // already dropped past the ship
            if (tFall < 0.0f) tFall = 0.0f;         // slightly below — still catchable
            float tSteer = fabsf(pu.x - shipX_) / shipSpeed_;
            if (tSteer > tFall + 1.2f) continue;    // can't reach it before it escapes
            float t = tSteer > tFall ? tSteer : tFall;
            // Skip if any asteroid currently sits in the steering corridor
            // between the ship and the power-up (plus a safety margin).
            float loX = fminf(shipX_, pu.x), hiX = fmaxf(shipX_, pu.x);
            float loY = fminf(shipY_, pu.y), hiY = fmaxf(shipY_, pu.y);
            bool pathClear = true;
            for (auto& a : asteroids_) {
                if (!a.alive) continue;
                float m = kCollectMargin + a.r;
                if (a.x > loX - m && a.x < hiX + m && a.y > loY - m && a.y < hiY + m) {
                    pathClear = false; break;
                }
            }
            if (!pathClear) continue;
            if (t < bestT) { bestT = t; collect = &pu; }
        }
        if (collect) {
            float dx = collect->x - shipX_;
            aiLeft_ = aiRight_ = false;
            if      (dx >  kSteerDeadband) aiRight_ = true;
            else if (dx < -kSteerDeadband) aiLeft_  = true;
        }
    }
    aiWantsCollect_ = (collect != nullptr);

    // Vertical: thrust to maintain preferred altitude; emergency if threat is very close
    bool emergency = closestDist < kEvadeRadius * 0.7f;
    aiThrust_ = (shipY_ > kPreferredY + 0.06f) || emergency;
    // Cut thrust to sink onto a power-up that is below the ship.
    if (collect && !emergency && collect->y > shipY_ + 0.02f) aiThrust_ = false;

    // Fire when aligned with target
    float fireThresh = spreadActive_ ? 0.14f : 0.055f;
    if (hasTarget && fabsf(bestTargetX - shipX_) < fireThresh) aiFire_ = true;
    if (bossActive_ && boss_.alive) {
        float bossThresh = spreadActive_ ? 0.20f : 0.08f;
        if (fabsf(boss_.x - shipX_) < bossThresh) aiFire_ = true;
    }
    (void)dt;
}

void Game::drawGearIcon(std::vector<DrawCmd>& out, float cx, float cy, float size,
                         float r, float g, float b, float a) const {
    // Circular body approximated by SHAPE_ASTEROID (32-vertex polygon)
    const float bodyR = size * 0.68f;
    emit(out, SHAPE_ASTEROID, cx, cy, bodyR, bodyR, 0.0f, r, g, b, a);

    // 6 rectangular teeth protruding clearly beyond the body
    const float toothCR = size * 0.94f;   // center of tooth from gear center
    const float toothHW = size * 0.20f;   // tooth half-width  (tangential)
    const float toothHH = size * 0.30f;   // tooth half-height (radial)
    for (int i = 0; i < 6; i++) {
        float ang = (float)i * 1.0472f;  // 60° apart
        emit(out, SHAPE_QUAD,
             cx + cosf(ang) * toothCR,
             cy + sinf(ang) * toothCR,
             toothHW, toothHH, ang, r, g, b, a);
    }

    // Centre hole punched through in background colour
    emit(out, SHAPE_ASTEROID, cx, cy, size * 0.30f, size * 0.30f, 0.0f,
         kClearColor[0], kClearColor[1], kClearColor[2], a);
}

void Game::drawSettingsScreen(std::vector<DrawCmd>& out) const {
    // Full-screen dark overlay
    emit(out, SHAPE_QUAD, 0.0f, 0.0f, asp_, 1.0f, 0.0f, 0.04f, 0.06f, 0.14f, 0.93f);

    drawText(out, "SETTINGS", 0.0f, -0.52f, 0.078f, 0.55f, 0.78f, 1.00f, 1.0f);

    const float labelX  = -asp_ * 0.48f;
    const float toggleX =  asp_ * 0.45f;

    // Sound row
    drawText(out, "SOUND", labelX, kSettingSoundY, 0.055f, 0.80f, 0.85f, 0.90f, 0.9f);
    emit(out, SHAPE_QUAD, toggleX, kSettingSoundY, 0.090f, 0.040f, 0.0f,
         soundEnabled_ ? 0.08f : 0.20f,
         soundEnabled_ ? 0.48f : 0.18f,
         soundEnabled_ ? 0.08f : 0.18f, 0.75f);
    drawText(out, soundEnabled_ ? "ON" : "OFF", toggleX, kSettingSoundY, 0.048f,
             soundEnabled_ ? 0.35f : 0.65f,
             soundEnabled_ ? 1.00f : 0.45f,
             soundEnabled_ ? 0.35f : 0.45f, 1.0f);

    // Auto Run row
    drawText(out, "AUTO RUN", labelX, kSettingAutoRunY, 0.055f, 0.80f, 0.85f, 0.90f, 0.9f);
    emit(out, SHAPE_QUAD, toggleX, kSettingAutoRunY, 0.090f, 0.040f, 0.0f,
         autoRunActive_ ? 0.08f : 0.20f,
         autoRunActive_ ? 0.48f : 0.18f,
         autoRunActive_ ? 0.08f : 0.18f, 0.75f);
    drawText(out, autoRunActive_ ? "ON" : "OFF", toggleX, kSettingAutoRunY, 0.048f,
             autoRunActive_ ? 0.35f : 0.65f,
             autoRunActive_ ? 1.00f : 0.45f,
             autoRunActive_ ? 0.35f : 0.45f, 1.0f);

    // Back button
    emit(out, SHAPE_QUAD, 0.0f, kSettingBackY, 0.12f, 0.052f, 0.0f, 0.22f, 0.22f, 0.25f, 0.82f);
    drawText(out, "BACK", 0.0f, kSettingBackY, 0.055f, 0.85f, 0.85f, 0.92f, 1.0f);
}

// The big end-screen score keeps this height until the digit count would
// clip at the screen sides, then shrinks to fit. drawNumber spaces digit
// centres 0.87h apart (0.6h glyph width x 1.45) and a glyph spans 0.6h, so
// n digits span (0.87(n-1) + 0.6)h; a 4-digit score already overflows a
// portrait phone (~0.95 world units wide) at the full 0.30 height.
static float endScoreHeight(int digits, float asp) {
    const float kMaxH = 0.30f;
    float span = 0.87f * (float)(digits - 1) + 0.60f;
    float h = 2.0f * asp * 0.90f / span;   // fill at most 90% of the width
    return h < kMaxH ? h : kMaxH;
}

void Game::render(std::vector<DrawCmd>& out) {
    // far stars (dim, slow)
    for (auto& s : stars_)
        emit(out, SHAPE_QUAD, s.x * asp_, s.y, s.size, s.size, 0.0f,
             0.55f, 0.60f, 0.78f, 0.70f);
    // near stars (bright, fast parallax layer)
    for (auto& s : starsNear_)
        emit(out, SHAPE_QUAD, s.x * asp_, s.y, s.size, s.size, 0.0f,
             0.85f, 0.88f, 1.00f, 0.90f);

    // asteroids — procedural rock surface seeded per rock, slight squash for
    // silhouette variety; armored ones get a brighter outline ring for extra HP
    for (auto& a : asteroids_) {
        emit(out, a.shape, a.x, a.y, a.r * a.squash, a.r / a.squash, a.rot,
             a.cr, a.cg, a.cb, 1.0f, (float)STYLE_ROCK, a.seed);
        if (a.type == AT_ARMORED && a.hp > 0) {
            float pulse = 0.55f + 0.35f * sinf(animTime_ * 5.0f);
            emit(out, a.shape, a.x, a.y, a.r * 1.22f, a.r * 1.22f, -a.rot,
                 0.95f, 0.55f, 0.15f, pulse * 0.45f);
        }
    }

    // boss — rocky core + inner layer, pulsing flat ring so it reads as a threat
    if (bossActive_ && boss_.alive) {
        float bpulse = 0.7f + 0.3f * sinf(animTime_ * 3.0f);
        emit(out, SHAPE_ASTEROID, boss_.x, boss_.y, boss_.r, boss_.r, boss_.rot,
             0.75f, 0.20f, 0.10f, 1.0f, (float)STYLE_ROCK, 7.31f);
        emit(out, SHAPE_ASTEROID_4, boss_.x, boss_.y, boss_.r * 0.65f, boss_.r * 0.65f,
             -boss_.rot * 1.5f, 0.95f, 0.45f, 0.10f, 0.9f, (float)STYLE_ROCK, 23.7f);
        emit(out, SHAPE_ASTEROID, boss_.x, boss_.y, boss_.r * 1.15f, boss_.r * 1.15f,
             boss_.rot * 0.7f, 1.0f, 0.30f, 0.05f, bpulse * 0.35f);
    }

    // power-ups: rotating diamond with glow halo
    for (auto& pu : powerUps_) {
        if (!pu.alive) continue;
        float pr, pg, pb;
        if      (pu.type == PU_SHIELD) { pr=0.30f; pg=0.55f; pb=1.00f; }
        else if (pu.type == PU_SPREAD) { pr=0.25f; pg=1.00f; pb=0.40f; }
        else                           { pr=1.00f; pg=0.85f; pb=0.10f; }
        float sz   = 0.035f;
        float glow = sz * (1.35f + 0.20f * sinf(animTime_ * 5.0f));
        emit(out, SHAPE_QUAD, pu.x, pu.y, glow, glow, pu.rot,      pr,   pg,   pb,   0.28f);
        emit(out, SHAPE_QUAD, pu.x, pu.y, sz,   sz,   pu.rot,      pr,   pg,   pb,   1.00f);
        emit(out, SHAPE_QUAD, pu.x, pu.y, sz*0.38f, sz*0.38f, pu.rot + 0.785f, 1.0f, 1.0f, 1.0f, 0.85f);
    }

    // bullets: two-layer laser bolt (outer glow + bright core)
    for (auto& b : bullets_) {
        float angle = b.vx != 0.0f ? atan2f(b.vx, -b.vy) : 0.0f;
        emit(out, SHAPE_QUAD, b.x, b.y, 0.018f, 0.034f, angle, 0.40f, 0.85f, 1.00f, 0.45f);
        emit(out, SHAPE_QUAD, b.x, b.y, 0.008f, 0.025f, angle, 1.00f, 1.00f, 0.80f, 1.00f);
    }

    // explosion flash rings (expanding, fading)
    for (auto& e : explosions_) {
        float t = e.t / e.maxLife;                // 0→1
        float s = e.radius * (1.0f + t * 3.5f);  // expand outward
        float a = (1.0f - t) * 0.85f;
        // Flash starts white-orange, fades toward asteroid colour
        float fr = 1.0f,        fg = 0.65f + e.cr * 0.35f, fb = 0.10f;
        emit(out, SHAPE_ASTEROID, e.x, e.y, s, s, 0.0f, fr, fg, fb, a);
    }

    // debris fragments
    for (auto& p : particles_) {
        float life = 1.0f - p.t / p.maxLife;     // 1→0
        float sz   = p.size * life;
        emit(out, SHAPE_ASTEROID, p.x, p.y, sz, sz, p.rot, p.r, p.g, p.b, life);
    }

    // ship (PLAYING + TITLE). Blink while invulnerable.
    bool showShip = (state_ == PLAYING || state_ == TITLE);
    bool blinkOn = invuln_ <= 0.0f || fmodf(animTime_ * 12.0f, 1.0f) < 0.6f;
    if (showShip && blinkOn) {
        float bob = (state_ == TITLE) ? 0.02f * sinf(animTime_ * 2.0f) : 0.0f;
        float tilt = (state_ == TITLE) ? 0.0f : shipTilt_;

        // Engine exhaust flame — drawn first so it appears behind the hull.
        bool thrusting = (state_ == PLAYING) && thrustHeld();
        float flicker = sinf(animTime_ * 31.0f) * sinf(animTime_ * 47.0f);
        float flameH = shipScale_ * (thrusting ? (0.55f + 0.08f * flicker) : 0.30f);
        float flameW = flameH * 0.45f;
        float ex = shipX_ + sinf(tilt) * shipScale_ * 0.88f;
        float ey = shipY_ + bob + cosf(tilt) * shipScale_ * 0.88f;
        float fg = thrusting ? (0.65f + 0.15f * flicker) : 0.45f;
        emit(out, SHAPE_SHIP_NOSE, ex, ey, flameW, flameH,
             3.14159265f + tilt, 1.0f, fg, 0.08f, 0.0f);

        // Hull — three layers matching the app icon style:
        // 1. Wide dark blue-gray wings (back)
        emit(out, SHAPE_SHIP_WINGS, shipX_, shipY_ + bob, shipScale_, shipScale_, tilt,
             0.22f, 0.42f, 0.65f, 1.0f);
        // 2. Narrow bright cyan fuselage (middle)
        emit(out, SHAPE_SHIP_BODY, shipX_, shipY_ + bob, shipScale_, shipScale_, tilt,
             0.28f, 0.72f, 0.92f, 1.0f);
        // 3. Bright white nose spike (front)
        emit(out, SHAPE_SHIP_NOSE, shipX_, shipY_ + bob, shipScale_, shipScale_, tilt,
             0.88f, 0.97f, 1.00f, 1.0f);

        // Shield bubble when active
        if (shieldActive_) {
            float pulse = 0.60f + 0.40f * sinf(animTime_ * 9.0f);
            float sr = shipScale_ * 1.85f;
            emit(out, SHAPE_ASTEROID, shipX_, shipY_ + bob, sr, sr, animTime_ * 1.8f,
                 0.30f, 0.55f, 1.00f, pulse * 0.55f);
        }
        // Speed-boost glow trail behind ship
        if (speedBoostActive_) {
            float g2 = 0.50f + 0.40f * sinf(animTime_ * 14.0f);
            emit(out, SHAPE_SHIP_NOSE, shipX_, shipY_ + bob + shipScale_ * 1.2f,
                 shipScale_ * 0.5f, shipScale_ * 0.6f, 3.14159265f + tilt,
                 1.0f, 0.85f, 0.10f, g2 * 0.70f);
        }
    }

    // HUD during gameplay — drawn without screen shake so it stays readable on hit.
    if (state_ == PLAYING || state_ == LEVEL_CLEAR) {
        ShakeOff noShake(shakeX_, shakeY_);

        float h = 0.085f;
        float w = h * 0.60f;
        float step = w * 1.45f;
        // score top-left
        drawNumber(out, score_, -asp_ + 0.06f + w * 0.5f, -0.90f, h,
                   1.0f, 1.0f, 1.0f, 1.0f);
        // level number top-right (yellow), right-aligned — supports 2 digits at level 10
        int nd = numDigits(level_);
        float levelFirstCx = asp_ - 0.06f - w * 0.5f - (float)(nd - 1) * step;
        drawNumber(out, level_, levelFirstCx, -0.90f, h,
                   1.0f, 0.85f, 0.2f, 1.0f);
        // lives as small ship icons, top-center
        float ls = 0.03f, gap = 0.085f;
        float startX = -(float)(lives_ - 1) * gap * 0.5f;
        for (int i = 0; i < lives_; i++) {
            emit(out, SHAPE_SHIP_WINGS, startX + (float)i * gap, -0.90f, ls, ls, 0.0f,
                 0.22f, 0.42f, 0.65f, 1.0f);
            emit(out, SHAPE_SHIP_BODY,  startX + (float)i * gap, -0.90f, ls, ls, 0.0f,
                 0.28f, 0.72f, 0.92f, 1.0f);
        }

        drawPowerUpHUD(out);
        drawBossHealthBar(out);
        // Combo indicator — centered on screen.
        drawComboIndicator(out);
    }

    // Touch button zones (only during active gameplay) — also shake-free.
    if (state_ == PLAYING) {
        ShakeOff noShake(shakeX_, shakeY_);
        bool th = thrustHeld(), fh = fireHeld();
        // Zone quads in world space, from the same fractions the input uses.
        const float zoneTop = 2.0f * kZoneTopY - 1.0f;
        const float zoneCy  = (zoneTop + 1.0f) * 0.5f, zoneHh = (1.0f - zoneTop) * 0.5f;
        const float thrustCx = -asp_ * (1.0f - kThrustZoneW), thrustHw = asp_ * kThrustZoneW;
        const float fireCx   =  asp_ * kFireZoneX, fireHw = asp_ * (1.0f - kFireZoneX);
        // Thrust zone: bottom-left corner
        emit(out, SHAPE_QUAD, thrustCx, zoneCy, thrustHw, zoneHh, 0.0f,
             0.30f, 0.60f, 1.00f, th ? 0.22f : 0.08f);
        emit(out, SHAPE_SHIP_WINGS, thrustCx, zoneCy, 0.035f, 0.035f, 0.0f,
             0.22f, 0.42f, 0.65f, th ? 1.00f : 0.40f);
        emit(out, SHAPE_SHIP_BODY,  thrustCx, zoneCy, 0.035f, 0.035f, 0.0f,
             0.28f, 0.72f, 0.92f, th ? 1.00f : 0.40f);
        // Fire zone: bottom-right corner
        emit(out, SHAPE_QUAD, fireCx, zoneCy, fireHw, zoneHh, 0.0f,
             1.00f, 0.40f, 0.20f, fh ? 0.22f : 0.08f);
        emit(out, SHAPE_QUAD, fireCx, zoneCy, 0.011f, 0.030f, 0.0f,
             1.00f, 0.90f, 0.40f, fh ? 1.00f : 0.45f);
    }

    float pulse = 0.5f + 0.5f * sinf(animTime_ * 4.0f);

    if (state_ == TITLE) {
        // Title text
        drawText(out, "VULKAN",    0.0f, -0.80f, 0.115f, 0.35f, 0.78f, 1.00f, 1.0f);
        drawText(out, "ASTEROIDS", 0.0f, -0.63f, 0.085f, 0.28f, 0.62f, 0.82f, 1.0f);

        // big title ship — three layers
        emit(out, SHAPE_SHIP_WINGS, 0.0f, -0.15f, 0.22f, 0.22f, 0.0f,
             0.22f, 0.42f, 0.65f, 1.0f);
        emit(out, SHAPE_SHIP_BODY,  0.0f, -0.15f, 0.22f, 0.22f, 0.0f,
             0.28f, 0.72f, 0.92f, 1.0f);
        emit(out, SHAPE_SHIP_NOSE,  0.0f, -0.15f, 0.22f, 0.22f, 0.0f,
             0.88f, 0.97f, 1.00f, 1.0f);

        // High score podium (top 3, gold/silver/bronze)
        static const float kPodR[3] = {1.00f, 0.78f, 0.72f};
        static const float kPodG[3] = {0.85f, 0.82f, 0.47f};
        static const float kPodB[3] = {0.20f, 0.92f, 0.22f};
        const float hh = 0.052f, rowY[3] = {0.13f, 0.25f, 0.37f};
        for (int i = 0; i < 3; i++) {
            if (highScores_[i].score <= 0) break;
            float pr = kPodR[i], pg = kPodG[i], pb = kPodB[i];
            // Rank ship icon
            emit(out, SHAPE_SHIP_WINGS, -asp_*0.72f, rowY[i], 0.020f, 0.020f, 0.0f, pr*0.5f, pg*0.5f, pb*0.5f, 1.0f);
            emit(out, SHAPE_SHIP_BODY,  -asp_*0.72f, rowY[i], 0.020f, 0.020f, 0.0f, pr,      pg,      pb,      1.0f);
            // Score
            drawNumber(out, highScores_[i].score, -asp_*0.50f, rowY[i], hh, pr, pg, pb, 1.0f);
            // Level digit (right-aligned)
            drawNumber(out, highScores_[i].level, asp_*0.62f, rowY[i], hh, pr, pg, pb, 0.80f);
        }

        // pulsing tap hint
        float s = 0.05f + 0.015f * pulse;
        emit(out, SHAPE_SHIP_WINGS, 0.0f, 0.45f, s, s, 0.0f,
             0.22f, 0.42f, 0.65f, 0.4f + 0.6f * pulse);
        emit(out, SHAPE_SHIP_BODY,  0.0f, 0.45f, s, s, 0.0f,
             0.28f, 0.72f, 0.92f, 0.4f + 0.6f * pulse);
    } else if (state_ == LEVEL_CLEAR) {
        // big green level number just cleared
        drawNumber(out, level_, 0.0f, -0.05f, 0.42f, 0.4f, 1.0f, 0.5f, 1.0f);
    } else if (state_ == GAME_OVER) {
        emit(out, SHAPE_QUAD, 0.0f, 0.0f, asp_, 1.0f, 0.0f,
             0.6f, 0.05f, 0.08f, 0.32f + 0.10f * pulse);
        int n = numDigits(score_);
        float fh = endScoreHeight(n, asp_), fw = fh * 0.6f * 1.45f;
        float firstCx = -(float)(n - 1) * fw * 0.5f;
        // Gold pulsing score if new high score, white otherwise
        float sr = 1.0f, sg = newHighScore_ ? (0.75f + 0.20f*pulse) : 1.0f, sb = newHighScore_ ? 0.10f : 1.0f;
        drawNumber(out, score_, firstCx, -0.05f, fh, sr, sg, sb, 1.0f);
        // Rank digit above score when it's a new high score
        if (newHighScore_ && newHighScoreRank_ >= 0 && newHighScoreRank_ < 3) {
            static const float kPodR[3] = {1.00f, 0.78f, 0.72f};
            static const float kPodG[3] = {0.85f, 0.82f, 0.47f};
            static const float kPodB[3] = {0.20f, 0.92f, 0.22f};
            int ri = newHighScoreRank_;
            drawDigit(out, ri + 1, 0.0f, -0.42f, 0.14f,
                      kPodR[ri], kPodG[ri], kPodB[ri], 0.65f + 0.35f * pulse);
        }
        emit(out, SHAPE_SHIP_WINGS, 0.0f, 0.5f, 0.05f, 0.05f, 0.0f,
             0.22f, 0.42f, 0.65f, 0.3f + 0.6f * pulse);
        emit(out, SHAPE_SHIP_BODY,  0.0f, 0.5f, 0.05f, 0.05f, 0.0f,
             0.28f, 0.72f, 0.92f, 0.3f + 0.6f * pulse);
    } else if (state_ == WIN) {
        emit(out, SHAPE_QUAD, 0.0f, 0.0f, asp_, 1.0f, 0.0f,
             0.1f, 0.5f, 0.15f, 0.30f + 0.10f * pulse);
        int n = numDigits(score_);
        float fh = endScoreHeight(n, asp_), fw = fh * 0.6f * 1.45f;
        float firstCx = -(float)(n - 1) * fw * 0.5f;
        float sr = 1.0f, sg = newHighScore_ ? (0.80f + 0.15f*pulse) : 0.9f, sb = newHighScore_ ? 0.10f : 0.3f;
        drawNumber(out, score_, firstCx, -0.05f, fh, sr, sg, sb, 1.0f);
        if (newHighScore_ && newHighScoreRank_ >= 0 && newHighScoreRank_ < 3) {
            static const float kPodR[3] = {1.00f, 0.78f, 0.72f};
            static const float kPodG[3] = {0.85f, 0.82f, 0.47f};
            static const float kPodB[3] = {0.20f, 0.92f, 0.22f};
            int ri = newHighScoreRank_;
            drawDigit(out, ri + 1, 0.0f, -0.42f, 0.14f,
                      kPodR[ri], kPodG[ri], kPodB[ri], 0.65f + 0.35f * pulse);
        }
        emit(out, SHAPE_SHIP_WINGS, 0.0f, 0.5f, 0.05f, 0.05f, 0.0f,
             0.22f, 0.42f, 0.65f, 0.3f + 0.6f * pulse);
        emit(out, SHAPE_SHIP_BODY,  0.0f, 0.5f, 0.05f, 0.05f, 0.0f,
             0.28f, 0.72f, 0.92f, 0.3f + 0.6f * pulse);
    }

    // ── Gear button — shake-free, top-right corner, every state but SETTINGS ─
    if (state_ != SETTINGS) {
        ShakeOff noShake(shakeX_, shakeY_);
        float gearWX = asp_ - kGearOffsetX, gearWY = kGearWY;
        float gearA  = 0.55f + 0.12f * sinf(animTime_ * 2.0f);
        float gr = autoRunActive_ ? 0.25f : 0.55f;
        float gg = autoRunActive_ ? 1.00f : 0.62f;
        float gb = autoRunActive_ ? 0.35f : 0.78f;
        drawGearIcon(out, gearWX, gearWY, 0.046f, gr, gg, gb, gearA);
        if (autoRunActive_ && (state_ == PLAYING || state_ == LEVEL_CLEAR)) {
            float autoA = 0.38f + 0.28f * sinf(animTime_ * 3.5f);
            drawText(out, "AUTO", gearWX - 0.18f, gearWY, 0.030f,
                     0.25f, 1.00f, 0.38f, autoA);
        }
    }

    // ── Settings overlay — drawn last so it covers everything ────────────────
    if (state_ == SETTINGS) {
        ShakeOff noShake(shakeX_, shakeY_);
        drawSettingsScreen(out);
    }
}
