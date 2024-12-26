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

#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
// #include <limits.h>
// #include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// #include <sys/mman.h>
// #include <sys/timerfd.h>
#include <unistd.h>
// #include <time.h>
// #include <assert.h>
#include <inttypes.h>

// int64_t get_time_ns(void) {
//     struct timespec tv;
//     clock_gettime(CLOCK_MONOTONIC, &tv);
//     return tv.tv_nsec + tv.tv_sec * NSEC_PER_SEC;
// }

// int create_program(const char* vs_src, const char* fs_src) {
//     GLuint vertex_shader, fragment_shader, program;
//     GLint ret;

//     vertex_shader = glCreateShader(GL_VERTEX_SHADER);
//     if (vertex_shader == 0) {
//         printf("vertex shader creation failed!:\n");
//         return -1;
//     }

//     glShaderSource(vertex_shader, 1, &vs_src, NULL);
//     glCompileShader(vertex_shader);

//     glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &ret);
//     if (!ret) {
//         char* log;

//         printf("vertex shader compilation failed!:\n");
//         glGetShaderiv(vertex_shader, GL_INFO_LOG_LENGTH, &ret);
//         if (ret > 1) {
//             log = malloc(ret);
//             glGetShaderInfoLog(vertex_shader, ret, NULL, log);
//             printf("%s", log);
//             free(log);
//         }

//         return -1;
//     }

//     fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
//     if (fragment_shader == 0) {
//         printf("fragment shader creation failed!:\n");
//         return -1;
//     }
//     glShaderSource(fragment_shader, 1, &fs_src, NULL);
//     glCompileShader(fragment_shader);

//     glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &ret);
//     if (!ret) {
//         char* log;

//         printf("fragment shader compilation failed!:\n");
//         glGetShaderiv(fragment_shader, GL_INFO_LOG_LENGTH, &ret);

//         if (ret > 1) {
//             log = malloc(ret);
//             glGetShaderInfoLog(fragment_shader, ret, NULL, log);
//             printf("%s", log);
//             free(log);
//         }
//         return -1;
//     }

//     program = glCreateProgram();
//     glAttachShader(program, vertex_shader);
//     glAttachShader(program, fragment_shader);
//     return program;
// }

// int link_program(unsigned program) {
//     GLint ret;

//     glLinkProgram(program);
//     glGetProgramiv(program, GL_LINK_STATUS, &ret);
//     if (!ret) {
//         char* log;

//         printf("program linking failed!:\n");
//         glGetProgramiv(program, GL_INFO_LOG_LENGTH, &ret);

//         if (ret > 1) {
//             log = malloc(ret);
//             glGetProgramInfoLog(program, ret, NULL, log);
//             printf("%s", log);
//             free(log);
//         }
//         return -1;
//     }
//     return 0;
// }

static int get_resources(int fd, drmModeRes** resources) {
    puts("drmModeGetResources");
    *resources = drmModeGetResources(fd);
    if (*resources == NULL)
        return -1;
    return 0;
}

static int find_drm_device(drmModeRes** resources) {
    drmDevicePtr devices[MAX_DRM_DEVICES] = { NULL };
    int num_devices, fd = -1;

    puts("drmGetDevices2");
    num_devices = drmGetDevices2(0, devices, MAX_DRM_DEVICES);
    if (num_devices < 0) {
        printf("drmGetDevices2 failed: %s\n", strerror(-num_devices));
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
        printf("Opening DRM device %s\n", device->nodes[DRM_NODE_PRIMARY]);
        fd = open(device->nodes[DRM_NODE_PRIMARY], O_RDWR);
        if (fd < 0)
            continue;
        ret = get_resources(fd, resources);
        if (!ret)
            break;
        close(fd);
        fd = -1;
    }
    puts("drmFreeDevices");
    drmFreeDevices(devices, num_devices);

    if (fd < 0)
        printf("no drm device found!\n");
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
        puts("drmModeGetEncoder");
        drmModeEncoder* encoder = drmModeGetEncoder(drm->fd, encoder_id);

        if (encoder) {
            const int32_t crtc_id = find_crtc_for_encoder(resources, encoder);
            puts("drmModeFreeEncoder");
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

    printf("drmModeConnector connector_id=%d count_connectors=%d\n", connector_id, resources->count_connectors);
    if (connector_id >= 0) {
        if (connector_id >= resources->count_connectors)
            return NULL;
        puts("drmModeGetConnector");
        connector = drmModeGetConnector(fd, resources->connectors[connector_id]);
        if (connector && connector->connection == DRM_MODE_CONNECTED)
            return connector;
        puts("drmModeFreeConnector");
        drmModeFreeConnector(connector);
        return NULL;
    }

    for (i = 0; i < resources->count_connectors; i++) {
        puts("drmModeGetConnector");
        connector = drmModeGetConnector(fd, resources->connectors[i]);
        if (connector && connector->connection == DRM_MODE_CONNECTED) {
            /* it's connected, let's use this! */
            break;
        }
        puts("drmModeFreeConnector");
        drmModeFreeConnector(connector);
        connector = NULL;
    }

    return connector;
}

int init_drm(struct drm* drm, const char* device, const char* mode_str,
    int connector_id, unsigned int vrefresh, unsigned int count, bool nonblocking) {
    drmModeRes* resources;
    drmModeConnector* connector = NULL;
    drmModeEncoder* encoder = NULL;
    int i, ret, area;

    if (device) {
        drm->fd = open(device, O_RDWR);
        ret = get_resources(drm->fd, &resources);
        if (ret < 0 && errno == EOPNOTSUPP)
            printf("%s does not look like a modeset device\n", device);
    } else {
        drm->fd = find_drm_device(&resources);
    }

    if (drm->fd < 0) {
        printf("could not open drm device\n");
        return -1;
    }

    if (!resources) {
        printf("drmModeGetResources failed: %s\n", strerror(errno));
        return -1;
    }

    /* find a connected connector: */
    connector = find_drm_connector(drm->fd, resources, connector_id);
    if (!connector) {
        /* we could be fancy and listen for hotplug events and wait for
         * a connector..
         */
        printf("no connected connector!\n");
        return -1;
    }

    /* find user requested mode: */
    printf("[init_drm] find user requested mode=%p\n", drm->mode);
    if (mode_str && *mode_str) {
        for (i = 0; i < connector->count_modes; i++) {
            drmModeModeInfo* current_mode = &connector->modes[i];

            if (strcmp(current_mode->name, mode_str) == 0) {
                if (vrefresh == 0 || current_mode->vrefresh == vrefresh) {
                    drm->mode = current_mode;
                    printf("[init_drm] Found requested mode for [%s]\n", mode_str);
                    break;
                }
            }
        }
        if (!drm->mode)
            printf("[init_drm] requested mode not found, using default mode!\n");
    }

    /* find preferred mode or the highest resolution mode: */
    if (!drm->mode) {
        for (i = 0, area = 0; i < connector->count_modes; i++) {
            drmModeModeInfo* current_mode = &connector->modes[i];

            if (current_mode->type & DRM_MODE_TYPE_PREFERRED) {
                drm->mode = current_mode;
                printf("[init_drm] Using requested resolution mode=%p\n", drm->mode);
                break;
            }

            int current_area = current_mode->hdisplay * current_mode->vdisplay;
            if (current_area > area) {
                drm->mode = current_mode;
                printf("[init_drm] Using highest resolution mode=%p\n", drm->mode);
                area = current_area;
            }
        }
    }

    if (!drm->mode) {
        printf("could not find mode!\n");
        return -1;
    }

    /* find encoder: */
    for (i = 0; i < resources->count_encoders; i++) {
        puts("drmModeGetEncoder");
        encoder = drmModeGetEncoder(drm->fd, resources->encoders[i]);
        if (encoder->encoder_id == connector->encoder_id)
            break;
        drmModeFreeEncoder(encoder);
        encoder = NULL;
    }

    if (encoder) {
        drm->crtc_id = encoder->crtc_id;
    } else {
        int32_t crtc_id = find_crtc_for_connector(drm, resources, connector);
        if (crtc_id == -1) {
            printf("no crtc found!\n");
            return -1;
        }
        drm->crtc_id = crtc_id;
    }

    for (i = 0; i < resources->count_crtcs; i++) {
        if (resources->crtcs[i] == drm->crtc_id) {
            drm->crtc_index = i;
            break;
        }
    }
    puts("drmModeFreeResources");
    drmModeFreeResources(resources);
    drm->connector_id = connector->connector_id;
    drm->count = count;
    drm->nonblocking = nonblocking;
    return 0;
}

// static bool has_ext(const char* extension_list, const char* ext) {
//     const char* ptr = extension_list;
//     int len = strlen(ext);

//     if (ptr == NULL || *ptr == '\0')
//         return false;

//     while (true) {
//         ptr = strstr(ptr, ext);
//         if (!ptr)
//             return false;

//         if (ptr[len] == ' ' || ptr[len] == '\0')
//             return true;

//         ptr += len;
//     }
// }

// static int match_config_to_visual(EGLDisplay egl_display, EGLint visual_id, EGLConfig* configs, int count) {
//     int i;

//     for (i = 0; i < count; ++i) {
//         EGLint id;

//         if (!eglGetConfigAttrib(egl_display, configs[i], EGL_NATIVE_VISUAL_ID, &id))
//             continue;

//         if (id == visual_id)
//             return i;
//     }
//     puts("common.c:init_egl:match_config_to_visual: Visual ID NOT FOUND");
//     return -1;
// }

// static bool egl_choose_config(EGLDisplay egl_display, const EGLint* attribs,
//     EGLint visual_id, EGLConfig* config_out) {
//     EGLint count = 0;
//     EGLint matched = 0;
//     EGLConfig* configs;
//     int config_index = -1;

//     if (!eglGetConfigs(egl_display, NULL, 0, &count) || count < 1) {
//         printf("No EGL configs to choose from.\n");
//         return false;
//     }
//     configs = malloc(count * sizeof * configs);
//     if (!configs)
//         return false;

//     if (!eglChooseConfig(egl_display, attribs, configs,
//         count, &matched) || !matched) {
//         printf("No EGL configs with appropriate attributes.\n");
//         goto out;
//     }
//     if (!visual_id) {
//         config_index = 0;
//         printf("Use first[0] matched EGL configs\n");
//     }
//     if (config_index == -1)
//         config_index = match_config_to_visual(egl_display, visual_id, configs, matched);
//     if (config_index != -1) {
//         *config_out = configs[config_index];
//         // printf("common.c:init_egl:egl_choose_config: Use index [%d] matched EGL configs\n", config_index);
//     }

// out:
//     free(configs);
//     if (config_index == -1)
//         return false;

//     return true;
// }

// int init_egl(struct egl* egl, const struct gbm* gbm, int samples) {
//     EGLint major, minor;

//     static const EGLint context_attribs[] = {
//         EGL_CONTEXT_CLIENT_VERSION, 2,
//         EGL_NONE
//     };

// #ifdef DRM_FORMAT_USE_NO_TRANSPARENCY
//     const EGLint config_attribs[] = {
//         EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
//         EGL_RED_SIZE, 8,
//         EGL_GREEN_SIZE, 8,
//         EGL_BLUE_SIZE, 8,
//         EGL_ALPHA_SIZE, 0,
//         EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
//         EGL_SAMPLES, samples,
//         EGL_NONE
//     };
// #else
//     const EGLint config_attribs[] = {
//         EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
//         EGL_RED_SIZE, 8,
//         EGL_GREEN_SIZE, 8,
//         EGL_BLUE_SIZE, 8,
//         EGL_ALPHA_SIZE, 8,               // Alpha component size (if needed)
//         EGL_DEPTH_SIZE, 24,              // Depth buffer size
//         EGL_STENCIL_SIZE, 8,             // Stencil buffer size
//         EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
//         EGL_SAMPLES, samples,
//         EGL_NONE
//     };
// #endif

//     const char* egl_exts_client, * egl_exts_dpy, * gl_exts;

/*#define get_proc_client(ext, name) do { \
        if (has_ext(egl_exts_client, #ext)) { \
            egl->name = (void *)eglGetProcAddress(#name); \
        } \
        } while (0)
#define get_proc_dpy(ext, name) do { \
        if (has_ext(egl_exts_dpy, #ext)) { \
            egl->name = (void*) eglGetProcAddress(#name); \
        } \
        } while (0)

#define get_proc_gl(ext, name) do { \
        if (has_ext(gl_exts, #ext)) {\
            egl->name = (void*) eglGetProcAddress(#name); \
        } \
        } while (0)
*/
//     egl_exts_client = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
//     get_proc_client(EGL_EXT_platform_base, eglGetPlatformDisplayEXT);

//     if (egl->eglGetPlatformDisplayEXT) {
//         egl->display = egl->eglGetPlatformDisplayEXT(EGL_PLATFORM_GBM_KHR,
//             gbm->dev, NULL);
//     } else {
//         egl->display = eglGetDisplay((EGLNativeDisplayType) gbm->dev);
//     }

//     if (!eglInitialize(egl->display, &major, &minor)) {
//         printf("failed to initialize\n");
//         return -1;
//     }
//     egl_exts_dpy = eglQueryString(egl->display, EGL_EXTENSIONS);
//     egl->modifiers_supported = has_ext(egl_exts_dpy,
//         "EGL_EXT_image_dma_buf_import_modifiers");

//     // printf("Using EGL Library version %d.%d\n", major, minor);

//     printf("\n===================================\n");
//     printf("EGL information:\n");
//     printf("  version: %s\n", eglQueryString(egl->display, EGL_VERSION));
//     printf("  vendor: %s\n", eglQueryString(egl->display, EGL_VENDOR));
//     // printf("  client extensions: \"%s\"\n", egl_exts_client);
//     // printf("  display extensions: \"%s\"\n", egl_exts_dpy);
//     // printf("===================================\n");

//     if (!eglBindAPI(EGL_OPENGL_ES_API)) {
//         printf("failed to bind api EGL_OPENGL_ES_API\n");
//         return -1;
//     }
//     if (!egl_choose_config(egl->display, config_attribs, gbm->format,
//         &egl->config)) {
//         printf("failed to choose config\n");
//         return -1;
//     }
//     egl->context = eglCreateContext(egl->display, egl->config, EGL_NO_CONTEXT, context_attribs);
//     if (egl->context == EGL_NO_CONTEXT) {
//         printf("failed to create context\n");
//         return -1;
//     }
//     if (!gbm->surface) {
//         egl->surface = EGL_NO_SURFACE;
//     } else {
//         egl->surface = eglCreateWindowSurface(egl->display, egl->config,
//             (EGLNativeWindowType) gbm->surface, NULL);
//         if (egl->surface == EGL_NO_SURFACE) {
//             printf("failed to create egl surface\n");
//             return -1;
//         }
//     }
//     /* connect the context to the surface */
//     eglMakeCurrent(egl->display, egl->surface, egl->surface, egl->context);
//     gl_exts = (char*) glGetString(GL_EXTENSIONS);
//     printf("OpenGL ES information:\n");
//     printf("  version: %s\n", glGetString(GL_VERSION));
//     printf("  shading language version: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
//     printf("  vendor: %s\n", glGetString(GL_VENDOR));
//     printf("  renderer: %s\n", glGetString(GL_RENDERER));
//     // printf("  extensions: \"%s\"\n", gl_exts);
//     printf("===================================\n");
//     EGLint red_size, green_size, blue_size, alpha_size, depth_size, stencil_size, surface_type, render_type;
//     eglGetConfigAttrib(egl->display, egl->config, EGL_RED_SIZE, &red_size);
//     eglGetConfigAttrib(egl->display, egl->config, EGL_GREEN_SIZE, &green_size);
//     eglGetConfigAttrib(egl->display, egl->config, EGL_BLUE_SIZE, &blue_size);
//     eglGetConfigAttrib(egl->display, egl->config, EGL_ALPHA_SIZE, &alpha_size);
//     eglGetConfigAttrib(egl->display, egl->config, EGL_DEPTH_SIZE, &depth_size);
//     eglGetConfigAttrib(egl->display, egl->config, EGL_STENCIL_SIZE, &stencil_size);
//     eglGetConfigAttrib(egl->display, egl->config, EGL_SURFACE_TYPE, &surface_type);
//     eglGetConfigAttrib(egl->display, egl->config, EGL_RENDERABLE_TYPE, &render_type);
//     printf("Chosen Config R:%d G:%d B:%d A:%d Depth:%d Stencil:%d Surface=0x%08X Render=0x%08X\n", red_size, green_size, blue_size, alpha_size, depth_size, stencil_size, surface_type, render_type);
//     return 0;
// }

static void drm_fb_destroy_callback(struct gbm_bo* bo, void* data) {
    int drm_fd = gbm_device_get_fd(gbm_bo_get_device(bo));
    struct drm_fb* fb = data;
    if (fb->fb_id) {
        drmModeRmFB(drm_fd, fb->fb_id);
    }
    free(fb);
    puts("Cleaning up [OK]");
}

struct drm_fb* drm_fb_get_from_bo(struct gbm_bo* bo) {
    puts("drm_fb_get_from_bo: gbm_device_get_fd");
    int drm_fd = gbm_device_get_fd(gbm_bo_get_device(bo));
    puts("drm_fb_get_from_bo: gbm_bo_get_user_data");
    struct drm_fb* fb = gbm_bo_get_user_data(bo);
    uint32_t width, height, format,
        strides[4] = { 0 }, handles[4] = { 0 },
        offsets[4] = { 0 }, flags = 0;
    int ret = -1;

    if (fb)
        return fb;

    fb = calloc(1, sizeof * fb);
    fb->bo = bo;

    puts("drm_fb_get_from_bo: gbm_bo_get_width gbm_bo_get_height gbm_bo_get_format");
    width = gbm_bo_get_width(bo);
    height = gbm_bo_get_height(bo);
    format = gbm_bo_get_format(bo);

    // if (gbm_bo_get_handle_for_plane && gbm_bo_get_modifier &&
    //     gbm_bo_get_plane_count && gbm_bo_get_stride_for_plane &&
    //     gbm_bo_get_offset) {

    uint64_t modifiers[4] = { 0 };
    puts("drm_fb_get_from_bo: gbm_bo_get_modifier gbm_bo_get_plane_count");
    modifiers[0] = gbm_bo_get_modifier(bo);
    const int num_planes = gbm_bo_get_plane_count(bo);
    for (int i = 0; i < num_planes; i++) {
        puts("drm_fb_get_from_bo: gbm_bo_get_handle_for_plane gbm_bo_get_stride_for_plane gbm_bo_get_offset");
        handles[i] = gbm_bo_get_handle_for_plane(bo, i).u32;
        strides[i] = gbm_bo_get_stride_for_plane(bo, i);
        offsets[i] = gbm_bo_get_offset(bo, i);
        modifiers[i] = modifiers[0];
    }

    if (modifiers[0] && modifiers[0] != DRM_FORMAT_MOD_INVALID) {
        flags = DRM_MODE_FB_MODIFIERS;
    }

    printf("drm_fb_get_from_bo: drmModeAddFB2WithModifiers drm_fd=%d width=%d height=%d format=%d num_planes=%d fb_id=%d flags=%d\n", drm_fd, width, height, format, num_planes, fb->fb_id, flags);
    ret = drmModeAddFB2WithModifiers(drm_fd, width, height, format, handles, strides, offsets, modifiers, &fb->fb_id, flags);
    // }

    if (ret) {
        if (flags)
            printf("Modifiers failed!\n");

        memcpy(handles, (uint32_t[4]) { gbm_bo_get_handle(bo).u32, 0, 0, 0 }, 16);
        memcpy(strides, (uint32_t[4]) { gbm_bo_get_stride(bo), 0, 0, 0 }, 16);
        memset(offsets, 0, 16);
        puts("drm_fb_get_from_bo: drmModeAddFB2");
        ret = drmModeAddFB2(drm_fd, width, height, format, handles, strides, offsets, &fb->fb_id, 0);
    }

    if (ret) {
        printf("failed to create fb: %s\n", strerror(errno));
        free(fb);
        return NULL;
    }
    puts("drm_fb_get_from_bo: gbm_bo_set_user_data");
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

    GLFWbool ret = init_drm(&(_glfw.kmsdrm.drm), _glfw.kmsdrm.device, _glfw.kmsdrm.mode_str, _glfw.kmsdrm.connector_id, _glfw.kmsdrm.vrefresh, _glfw.kmsdrm.count, _glfw.kmsdrm.nonblocking);
    if (ret) {
        printf("init_drm fail\n");
    } else {
        printf("init_drm ok\n");
    }

    TODO:
    //createKeyTables();
    //if (!_glfwInitJoysticksLinux())
    //    return GLFW_FALSE;
    //_glfwInitTimerPOSIX();
    return !ret;
}

// Terminate the KMSDRM platform
void _glfwTerminateKMSDRM(void) {
    //_glfwTerminateEGL();
    //_glfwTerminateJoysticksLinux();
    // printf("kmsdrm_init.c:%d _glfwTerminateKMSDRM BEGIN\n", __LINE__);
    if (_glfw.kmsdrm.drm.fd >= 0)
        close(_glfw.kmsdrm.drm.fd);
    // printf("kmsdrm_init.c:%d _glfwTerminateKMSDRM END\n", __LINE__);
}

//////////////////////////////////////////////////////////////////////////
//////                       GLFW platform API                      //////
//////////////////////////////////////////////////////////////////////////

GLFWbool _glfwConnectKMSDRM(int platformID, _GLFWplatform* platform) {
    // printf("kmsdrm_init.c:%d _glfwConnectKMSDRM BEGIN\n", __LINE__);
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
    // printf("kmsdrm_init.c:%d _glfwConnectKMSDRM END\n", __LINE__);
    return GLFW_TRUE;
}

#endif // _GLFW_KMSDRM

