#include "asset_loader.h"
#include "audio_player.h"
#include "game/app.h"
#include "viewport_manager.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>

#include <android/input.h>
#include <android/log.h>
#include <android/native_window.h>

#include <chrono>

namespace {

constexpr char kLogTag[] = "WrestleFestNative";

// The game's logical canvas. Everything in the SML data and the arena art is
// authored against these dimensions (GAMEPLAY_IMPLEMENTATION 1.2).
constexpr int kLogicalWidth = 960;
constexpr int kLogicalHeight = 640;

class Renderer {
public:
    void setAssets(AssetLoader* assets) { assets_ = assets; }
    void setAudio(AudioPlayer* audio) { audio_ = audio; }

    bool initialize(ANativeWindow* window) {
        display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (display_ == EGL_NO_DISPLAY || eglInitialize(display_, nullptr, nullptr) != EGL_TRUE) {
            logEglFailure("eglInitialize");
            release();
            return false;
        }

        const EGLint config_attributes[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, 16,
            EGL_NONE};
        EGLConfig config = nullptr;
        EGLint config_count = 0;
        if (eglChooseConfig(display_, config_attributes, &config, 1, &config_count) != EGL_TRUE ||
            config_count == 0) {
            logEglFailure("eglChooseConfig");
            release();
            return false;
        }

        const EGLint context_attributes[] = {
            EGL_CONTEXT_MAJOR_VERSION_KHR, 3,
            EGL_CONTEXT_MINOR_VERSION_KHR, 2,
            EGL_NONE};
        context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, context_attributes);
        if (context_ == EGL_NO_CONTEXT) {
            logEglFailure("eglCreateContext for OpenGL ES 3.2");
            release();
            return false;
        }

        surface_ = eglCreateWindowSurface(display_, config, window, nullptr);
        if (surface_ == EGL_NO_SURFACE ||
            eglMakeCurrent(display_, surface_, surface_, context_) != EGL_TRUE) {
            logEglFailure("eglCreateWindowSurface/eglMakeCurrent");
            release();
            return false;
        }

        const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "Created GLES context: %s",
                            version == nullptr ? "unknown" : version);

        if (!resize(window)) {
            return false;
        }

        // The game needs a current GL context before it can upload textures.
        app_ready_ = app_.initialize(assets_, audio_);
        if (!app_ready_) {
            __android_log_print(ANDROID_LOG_ERROR, kLogTag, "game init failed: %s",
                                app_.error().c_str());
        }
        last_tick_ = std::chrono::steady_clock::now();
        return true;
    }

    bool resize(ANativeWindow* window) {
        if (display_ == EGL_NO_DISPLAY || surface_ == EGL_NO_SURFACE || window == nullptr) {
            return false;
        }

        EGLint width = 0;
        EGLint height = 0;
        if (eglQuerySurface(display_, surface_, EGL_WIDTH, &width) != EGL_TRUE ||
            eglQuerySurface(display_, surface_, EGL_HEIGHT, &height) != EGL_TRUE ||
            width <= 0 || height <= 0) {
            logEglFailure("eglQuerySurface");
            return false;
        }
        viewport_.setSurfaceSize(width, height);

        legacy_viewport::IntRect content{};
        if (!viewport_.mapGlRect({0, 0, kLogicalWidth, kLogicalHeight}, &content)) {
            return false;
        }
        content_ = content;
        glViewport(content.x, content.y, content.width, content.height);
        return true;
    }

    void update() {
        const auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - last_tick_).count();
        last_tick_ = now;
        if (app_ready_) {
            app_.update(dt);
        }
    }

    void render(ANativeWindow* window) {
        if (!resize(window) || !app_ready_) {
            return;
        }
        app_.render(content_.x, content_.y, content_.width, content_.height);
        if (eglSwapBuffers(display_, surface_) != EGL_TRUE) {
            logEglFailure("eglSwapBuffers");
        }
    }

    // Surface pixels in, logical 960x640 out.
    void touchDown(int pointer_id, float surface_x, float surface_y) {
        float x = 0.0f;
        float y = 0.0f;
        if (app_ready_ && mapTouch(surface_x, surface_y, &x, &y)) {
            app_.touchDown(pointer_id, x, y);
        }
    }

    void touchMove(int pointer_id, float surface_x, float surface_y) {
        float x = 0.0f;
        float y = 0.0f;
        if (app_ready_ && mapTouch(surface_x, surface_y, &x, &y)) {
            app_.touchMove(pointer_id, x, y);
        }
    }

    void touchUp(int pointer_id) {
        if (app_ready_) {
            app_.touchUp(pointer_id);
        }
    }

    void release() noexcept {
        if (app_ready_) {
            app_.shutdown();
            app_ready_ = false;
        }
        if (display_ != EGL_NO_DISPLAY) {
            eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (surface_ != EGL_NO_SURFACE) {
                eglDestroySurface(display_, surface_);
                surface_ = EGL_NO_SURFACE;
            }
            if (context_ != EGL_NO_CONTEXT) {
                eglDestroyContext(display_, context_);
                context_ = EGL_NO_CONTEXT;
            }
            eglTerminate(display_);
            display_ = EGL_NO_DISPLAY;
        }
        viewport_.setSurfaceSize(0, 0);
    }

private:
    bool mapTouch(float surface_x, float surface_y, float* logical_x, float* logical_y) {
        legacy_viewport::Point logical{};
        if (!viewport_.mapTouchToLogical({surface_x, surface_y}, &logical)) {
            return false;
        }
        *logical_x = static_cast<float>(logical.x);
        *logical_y = static_cast<float>(logical.y);
        return true;
    }

    static void logEglFailure(const char* operation) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "%s failed (EGL error 0x%x)",
                            operation, eglGetError());
    }

    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLSurface surface_ = EGL_NO_SURFACE;
    EGLContext context_ = EGL_NO_CONTEXT;
    legacy_viewport::ViewportManager viewport_{kLogicalWidth, kLogicalHeight};
    legacy_viewport::IntRect content_{};
    AssetLoader* assets_ = nullptr;
    AudioPlayer* audio_ = nullptr;
    wrestlefest::GameApp app_;
    bool app_ready_ = false;
    std::chrono::steady_clock::time_point last_tick_{};
};

void processInput(android_app* app, Renderer* renderer, AudioPlayer* audio) {
    android_input_buffer* input = android_app_swap_input_buffers(app);
    if (input == nullptr) {
        return;
    }

    for (uint64_t i = 0; i < input->motionEventsCount; ++i) {
        const GameActivityMotionEvent& event = input->motionEvents[i];
        const int action = event.action & AMOTION_EVENT_ACTION_MASK;
        for (uint32_t p = 0; p < event.pointerCount; ++p) {
            const GameActivityPointerAxes& pointer = event.pointers[p];
            const int id = pointer.id;
            const float x = GameActivityPointerAxes_getAxisValue(
                const_cast<GameActivityPointerAxes*>(&pointer), AMOTION_EVENT_AXIS_X);
            const float y = GameActivityPointerAxes_getAxisValue(
                const_cast<GameActivityPointerAxes*>(&pointer), AMOTION_EVENT_AXIS_Y);
            switch (action) {
                case AMOTION_EVENT_ACTION_DOWN:
                case AMOTION_EVENT_ACTION_POINTER_DOWN:
                    if (p == event.actionIndex) {
                        renderer->touchDown(id, x, y);
                        if (audio != nullptr) audio->play();
                    }
                    break;
                case AMOTION_EVENT_ACTION_MOVE:
                    renderer->touchMove(id, x, y);
                    break;
                case AMOTION_EVENT_ACTION_UP:
                case AMOTION_EVENT_ACTION_POINTER_UP:
                    if (p == event.actionIndex) {
                        renderer->touchUp(id);
                    }
                    break;
                case AMOTION_EVENT_ACTION_CANCEL:
                    renderer->touchUp(id);
                    break;
                default:
                    break;
            }
        }
    }
    android_app_clear_motion_events(input);
    android_app_clear_key_events(input);
}

void handleAppCommand(android_app* app, int32_t command) {
    auto* renderer = static_cast<Renderer*>(app->userData);
    if (renderer == nullptr) {
        return;
    }

    switch (command) {
        case APP_CMD_INIT_WINDOW:
            if (app->window != nullptr) {
                renderer->initialize(app->window);
            }
            break;
        case APP_CMD_TERM_WINDOW:
            renderer->release();
            break;
        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONTENT_RECT_CHANGED:
            if (app->window != nullptr) {
                renderer->resize(app->window);
            }
            break;
        default:
            break;
    }
}

}  // namespace

extern "C" void android_main(android_app* app) {
    Renderer renderer;
    AssetLoader assets;
    assets.initialize(app);
    renderer.setAssets(&assets);

    AudioPlayer audio;
    audio.prepareWav(&assets, "audio/sfx/Attack01.wav");
    renderer.setAudio(&audio);
    app->userData = &renderer;
    app->onAppCmd = handleAppCommand;

    while (app->destroyRequested == 0) {
        int events = 0;
        android_poll_source* source = nullptr;
        const int timeout = app->window == nullptr ? -1 : 16;
        const int result = ALooper_pollOnce(timeout, nullptr, &events,
                                            reinterpret_cast<void**>(&source));
        if (result >= 0 && source != nullptr) {
            source->process(app, source);
        }
        processInput(app, &renderer, &audio);
        if (app->window != nullptr) {
            renderer.update();
            renderer.render(app->window);
        }
    }

    audio.shutdown();
    renderer.release();
}
