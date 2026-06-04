#include <android_native_app_glue.h>
#include <android/input.h>
#include <time.h>
#include <vector>

#include "common.h"
#include "vk_renderer.h"
#include "game.h"

struct Engine {
    android_app* app = nullptr;
    VkRenderer renderer;
    Game game;
    bool instanceReady = false;
    double lastTime = 0.0;
};

static double now_s() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

static void handle_cmd(android_app* app, int32_t cmd) {
    auto* e = (Engine*)app->userData;
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (app->window && e->instanceReady) {
                e->renderer.initWindow(app->window);
                e->lastTime = now_s();
            }
            break;
        case APP_CMD_TERM_WINDOW:
            e->renderer.termWindow();
            break;
        default:
            break;
    }
}

static int32_t handle_input(android_app* app, AInputEvent* ev) {
    auto* e = (Engine*)app->userData;
    if (AInputEvent_getType(ev) != AINPUT_EVENT_TYPE_MOTION) return 0;

    int32_t action = AMotionEvent_getAction(ev);
    int32_t flag = action & AMOTION_EVENT_ACTION_MASK;
    int32_t idx = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                  >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;

    switch (flag) {
        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_POINTER_DOWN: {
            int id = AMotionEvent_getPointerId(ev, idx);
            e->game.onPointerDown(id, AMotionEvent_getX(ev, idx), AMotionEvent_getY(ev, idx));
            break;
        }
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_POINTER_UP: {
            int id = AMotionEvent_getPointerId(ev, idx);
            e->game.onPointerUp(id);
            break;
        }
        case AMOTION_EVENT_ACTION_MOVE: {
            size_t n = AMotionEvent_getPointerCount(ev);
            for (size_t i = 0; i < n; i++) {
                int id = AMotionEvent_getPointerId(ev, i);
                e->game.onPointerMove(id, AMotionEvent_getX(ev, i), AMotionEvent_getY(ev, i));
            }
            break;
        }
        case AMOTION_EVENT_ACTION_CANCEL:
            e->game.onPointersCancel();
            break;
        default:
            break;
    }
    return 1;
}

void android_main(android_app* app) {
    Engine engine;
    engine.app = app;
    app->userData = &engine;
    app->onAppCmd = handle_cmd;
    app->onInputEvent = handle_input;

    engine.instanceReady = engine.renderer.initInstance();
    engine.lastTime = now_s();

    while (true) {
        int events;
        android_poll_source* source;
        int timeout = engine.renderer.ready() ? 0 : -1;
        while (ALooper_pollOnce(timeout, nullptr, &events, (void**)&source) >= 0) {
            if (source) source->process(app, source);
            if (app->destroyRequested) {
                engine.renderer.cleanup();
                return;
            }
            timeout = 0;  // drain remaining events without blocking
        }

        if (engine.renderer.ready()) {
            double now = now_s();
            float dt = (float)(now - engine.lastTime);
            engine.lastTime = now;

            engine.game.setViewport(engine.renderer.width(), engine.renderer.height());
            engine.game.update(dt);

            std::vector<DrawCmd> cmds;
            engine.game.render(cmds);
            float clear[3];
            engine.game.clearColor(clear);
            engine.renderer.drawFrame(cmds, clear);
        }
    }
}
