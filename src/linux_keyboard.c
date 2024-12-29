//========================================================================
// GLFW 3.5 Linux - www.glfw.org
//------------------------------------------------------------------------
// Copyright (c) 2002-2006 Marcus Geelnard
// Copyright (c) 2006-2017 Camilla Löwy <elmindreda@glfw.org>
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
glfwPollEvents
    _glfwPollEventsWayland
        void handleEvents(double* timeout)
            _glfwDetectJoystickConnectionLinux();
            _glfwDetectKeyboardConnectionLinux();
            _glfwPollPOSIX(struct pollfd* fds, nfds_t count, double* timeout)
    _glfwPollEventsKMSDRM
        void handleEvents(double* timeout)  // FIX src/kmsdrm_window.c
            _glfwDetectJoystickConnectionLinux();
            _glfwDetectKeyboardConnectionLinux();
            _glfwPollPOSIX(struct pollfd* fds, nfds_t count, double* timeout)
            _glfwPollKeyboardLinux(js, _GLFW_POLL_ALL);
                handleKeyEvent(js, e.code, e.value);
*/

#include "internal.h"

#if defined(GLFW_BUILD_LINUX_KEYBOARD)

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef SYN_DROPPED // < v2.6.39 kernel headers
// Workaround for CentOS-6, which is supported till 2020-11-30, but still on v2.6.32
#define SYN_DROPPED 3
#endif

#define isBitSet(bit, arr) (arr[(bit) / 8] & (1 << ((bit) % 8)))

static int translateKey(uint32_t scancode) {
    if (scancode < sizeof(_glfw.kmsdrm.keycodes) / sizeof(_glfw.kmsdrm.keycodes[0]))
        return _glfw.kmsdrm.keycodes[scancode];

    return GLFW_KEY_UNKNOWN;
}

int isKeyboardDevice(const char* devicePath, char* deviceName, size_t nameSize) {
    int fd = open(devicePath, O_RDONLY);
    if (fd < 0) {
        return 0;
    }

    struct input_id deviceInfo;
    if (ioctl(fd, EVIOCGID, &deviceInfo) < 0) {
        close(fd);
        return 0;
    }

    if (ioctl(fd, EVIOCGNAME(nameSize), deviceName) < 0) {
        strncpy(deviceName, "Unknown", nameSize - 1);
        deviceName[nameSize - 1] = '\0';
    }

    unsigned long evbit[EV_MAX / sizeof(unsigned long)];
    if (ioctl(fd, EVIOCGBIT(0, sizeof(evbit)), evbit) < 0) {
        close(fd);
        return 0;
    }

    unsigned long keybit[KEY_MAX / sizeof(unsigned long)];
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybit)), keybit) < 0) {
        close(fd);
        return 0;
    }

    close(fd);

    // Check if the device supports EV_KEY events and has keys typically found on keyboards
    return (evbit[EV_KEY / (8 * sizeof(unsigned long))] & (1 << (EV_KEY % (8 * sizeof(unsigned long))))) &&
        (keybit[KEY_A / (8 * sizeof(unsigned long))] & (1 << (KEY_A % (8 * sizeof(unsigned long)))));
}

// Apply an EV_KEY event to the specified keyboard
//
// static void handleKeyEvent(_GLFWkeyboard* js, int code, int value) {
    // _glfwInputKeyboardButton(js,        js->linjs.keyMap[code - BTN_MISC],        value ? GLFW_PRESS : GLFW_RELEASE);
//     _glfwInputKey(_glfw.kmsdrm.window, code, value ? GLFW_PRESS : GLFW_RELEASE, 0, 0);
// }

// Attempt to open the specified keyboard device
//
static GLFWbool openKeyboardDevice(const char* path) {
    char name[256] = "";

    for (int jid = 0; jid <= GLFW_KEYBOARD_LAST; jid++) {
        if (!_glfw.keyboards[jid].connected)
            continue;
        if (strcmp(_glfw.keyboards[jid].linjs.path, path) == 0)
            return GLFW_FALSE;
    }

    if (!isKeyboardDevice(path, name, sizeof(name)))
        return GLFW_FALSE;

    _GLFWkeyboardLinux linjs = { 0 };
    linjs.fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (linjs.fd == -1)
        return GLFW_FALSE;

    char evBits[(EV_CNT + 7) / 8] = { 0 };
    char keyBits[(KEY_CNT + 7) / 8] = { 0 };
    char absBits[(ABS_CNT + 7) / 8] = { 0 };
    struct input_id id;

    if (ioctl(linjs.fd, EVIOCGBIT(0, sizeof(evBits)), evBits) < 0 ||
        ioctl(linjs.fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits) < 0 ||
        ioctl(linjs.fd, EVIOCGBIT(EV_ABS, sizeof(absBits)), absBits) < 0 ||
        ioctl(linjs.fd, EVIOCGID, &id) < 0) {
        _glfwInputError(GLFW_PLATFORM_ERROR,
            "Linux: Failed to query input device: %s",
            strerror(errno));
        close(linjs.fd);
        return GLFW_FALSE;
    }

    char guid[33] = "";
    // Generate a keyboard GUID that matches the SDL 2.0.5+ one
    if (id.vendor && id.product && id.version) {
        sprintf(guid, "%02x%02x0000%02x%02x0000%02x%02x0000%02x%02x0000",
            id.bustype & 0xff, id.bustype >> 8,
            id.vendor & 0xff, id.vendor >> 8,
            id.product & 0xff, id.product >> 8,
            id.version & 0xff, id.version >> 8);
    } else {
        sprintf(guid, "%02x%02x0000%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x00",
            id.bustype & 0xff, id.bustype >> 8,
            name[0], name[1], name[2], name[3],
            name[4], name[5], name[6], name[7],
            name[8], name[9], name[10]);
    }

    int axisCount = 0, buttonCount = 0, hatCount = 0;

    for (int code = BTN_MISC; code < KEY_CNT; code++) {
        if (!isBitSet(code, keyBits))
            continue;

        linjs.keyMap[code - BTN_MISC] = buttonCount;
        buttonCount++;
    }

    for (int code = 0; code < ABS_CNT; code++) {
        linjs.absMap[code] = -1;
        if (!isBitSet(code, absBits))
            continue;

        if (code >= ABS_HAT0X && code <= ABS_HAT3Y) {
            linjs.absMap[code] = hatCount;
            hatCount++;
            // Skip the Y axis
            code++;
        } else {
            if (ioctl(linjs.fd, EVIOCGABS(code), &linjs.absInfo[code]) < 0)
                continue;

            linjs.absMap[code] = axisCount;
            axisCount++;
        }
    }

    _GLFWkeyboard* js = _glfwAllocKeyboard(name, guid, axisCount, buttonCount, hatCount);
    if (!js) {
        close(linjs.fd);
        return GLFW_FALSE;
    }

    strncpy(linjs.path, path, sizeof(linjs.path) - 1);
    memcpy(&js->linjs, &linjs, sizeof(linjs));

    debug_printf("openKeyboardDevice: %s \"%s\" [OK]\n", path, name);
    js->connected = GLFW_TRUE;
    return GLFW_TRUE;
}

// Frees all resources associated with the specified keyboard
//
static void closeKeyboard(_GLFWkeyboard* js) {
    js->connected = GLFW_FALSE;
    close(js->linjs.fd);
    _glfwFreeKeyboard(js);
}

// Lexically compare keyboards by name; used by qsort
//
static int compareKeyboards(const void* fp, const void* sp) {
    const _GLFWkeyboard* fj = fp;
    const _GLFWkeyboard* sj = sp;
    return strcmp(fj->linjs.path, sj->linjs.path);
}


//////////////////////////////////////////////////////////////////////////
//////                       GLFW internal API                      //////
//////////////////////////////////////////////////////////////////////////

void _glfwDetectKeyboardConnectionLinux(void) {
    if (_glfw.linjs.inotify <= 0)
        return;

    ssize_t offset = 0;
    char buffer[16384];
    const ssize_t size = read(_glfw.linjs.inotify, buffer, sizeof(buffer));

    while (size > offset) {
        regmatch_t match;
        const struct inotify_event* e = (struct inotify_event*) (buffer + offset);

        offset += sizeof(struct inotify_event) + e->len;

        if (regexec(&_glfw.linjs.regex, e->name, 1, &match, 0) != 0)
            continue;

        char path[PATH_MAX];
        snprintf(path, sizeof(path), "/dev/input/%s", e->name);

        if (e->mask & (IN_CREATE | IN_ATTRIB))
            openKeyboardDevice(path);
        else if (e->mask & IN_DELETE) {
            for (int jid = 0; jid <= GLFW_KEYBOARD_LAST; jid++) {
                if (strcmp(_glfw.keyboards[jid].linjs.path, path) == 0) {
                    closeKeyboard(_glfw.keyboards + jid);
                    break;
                }
            }
        }
    }
}


//////////////////////////////////////////////////////////////////////////
//////                       GLFW platform API                      //////
//////////////////////////////////////////////////////////////////////////

GLFWbool _glfwInitKeyboardsLinux(void) {
    const char* dirname = "/dev/input";

    _glfw.linjs.inotify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (_glfw.linjs.inotify > 0) {
        // HACK: Register for IN_ATTRIB to get notified when udev is done
        //       This works well in practice but the true way is libudev

        _glfw.linjs.watch = inotify_add_watch(_glfw.linjs.inotify,
            dirname,
            IN_CREATE | IN_ATTRIB | IN_DELETE);
    }

    // Continue without device connection notifications if inotify fails

    _glfw.linjs.regexCompiled = (regcomp(&_glfw.linjs.regex, "^event[0-9]\\+$", 0) == 0);
    if (!_glfw.linjs.regexCompiled) {
        _glfwInputError(GLFW_PLATFORM_ERROR, "Linux: Failed to compile regex");
        return GLFW_FALSE;
    }

    int count = 0;

    DIR* dir = opendir(dirname);
    if (dir) {
        struct dirent* entry;

        while ((entry = readdir(dir))) {
            regmatch_t match;

            if (regexec(&_glfw.linjs.regex, entry->d_name, 1, &match, 0) != 0)
                continue;

            char path[PATH_MAX];

            snprintf(path, sizeof(path), "%s/%s", dirname, entry->d_name);

            if (openKeyboardDevice(path))
                count++;
        }

        closedir(dir);
    }

    // Continue with no keyboards if enumeration fails

    qsort(_glfw.keyboards, count, sizeof(_GLFWkeyboard), compareKeyboards);
    return GLFW_TRUE;
}

void _glfwTerminateKeyboardsLinux(void) {
    for (int jid = 0; jid <= GLFW_KEYBOARD_LAST; jid++) {
        _GLFWkeyboard* js = _glfw.keyboards + jid;
        if (js->connected)
            closeKeyboard(js);
    }

    if (_glfw.linjs.inotify > 0) {
        if (_glfw.linjs.watch > 0)
            inotify_rm_watch(_glfw.linjs.inotify, _glfw.linjs.watch);

        close(_glfw.linjs.inotify);
    }

    if (_glfw.linjs.regexCompiled)
        regfree(&_glfw.linjs.regex);
}

GLFWbool _glfwPollKeyboardLinux(_GLFWkeyboard* js, int mode) {
    // Read all queued events (non-blocking)
    for (;;) {
        struct input_event e;
        errno = 0;
        if (read(js->linjs.fd, &e, sizeof(e)) < 0) {
            // Reset the keyboard slot if the device was disconnected
            if (errno == ENODEV)
                closeKeyboard(js);
            break;
        }
        debug_printf("PollKeyboardLinux: %d %d %d\n", e.type, e.code, e.value);
        if (e.type == EV_KEY) {
            // handleKeyEvent(js, e.code, e.value);
            // _glfwInputKey(_glfw.kmsdrm.window, 0, e.code, e.value , 0);
            _glfwInputKey(_glfw.kmsdrm.window, translateKey(e.code), e.code, e.value ? GLFW_PRESS : GLFW_RELEASE, 0);
        }

    }
    return js->connected;
}

#endif // GLFW_BUILD_LINUX_KEYBOARD

