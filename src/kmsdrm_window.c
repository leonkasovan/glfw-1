//========================================================================
// GLFW 3.5 Wayland - www.glfw.org
//------------------------------------------------------------------------
// Copyright (c) 2014 Jonas Ådahl <jadahl@gmail.com>
//
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would
//    be appreciated but is not required.
//
// 2. Altered source versions must be plainly marked as such, and must not
//    be misrepresented as being the original software.
//
// 3. This notice may not be removed or altered from any source
//    distribution.
//
//========================================================================
/*
_glfwCreateWindowKMSDRM
    _glfwInitEGL
        _glfw.egl.display = eglGetDisplay(_glfw.platform.getEGLNativeDisplay());
        if (!eglInitialize(_glfw.egl.display, &_glfw.egl.major, &_glfw.egl.minor)) {
    _glfwCreateContextEGL
        if (!chooseEGLConfig(ctxconfig, fbconfig, &config))
        if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        window->context.egl.handle = eglCreateContext(_glfw.egl.display,config, share, attribs);
        window->context.egl.surface = eglCreateWindowSurface(_glfw.egl.display, config, native, attribs);
    _glfwRefreshContextAttribs
        glfwMakeContextCurrent((GLFWwindow*) window);
        window->context.swapBuffers(window); => static void swapBuffersEGL(_GLFWwindow* window) {
            gbm_surface_release_buffer(window->context.egl.surface, _glfw.kmsdrm.gbm.bo);
            eglSwapBuffers(_glfw.egl.display, window->context.egl.surface);
            _glfw.kmsdrm.next_bo = gbm_surface_lock_front_buffer(window->context.egl.surface);
            void* fb_id_ptr = gbm_bo_get_user_data(_glfw.kmsdrm.next_bo);
            unsigned w = gbm_bo_get_width(_glfw.kmsdrm.next_bo);
            unsigned h = gbm_bo_get_height(_glfw.kmsdrm.next_bo);
            uint32_t strides = gbm_bo_get_stride(_glfw.kmsdrm.next_bo);
            uint32_t handles = gbm_bo_get_handle(_glfw.kmsdrm.next_bo).u32;
            int rc = drmModeAddFB(_glfw.kmsdrm.drm_fd, w, h, 24, 32, strides, handles, &fb_id);
            gbm_bo_set_user_data(_glfw.kmsdrm.next_bo, (void*) &fb_id, KMSDRM_FBDestroyCallback); // Associate our DRM framebuffer with this buffer object
            int ret = drmModeSetCrtc(_glfw.kmsdrm.drm_fd,
            OR
            int ret = drmModePageFlip(_glfw.kmsdrm.drm_fd, _glfw.kmsdrm.encoder->crtc_id,fb_id, flip_flags, &(_glfw.kmsdrm.waiting_for_flip));
        glfwMakeContextCurrent((GLFWwindow*) previous);
*/

#define _GNU_SOURCE

#include "internal.h"

#if defined(_GLFW_KMSDRM)

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

int init_surface(struct gbm* gbm, uint64_t modifier) {
    debug_printf("init_surface: gbm_surface_create_with_modifiers gbm.device:%p gbm.width=%d gbm.height=%d, gbm.format=%d modifier=%ld\n", gbm->dev, gbm->width, gbm->height, gbm->format, modifier);
    gbm->surface = gbm_surface_create_with_modifiers(gbm->dev, gbm->width, gbm->height, gbm->format, &modifier, 1);

    if (!gbm->surface) {
        if (modifier != DRM_FORMAT_MOD_LINEAR) {
            _glfwInputError(GLFW_PLATFORM_ERROR, "init_surface: Modifiers requested but support isn't available\n");
            return -2;
        }
        debug_printf("init_surface: gbm_surface_create gbm.device:%p gbm.width=%d gbm.height=%d, gbm.format=%d modifier=%d", gbm->dev, gbm->width, gbm->height, gbm->format, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
        gbm->surface = gbm_surface_create(gbm->dev, gbm->width, gbm->height, gbm->format, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    }
    if (!gbm->surface) {
        _glfwInputError(GLFW_PLATFORM_ERROR, "init_surface: Failed to create gbm surface\n");
        return -3;
    }
    printf("init_surface: %dx%d created\n", gbm->width, gbm->height);
    return 0;
}

int init_gbm(struct gbm* gbm, int drm_fd, int w, int h, uint32_t format, uint64_t modifier) {
    debug_printf("init_gbm: gbm_create_device drm_fd=%d\n", drm_fd);
    gbm->dev = gbm_create_device(drm_fd);
    if (!gbm->dev)
        return -1;

    gbm->format = format;
    gbm->surface = NULL;

    debug_printf("init_gbm: request width=%d height=%d\n", w, h);
    if (w && h) {
        gbm->width = w;
        gbm->height = h;
    } else {
        gbm->width = _glfw.kmsdrm.drm.mode->hdisplay;
        gbm->height = _glfw.kmsdrm.drm.mode->vdisplay;
    }

    return init_surface(gbm, modifier);
}

static void handleEvents(double* timeout) {
#if defined(GLFW_BUILD_LINUX_JOYSTICK)
    if (_glfw.joysticksInitialized)
        _glfwDetectJoystickConnectionLinux();
#endif
// #if defined(GLFW_BUILD_LINUX_KEYBOARD)
//     if (_glfw.keyboardsInitialized)
//         _glfwDetectKeyboardConnectionLinux();
// #endif

    GLFWbool event = GLFW_FALSE;
    struct pollfd fds[1];

    if (_glfw.kmsdrm.keyboard_fd > 0) {
        fds[0].fd = _glfw.kmsdrm.keyboard_fd;
        fds[0].events = POLLIN;
    }

    GLFWbool ret = 0;
    while (!event) {
        ret = _glfwPollPOSIX(fds, sizeof(fds) / sizeof(fds[0]), timeout);
        if (ret == -1) { // Poll error
            perror("poll");
            return;
        } else if (ret == 0) { // Timeout occurred! No data
            return;
        }

        // Data available then handle event
        if (fds[0].revents & POLLIN) {
            _glfwPollKeyboardLinux();
            event = GLFW_TRUE;
        }
    }
}

EGLenum _glfwGetEGLPlatformKMSDRM(EGLint** attribs) {
    if (_glfw.egl.EXT_platform_base && _glfw.egl.EXT_platform_kmsdrm)
        return EGL_PLATFORM_GBM_MESA;
    else
        return 0;
}

EGLNativeDisplayType _glfwGetEGLNativeDisplayKMSDRM(void) {
    return _glfw.kmsdrm.gbm.dev;
}

EGLNativeWindowType _glfwGetEGLNativeWindowKMSDRM(_GLFWwindow* window) {
    // debug_printf("kmsdrm_window.c:%d _glfwGetEGLNativeWindowKMSDRM => %p\n", __LINE__, (void*) _glfw.kmsdrm.gbm.surface);
    // return _glfw.kmsdrm.egl.surface;
    return _glfw.kmsdrm.gbm.surface;
    // return window->context.egl.surface;
}

GLFWbool _glfwCreateWindowKMSDRM(_GLFWwindow* window, const _GLFWwndconfig* wndconfig, const _GLFWctxconfig* ctxconfig, const _GLFWfbconfig* fbconfig) {
    if (init_gbm(&_glfw.kmsdrm.gbm, _glfw.kmsdrm.drm.fd, wndconfig->width, wndconfig->height, _glfw.kmsdrm.format, _glfw.kmsdrm.modifier)) {
        debug_printf("Failed to initialize GBM.\n");
        return GLFW_FALSE;
    } else {
        debug_printf("Initializing GBM [OK]\n");
    }

    // Initialize EGL
    if (ctxconfig->client != GLFW_NO_API) {
        if (ctxconfig->source == GLFW_EGL_CONTEXT_API || ctxconfig->source == GLFW_NATIVE_CONTEXT_API) {
            if (!_glfwInitEGL()) {
                _glfwInputError(GLFW_PLATFORM_ERROR, "_glfwCreateWindowKMSDRM: _glfwInitEGL failed\n");
                return GLFW_FALSE;
            }
        }
    }

    // Create Context (EGL)
    if (ctxconfig->client != GLFW_NO_API) {
        if (ctxconfig->source == GLFW_EGL_CONTEXT_API || ctxconfig->source == GLFW_NATIVE_CONTEXT_API) {
            if (!_glfwCreateContextEGL(window, ctxconfig, fbconfig)) {
                _glfwInputError(GLFW_PLATFORM_ERROR, "_glfwCreateWindowKMSDRM: _glfwCreateContextEGL failed\n");
                return GLFW_FALSE;
            }
        }

        if (!_glfwRefreshContextAttribs(window, ctxconfig)) {
            debug_printf("kmsdrm_window.c:%d _glfwRefreshContextAttribs failed\n", __LINE__);
            return GLFW_FALSE;
        } else {
            // debug_printf("kmsdrm_window.c:%d _glfwRefreshContextAttribs succeed\n", __LINE__);
        }
    }

    // initial eglSwap here
    debug_printf("_glfwCreateWindowKMSDRM: eglMakeCurrent egl.display=%p egl.surface=%p egl.context=%p\n", _glfw.egl.display, window->context.egl.surface, window->context.egl.handle);
    if (!eglMakeCurrent(_glfw.egl.display, window->context.egl.surface, window->context.egl.surface, window->context.egl.handle)) {
        debug_printf("EGL: Failed to make context current\n");
        return GLFW_FALSE;
    }
    if (_glfw.kmsdrm.gbm.surface) {
        debug_printf("_glfwCreateWindowKMSDRM: eglSwapBuffers egl.display=%p egl.surface=%p\n", _glfw.egl.display, window->context.egl.surface);
        if (!eglSwapBuffers(_glfw.egl.display, window->context.egl.surface)) {
            debug_printf("_glfwCreateWindowKMSDRM: eglSwapBuffers error\n");
            return GLFW_FALSE;
        }
        debug_puts("_glfwCreateWindowKMSDRM: gbm_surface_lock_front_buffer");
        _glfw.kmsdrm.gbm.bo = gbm_surface_lock_front_buffer(_glfw.kmsdrm.gbm.surface);
        if (!_glfw.kmsdrm.gbm.bo) {
            debug_printf("gbm_surface_lock_front_buffer error.\n");
            return GLFW_FALSE;
        }
    }
    debug_puts("_glfwCreateWindowKMSDRM: drm_fb_get_from_bo");
    _glfw.kmsdrm.gbm.fb = drm_fb_get_from_bo(_glfw.kmsdrm.gbm.bo);
    if (!_glfw.kmsdrm.gbm.fb) {
        debug_printf("Failed to get a new framebuffer BO\n");
        return GLFW_FALSE;
    }
    debug_printf("_glfwCreateWindowKMSDRM: drmModeSetCrtc drm.fd=%d drm.crtc_id=%d fb_id=%d drm.connector_id=%d drm.mode=%p\n", _glfw.kmsdrm.drm.fd, _glfw.kmsdrm.drm.crtc_id, _glfw.kmsdrm.gbm.fb->fb_id, _glfw.kmsdrm.drm.connector_id, _glfw.kmsdrm.drm.mode);
    if (drmModeSetCrtc(_glfw.kmsdrm.drm.fd, _glfw.kmsdrm.drm.crtc_id, _glfw.kmsdrm.gbm.fb->fb_id, 0, 0, &(_glfw.kmsdrm.drm.connector_id), 1, _glfw.kmsdrm.drm.mode)) {
        debug_printf("drmModeSetCrtc error: %s (%d)\n", strerror(errno), errno);
        return GLFW_FALSE;
    }

#if defined(GLFW_BUILD_LINUX_KEYBOARD)
    _glfw.kmsdrm.window = window;
#endif
    return GLFW_TRUE;
}

void _glfwDestroyWindowKMSDRM(_GLFWwindow* window) {
    // Clean up framebuffer, CRTC, etc.
    // drmModeSetCrtc(_glfw.kmsdrm.fd, window->kmsdrm.saved_crtc, 0, 0, 0, NULL, 0, NULL);
}

void _glfwGetFramebufferSizeKMSDRM(_GLFWwindow* window, int* width, int* height) {
    if (width)
        *width = _glfw.kmsdrm.gbm.width;
    if (height)
        *height = _glfw.kmsdrm.gbm.height;
}

void _glfwPollEventsKMSDRM(void) {
    double timeout = 0.0;
    handleEvents(&timeout);
}

void _glfwSetWindowDecoratedKMSDRM(_GLFWwindow* window, GLFWbool enabled){
    debug_printf("_glfwSetWindowDecoratedKMSDRM not implemented\n\tenabled=%d\n", enabled);
}

void _glfwSetWindowPosKMSDRM(_GLFWwindow* window, int xpos, int ypos){
    debug_printf("_glfwSetWindowPosKMSDRM not implemented\n\txpos=%d\n\typos=%d\n", xpos, ypos);
}
void _glfwGetWindowPosKMSDRM(_GLFWwindow* window, int* xpos, int* ypos){
    if (xpos)
        *xpos = 0;
    if (ypos)
        *ypos = 0;
}

void _glfwSetWindowSizeKMSDRM(_GLFWwindow* window, int width, int height){
    debug_printf("_glfwSetWindowSizeKMSDRM not implemented\n\twidth=%d\n\theight=%d\n", width, height);
}
void _glfwGetWindowSizeKMSDRM(_GLFWwindow* window, int* width, int* height){
    if (width)
        *width = _glfw.kmsdrm.gbm.width;
    if (height)
        *height = _glfw.kmsdrm.gbm.height;
}

void _glfwSetCursorModeKMSDRM(_GLFWwindow* window, int mode){
    debug_printf("_glfwSetCursorModeKMSDRM not implemented\n\tmode=%d\n", mode);
}

const char *_glfwGetClipboardStringKMSDRM(void){
    debug_puts("_glfwGetClipboardStringKMSDRM not implemented");
    return "";
}

GLFWvidmode *_glfwGetVideoModesKMSDRM(_GLFWmonitor *monitor, int *found){
    *found = 1;
    return _glfw.monitors[0]->modes;
}

GLFWbool _glfwGetVideoModeKMSDRM(_GLFWmonitor *monitor, GLFWvidmode *mode){
    *mode = _glfw.monitors[0]->modes[0];
    return GLFW_TRUE;
}

void _glfwSetWindowIconKMSDRM(_GLFWwindow *window, int count, const GLFWimage *images){
    debug_puts("_glfwSetWindowIconKMSDRM not implemented");
}

GLFWbool _glfwGetGammaRampKMSDRM(_GLFWmonitor *monitor, GLFWgammaramp *ramp){
    debug_puts("_glfwGetGammaRampKMSDRM not implemented");
    return GLFW_FALSE;
}

void _glfwSetGammaRampKMSDRM(_GLFWmonitor *monitor, const GLFWgammaramp *ramp){
    debug_puts("_glfwSetGammaRampKMSDRM not implemented");
}

void _glfwSetWindowTitleKMSDRM(_GLFWwindow *window, const char *title){
    debug_printf("_glfwSetWindowTitleKMSDRM not implemented title=%s\n", title);
}

void _glfwSetWindowSizeLimitsKMSDRM(_GLFWwindow *window, int minwidth, int minheight, int maxwidth, int maxheight){
    debug_printf("_glfwSetWindowSizeLimitsKMSDRM not implemented minwidth=%d, minheight=%d, maxwidth=%d maxheight=%d\n", minwidth, minheight, maxwidth, maxheight);
}

void _glfwSetWindowAspectRatioKMSDRM(_GLFWwindow *window, int n, int d){
    debug_printf("_glfwSetWindowAspectRatioKMSDRM not implemented n=%d d=%d\n", n, d);
}

void _glfwGetWindowFrameSizeKMSDRM(_GLFWwindow *window, int *left, int *top, int *right, int *bottom){
    *left = 0;
    *top = 0;
    *right = _glfw.kmsdrm.gbm.width;
    *bottom = _glfw.kmsdrm.gbm.height;
    debug_printf("_glfwGetWindowFrameSizeKMSDRM not implemented\n");
}

void _glfwGetWindowContentScaleKMSDRM(_GLFWwindow *window, float *xscale, float *yscale){
    *xscale = 1.0;
    *yscale = 1.0;
    debug_printf("_glfwGetWindowContentScaleKMSDRM not implemented\n");
}

void _glfwRestoreWindowKMSDRM(_GLFWwindow *window){
    debug_puts("_glfwRestoreWindowKMSDRM not implemented");
}

void _glfwMaximizeWindowKMSDRM(_GLFWwindow *window){
    debug_puts("_glfwMaximizeWindowKMSDRM not implemented");
}

void _glfwShowWindowKMSDRM(_GLFWwindow *window){
    debug_puts("_glfwShowWindowKMSDRM not implemented");
}

void _glfwHideWindowKMSDRM(_GLFWwindow *window){
    debug_puts("_glfwHideWindowKMSDRM not implemented");
}

void _glfwFocusWindowKMSDRM(_GLFWwindow *window){
    debug_puts("_glfwFocusWindowKMSDRM not implemented");
}

GLFWbool _glfwWindowFocusedKMSDRM(_GLFWwindow *window){
    debug_puts("_glfwWindowFocusedKMSDRM not implemented");
    return GLFW_FALSE;
}

GLFWbool _glfwWindowIconifiedKMSDRM(_GLFWwindow *window){
    debug_puts("_glfwWindowIconifiedKMSDRM not implemented");
    return GLFW_FALSE;
}

GLFWbool _glfwWindowVisibleKMSDRM(_GLFWwindow *window){
    debug_puts("_glfwWindowVisibleKMSDRM not implemented");
    return GLFW_FALSE;
}

void _glfwIconifyWindowKMSDRM(_GLFWwindow *window){
    debug_puts("_glfwIconifyWindowKMSDRM not implemented");
}

void _glfwSetWindowMonitorKMSDRM(_GLFWwindow *window, _GLFWmonitor *monitor, int xpos, int ypos, int width, int height, int refreshRate){
    debug_puts("_glfwSetWindowMonitorKMSDRM not implemented");
}

// void _glfwPollMonitorsKMSDRM(void){
//     debug_puts("_glfwPollMonitorsKMSDRM not implemented");
// }

// GLFWbool _glfwDetectMonitorsKMSDRM(void){
//     debug_puts("_glfwDetectMonitorsKMSDRM not implemented");
//     return GLFW_FALSE;
// }

// void _glfwFreeMonitorKMSDRM(_GLFWmonitor *monitor){
//     debug_puts("_glfwFreeMonitorKMSDRM not implemented");
// }

// void _glfwGetMonitorContentScaleKMSDRM(_GLFWmonitor *monitor, float *xscale, float *yscale){
//     debug_puts("_glfwGetMonitorContentScaleKMSDRM not implemented");
// }

// void _glfwGetMonitorWorkareaKMSDRM(_GLFWmonitor *monitor, int *xpos, int *ypos, int *width, int *height){
//     debug_puts("_glfwGetMonitorWorkareaKMSDRM not implemented");
// }

void _glfwSetWindowResizableKMSDRM(_GLFWwindow *window, GLFWbool enabled){
    debug_puts("_glfwSetWindowResizableKMSDRM not implemented");
}

void _glfwSetWindowFloatingKMSDRM(_GLFWwindow *window, GLFWbool enabled){
    debug_puts("_glfwSetWindowFloatingKMSDRM not implemented");
}

void _glfwSetWindowMousePassthroughKMSDRM(_GLFWwindow *window, GLFWbool enabled){
    debug_puts("_glfwSetWindowMousePassthroughKMSDRM not implemented");
}

float _glfwGetWindowOpacityKMSDRM(_GLFWwindow *window){
    debug_puts("_glfwGetWindowOpacityKMSDRM not implemented");
    return 1.0;
}

void _glfwSetWindowOpacityKMSDRM(_GLFWwindow *window, float opacity){
    debug_puts("_glfwSetWindowOpacityKMSDRM not implemented");
}

void _glfwSetRawMouseMotionKMSDRM(_GLFWwindow *window, GLFWbool enabled){
    debug_puts("_glfwSetRawMouseMotionKMSDRM not implemented");
}

GLFWbool _glfwRawMouseMotionSupportedKMSDRM(void){
    debug_puts("_glfwRawMouseMotionSupportedKMSDRM not implemented");
    return GLFW_FALSE;
}

void _glfwRequestWindowAttentionKMSDRM(_GLFWwindow *window){
    debug_puts("_glfwRequestWindowAttentionKMSDRM not implemented");
}

GLFWbool _glfwWindowMaximizedKMSDRM(_GLFWwindow *window){
    debug_puts("_glfwWindowMaximizedKMSDRM not implemented");
    return GLFW_FALSE;
}

GLFWbool _glfwWindowHoveredKMSDRM(_GLFWwindow *window){
    debug_puts("_glfwWindowHoveredKMSDRM not implemented");
    return GLFW_FALSE;
}

GLFWbool _glfwFramebufferTransparentKMSDRM(_GLFWwindow *window){
    debug_puts("_glfwFramebufferTransparentKMSDRM not implemented");
    return GLFW_FALSE;
}

void _glfwWaitEventsKMSDRM(void){
    double timeout = -1.0;
    handleEvents(&timeout);
}

void _glfwWaitEventsTimeoutKMSDRM(double timeout){
    handleEvents(&timeout);
}

void _glfwPostEmptyEventKMSDRM(void){
    debug_puts("_glfwPostEmptyEventKMSDRM not implemented");
}

void _glfwGetRequiredInstanceExtensionsKMSDRM(char **extensions){
    debug_puts("_glfwGetRequiredInstanceExtensionsKMSDRM not implemented");
}

GLFWbool _glfwGetPhysicalDevicePresentationSupportKMSDRM(VkInstance instance, VkPhysicalDevice device, uint32_t queuefamily){
    debug_puts("_glfwGetPhysicalDevicePresentationSupportKMSDRM not implemented");
    return GLFW_FALSE;
}

VkResult _glfwCreateWindowSurfaceKMSDRM(VkInstance instance, _GLFWwindow *window, const VkAllocationCallbacks *allocator, VkSurfaceKHR *surface){
    debug_puts("_glfwCreateWindowSurfaceKMSDRM not implemented");
    return VK_ERROR_EXTENSION_NOT_PRESENT;
}

void _glfwGetCursorPosKMSDRM(_GLFWwindow *window, double *xpos, double *ypos){
    debug_puts("_glfwGetCursorPosKMSDRM not implemented");
}

void _glfwSetCursorPosKMSDRM(_GLFWwindow *window, double x, double y){
    debug_puts("_glfwSetCursorPosKMSDRM not implemented");
}

GLFWbool _glfwCreateCursorKMSDRM(_GLFWcursor *cursor, const GLFWimage *image, int xhot, int yhot){
    debug_puts("_glfwCreateCursorKMSDRM not implemented");
    return GLFW_FALSE;
}
GLFWbool _glfwCreateStandardCursorKMSDRM(_GLFWcursor *cursor, int shape){
    debug_puts("_glfwCreateStandardCursorKMSDRM not implemented");
    return GLFW_FALSE;
}
void _glfwDestroyCursorKMSDRM(_GLFWcursor *cursor){
    debug_puts("_glfwDestroyCursorKMSDRM not implemented");
}
void _glfwSetCursorKMSDRM(_GLFWwindow *window, _GLFWcursor *cursor){
    debug_puts("_glfwSetCursorKMSDRM not implemented");
}
void _glfwSetClipboardStringKMSDRM(const char *string){
    debug_puts("_glfwSetClipboardStringKMSDRM not implemented");
}

const char *_glfwGetScancodeNameKMSDRM(int scancode){
    debug_puts("_glfwGetScancodeNameKMSDRM not implemented");
    return "";
}
int _glfwGetKeyScancodeKMSDRM(int key){
    debug_puts("_glfwGetKeyScancodeKMSDRM not implemented");
    return 0;
}

#endif