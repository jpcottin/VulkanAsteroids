# Vulkan Asteroids

[![CI](https://github.com/jpcottin/VulkanAsteroids/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/jpcottin/VulkanAsteroids/actions/workflows/ci.yml)

<details>
<summary><b>CI details</b> — Auto Run bench, native tests + smoke matrix, API 34 → 37.2, plus Android CLI and Emulator Preview legs</summary>

| Legs | Image | Emulator channel | GPU | Gating |
|---|---|---|---|---|
| Auto Run bench | host build, no emulator | — | — | ✅ blocking |
| Native tests: API 34, 36 | `default` x86_64 | stable | lavapipe / auto | ✅ blocking |
| Host tests | host build, no emulator | — | — | ✅ blocking |
| Smoke: API 34, 36 | `default` x86_64 | stable | lavapipe / auto | ✅ blocking |
| Smoke: API 37.0 | `google_apis_ps16k` (16 KB page size) | stable | lavapipe | non-blocking |
| Smoke: API 37.0 | `google_apis_ps16k` | canary (`--channel=3`) | lavapipe, auto | non-blocking |
| Smoke: API 37.1 | `google_apis_ps16k` | canary | lavapipe, auto | non-blocking |
| Smoke: API 37.2 | `google_apis_ps16k` | canary | lavapipe, auto | non-blocking |
| Android CLI experiment | `google_apis_ps16k` 37.0, 37.2 | canary | emulator default | non-blocking |
| Emulator Preview (`emulators;latest`) | `google_apis_ps16k` 37.0, 37.1, 37.2 | preview package | auto | non-blocking |
| Emulator Preview multi-run (snapshot cycles) | `google_apis_ps16k` 37.0 | preview package | auto | non-blocking |
| Android CLI multi-run (snapshot cycles) | `google_apis_ps16k` 37.0 | canary | emulator default | non-blocking |

The Android CLI leg drives the whole flow with the [`android` CLI](https://d.android.com/tools/agents/android-cli) (`android sdk install --canary`, `android emulator create/start/stop`) instead of `sdkmanager`/`avdmanager` and the emulator-runner action.

All emulator-runner legs use full diagnostics (`-verbose -show-kernel -debug-metrics -metrics-collection`) and a `cmdline-tools;latest` update so `avdmanager` writes a valid `target=android-37.x` (the runner's preinstalled version writes `android-0`, which the emulator clamps to API 3, disabling the Vulkan/GLDirectMem auto-enable the ps16k images need).

</details>

A 2D **Asteroids-like** game for **Android**, rendered with **Vulkan**.

Dodge and shoot asteroids across ten increasingly difficult levels.

## Gameplay

| Control | Action |
|---------|--------|
| Hold **left half** of screen | Move ship left |
| Hold **right half** of screen | Move ship right |
| Hold **bottom-left corner** | Thrust — fight gravity, fly toward asteroids |
| Hold **bottom-right corner** | Fire — auto-shoot bullets upward |

- **Goal:** clear each level by destroying or dodging enough asteroids.
- **Progression:** 10 levels, each faster and denser than the last.
- **Scoring:** +5×level per dodge, +10×level per kill, +25×level per power-up collected, +100×level per cleared level, +50×level for the boss.
- **Lives:** 5 lives; invulnerability blinks after each hit, and after a shield save.
- **HUD:** score (top-left) · lives (top-center) · level (top-right).
- **Ship banking:** the ship tilts ±20° into the direction of travel.
- **High scores:** top-5 leaderboard persisted locally; gold/silver/bronze podium on the title screen; gold pulsing score + rank medal on game over when a new record is set.
- **Asteroid splitting:** large and medium asteroids split into two smaller faster children when shot.
- **Asteroid variety:** fast (smaller, 1.65×, lateral drift, level 5+) and armored (2 HP, orange ring, level 7+) variants.
- **Power-ups:** 🔵 Shield (absorbs one hit) · 🟢 Spread shot (3-way fire) · 🟡 Speed boost (1.65× speed) — drop from kills and fall from above; HUD timer bar shows remaining duration.
- **Combo multiplier:** chain kills within 1.8 s for x2–x4 score bonus; multiplier shown on screen; the chain ends with the level.
- **Boss fight at level 10:** 12-HP boss with sinusoidal drift and health bar; defeating it wins the game.
- **Screen shake** on ship collision; HUD stays stable.
- **Haptic feedback** (50 ms vibration) on ship hit.
- **Background music:** procedurally synthesised ambient space track (A-minor pad + bass + arpeggio) starts with each new game.
- **Power-up collection sound:** rising sparkle arpeggio on pickup.
- **Settings:** gear icon (top-right) opens an overlay from any game state. Toggles: Sound on/off (persisted to disk), Auto Run. Both preferences survive app restarts.
- **Auto Run:** AI autopilot mode — the ship looks 0.6 s ahead every frame: it predicts each rock's path and the boss's drift, rolls itself forward under a handful of steering and thrust plans and keeps the one that stays clear (or the one that comes closest when nothing does), counts the bullets already in flight so a rock that is as good as dead is not shot twice, lead-aims drifting rocks, leaves rocks it cannot line up with to fall past, and chases a power-up only when it can reach it (a shield is skipped while one is up). Activate from Settings; gear turns green with a pulsing "AUTO" label while active.

## Tech

- **Pure native C++** — Android [`NativeActivity`](https://developer.android.com/ndk/reference/group/native-activity)
  with `native_app_glue`; no Kotlin, no Compose, no Java.
- **Hand-written Vulkan 2D renderer** — one graphics pipeline, push-constant
  transforms, flat-shaded primitives. Ship rendered as three layered shapes (dark
  blue-gray swept wings, bright cyan fuselage, white nose spike) matching the app
  icon style. Asteroids come in four 32-vertex silhouette variants (jagged,
  boulder, shard, potato) with a slight random squash, shaded per-pixel in the
  fragment shader: 3-octave fbm value noise, hash-placed crater pockets with lit
  rims, and fake-sphericity rim darkening — all seeded per rock so no two look
  alike, no textures involved. Bullets are two-layer laser bolts (glow +
  bright core). Two-speed parallax starfield (60 far + 20 near stars). Stroke
  vector font for the in-game title; 7-segment display font for HUD numbers.
  Asteroid destruction spawns a visual
  explosion: expanding flash ring + 10 debris fragments with drag and spin. Ship
  collision triggers screen shake + cyan impact flash.
- **Procedural audio via [Oboe](https://github.com/google/oboe)** — laser sweep,
  explosion burst, hit thud, level-clear arpeggio, thrust rumble; all synthesised
  at runtime, no audio files.
- **GLSL → SPIR-V** compiled at build time with the NDK's `glslc`.
- **Runtime diagnostics via Logcat** — on every launch the game logs a full
  Vulkan extension audit (`✓ USED` / `~ PRESENT` / `✗ ABSENT` / `? UNKNOWN`)
  for both instance and device extensions, with an inline explanation for each
  known extension. During gameplay a periodic FPS line logs frames/s, average
  frame time, and draw-call count every 5 seconds. Filter with `adb logcat -s Asteroids`.
- `minSdk 24` (Vulkan requires API 24+), AGP 9, NDK r29, CMake 3.22.

## Build & run

```bash
# Build
./gradlew assembleDebug

# Install and launch (physical device or emulator)
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n com.jpcottin.vulkanasteroids/.GameActivity

# Or use the Android CLI
android run --apks app/build/outputs/apk/debug/app-debug.apk
```

Requires a device with a Vulkan driver (API 24+).

## Testing

### Native unit tests (Google Test)

93 tests covering ship physics, input zones, bullets, spread shot, speed
boost, asteroid splitting, combo multiplier, armored asteroid HP, boss
spawn, power-up session isolation, power-up pickup scoring, settings state
machine (gear tap, back button, sound/auto-run toggles, row hit-bounds,
persistence, multi-touch tap race), auto-run AI (fire alignment, lateral
steering, power-up interception, declining blocked power-ups, holding fire on
a rock a bullet is already heading for, lead aiming, dodging a falling rock
onto the safe side, skipping a redundant shield), the clocks (a power-up
lasts 8 s and a second pickup restarts it, the combo window closes without
a kill and a hit breaks the chain, invulnerability wears off), the haptic
callback (a lost life buzzes, a shielded hit does not, no callback is fine),
pointer cancellation, which screens count as idle, and the high-score table
(order, displacement and cap, the level kept with each run, a tie leaving the
holder on top, a zero run never recorded, saved on game over and reloaded, and
a file with a wrong magic, a truncated one or an oversized count left ignored).
Run on a connected device or emulator:

```bash
# ARM device (default)
./gradlew runNativeTests

# x86_64 emulator
./gradlew runNativeTests -PtestAbi=x86_64
```

The same suite also builds and runs on the host, without a device or an
emulator (`game.cpp` is platform-independent; `app/src/main/cpp/test/host/`
stands in for `<android/log.h>`, Google Test comes from the NDK or
`libgtest-dev`):

```bash
scripts/host-tests.sh                              # all tests
scripts/host-tests.sh --gtest_filter='Settings.*'  # a subset
```

### Auto Run bench (host build)

`scripts/autorun-bench.sh` compiles `game.cpp` with the host compiler (no
Android SDK, no emulator — `app/src/main/cpp/test/host/` stands in for
`<android/log.h>`) and plays seeded, headless games with the autopilot. It
reports, per level and per campaign, how often the autopilot clears the level,
how many lives it loses (and how many the shield saved), how long a clear
takes, kills per run, shots per kill and bullets lost off-screen:

```bash
scripts/autorun-bench.sh --runs 500 --levels --campaigns          # portrait phone
scripts/autorun-bench.sh --width 2400 --height 1080 --campaigns   # landscape
scripts/autorun-bench.sh --runs 100 --levels --trace              # dump every lost life
```

Runs are seeded, so two builds of the same code give the same numbers and a
change to the autopilot is a before/after comparison. CI plays 300 portrait
and 100 landscape campaigns and fails when fewer than 95% are won. Numbers
for the current autopilot (campaigns won / lives lost per campaign / average
length of a won campaign):

| Screen | Before the look-ahead | Now |
|---|---|---|
| Portrait 1080×2400, 2000 campaigns | 74.2% / 3.03 / 61 s | 100% / 0.11 / 56 s |
| Foldable inner 2152×2076, 500 campaigns | 63.8% / 3.48 / 67 s | 100% / 0.01 / 61 s |
| Landscape 2400×1080, 500 campaigns | 56.8% / 3.71 / 74 s | 100% / 0.00 / 67 s |

### Instrumented smoke test

Launches the game activity on a connected device, polls the Vulkan log for
Vulkan to initialise, asserts the activity is still `RESUMED` and that a
`UiAutomation` screenshot of it is not a black frame:

```bash
./gradlew connectedAndroidTest
```

## CI/CD

GitHub Actions runs on every push and pull request to `main`. The first five
jobs gate merges. The last four explore newer Android emulator tooling and are
marked `continue-on-error`, so a preview package that moves underneath us
reports its findings without ever blocking a PR.

| Job | What it does | Artifacts |
|-----|-------------|-----------|
| **Build APK** | Compiles the debug APK | `debug-apk` |
| **Auto Run bench** | Builds `game.cpp` with the host compiler and plays 300 portrait and 100 landscape seeded campaigns with the autopilot; fails when fewer than 95% are won | — |
| **Native Tests** | Runs 93 Google Test cases on x86\_64 emulators (API 34 + API 36) | — |
| **Host tests** | Builds `game.cpp` and the same 93 Google Test cases with the host compiler and runs them, no emulator | — |
| **Smoke Test** | Launches the game on x86\_64 emulators and captures a `screencap` of it as the screenshot artifact, then runs the Android instrumented test, which asserts the activity is RESUMED and that a `UiAutomation` capture is not a black frame, so a dead Vulkan path fails loudly. Blocking on API 34 + 36; non-blocking preview legs on API 37.0, 37.1 and 37.2 (`google_apis_ps16k`, 16 KB pages) across the lavapipe / auto GPU backends | `smoke-screenshot-api*`, `smoke-test-results-api*`, `smoke-logcat-api*` (suffixed per leg) |
| **Android CLI experiment** | Drives the same instrumented test through the `android` CLI — SDK install, AVD creation, boot and teardown — instead of `sdkmanager`/`avdmanager` plus the emulator-runner action | `cli-smoke-*`; `instrumented-test-report-android-cli-experiment-*` on failure |
| **Emulator Preview** | Boots the Android Emulator Preview package (`emulators;latest`, which installs alongside the stable emulator under `emulators/latest/`) and runs the instrumented test against it | `preview-smoke-*` |
| **Emulator Preview multi-run** | Four boot cycles against the same AVD with quickboot snapshots enabled: each cycle plays the game briefly, screenshots it, then shuts down so the emulator saves its snapshot. Checks whether a live Vulkan app survives snapshot save/restore | `preview-multirun-screenshots`, `preview-multirun-emulator-logs` |
| **Android CLI multi-run** | The same four-cycle snapshot experiment driven entirely by the `android` CLI (`emulator start` / `stop`, `run`, `screen capture`, `layout`) against the canary emulator. The CLI drives the SDK's emulator package rather than the preview one, so the two multi-run jobs together show how the same experiment behaves on each. The app is never relaunched after a restore, so the screenshots and the `app survived restore:` lines reflect what the snapshot actually preserved | `cli-multirun-screenshots`, `cli-multirun-logs` |

The preview jobs share their setup through the composite action in
`.github/actions/preview-emulator`, which installs the system image, creates
the AVD, installs the preview emulator and its host dependencies.

### Replaying the Emulator Preview job locally

Pushing to see what a preview emulator does is a slow way to iterate, so the
multi-run job can be replayed on a local machine in a few minutes:

```bash
scripts/replay-preview-multirun.sh            # 4 cycles, throwaway AVD, cleaned up after
scripts/replay-preview-multirun.sh -n 2 -k    # 2 cycles, keep the AVD for inspection
scripts/replay-preview-multirun.sh -h         # options
```

It needs `emulators;latest`, the API 37.0 `google_apis_ps16k` system image and
KVM. Every `adb` call is pinned to the emulator it launches and shutdown is
scoped to that emulator's process group, so it is safe to run while other
devices or emulators are attached.

## License

[Apache License 2.0](LICENSE).
