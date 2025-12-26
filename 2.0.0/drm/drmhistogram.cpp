#define DEBUG_LOG_TAG "DrmHistogramDevice"
#include "drmhistogram.h"

#include <xf86drm.h>
#include <sys/ioctl.h>
#include <system/graphics-base.h>
#include <cutils/bitops.h>

#include "utils/debug.h"
#include "platform_wrap.h"

#define MTK_DRM_HISTOGRAM_DEVICE "/dev/dri/card0"

#ifdef USE_SWWATCHDOG
#include "utils/swwatchdog.h"

#define DRM_WDT_IOCTL(CMD, ...)                                                             \
({                                                                                          \
    int err = 0;                                                                            \
    if (Platform::getInstance().m_config.wdt_trace)                                         \
    {                                                                                       \
        ATRACE_NAME(#CMD);                                                                  \
        SWWatchDog::AutoWDT _wdt("[DEV] ioctl(" #CMD "):" STRINGIZE(__LINE__), 500);        \
        err = m_drm->ioctl(CMD, ##__VA_ARGS__);                                             \
    }                                                                                       \
    else                                                                                    \
    {                                                                                       \
        err = m_drm->ioctl(CMD, ##__VA_ARGS__);                                             \
    }                                                                                       \
    err;                                                                                    \
})

#else // USE_SWWATCHDOG

#define DRM_WDT_IOCTL(CMD, ...)                                                             \
({                                                                                          \
    int err = 0;                                                                            \
    if (Platform::getInstance().m_config.wdt_trace)                                         \
    {                                                                                       \
        ATRACE_NAME(#CMD);                                                                  \
        err = m_drm->ioctl(CMD, ##__VA_ARGS__);                                             \
    }                                                                                       \
    else                                                                                    \
    {                                                                                       \
        err = m_drm->ioctl(CMD, ##__VA_ARGS__);                                             \
    }                                                                                       \
    err;                                                                                    \
})

#endif // USE_SWWATCHDOG

#define DRM_IOCTL(CMD, ...)                                                                \
({                                                                                         \
    int err = 0;                                                                           \
    ATRACE_NAME(#CMD);                                                                     \
    err = ioctl(m_fd, CMD, ##__VA_ARGS__);                                                 \
    err;                                                                                   \
})

//=============================================================================

DrmHistogramDevice::DrmHistogramDevice()
    : m_drm(nullptr)
    , m_hw_state(HISTOGRAM_STATE_NO_SUPPORT)
    , m_format_bit(0)
    , m_format(HAL_PIXEL_FORMAT_HSV_888)
    , m_dataspace(0)
    , m_mask(0)
    , m_max_bin(0)
    , m_max_channel(0)
    , m_enable(false)
    , m_collected_mask(0)
    , m_channel_number(0)
    , m_bin_number(0)
    , m_total_bin_number(0)
{
}

DrmHistogramDevice::~DrmHistogramDevice()
{
}

void DrmHistogramDevice::initState(DrmModeResource* drm, mtk_drm_disp_caps_info& caps_info)
{
    if (drm == nullptr)
    {
        HWC_LOGW("%s: DrmModeResource is null. Stop initalize DrmHistogramDevice.", __func__);
        return;
    }
    m_drm = drm;

    m_format_bit = caps_info.color_format;
    m_dataspace = caps_info.lcm_color_mode;
    m_max_bin = caps_info.max_bin;
    m_max_channel = caps_info.max_channel;
    m_mask = calculateColorMask(m_format, m_format_bit);

    // check hw state last, otherwise it may get a wrong state
    m_hw_state = checkHwState();
}

uint8_t DrmHistogramDevice::calculateColorMask(int32_t format, uint32_t bit)
{
    uint8_t mask = 0;
    uint32_t available_channel = m_max_channel;
    switch (format)
    {
        case HAL_PIXEL_FORMAT_HSV_888:
            // AOSP want to use value channel, so it has a high priority
            if ((bit & MTK_DRM_COLOR_FORMAT_M_BIT ) && (available_channel > 0))
            {
                mask |= HWC2_FORMAT_COMPONENT_2;
                available_channel--;
            }
            if ((bit & MTK_DRM_COLOR_FORMAT_H_BIT ) && (available_channel > 0))
            {
                mask |= HWC2_FORMAT_COMPONENT_0;
                available_channel--;
            }
            if ((bit & MTK_DRM_COLOR_FORMAT_S_BIT) && (available_channel > 0))
            {
                mask |= HWC2_FORMAT_COMPONENT_1;
                available_channel--;
            }
            break;

        default:
            HWC_LOGW("%s: unknown format: %d", __func__, format);
            break;
    }

    return mask;
}

int32_t DrmHistogramDevice::checkHwState()
{
    if (m_mask != 0)
    {
        return HISTOGRAM_STATE_SUPPORT;
    }

    if ((m_format_bit != 0) && (m_mask == 0))
    {
        return HISTOGRAM_STATE_NO_RESOURCE;
    }

    return HISTOGRAM_STATE_NO_SUPPORT;
}

int32_t DrmHistogramDevice::getDeviceState()
{
    return m_hw_state;
}

bool DrmHistogramDevice::isHwSupport()
{
    return getDeviceState() == HISTOGRAM_STATE_SUPPORT;
}

int32_t DrmHistogramDevice::getAttribute(int32_t* color_format, int32_t* dataspace,
        uint8_t* mask, uint32_t* max_bin)
{
    int32_t state = getDeviceState();
    if (state != HISTOGRAM_STATE_SUPPORT)
    {
        return INVALID_OPERATION;
    }

    *color_format = m_format;
    *dataspace = m_dataspace;
    *mask = m_mask;
    *max_bin = m_max_bin;

    return NO_ERROR;
}

int32_t DrmHistogramDevice::enableHistogram(const bool enable, const int32_t format,
        const uint8_t format_mask, const int32_t dataspace, const uint32_t bin_count)
{
    int32_t state = getDeviceState();
    if (state != HISTOGRAM_STATE_SUPPORT)
    {
        return -ENODEV;
    }

    drm_mtk_chist_config chist_config;
    memset(&chist_config, 0, sizeof(chist_config));
    chist_config.lcm_color_mode = static_cast<unsigned int>(dataspace);
    chist_config.caller = MTK_DRM_CHIST_CALLER_HWC;

    if (enable && m_enable != enable)
    {
        m_enable = enable;
        m_collected_mask = format_mask;
        m_bin_number = bin_count;
        m_channel_number = static_cast<uint8_t>(popcount(format_mask));
        m_total_bin_number = static_cast<uint64_t>(m_channel_number) * m_bin_number;

        chist_config.config_channel_count = m_channel_number;
        for (uint8_t i = 0; i < m_channel_number; i++)
        {
            struct drm_mtk_channel_config* channel_config = &chist_config.chist_config[i];
            channel_config->enabled = true;
            channel_config->bin_count = bin_count;
            MTK_DRM_CHIST_COLOR_FORMT driver_format = getNthHistogramFormat(format, format_mask, i);
            if (driver_format == MTK_DRM_COLOR_FORMAT_MAX)
            {
                return -EINVAL;
            }
            channel_config->color_format = driver_format;
        }
        int32_t err = DRM_WDT_IOCTL(DRM_IOCTL_MTK_SET_CHIST_CONFIG, &chist_config);
        if (err < 0)
        {
            HWC_LOGE("DRM_IOCTL_MTK_SET_CHIST_CONFIG err:%d (e=%d m=%u)", err, enable, format_mask);
            m_enable = false;
            m_collected_mask = 0;
            m_channel_number = 0;
            m_bin_number = 0;
            return err;
        }
    }
    else if (!enable && m_enable != enable)
    {
        chist_config.config_channel_count = m_channel_number;
        for (uint8_t i = 0; i < m_channel_number; i++)
        {
            struct drm_mtk_channel_config* channel_config = &chist_config.chist_config[i];
            channel_config->enabled = false;
        }
        int32_t err = DRM_WDT_IOCTL(DRM_IOCTL_MTK_SET_CHIST_CONFIG, &chist_config);
        if (err < 0)
        {
            HWC_LOGE("DRM_IOCTL_MTK_SET_CHIST_CONFIG err:%d (e=%d m=%u)", err, enable, format_mask);
            return err;
        }

        m_enable = enable;
        m_collected_mask = 0;
        m_channel_number = 0;
        m_bin_number = 0;
    }

    return NO_ERROR;
}

void DrmHistogramDevice::getFormatOrder(int32_t format, uint8_t order[NUM_FORMAT_COMPONENTS])
{
    switch (format)
    {
        case HAL_PIXEL_FORMAT_HSV_888:
            // the order of driver is SHV
            order[0] = 1; // S is HWC2_FORMAT_COMPONENT_1
            order[1] = 0; // H is HWC2_FORMAT_COMPONENT_0
            order[2] = 2; // V is HWC2_FORMAT_COMPONENT_2
            order[3] = 0xff;
            break;

        default:
            HWC_LOGW("%s: unknown format: %d", __func__, format);
            break;
    }
}

void DrmHistogramDevice::getFormatBit(int32_t format, uint32_t bit[NUM_FORMAT_COMPONENTS])
{
    switch (format)
    {
        case HAL_PIXEL_FORMAT_HSV_888:
            bit[0] = MTK_DRM_COLOR_FORMAT_H_BIT ;
            bit[1] = MTK_DRM_COLOR_FORMAT_S_BIT ;
            bit[2] = MTK_DRM_COLOR_FORMAT_M_BIT ;
            bit[3] = 0x00;
            break;

        default:
            HWC_LOGW("%s: unknown format: %d", __func__, format);
            break;
    }
}

MTK_DRM_CHIST_COLOR_FORMT DrmHistogramDevice::getNthHistogramFormat(int32_t format, uint8_t format_mask, uint8_t n)
{
    MTK_DRM_CHIST_COLOR_FORMT histogram_format = MTK_DRM_COLOR_FORMAT_MAX;
    MTK_DRM_CHIST_COLOR_FORMT format_table[NUM_FORMAT_COMPONENTS];
    switch (format)
    {
        case HAL_PIXEL_FORMAT_HSV_888:
            format_table[0] = MTK_DRM_COLOR_FORMAT_H;
            format_table[1] = MTK_DRM_COLOR_FORMAT_S;
            format_table[2] = MTK_DRM_COLOR_FORMAT_M;
            format_table[3] = MTK_DRM_COLOR_FORMAT_MAX;
            break;

        default:
            HWC_LOGW("%s: unknown format: %d", __func__, format);
            return histogram_format;
    }

    uint8_t count = 0;
    for (uint8_t i = 0; i < NUM_FORMAT_COMPONENTS; i++)
    {
        uint8_t mask = format_mask >> i;
        if (mask & 0x01)
        {
            count++;
        }
        if (count > n)
        {
            histogram_format = format_table[i];
            break;
        }
    }
    if (count <= n)
    {
        HWC_LOGW("%s: failed to get the histogram fomat(%d) for %dth", __func__, format, n);
    }

    return histogram_format;
}

uint32_t DrmHistogramDevice::getFormatOrder(int32_t format, MTK_DRM_CHIST_COLOR_FORMT histogram_format)
{
    uint32_t order = NUM_FORMAT_COMPONENTS;
    switch (format)
    {
        case HAL_PIXEL_FORMAT_HSV_888:
            if (histogram_format == MTK_DRM_COLOR_FORMAT_H)
            {
                order = 0;
            }
            else if (histogram_format == MTK_DRM_COLOR_FORMAT_S)
            {
                order = 1;
            }
            else if (histogram_format == MTK_DRM_COLOR_FORMAT_M)
            {
                order = 2;
            }
            else
            {
                HWC_LOGW("%s: invalid format: %d", __func__, histogram_format);
            }
            break;
        default:
            HWC_LOGW("%s: unknown format: %d", __func__, format);
            break;
    }

    return order;
}

int32_t DrmHistogramDevice::collectHistogram(uint32_t* fence_index, uint32_t* histogram_ptr[NUM_FORMAT_COMPONENTS])
{
    if (!m_enable)
    {
        return -EPERM;
    }

    if (fence_index == nullptr)
    {
        return -EINVAL;
    }

    if (histogram_ptr == nullptr)
    {
        return -EINVAL;
    }

    drm_mtk_chist_info chist_info;
    memset(&chist_info, 0, sizeof(chist_info));
    chist_info.caller = MTK_DRM_CHIST_CALLER_HWC;
    chist_info.get_channel_count = m_channel_number;
    for (uint8_t i = 0; i < m_channel_number; i++)
    {
        drm_mtk_channel_hist* channel_hist = &chist_info.channel_hist[i];
        channel_hist->bin_count = m_bin_number;
        MTK_DRM_CHIST_COLOR_FORMT driver_format = getNthHistogramFormat(m_format,
                m_collected_mask, i);
        if (driver_format == MTK_DRM_COLOR_FORMAT_MAX)
        {
            return -EINVAL;
        }
        channel_hist->color_format = driver_format;
    }
    int err = DRM_WDT_IOCTL(DRM_IOCTL_MTK_GET_CHIST, &chist_info);
    if (err < 0)
    {
        HWC_LOGE("DRM_IOCTL_MTK_GET_CHIST err=%d", err);
        return err;
    }

    *fence_index = static_cast<uint32_t>(chist_info.present_fence);
    for (uint8_t i = 0; i < m_channel_number; i++)
    {
        drm_mtk_channel_hist* channel_hist = &chist_info.channel_hist[i];
        uint32_t order = getFormatOrder(m_format, channel_hist->color_format);
        if (order < NUM_FORMAT_COMPONENTS && histogram_ptr[order] != nullptr)
        {
            memcpy(histogram_ptr[order], channel_hist->hist,
                    channel_hist->bin_count * sizeof(*channel_hist->hist));
        }
    }

    return 0;
}
