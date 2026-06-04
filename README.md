# Vulkan Asteroids

A small 2D **Asteroids-like** game for **Android**, rendered with **Vulkan**.

Fly a spaceship left and right to dodge asteroids falling toward you. Survive
each wave to advance through five increasingly difficult levels while racking up
your score.

## Gameplay

- **Steer:** hold the **left half** of the screen to move left, the **right half** to move right.
- **Goal:** dodge the asteroids — survive enough to clear the level.
- **Progression:** 5 levels, each faster and denser than the last.
- **Score & lives:** earn points for every asteroid you dodge; you start with 3 lives.

The HUD shows your score (top-left), remaining lives (top-center), and the
current level (top-right).

## Tech

- **Pure native** (C++): an Android [`NativeActivity`](https://developer.android.com/ndk/reference/group/native-activity)
  with `native_app_glue` — no Kotlin/Java/Compose.
- A small **hand-written Vulkan 2D renderer**: one graphics pipeline, push-constant
  transforms, flat-shaded primitives.
- GLSL shaders are compiled to SPIR-V at build time with the NDK's `glslc`.
- `minSdk 24` (Vulkan requires API 24+), AGP 9, NDK r29, CMake.

## Build & run

```bash
./gradlew assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n com.jpcottin.vulkanasteroids/android.app.NativeActivity
```

Runs on any physical device with a Vulkan driver (API 24+).

> **Emulator note:** guest Vulkan does not work on every AVD. An API 30 image
> launched with the Vulkan feature enabled works well:
> ```bash
> emulator -avd <api30_avd> -gpu host -feature Vulkan,GLDirectMem
> ```

## License

[Apache License 2.0](LICENSE).
