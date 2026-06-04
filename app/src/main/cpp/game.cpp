#include "game.h"
#include <cmath>

// ---- level tuning (1..5, very easy -> hard) ----
static int clampLevel(int L) { return L < 1 ? 1 : (L > 5 ? 5 : L); }
static float levelFall(int L)   { return 0.50f + 0.13f * (clampLevel(L) - 1); }
static float levelSpawn(int L)  { return 1.00f - 0.11f * (clampLevel(L) - 1); }
static int   levelGoal(int L)   { return 10 + 2 * clampLevel(L); }

static const float kStarSpeed = 0.18f;

// 7-segment masks for digits 0..9 (bit a=1,b=2,c=4,d=8,e=16,f=32,g=64).
static const int kDigitSeg[10] = {63, 6, 91, 79, 102, 109, 125, 7, 127, 111};

Game::Game() {}

float Game::frand() {
    uint32_t x = rng_;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    rng_ = x;
    return (x & 0xFFFFFF) / float(0x1000000);
}
float Game::frange(float a, float b) { return a + (b - a) * frand(); }

void Game::setViewport(int w, int h) {
    if (w <= 0 || h <= 0) return;
    vw_ = w; vh_ = h;
    asp_ = (float)w / (float)h;
    if (stars_.empty()) {
        for (int i = 0; i < 60; i++) {
            Star s;
            s.x = frange(-1.0f, 1.0f);   // normalized; scaled by asp at draw time
            s.y = frange(-1.0f, 1.0f);
            s.size = frange(0.004f, 0.011f);
            stars_.push_back(s);
        }
    }
    float lim = asp_ - shipScale_;
    if (shipX_ > lim) shipX_ = lim;
    if (shipX_ < -lim) shipX_ = -lim;
}

bool Game::leftHeld() const {
    for (auto& p : pointers_) if (p.active && p.x < vw_ * 0.5f) return true;
    return false;
}
bool Game::rightHeld() const {
    for (auto& p : pointers_) if (p.active && p.x >= vw_ * 0.5f) return true;
    return false;
}

void Game::onPointerDown(int id, float x, float y) {
    if (id >= 0 && id < kMaxPointers) { pointers_[id].active = true; pointers_[id].x = x; }
    tapPending_ = true;
}
void Game::onPointerMove(int id, float x, float y) {
    if (id >= 0 && id < kMaxPointers && pointers_[id].active) pointers_[id].x = x;
}
void Game::onPointerUp(int id) {
    if (id >= 0 && id < kMaxPointers) pointers_[id].active = false;
}
void Game::onPointersCancel() {
    for (auto& p : pointers_) p.active = false;
}

void Game::startGame() {
    score_ = 0;
    lives_ = 3;
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
    invuln_ = 1.2f;        // brief grace at level start
    asteroids_.clear();
    state_ = PLAYING;
}

void Game::spawnAsteroid(bool ambient) {
    Asteroid a;
    a.r = frange(0.05f, 0.11f);
    a.x = frange(-asp_ + a.r, asp_ - a.r);
    a.y = -1.15f - a.r;
    a.vy = (ambient ? frange(0.30f, 0.55f) : fallSpeed_ * frange(0.85f, 1.15f));
    a.spin = frange(-2.0f, 2.0f);
    a.rot = frange(0.0f, 6.28f);
    float tint = frange(-0.08f, 0.08f);
    a.cr = 0.62f + tint;
    a.cg = 0.55f + tint * 0.6f;
    a.cb = 0.48f + tint * 0.4f;
    a.alive = true;
    asteroids_.push_back(a);
}

void Game::update(float dt) {
    if (dt > 0.05f) dt = 0.05f;   // clamp huge hitches
    animTime_ += dt;

    // Stars scroll in every state for a sense of motion.
    for (auto& s : stars_) {
        s.y += kStarSpeed * dt;
        if (s.y > 1.05f) { s.y = -1.05f; s.x = frange(-1.0f, 1.0f); }
    }

    bool tapped = tapPending_;
    tapPending_ = false;

    switch (state_) {
        case TITLE: {
            if (tapped) { startGame(); break; }
            spawnTimer_ -= dt;
            if (spawnTimer_ <= 0.0f) { spawnAsteroid(true); spawnTimer_ = 0.8f; }
            for (auto& a : asteroids_) {
                a.y += a.vy * dt; a.rot += a.spin * dt;
                if (a.y - a.r > 1.1f) a.alive = false;
            }
            break;
        }
        case PLAYING: {
            int dir = (rightHeld() ? 1 : 0) - (leftHeld() ? 1 : 0);
            shipX_ += dir * shipSpeed_ * dt;
            float lim = asp_ - shipScale_;
            if (shipX_ > lim) shipX_ = lim;
            if (shipX_ < -lim) shipX_ = -lim;

            if (invuln_ > 0.0f) invuln_ -= dt;

            spawnTimer_ -= dt;
            if (spawnTimer_ <= 0.0f) {
                spawnAsteroid(false);
                spawnTimer_ = spawnInterval_ * frange(0.8f, 1.2f);
            }

            for (auto& a : asteroids_) {
                a.y += a.vy * dt;
                a.rot += a.spin * dt;
                if (invuln_ <= 0.0f) {
                    float dx = a.x - shipX_, dy = a.y - shipY_;
                    float rad = a.r + shipR_;
                    if (dx * dx + dy * dy < rad * rad) {
                        a.alive = false;
                        lives_--;
                        invuln_ = 1.5f;
                        if (lives_ <= 0) { state_ = GAME_OVER; stateTimer_ = 0.0f; }
                    }
                }
                if (a.alive && a.y - a.r > 1.05f) {
                    a.alive = false;
                    dodgedThisLevel_++;
                    score_ += 5 * level_;
                }
            }
            for (size_t i = asteroids_.size(); i-- > 0;)
                if (!asteroids_[i].alive) asteroids_.erase(asteroids_.begin() + i);

            if (state_ == PLAYING && dodgedThisLevel_ >= goal_) {
                score_ += 100 * level_;
                state_ = LEVEL_CLEAR;
                stateTimer_ = 0.0f;
                asteroids_.clear();
            }
            break;
        }
        case LEVEL_CLEAR: {
            stateTimer_ += dt;
            if (stateTimer_ >= 1.8f) {
                if (level_ >= 5) { state_ = WIN; stateTimer_ = 0.0f; }
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
            for (size_t i = asteroids_.size(); i-- > 0;)
                if (!asteroids_[i].alive) asteroids_.erase(asteroids_.begin() + i);
            if (state_ == GAME_OVER && asteroids_.size() < 4) {
                spawnTimer_ -= dt;
                if (spawnTimer_ <= 0.0f) { spawnAsteroid(true); spawnTimer_ = 0.9f; }
            }
            if (tapped && stateTimer_ > 0.6f) { state_ = TITLE; asteroids_.clear(); }
            break;
        }
    }
}

// ---- drawing helpers ----
void Game::emit(std::vector<DrawCmd>& out, int shape, float wx, float wy,
                float sx, float sy, float rot, float r, float g, float b, float a) {
    float c = cosf(rot), s = sinf(rot);
    DrawCmd d;
    d.mtx[0] = sx * c / asp_;
    d.mtx[1] = -sy * s / asp_;
    d.mtx[2] = sx * s;
    d.mtx[3] = sy * c;
    d.tx = wx / asp_;
    d.ty = wy;
    d.color[0] = r; d.color[1] = g; d.color[2] = b; d.color[3] = a;
    d.shape = shape;
    out.push_back(d);
}

void Game::drawDigit(std::vector<DrawCmd>& out, int dgt, float cx, float cy,
                     float h, float r, float g, float b, float a) {
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

int Game::numDigits(int v) const {
    if (v <= 0) return 1;
    int n = 0;
    while (v > 0) { n++; v /= 10; }
    return n;
}

void Game::drawNumber(std::vector<DrawCmd>& out, int value, float firstCx, float cy,
                      float h, float r, float g, float b, float a) {
    if (value < 0) value = 0;
    int n = numDigits(value);
    float w = h * 0.60f;
    float step = w * 1.45f;
    int digits[12];
    int tmp = value, count = 0;
    if (value == 0) { digits[count++] = 0; }
    while (tmp > 0 && count < 12) { digits[count++] = tmp % 10; tmp /= 10; }
    // digits[] is reversed; draw most-significant first.
    for (int i = 0; i < n; i++) {
        int dgt = digits[n - 1 - i];
        drawDigit(out, dgt, firstCx + i * step, cy, h, r, g, b, a);
    }
}

void Game::render(std::vector<DrawCmd>& out) {
    // stars
    for (auto& s : stars_)
        emit(out, SHAPE_QUAD, s.x * asp_, s.y, s.size, s.size, 0.0f,
             0.55f, 0.6f, 0.78f, 0.7f);

    // asteroids
    for (auto& a : asteroids_)
        emit(out, SHAPE_ASTEROID, a.x, a.y, a.r, a.r, a.rot, a.cr, a.cg, a.cb, 1.0f);

    // ship (PLAYING + TITLE). Blink while invulnerable.
    bool showShip = (state_ == PLAYING || state_ == TITLE);
    bool blinkOn = invuln_ <= 0.0f || fmodf(animTime_ * 12.0f, 1.0f) < 0.6f;
    if (showShip && blinkOn) {
        float bob = (state_ == TITLE) ? 0.02f * sinf(animTime_ * 2.0f) : 0.0f;
        emit(out, SHAPE_SHIP, shipX_, shipY_ + bob, shipScale_, shipScale_, 0.0f,
             0.45f, 0.9f, 1.0f, 1.0f);
    }

    // HUD during gameplay
    if (state_ == PLAYING || state_ == LEVEL_CLEAR) {
        float h = 0.085f;
        float w = h * 0.60f;
        // score top-left
        drawNumber(out, (int)score_, -asp_ + 0.06f + w * 0.5f, -0.90f, h,
                   1.0f, 1.0f, 1.0f, 1.0f);
        // level digit top-right (yellow)
        drawDigit(out, level_, asp_ - 0.06f - w * 0.5f, -0.90f, h,
                  1.0f, 0.85f, 0.2f, 1.0f);
        // lives as small ship icons, top-center
        float ls = 0.03f, gap = 0.085f;
        float startX = -(lives_ - 1) * gap * 0.5f;
        for (int i = 0; i < lives_; i++)
            emit(out, SHAPE_SHIP, startX + i * gap, -0.90f, ls, ls, 0.0f,
                 0.45f, 0.9f, 1.0f, 1.0f);
    }

    float pulse = 0.5f + 0.5f * sinf(animTime_ * 4.0f);

    if (state_ == TITLE) {
        // big title ship + pulsing tap hint
        emit(out, SHAPE_SHIP, 0.0f, -0.15f, 0.22f, 0.22f, 0.0f,
             0.5f, 0.95f, 1.0f, 1.0f);
        float s = 0.05f + 0.015f * pulse;
        emit(out, SHAPE_SHIP, 0.0f, 0.45f, s, s, 0.0f,
             1.0f, 1.0f, 1.0f, 0.4f + 0.6f * pulse);
    } else if (state_ == LEVEL_CLEAR) {
        // big green level number just cleared
        drawNumber(out, level_, 0.0f, -0.05f, 0.42f, 0.4f, 1.0f, 0.5f, 1.0f);
    } else if (state_ == GAME_OVER) {
        // red wash + final score
        emit(out, SHAPE_QUAD, 0.0f, 0.0f, asp_, 1.0f, 0.0f,
             0.6f, 0.05f, 0.08f, 0.32f + 0.10f * pulse);
        int n = numDigits((int)score_);
        float fh = 0.30f, fw = fh * 0.6f * 1.45f;
        float firstCx = -(n - 1) * fw * 0.5f;
        drawNumber(out, (int)score_, firstCx, -0.05f, fh, 1.0f, 1.0f, 1.0f, 1.0f);
        // pulsing restart hint (small ship)
        emit(out, SHAPE_SHIP, 0.0f, 0.5f, 0.05f, 0.05f, 0.0f,
             1.0f, 1.0f, 1.0f, 0.3f + 0.6f * pulse);
    } else if (state_ == WIN) {
        emit(out, SHAPE_QUAD, 0.0f, 0.0f, asp_, 1.0f, 0.0f,
             0.1f, 0.5f, 0.15f, 0.30f + 0.10f * pulse);
        int n = numDigits((int)score_);
        float fh = 0.30f, fw = fh * 0.6f * 1.45f;
        float firstCx = -(n - 1) * fw * 0.5f;
        drawNumber(out, (int)score_, firstCx, -0.05f, fh, 1.0f, 0.9f, 0.3f, 1.0f);
        emit(out, SHAPE_SHIP, 0.0f, 0.5f, 0.05f, 0.05f, 0.0f,
             1.0f, 1.0f, 1.0f, 0.3f + 0.6f * pulse);
    }
}

void Game::clearColor(float out[3]) const {
    out[0] = 0.03f; out[1] = 0.04f; out[2] = 0.09f;
}
