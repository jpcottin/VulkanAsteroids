#pragma once
#include <android/log.h>
#include <cstdint>

#define LOG_TAG "Asteroids"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Drawable primitives the renderer knows how to draw.
enum Shape {
    SHAPE_SHIP = 0,
    SHAPE_ASTEROID = 1,
    SHAPE_QUAD = 2,
    SHAPE_SHIP_WINGS = 3,   // wide swept wings layer (dark blue-gray)
    SHAPE_SHIP_BODY  = 4,   // narrow fuselage layer (bright cyan)
    SHAPE_SHIP_NOSE  = 5,   // sharp nose spike layer (white)
    SHAPE_COUNT
};

// One draw: a 2x2 linear transform + NDC translation + RGBA colour, applied to a shape.
// Matches the push-constant layout consumed by shape.vert/.frag.
struct DrawCmd {
    float mtx[4];   // m00, m01, m10, m11
    float tx, ty;   // NDC translation
    float color[4]; // r, g, b, a
    int shape;
};
