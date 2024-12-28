//_glfwInputError(GLFW_PLATFORM_ERROR, "KMSDRM: Failed to open directory '%s'", device);
//========================================================================
// GLFW 3.5 KMSDRM - www.glfw.org
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
glfwInit
glfwWindowHint
glfwCreateWindow
glfwMakeContextCurrent
glfwSetFramebufferSizeCallback
glfwWindowShouldClose
glfwSwapBuffers
glfwPollEvents
glfwTerminate
glfwGetKey
glfwSetWindowShouldClose
glfwGetProcAddress

Snippet:
_glfwInputError(GLFW_PLATFORM_ERROR, "KMSDRM: ");
return GLFW_FALSE;
*/

#include "internal.h"

#if defined(_GLFW_KMSDRM)

static int get_resources(const char* src, int fd, drmModeRes** resources) {
    debug_printf("%s: drmModeGetResources\n", src);
    *resources = drmModeGetResources(fd);
    if (*resources == NULL)
        return -1;
    return 0;
}

static int find_drm_device(drmModeRes** resources) {
    drmDevicePtr devices[MAX_DRM_DEVICES] = { NULL };
    int num_devices, fd = -1;

    debug_puts("find_drm_device: drmGetDevices2");
    num_devices = drmGetDevices2(0, devices, MAX_DRM_DEVICES);
    if (num_devices < 0) {
        _glfwInputError(GLFW_PLATFORM_ERROR, "drmGetDevices2 failed: %s\n", strerror(-num_devices));
        return -1;
    }

    for (int i = 0; i < num_devices; i++) {
        drmDevicePtr device = devices[i];
        int ret;

        if (!(device->available_nodes & (1 << DRM_NODE_PRIMARY)))
            continue;
        /* OK, it's a primary device. If we can get the
         * drmModeResources, it means it's also a
         * KMS-capable device.
         */
        printf("[GLFW] Opening DRM device %s\n", device->nodes[DRM_NODE_PRIMARY]);
        fd = open(device->nodes[DRM_NODE_PRIMARY], O_RDWR);
        if (fd < 0)
            continue;
        ret = get_resources("find_drm_device", fd, resources);
        if (!ret)
            break;
        close(fd);
        fd = -1;
    }
    debug_puts("find_drm_device: drmFreeDevices");
    drmFreeDevices(devices, num_devices);

    if (fd < 0)
        _glfwInputError(GLFW_PLATFORM_ERROR, "No drm device found!");
    return fd;
}

static int32_t find_crtc_for_encoder(const drmModeRes* resources,
    const drmModeEncoder* encoder) {
    int i;

    for (i = 0; i < resources->count_crtcs; i++) {
        /* possible_crtcs is a bitmask as described here:
         * https://dvdhrm.wordpress.com/2012/09/13/linux-drm-mode-setting-api
         */
        const uint32_t crtc_mask = 1 << i;
        const uint32_t crtc_id = resources->crtcs[i];
        if (encoder->possible_crtcs & crtc_mask) {
            return crtc_id;
        }
    }
    /* no match found */
    return -1;
}

static int32_t find_crtc_for_connector(const struct drm* drm, const drmModeRes* resources,
    const drmModeConnector* connector) {
    int i;

    for (i = 0; i < connector->count_encoders; i++) {
        const uint32_t encoder_id = connector->encoders[i];
        debug_puts("find_crtc_for_connector: drmModeGetEncoder");
        drmModeEncoder* encoder = drmModeGetEncoder(drm->fd, encoder_id);

        if (encoder) {
            const int32_t crtc_id = find_crtc_for_encoder(resources, encoder);
            debug_puts("find_crtc_for_connector: drmModeFreeEncoder");
            drmModeFreeEncoder(encoder);
            if (crtc_id != 0) {
                return crtc_id;
            }
        }
    }
    /* no match found */
    return -1;
}

static drmModeConnector* find_drm_connector(int fd, drmModeRes* resources,
    int connector_id) {
    drmModeConnector* connector = NULL;
    int i;

    if (connector_id >= 0) {
        if (connector_id >= resources->count_connectors)
            return NULL;
        debug_puts("find_drm_connector: drmModeGetConnector");
        connector = drmModeGetConnector(fd, resources->connectors[connector_id]);
        if (connector && connector->connection == DRM_MODE_CONNECTED)
            return connector;
        debug_puts("find_drm_connector: drmModeFreeConnector");
        drmModeFreeConnector(connector);
        return NULL;
    }

    debug_printf("find_drm_connector: count_connectors=%d\n", resources->count_connectors);
    for (i = 0; i < resources->count_connectors; i++) {
        debug_printf("find_drm_connector: drmModeGetConnector i=%d\n", i);
        connector = drmModeGetConnector(fd, resources->connectors[i]);
        if (connector && connector->connection == DRM_MODE_CONNECTED) {
            /* it's connected, let's use this! */
            debug_printf("find_drm_connector: connector[%d] is CONNECTED.\n", i);
            break;
        }
        debug_puts("find_drm_connector: drmModeFreeConnector");
        drmModeFreeConnector(connector);
        connector = NULL;
    }

    return connector;
}

int init_drm(struct drm* drm, const char* device, const char* mode_str, int connector_id, unsigned int vrefresh, unsigned int count, bool nonblocking) {
    drmModeRes* resources;
    drmModeConnector* connector = NULL;
    drmModeEncoder* encoder = NULL;
    int i, ret, area;

    if (device) {
        drm->fd = open(device, O_RDWR);
        ret = get_resources("init_drm", drm->fd, &resources);
        if (ret < 0 && errno == EOPNOTSUPP)
            _glfwInputError(GLFW_PLATFORM_ERROR, "%s does not look like a modeset device\n", device);
    } else {
        drm->fd = find_drm_device(&resources);
    }

    if (drm->fd < 0) {
        _glfwInputError(GLFW_PLATFORM_ERROR, "Could not open drm device");
        return -1;
    }

    if (!resources) {
        _glfwInputError(GLFW_PLATFORM_ERROR, "drmModeGetResources failed: %s", strerror(errno));
        return -1;
    }

    /* find a connected connector: */
    connector = find_drm_connector(drm->fd, resources, connector_id);
    if (!connector) {
        /* we could be fancy and listen for hotplug events and wait for
         * a connector..
         */
        _glfwInputError(GLFW_PLATFORM_ERROR, "No connected connector!");
        return -1;
    }

    /* find user requested mode: */
    if (mode_str && *mode_str) {
        for (i = 0; i < connector->count_modes; i++) {
            drmModeModeInfo* current_mode = &connector->modes[i];

            if (strcmp(current_mode->name, mode_str) == 0) {
                if (vrefresh == 0 || current_mode->vrefresh == vrefresh) {
                    drm->mode = current_mode;
                    debug_printf("init_drm: found requested mode %dx%d\n", current_mode->hdisplay, current_mode->vdisplay);
                    break;
                }
            }
        }
        if (!drm->mode)
            debug_printf("init_drm: requested mode not found, using default mode!\n");
    }

    /* find preferred mode or the highest resolution mode: */
    if (!drm->mode) {
        for (i = 0, area = 0; i < connector->count_modes; i++) {
            drmModeModeInfo* current_mode = &connector->modes[i];

            if (current_mode->type & DRM_MODE_TYPE_PREFERRED) {
                drm->mode = current_mode;
                debug_printf("init_drm: found preferred mode %dx%d\n", current_mode->hdisplay, current_mode->vdisplay);
                break;
            }

            int current_area = current_mode->hdisplay * current_mode->vdisplay;
            if (current_area > area) {
                drm->mode = current_mode;
                debug_printf("init_drm: found higher mode %dx%d\n", current_mode->hdisplay, current_mode->vdisplay);
                area = current_area;
            }
        }
    }

    if (!drm->mode) {
        _glfwInputError(GLFW_PLATFORM_ERROR, "init_drm: could not find mode!");
        return -1;
    }

    /* find encoder: */
    for (i = 0; i < resources->count_encoders; i++) {
        debug_puts("init_drm: drmModeGetEncoder");
        encoder = drmModeGetEncoder(drm->fd, resources->encoders[i]);
        if (encoder->encoder_id == connector->encoder_id)
            break;
        debug_puts("init_drm: drmModeFreeEncoder");
        drmModeFreeEncoder(encoder);
        encoder = NULL;
    }

    if (encoder) {
        drm->crtc_id = encoder->crtc_id;
    } else {
        int32_t crtc_id = find_crtc_for_connector(drm, resources, connector);
        if (crtc_id == -1) {
            printf("init_drm: no crtc found!\n");
            return -1;
        }
        drm->crtc_id = crtc_id;
    }
    debug_printf("init_drm: using encoder crtc_id=%d\n", drm->crtc_id);

    for (i = 0; i < resources->count_crtcs; i++) {
        if (resources->crtcs[i] == drm->crtc_id) {
            debug_printf("init_drm: using crtc_index=%d\n", drm->crtc_index);
            drm->crtc_index = i;
            break;
        }
    }
    debug_puts("init_drm: drmModeFreeResources");
    drmModeFreeResources(resources);
    drm->connector_id = connector->connector_id;
    drm->count = count;
    drm->nonblocking = nonblocking;
    return 0;
}

static void drm_fb_destroy_callback(struct gbm_bo* bo, void* data) {
    int drm_fd = gbm_device_get_fd(gbm_bo_get_device(bo));
    struct drm_fb* fb = data;
    if (fb->fb_id) {
        drmModeRmFB(drm_fd, fb->fb_id);
    }
    free(fb);
    debug_puts("Cleaning up [OK]");
}

struct drm_fb* drm_fb_get_from_bo(struct gbm_bo* bo) {
    debug_printf("drm_fb_get_from_bo: gbm_device_get_fd gbm_bo_get_device gbm_bo_get_user_data bo=%p\n", bo);
    int drm_fd = gbm_device_get_fd(gbm_bo_get_device(bo));
    struct drm_fb* fb = gbm_bo_get_user_data(bo);
    uint32_t width, height, format,
        strides[4] = { 0 }, handles[4] = { 0 },
        offsets[4] = { 0 }, flags = 0;
    int ret = -1;

    if (fb)
        return fb;

    fb = calloc(1, sizeof * fb);
    fb->bo = bo;

    debug_puts("drm_fb_get_from_bo: gbm_bo_get_width gbm_bo_get_height gbm_bo_get_format");
    width = gbm_bo_get_width(bo);
    height = gbm_bo_get_height(bo);
    format = gbm_bo_get_format(bo);

    // if (gbm_bo_get_handle_for_plane && gbm_bo_get_modifier &&
    //     gbm_bo_get_plane_count && gbm_bo_get_stride_for_plane &&
    //     gbm_bo_get_offset) {

    uint64_t modifiers[4] = { 0 };
    debug_puts("drm_fb_get_from_bo: gbm_bo_get_modifier gbm_bo_get_plane_count");
    modifiers[0] = gbm_bo_get_modifier(bo);
    const int num_planes = gbm_bo_get_plane_count(bo);
    for (int i = 0; i < num_planes; i++) {
        debug_printf("drm_fb_get_from_bo: plane[%d] gbm_bo_get_modifier gbm_bo_get_plane_count gbm_bo_get_stride_for_plane gbm_bo_get_offset\n", i);
        handles[i] = gbm_bo_get_handle_for_plane(bo, i).u32;
        strides[i] = gbm_bo_get_stride_for_plane(bo, i);
        offsets[i] = gbm_bo_get_offset(bo, i);
        modifiers[i] = modifiers[0];
    }

    if (modifiers[0] && modifiers[0] != DRM_FORMAT_MOD_INVALID) {
        flags = DRM_MODE_FB_MODIFIERS;
    }

    debug_printf("drm_fb_get_from_bo: drmModeAddFB2WithModifiers drm_fd=%d width=%d height=%d format=%d modifiers={%lu,%lu,%lu,%lu} fb.fb_id=%d flags=%d\n", drm_fd, width, height, format
        , modifiers[0], modifiers[1], modifiers[2], modifiers[3]
        , fb->fb_id, flags);
    ret = drmModeAddFB2WithModifiers(drm_fd, width, height, format, handles, strides, offsets, modifiers, &fb->fb_id, flags);
    // }

    if (ret) {
        if (flags)
            debug_puts("drm_fb_get_from_bo: Modifiers failed!");

        memcpy(handles, (uint32_t[4]) { gbm_bo_get_handle(bo).u32, 0, 0, 0 }, 16);
        memcpy(strides, (uint32_t[4]) { gbm_bo_get_stride(bo), 0, 0, 0 }, 16);
        memset(offsets, 0, 16);
        debug_printf("drm_fb_get_from_bo: drmModeAddFB2 drm_fd=%d width=%d height=%d format=%d fb.fb_id=%d\n", drm_fd, width, height, format, fb->fb_id);
        ret = drmModeAddFB2(drm_fd, width, height, format, handles, strides, offsets, &fb->fb_id, 0);
    }

    if (ret) {
        printf("drm_fb_get_from_bo: failed to create fb: %s\n", strerror(errno));
        free(fb);
        return NULL;
    }
    debug_puts("drm_fb_get_from_bo: gbm_bo_set_user_data");
    gbm_bo_set_user_data(bo, fb, drm_fb_destroy_callback);
    return fb;
}

// Initialize the KMSDRM platform
GLFWbool _glfwInitKMSDRM(void) {
    _glfw.kmsdrm.device = NULL;
    _glfw.kmsdrm.format = DRM_FORMAT_ARGB8888;  // Alternate DRM_FORMAT_RGBA8888
    _glfw.kmsdrm.modifier = DRM_FORMAT_MOD_LINEAR;
    _glfw.kmsdrm.samples = 0;
    _glfw.kmsdrm.atomic = 0;
    _glfw.kmsdrm.gears = 0;
    _glfw.kmsdrm.offscreen = 0;
    _glfw.kmsdrm.connector_id = -1;
    _glfw.kmsdrm.vrefresh = 0;
    _glfw.kmsdrm.count = ~0;
    _glfw.kmsdrm.surfaceless = false;
    _glfw.kmsdrm.nonblocking = false;
    strncpy(_glfw.kmsdrm.mode_str, "", DRM_DISPLAY_MODE_LEN);
    _glfw.kmsdrm.drm.mode = NULL;
#ifdef DEBUG    
    _glfw.kmsdrm.start_time = get_time_ns();
    _glfw.kmsdrm.report_time = get_time_ns();
#endif    

    GLFWbool ret = init_drm(&(_glfw.kmsdrm.drm), _glfw.kmsdrm.device, _glfw.kmsdrm.mode_str, _glfw.kmsdrm.connector_id, _glfw.kmsdrm.vrefresh, _glfw.kmsdrm.count, _glfw.kmsdrm.nonblocking);
    if (ret) {
        debug_printf("_glfwInitKMSDRM: Initializing DRM [FAIL:%d]\n", ret);
    } else {
        debug_printf("_glfwInitKMSDRM: Initializing DRM fullscreen %dx%d [OK]\n", _glfw.kmsdrm.drm.mode->hdisplay, _glfw.kmsdrm.drm.mode->vdisplay);
    }

    // TODO:
        //createKeyTables();
    if (!_glfwInitJoysticksLinux())
        return GLFW_FALSE;
    return !ret;
}

// Terminate the KMSDRM platform
void _glfwTerminateKMSDRM(void) {
    _glfwTerminateEGL();
    _glfwTerminateJoysticksLinux();
    if (_glfw.kmsdrm.drm.fd >= 0)
        close(_glfw.kmsdrm.drm.fd);
}

//////////////////////////////////////////////////////////////////////////
//////                       GLFW platform API                      //////
//////////////////////////////////////////////////////////////////////////

GLFWbool _glfwConnectKMSDRM(int platformID, _GLFWplatform* platform) {
    // debug_printf("kmsdrm_init.c:%d _glfwConnectKMSDRM BEGIN\n", __LINE__);
    const _GLFWplatform kmsdrm =
    {
        .platformID = GLFW_PLATFORM_KMSDRM,
        .init = _glfwInitKMSDRM,
        .terminate = _glfwTerminateKMSDRM,
        // .getCursorPos = _glfwGetCursorPosKMSDRM,
        // .setCursorPos = _glfwSetCursorPosKMSDRM,
        // .setCursorMode = _glfwSetCursorModeKMSDRM,
        // .setRawMouseMotion = _glfwSetRawMouseMotionKMSDRM,
        // .rawMouseMotionSupported = _glfwRawMouseMotionSupportedKMSDRM,
        // .createCursor = _glfwCreateCursorKMSDRM,
        // .createStandardCursor = _glfwCreateStandardCursorKMSDRM,
        // .destroyCursor = _glfwDestroyCursorKMSDRM,
        // .setCursor = _glfwSetCursorKMSDRM,
        // .getScancodeName = _glfwGetScancodeNameKMSDRM,
        // .getKeyScancode = _glfwGetKeyScancodeKMSDRM,
        // .setClipboardString = _glfwSetClipboardStringKMSDRM,
        // .getClipboardString = _glfwGetClipboardStringKMSDRM,
#if defined(GLFW_BUILD_LINUX_JOYSTICK)
        .initJoysticks = _glfwInitJoysticksLinux,
        .terminateJoysticks = _glfwTerminateJoysticksLinux,
        .pollJoystick = _glfwPollJoystickLinux,
        .getMappingName = _glfwGetMappingNameLinux,
        .updateGamepadGUID = _glfwUpdateGamepadGUIDLinux,
#else
        .initJoysticks = _glfwInitJoysticksNull,
        .terminateJoysticks = _glfwTerminateJoysticksNull,
        .pollJoystick = _glfwPollJoystickNull,
        .getMappingName = _glfwGetMappingNameNull,
        .updateGamepadGUID = _glfwUpdateGamepadGUIDNull,
#endif
        // .freeMonitor = _glfwFreeMonitorKMSDRM,
        // .getMonitorPos = _glfwGetMonitorPosKMSDRM,
        // .getMonitorContentScale = _glfwGetMonitorContentScaleKMSDRM,
        // .getMonitorWorkarea = _glfwGetMonitorWorkareaKMSDRM,
        // .getVideoModes = _glfwGetVideoModesKMSDRM,
        // .getVideoMode = _glfwGetVideoModeKMSDRM,
        // .getGammaRamp = _glfwGetGammaRampKMSDRM,
        // .setGammaRamp = _glfwSetGammaRampKMSDRM,
        .createWindow = _glfwCreateWindowKMSDRM,
        .destroyWindow = _glfwDestroyWindowKMSDRM,
        // .setWindowTitle = _glfwSetWindowTitleKMSDRM,
        // .setWindowIcon = _glfwSetWindowIconKMSDRM,
        // .getWindowPos = _glfwGetWindowPosKMSDRM,
        // .setWindowPos = _glfwSetWindowPosKMSDRM,
        // .getWindowSize = _glfwGetWindowSizeKMSDRM,
        // .setWindowSize = _glfwSetWindowSizeKMSDRM,
        // .setWindowSizeLimits = _glfwSetWindowSizeLimitsKMSDRM,
        // .setWindowAspectRatio = _glfwSetWindowAspectRatioKMSDRM,
        .getFramebufferSize = _glfwGetFramebufferSizeKMSDRM,
        // .getWindowFrameSize = _glfwGetWindowFrameSizeKMSDRM,
        // .getWindowContentScale = _glfwGetWindowContentScaleKMSDRM,
        // .iconifyWindow = _glfwIconifyWindowKMSDRM,
        // .restoreWindow = _glfwRestoreWindowKMSDRM,
        // .maximizeWindow = _glfwMaximizeWindowKMSDRM,
        // .showWindow = _glfwShowWindowKMSDRM,
        // .hideWindow = _glfwHideWindowKMSDRM,
        // .requestWindowAttention = _glfwRequestWindowAttentionKMSDRM,
        // .focusWindow = _glfwFocusWindowKMSDRM,
        // .setWindowMonitor = _glfwSetWindowMonitorKMSDRM,
        // .windowFocused = _glfwWindowFocusedKMSDRM,
        // .windowIconified = _glfwWindowIconifiedKMSDRM,
        // .windowVisible = _glfwWindowVisibleKMSDRM,
        // .windowMaximized = _glfwWindowMaximizedKMSDRM,
        // .windowHovered = _glfwWindowHoveredKMSDRM,
        // .framebufferTransparent = _glfwFramebufferTransparentKMSDRM,
        // .getWindowOpacity = _glfwGetWindowOpacityKMSDRM,
        // .setWindowResizable = _glfwSetWindowResizableKMSDRM,
        // .setWindowDecorated = _glfwSetWindowDecoratedKMSDRM,
        // .setWindowFloating = _glfwSetWindowFloatingKMSDRM,
        // .setWindowOpacity = _glfwSetWindowOpacityKMSDRM,
        // .setWindowMousePassthrough = _glfwSetWindowMousePassthroughKMSDRM,
        .pollEvents = _glfwPollEventsKMSDRM,
        // .waitEvents = _glfwWaitEventsKMSDRM,
        // .waitEventsTimeout = _glfwWaitEventsTimeoutKMSDRM,
        // .postEmptyEvent = _glfwPostEmptyEventKMSDRM,
        .getEGLPlatform = _glfwGetEGLPlatformKMSDRM,
        .getEGLNativeDisplay = _glfwGetEGLNativeDisplayKMSDRM,
        .getEGLNativeWindow = _glfwGetEGLNativeWindowKMSDRM,
        // .getRequiredInstanceExtensions = _glfwGetRequiredInstanceExtensionsKMSDRM,
        // .getPhysicalDevicePresentationSupport = _glfwGetPhysicalDevicePresentationSupportKMSDRM,
        // .createWindowSurface = _glfwCreateWindowSurfaceKMSDRM
    };

    *platform = kmsdrm;
    // debug_printf("kmsdrm_init.c:%d _glfwConnectKMSDRM END\n", __LINE__);
    return GLFW_TRUE;
}

#endif // _GLFW_KMSDRM

