# Vulkan Asteroids

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
- **Scoring:** +5×level per dodge, +10×level per kill, +100×level per cleared level.
- **Lives:** 3 lives; invulnerability blinks after each hit.
- **HUD:** score (top-left) · lives (top-center) · level (top-right).
- **Ship banking:** the ship tilts ±20° into the direction of travel.
- **High scores:** top-5 leaderboard persisted locally; gold/silver/bronze podium on the title screen; gold pulsing score + rank medal on game over when a new record is set.
- **Asteroid splitting:** large and medium asteroids split into two smaller faster children when shot.
- **Power-ups:** 🔵 Shield (absorbs one hit) · 🟢 Spread shot (3-way fire) · 🟡 Speed boost (1.65× speed) — drop randomly from destroyed asteroids and fall from above.
- **Screen shake:** camera jolt on ship collision for impact feedback.

## Tech

- **Pure native C++** — Android [`NativeActivity`](https://developer.android.com/ndk/reference/group/native-activity)
  with `native_app_glue`; no Kotlin, no Compose, no Java.
- **Hand-written Vulkan 2D renderer** — one graphics pipeline, push-constant
  transforms, flat-shaded primitives. Ship rendered as three layered shapes (dark
  blue-gray swept wings, bright cyan fuselage, white nose spike) matching the app
  icon style. Asteroids use a 32-vertex irregular polygon with aggressive radius
  variation for a jagged rocky surface. Bullets are two-layer laser bolts (glow +
  bright core). Two-speed parallax starfield (60 far + 20 near stars). Stroke
  vector font for the in-game title; 7-segment display font for HUD numbers.
  Asteroid destruction spawns a visual
  explosion: expanding flash ring + 10 debris fragments with drag and spin. Ship
  collision triggers screen shake + cyan impact flash.
- **Procedural audio via [Oboe](https://github.com/google/oboe)** — laser sweep,
  explosion burst, hit thud, level-clear arpeggio, thrust rumble; all synthesised
  at runtime, no audio files.
- **GLSL → SPIR-V** compiled at build time with the NDK's `glslc`.
- `minSdk 24` (Vulkan requires API 24+), AGP 9, NDK r29, CMake 3.22.

## Build & run

```bash
# Build
./gradlew assembleDebug

# Install and launch (physical device or emulator)
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n com.jpcottin.vulkanasteroids/android.app.NativeActivity

# Or use the Android CLI
android run --apks app/build/outputs/apk/debug/app-debug.apk
```

Requires a device with a Vulkan driver (API 24+).

## Testing

### Native unit tests (Google Test)

23 tests covering ship physics (gravity, thrust, clamping), input zone
isolation, bullet spawning/cooldown/expiry, spread shot, speed boost,
and asteroid splitting. Run on a connected device or emulator:

```bash
# ARM device (default)
./gradlew runNativeTests

# x86_64 emulator
./gradlew runNativeTests -PtestAbi=x86_64
```

### Instrumented smoke test

Launches the `NativeActivity` on a connected device, waits 4 s for
Vulkan to initialise, and asserts the activity is still `RESUMED`:

```bash
./gradlew connectedAndroidTest
```

## CI/CD

Three GitHub Actions jobs run on every push and pull request to `main`:

| Job | What it does | Artifacts |
|-----|-------------|-----------|
| **Build APK** | Compiles the debug APK | `debug-apk` |
| **Native Tests** | Runs 23 Google Test cases on an API-34 x86\_64 emulator | — |
| **Smoke Test** | Runs the Android instrumented test; captures an in-game screenshot via `UiAutomation` | `smoke-screenshot`, `smoke-test-results`, `smoke-logcat` |

## License

[Apache License 2.0](LICENSE).
