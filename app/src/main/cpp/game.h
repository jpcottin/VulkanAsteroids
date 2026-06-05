#pragma once
#include <vector>
#include "common.h"

class AudioEngine;

// All gameplay: ship, asteroids, 5 progressing levels, score, lives, HUD.
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

private:
    enum State { TITLE, PLAYING, LEVEL_CLEAR, GAME_OVER, WIN };

    struct Asteroid {
        float x, y, r, vy, rot, spin;
        float cr, cg, cb;
        bool alive;
    };
    struct Bullet { float x, y, vy, life; bool alive; };
    struct Star { float x, y, size; };
    struct Pointer { bool active; float x, y; };

    struct HighScore { long score = 0; int level = 0; };
    static const int kMaxScores = 5;

    // --- helpers ---
    float frand();
    float frange(float a, float b);
    void startGame();
    void startLevel(int level);
    void spawnAsteroid(bool ambient);
    bool leftHeld() const;
    bool rightHeld() const;
    bool thrustHeld() const;
    bool fireHeld() const;
    void loadHighScores();
    void saveHighScores();
    void checkHighScore();

    // emit one shape: world centre (wx,wy), world half-extents (sx,sy), rotation, rgba.
    void emit(std::vector<DrawCmd>& out, int shape, float wx, float wy,
              float sx, float sy, float rot, float r, float g, float b, float a);
    void drawDigit(std::vector<DrawCmd>& out, int d, float cx, float cy,
                   float h, float r, float g, float b, float a);
    void drawNumber(std::vector<DrawCmd>& out, int value, float leftX, float cy,
                    float h, float r, float g, float b, float a);
    int numDigits(int v) const;

    // --- audio ---
    AudioEngine* audio_ = nullptr;

    // --- high scores ---
    HighScore highScores_[kMaxScores] = {};
    bool newHighScore_    = false;
    int  newHighScoreRank_= -1;
    char dataPath_[512]   = {};

public:
    void setAudioEngine(AudioEngine* a) { audio_ = a; }
    void setDataPath(const char* path);

    // --- test / debug accessors ---
public:
    float shipX() const { return shipX_; }
    float shipY() const { return shipY_; }
    int   bulletCount() const { return (int)bullets_.size(); }
    long  score() const { return score_; }
    int   lives() const { return lives_; }

private:
    // --- viewport ---
    int vw_ = 1, vh_ = 1;
    float asp_ = 0.5f;

    // --- input ---
    static const int kMaxPointers = 16;
    Pointer pointers_[kMaxPointers] = {};
    bool tapPending_ = false;

    // --- state ---
    State state_ = TITLE;
    float animTime_ = 0.0f;
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

    // --- progression / scoring ---
    int level_ = 1;
    int lives_ = 3;
    long score_ = 0;
    int dodgedThisLevel_ = 0;
    int goal_ = 12;
    float spawnTimer_ = 0.0f;
    float spawnInterval_ = 1.0f;
    float fallSpeed_ = 0.5f;

    std::vector<Asteroid> asteroids_;
    std::vector<Bullet> bullets_;
    std::vector<Star> stars_;
    uint32_t rng_ = 0x1234567u;
};
