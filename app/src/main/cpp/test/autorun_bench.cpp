// Auto Run bench: plays seeded, headless games with the autopilot and reports
// how often it clears each level, how many lives it loses on the way, how long
// a clear takes and how well it shoots. Every Auto Run change is measured here
// before it ships (the numbers in the commit messages come from this program).
//
// Builds on the host, no Android SDK needed: scripts/autorun-bench.sh.
//
//   autorun_bench [--runs N] [--width W --height H] [--levels] [--campaigns]
//                 [--min-campaign-win PCT] [--trace]
//
// --levels    plays each level 1..10 on its own, N seeds each, starting with
//             a fresh game's five lives (the default when nothing is selected).
// --campaigns plays N full campaigns from level 1 to the boss.
// --min-campaign-win PCT exits 1 when fewer than PCT% of campaigns are won:
//             the CI regression guard.
// --trace     prints the scene (ship, autopilot intent, nearby rocks) at every
//             lost life.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "game.h"

namespace {

constexpr float kDt = 1.0f / 60.0f;
constexpr float kLevelTimeout = 300.0f;     // seconds before a level counts as stuck
bool gTrace = false;

struct Stats {
    int runs = 0, cleared = 0, timeouts = 0;
    int hits = 0, bossHits = 0, saves = 0;   // lives lost, lives lost to the boss, shield saves
    double seconds = 0.0;                    // summed over cleared runs
    long score = 0;
    long shots = 0, kills = 0, lost = 0;

    void add(const Stats& s) {
        runs += s.runs; cleared += s.cleared; timeouts += s.timeouts;
        hits += s.hits; bossHits += s.bossHits; saves += s.saves;
        seconds += s.seconds; score += s.score;
        shots += s.shots; kills += s.kills; lost += s.lost;
    }
};

void dumpScene(const Game& g, float t) {
    printf("  hit L%d t=%.1f ship x=%.3f y=%.3f vy=%.2f left=%d right=%d thrust=%d fire=%d shield=%d\n",
           g.levelForTest(), t, g.shipX(), g.shipY(), g.shipVyForTest(),
           g.aiLeftForTest(), g.aiRightForTest(), g.aiThrustForTest(), g.aiFireForTest(),
           g.shieldActiveForTest());
    for (int i = 0; i < g.asteroidStorageForTest(); i++) {
        float x, y, vx, vy, r;
        if (!g.asteroidForTest(i, x, y, vx, vy, r)) continue;
        float dx = x - g.shipX(), dy = y - g.shipY();
        if (sqrtf(dx * dx + dy * dy) > 0.45f) continue;
        printf("    rock x=%.3f y=%.3f vx=%.2f vy=%.2f r=%.3f (dx=%.3f dy=%.3f)\n",
               x, y, vx, vy, r, dx, dy);
    }
}

// Plays from the current PLAYING state until the level ends or the game does.
// Returns true when the level was cleared (LEVEL_CLEAR or WIN).
bool playLevel(Game& g, Stats& s, float& seconds) {
    seconds = 0.0f;
    int hits0 = g.hitsTakenForTest(), boss0 = g.bossHitsTakenForTest(), saves0 = g.shieldSavesForTest();
    int shots0 = g.shotsFiredForTest(), kills0 = g.killsForTest(), lost0 = g.bulletsLostForTest();
    int hits = hits0;
    while (g.isPlayingForTest()) {
        g.update(kDt);
        seconds += kDt;
        if (gTrace && g.hitsTakenForTest() > hits) { dumpScene(g, seconds); hits = g.hitsTakenForTest(); }
        if (seconds > kLevelTimeout) { s.timeouts++; break; }
    }
    s.hits += g.hitsTakenForTest() - hits0;
    s.bossHits += g.bossHitsTakenForTest() - boss0;
    s.saves += g.shieldSavesForTest() - saves0;
    s.shots += g.shotsFiredForTest() - shots0;
    s.kills += g.killsForTest() - kills0;
    s.lost += g.bulletsLostForTest() - lost0;
    return g.isLevelClearForTest() || g.isWinForTest();
}

void printStats(const char* label, const Stats& s) {
    double avg = s.cleared ? s.seconds / s.cleared : 0.0;
    printf("%-10s cleared %5.1f%%  lives lost/run %.2f (boss %.2f)  shield saves/run %.2f"
           "  timeouts %2d  avg %6.1f s  kills/run %5.1f  shots/kill %.2f  lost/run %5.1f"
           "  avg score %7ld\n",
           label, 100.0 * s.cleared / s.runs,
           (double)s.hits / s.runs, (double)s.bossHits / s.runs, (double)s.saves / s.runs,
           s.timeouts, avg, (double)s.kills / s.runs,
           s.kills ? (double)s.shots / s.kills : 0.0,
           (double)s.lost / s.runs, s.runs ? s.score / s.runs : 0L);
}

}  // namespace

int main(int argc, char** argv) {
    int runs = 200, w = 1080, h = 2400;
    bool levels = false, campaigns = false;
    double minCampaignWin = -1.0;
    for (int i = 1; i < argc; i++) {
        auto next = [&](int& dst) { if (i + 1 < argc) dst = atoi(argv[++i]); };
        if      (!strcmp(argv[i], "--runs"))      next(runs);
        else if (!strcmp(argv[i], "--width"))     next(w);
        else if (!strcmp(argv[i], "--height"))    next(h);
        else if (!strcmp(argv[i], "--levels"))    levels = true;
        else if (!strcmp(argv[i], "--campaigns")) campaigns = true;
        else if (!strcmp(argv[i], "--trace"))     gTrace = true;
        else if (!strcmp(argv[i], "--min-campaign-win") && i + 1 < argc)
            minCampaignWin = atof(argv[++i]);
        else { fprintf(stderr, "unknown argument %s\n", argv[i]); return 2; }
    }
    if (minCampaignWin >= 0.0) campaigns = true;
    if (!levels && !campaigns) levels = true;
    if (runs <= 0 || w <= 0 || h <= 0) { fprintf(stderr, "bad size or run count\n"); return 2; }
    printf("Auto Run bench: %dx%d, %d seeded runs\n", w, h, runs);

    if (levels) {
        Stats total;
        for (int L = 1; L <= 10; L++) {
            Stats s;
            for (int seed = 1; seed <= runs; seed++) {
                Game g;
                g.setViewport(w, h);
                g.seedRngForTest((uint32_t)seed * 2654435761u);
                g.startLevelForTest(L);
                g.setAutoRunForTest(true);
                float sec;
                s.runs++;
                if (playLevel(g, s, sec)) { s.cleared++; s.seconds += sec; }
                s.score += g.score();
            }
            char label[16];
            snprintf(label, sizeof(label), "level %2d", L);
            printStats(label, s);
            total.add(s);
        }
        printStats("all", total);
    }

    if (campaigns) {
        Stats s;
        double wonSeconds = 0.0;
        int won = 0;
        int reached[11] = {};
        for (int seed = 1; seed <= runs; seed++) {
            Game g;
            g.setViewport(w, h);
            g.seedRngForTest((uint32_t)seed * 2654435761u);
            g.triggerNewGameForTest();
            g.setAutoRunForTest(true);
            float total = 0.0f;
            s.runs++;
            for (;;) {
                float sec;
                reached[g.levelForTest()]++;
                bool ok = playLevel(g, s, sec);
                total += sec;
                if (!ok || g.isWinForTest()) break;
                while (g.isLevelClearForTest()) g.update(kDt);   // the interlude
            }
            if (g.isWinForTest()) { won++; wonSeconds += total; }
            s.score += g.score();
        }
        printf("campaigns  won %5.1f%%  lives lost/run %.2f (boss %.2f)  shield saves/run %.2f"
               "  timeouts %2d  avg won %6.1f s  shots/kill %.2f  avg score %7ld\n",
               100.0 * won / s.runs,
               (double)s.hits / s.runs, (double)s.bossHits / s.runs, (double)s.saves / s.runs,
               s.timeouts, won ? wonSeconds / won : 0.0,
               s.kills ? (double)s.shots / s.kills : 0.0, s.score / s.runs);
        printf("reached    ");
        for (int L = 1; L <= 10; L++) printf("L%d %3d  ", L, reached[L]);
        printf("\n");
        if (minCampaignWin >= 0.0 && 100.0 * won / s.runs < minCampaignWin) {
            printf("FAIL: campaign win rate below %.1f%%\n", minCampaignWin);
            return 1;
        }
    }
    return 0;
}
