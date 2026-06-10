#pragma once
#include <vector>
#include <functional>
#include "common.h"

class AudioEngine;

// All gameplay: ship, asteroids, 10 progressing levels, score, lives, HUD.
// World coordinates: x in [-asp, asp], y in [-1, 1], y pointing DOWN (so asteroids
// fall from -y toward +y). asp = width/height. The renderer divides x by asp, so
// shapes stay round on screen.
class Game {
public:
    Game();
    void setViewport(int w, int h);

    // Input (pixel coordinates from the touch screen).
    void onPointerDown(int id, float x, float y);
    void onPointerMove(int id, float x, float y);
    void onPointerUp(int id);
    void onPointersCancel();

    void update(float dt);
    void render(std::vector<DrawCmd>& out);
    void clearColor(float out[3]) const;

public:
    // Exposed so tests can construct specific asteroid variants.
    enum AsteroidType { AT_NORMAL = 0, AT_FAST = 1, AT_ARMORED = 2 };

private:
    enum State { TITLE, PLAYING, LEVEL_CLEAR, GAME_OVER, WIN, SETTINGS };

    struct Asteroid {
        float x, y, vx, vy, r, rot, spin;
        float cr, cg, cb;
        float seed   = 0.0f;            // per-rock noise seed for STYLE_ROCK
        float squash = 1.0f;            // visual x/y aspect (collision stays circular)
        int   shape  = SHAPE_ASTEROID;  // silhouette variant
        int gen;          // 0=large (splits), 1=medium (splits), 2=small (no split)
        int hp;           // hit points: 1 normal/fast, 2 armored
        AsteroidType type;
        bool alive;
    };
    struct Bullet { float x, y, vx, vy, life; bool alive; };
    struct Particle {
        float x, y, vx, vy;
        float rot, spin;
        float t, maxLife, size;
        float r, g, b;
        bool alive;
    };
    struct Explosion {
        float x, y, radius;
        float t, maxLife;
        float cr, cg, cb;
        bool alive;
    };
    struct Star { float x, y, size; };
    struct Pointer { bool active; float x, y; };
    struct HighScore { long score = 0; int level = 0; };

    enum PowerUpType { PU_SHIELD = 0, PU_SPREAD = 1, PU_SPEED = 2 };
    struct PowerUp {
        float x, y, vy, rot, spin;
        PowerUpType type;
        bool alive;
    };

    // Level-10 boss: large multi-HP asteroid with sinusoidal drift.
    struct Boss {
        float x, y, vy, t, rot, spin, r;
        int   hp, maxHp;
        bool  alive;
    };

    static const int kMaxScores = 5;

    // --- helpers ---
    float frand();
    float frange(float a, float b);
    void startGame();
    void startLevel(int level);
    void spawnAsteroid(bool ambient);
    void splitAsteroid(const Asteroid& parent);
    void randomizeRockVisuals(Asteroid& a);
    void spawnPowerUp(float x, float y);
    void spawnBoss();
    bool leftHeld() const;
    bool rightHeld() const;
    bool thrustHeld() const;
    bool fireHeld() const;
    void loadHighScores();
    void saveHighScores();
    void checkHighScore();
    void spawnDebris(float x, float y, float r, float cr, float cg, float cb);

    // emit one shape: world centre (wx,wy), world half-extents (sx,sy), rotation, rgba.
    // style/seed select the fragment-shader fill (STYLE_FLAT default, STYLE_ROCK + seed).
    void emit(std::vector<DrawCmd>& out, int shape, float wx, float wy,
              float sx, float sy, float rot, float r, float g, float b, float a,
              float style = (float)STYLE_FLAT, float seed = 0.0f);
    void drawDigit(std::vector<DrawCmd>& out, int d, float cx, float cy,
                   float h, float r, float g, float b, float a);
    void drawNumber(std::vector<DrawCmd>& out, int value, float leftX, float cy,
                    float h, float r, float g, float b, float a);
    void drawLetter(std::vector<DrawCmd>& out, char ch, float cx, float cy,
                    float h, float r, float g, float b, float a);
    void drawText(std::vector<DrawCmd>& out, const char* text, float cx, float cy,
                  float h, float r, float g, float b, float a);
    void drawPowerUpHUD(std::vector<DrawCmd>& out);
    void drawComboIndicator(std::vector<DrawCmd>& out);
    void drawBossHealthBar(std::vector<DrawCmd>& out);
    void drawGearIcon(std::vector<DrawCmd>& out, float cx, float cy, float size,
                      float r, float g, float b, float a);
    void drawSettingsScreen(std::vector<DrawCmd>& out);
    void loadSettings();
    void saveSettings();
    void updateAutoRun(float dt);
    bool isGearTap(float px, float py) const;
    int numDigits(int v) const;

    // --- audio ---
    AudioEngine* audio_ = nullptr;

    // --- haptic ---
    std::function<void()> haptic_;

    // --- high scores ---
    HighScore highScores_[kMaxScores] = {};
    bool newHighScore_    = false;
    int  newHighScoreRank_= -1;
    char dataPath_[512]   = {};

public:
    void setAudioEngine(AudioEngine* a) { audio_ = a; }
    void setDataPath(const char* path);
    void setHapticCallback(std::function<void()> fn) { haptic_ = std::move(fn); }

    // --- test / debug accessors ---
public:
    float shipX() const { return shipX_; }
    float shipY() const { return shipY_; }
    int   bulletCount()  const { return (int)bullets_.size(); }
    int   asteroidCount() const {
        int n = 0;
        for (auto& a : asteroids_) if (a.alive) n++;
        return n;
    }
    long  score() const { return score_; }
    int   lives() const { return lives_; }
    int   combo() const { return comboCount_; }

#ifndef NDEBUG
    // Test-only helpers — excluded from release builds (NDEBUG defined).
    void activatePowerUpForTest(int type) {
        if (type == 0) { shieldActive_ = true;     shieldTimer_     = 10.0f; }
        if (type == 1) { spreadActive_ = true;     spreadTimer_     = 10.0f; }
        if (type == 2) { speedBoostActive_ = true; speedBoostTimer_ = 10.0f; }
    }
    void spawnTestAsteroid(float x, float y, float r, int gen,
                           AsteroidType type = AT_NORMAL, int hp = 1) {
        Asteroid a;
        a.x = x; a.y = y; a.vx = 0; a.vy = 0;
        a.r = r; a.rot = 0; a.spin = 0;
        a.cr = 0.6f; a.cg = 0.5f; a.cb = 0.4f;
        a.gen = gen; a.hp = hp; a.type = type; a.alive = true;
        asteroids_.push_back(a);
    }
    bool shieldActiveForTest()     const { return shieldActive_; }
    bool spreadActiveForTest()     const { return spreadActive_; }
    bool speedBoostActiveForTest() const { return speedBoostActive_; }
    void triggerNewGameForTest()         { startGame(); }
    bool bossAliveForTest()        const { return boss_.alive; }
    int  bossHpForTest()           const { return boss_.hp; }
    // Settings / auto-run accessors
    bool soundEnabledForTest()     const { return soundEnabled_; }
    bool autoRunActiveForTest()    const { return autoRunActive_; }
    bool inSettingsForTest()       const { return state_ == SETTINGS; }
    bool inTitleForTest()          const { return state_ == TITLE; }
    void setAutoRunForTest(bool v)       { autoRunActive_ = v; }
#endif

private:
    // --- viewport ---
    int vw_ = 1, vh_ = 1;
    float asp_ = 0.5f;

    // --- input ---
    static const int kMaxPointers = 16;
    Pointer pointers_[kMaxPointers] = {};
    bool  tapPending_ = false;
    float tapX_       = 0.0f;
    float tapY_       = 0.0f;

    // --- state ---
    State state_     = TITLE;
    State prevState_ = TITLE;
    float animTime_  = 0.0f;
    float stateTimer_ = 0.0f;

    // --- ship ---
    float shipX_ = 0.0f;
    float shipY_ = 0.80f;
    float shipVy_ = 0.0f;
    float shipTilt_ = 0.0f;
    float fireCooldown_ = 0.0f;
    const float shipScale_ = 0.075f;
    const float shipR_ = 0.058f;
    const float shipSpeed_ = 1.7f;
    float invuln_ = 0.0f;

    // --- screen shake ---
    float shakeAmt_ = 0.0f;
    float shakeX_   = 0.0f;
    float shakeY_   = 0.0f;

    // --- power-ups ---
    bool  shieldActive_      = false;
    float shieldTimer_       = 0.0f;
    bool  spreadActive_      = false;
    float spreadTimer_       = 0.0f;
    bool  speedBoostActive_  = false;
    float speedBoostTimer_   = 0.0f;
    float powerUpSpawnTimer_ = 0.0f;
    static constexpr float kPowerUpDuration = 8.0f;

    // --- combo ---
    int   comboCount_       = 0;
    float comboTimer_       = 0.0f;  // window before combo resets
    float comboDisplayTimer_= 0.0f;  // how long to show the x-multiplier text

    // --- boss (level 10) ---
    Boss boss_       = {};
    bool bossActive_ = false;

    // --- progression / scoring ---
    int level_ = 1;
    int lives_ = 3;
    long score_ = 0;
    int dodgedThisLevel_ = 0;
    int goal_ = 12;
    float spawnTimer_ = 0.0f;
    float spawnInterval_ = 1.0f;
    float fallSpeed_ = 0.5f;

    // --- settings (persisted) ---
    bool soundEnabled_  = true;
    bool autoRunActive_ = false;
    char settingsPath_[512] = {};

    // --- auto-run AI ---
    bool aiLeft_   = false;
    bool aiRight_  = false;
    bool aiThrust_ = false;
    bool aiFire_   = false;

    std::vector<Asteroid>  asteroids_;
    std::vector<Bullet>    bullets_;
    std::vector<Particle>  particles_;
    std::vector<Explosion> explosions_;
    std::vector<PowerUp>   powerUps_;
    std::vector<Star>      stars_;
    std::vector<Star>      starsNear_;  // fast parallax layer
    uint32_t rng_ = 0x1234567u;
};
