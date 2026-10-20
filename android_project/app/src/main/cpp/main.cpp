#include "asset_loader.h"
#include "audio_player.h"
#include "viewport_manager.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>

#include <android/log.h>
#include <android/native_window.h>

namespace {

constexpr char kLogTag[] = "WrestleFestNative";

class Renderer {
public:
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
        return resize(window);
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
        if (!viewport_.mapGlRect({0, 0, 480, 320}, &content)) {
            return false;
        }
        glViewport(content.x, content.y, content.width, content.height);
        return true;
    }

    void render(ANativeWindow* window) {
        if (!resize(window)) {
            return;
        }
        glClearColor(0.035F, 0.045F, 0.075F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (eglSwapBuffers(display_, surface_) != EGL_TRUE) {
            logEglFailure("eglSwapBuffers");
        }
    }

    void release() noexcept {
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
    static void logEglFailure(const char* operation) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "%s failed (EGL error 0x%x)",
                            operation, eglGetError());
    }

    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLSurface surface_ = EGL_NO_SURFACE;
    EGLContext context_ = EGL_NO_CONTEXT;
    legacy_viewport::ViewportManager viewport_{480, 320};
};

void processInput(android_app* app, AudioPlayer* audio) {
    android_input_buffer* input = android_app_swap_input_buffers(app);
    if (input == nullptr) {
        return;
    }

    bool touch_down = false;
    for (uint64_t i = 0; i < input->motionEventsCount; ++i) {
        const int action = input->motionEvents[i].action & AMOTION_EVENT_ACTION_MASK;
        touch_down = touch_down || action == AMOTION_EVENT_ACTION_DOWN;
    }
    android_app_clear_motion_events(input);
    android_app_clear_key_events(input);

    if (touch_down && audio != nullptr) {
        audio->play();
    }
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

    AudioPlayer audio;
    audio.prepareWav(&assets, "audio/sfx/Attack01.wav");
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
        processInput(app, &audio);
        if (app->window != nullptr) {
            renderer.render(app->window);
        }
    }

    audio.shutdown();
    renderer.release();
}
