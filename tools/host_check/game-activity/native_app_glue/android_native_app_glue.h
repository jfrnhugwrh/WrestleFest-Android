// Host stand-in for the GameActivity native glue header (syntax checking only).
#ifndef WRESTLEFEST_HOST_STUB_ANDROID_APP_GLUE
#define WRESTLEFEST_HOST_STUB_ANDROID_APP_GLUE
#include <android/input.h>
#include <android/native_window.h>
#include <jni.h>
#include <android/asset_manager.h>

#define GAMEACTIVITY_MAX_NUM_POINTERS_IN_MOTION_EVENT 16
#define GAME_ACTIVITY_POINTER_INFO_AXIS_COUNT 48

struct GameActivityPointerAxes {
    std::int32_t id;
    float axisValues[GAME_ACTIVITY_POINTER_INFO_AXIS_COUNT];
};

inline float GameActivityPointerAxes_getAxisValue(GameActivityPointerAxes *pointerInfo,
                                                  std::int32_t axis) {
    if (axis < 0 || axis >= GAME_ACTIVITY_POINTER_INFO_AXIS_COUNT) return 0.0f;
    return pointerInfo->axisValues[axis];
}

struct GameActivityMotionEvent {
    std::int32_t action;
    std::uint32_t actionIndex;
    std::uint32_t pointerCount;
    GameActivityPointerAxes pointers[GAMEACTIVITY_MAX_NUM_POINTERS_IN_MOTION_EVENT];
};

struct android_input_buffer {
    GameActivityMotionEvent *motionEvents;
    std::uint64_t motionEventsCount;
    void *keyEvents;
    std::uint64_t keyEventsCount;
};

struct android_app;
struct android_poll_source {
    std::int32_t id;
    void (*process)(struct android_app *app, struct android_poll_source *source);
};

enum {
    APP_CMD_INIT_WINDOW = 10,
    APP_CMD_TERM_WINDOW = 11,
    APP_CMD_WINDOW_RESIZED = 12,
    APP_CMD_CONTENT_RECT_CHANGED = 15,
};

struct ANativeActivity {
    JavaVM *vm;
    jobject javaGameActivity;
    AAssetManager *assetManager;
};

struct android_app {
    ANativeActivity *activity;
    void *userData;
    void (*onAppCmd)(struct android_app *app, std::int32_t cmd);
    ANativeWindow *window;
    std::int32_t destroyRequested;
};

extern "C" android_input_buffer *android_app_swap_input_buffers(android_app *app);
extern "C" void android_app_clear_motion_events(android_input_buffer *buffer);
extern "C" void android_app_clear_key_events(android_input_buffer *buffer);
extern "C" std::int32_t ALooper_pollOnce(std::int32_t timeoutMillis, std::int32_t *outFd,
                                         std::int32_t *outEvents, void **outData);
#endif
