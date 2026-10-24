#include <gtest/gtest.h>
#include <vector>
#include <string>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <unistd.h>
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
    EXPECT_EQ(g.lives(), 5);
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

// update() clamps dt to 50 ms per call, so a long interval has to be fed as
// many small steps: 5 s of free fall from y=0.80 under 0.9 gravity would
// otherwise be one 50 ms step that never reaches the clamp.
static void runFor(Game& g, float seconds) {
    for (float t = 0.0f; t < seconds; t += 0.016f) g.update(0.016f);
}

TEST(ShipPhysics, ShipClampsAtBottom) {
    Game g;
    startPlaying(g);
    runFor(g, 5.0f);  // long free-fall: well past the 0.92 floor without a clamp
    EXPECT_NEAR(g.shipY(), 0.92f, 1e-4f);
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
    runFor(g, 5.0f);  // thrust for a long time
    g.onPointerUp(0);
    EXPECT_NEAR(g.shipY(), 0.12f, 1e-4f);
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

// ── Combo multiplier ──────────────────────────────────────────────────────────

TEST(Combo, FirstKillSetsComboToOne) {
    Game g;
    startPlaying(g);
    g.spawnTestAsteroid(0.0f, 0.55f, 0.08f, 0);
    g.onPointerDown(0, kFireX, kFireY);
    for (int i = 0; i < 5; i++) g.update(0.016f);
    g.onPointerUp(0);
    EXPECT_EQ(g.combo(), 1);
}

TEST(Combo, KillsInWindowIncrementCombo) {
    Game g;
    startPlaying(g);
    // One asteroid per shot, each fired after the 0.22 s cooldown but inside
    // the 1.8 s combo window: the counter must climb 1, 2, 3, 4 and then cap.
    for (int kill = 1; kill <= 5; kill++) {
        g.spawnTestAsteroid(0.0f, 0.55f, 0.06f, 2);
        g.onPointerDown(0, kFireX, kFireY);
        for (int i = 0; i < 5; i++) g.update(0.016f);   // fire + travel
        g.onPointerUp(0);
        EXPECT_EQ(g.combo(), kill < 4 ? kill : 4) << "after kill " << kill;
        for (int i = 0; i < 16; i++) g.update(0.016f);  // cooldown (< combo window)
    }
}

TEST(Combo, ComboScoreHigherThanBaseScore) {
    // With combo active, the second kill within the window yields more total score.
    Game g1, g2;
    startPlaying(g1); startPlaying(g2);

    // g1: single kill, no combo
    g1.spawnTestAsteroid(0.0f, 0.55f, 0.06f, 2);
    g1.onPointerDown(0, kFireX, kFireY);
    for (int i = 0; i < 5; i++) g1.update(0.016f);
    g1.onPointerUp(0);
    long score1 = g1.score();

    // g2: two kills — first fires, wait for cooldown, fire again within combo window
    g2.spawnTestAsteroid(0.0f, 0.55f, 0.06f, 2);
    g2.onPointerDown(0, kFireX, kFireY);
    for (int i = 0; i < 5; i++) g2.update(0.016f);  // first kill
    g2.onPointerUp(0);
    for (int i = 0; i < 16; i++) g2.update(0.016f);  // wait cooldown (0.22s)
    g2.spawnTestAsteroid(0.0f, 0.55f, 0.06f, 2);     // second asteroid
    g2.onPointerDown(0, kFireX, kFireY);
    for (int i = 0; i < 5; i++) g2.update(0.016f);  // second kill (combo x2)
    g2.onPointerUp(0);

    EXPECT_GT(g2.score(), score1 * 2);  // two kills with combo earn more than 2× single score
}

// ── Armored asteroids ─────────────────────────────────────────────────────────

TEST(ArmoredAsteroid, TakesTwoHitsToDestroy) {
    Game g;
    startPlaying(g);
    g.spawnTestAsteroid(0.0f, 0.55f, 0.09f, 2, Game::AT_ARMORED, 2);
    ASSERT_EQ(g.asteroidCount(), 1);

    // First shot — still alive (hp 2→1)
    g.onPointerDown(0, kFireX, kFireY);
    for (int i = 0; i < 5; i++) g.update(0.016f);
    g.onPointerUp(0);
    EXPECT_EQ(g.asteroidCount(), 1);

    // Wait for fire cooldown (0.22s = 14 frames at 0.016s)
    for (int i = 0; i < 16; i++) g.update(0.016f);

    // Second shot — now destroyed (hp 1→0)
    g.onPointerDown(0, kFireX, kFireY);
    for (int i = 0; i < 5; i++) g.update(0.016f);
    g.onPointerUp(0);
    EXPECT_EQ(g.asteroidCount(), 0);
}

// ── Boss ──────────────────────────────────────────────────────────────────────

TEST(Boss, SpawnsAtLevel10Only) {
    Game g;
    g.setViewport(kW, kH);
    g.startLevelForTest(9);
    EXPECT_FALSE(g.bossAliveForTest());
    g.startLevelForTest(10);
    EXPECT_TRUE(g.bossAliveForTest());
}

TEST(Boss, HasCorrectInitialHP) {
    Game g;
    g.setViewport(kW, kH);
    g.startLevelForTest(10);
    EXPECT_EQ(g.bossHpForTest(), 12);
}

TEST(Boss, DefeatingBossWinsTheGame) {
    Game g;
    g.setViewport(kW, kH);
    g.startLevelForTest(10);
    ASSERT_TRUE(g.bossAliveForTest());
    // The boss enters from the top and drifts sideways; the auto-run AI
    // tracks and shoots it. 12 hits at a 0.22 s cooldown is under 3 s, so
    // 60 s is generous even with evasive manoeuvres.
    g.setAutoRunForTest(true);
    g.setScoreForTest(500);
    for (int i = 0; i < 60 * 60 && !g.isWinForTest(); i++) g.update(0.016f);
    EXPECT_TRUE(g.isWinForTest());
    EXPECT_FALSE(g.bossAliveForTest());
    // Whatever the winning volley did after the last boss hit must not have
    // scored: the table was written on the WIN transition.
    EXPECT_EQ(g.topHighScoreForTest(), g.score());
}

// ── Title screen ──────────────────────────────────────────────────────────────

TEST(TitleScreen, DoesNotAccumulateDeadAsteroids) {
    // Ambient asteroids scroll off the bottom on the title screen; they must be
    // erased, not just flagged, or the idle screen grows without bound.
    Game g;
    g.setViewport(kW, kH);
    runFor(g, 60.0f);  // one idle minute
    EXPECT_EQ(g.asteroidStorageForTest(), g.asteroidCount());
    EXPECT_LT(g.asteroidStorageForTest(), 12);   // ~2.5 s to cross, one every 0.8 s
}

// ── Game over ─────────────────────────────────────────────────────────────────

TEST(GameOver, HighScoreMatchesFinalScore) {
    // The fatal hit must be the last thing that scores this frame: a dodge or
    // kill processed afterwards would show on screen but not in the table.
    Game g;
    startPlaying(g);
    g.setScoreForTest(500);
    // An asteroid about to leave the bottom edge (dodge points) and one on the
    // ship, plus a bullet lane target; lives burn down one per frame.
    for (int hit = 0; hit < 5; hit++) {
        g.clearInvulnForTest();
        g.spawnTestAsteroid(g.shipX(), g.shipY(), 0.06f, 2);        // fatal contact
        g.spawnTestAsteroid(0.3f, 1.08f, 0.02f, 2);                  // exits this frame
        g.update(0.016f);
    }
    ASSERT_TRUE(g.isGameOverForTest());
    EXPECT_EQ(g.topHighScoreForTest(), g.score());
}

// ── Power-up session isolation ─────────────────────────────────────────────────

TEST(PowerUp, NotCarriedToNewGame) {
    // Verify that power-ups active at game-over do NOT bleed into the next game.
    Game g;
    startPlaying(g);
    g.activatePowerUpForTest(1);  // spread
    ASSERT_TRUE(g.spreadActiveForTest());

    // Start a new game (simulates tap-restart after GAME_OVER/WIN).
    g.triggerNewGameForTest();

    EXPECT_FALSE(g.spreadActiveForTest());
    EXPECT_FALSE(g.shieldActiveForTest());
    EXPECT_FALSE(g.speedBoostActiveForTest());

    // Confirm in practice: firing now produces 1 bullet (not 3).
    g.onPointerDown(0, kFireX, kFireY);
    g.update(0.016f);
    g.onPointerUp(0);
    EXPECT_EQ(g.bulletCount(), 1);
}

TEST(PowerUp, CollectingAwardsPoints) {
    // Flying into a power-up grants 25 * level_ points (level 1 here).
    Game g;
    startPlaying(g);
    ASSERT_EQ(g.score(), 0L);
    g.spawnTestPowerUp(g.shipX(), g.shipY());  // spawn right on top of the ship
    g.update(0.016f);                          // collected this frame
    EXPECT_EQ(g.score(), 25L);
}

// ── Settings & gear icon ──────────────────────────────────────────────────────

// Gear icon pixel position for the test viewport (1080×2400, asp=0.45).
// world gear = (0.45 - 0.062, -0.82)  →  pixel (1006, 216).
static constexpr float kGearPX = 1006.f, kGearPY = 216.f;

// Settings row y-pixels (x = screen centre, which is within the back-button x-bound).
static constexpr float kSettingsCX  = 540.f;
static constexpr float kSoundPY     = 1020.f;   // world_y -0.15
static constexpr float kAutoRunPY   = 1320.f;   // world_y  0.10
static constexpr float kBackPY      = 1824.f;   // world_y  0.52

TEST(Settings, GearTapFromTitleOpensSettings) {
    Game g;
    g.setViewport(kW, kH);
    g.onPointerDown(0, kGearPX, kGearPY);
    g.update(0.016f);
    g.onPointerUp(0);
    EXPECT_TRUE(g.inSettingsForTest());
}

TEST(Settings, GearTapFromPlayingOpensSettings) {
    Game g;
    startPlaying(g);
    g.onPointerDown(0, kGearPX, kGearPY);
    g.update(0.016f);
    g.onPointerUp(0);
    EXPECT_TRUE(g.inSettingsForTest());
}

TEST(Settings, BackButtonReturnsToTitle) {
    Game g;
    g.setViewport(kW, kH);
    g.onPointerDown(0, kGearPX, kGearPY);
    g.update(0.016f);
    g.onPointerUp(0);
    ASSERT_TRUE(g.inSettingsForTest());
    g.onPointerDown(0, kSettingsCX, kBackPY);
    g.update(0.016f);
    g.onPointerUp(0);
    EXPECT_TRUE(g.inTitleForTest());
}

TEST(Settings, SoundToggleFlipsSoundEnabled) {
    Game g;
    g.setViewport(kW, kH);
    ASSERT_TRUE(g.soundEnabledForTest());
    g.onPointerDown(0, kGearPX, kGearPY);
    g.update(0.016f);
    g.onPointerUp(0);
    ASSERT_TRUE(g.inSettingsForTest());
    g.onPointerDown(0, kSettingsCX, kSoundPY);
    g.update(0.016f);
    g.onPointerUp(0);
    EXPECT_FALSE(g.soundEnabledForTest());
}

TEST(Settings, AutoRunToggleFlipsAutoRunActive) {
    Game g;
    g.setViewport(kW, kH);
    ASSERT_FALSE(g.autoRunActiveForTest());
    g.onPointerDown(0, kGearPX, kGearPY);
    g.update(0.016f);
    g.onPointerUp(0);
    ASSERT_TRUE(g.inSettingsForTest());
    g.onPointerDown(0, kSettingsCX, kAutoRunPY);
    g.update(0.016f);
    g.onPointerUp(0);
    EXPECT_TRUE(g.autoRunActiveForTest());
}

TEST(Settings, AutoRunPersistedAcrossSaveLoad) {
    // On device the writable scratch space is /data/local/tmp; the host
    // test runner points TMPDIR at its build tree.
    const char* base = getenv("TMPDIR");
    std::string tmpl = std::string(base ? base : "/data/local/tmp") + "/vktest_XXXXXX";
    std::vector<char> tmpDir(tmpl.begin(), tmpl.end());
    tmpDir.push_back('\0');
    char* dir = mkdtemp(tmpDir.data());
    if (!dir) { GTEST_SKIP() << "cannot create temp dir"; }

    // Enable AUTO RUN and save via the settings toggle flow.
    {
        Game g;
        g.setViewport(kW, kH);
        g.setDataPath(dir);
        g.onPointerDown(0, kGearPX, kGearPY);
        g.update(0.016f);
        g.onPointerUp(0);
        g.onPointerDown(0, kSettingsCX, kAutoRunPY);
        g.update(0.016f);
        g.onPointerUp(0);
        ASSERT_TRUE(g.autoRunActiveForTest());
    }

    // A fresh Game loaded from the same path must restore autoRunActive_.
    {
        Game g2;
        g2.setViewport(kW, kH);
        g2.setDataPath(dir);
        EXPECT_TRUE(g2.autoRunActiveForTest());
    }

    char settingsPath[600];
    snprintf(settingsPath, sizeof(settingsPath), "%s/settings.bin", dir);
    remove(settingsPath);
    rmdir(dir);
}

TEST(Settings, EdgeTapDoesNotToggleSound) {
    // Rows must only respond across the label-to-toggle span, not at the
    // extreme screen edge (x-bound regression test).
    Game g;
    g.setViewport(kW, kH);
    g.onPointerDown(0, kGearPX, kGearPY);
    g.update(0.016f);
    g.onPointerUp(0);
    ASSERT_TRUE(g.inSettingsForTest());
    ASSERT_TRUE(g.soundEnabledForTest());
    g.onPointerDown(0, 1070.f, kSoundPY);  // far right edge, sound-row height
    g.update(0.016f);
    g.onPointerUp(0);
    EXPECT_TRUE(g.soundEnabledForTest());  // outside x-bounds — no toggle
}

TEST(Settings, FirstFingerWinsForTapPosition) {
    // Multi-touch race: second finger landing on the gear must not override the
    // first finger's tap coordinates (first-wins fix).
    Game g;
    g.setViewport(kW, kH);
    g.onPointerDown(0, kW * 0.5f, kH * 0.5f);  // centre — NOT a gear tap
    g.onPointerDown(1, kGearPX, kGearPY);        // gear — must be ignored for tap coords
    g.update(0.016f);
    g.onPointerUp(0);
    g.onPointerUp(1);
    // First tap was at centre → startGame() should have fired, not settings.
    EXPECT_FALSE(g.inSettingsForTest());
    EXPECT_FALSE(g.inTitleForTest());
}

// ── Auto-run AI ───────────────────────────────────────────────────────────────

TEST(AutoRun, FiresWhenAlignedWithAsteroid) {
    Game g;
    startPlaying(g);
    g.spawnTestAsteroid(0.0f, 0.55f, 0.06f, 2);  // directly above ship (ship at x≈0)
    g.setAutoRunForTest(true);
    g.update(0.016f);
    EXPECT_GT(g.bulletCount(), 0);
}

TEST(AutoRun, MovesShipTowardTarget) {
    // Ship starts centred; asteroid spawned to the right — AI should steer right.
    Game g;
    startPlaying(g);
    g.update(1.0f);  // let ship settle
    float xBefore = g.shipX();
    g.spawnTestAsteroid(0.4f, 0.30f, 0.06f, 2);  // above and to the right
    g.setAutoRunForTest(true);
    g.update(0.3f);
    EXPECT_GT(g.shipX(), xBefore);
}

TEST(AutoRun, SteersTowardPowerUp) {
    // No asteroids, one power-up falling to the right of the ship — the AI
    // should steer toward it to collect (instead of idling at centre).
    Game g;
    startPlaying(g);
    g.update(1.0f);  // settle (dt clamps to 0.05 s per call)
    float xBefore = g.shipX();
    g.spawnTestPowerUp(0.30f, -0.40f);  // above and to the right, falling
    g.setAutoRunForTest(true);
    g.update(0.3f);
    EXPECT_GT(g.shipX(), xBefore);
}

TEST(AutoRun, WantsToCollectClearPowerUp) {
    // Clear path to an off-axis power-up → AI commits to collecting it.
    Game g;
    startPlaying(g);
    g.setAutoRunForTest(true);
    g.spawnTestPowerUp(g.shipX() + 0.30f, g.shipY() - 0.40f);  // up & to the right
    g.update(0.016f);
    EXPECT_TRUE(g.aiWantsCollectForTest());
}

TEST(AutoRun, SkipsPowerUpBlockedByAsteroid) {
    // An asteroid sitting in the steering corridor (but outside the 0.22 evade
    // radius) must make the AI decline to chase the power-up through it.
    Game g;
    startPlaying(g);
    g.setAutoRunForTest(true);
    float sx = g.shipX(), sy = g.shipY();
    g.spawnTestPowerUp(sx + 0.30f, sy - 0.40f);            // up & to the right
    g.spawnTestAsteroid(sx + 0.15f, sy - 0.30f, 0.05f, 2); // blocks the corridor
    g.update(0.016f);
    EXPECT_FALSE(g.aiWantsCollectForTest());
}

TEST(AutoRun, DoesNotFireAtARockABulletIsAlreadyOnItsWayTo) {
    // One bullet is enough for a plain rock: while it is in flight the
    // autopilot holds fire instead of pumping more bullets into the same rock.
    Game g;
    startPlaying(g);
    g.spawnTestAsteroid(0.0f, 0.30f, 0.06f, 2);   // straight above the gun
    g.setAutoRunForTest(true);
    g.update(0.016f);
    ASSERT_EQ(g.bulletCount(), 1);
    ASSERT_TRUE(g.aiFireForTest());
    g.update(0.016f);
    EXPECT_TRUE(g.aiHasTargetForTest() == false || g.aiFireForTest() == false);
    EXPECT_FALSE(g.aiFireForTest());
}

TEST(AutoRun, KeepsFiringAtAnArmoredRockUntilTwoBulletsAreOnTheWay) {
    // An armored rock takes two hits, so one bullet in flight does not
    // satisfy the autopilot: it still wants to fire.
    Game g;
    startPlaying(g);
    g.spawnTestAsteroid(0.0f, 0.30f, 0.07f, 2, Game::AT_ARMORED, 2);
    g.setAutoRunForTest(true);
    g.update(0.016f);
    ASSERT_EQ(g.bulletCount(), 1);
    g.update(0.016f);
    EXPECT_TRUE(g.aiFireForTest());
}

TEST(AutoRun, LeadsARockThatIsDriftingSideways) {
    // A rock drifting right is aimed at where it will be when the bullet
    // arrives, not where it is now.
    Game g;
    startPlaying(g);
    g.spawnTestAsteroid(-0.15f, 0.20f, 0.05f, 2, Game::AT_NORMAL, 1, 0.40f, 0.0f);
    g.setAutoRunForTest(true);
    g.update(0.016f);
    ASSERT_TRUE(g.aiHasTargetForTest());
    EXPECT_GT(g.aiAimXForTest(), -0.15f + 0.03f);
    EXPECT_LT(g.aiAimXForTest(), 0.0f);
}

TEST(AutoRun, DodgesARockFallingOntoIt) {
    // A tough rock drops straight onto the ship faster than the gun can
    // kill it: the autopilot steps aside and keeps its lives.
    Game g;
    startPlaying(g);
    g.update(1.0f);                                   // settle
    g.clearInvulnForTest();
    g.setAutoRunForTest(true);
    int lives = g.lives();
    float x0 = g.shipX();
    g.spawnTestAsteroid(x0, g.shipY() - 0.35f, 0.10f, 2, Game::AT_ARMORED, 9, 0.0f, 1.5f);
    g.update(0.016f);
    EXPECT_TRUE(g.aiLeftForTest() || g.aiRightForTest());
    float moved = 0.0f;
    for (int i = 0; i < 60; i++) {
        g.update(1.0f / 60.0f);
        moved = fmaxf(moved, fabsf(g.shipX() - x0));
    }
    EXPECT_GT(moved, 0.10f);
    EXPECT_EQ(g.lives(), lives);
}

TEST(AutoRun, DodgesAwayFromTheSecondRock) {
    // Same drop, but a second rock hangs at ship height just to the right:
    // the way out is to the left.
    Game g;
    startPlaying(g);
    g.update(1.0f);
    g.clearInvulnForTest();
    g.setAutoRunForTest(true);
    float x0 = g.shipX(), y0 = g.shipY();
    g.spawnTestAsteroid(x0, y0 - 0.35f, 0.10f, 2, Game::AT_ARMORED, 9, 0.0f, 1.5f);
    g.spawnTestAsteroid(x0 + 0.22f, y0, 0.08f, 2, Game::AT_ARMORED, 9);
    g.update(0.016f);
    EXPECT_TRUE(g.aiLeftForTest());
    EXPECT_FALSE(g.aiRightForTest());
}

TEST(AutoRun, LeavesAShieldPickupWhileShielded) {
    // A second shield adds nothing while one is up, so the autopilot does
    // not go out of its way for it.
    Game g;
    startPlaying(g);
    g.setAutoRunForTest(true);
    g.activatePowerUpForTest(0);
    g.spawnTestPowerUp(g.shipX() + 0.30f, g.shipY() - 0.40f, 0);   // a shield, up & right
    g.update(0.016f);
    EXPECT_FALSE(g.aiWantsCollectForTest());
    g.spawnTestPowerUp(g.shipX() + 0.30f, g.shipY() - 0.40f, 1);   // spread shot: worth it
    g.update(0.016f);
    EXPECT_TRUE(g.aiWantsCollectForTest());
}

// ── Render ─────────────────────────────────────────────────────────────────────

// The end-screen score must shrink to fit: at the fixed 0.30 glyph height,
// four digits already touch the sides of a portrait phone and six run far
// past the edges. Every quad (the digit segments are quads) must stay inside
// NDC once the dust has settled — asteroids and ship layers are other shapes,
// and background stars never reach past ~1.04.
TEST(Render, EndScreenScoreFitsTheScreen) {
    Game g;
    startPlaying(g);
    g.setScoreForTest(123456);                     // six digits
    for (int hit = 0; hit < 5; hit++) {            // burn all five lives
        g.clearInvulnForTest();
        g.spawnTestAsteroid(g.shipX(), g.shipY(), 0.06f, 1);
        g.update(0.016f);
    }
    ASSERT_TRUE(g.isGameOverForTest());
    for (int i = 0; i < 60; i++) g.update(1.0f / 60.0f);   // shake/debris settle
    std::vector<DrawCmd> cmds;
    g.render(cmds);
    ASSERT_GT(cmds.size(), 0u);
    for (const auto& c : cmds) {
        if (c.shape != SHAPE_QUAD) continue;
        float ext = fabsf(c.mtx[0]) + fabsf(c.mtx[1]);     // NDC half-width
        EXPECT_LE(fabsf(c.tx) + ext, 1.05f)
            << "quad past the screen edge at tx=" << c.tx;
    }
}

// ── Ship hits ─────────────────────────────────────────────────────────────────

TEST(ShipHit, ShieldAbsorbsTheHitWithoutCostingALife) {
    Game g;
    startPlaying(g);
    const int lives = g.livesForTest();
    g.activatePowerUpForTest(0);   // 0 = shield
    ASSERT_TRUE(g.shieldActiveForTest());
    g.applyShipHitForTest();
    EXPECT_EQ(g.livesForTest(), lives);
    EXPECT_FALSE(g.shieldActiveForTest());      // one shield, one save
    EXPECT_EQ(g.shieldSavesForTest(), 1);
    EXPECT_EQ(g.hitsTakenForTest(), 0);
    EXPECT_TRUE(g.isPlayingForTest());
}

TEST(ShipHit, AHitCostsALifeAndGrantsInvulnerability) {
    Game g;
    startPlaying(g);
    const int lives = g.livesForTest();
    g.clearInvulnForTest();
    g.applyShipHitForTest();
    EXPECT_EQ(g.livesForTest(), lives - 1);
    EXPECT_EQ(g.hitsTakenForTest(), 1);
    EXPECT_GT(g.invulnForTest(), 0.0f);
    EXPECT_TRUE(g.isPlayingForTest());
}

TEST(ShipHit, TheLastLifeEndsTheGame) {
    Game g;
    startPlaying(g);
    g.setLivesForTest(1);
    g.applyShipHitForTest();
    EXPECT_EQ(g.livesForTest(), 0);
    EXPECT_TRUE(g.isGameOverForTest());
}

// ── Level clear ───────────────────────────────────────────────────────────────

TEST(LevelClear, DodgingTheGoalClearsTheLevelAndPaysTheBonus) {
    Game g;
    g.setViewport(kW, kH);
    g.startLevelForTest(3);
    ASSERT_TRUE(g.isPlayingForTest());
    const long score = g.scoreForTest();
    g.setDodgedForTest(g.goalForTest());
    g.update(1.0f / 60.0f);
    EXPECT_TRUE(g.isLevelClearForTest());
    EXPECT_EQ(g.scoreForTest(), score + 100 * 3);
    EXPECT_EQ(g.asteroidStorageForTest(), 0);  // the field is cleared for the next level
}

TEST(LevelClear, OneShortOfTheGoalKeepsPlaying) {
    Game g;
    g.setViewport(kW, kH);
    g.startLevelForTest(3);
    g.setDodgedForTest(g.goalForTest() - 1);
    g.update(1.0f / 60.0f);
    EXPECT_TRUE(g.isPlayingForTest());
}

TEST(LevelClear, LevelTenIsOnlyClearedByTheBoss) {
    Game g;
    g.setViewport(kW, kH);
    g.startLevelForTest(10);
    g.setDodgedForTest(g.goalForTest());
    g.update(1.0f / 60.0f);
    EXPECT_FALSE(g.isLevelClearForTest());
    EXPECT_TRUE(g.isPlayingForTest());
}


// ── App pause ─────────────────────────────────────────────────────────────────

TEST(AppPause, AHumanRoundParksOnSettings) {
    Game g;
    startPlaying(g);
    g.onAppPause();
    EXPECT_TRUE(g.inSettingsForTest());
    // BACK returns to the round it came from.
    g.onPointerDown(0, kSettingsCX, kBackPY);
    g.update(0.016f);
    g.onPointerUp(0);
    EXPECT_TRUE(g.isPlayingForTest());
}

TEST(AppPause, AnAutoRunRoundKeepsPlaying) {
    Game g;
    startPlaying(g);
    g.setAutoRunForTest(true);
    g.onAppPause();
    EXPECT_TRUE(g.isPlayingForTest());
}

TEST(AppPause, TheTitleScreenIsLeftAlone) {
    Game g;
    g.setViewport(kW, kH);
    g.onAppPause();
    EXPECT_TRUE(g.inTitleForTest());
}

TEST(AppPause, AHeldPointerIsReleased) {
    Game g;
    startPlaying(g);
    g.onPointerDown(0, kThrustX, kThrustY);        // a finger on the thrust zone
    g.update(0.016f);
    ASSERT_TRUE(g.thrustHeldForTest());
    g.onAppPause();
    // Back in the round, the ship must not keep thrusting from a finger that is gone.
    g.onPointerDown(0, kSettingsCX, kBackPY);
    g.update(0.016f);
    g.onPointerUp(0);
    ASSERT_TRUE(g.isPlayingForTest());
    EXPECT_FALSE(g.thrustHeldForTest());
}

// ── Timers: power-up duration, combo window, invulnerability ──────────────────

// Advance a round frame by frame (update() clamps a single big dt).
static void advance(Game& g, float seconds) {
    for (float t = 0.0f; t < seconds; t += 0.016f) g.update(0.016f);
}

TEST(Timers, APowerUpWearsOffAfterEightSeconds) {
    Game g;
    startPlaying(g);
    g.spawnTestPowerUp(g.shipX(), g.shipY(), 2);   // speed boost, collected this frame
    g.update(0.016f);
    ASSERT_TRUE(g.speedBoostActiveForTest());
    advance(g, 7.5f);
    EXPECT_TRUE(g.speedBoostActiveForTest()) << "still inside the 8 s window";
    advance(g, 1.0f);
    EXPECT_FALSE(g.speedBoostActiveForTest()) << "past the 8 s window";
}

TEST(Timers, CollectingTheSamePowerUpAgainRestartsItsClock) {
    Game g;
    startPlaying(g);
    g.spawnTestPowerUp(g.shipX(), g.shipY(), 1);   // spread
    g.update(0.016f);
    ASSERT_TRUE(g.spreadActiveForTest());
    advance(g, 6.0f);
    ASSERT_TRUE(g.spreadActiveForTest());
    g.spawnTestPowerUp(g.shipX(), g.shipY(), 1);   // a second spread, 2 s before the first ends
    g.update(0.016f);
    advance(g, 6.0f);                               // 12 s after the first pickup
    EXPECT_TRUE(g.spreadActiveForTest()) << "the second pickup bought a fresh 8 s";
    advance(g, 3.0f);
    EXPECT_FALSE(g.spreadActiveForTest());
}

TEST(Timers, TheComboWindowClosesWithoutAKill) {
    Game g;
    startPlaying(g);
    g.spawnTestAsteroid(0.0f, 0.55f, 0.06f, 2);
    g.onPointerDown(0, kFireX, kFireY);
    for (int i = 0; i < 5; i++) g.update(0.016f);
    g.onPointerUp(0);
    ASSERT_EQ(g.combo(), 1);
    advance(g, 2.0f);                               // longer than the 1.8 s window
    EXPECT_EQ(g.combo(), 0);
    // The next kill starts a fresh chain instead of continuing the old one.
    g.spawnTestAsteroid(0.0f, 0.55f, 0.06f, 2);
    g.onPointerDown(0, kFireX, kFireY);
    for (int i = 0; i < 5; i++) g.update(0.016f);
    g.onPointerUp(0);
    EXPECT_EQ(g.combo(), 1);
}

TEST(Timers, AHitBreaksTheCombo) {
    Game g;
    startPlaying(g);
    g.spawnTestAsteroid(0.0f, 0.55f, 0.06f, 2);
    g.onPointerDown(0, kFireX, kFireY);
    for (int i = 0; i < 5; i++) g.update(0.016f);
    g.onPointerUp(0);
    ASSERT_EQ(g.combo(), 1);
    g.applyShipHitForTest();
    EXPECT_EQ(g.combo(), 0);
}

TEST(Timers, InvulnerabilityWearsOff) {
    Game g;
    startPlaying(g);
    g.clearInvulnForTest();
    g.applyShipHitForTest();
    ASSERT_GT(g.invulnForTest(), 0.0f);
    advance(g, 1.0f);
    EXPECT_GT(g.invulnForTest(), 0.0f) << "1.5 s of grace, 1 s in";
    advance(g, 0.7f);
    EXPECT_LE(g.invulnForTest(), 0.0f) << "grace over";
}

// ── Haptic callback ───────────────────────────────────────────────────────────

TEST(Haptic, LosingALifeBuzzes) {
    Game g;
    startPlaying(g);
    int buzzes = 0;
    g.setHapticCallback([&] { buzzes++; });
    g.clearInvulnForTest();
    g.applyShipHitForTest();
    EXPECT_EQ(buzzes, 1);
}

TEST(Haptic, AShieldedHitStaysSilent) {
    Game g;
    startPlaying(g);
    int buzzes = 0;
    g.setHapticCallback([&] { buzzes++; });
    g.activatePowerUpForTest(0);   // shield
    g.applyShipHitForTest();
    EXPECT_EQ(buzzes, 0);
    EXPECT_FALSE(g.shieldActiveForTest());
}

TEST(Haptic, NoCallbackIsFine) {
    Game g;
    startPlaying(g);
    g.clearInvulnForTest();
    g.applyShipHitForTest();       // must not dereference an empty std::function
    EXPECT_EQ(g.hitsTakenForTest(), 1);
}

// ── Pointer cancel and idle screens ───────────────────────────────────────────

TEST(Input, CancellingThePointersStopsTheShip) {
    Game g;
    startPlaying(g);
    g.onPointerDown(0, kRightX, kMidY);
    advance(g, 0.2f);
    const float movedTo = g.shipX();
    ASSERT_GT(movedTo, 0.0f);
    g.onPointersCancel();
    advance(g, 0.2f);
    EXPECT_FLOAT_EQ(g.shipX(), movedTo);
}

TEST(Input, CancelDoesNotReviveAFingerThatLiftsLater) {
    Game g;
    startPlaying(g);
    g.onPointerDown(0, kRightX, kMidY);
    g.onPointersCancel();
    g.onPointerUp(0);               // the stale up event for the cancelled finger
    const float x0 = g.shipX();
    advance(g, 0.2f);
    EXPECT_FLOAT_EQ(g.shipX(), x0);
}

TEST(IdleScreen, OnlyTheStaticScreensAreIdle) {
    Game g;
    g.setViewport(kW, kH);
    EXPECT_TRUE(g.isIdleScreen()) << "title";
    startPlaying(g);
    EXPECT_FALSE(g.isIdleScreen()) << "playing";
    g.onPointerDown(0, kGearPX, kGearPY);
    g.update(0.016f);
    g.onPointerUp(0);
    ASSERT_TRUE(g.inSettingsForTest());
    EXPECT_FALSE(g.isIdleScreen()) << "settings overlay animates";
    g.onPointerDown(0, kSettingsCX, kBackPY);
    g.update(0.016f);
    g.onPointerUp(0);
    ASSERT_TRUE(g.isPlayingForTest());
    g.setLivesForTest(1);
    g.applyShipHitForTest();
    ASSERT_TRUE(g.isGameOverForTest());
    EXPECT_TRUE(g.isIdleScreen()) << "game over";
}

// ── High-score table ──────────────────────────────────────────────────────────
// A run ends when the last life goes; that is where the table is updated and
// (with a data path set) written to disk.

static void endRunWith(Game& g, long score, int level = 1) {
    g.startLevelForTest(level);          // a fresh run on that level
    g.setScoreForTest(score);
    g.setLivesForTest(1);
    g.applyShipHitForTest();
    ASSERT_TRUE(g.isGameOverForTest());
}

// A private directory for one test's highscores.bin; "" when none can be made.
static std::string scratchDir() {
    const char* base = getenv("TMPDIR");
    std::string tmpl = std::string(base && base[0] ? base : "/data/local/tmp") + "/vkhs_XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    char* d = mkdtemp(buf.data());
    return d ? std::string(d) : std::string();
}

static void writeBytes(const std::string& path, const void* data, size_t n) {
    FILE* f = fopen(path.c_str(), "wb");
    ASSERT_TRUE(f != nullptr);
    ASSERT_EQ(fwrite(data, 1, n, f), n);
    fclose(f);
}

static void removeScratch(const std::string& dir) {
    remove((dir + "/highscores.bin").c_str());
    rmdir(dir.c_str());
}

TEST(HighScores, TableOrdersDisplacesAndCaps) {
    Game g;
    g.setViewport(kW, kH);               // no data path: the table lives in memory
    const long runs[6] = {300, 100, 500, 200, 400, 250};
    for (long r : runs) endRunWith(g, r);
    EXPECT_EQ(g.highScoreForTest(0), 500);
    EXPECT_EQ(g.highScoreForTest(1), 400);
    EXPECT_EQ(g.highScoreForTest(2), 300);
    EXPECT_EQ(g.highScoreForTest(3), 250);
    EXPECT_EQ(g.highScoreForTest(4), 200);     // the 100 was displaced off
    EXPECT_EQ(g.newHighScoreRankForTest(), 3); // the last run (250) landed fourth
}

TEST(HighScores, ARunKeepsItsLevel) {
    Game g;
    g.setViewport(kW, kH);
    endRunWith(g, 800, 4);
    EXPECT_EQ(g.highScoreForTest(0), 800);
    EXPECT_EQ(g.highScoreLevelForTest(0), 4);
    EXPECT_EQ(g.newHighScoreRankForTest(), 0);
}

TEST(HighScores, ATieDoesNotDisplaceTheHolder) {
    Game g;
    g.setViewport(kW, kH);
    endRunWith(g, 500, 1);
    endRunWith(g, 500, 7);                     // same score, later and on a higher level
    EXPECT_EQ(g.highScoreLevelForTest(0), 1);  // the first holder keeps the top row
    EXPECT_EQ(g.highScoreForTest(1), 500);
    EXPECT_EQ(g.highScoreLevelForTest(1), 7);
    EXPECT_EQ(g.newHighScoreRankForTest(), 1);
}

TEST(HighScores, AZeroRunIsNotRecorded) {
    Game g;
    g.setViewport(kW, kH);
    endRunWith(g, 0);
    EXPECT_EQ(g.highScoreForTest(0), 0);
    EXPECT_EQ(g.newHighScoreRankForTest(), -1);
}

TEST(HighScores, ANewGameKeepsTheTableAndClearsTheRank) {
    Game g;
    g.setViewport(kW, kH);
    endRunWith(g, 900, 2);
    ASSERT_EQ(g.newHighScoreRankForTest(), 0);
    g.triggerNewGameForTest();
    EXPECT_EQ(g.highScoreForTest(0), 900);
    EXPECT_EQ(g.highScoreLevelForTest(0), 2);
    EXPECT_EQ(g.newHighScoreRankForTest(), -1);
}

TEST(HighScores, SavedOnGameOverAndReloaded) {
    std::string dir = scratchDir();
    if (dir.empty()) GTEST_SKIP() << "cannot create temp dir";
    {
        Game g;
        g.setViewport(kW, kH);
        g.setDataPath(dir.c_str());
        endRunWith(g, 1234, 3);                // game over -> table -> disk
    }
    {
        Game g;
        g.setViewport(kW, kH);
        g.setDataPath(dir.c_str());
        EXPECT_EQ(g.highScoreForTest(0), 1234);
        EXPECT_EQ(g.highScoreLevelForTest(0), 3);
        EXPECT_EQ(g.highScoreForTest(1), 0);
        EXPECT_EQ(g.newHighScoreRankForTest(), -1);   // loading is not a new record
    }
    removeScratch(dir);
}

// The loader must ignore a file it cannot trust and keep the in-memory table.
// On disk: uint32 magic "ASTR", uint32 count, then kMaxScores x {int64 score, int32 level, pad}.
static constexpr uint32_t kFileMagic = 0x41535452u;
static constexpr size_t   kFileEntry = 16;
static constexpr size_t   kFileSize  = 8 + kFileEntry * Game::kMaxScoresForTest;

TEST(HighScores, FileWithWrongMagicIsIgnored) {
    std::string dir = scratchDir();
    if (dir.empty()) GTEST_SKIP() << "cannot create temp dir";
    unsigned char buf[kFileSize] = {};
    uint32_t magic = 0x454B4146u, count = Game::kMaxScoresForTest;   // "FAKE"
    memcpy(buf, &magic, 4); memcpy(buf + 4, &count, 4);
    for (int i = 0; i < Game::kMaxScoresForTest; i++) { int64_t s = 1000 * (i + 1); memcpy(buf + 8 + kFileEntry * i, &s, 8); }
    writeBytes(dir + "/highscores.bin", buf, sizeof(buf));

    Game g; g.setViewport(kW, kH); g.setDataPath(dir.c_str());
    for (int i = 0; i < Game::kMaxScoresForTest; i++) EXPECT_EQ(g.highScoreForTest(i), 0) << "entry " << i;
    removeScratch(dir);
}

TEST(HighScores, TruncatedFileIsIgnored) {
    std::string dir = scratchDir();
    if (dir.empty()) GTEST_SKIP() << "cannot create temp dir";
    // The right magic but only part of the header: the record read fails.
    unsigned char buf[5] = {};
    memcpy(buf, &kFileMagic, 4); buf[4] = 1;
    writeBytes(dir + "/highscores.bin", buf, sizeof(buf));

    Game g; g.setViewport(kW, kH); g.setDataPath(dir.c_str());
    for (int i = 0; i < Game::kMaxScoresForTest; i++) EXPECT_EQ(g.highScoreForTest(i), 0) << "entry " << i;
    removeScratch(dir);
}

TEST(HighScores, OversizedCountIsClampedToTheTable) {
    std::string dir = scratchDir();
    if (dir.empty()) GTEST_SKIP() << "cannot create temp dir";
    // A valid record whose count claims far more entries than the table holds.
    unsigned char buf[kFileSize] = {};
    uint32_t count = 1000;
    memcpy(buf, &kFileMagic, 4); memcpy(buf + 4, &count, 4);
    for (int i = 0; i < Game::kMaxScoresForTest; i++) {
        int64_t s = 500 - 100 * i; int32_t lvl = i + 1;
        memcpy(buf + 8 + kFileEntry * i, &s, 8); memcpy(buf + 16 + kFileEntry * i, &lvl, 4);
    }
    writeBytes(dir + "/highscores.bin", buf, sizeof(buf));

    Game g; g.setViewport(kW, kH); g.setDataPath(dir.c_str());
    for (int i = 0; i < Game::kMaxScoresForTest; i++) {
        EXPECT_EQ(g.highScoreForTest(i), 500 - 100 * i) << "entry " << i;
        EXPECT_EQ(g.highScoreLevelForTest(i), i + 1) << "entry " << i;
    }
    removeScratch(dir);
}
