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

    // The activity lost focus or was paused mid-game: drop held pointers and
    // park an active game on the settings overlay so nothing moves until the
    // player taps BACK.
    void onAppPause();

    // True on static screens (title / game over / win) where the main loop may
    // throttle the frame rate to save battery.
    bool isIdleScreen() const {
        return state_ == TITLE || state_ == GAME_OVER || state_ == WIN;
    }

public:
    // Exposed so tests can construct specific asteroid variants.
    enum AsteroidType { AT_NORMAL = 0, AT_FAST = 1, AT_ARMORED = 2 };

private:
    enum State { TITLE, PLAYING, LEVEL_CLEAR, GAME_OVER, WIN, SETTINGS };

    struct Asteroid {
        float x = 0.0f, y = 0.0f, vx = 0.0f, vy = 0.0f, r = 0.0f, rot = 0.0f, spin = 0.0f;
        float cr = 0.0f, cg = 0.0f, cb = 0.0f;
        float seed   = 0.0f;            // per-rock noise seed for STYLE_ROCK
        float squash = 1.0f;            // visual x/y aspect (collision stays circular)
        int   shape  = SHAPE_ASTEROID;  // silhouette variant
        int gen = 0;      // 0=large (splits), 1=medium (splits), 2=small (no split)
        int hp = 1;       // hit points: 1 normal/fast, 2 armored
        AsteroidType type = AT_NORMAL;
        bool alive = false;
    };
    struct Bullet { float x = 0.0f, y = 0.0f, vx = 0.0f, vy = 0.0f, life = 0.0f; bool alive = false; };
    struct Particle {
        float x = 0.0f, y = 0.0f, vx = 0.0f, vy = 0.0f;
        float rot = 0.0f, spin = 0.0f;
        float t = 0.0f, maxLife = 0.0f, size = 0.0f;
        float r = 0.0f, g = 0.0f, b = 0.0f;
        bool alive = false;
    };
    struct Explosion {
        float x = 0.0f, y = 0.0f, radius = 0.0f;
        float t = 0.0f, maxLife = 0.0f;
        float cr = 0.0f, cg = 0.0f, cb = 0.0f;
        bool alive = false;
    };
    struct Star { float x = 0.0f, y = 0.0f, size = 0.0f; };
    struct Pointer { bool active = false; float x = 0.0f, y = 0.0f; };
    struct HighScore { long score = 0; int level = 0; };

    enum PowerUpType { PU_SHIELD = 0, PU_SPREAD = 1, PU_SPEED = 2 };
    struct PowerUp {
        float x = 0.0f, y = 0.0f, vy = 0.0f, rot = 0.0f, spin = 0.0f;
        PowerUpType type = PU_SHIELD;
        bool alive = false;
    };

    // Level-10 boss: large multi-HP asteroid with sinusoidal drift.
    struct Boss {
        float x = 0.0f, y = 0.0f, vy = 0.0f, t = 0.0f, rot = 0.0f, spin = 0.0f, r = 0.0f;
        int   hp = 0, maxHp = 0;
        bool  alive = false;
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
    // PLAYING-state update steps, called in this order from update().
    void updatePowerUps(float dt);
    void updateBossFight(float dt);
    void updateShipMotion(float dt);
    void updateAsteroids(float dt);
    void updateBullets(float dt);
    void removeDeadEntities();
    void checkLevelClear();
    void applyShipHit();
    void spawnPowerUp(float x, float y);
    void spawnBoss();
    bool leftHeld() const;
    bool rightHeld() const;
    bool thrustHeld() const;
    bool fireHeld() const;
    bool inThrustZone(const Pointer& p) const;
    bool inFireZone(const Pointer& p) const;
    // Touch-screen pixel -> world coordinates.
    void toWorld(float px, float py, float& wx, float& wy) const;
    void clampShipX();
    void loadHighScores();
    void saveHighScores();
    void checkHighScore();
    void spawnDebris(float x, float y, float r, float cr, float cg, float cb);

    // emit one shape: world centre (wx,wy), world half-extents (sx,sy), rotation, rgba.
    // style/seed select the fragment-shader fill (STYLE_FLAT default, STYLE_ROCK + seed).
    void emit(std::vector<DrawCmd>& out, int shape, float wx, float wy,
              float sx, float sy, float rot, float r, float g, float b, float a,
              float style = (float)STYLE_FLAT, float seed = 0.0f) const;
    void drawDigit(std::vector<DrawCmd>& out, int d, float cx, float cy,
                   float h, float r, float g, float b, float a) const;
    void drawNumber(std::vector<DrawCmd>& out, long value, float leftX, float cy,
                    float h, float r, float g, float b, float a) const;
    void drawLetter(std::vector<DrawCmd>& out, char ch, float cx, float cy,
                    float h, float r, float g, float b, float a) const;
    void drawText(std::vector<DrawCmd>& out, const char* text, float cx, float cy,
                  float h, float r, float g, float b, float a) const;
    void drawPowerUpHUD(std::vector<DrawCmd>& out) const;
    void drawComboIndicator(std::vector<DrawCmd>& out) const;
    void drawBossHealthBar(std::vector<DrawCmd>& out) const;
    void drawGearIcon(std::vector<DrawCmd>& out, float cx, float cy, float size,
                      float r, float g, float b, float a) const;
    void drawSettingsScreen(std::vector<DrawCmd>& out) const;
    void loadSettings();
    void saveSettings();
    void updateAutoRun(float dt);
    bool isGearTap(float px, float py) const;
    static int numDigits(long v);

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
    /** Seed the game's generator; a launch should not show the same sky every time. */
    void seedRng(uint32_t s) { rng_ = s ? s : 1u; }
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
                           AsteroidType type = AT_NORMAL, int hp = 1,
                           float vx = 0.0f, float vy = 0.0f) {
        Asteroid a;
        a.x = x; a.y = y; a.vx = vx; a.vy = vy;
        a.r = r; a.rot = 0; a.spin = 0;
        a.cr = 0.6f; a.cg = 0.5f; a.cb = 0.4f;
        a.gen = gen; a.hp = hp; a.type = type; a.alive = true;
        asteroids_.push_back(a);
    }
    // type: -1 keeps the random pick, else PowerUpType (0 shield, 1 spread, 2 speed).
    void spawnTestPowerUp(float x, float y, int type = -1) {
        spawnPowerUp(x, y);
        if (type >= 0) powerUps_.back().type = (PowerUpType)type;
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
    bool aiWantsCollectForTest()   const { return aiWantsCollect_; }
    bool  aiHasTargetForTest()     const { return aiHasTarget_; }
    float aiAimXForTest()          const { return aiAimX_; }
    void setScoreForTest(long s)         { score_ = s; }
    void clearInvulnForTest()            { invuln_ = 0.0f; }
    bool parkedForTest()           const { return parked_; }
    bool isGameOverForTest()       const { return state_ == GAME_OVER; }
    bool isWinForTest()            const { return state_ == WIN; }
    void startLevelForTest(int level)    { startGame(); startLevel(level); }
    // Total asteroid slots held, dead ones included (leak regression test).
    int  asteroidStorageForTest()  const { return (int)asteroids_.size(); }
    long topHighScoreForTest()     const { return highScores_[0].score; }
    long highScoreForTest(int i)      const { return highScores_[i].score; }
    int  highScoreLevelForTest(int i) const { return highScores_[i].level; }
    int  newHighScoreRankForTest()    const { return newHighScoreRank_; }
    static constexpr int kMaxScoresForTest = kMaxScores;
    // Auto Run bench accessors (test/autorun_bench.cpp).
    void seedRngForTest(uint32_t s)      { seedRng(s); }
    float starXForTest(int i)      const { return stars_[i].x; }
    float animTimeForTest()        const { return animTime_; }
    int  levelForTest()            const { return level_; }
    bool isPlayingForTest()        const { return state_ == PLAYING; }
    bool isLevelClearForTest()     const { return state_ == LEVEL_CLEAR; }
    int  shotsFiredForTest()       const { return shotsFired_; }
    int  bulletsLostForTest()      const { return bulletsLost_; }
    int  killsForTest()            const { return kills_; }
    int  hitsTakenForTest()        const { return hitsTaken_; }
    int  bossHitsTakenForTest()    const { return bossHits_; }
    int  shieldSavesForTest()      const { return shieldSaves_; }
    int  livesForTest()            const { return lives_; }
    void setLivesForTest(int n)          { lives_ = n; }
    long scoreForTest()            const { return score_; }
    float invulnForTest()          const { return invuln_; }
    int  goalForTest()             const { return goal_; }
    void setDodgedForTest(int n)         { dodgedThisLevel_ = n; }
    void applyShipHitForTest()           { applyShipHit(); }
    bool aiLeftForTest()           const { return aiLeft_; }
    bool aiRightForTest()          const { return aiRight_; }
    bool aiThrustForTest()         const { return aiThrust_; }
    bool thrustHeldForTest()       const { return thrustHeld(); }
    bool aiFireForTest()           const { return aiFire_; }
    float shipVyForTest()          const { return shipVy_; }
    // Position, velocity and radius of asteroid slot i (false when the slot is dead).
    bool asteroidForTest(int i, float& x, float& y, float& vx, float& vy, float& r) const {
        if (i < 0 || i >= (int)asteroids_.size() || !asteroids_[i].alive) return false;
        const Asteroid& a = asteroids_[i];
        x = a.x; y = a.y; vx = a.vx; vy = a.vy; r = a.r;
        return true;
    }
#endif

private:
    // --- viewport ---
    float vwf_ = 1.0f, vhf_ = 1.0f;   // viewport in pixels, floats for the hit-testing math
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
    // The round was parked on Settings by onAppPause(), not by the gear:
    // BACK then resumes it behind a breather (see update()).
    bool  parked_    = false;
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
    bool aiLeft_        = false;
    bool aiRight_       = false;
    bool aiThrust_      = false;
    bool aiFire_        = false;
    bool aiWantsCollect_ = false;  // last auto-run frame steered to collect a power-up
    bool  aiHasTarget_  = false;   // last auto-run frame had a rock or the boss to aim at
    float aiAimX_       = 0.0f;    // where it wanted the gun

    // --- run counters (read by the tests and the Auto Run bench) ---
    int shotsFired_  = 0;   // volleys fired
    int bulletsLost_ = 0;   // bullets that left the screen or timed out without a hit
    int kills_       = 0;   // asteroids destroyed by bullets
    int hitsTaken_   = 0;   // lives lost
    int bossHits_    = 0;   // lives lost to the boss
    int shieldSaves_ = 0;   // hits the shield absorbed

    std::vector<Asteroid>  asteroids_;
    std::vector<Bullet>    bullets_;
    std::vector<Particle>  particles_;
    std::vector<Explosion> explosions_;
    std::vector<PowerUp>   powerUps_;
    std::vector<Star>      stars_;
    std::vector<Star>      starsNear_;  // fast parallax layer
    uint32_t rng_ = 0x1234567u;
};
