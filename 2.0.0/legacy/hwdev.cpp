#define DEBUG_LOG_TAG "DEV"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include "utils/tools.h"
#include <cutils/properties.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>

#include <fcntl.h>
#include <sys/ioctl.h>

#include <linux/fb.h>
#include <linux/mtkfb.h>

#include "ddp_ovl.h"

#include "utils/debug.h"

#include <hwc_feature_list.h>

#include <utils/Trace.h>
#include "hwdev.h"
#include "display.h"
#include "overlay.h"
#include "platform_wrap.h"
#include "hwc2.h"
#include "dispatcher.h"
#include "sync.h"
#ifdef USE_SWWATCHDOG
#include "utils/swwatchdog.h"

#define WDT_IOCTL(fd, CMD, ...)                                                             \
({                                                                                          \
    int err = 0;                                                                            \
    if (Platform::getInstance().m_config.wdt_trace)                                         \
    {                                                                                       \
        ATRACE_NAME(#CMD);                                                                  \
        SWWatchDog::AutoWDT _wdt("[DEV] ioctl(" #CMD "):" STRINGIZE(__LINE__), 500);        \
        err = ioctl(fd, CMD, ##__VA_ARGS__);                                                \
    }                                                                                       \
    else                                                                                    \
    {                                                                                       \
        SWWatchDog::AutoWDT _wdt("[DEV] ioctl(" #CMD "):" STRINGIZE(__LINE__), 500);        \
        err = ioctl(fd, CMD, ##__VA_ARGS__);                                                \
    }                                                                                       \
    err;                                                                                    \
})
#else // USE_SWWATCHDOG
#define WDT_IOCTL(fd, CMD, ...)                                                             \
({                                                                                          \
    int err = 0;                                                                            \
    if (Platform::getInstance().m_config.wdt_trace)                                         \
    {                                                                                       \
        ATRACE_NAME(#CMD);                                                                  \
        err = ioctl(fd, CMD, ##__VA_ARGS__);                                                \
    }                                                                                       \
    else                                                                                    \
    {                                                                                       \
        err = ioctl(fd, CMD, ##__VA_ARGS__);                                                \
    }                                                                                       \
    err;                                                                                    \
})
#endif // USE_SWWATCHDOG

#define HWC_ATRACE_BUFFER_INFO(string, n1, n2, n3, n4)                                     \
    if (ATRACE_ENABLED()) {                                                                \
        char ___traceBuf[1024];                                                            \
        int ret = snprintf(___traceBuf, sizeof(___traceBuf), "%s(%" PRIu64 ":%u): %u %d",  \
                           (string), (n1), (n2), (n3), (n4));                              \
        if (ret >= 0 && ret < static_cast<int>(sizeof(___traceBuf))) {                     \
            android::ScopedTrace ___bufTracer(ATRACE_TAG, ___traceBuf);                    \
        }                                                                                  \
    }

// ---------------------------------------------------------------------------

#define DLOGD(i, x, ...) HWC_LOGD("(%" PRIu64 ") " x " id:%x", i, ##__VA_ARGS__, m_frame_cfg[i].session_id)
#define DLOGI(i, x, ...) HWC_LOGI("(%" PRIu64 ") " x " id:%x", i, ##__VA_ARGS__, m_frame_cfg[i].session_id)
#define DLOGW(i, x, ...) HWC_LOGW("(%" PRIu64 ") " x " id:%x", i, ##__VA_ARGS__, m_frame_cfg[i].session_id)
#define DLOGE(i, x, ...) HWC_LOGE("(%" PRIu64 ") " x " id:%x", i, ##__VA_ARGS__, m_frame_cfg[i].session_id)

#define IOLOGE(i, err, x, ...) HWC_LOGE("(%" PRIu64 ") " x " id:%x err:%d, %s", i, ##__VA_ARGS__, m_frame_cfg[i].session_id, err, strerror(err))

DISP_SESSION_TYPE g_session_type[DisplayManager::MAX_DISPLAYS] = {
    DISP_SESSION_PRIMARY,
    DISP_SESSION_EXTERNAL,
    DISP_SESSION_MEMORY
};

enum {
    EXT_DEVICE_MHL = 1,
    EXT_DEVICE_EPD = 2,
    EXT_DEVICE_LCM = 3,
};

DISP_MODE mapHwcDispMode2Disp(HWC_DISP_MODE mode)
{
    switch (mode)
    {
        case HWC_DISP_INVALID_SESSION_MODE:
            return DISP_INVALID_SESSION_MODE;

        case HWC_DISP_SESSION_DIRECT_LINK_MODE:
            return DISP_SESSION_DIRECT_LINK_MODE;

        case HWC_DISP_SESSION_DECOUPLE_MODE:
            return DISP_SESSION_DECOUPLE_MODE;

        case HWC_DISP_SESSION_DIRECT_LINK_MIRROR_MODE:
            return DISP_SESSION_DIRECT_LINK_MIRROR_MODE;

        case HWC_DISP_SESSION_DECOUPLE_MIRROR_MODE:
            return DISP_SESSION_DECOUPLE_MIRROR_MODE;

        case HWC_DISP_SESSION_RDMA_MODE:
            return DISP_SESSION_RDMA_MODE;

        case HWC_DISP_SESSION_DUAL_DIRECT_LINK_MODE:
            return DISP_SESSION_DUAL_DIRECT_LINK_MODE;

        case HWC_DISP_SESSION_DUAL_DECOUPLE_MODE:
            return DISP_SESSION_DUAL_DECOUPLE_MODE;

        case HWC_DISP_SESSION_DUAL_RDMA_MODE:
            return DISP_SESSION_DUAL_RDMA_MODE;

        case HWC_DISP_SESSION_TRIPLE_DIRECT_LINK_MODE:
            return DISP_SESSION_TRIPLE_DIRECT_LINK_MODE;

        default:
            HWC_LOGW("failed to map HWC_DISP_MODE: %d", mode);
            return DISP_INVALID_SESSION_MODE;
    }
}

static HWC_DISP_MODE mapDispMode2Hwc(DISP_MODE mode)
{
    switch (mode)
    {
        case DISP_INVALID_SESSION_MODE:
            return HWC_DISP_INVALID_SESSION_MODE;

        case DISP_SESSION_DIRECT_LINK_MODE:
            return HWC_DISP_SESSION_DIRECT_LINK_MODE;

        case DISP_SESSION_DECOUPLE_MODE:
            return HWC_DISP_SESSION_DECOUPLE_MODE;

        case DISP_SESSION_DIRECT_LINK_MIRROR_MODE:
            return HWC_DISP_SESSION_DIRECT_LINK_MIRROR_MODE;

        case DISP_SESSION_DECOUPLE_MIRROR_MODE:
            return HWC_DISP_SESSION_DECOUPLE_MIRROR_MODE;

        case DISP_SESSION_RDMA_MODE:
            return HWC_DISP_SESSION_RDMA_MODE;

        case DISP_SESSION_DUAL_DIRECT_LINK_MODE:
            return HWC_DISP_SESSION_DUAL_DIRECT_LINK_MODE;

        case DISP_SESSION_DUAL_DECOUPLE_MODE:
            return HWC_DISP_SESSION_DUAL_DECOUPLE_MODE;

        case DISP_SESSION_DUAL_RDMA_MODE:
            return HWC_DISP_SESSION_DUAL_RDMA_MODE;

        case DISP_SESSION_TRIPLE_DIRECT_LINK_MODE:
            return HWC_DISP_SESSION_TRIPLE_DIRECT_LINK_MODE;

        default:
            HWC_LOGW("failed to map DISP_MODE: %d", mode);
            return HWC_DISP_INVALID_SESSION_MODE;
    }
}

static HWC_DISP_IF_TYPE mapDispIfType2Hwc(DISP_IF_TYPE type)
{
    switch (type)
    {
        case DISP_IF_TYPE_DBI:
            return HWC_DISP_IF_TYPE_DBI;

        case DISP_IF_TYPE_DPI:
            return HWC_DISP_IF_TYPE_DPI;

        case DISP_IF_TYPE_DSI0:
            return HWC_DISP_IF_TYPE_DSI0;

        case DISP_IF_TYPE_DSI1:
            return HWC_DISP_IF_TYPE_DSI1;

        case DISP_IF_TYPE_DSIDUAL:
            return HWC_DISP_IF_TYPE_DSIDUAL;

        case DISP_IF_HDMI:
            return HWC_DISP_IF_HDMI;

        case DISP_IF_MHL:
            return HWC_DISP_IF_MHL;

        case DISP_IF_EPD:
            return HWC_DISP_IF_EPD;

        case DISP_IF_SLIMPORT:
            return HWC_DISP_IF_SLIMPORT;

        default:
            HWC_LOGW("failed to map DISP_IF_TYPE: %d", type);
            return HWC_DISP_IF_TYPE_DBI;
    }
}

static HWC_DISP_IF_MODE mapDispIfMode2Hwc(DISP_IF_MODE mode)
{
    switch (mode)
    {
        case DISP_IF_MODE_VIDEO:
            return HWC_DISP_IF_MODE_VIDEO;

        case DISP_IF_MODE_COMMAND:
            return HWC_DISP_IF_MODE_COMMAND;

        default:
            HWC_LOGW("failed to map DISP_IF_MODE: %d", mode);
            return HWC_DISP_IF_MODE_VIDEO;
    }
}

static HWC_SELF_REFRESH_TYPE mapDispSelfRefreshType2Hwc(DISP_SELF_REFRESH_TYPE type)
{
    switch (type)
    {
        case WAIT_FOR_REFRESH:
            return HWC_WAIT_FOR_REFRESH;

        case REFRESH_FOR_ANTI_LATENCY2:
            return HWC_REFRESH_FOR_ANTI_LATENCY2;

        case REFRESH_FOR_SWITCH_DECOUPLE:
            return HWC_REFRESH_FOR_SWITCH_DECOUPLE;

        case REFRESH_FOR_SWITCH_DECOUPLE_MIRROR:
            return HWC_REFRESH_FOR_SWITCH_DECOUPLE_MIRROR;

        case REFRESH_FOR_IDLE:
            return HWC_REFRESH_FOR_IDLE;

        default:
            HWC_LOGW("failed to map DISP_SELF_REFRESH_TYPE: %d", type);
            return HWC_WAIT_FOR_REFRESH;
    }
}

static void covertDispSessionInfo2SessionInfo(SessionInfo *dst, disp_session_info *src)
{
    dst->maxLayerNum = src->maxLayerNum;
    dst->isHwVsyncAvailable = src->isHwVsyncAvailable;
    dst->displayType = mapDispIfType2Hwc(src->displayType);
    dst->displayWidth = src->displayWidth;
    dst->displayHeight = src->displayHeight;
    dst->displayFormat = src->displayFormat;
    dst->displayMode = mapDispIfMode2Hwc(src->displayMode);
    dst->vsyncFPS = src->vsyncFPS;
    dst->physicalWidth = src->physicalWidth;
    dst->physicalHeight = src->physicalHeight;
    dst->physicalWidthUm = src->physicalWidthUm;
    dst->physicalHeightUm = src->physicalHeightUm;
    dst->density = src->density;
    dst->isConnected = src->isConnected;
    dst->isHDCPSupported = src->isHDCPSupported;
}

static DISP_FORMAT mapDispOutFormat(unsigned int format)
{
    switch (format)
    {
        case HAL_PIXEL_FORMAT_RGBA_8888:
            return DISP_FORMAT_RGBA8888;

        case HAL_PIXEL_FORMAT_YV12:
            return DISP_FORMAT_YV12;

        case HAL_PIXEL_FORMAT_RGBX_8888:
            return DISP_FORMAT_RGBX8888;

        case HAL_PIXEL_FORMAT_BGRA_8888:
            return DISP_FORMAT_BGRA8888;

        case HAL_PIXEL_FORMAT_BGRX_8888:
        case HAL_PIXEL_FORMAT_IMG1_BGRX_8888:
            return DISP_FORMAT_BGRX8888;

        case HAL_PIXEL_FORMAT_RGB_888:
            return DISP_FORMAT_RGB888;

        case HAL_PIXEL_FORMAT_BGR_888:
            return DISP_FORMAT_BGR888;

        case HAL_PIXEL_FORMAT_RGB_565:
            return DISP_FORMAT_RGB565;

        case HAL_PIXEL_FORMAT_I420:
        case HAL_PIXEL_FORMAT_NV12_BLK:
        case HAL_PIXEL_FORMAT_NV12_BLK_FCM:
        case HAL_PIXEL_FORMAT_YUV_PRIVATE:
        case HAL_PIXEL_FORMAT_YUYV:
        case HAL_PIXEL_FORMAT_YCbCr_420_888:
        case HAL_PIXEL_FORMAT_YCbCr_422_I:
            return DISP_FORMAT_YUV422;

        case HAL_PIXEL_FORMAT_RGBA_1010102:
            return DISP_FORMAT_RGBA1010102;

        case HAL_PIXEL_FORMAT_RGBA_FP16:
            return DISP_FORMAT_RGBA_FP16;
    }
    HWC_LOGW("Not support output format(%d), use default RGBA8888", format);
    return DISP_FORMAT_ABGR8888;
}

static DISP_YUV_RANGE_ENUM mapDispColorRangefromDS(int ds)
{
    switch (ds & HAL_DATASPACE_STANDARD_MASK)
    {
        case HAL_DATASPACE_STANDARD_BT601_625:
        case HAL_DATASPACE_STANDARD_BT601_625_UNADJUSTED:
        case HAL_DATASPACE_STANDARD_BT601_525:
        case HAL_DATASPACE_STANDARD_BT601_525_UNADJUSTED:
            return ((ds & HAL_DATASPACE_RANGE_MASK) == HAL_DATASPACE_RANGE_FULL) ?
                DISP_YUV_BT601_FULL : DISP_YUV_BT601;

        case HAL_DATASPACE_STANDARD_BT709:
        case HAL_DATASPACE_STANDARD_DCI_P3:
        case HAL_DATASPACE_STANDARD_BT2020:
            return DISP_YUV_BT709;
    }

    return DISP_YUV_BT601;
}

static uint32_t mapHwcFeatureFlag(uint32_t flag)
{
    uint32_t tmp = 0;

    if (flag & HWC_FEATURE_TIME_SHARING)
    {
        tmp |= DISP_FEATURE_TIME_SHARING;
    }
    if (flag & HWC_FEATURE_HRT)
    {
        tmp |= DISP_FEATURE_HRT;
    }
    if (flag & HWC_FEATURE_PARTIAL)
    {
        tmp |= DISP_FEATURE_PARTIAL;
    }
    if (flag & HWC_FEATURE_FENCE_WAIT)
    {
        tmp |= DISP_FEATURE_FENCE_WAIT;
    }
    if (flag & HWC_FEATURE_NO_PARGB)
    {
        tmp |= DISP_FEATURE_NO_PARGB;
    }
    if (flag & HWC_FEATURE_DISP_SELF_REFRESH)
    {
        tmp |= DISP_FEATURE_DISP_SELF_REFRESH;
    }
    if (flag & HWC_FEATURE_RPO)
    {
        tmp |= DISP_FEATURE_RPO;
    }
    if (flag & HWC_FEATURE_FBDC)
    {
        tmp |= DISP_FEATURE_FBDC;
    }
    if (flag & HWC_FEATURE_FORCE_DISABLE_AOD)
    {
        tmp |= DISP_FEATURE_FORCE_DISABLE_AOD;
    }

    return tmp;
}

// ---------------------------------------------------------------------------

DISP_FORMAT mapDispInFormat(unsigned int format, int mode)
{
    if (!DispDevice::getInstance().isConstantAlphaForRGBASupported())
    {
        mode = HWC2_BLEND_MODE_NONE;
    }

    switch (format)
    {
        case HAL_PIXEL_FORMAT_RGBA_8888:
            return (HWC2_BLEND_MODE_PREMULTIPLIED == mode) ? DISP_FORMAT_PRGBA8888 : DISP_FORMAT_RGBA8888;

        case HAL_PIXEL_FORMAT_RGBX_8888:
            return DISP_FORMAT_RGBX8888;

        case HAL_PIXEL_FORMAT_BGRA_8888:
            return (HWC2_BLEND_MODE_PREMULTIPLIED == mode) ? DISP_FORMAT_PBGRA8888 : DISP_FORMAT_BGRA8888;

        case HAL_PIXEL_FORMAT_BGRX_8888:
        case HAL_PIXEL_FORMAT_IMG1_BGRX_8888:
            return DISP_FORMAT_BGRX8888;

        case HAL_PIXEL_FORMAT_RGB_888:
            return DISP_FORMAT_RGB888;

        case HAL_PIXEL_FORMAT_BGR_888:
            return DISP_FORMAT_BGR888;

        case HAL_PIXEL_FORMAT_RGB_565:
            return DISP_FORMAT_RGB565;

        case HAL_PIXEL_FORMAT_I420:
        case HAL_PIXEL_FORMAT_YV12:
        case HAL_PIXEL_FORMAT_NV12_BLK:
        case HAL_PIXEL_FORMAT_NV12_BLK_FCM:
        case HAL_PIXEL_FORMAT_YUV_PRIVATE:
        case HAL_PIXEL_FORMAT_YCbCr_422_I:
        case HAL_PIXEL_FORMAT_YUYV:
        case HAL_PIXEL_FORMAT_UFO:
        case HAL_PIXEL_FORMAT_UFO_AUO:
        case HAL_PIXEL_FORMAT_YCbCr_420_888:
        case HAL_PIXEL_FORMAT_YUV_PRIVATE_10BIT:
        case HAL_PIXEL_FORMAT_NV12_BLK_10BIT_H:
        case HAL_PIXEL_FORMAT_NV12_BLK_10BIT_H_JUMP:
        case HAL_PIXEL_FORMAT_NV12_BLK_10BIT_V:
        case HAL_PIXEL_FORMAT_NV12_BLK_10BIT_V_JUMP:
        case HAL_PIXEL_FORMAT_UFO_10BIT_H:
        case HAL_PIXEL_FORMAT_UFO_10BIT_H_JUMP:
        case HAL_PIXEL_FORMAT_UFO_10BIT_V:
        case HAL_PIXEL_FORMAT_UFO_10BIT_V_JUMP:
        case HAL_PIXEL_FORMAT_YCRCB_420_SP:
        case HAL_PIXEL_FORMAT_NV12:
            return DISP_FORMAT_YUV422;

        case HAL_PIXEL_FORMAT_RGBA_1010102:
            return (HWC2_BLEND_MODE_PREMULTIPLIED == mode) ? DISP_FORMAT_PRGBA1010102 : DISP_FORMAT_RGBA1010102;

        case HAL_PIXEL_FORMAT_RGBA_FP16:
            return (HWC2_BLEND_MODE_PREMULTIPLIED == mode) ? DISP_FORMAT_PRGBA_FP16 : DISP_FORMAT_RGBA_FP16;

        case HAL_PIXEL_FORMAT_DIM:
            return DISP_FORMAT_DIM;
    }
    HWC_LOGW("Not support input format(%d), use default RGBA8888", format);
    return (HWC2_BLEND_MODE_PREMULTIPLIED == mode) ? DISP_FORMAT_PABGR8888 : DISP_FORMAT_ABGR8888;
}

DISP_FORMAT convertFormat4Hrt(unsigned int format)
{
    DISP_FORMAT hrt_format = mapDispInFormat(format);

    // when Platform does not support 2-subsampled format with odd size of roi, we
    // should not use YUYV. If the roi size of YUYV buffer is odd, ovl shows abnormal
    // screen content. Therefore we change the format to RGB888.
    if (Platform::getInstance().m_config.support_2subsample_with_odd_size_roi == false &&
        hrt_format == DISP_FORMAT_YUV422)
    {
        hrt_format = DISP_FORMAT_RGB888;
    }
    return hrt_format;
}

DispDevice& DispDevice::getInstance()
{
    static DispDevice gInstance;
    return gInstance;
}

DispDevice::DispDevice()
{
    std::string filename("/dev/");
    filename += DISP_SESSION_DEVICE;
    m_dev_fd = open(filename.c_str(), O_RDONLY);
    if (m_dev_fd <= 0)
    {
        HWC_LOGE("Failed to open display device: %s ", strerror(errno));
        abort();
    }

    // query hw capibilities and set to m_caps_info
    if (NO_ERROR != queryCapsInfo())
    {
        abort();
    }

    m_ovl_input_num = getMaxOverlayInputNum();

    memset(m_frame_cfg, 0, sizeof(disp_frame_cfg_t) * DisplayManager::MAX_DISPLAYS);

    for (int i = 0; i < DisplayManager::MAX_DISPLAYS; i++)
    {
        m_frame_cfg[i].session_id = DISP_INVALID_SESSION;
        m_frame_cfg[i].mode = DISP_INVALID_SESSION_MODE;

        // partial update - allocate dirty rect structures for ioctl to disp driver
        m_hwdev_dirty_rect[i] = (layer_dirty_roi**)malloc(sizeof(layer_dirty_roi*) * m_ovl_input_num);
        LOG_ALWAYS_FATAL_IF(m_hwdev_dirty_rect[i] == nullptr, "DispDevice() malloc(%zu) fail",
            sizeof(layer_dirty_roi*) * m_ovl_input_num);
        for (unsigned int j = 0; j < m_ovl_input_num; j++)
        {
            m_hwdev_dirty_rect[i][j] = (layer_dirty_roi*)malloc(sizeof(layer_dirty_roi) * MAX_DIRTY_RECT_CNT);
            LOG_ALWAYS_FATAL_IF(m_hwdev_dirty_rect[i][j] == nullptr, "DispDevice() malloc(%zu) fail",
                sizeof(layer_dirty_roi) * MAX_DIRTY_RECT_CNT);
        }
    }

    memset(m_layer_config_list, 0, sizeof(layer_config*) * DisplayManager::MAX_DISPLAYS);
    memset(m_input_config, 0, sizeof(disp_session_input_config) * DisplayManager::MAX_DISPLAYS);
    memset(m_output_config, 0, sizeof(disp_session_output_config) * DisplayManager::MAX_DISPLAYS);

    // get device multi configs info
    memset(m_multi_cfgs, 0, sizeof(struct multi_configs) * DisplayManager::MAX_DISPLAYS);
    if (isMultiConfigsSupport())
    {
        getOverlayMultiConfigs(HWC_DISPLAY_PRIMARY);
    }
}

DispDevice::~DispDevice()
{
    // partial update - free dirty rect structures
    for (int i = 0; i < DisplayManager::MAX_DISPLAYS; i++)
    {
        for (unsigned int j = 0; j < m_ovl_input_num; j++)
        {
            free(m_hwdev_dirty_rect[i][j]);
        }
        free(m_hwdev_dirty_rect[i]);
        if (NULL != m_layer_config_list[i])
        {
            free(m_layer_config_list[i]);
        }
    }

    protectedClose(m_dev_fd);
}

void DispDevice::initOverlay()
{
    Platform::getInstance().initOverlay();
}

unsigned int DispDevice::getHwVersion()
{
    HWC_LOGE("%s does not support this function", __FUNCTION__);
    return 0;
}

status_t DispDevice::queryCapsInfo()
{
    memset(&m_caps_info, 0, sizeof(disp_caps_info));

    // query device Capabilities
    int err = WDT_IOCTL(m_dev_fd, DISP_IOCTL_GET_DISPLAY_CAPS, &m_caps_info);
    if (err < 0)
    {
        IOLOGE(uint64_t(0), err, "DISP_IOCTL_GET_DISPLAY_CAPS");
        return err;
    }

    HWC_LOGD("CapsInfo [%d] lcm_degree",                 m_caps_info.lcm_degree);
    HWC_LOGD("CapsInfo [%d] output_rotated",             m_caps_info.is_output_rotated);
    HWC_LOGD("CapsInfo [%d] lcm_color_mode",             m_caps_info.lcm_color_mode);
    HWC_LOGD("CapsInfo [%d] disp_feature",               m_caps_info.disp_feature);
    HWC_LOGD("CapsInfo [%d] support_frame_cfg_ioctl",    m_caps_info.is_support_frame_cfg_ioctl);
    HWC_LOGD("CapsInfo [%d,%d,%d] rpo,rsz_in_max(w,h)",
        m_caps_info.disp_feature & DISP_FEATURE_RPO,
        m_caps_info.rsz_in_max[0], m_caps_info.rsz_in_max[1]);
    HWC_LOGD("CapsInfo [%d]dispRszSupported",            isDispRszSupported());
    HWC_LOGD("CapsInfo [%d]fbdc",                        m_caps_info.disp_feature & DISP_FEATURE_FBDC);
    HWC_LOGD("CapsInfo [%d]multiConfigs",                m_caps_info.disp_feature & DISP_FEATURE_DYNFPS);
    // print supported resolutions
    if (isDispRszSupported())
    {
        for (uint32_t i = 0; i < m_caps_info.rsz_list_length; ++i)
        {
            HWC_LOGD(" (%d, %d, %d)",
                m_caps_info.rsz_in_res_list[i][0],
                m_caps_info.rsz_in_res_list[i][1],
                m_caps_info.rsz_in_res_list[i][2]);
        }
    }

    const bool isSelfRefreshSupported = isDispSelfRefreshSupported();
    HWC_LOGD("CapsInfo [%d]isDispSelfRefreshSupported", isSelfRefreshSupported);

    return NO_ERROR;
}

bool DispDevice::isDispRszSupported()
{
    return (0 != (m_caps_info.disp_feature & DISP_FEATURE_RSZ));
}

bool DispDevice::isDispRpoSupported()
{
    return m_caps_info.disp_feature & DISP_FEATURE_RPO;
}

bool DispDevice::isDispAodForceDisable()
{
    return m_caps_info.disp_feature & DISP_FEATURE_FORCE_DISABLE_AOD;
}

bool DispDevice::isDisp3X4DisplayColorTransformSupported()
{
    return false;
}

bool DispDevice::isPartialUpdateSupported()
{
    if (Platform::getInstance().m_config.force_full_invalidate)
    {
        HWC_LOGW("!!!! force full invalidate !!!!");
        return false;
    }

    return (0 != (m_caps_info.disp_feature & DISP_FEATURE_PARTIAL));
}

bool DispDevice::isFenceWaitSupported()
{
    if (Platform::getInstance().m_config.wait_fence_for_display)
    {
        HWC_LOGW("!!!! force hwc wait fence for display !!!!");
        return false;
    }

    return m_caps_info.disp_feature & DISP_FEATURE_FENCE_WAIT;
}

bool DispDevice::isConstantAlphaForRGBASupported()
{
    return  (0 == (m_caps_info.disp_feature & DISP_FEATURE_NO_PARGB));
}

bool DispDevice::isDispSelfRefreshSupported()
{
    return m_caps_info.disp_feature & DISP_FEATURE_DISP_SELF_REFRESH;
}

bool DispDevice::isDisplayHrtSupport()
{
    return m_caps_info.disp_feature & DISP_FEATURE_HRT;
}

bool DispDevice::isDisplaySupportedWidthAndHeight(unsigned int /*width*/, unsigned int /*height*/)
{
    return true;
}

int32_t DispDevice::getSupportedColorMode()
{
    return m_caps_info.lcm_color_mode;
}

unsigned int DispDevice::getMaxOverlayInputNum()
{
    // TODO: deinfe in ddp_ovl.h now.
    // This's header file for kernel, not for userspace. Need to refine later
    return OVL_LAYER_NUM;
}

uint32_t DispDevice::getMaxOverlayHeight()
{
    // deinfe in ddp_ovl.h now
    return OVL_MAX_HEIGHT;
}

uint32_t DispDevice::getMaxOverlayWidth()
{
    // deinfe in ddp_ovl.h.
    return OVL_MAX_WIDTH;
}

int32_t DispDevice::getDisplayOutputRotated()
{
    return m_caps_info.is_output_rotated;
}

uint32_t DispDevice::getRszMaxWidthInput()
{
    return m_caps_info.rsz_in_max[0];
}

uint32_t DispDevice::getRszMaxHeightInput()
{
    return m_caps_info.rsz_in_max[1];
}

void DispDevice::enableDisplayFeature(uint32_t flag)
{
    m_caps_info.disp_feature |= mapHwcFeatureFlag(flag);
}

void DispDevice::disableDisplayFeature(uint32_t flag)
{
    m_caps_info.disp_feature &= ~mapHwcFeatureFlag(flag);
}

status_t DispDevice::createOverlaySession(uint64_t dpy, uint32_t /*drm_id_crtc*/, uint32_t /*width*/, uint32_t /*height*/,
                                          HWC_DISP_MODE mode)
{
    CHECK_DPY_RET_STATUS(dpy);

    unsigned int session_id = m_frame_cfg[dpy].session_id;
    if (DISP_INVALID_SESSION != session_id)
    {
        HWC_LOGW("(%" PRIu64 ") Failed to create existed DispSession (id=0x%x)", dpy, session_id);
        return INVALID_OPERATION;
    }

    disp_session_config config;
    memset(&config, 0, sizeof(disp_session_config));

    config.type       = g_session_type[dpy];
    config.device_id  = getDeviceId(dpy);
    config.mode       = mapHwcDispMode2Disp(mode);
    config.session_id = DISP_INVALID_SESSION;

    int err = WDT_IOCTL(m_dev_fd, DISP_IOCTL_CREATE_SESSION, &config);
    if (err < 0)
    {
        m_frame_cfg[dpy].session_id = DISP_INVALID_SESSION;
        m_frame_cfg[dpy].mode = DISP_INVALID_SESSION_MODE;

        IOLOGE(dpy, err, "DISP_IOCTL_CREATE_SESSION (%s)", getSessionModeString(mode).string());
        return BAD_VALUE;
    }

    m_frame_cfg[dpy].session_id = config.session_id;
    m_frame_cfg[dpy].mode = config.mode;

    DLOGD(dpy, "Create Session (%s)", getSessionModeString(mode).string());

    return NO_ERROR;
}

void DispDevice::destroyOverlaySession(uint64_t dpy, uint32_t /*drm_id_crtc*/)
{
    CHECK_DPY_RET_VOID(dpy);

    unsigned int session_id = m_frame_cfg[dpy].session_id;
    if (DISP_INVALID_SESSION == session_id)
    {
        HWC_LOGW("(%" PRIu64 ") Failed to destroy invalid DispSession", dpy);
        return;
    }

    disp_session_config config;
    memset(&config, 0, sizeof(disp_session_config));

    config.type       = g_session_type[dpy];
    config.device_id  = getDeviceId(dpy);
    config.session_id = session_id;

    int err = WDT_IOCTL(m_dev_fd, DISP_IOCTL_DESTROY_SESSION, &config);
    if (err < 0)
    {
        IOLOGE(dpy, err, "DISP_IOCTL_DESTROY_SESSION");
    }

    m_frame_cfg[dpy].session_id = DISP_INVALID_SESSION;
    m_frame_cfg[dpy].mode = DISP_INVALID_SESSION_MODE;

    DLOGD(dpy, "Destroy DispSession");
}

status_t DispDevice::legacySetInputBuffer(uint64_t dpy)
{
    CHECK_DPY_RET_STATUS(dpy);

    if (m_caps_info.is_support_frame_cfg_ioctl)
        return NO_ERROR;

    memset(&m_input_config[dpy], 0, sizeof(disp_session_input_config));

    m_input_config[dpy].session_id = m_frame_cfg[dpy].session_id;
    m_input_config[dpy].config_layer_num = m_frame_cfg[dpy].input_layer_num;
    size_t size = m_input_config[dpy].config_layer_num * sizeof(disp_input_config);
    memcpy(m_input_config[dpy].config, m_frame_cfg[dpy].input_cfg, size);
    memcpy(&m_input_config[dpy].ccorr_config, &m_frame_cfg[dpy].ccorr_config, sizeof(m_frame_cfg[dpy].ccorr_config));

    int err = WDT_IOCTL(m_dev_fd, DISP_IOCTL_SET_INPUT_BUFFER, &m_input_config[dpy]);
    if (err < 0)
    {
        IOLOGE(dpy, err, "DISP_IOCTL_SET_INPUT_BUFFER");
    }

    return err;
}

status_t DispDevice::legacySetOutputBuffer(uint64_t dpy)
{
    CHECK_DPY_RET_STATUS(dpy);

    if (m_caps_info.is_support_frame_cfg_ioctl)
        return NO_ERROR;

    memset(&m_output_config[dpy], 0, sizeof(disp_session_output_config));

    m_output_config[dpy].session_id = m_frame_cfg[dpy].session_id;
    memcpy(&m_output_config[dpy].config, &m_frame_cfg[dpy].output_cfg, sizeof(disp_output_config));

    int err = WDT_IOCTL(m_dev_fd, DISP_IOCTL_SET_OUTPUT_BUFFER, &m_output_config[dpy]);
    if (err < 0)
    {
        IOLOGE(dpy, err, "DISP_IOCTL_SET_OUTPUT_BUFFER");
    }

    return err;
}

status_t DispDevice::legacyTriggerSession(uint64_t dpy, int pf_idx)
{
    CHECK_DPY_RET_STATUS(dpy);

    if (m_caps_info.is_support_frame_cfg_ioctl)
        return NO_ERROR;

    disp_session_config config;
    memset(&config, 0, sizeof(disp_session_config));

    config.type       = g_session_type[dpy];
    config.device_id  = getDeviceId(dpy);
    config.session_id = m_frame_cfg[dpy].session_id;
    config.present_fence_idx = static_cast<unsigned int>(pf_idx);

    int err = WDT_IOCTL(m_dev_fd, DISP_IOCTL_TRIGGER_SESSION, &config);
    if (err < 0)
    {
        IOLOGE(dpy, err, "DISP_IOCTL_TRIGGER_SESSION pf_idx=%d", pf_idx);
    }
    else
    {
        DLOGD(dpy, "DISP_IOCTL_TRIGGER_SESSION pf_idx=%d", pf_idx);
    }

    return err;
}

status_t DispDevice::frameConfig(uint64_t dpy, int pf_idx, int ovlp_layer_num,
                                 int prev_present_fence_fd, hwc2_config_t config,
#ifdef MTK_IN_DISPLAY_FINGERPRINT
                                 const uint32_t& hrt_weight, const uint32_t& hrt_idx,
                                 const bool& is_HBM)
#else
                                 const uint32_t& hrt_weight, const uint32_t& hrt_idx)
#endif
{
    CHECK_DPY_RET_STATUS(dpy);

    if (-1 == ovlp_layer_num)
    {
        HWC_LOGW("ovlp_layer_num is not available, calculate roughly ...");
        ovlp_layer_num = 0;
        for (unsigned int i = 0; i < m_ovl_input_num; i++)
        {
            disp_input_config* input = &m_frame_cfg[dpy].input_cfg[i];
            if (1 == input->layer_enable)
                ovlp_layer_num++;
        }
    }

    m_frame_cfg[dpy].present_fence_idx = static_cast<unsigned int>(pf_idx);
    m_frame_cfg[dpy].overlap_layer_num = static_cast<unsigned int>(ovlp_layer_num);
    m_frame_cfg[dpy].tigger_mode = TRIGGER_NORMAL;
    m_frame_cfg[dpy].prev_present_fence_fd = prev_present_fence_fd;
    m_frame_cfg[dpy].active_config = static_cast<int>(config);
    m_frame_cfg[dpy].hrt_weight = hrt_weight;
    m_frame_cfg[dpy].hrt_idx = hrt_idx;
#ifdef MTK_IN_DISPLAY_FINGERPRINT
    m_frame_cfg[dpy].hbm_en = is_HBM;
#endif

    HWC_ATRACE_FORMAT_NAME("active_config(%d)", config);
    int err = WDT_IOCTL(m_dev_fd, DISP_IOCTL_FRAME_CONFIG, &m_frame_cfg[dpy]);
    if (err < 0)
    {
        IOLOGE(dpy, err, "DISP_IOCTL_FRAME_CONFIG ovlp:%d pf_idx=%d", ovlp_layer_num, pf_idx);
    }
    else
    {
        DbgLogger logger(DbgLogger::TYPE_HWC_LOG, 'D', nullptr);
#ifdef MTK_IN_DISPLAY_FINGERPRINT
        logger.printf("(%" PRIu64 ") DISP_IOCTL_FRAME_CONFIG ovlp:%d pf_idx=%d id:%x hrt:%u,%u hbm:%d",
                                    dpy, ovlp_layer_num, pf_idx, m_frame_cfg[dpy].session_id, hrt_weight, hrt_idx, is_HBM);
#else
        logger.printf("(%" PRIu64 ") DISP_IOCTL_FRAME_CONFIG ovlp:%d pf_idx=%d id:%x hrt:%u,%u",
                                    dpy, ovlp_layer_num, pf_idx, m_frame_cfg[dpy].session_id, hrt_weight, hrt_idx);
#endif
    }

    return err;
}

bool DispDevice::queryValidLayer(void* ptr)
{
    disp_layer_info* disp_layer = static_cast<disp_layer_info*>(ptr);
    int err = WDT_IOCTL(m_dev_fd, DISP_IOCTL_QUERY_VALID_LAYER, disp_layer);

    if (err < 0)
    {
        IOLOGE(uint64_t(HWC_DISPLAY_PRIMARY), err, "DISP_IOCTL_QUERY_VALID_LAYER: %d", err);
        return false;
    }

    return (disp_layer->hrt_num != -1) ? true : false;
}

status_t DispDevice::triggerOverlaySession(uint64_t dpy, uint32_t /*drm_id_crtc*/, int present_fence_idx,
                                           int ovlp_layer_num, int prev_present_fence_fd, hwc2_config_t config,
                                           const uint32_t& hrt_weight, const uint32_t& hrt_idx,
                                           unsigned int /*num*/, OverlayPortParam* const* /*params*/,
                                           sp<ColorTransform> /*color_transform*/,
                                           TriggerOverlayParam trigger_param
                                          )
{
    CHECK_DPY_RET_STATUS(dpy);

    if (DISP_INVALID_SESSION == m_frame_cfg[dpy].session_id)
    {
        HWC_LOGW("(%" PRIu64 ") Failed to trigger invalid DispSession", dpy);
        return INVALID_OPERATION;
    }

    HWC_ATRACE_FORMAT_NAME("TrigerOVL:%d", present_fence_idx);
    if (!m_caps_info.is_support_frame_cfg_ioctl)
        return legacyTriggerSession(dpy, present_fence_idx);
    else
       return frameConfig(dpy, present_fence_idx, ovlp_layer_num, prev_present_fence_fd, config,
#ifdef MTK_IN_DISPLAY_FINGERPRINT
                          hrt_weight, hrt_idx, trigger_param.is_HBM);
#else
                          hrt_weight, hrt_idx);
#endif
}

void DispDevice::disableOverlaySession(
    uint64_t dpy, uint32_t /*drm_id_crtc*/, OverlayPortParam* const* params, unsigned int num)
{
    CHECK_DPY_RET_VOID(dpy);

    unsigned int session_id = m_frame_cfg[dpy].session_id;
    if (DISP_INVALID_SESSION == session_id)
    {
        HWC_LOGW("(%" PRIu64 ") Failed to disable invalid DispSession", dpy);
        return;
    }

    {
        DbgLogger logger(DbgLogger::TYPE_HWC_LOG, 'D', "(%" PRIu64 ") disableOverlaySession ", dpy);
        for (unsigned int i = 0; i < m_ovl_input_num; i++)
        {
            disp_input_config* input = &m_frame_cfg[dpy].input_cfg[i];

            if (i >= num)
            {
                input->layer_id = static_cast<__u8>(m_ovl_input_num + 1);
                continue;
            }

            input->layer_id     = static_cast<__u8>(i);
            input->layer_enable = 0;
            input->next_buff_idx = params[i]->fence_index;

            logger.printf("-%d,idx=%d/ ", i, input->next_buff_idx);
        }
        m_frame_cfg[dpy].input_layer_num = (num < m_ovl_input_num) ? num : m_ovl_input_num;
    }

    if (!m_caps_info.is_support_frame_cfg_ioctl)
        legacySetInputBuffer(dpy);

    disableOverlayOutput(dpy, UINT32_MAX);
    triggerOverlaySession(dpy, UINT32_MAX, -1, 0, -1, 0, 0, 0, 0, nullptr, nullptr, {});

    DLOGD(dpy, "Disable DispSession");
}

status_t DispDevice::setOverlaySessionMode(uint64_t dpy, HWC_DISP_MODE mode)
{
    CHECK_DPY_RET_STATUS(dpy);

    unsigned int session_id = m_frame_cfg[dpy].session_id;
    if (DISP_INVALID_SESSION == session_id)
    {
        HWC_LOGW("(%" PRIu64 ") Failed to set invalid DispSession (mode)", dpy);
        return INVALID_OPERATION;
    }

    disp_session_config config;
    memset(&config, 0, sizeof(disp_session_config));

    config.device_id  = getDeviceId(dpy);
    config.session_id = session_id;
    config.mode       = mapHwcDispMode2Disp(mode);

    int err = WDT_IOCTL(m_dev_fd, DISP_IOCTL_SET_SESSION_MODE, &config);
    if (err < 0)
    {
        IOLOGE(dpy, err, "DISP_IOCTL_SET_SESSION_MODE %s", getSessionModeString(mode).string());
        return BAD_VALUE;
    }

    m_frame_cfg[dpy].mode = config.mode;

    DLOGD(dpy, "DispSessionMode (%s)", getSessionModeString(mode).string());
    return NO_ERROR;
}

HWC_DISP_MODE DispDevice::getOverlaySessionMode(uint64_t dpy, uint32_t /*drm_id_crtc*/)
{
    if (!CHECK_DPY_VALID(dpy))
    {
        HWC_LOGE("%s(), invalid dpy %" PRIu64 "", __FUNCTION__, dpy);
        return HWC_DISP_INVALID_SESSION_MODE;
    }

    unsigned int session_id = m_frame_cfg[dpy].session_id;
    if (DISP_INVALID_SESSION == session_id)
    {
        HWC_LOGW("(%" PRIu64 ") Failed to get invalid DispSession", dpy);
        return HWC_DISP_INVALID_SESSION_MODE;
    }

    return mapDispMode2Hwc(m_frame_cfg[dpy].mode);
}

status_t DispDevice::getOverlaySessionInfo(uint64_t dpy, uint32_t /*drm_id_crtc*/, SessionInfo* info)
{
    CHECK_DPY_RET_STATUS(dpy);

    unsigned int session_id = m_frame_cfg[dpy].session_id;
    if (DISP_INVALID_SESSION == session_id)
    {
        HWC_LOGW("(%" PRIu64 ") Failed to get info for invalid DispSession", dpy);
        return INVALID_OPERATION;
    }

    disp_session_info tmp_info;
    memset(&tmp_info, 0, sizeof(tmp_info));
    tmp_info.session_id = session_id;

    int err = WDT_IOCTL(m_dev_fd, DISP_IOCTL_GET_SESSION_INFO, &tmp_info);
    if (err < 0)
    {
        IOLOGE(dpy, err, "DISP_IOCTL_GET_SESSION_INFO");
    }
    else
    {
        covertDispSessionInfo2SessionInfo(info, &tmp_info);
    }

    return NO_ERROR;
}

unsigned int DispDevice::getAvailableOverlayInput(uint64_t dpy, uint32_t /*drm_id_crtc*/)
{
    SessionInfo info;

    getOverlaySessionInfo(dpy, UINT32_MAX, &info);

    return info.maxLayerNum;
}

void DispDevice::prepareOverlayInput(
    uint64_t dpy, OverlayPrepareParam* param)
{
    CHECK_DPY_RET_VOID(dpy);

    unsigned int session_id = m_frame_cfg[dpy].session_id;
    if (DISP_INVALID_SESSION == session_id)
    {
        HWC_LOGW("(%" PRIu64 ") Failed to preapre invalid DispSession (input)", dpy);
        return;
    }

    disp_buffer_info buffer;
    memset(&buffer, 0, sizeof(disp_buffer_info));

    buffer.session_id = session_id;
    buffer.layer_id   = param->id;
    buffer.layer_en   = 1;
    buffer.ion_fd     = param->ion_fd;
    buffer.cache_sync = param->is_need_flush;
    buffer.index      = UINT_MAX;
    buffer.fence_fd   = -1;

    int err = WDT_IOCTL(m_dev_fd, DISP_IOCTL_PREPARE_INPUT_BUFFER, &buffer);
    if (err < 0)
    {
        IOLOGE(dpy, err, "DISP_IOCTL_PREPARE_INPUT_BUFFER");
    }
    param->fence_index = buffer.index;
    param->fence_fd    = dupCloseAndSetFd(&buffer.fence_fd);

    HWC_ATRACE_BUFFER_INFO("pre_input",
        dpy, param->id, param->fence_index, param->fence_fd);

    HWC_LOGV("DispDevice::prepareOverlayInput() dpy:%" PRIu64 " id:%u ion:%d fence_idx:%d fence_fd:%d",
             dpy, param->id, param->ion_fd, param->fence_index, param->fence_fd);
}

void DispDevice::updateOverlayInputs(uint64_t dpy, uint32_t /*drm_id_crtc*/,
                                     OverlayPortParam* const* params,
                                     unsigned int num,
                                     sp<ColorTransform> color_transform)
{
    CHECK_DPY_RET_VOID(dpy);

    HWC_LOGV("+ updateOverlayInputs num:%u", num);
    unsigned int session_id = m_frame_cfg[dpy].session_id;
    if (DISP_INVALID_SESSION == session_id)
    {
        HWC_LOGW("(%" PRIu64 ") Failed to update invalid DispSession (input)", dpy);
        return;
    }

    DbgLogger logger(DbgLogger::TYPE_HWC_LOG, 'D', "(%" PRIu64 ") Input: ", dpy);
    DbgLogger logger_dirty(DbgLogger::TYPE_HWC_LOG, 'V', "[DIRTY] dev(xywh)");

    bool is_fbt_exist = false;
    size_t non_fbt_layer_num = 0;
    unsigned int i = 0;
    size_t layer_enable_amount = 0;
    for (; i < m_ovl_input_num; i++)
    {
        if (i >= num) break;

        HWC_LOGV("i:%u params[i]->state:%d", i, params[i]->state);

        disp_input_config* input = &m_frame_cfg[dpy].input_cfg[i];

        if (OVL_IN_PARAM_DISABLE == params[i]->state)
        {
            input->layer_id      = static_cast<__u8>(i);
            input->layer_enable  = 0;
            input->next_buff_idx = params[i]->fence_index;
            input->src_fence_fd = -1;

            logger.printf("-%u,idx:%d/ ", i, params[i]->fence_index);
            continue;
        }
        else
        {
            ++layer_enable_amount;
        }

        //some crop may be empty(w/h = 0), don't enable layer if crop is empty
        if (params[i]->src_crop.getWidth() < 1 ||
            params[i]->src_crop.getHeight() < 1 ||
            params[i]->dst_crop.getWidth() < 1 ||
            params[i]->dst_crop.getHeight() < 1)
        {
            input->layer_id      = static_cast<__u8>(i);
            input->layer_enable  = 0;
            input->next_buff_idx = params[i]->fence_index;
            input->src_fence_fd = -1;

            HWC_LOGW("(%" PRIu64 ":%u) Layer- (idx=%d,w/h=0) ", dpy, i, input->next_buff_idx);
            continue;
        }

        input->layer_id       = static_cast<__u8>(i);
        input->layer_enable   = 1;

        if (params[i]->dim)
        {
            input->buffer_source = DISP_BUFFER_ALPHA;
            input->dim_color = params[i]->layer_color;
        }
        else if (params[i]->ion_fd == HWC_NO_ION_FD)
        {
            input->buffer_source = DISP_BUFFER_MVA;
        }
        else
        {
            input->buffer_source = DISP_BUFFER_ION;
        }

        input->layer_type     = DISP_LAYER_2D;
        input->layer_rotation = DISP_ORIENTATION_0;
        input->src_base_addr  = params[i]->va;
        input->src_phy_addr   = params[i]->mva;
        input->src_pitch      = static_cast<__u16>(params[i]->pitch);
        input->src_v_pitch    = static_cast<__u16>(params[i]->v_pitch);
        input->src_fmt        = mapDispInFormat(params[i]->format, params[i]->blending);
        input->src_offset_x   = static_cast<__u16>(params[i]->src_crop.left);
        input->src_offset_y   = static_cast<__u16>(params[i]->src_crop.top);
        input->src_width      = static_cast<__u16>(params[i]->src_crop.getWidth());
        input->src_height     = static_cast<__u16>(params[i]->src_crop.getHeight());
        input->tgt_offset_x   = static_cast<__u16>(params[i]->dst_crop.left);
        input->tgt_offset_y   = static_cast<__u16>(params[i]->dst_crop.top);
        input->tgt_width      = static_cast<__u16>(params[i]->dst_crop.getWidth());
        input->tgt_height     = static_cast<__u16>(params[i]->dst_crop.getHeight());
        input->isTdshp        = static_cast<__u8>(params[i]->is_sharpen);
        input->next_buff_idx  = params[i]->fence_index;
        input->identity       = static_cast<__u8>(params[i]->identity);
        if (params[i]->identity == HWC_LAYER_TYPE_FBT)
        {
            is_fbt_exist = true;
        }
        else
        {
            ++non_fbt_layer_num;
        }

        input->connected_type = static_cast<__u8>(params[i]->connected_type);
        input->alpha_enable   = static_cast<__u8>(params[i]->alpha_enable);
        input->alpha          = params[i]->alpha;
        input->frm_sequence   = static_cast<uint32_t>(params[i]->sequence % UINT32_MAX);
        input->ext_sel_layer  = static_cast<__s8>(params[i]->ext_sel_layer);
        input->yuv_range = mapDispColorRangefromDS(params[i]->dataspace);

        // widegamut series layer uses dataspace to config
        input->dataspace      = params[i]->dataspace;
        input->src_fence_fd   = params[i]->fence;

        // partial update - fill dirty rects info
        logger_dirty.printf(" (%u,n:%zu)", i, params[i]->ovl_dirty_rect_cnt);
        if (0 == params[i]->ovl_dirty_rect_cnt)
        {
            input->dirty_roi_num = 0;
            logger_dirty.printf("%u NULL", i);
        }
        else
        {
            layer_dirty_roi* hwdev_dirty_rect = m_hwdev_dirty_rect[dpy][i];
            hwc_rect_t* ovl_dirty_rect = params[i]->ovl_dirty_rect;
            for (size_t j = 0; j < params[i]->ovl_dirty_rect_cnt; j++)
            {
                hwdev_dirty_rect[j].dirty_x = static_cast<__u16>(ovl_dirty_rect[j].left);
                hwdev_dirty_rect[j].dirty_y = static_cast<__u16>(ovl_dirty_rect[j].top);
                hwdev_dirty_rect[j].dirty_w = static_cast<__u16>(ovl_dirty_rect[j].right - ovl_dirty_rect[j].left);
                hwdev_dirty_rect[j].dirty_h = static_cast<__u16>(ovl_dirty_rect[j].bottom - ovl_dirty_rect[j].top);
                logger_dirty.printf("[%d,%d,%d,%d]", hwdev_dirty_rect[j].dirty_x,
                                                     hwdev_dirty_rect[j].dirty_y,
                                                     hwdev_dirty_rect[j].dirty_w,
                                                     hwdev_dirty_rect[j].dirty_h);
            }
            input->dirty_roi_addr = hwdev_dirty_rect;
            input->dirty_roi_num = static_cast<__u16>(params[i]->ovl_dirty_rect_cnt);
        }

        if (!isConstantAlphaForRGBASupported())
        {
            if (params[i]->blending == HWC2_BLEND_MODE_PREMULTIPLIED)
            {
                input->sur_aen = 1;
                input->src_alpha = DISP_ALPHA_ONE;
                input->dst_alpha = DISP_ALPHA_SRC_INVERT;
            }
            else
            {
                input->sur_aen = 0;
            }
        }

        if (params[i]->secure)
            input->security = DISP_SECURE_BUFFER;
        else if (params[i]->protect)
            input->security = DISP_PROTECT_BUFFER;
        else
            input->security = DISP_NORMAL_BUFFER;

        input->compress = params[i]->compress;

        logger.printf("+%u,ion:%d,idx:%d,ext:%d/ ", i, params[i]->ion_fd, params[i]->fence_index, params[i]->ext_sel_layer);

        DbgLogger* ovl_logger = &Debugger::getInstance().m_logger->ovlInput[static_cast<size_t>(dpy)][i];
        ovl_logger->printf("(%" PRIu64 ":%u) Layer+ ("
                 "mva=%p/sec=%d/prot=%d/"
                 "alpha=%d:0x%02x/blend=%04x/dim=%d/fmt=%d/range=%x/"
                 "x=%d y=%d w=%d h=%d s=%d,%d -> x=%d y=%d w=%d h=%d/"
                 "layer_type=%d ext_layer=%d ds=%d fbdc=%d)",
                 dpy, i, params[i]->mva, params[i]->secure, params[i]->protect,
                 params[i]->alpha_enable, params[i]->alpha, params[i]->blending,
                 params[i]->dim, input->src_fmt, input->yuv_range,
                 params[i]->src_crop.left, params[i]->src_crop.top,
                 params[i]->src_crop.getWidth(), params[i]->src_crop.getHeight(), params[i]->pitch, input->src_v_pitch,
                 params[i]->dst_crop.left, params[i]->dst_crop.top,
                 params[i]->dst_crop.getWidth(), params[i]->dst_crop.getHeight(),
                 input->layer_type , params[i]->ext_sel_layer, params[i]->dataspace, input->compress);

        ovl_logger->tryFlush();

        HWC_ATRACE_BUFFER_INFO("set_input",
            dpy, i, params[i]->fence_index, params[i]->if_fence_index);
    }

    if (layer_enable_amount == 0)
    {
        HWC_LOGI("[DEV] updateOverlayInputs(): No enabling layer");
    }

    m_frame_cfg[dpy].input_layer_num = i;

    if (HwcFeatureList::getInstance().getFeature().is_support_pq &&
        Platform::getInstance().m_config.support_color_transform)
    {
        if (color_transform != nullptr && color_transform->dirty)
        {
            m_frame_cfg[dpy].ccorr_config.is_dirty = true;
            m_frame_cfg[dpy].ccorr_config.mode = color_transform->hint;
            m_frame_cfg[dpy].ccorr_config.feature_flag = color_transform->force_disable_color;

            for (unsigned int i = 0; i < COLOR_MATRIX_DIM * COLOR_MATRIX_DIM ; ++i)
                m_frame_cfg[dpy].ccorr_config.color_matrix[i] = transFloatToIntForCCORR(color_transform->matrix[i / 4][i % 4]);

            if (m_frame_cfg[dpy].ccorr_config.mode == HAL_COLOR_TRANSFORM_IDENTITY)
            {
                HWC_LOGI("[DEV] (Send identity matrix)");
            }
            else
            {
                for (unsigned int i = 0; i < COLOR_MATRIX_DIM; ++i)
                {
                    DbgLogger logger(DbgLogger::TYPE_HWC_LOG, 'I', "[DEV] ");
                    logger.printf("%4d %4d %4d %4d",
                        m_frame_cfg[dpy].ccorr_config.color_matrix[i * COLOR_MATRIX_DIM],
                        m_frame_cfg[dpy].ccorr_config.color_matrix[i * COLOR_MATRIX_DIM + 1],
                        m_frame_cfg[dpy].ccorr_config.color_matrix[i * COLOR_MATRIX_DIM + 2],
                        m_frame_cfg[dpy].ccorr_config.color_matrix[i * COLOR_MATRIX_DIM + 3]);
                }
                HWC_LOGI("[DEV] color transform: feature_flag=%d", m_frame_cfg[dpy].ccorr_config.feature_flag);
            }
        }
    }
    else
    {
        m_frame_cfg[dpy].ccorr_config.is_dirty = false;
    }

    HWC_LOGV("- updateOverlayInputs");

    if (!m_caps_info.is_support_frame_cfg_ioctl)
        legacySetInputBuffer(dpy);
}

void DispDevice::prepareOverlayOutput(uint64_t dpy, OverlayPrepareParam* param)
{
    CHECK_DPY_RET_VOID(dpy);

    unsigned int session_id = m_frame_cfg[dpy].session_id;
    if (DISP_INVALID_SESSION == session_id)
    {
        HWC_LOGW("(%" PRIu64 ") Failed to prepare invalid DispSession (output)", dpy);
        return;
    }

    disp_buffer_info buffer;
    memset(&buffer, 0, sizeof(disp_buffer_info));

    buffer.session_id = session_id;
    buffer.layer_id   = param->id;
    buffer.layer_en   = 1;
    buffer.ion_fd     = param->ion_fd;
    buffer.cache_sync = param->is_need_flush;
    buffer.index      = UINT_MAX;
    buffer.fence_fd   = -1;

    int err = WDT_IOCTL(m_dev_fd, DISP_IOCTL_PREPARE_OUTPUT_BUFFER, &buffer);
    if (err < 0)
    {
        IOLOGE(dpy, err, "DISP_IOCTL_PREPARE_OUTPUT_BUFFER");
    }

    param->fence_index = buffer.index;
    param->fence_fd    = dupCloseAndSetFd(&buffer.fence_fd);

    param->if_fence_index = buffer.interface_index;
    param->if_fence_fd    = dupCloseAndSetFd(&buffer.interface_fence_fd);

    HWC_ATRACE_BUFFER_INFO("pre_output",
        dpy, param->id, param->fence_index, param->fence_fd);
}

void DispDevice::disableOverlayOutput(uint64_t dpy, uint32_t /*drm_id_crtc*/)
{
    CHECK_DPY_RET_VOID(dpy);

    m_frame_cfg[dpy].output_en = false;
    m_frame_cfg[dpy].output_cfg.src_fence_fd = -1;
}

void DispDevice::enableOverlayOutput(uint64_t dpy, uint32_t /*drm_id_crtc*/, OverlayPortParam* param)
{
    CHECK_DPY_RET_VOID(dpy);

    unsigned int session_id = m_frame_cfg[dpy].session_id;
    if (DISP_INVALID_SESSION == session_id)
    {
        HWC_LOGW("(%" PRIu64 ") Failed to update invalid DispSession (output)", dpy);
        return;
    }

    disp_output_config* out_cfg = &m_frame_cfg[dpy].output_cfg;

    out_cfg->va     = param->va;
    out_cfg->pa     = param->mva;
    out_cfg->fmt    = mapDispOutFormat(param->format);
    out_cfg->x      = static_cast<unsigned int>(param->dst_crop.left);
    out_cfg->y      = static_cast<unsigned int>(param->dst_crop.top);
    out_cfg->width  = static_cast<unsigned int>(param->dst_crop.getWidth());
    out_cfg->height = static_cast<unsigned int>(param->dst_crop.getHeight());
    out_cfg->pitch  = param->pitch;
    out_cfg->src_fence_fd = param->fence;

    if (param->secure)
        out_cfg->security = DISP_SECURE_BUFFER;
    else
        out_cfg->security = DISP_NORMAL_BUFFER;

    out_cfg->buff_idx = param->fence_index;

    out_cfg->interface_idx = param->if_fence_index;

    out_cfg->frm_sequence  = static_cast<uint32_t>(param->sequence % UINT32_MAX);

    HWC_LOGD("(%" PRIu64 ") Output+ (ion=%d/idx=%d/if_idx=%d/sec=%d/fmt=%d"
             "/x=%d y=%d w=%d h=%d s=%d ds=%d)",
             dpy, param->ion_fd, param->fence_index, param->if_fence_index, param->secure,
             out_cfg->fmt, param->dst_crop.left, param->dst_crop.top,
             param->dst_crop.getWidth(), param->dst_crop.getHeight(),
             param->pitch, param->dataspace);

    m_frame_cfg[dpy].output_en = true;

    if (!m_caps_info.is_support_frame_cfg_ioctl)
        legacySetOutputBuffer(dpy);
}

void DispDevice::prepareOverlayPresentFence(uint64_t dpy, OverlayPrepareParam* param)
{
    CHECK_DPY_RET_VOID(dpy);

    unsigned int session_id = m_frame_cfg[dpy].session_id;
    if (DISP_INVALID_SESSION == session_id)
    {
        HWC_LOGW("(%" PRIu64 ") Failed to preapre invalid DispSession (present)", dpy);
        return;
    }

    {
        disp_present_fence fence;

        fence.session_id = session_id;

        int err = WDT_IOCTL(m_dev_fd, DISP_IOCTL_GET_PRESENT_FENCE, &fence);
        if (err < 0)
        {
            IOLOGE(dpy, err, "DISP_IOCTL_GET_PRESENT_FENCE");
        }

        param->fence_index = fence.present_fence_index;
        param->fence_fd    = dupCloseAndSetFd(&fence.present_fence_fd);

        HWC_LOGV("(%" PRIu64 ") DispDevice::prepareOverlayPresentFence() idx:%d fence fd:%d",
            dpy, param->fence_index, param->fence_fd);
    }
}

status_t DispDevice::waitVSync(uint64_t dpy, uint32_t /*drm_id_crtc*/, nsecs_t *ts)
{
    CHECK_DPY_RET_STATUS(dpy);

    unsigned int session_id = m_frame_cfg[dpy].session_id;
    if (DISP_INVALID_SESSION == session_id)
    {
        HWC_LOGW("Failed to wait vsync for invalid DispSession (dpy=%" PRIu64 ")", dpy);
        return BAD_VALUE;
    }

    disp_session_vsync_config config;
    memset(&config, 0, sizeof(config));

    config.session_id = session_id;

    int err = WDT_IOCTL(m_dev_fd, DISP_IOCTL_WAIT_FOR_VSYNC, &config);
    if (err < 0)
    {
        IOLOGE(dpy, err, "DISP_IOCTL_WAIT_FOR_VSYNC");
        return BAD_VALUE;
    }

    *ts = static_cast<nsecs_t>(config.vsync_ts);

    return NO_ERROR;
}

void DispDevice::setPowerMode(uint64_t dpy, uint32_t /*drm_id_crtc*/, int mode)
{
    CHECK_DPY_RET_VOID(dpy);

    HWC_LOGD("DispDevice::%s() dpy:%" PRIu64 " mode:%d", __FUNCTION__, dpy, mode);
#ifdef USE_SWWATCHDOG
    AUTO_WDT(1000);
#endif
    std::string filename("/dev/graphics/fb");
    filename += std::to_string(dpy);
    int fb_fd = open(filename.c_str(), O_RDWR);
    if (fb_fd < 0)
    {
        HWC_LOGE("Failed to open fb%" PRIu64 " device: %s", dpy, strerror(errno));
        return;
    }

    int err = NO_ERROR;
    switch (mode)
    {
        case HWC_POWER_MODE_OFF:
        {
            err = WDT_IOCTL(fb_fd, FBIOBLANK, FB_BLANK_POWERDOWN);
#ifdef USE_SWWATCHDOG
            if (dpy == HWC_DISPLAY_PRIMARY)
                SWWatchDog::suspend(dpy);
#endif
            break;
        }
        case HWC_POWER_MODE_NORMAL:
        {
#ifdef USE_SWWATCHDOG
            if (dpy == HWC_DISPLAY_PRIMARY)
                SWWatchDog::resume(dpy);
#endif
            err = WDT_IOCTL(fb_fd, FBIOBLANK, FB_BLANK_UNBLANK);
            break;
        }
        case HWC_POWER_MODE_DOZE:
        {
            if (HwcFeatureList::getInstance().getFeature().aod)
            {
                err = WDT_IOCTL(fb_fd, MTKFB_SET_AOD_POWER_MODE, MTKFB_AOD_DOZE);
            }
            else
            {
                err = WDT_IOCTL(fb_fd, FBIOBLANK, FB_BLANK_UNBLANK);
                HWC_LOGE("%s: receive HWC_POWER_MODE_DOZE without aod enabled", __FUNCTION__);
            }
            break;
        }
        case HWC_POWER_MODE_DOZE_SUSPEND:
        {
            if (HwcFeatureList::getInstance().getFeature().aod)
            {
                err = WDT_IOCTL(fb_fd, MTKFB_SET_AOD_POWER_MODE, MTKFB_AOD_DOZE_SUSPEND);
            }
            else
            {
                err = WDT_IOCTL(fb_fd, FBIOBLANK, FB_BLANK_UNBLANK);
                HWC_LOGE("%s: receive HWC_POWER_MODE_DOZE_SUSPEND without aod enabled", __FUNCTION__);
            }
            break;
        }

        default:
            HWC_LOGE("%s: receive unknown power mode: %d", __FUNCTION__, mode);
    }

    if (err != NO_ERROR) {
        HWC_LOGE("Failed to set power(%d) to fb%" PRIu64 " device: %s", mode, dpy, strerror(errno));
    }

    protectedClose(fb_fd);
}

unsigned int DispDevice::getDeviceId(uint64_t dpy)
{
    return static_cast<unsigned int>(dpy);
}

status_t DispDevice::waitAllJobDone(const uint64_t dpy)
{
    CHECK_DPY_RET_STATUS(dpy);

    if (isFenceWaitSupported())
    {
        if (m_frame_cfg[dpy].session_id == DISP_INVALID_SESSION)
        {
            HWC_LOGW("try to wait for an invalid display session");
            return INVALID_OPERATION;
        }

        int err = WDT_IOCTL(m_dev_fd, DISP_IOCTL_WAIT_ALL_JOBS_DONE, m_frame_cfg[dpy].session_id);
        if (err < 0)
        {
            IOLOGE(dpy, err, "DISP_IOCTL_WAIT_ALL_JOBS_DONE");
            return err;
        }
    }

    return NO_ERROR;
}

status_t DispDevice::waitRefreshRequest(unsigned int* type)
{
    // NOTE: don't use WDT_IOCTL since this DISP_IOCTL_WAIT_DISP_SELF_REFRESH will
    // blocked by driver until self refresh is needed
    DISP_SELF_REFRESH_TYPE temp = WAIT_FOR_REFRESH;
    int err = ioctl(m_dev_fd, DISP_IOCTL_WAIT_DISP_SELF_REFRESH, &temp);
    if (err < 0)
    {
        IOLOGE(uint64_t(HWC_DISPLAY_PRIMARY), err, "DISP_IOCTL_WAIT_DISP_SELF_REFRESH");
        return BAD_VALUE;
    }
    *type = mapDispSelfRefreshType2Hwc(temp);

    return NO_ERROR;
}

int32_t DispDevice::getWidth(uint64_t dpy, uint32_t /*drm_id_connector*/, hwc2_config_t config)
{
    return static_cast<int32_t>(m_multi_cfgs[dpy].dyn_cfgs[config].width);
}

int32_t DispDevice::getHeight(uint64_t dpy, uint32_t /*drm_id_connector*/, hwc2_config_t config)
{
    return static_cast<int32_t>(m_multi_cfgs[dpy].dyn_cfgs[config].height);
}

int32_t DispDevice::getRefresh(uint64_t dpy, uint32_t /*drm_id_connector*/, hwc2_config_t config)
{
    // display Driver will multiply 100 for float support
    return m_multi_cfgs[dpy].dyn_cfgs[config].vsyncFPS / 100;
}

uint32_t DispDevice::getNumConfigs(uint64_t dpy, uint32_t /*drm_id_connector*/)
{
    if (!isMultiConfigsSupport())
    {
        return 1;
    }
    return m_multi_cfgs[dpy].config_num;
}


void DispDevice::dump(const uint64_t& /*dpy*/, String8* /*dump_str*/)
{
}


int32_t DispDevice::updateDisplayResolution(uint64_t /*dpy*/)
{
    return 0;
}

bool DispDevice::isMultiConfigsSupport()
{
    return m_caps_info.disp_feature & DISP_FEATURE_DYNFPS;
}

status_t DispDevice::getOverlayMultiConfigs(uint64_t dpy)
{
    if (!isMultiConfigsSupport())
    {
        HWC_LOGW("Not support to get multi configs");
        return INVALID_OPERATION;
    }
    if (dpy != HWC_DISPLAY_PRIMARY )
    {
        HWC_LOGW("(%" PRIu64 ") Failed to get multi configs for ", dpy);
        return INVALID_OPERATION;
    }

    struct multi_configs tmp_configs;
    memset(&tmp_configs, 0, sizeof(tmp_configs));
    int err = WDT_IOCTL(m_dev_fd, DISP_IOCTL_GET_MULTI_CONFIGS, &tmp_configs);
    if (err < 0)
    {
        IOLOGE(dpy, err, "DISP_IOCTL_GET_MULTI_CONFIGS");
    }
    else
    {
        m_multi_cfgs[dpy].config_num = tmp_configs.config_num;
        for (unsigned int i = 0; i < m_multi_cfgs[dpy].config_num; i++)
        {
            m_multi_cfgs[dpy].dyn_cfgs[i].vsyncFPS = tmp_configs.dyn_cfgs[i].vsyncFPS;
            m_multi_cfgs[dpy].dyn_cfgs[i].width = tmp_configs.dyn_cfgs[i].width;
            m_multi_cfgs[dpy].dyn_cfgs[i].height = tmp_configs.dyn_cfgs[i].height;
        }
    }

    return NO_ERROR;
}

int32_t DispDevice::getCurrentRefresh(uint64_t dpy, uint32_t /*drm_id_crtc*/)
{
    SessionInfo info;
    getOverlaySessionInfo(dpy, UINT32_MAX, &info);
    return static_cast<int32_t>(info.vsyncFPS);
}

void DispDevice::submitMML(uint64_t /*dpy*/, struct mml_submit& /*params*/)
{
}

void DispDevice::enableDisplayDriverLog(uint32_t /*param*/)
{
}

int32_t DispDevice::updateConnectorMode(uint64_t /*dpy*/, uint32_t /*drm_id_crtc*/)
{
    return 0;
}

int32_t DispDevice::getSpportedConnectorMode(uint64_t /*dpy*/, uint32_t /*drm_id_crtc*/, uint32_t* /*modeinfo*/)
{
    return 0;
}

int32_t DispDevice::getConnectorEDID(uint64_t /*dpy*/, uint32_t /*drm_id_crtc*/, uint32_t* /*edid*/)
{
    return 0;
}

int32_t DispDevice::setHDMIMode(uint64_t /*dpy*/, uint32_t /*drm_id_crtc*/, uint32_t /*mode*/, uint32_t /*value*/)
{
    return 0;
}

int32_t DispDevice::getHDMIMode(uint64_t /*dpy*/, uint32_t /*drm_id_crtc*/, uint32_t /*mode*/, uint32_t* /*value*/)
{
    return 0;
}

bool AsyncBlitDevice::getEthdrSupport(uint64_t /*dpy*/)
{
    return false;
}

int32_t DispDevice::getKernelLogoSize(int32_t* /*width*/, int32_t* /*height*/)
{
    return 0;
}
