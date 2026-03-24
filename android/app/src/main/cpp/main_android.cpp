#include <android_native_app_glue.h>
#include <android/log.h>
#include <android/native_window.h>
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vg_lite.h"

#define LOG_TAG "VGLiteVulkan"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static int32_t handle_input(struct android_app* app, AInputEvent* event) {
    (void)app;
    (void)event;
    return 0;
}

static void handle_cmd(struct android_app* app, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            LOGI("APP_CMD_INIT_WINDOW");
            if (app->window != NULL) {
                ANativeWindow_setBuffersGeometry(app->window, 0, 0, AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM);
            }
            break;
        case APP_CMD_TERM_WINDOW:
            LOGI("APP_CMD_TERM_WINDOW");
            break;
        case APP_CMD_GAINED_FOCUS:
            LOGI("APP_CMD_GAINED_FOCUS");
            break;
        case APP_CMD_LOST_FOCUS:
            LOGI("APP_CMD_LOST_FOCUS");
            break;
    }
}

void android_main(struct android_app* state) {
    LOGI("VGLite Vulkan Android starting...");

    vg_lite_error_t err = vg_lite_init(0, 0);
    if (err != VG_LITE_SUCCESS) {
        LOGE("vg_lite_init failed: %d", err);
        return;
    }
    LOGI("vg_lite_init succeeded");

    state->onAppCmd = handle_cmd;
    state->onInputEvent = handle_input;

    while (1) {
        int ident;
        int events;
        struct android_poll_source* source;

        while ((ident = ALooper_pollOnce(0, NULL, &events, (void**)&source)) >= 0) {
            if (source != NULL) {
                source->process(state, source);
            }

            if (state->destroyRequested != 0) {
                LOGI("Destroy requested, exiting loop");
                goto cleanup;
            }
        }

        if (state->window != NULL) {
            vg_lite_buffer_t buffer;
            memset(&buffer, 0, sizeof(buffer));
            buffer.width = ANativeWindow_getWidth(state->window);
            buffer.height = ANativeWindow_getHeight(state->window);
            buffer.format = VG_LITE_RGB565;

            err = vg_lite_allocate(&buffer);
            if (err == VG_LITE_SUCCESS) {
                vg_lite_clear(&buffer, NULL, 0xFFFF0000);
                vg_lite_finish();
                vg_lite_free(&buffer);
            }
        }
    }

cleanup:
    err = vg_lite_close();
    if (err != VG_LITE_SUCCESS) {
        LOGE("vg_lite_close failed: %d", err);
    }
    LOGI("VGLite Vulkan Android exiting");
}
