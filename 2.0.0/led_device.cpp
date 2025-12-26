#define DEBUG_LOG_TAG "LEDDEV"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include "led_device.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>

#include "utils/tools.h"

#define PRIMARY_LED_PATH "/sys/class/leds/lcd-backlight/brightness"
#define PRIMARY_LED_MAX_PATH "/sys/class/leds/lcd-backlight/max_brightness"
#define PRIMARY_LED_MIN_PATH "/sys/class/leds/lcd-backlight/min_brightness"

#define CHECK_DPY_RET_BAD_DISP(dpy)                                  \
    do {                                                             \
        if (dpy >= DisplayManager::MAX_DISPLAYS) {                   \
            HWC_LOGE("%s: invalid dpy %" PRIu64, __FUNCTION__, dpy); \
            return HWC2_ERROR_BAD_DISPLAY;                           \
        }                                                            \
    } while(0)

//-----------------------------------------------------------------------------

ssize_t writeMsg(int fd, void* buf, size_t size)
{
    ssize_t res = -1;
    if (fd != -1) {
        res = write(fd, buf, size);
    }
    return res;
}

ssize_t writeInt(int fd, int val)
{
    char buf[16] = {0};
    int size = snprintf(buf, sizeof(buf), "%d", val);
    return writeMsg(fd, buf, static_cast<size_t>(size));
}

ssize_t readMsg(int fd, void* buf, size_t size)
{
    ssize_t res = -1;
    if (fd != -1) {
        res = read(fd, buf, size);
    }
    return res;
}

ssize_t readInt(int fd, int* val)
{
    char buf[16] = {0};
    ssize_t res = readMsg(fd, buf, sizeof(buf));
    if (res > 0)
    {
        *val = atoi(buf);
    }
    return res;
}

//-----------------------------------------------------------------------------

LedDevice& LedDevice::getInstance()
{
    static LedDevice gInstance;
    return gInstance;
}

LedDevice::LedDevice()
{
    memset(m_led, 0, sizeof(m_led));
    for (size_t i = 0; i < DisplayManager::MAX_DISPLAYS; i++)
    {
        m_led[i].fd = -1;
        // the defination of brightness as below.
        // max:1.0f  min:0.0f  off:-1.0f
        // we wish the first value of brightness always can be applied, so we set its default
        // value to -2.0f.
        m_led[i].brightness = -2.0f;
        m_led[i].cur_brightness = -1;
    }

    for (size_t i = 0; i < DisplayManager::MAX_DISPLAYS; i++)
    {
        switch (i)
        {
            case HWC_DISPLAY_PRIMARY:
                initLedState(i, &m_led[i], PRIMARY_LED_PATH, PRIMARY_LED_MAX_PATH,
                        PRIMARY_LED_MIN_PATH);
                break;

            default:
                //TODO initialize the backlight of other display
                break;
        }
    }
}

LedDevice::~LedDevice()
{
    for (size_t i = 0; i < DisplayManager::MAX_DISPLAYS; i++)
    {
        if (m_led[i].fd >= 0)
        {
            protectedClose(m_led[i].fd);
        }
    }
}

void LedDevice::initLedState(uint64_t dpy, DisplayLedState* state, const char* led_path,
        const char* max_path, const char* min_path)
{
    if (state == nullptr)
    {
        return;
    }

    state->is_support = false;
    if (led_path == nullptr)
    {
        HWC_LOGW("(%" PRIu64 ") led path is null", dpy);
        return;
    }

    ssize_t res = readIntFromPath(max_path, &state->max_brightness);
    if (res <= 0)
    {
        HWC_LOGW("(%" PRIu64 ") failed to read max brightness", dpy);
        return;
    }

    res = readIntFromPath(min_path, &state->min_brightness);
    if (res <= 0)
    {
        HWC_LOGW("(%" PRIu64 ") failed to read min brightness", dpy);
        return;
    }

    if (state->min_brightness >= state->max_brightness)
    {
        HWC_LOGW("(%" PRIu64 ") the max and min brightness area invalid(max=%d, min=%d)",
                dpy, state->max_brightness, state->min_brightness);
        return;
    }

    state->fd = open(led_path, O_RDWR);
    if (state->fd < 0)
    {
        HWC_LOGW("(%" PRIu64 ") failed to open led path[%s]", dpy, led_path);
        return;
    }

    HWC_LOGI("(%" PRIu64 ") led state: range[%d~%d]",
            dpy, state->min_brightness, state->max_brightness);
    state->is_support = true;
}

ssize_t LedDevice::readIntFromPath(const char* path, int* val)
{
    ssize_t size = 0;
    if (path == nullptr || val == nullptr)
    {
        HWC_LOGW("%s: invalid parameter path[%p] val[%p]", __func__, path, val);
        return size;
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        HWC_LOGW("%s: failed to open path[%s]", __func__, path);
        return size;
    }

    size = readInt(fd, val);
    if (size <= 0)
    {
        HWC_LOGW("%s: failed to read path[%s][%zd]", __func__, path, size);
    }

    protectedClose(fd);
    return size;
}

int32_t LedDevice::getBrightnessSupport(uint64_t dpy, bool* support)
{
    CHECK_DPY_RET_BAD_DISP(dpy);
    *support = m_led[dpy].is_support;
    return HWC2_ERROR_NONE;
}

int32_t LedDevice::setBrightness(uint64_t dpy, float brightness)
{
    CHECK_DPY_RET_BAD_DISP(dpy);

    if (!m_led[dpy].is_support)
    {
        HWC_LOGW("(%" PRIu64 ") %s: set brightness(%f) with unsupported display",
                dpy, __func__, brightness);
        return HWC2_ERROR_UNSUPPORTED;
    }

    if (m_led[dpy].brightness == brightness)
    {
        HWC_LOGD("(%" PRIu64 ") set brightness with the same brightness(%f)", dpy, brightness);
        return HWC2_ERROR_NONE;
    }

    // max:1.0f  min:0.0f  off:-1.0f
    int val = -1;
    if (brightness == -1.0f)
    {
        val = 0;
    }
    else if (brightness < 0.0f || brightness > 1.0f)
    {
        HWC_LOGW("(%" PRIu64 ") set brightness with invalid value(%f)", dpy, brightness);
        return HWC2_ERROR_BAD_PARAMETER;
    }
    else
    {
        val = static_cast<int>((m_led[dpy].max_brightness - m_led[dpy].min_brightness) * brightness);
        val += m_led[dpy].min_brightness;
    }

    if (val == m_led[dpy].cur_brightness)
    {
        HWC_LOGD("(%" PRIu64 ") set brightness with the same config value(%d, %f->%f)",
                dpy, val, m_led[dpy].brightness, brightness);
        return HWC2_ERROR_NONE;
    }

    ssize_t size = writeInt(m_led[dpy].fd, val);
    if (size <= 0)
    {
        HWC_LOGW("(%" PRIu64 ")failed to set brightness[%d]", dpy, val);
        return HWC2_ERROR_NO_RESOURCES;
    }
    HWC_LOGD("(%" PRIu64 ") set brightness[float:%f->%f | int:%d->%d]", dpy, m_led[dpy].brightness,
            brightness, m_led[dpy].cur_brightness, val);
    m_led[dpy].brightness = brightness;
    m_led[dpy].cur_brightness = val;

    return HWC2_ERROR_NONE;
}

void LedDevice::dump(String8* dump_str)
{
    dump_str->appendFormat("[LED state]\n");
    for (size_t i = 0; i < DisplayManager::MAX_DISPLAYS; i++)
    {
        dump_str->appendFormat("\tdisplay_%zu: support[%d]\n", i, m_led[i].is_support);
        if (m_led[i].is_support)
        {
            dump_str->appendFormat("\t             brightness[%f] range[%d~%d] current[%d]\n",
                    m_led[i].brightness, m_led[i].min_brightness, m_led[i].max_brightness,
                    m_led[i].cur_brightness);
        }
    }
    dump_str->appendFormat("\n");
}
