#include <gtest/gtest.h>
#include "game.h"

// Viewport used for all tests: portrait phone dimensions.
static constexpr int kW = 1080, kH = 2400;

// Touch zone centres (well inside each zone boundary).
// Thrust zone:  x < kW*0.30 && y > kH*0.72  →  (162, 2100)
// Fire zone:    x > kW*0.70 && y > kH*0.72  →  (918, 2100)
// Left move:    x < kW*0.50, outside thrust  →  (300, 800)
// Right move:   x >= kW*0.50, outside fire   →  (800, 800)
static constexpr float kThrustX = 162.f, kThrustY = 2100.f;
static constexpr float kFireX   = 918.f, kFireY   = 2100.f;
static constexpr float kLeftX   = 300.f, kMidY    = 800.f;
static constexpr float kRightX  = 800.f;

// Put the game into PLAYING state (tap screen centre, advance one frame, release).
static void startPlaying(Game& g) {
    g.setViewport(kW, kH);
    g.onPointerDown(0, kW * 0.5f, kH * 0.5f);
    g.update(0.016f);
    g.onPointerUp(0);
}

// ── Initial state ──────────────────────────────────────────────────────────────

TEST(InitialState, LivesAndScore) {
    Game g;
    startPlaying(g);
    EXPECT_EQ(g.lives(), 3);
    EXPECT_EQ(g.score(), 0L);
}

TEST(InitialState, NoBullets) {
    Game g;
    startPlaying(g);
    EXPECT_EQ(g.bulletCount(), 0);
}

TEST(InitialState, ShipCentred) {
    Game g;
    startPlaying(g);
    EXPECT_NEAR(g.shipX(), 0.0f, 0.05f);
}

// ── Gravity & thrust ──────────────────────────────────────────────────────────

TEST(ShipPhysics, GravityPullsShipDown) {
    Game g;
    startPlaying(g);
    float y0 = g.shipY();
    g.update(0.5f);  // half a second — enough to measure drift
    EXPECT_GT(g.shipY(), y0);
}

TEST(ShipPhysics, ShipClampsAtBottom) {
    Game g;
    startPlaying(g);
    g.update(5.0f);  // long free-fall
    EXPECT_LE(g.shipY(), 0.92f);
}

TEST(ShipPhysics, ThrustMovesShipUp) {
    Game g;
    startPlaying(g);
    g.update(1.0f);               // let ship settle near bottom
    float yBefore = g.shipY();
    g.onPointerDown(0, kThrustX, kThrustY);
    g.update(0.4f);               // thrust for 0.4 s
    g.onPointerUp(0);
    EXPECT_LT(g.shipY(), yBefore);
}

TEST(ShipPhysics, ShipClampsAtTop) {
    Game g;
    startPlaying(g);
    g.onPointerDown(0, kThrustX, kThrustY);
    g.update(5.0f);  // thrust for a long time
    g.onPointerUp(0);
    EXPECT_GE(g.shipY(), 0.12f);
}

// ── Input zone isolation ───────────────────────────────────────────────────────

TEST(InputZones, ThrustZoneDoesNotMovShipLeft) {
    // A pointer in the thrust zone must not trigger leftHeld().
    Game g;
    startPlaying(g);
    g.update(1.0f);  // let ship reach equilibrium first
    float xBefore = g.shipX();
    g.onPointerDown(0, kThrustX, kThrustY);
    g.update(0.2f);
    g.onPointerUp(0);
    // If leftHeld() fired, shipX would have drifted left (< xBefore).
    EXPECT_NEAR(g.shipX(), xBefore, 0.01f);
}

TEST(InputZones, FireZoneDoesNotMoveShipRight) {
    Game g;
    startPlaying(g);
    g.update(1.0f);
    float xBefore = g.shipX();
    g.onPointerDown(0, kFireX, kFireY);
    g.update(0.2f);
    g.onPointerUp(0);
    EXPECT_NEAR(g.shipX(), xBefore, 0.01f);
}

TEST(InputZones, LeftZoneMoveShipLeft) {
    Game g;
    startPlaying(g);
    // Start centred. Left-zone touch should push ship left.
    float xBefore = g.shipX();
    g.onPointerDown(0, kLeftX, kMidY);
    g.update(0.2f);
    g.onPointerUp(0);
    EXPECT_LT(g.shipX(), xBefore);
}

TEST(InputZones, RightZoneMoveShipRight) {
    Game g;
    startPlaying(g);
    float xBefore = g.shipX();
    g.onPointerDown(0, kRightX, kMidY);
    g.update(0.2f);
    g.onPointerUp(0);
    EXPECT_GT(g.shipX(), xBefore);
}

// ── Bullets ────────────────────────────────────────────────────────────────────

TEST(Bullets, FireZoneSpawnsBullet) {
    Game g;
    startPlaying(g);
    g.onPointerDown(0, kFireX, kFireY);
    g.update(0.016f);
    g.onPointerUp(0);
    EXPECT_GT(g.bulletCount(), 0);
}

TEST(Bullets, CooldownLimitsRapidFire) {
    Game g;
    startPlaying(g);
    g.onPointerDown(0, kFireX, kFireY);
    g.update(0.016f);           // first bullet spawned
    int after1 = g.bulletCount();
    g.update(0.016f);           // still within 0.22 s cooldown
    int after2 = g.bulletCount();
    g.onPointerUp(0);
    EXPECT_EQ(after1, after2);  // no second bullet yet
}

TEST(Bullets, LeftZoneDoesNotFire) {
    Game g;
    startPlaying(g);
    g.onPointerDown(0, kLeftX, kMidY);
    g.update(0.1f);
    g.onPointerUp(0);
    EXPECT_EQ(g.bulletCount(), 0);
}

TEST(Bullets, BulletsExpireOffScreen) {
    Game g;
    startPlaying(g);
    g.onPointerDown(0, kFireX, kFireY);
    g.update(0.016f);  // spawn bullet
    g.onPointerUp(0);
    ASSERT_GT(g.bulletCount(), 0);
    for (int i = 0; i < 50; i++) g.update(0.05f);  // 2.5 s > kBulletLife (2.2 s)
    EXPECT_EQ(g.bulletCount(), 0);
}

// ── Spread shot ────────────────────────────────────────────────────────────────

TEST(SpreadShot, NormalFireSpawnsOneBullet) {
    Game g;
    startPlaying(g);
    g.onPointerDown(0, kFireX, kFireY);
    g.update(0.016f);
    g.onPointerUp(0);
    EXPECT_EQ(g.bulletCount(), 1);
}

TEST(SpreadShot, SpreadFireSpawnsThreeBullets) {
    Game g;
    startPlaying(g);
    g.activatePowerUpForTest(1);  // spread
    g.onPointerDown(0, kFireX, kFireY);
    g.update(0.016f);
    g.onPointerUp(0);
    EXPECT_EQ(g.bulletCount(), 3);
}

TEST(SpreadShot, SpreadCooldownStillLimitsRapidFire) {
    Game g;
    startPlaying(g);
    g.activatePowerUpForTest(1);
    g.onPointerDown(0, kFireX, kFireY);
    g.update(0.016f);  // fires 3
    int after1 = g.bulletCount();
    g.update(0.016f);  // still within cooldown — no new bullets
    int after2 = g.bulletCount();
    g.onPointerUp(0);
    EXPECT_EQ(after1, after2);
}

// ── Speed boost ────────────────────────────────────────────────────────────────

TEST(SpeedBoost, BoostedShipMovesMoreThanUnboosted) {
    // Unboosted displacement
    Game g1;
    startPlaying(g1);
    g1.update(1.0f);  // let ship settle
    float x0 = g1.shipX();
    g1.onPointerDown(0, kRightX, kMidY);
    g1.update(0.3f);
    g1.onPointerUp(0);
    float dx_normal = g1.shipX() - x0;

    // Boosted displacement (same conditions + speed power-up)
    Game g2;
    startPlaying(g2);
    g2.update(1.0f);
    g2.activatePowerUpForTest(2);  // speed boost
    float x0b = g2.shipX();
    g2.onPointerDown(0, kRightX, kMidY);
    g2.update(0.3f);
    g2.onPointerUp(0);
    float dx_boosted = g2.shipX() - x0b;

    EXPECT_GT(dx_boosted, dx_normal);
}

// ── Asteroid splitting ─────────────────────────────────────────────────────────

TEST(Splitting, LargeAsteroidSplitsIntoTwo) {
    Game g;
    startPlaying(g);
    // Inject a gen-0 asteroid just above the bullet's travel path.
    g.spawnTestAsteroid(0.0f, 0.55f, 0.08f, 0);
    ASSERT_EQ(g.asteroidCount(), 1);
    g.onPointerDown(0, kFireX, kFireY);
    for (int i = 0; i < 5; i++) g.update(0.016f);  // ~80 ms for bullet to travel
    g.onPointerUp(0);
    // Original dies, 2 children spawned.
    EXPECT_EQ(g.asteroidCount(), 2);
}

TEST(Splitting, SmallAsteroidDoesNotSplit) {
    Game g;
    startPlaying(g);
    g.spawnTestAsteroid(0.0f, 0.55f, 0.06f, 2);  // gen=2: no split
    ASSERT_EQ(g.asteroidCount(), 1);
    g.onPointerDown(0, kFireX, kFireY);
    for (int i = 0; i < 5; i++) g.update(0.016f);
    g.onPointerUp(0);
    EXPECT_EQ(g.asteroidCount(), 0);
}

TEST(Splitting, MediumAsteroidSplitsOnce) {
    Game g;
    startPlaying(g);
    g.spawnTestAsteroid(0.0f, 0.55f, 0.07f, 1);  // gen=1: splits to gen-2 children
    ASSERT_EQ(g.asteroidCount(), 1);
    g.onPointerDown(0, kFireX, kFireY);
    for (int i = 0; i < 5; i++) g.update(0.016f);
    g.onPointerUp(0);
    EXPECT_EQ(g.asteroidCount(), 2);
}
