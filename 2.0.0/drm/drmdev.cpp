#define DEBUG_LOG_TAG "DRMDEV"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include "drmdev.h"

#include <cutils/properties.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>

#include <fcntl.h>
#include <sys/ioctl.h>

#include <linux/fb.h>

#include "hwc2_defs.h"
#include <hwc_feature_list.h>

#include "utils/debug.h"
#include "utils/tools.h"
#include <utils/Trace.h>
#include "overlay.h"
#include "platform_wrap.h"

#include "ddp_ovl.h"
#include "hdmi_interface.h"

#include <xf86drm.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#include <xf86drmMode.h>
#pragma clang diagnostic pop
#include <drm_fourcc.h>
#include <sys/mman.h>
#include <libdrm_macros.h>

#include <drm/drmmodeplane.h>
#include <drm/drmmodeencoder.h>
#include <drm/drmmodeconnector.h>

#include <cutils/properties.h>

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
        err = ioctl(fd, CMD, ##__VA_ARGS__);                                                \
    }                                                                                       \
    err;                                                                                    \
})

// if still in constructor, should not use watch dog, use DRM_IOCTL
#define DRM_WDT_IOCTL(CMD, ...)                                                             \
({                                                                                          \
    int err = 0;                                                                            \
    if (Platform::getInstance().m_config.wdt_trace)                                         \
    {                                                                                       \
        ATRACE_NAME(#CMD);                                                                  \
        SWWatchDog::AutoWDT _wdt("[DEV] ioctl(" #CMD "):" STRINGIZE(__LINE__), 500);        \
        err = DrmModeResource::getInstance().ioctl(CMD, ##__VA_ARGS__);                                              \
    }                                                                                       \
    else                                                                                    \
    {                                                                                       \
        err = DrmModeResource::getInstance().ioctl(CMD, ##__VA_ARGS__);                                              \
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

#define DRM_WDT_IOCTL(CMD, ...)                                                             \
({                                                                                          \
    int err = 0;                                                                            \
    if (Platform::getInstance().m_config.wdt_trace)                                         \
    {                                                                                       \
        ATRACE_NAME(#CMD);                                                                  \
        err = DrmModeResource::getInstance().ioctl(CMD, ##__VA_ARGS__);                                              \
    }                                                                                       \
    else                                                                                    \
    {                                                                                       \
        err = DrmModeResource::getInstance().ioctl(CMD, ##__VA_ARGS__);                                              \
    }                                                                                       \
    err;                                                                                    \
})

#endif // USE_SWWATCHDOG

#define DRM_IOCTL(CMD, ...)                                                                \
({                                                                                         \
    int err = 0;                                                                           \
    ATRACE_NAME(#CMD);                                                                     \
    err = DrmModeResource::getInstance().ioctl(CMD, ##__VA_ARGS__);                                                 \
    err;                                                                                   \
})

#define HWC_ATRACE_BUFFER_INFO(string, n1, n2, n3, n4)                                     \
    if (ATRACE_ENABLED()) {                                                                \
        char ___traceBuf[1024];                                                            \
        int ret = snprintf(___traceBuf, sizeof(___traceBuf), "%s(%" PRIu64 ":%u): %u %d",  \
                           (string), (n1), (n2), (n3), (n4));                              \
        if (ret >= 0 && ret < static_cast<int>(sizeof(___traceBuf))) {                     \
            android::ScopedTrace ___bufTracer(ATRACE_TAG, ___traceBuf);                    \
        }                                                                                  \
    }

#define AFBC_COMPRESSION_NAME "arm.graphics.Compression"
#define PVRIC_COMPRESSION_NAME "android.hardware.graphics.common.Compression"
// ---------------------------------------------------------------------------

#define DLOGD(i, x, ...) HWC_LOGD("(%" PRIu64 ") " x, i, ##__VA_ARGS__)
#define DLOGI(i, x, ...) HWC_LOGI("(%" PRIu64 ") " x, i, ##__VA_ARGS__)
#define DLOGW(i, x, ...) HWC_LOGW("(%" PRIu64 ") " x, i, ##__VA_ARGS__)
#define DLOGE(i, x, ...) HWC_LOGE("(%" PRIu64 ") " x, i, ##__VA_ARGS__)

#define IOLOGE(i, err, x, ...) HWC_LOGE("(%" PRIu64 ") " x " err:%d, %s", i, ##__VA_ARGS__, err, strerror(err))

#define DV_STATUS_SUPPORT 0

static uint32_t mapHwcDispMode2Drm(HWC_DISP_MODE mode)
{
    switch (mode)
    {
        case HWC_DISP_INVALID_SESSION_MODE:
            return MTK_DRM_SESSION_INVALID;

        case HWC_DISP_SESSION_DIRECT_LINK_MODE:
            return MTK_DRM_SESSION_DL;

        case HWC_DISP_SESSION_DECOUPLE_MIRROR_MODE:
            return MTK_DRM_SESSION_DC_MIRROR;

        case HWC_DISP_SESSION_TRIPLE_DIRECT_LINK_MODE:
            return MTK_DRM_SESSION_TRIPLE_DL;

        default:
           HWC_LOGW("failed to map session mode(%d) to drm definition", mode);
           return MTK_DRM_SESSION_INVALID;
    }
}

static HWC_DISP_MODE mapDispMode2Hwc(uint32_t mode)
{
    switch (mode)
    {
        case MTK_DRM_SESSION_INVALID:
            return HWC_DISP_INVALID_SESSION_MODE;

        case MTK_DRM_SESSION_DL:
            return HWC_DISP_SESSION_DIRECT_LINK_MODE;

        case MTK_DRM_SESSION_DOUBLE_DL:
            return HWC_DISP_SESSION_DIRECT_LINK_MODE;

        case MTK_DRM_SESSION_DC_MIRROR:
            return HWC_DISP_SESSION_DECOUPLE_MIRROR_MODE;

        case MTK_DRM_SESSION_TRIPLE_DL:
            return HWC_DISP_SESSION_DIRECT_LINK_MODE;

        default:
            HWC_LOGW("failed to map session mode(%d) to hwc definition", mode);
            return HWC_DISP_INVALID_SESSION_MODE;
    }
}

static uint32_t mapHwcFeatureFlag(uint32_t flag)
{
    uint32_t tmp = 0;

    if (flag & HWC_FEATURE_HRT)
    {
        tmp |= DRM_DISP_FEATURE_HRT;
    }
    if (flag & HWC_FEATURE_DISP_SELF_REFRESH)
    {
        tmp |= DRM_DISP_FEATURE_DISP_SELF_REFRESH;
    }
    if (flag & HWC_FEATURE_RPO)
    {
        tmp |= DRM_DISP_FEATURE_RPO;
    }
    if (flag & HWC_FEATURE_FORCE_DISABLE_AOD)
    {
        tmp |= DRM_DISP_FEATURE_FORCE_DISABLE_AOD;
    }
    if (flag & HWC_FEATURE_MSYNC2_0)
    {
        tmp |= DRM_DISP_FEATURE_MSYNC2_0;
    }
    if (flag & HWC_FEATURE_OVL_VIRUTAL_DISPLAY)
    {
        tmp |= DRM_DISP_FEATURE_VIRUTAL_DISPLAY;
    }
    if (flag & HWC_FEATURE_BW_MONITOR_SUPPORT)
    {
        tmp |= DRM_DISP_FEATURE_OVL_BW_MONITOR;
    }
    if (flag & HWC_FEATURE_SMART_COMPOSITION_SUPPORT)
    {
        tmp |= DRM_DISP_FEATURE_GPU_CACHE;
    }

    return tmp;
}

static HWC_DISP_SESSION_TYPE g_session_type[DisplayManager::MAX_DISPLAYS] = {
    HWC_DISP_SESSION_PRIMARY,
    HWC_DISP_SESSION_EXTERNAL,
    HWC_DISP_SESSION_MEMORY,
    HWC_DISP_SESSION_EXTERNAL_1
};

enum {
    EXT_DEVICE_MHL = 1,
    EXT_DEVICE_EPD = 2,
    EXT_DEVICE_LCM = 3,
};

DisplayState::DisplayState()
    : m_num_active_display(0)
    , m_num_waited_display(0)
{
    for (unsigned int i = 0; i < DisplayManager::MAX_DISPLAYS; i++)
    {
        m_state[i] = DISPLAY_STATE_INACTIVE;
        m_drm_id_crtc[i] = UINT32_MAX;
    }
}

DisplayState::~DisplayState()
{
}

int DisplayState::checkDisplayStateMachine(uint64_t dpy, int new_state)
{
    CHECK_DPY_RET_STATUS(dpy);

    switch (m_state[dpy])
    {
        case DISPLAY_STATE_INACTIVE:
            if (new_state != DISPLAY_STATE_WAIT_TO_CREATE)
            {
                return INVALID_OPERATION;
            }
            break;

        case DISPLAY_STATE_WAIT_TO_CREATE:
            if (new_state != DISPLAY_STATE_INACTIVE && new_state != DISPLAY_STATE_ACTIVE)
            {
                return INVALID_OPERATION;
            }
            break;

        case DISPLAY_STATE_ACTIVE:
            if (new_state != DISPLAY_STATE_INACTIVE)
            {
                return INVALID_OPERATION;
            }
            break;

        default:
            return INVALID_OPERATION;
    }

    return NO_ERROR;
}

int DisplayState::changeDisplayState(uint64_t dpy, int new_state)
{
    CHECK_DPY_RET_STATUS(dpy);

    int res = checkDisplayStateMachine(dpy, new_state);
    if (res != NO_ERROR)
    {
        return res;
    }

    if (m_state[dpy] == DISPLAY_STATE_ACTIVE)
    {
        m_num_active_display--;
    }
    else if (m_state[dpy] == DISPLAY_STATE_WAIT_TO_CREATE)
    {
        m_num_waited_display--;
    }

    m_state[dpy] = new_state;

    if (new_state == DISPLAY_STATE_ACTIVE)
    {
        m_num_active_display++;
    }
    else if (new_state == DISPLAY_STATE_WAIT_TO_CREATE)
    {
        m_num_waited_display++;
    }

    return NO_ERROR;
}

DrmDevice::FbCacheInfo::FbCacheInfo(const OverlayPortParam* param)
    : id(param->hwc_layer_id)
    , src_buf_width(param->src_buf_width)
    , src_buf_height(param->src_buf_height)
    , pitch(param->pitch)
    , format(param->format)
    , secure(param->secure)
    , count(0)
    , last_alloc_id(static_cast<uint64_t>(-1))
    , last_buf_update(systemTime(CLOCK_MONOTONIC))
{
}

bool DrmDevice::FbCacheInfo::paramIsSame(const OverlayPortParam* param)
{
    if (src_buf_width != param->src_buf_width ||
        src_buf_height != param->src_buf_height ||
        pitch != param->pitch ||
        format != param->format ||
        secure != param->secure)
    {
        HWC_LOGW("%s(), id %" PRIu64 ", w %u/%u, h %u/%u, pitch %u/%u, format 0x%x/0x%x, secure %d/%d",
                 __FUNCTION__,
                 id,
                 src_buf_width, param->src_buf_width,
                 src_buf_height, param->src_buf_height,
                 pitch, param->pitch,
                 format, param->format,
                 secure, param->secure);
        return false;
    }
    return true;
}


void DrmDevice::FbCacheInfo::updateParam(const OverlayPortParam* param)
{
    src_buf_width = param->src_buf_width;
    src_buf_height = param->src_buf_height;
    pitch = param->pitch;
    format = param->format;
    secure = param->secure;
}

void DrmDevice::FbCache::moveFbCachesToRemove(FbCacheEntry& entry)
{
    fb_caches_pending_remove.push_back(entry);
}

void DrmDevice::FbCache::moveFbCachesToRemove(FbCacheInfo* cache)
{
    if (!cache)
    {
        return;
    }
    fb_caches_pending_remove.insert(fb_caches_pending_remove.end(),
                                    cache->fb_caches.begin(),
                                    cache->fb_caches.end());
    cache->fb_caches.clear();
}

void DrmDevice::FbCache::moveFbCachesToRemoveExcept(FbCacheInfo* cache, uint32_t fb_id)
{
    if (!cache)
    {
        return;
    }
    cache->fb_caches.remove_if(
        [&](FbCacheEntry& entry)
        {
            if (entry.fb_id != fb_id)
            {
                fb_caches_pending_remove.push_back(entry);
                return true;
            }
            return false;
        });
}

DrmDevice::FbCacheInfo* DrmDevice::FbCache::getLayerCacheForId(uint64_t id)
{
    for (FbCacheInfo& cache : layer_caches)
    {
        if (cache.id == id)
        {
            return &cache;
        }
    }
    return nullptr;
}

void DrmDevice::FbCache::dump(String8* str)
{
    if (CC_UNLIKELY(!str))
    {
        return;
    }

    for (FbCacheInfo& cache : layer_caches)
    {
        for (FbCacheEntry& entry : cache.fb_caches)
        {
            str->appendFormat("cache, id: %" PRIu64 ", alloc_id %" PRIu64 ", fb_id %" PRIu32", fmt 0x%x\n",
                              cache.id, entry.alloc_id, entry.fb_id, entry.format);
        }
    }
}

DrmDevice& DrmDevice::getInstance()
{
    static DrmDevice gInstance;
    return gInstance;
}

DrmDevice::DrmDevice()
    : m_drm(&DrmModeResource::getInstance())
{
    for (unsigned int i = 0; i < DisplayManager::MAX_DISPLAYS; i++)
    {
        m_atomic_req[i] = nullptr;
        createAtomicRequirement(i);
    }

    m_drm->connectAllDisplay();
    m_drm->dumpResourceInfo();

    m_max_overlay_num = m_drm->getMaxPlaneNum();
    m_drm_max_support_width = m_drm->getMaxSupportWidth();
    m_drm_max_support_height = m_drm->getMaxSupportHeight();

    for (unsigned int i = 0; i < DisplayManager::MAX_DISPLAYS; i++)
    {
        m_prev_commit_dcm_out_fb_id[i] = new std::pair<uint64_t, uint32_t>[NUM_DECOUPLE_FB_ID_BACKUP_SLOTS];
        for (unsigned int j = 0; j < NUM_DECOUPLE_FB_ID_BACKUP_SLOTS; j++)
        {
            m_prev_commit_dcm_out_fb_id[i][j] = std::make_pair(UINT64_MAX, 0);
        }
    }

    for (unsigned int i = 0; i < DisplayManager::MAX_DISPLAYS; i++)
    {
        m_prev_commit_color_transform[i] = 0;
    }

    queryCapsInfo();

    getMSyncDefaultParamTableInternal();

    m_trash_cleaner_thread = std::thread(&DrmDevice::trashCleanerLoop, this);
    if (pthread_setname_np(m_trash_cleaner_thread.native_handle(), "TrashCleaner")) {
        ALOGI("pthread_setname_np TrashCleaner fail");
    }

    // init color histogram
    m_drm_histogram.initState(m_drm, m_caps_info);
}

DrmDevice::~DrmDevice()
{
    for (unsigned int i = 0; i < DisplayManager::MAX_DISPLAYS; i++)
    {
        delete[] m_prev_commit_dcm_out_fb_id[i];
        releaseAtomicRequirement(i);
    }

    removeFbCacheAllDisplay();

    {
        std::lock_guard<std::mutex> lock(m_trash_mutex);
        m_trash_cleaner_thread_stop = true;
        m_condition.notify_all();
    }
    m_trash_cleaner_thread.join();
}

void DrmDevice::initOverlay()
{
    Platform::getInstance().initOverlay();
}

void DrmDevice::queryCapsInfo()
{
    memcpy(&m_caps_info, &DrmModeResource::getInstance().getCapsInfo(), sizeof(m_caps_info));

    HWC_LOGD("CapsInfo [0x%x] hw_ver", m_caps_info.hw_ver);
    HWC_LOGD("CapsInfo [%d] lcm_degree",        m_caps_info.lcm_degree);
    //HWC_LOGD("CapsInfo [%d] output_rotated",  m_caps_info.is_output_rotated);
    HWC_LOGD("CapsInfo [0x%x] disp_feature",      m_caps_info.disp_feature_flag);
    HWC_LOGD("CapsInfo [%d,%d,%d] rpo,rsz_in_max(w,h)",
            isDispRpoSupported(), m_caps_info.rsz_in_max[0], m_caps_info.rsz_in_max[1]);
    HWC_LOGD("CapsInfo [%d] dispRszSupported",   isDispRszSupported());
    HWC_LOGD("CapsInfo [%d] dispSelfRefreshSupported", isDispSelfRefreshSupported());
    HWC_LOGD("CapsInfo [%d] dispHrtSupport", isDisplayHrtSupport());
    HWC_LOGD("CapsInfo [%d] colorMode", m_caps_info.lcm_color_mode);
    HWC_LOGD("CapsInfo [%d,%d,%d] luminance(max,min,avg)",
            m_caps_info.max_luminance, m_caps_info.min_luminance, m_caps_info.average_luminance);
    HWC_LOGD("CapsInfo [%d] MMLPrimarySupported",
            m_caps_info.disp_feature_flag & DRM_DISP_FEATURE_MML_PRIMARY);
    HWC_LOGD("CapsInfo [%u, %d, %u, %u] hist_color_format, lcd_color_mode, max_bin, max_channel",
            m_caps_info.color_format, m_caps_info.lcm_color_mode,
            m_caps_info.max_bin, m_caps_info.max_channel);
    HWC_LOGD("CapsInfo [%d] msync_level_num", m_caps_info.msync_level_num);
}

unsigned int DrmDevice::getHwVersion()
{
    unsigned int version = PLATFORM_NOT_DEFINE;
    const mtk_drm_disp_caps_info& caps = DrmModeResource::getInstance().getCapsInfo();
    switch (caps.hw_ver)
    {
        case MMSYS_MT6885:
            version = PLATFORM_MT6885;
            break;

        case MMSYS_MT6983:
            version = PLATFORM_MT6983;
            break;

        case MMSYS_MT6879:
            version = PLATFORM_MT6879;
            break;

        case MMSYS_MT6895:
            version = PLATFORM_MT6895;
            break;

        case MMSYS_MT6855:
            version = PLATFORM_MT6855;
            break;

        case MMSYS_MT6985:
            version = PLATFORM_MT6985;
            break;

        case MMSYS_MT6886:
            version = PLATFORM_MT6886;
            break;

        default:
            HWC_LOGW("%s: unknown hw version: 0x%x", __FUNCTION__, caps.hw_ver);
            version = PLATFORM_NOT_DEFINE;
            break;
    }
    return version;
}

bool DrmDevice::isDispRszSupported()
{
    return false;
}

bool DrmDevice::isDispRpoSupported()
{
    return m_caps_info.disp_feature_flag & DRM_DISP_FEATURE_RPO;
}

bool DrmDevice::isPartialUpdateSupported()
{
    return false;
}

bool DrmDevice::isFenceWaitSupported()
{
    return false;
}

bool DrmDevice::isConstantAlphaForRGBASupported()
{
    return  true;
}

bool DrmDevice::isDispSelfRefreshSupported()
{
    return m_caps_info.disp_feature_flag & DRM_DISP_FEATURE_DISP_SELF_REFRESH;
}

bool DrmDevice::isDisplayHrtSupport()
{
    return m_caps_info.disp_feature_flag & DRM_DISP_FEATURE_HRT;
}

bool DrmDevice::isSupportMml(uint32_t drm_id_crtc)
{
    const DrmModeCrtc* crtc = m_drm->getCrtc(drm_id_crtc);
    if (!crtc)
    {
        return false;
    }

    return crtc->isSupportMml();
}

bool DrmDevice::isSupportDispPq(uint32_t drm_id_crtc)
{
    DrmModeCrtc* crtc = m_drm->getCrtc(drm_id_crtc);
    if (!crtc)
    {
        return false;
    }

    // Do not use main crtc to decide whether display support pq,
    // change to use pq_support_flag to get display pq capability.
    uint32_t pipeId = crtc->getPipe();
    if (pipeId == HWC_DISPLAY_PRIMARY)
        return m_caps_info.pq_support_flag & DRM_PQ_SUPPORT_CRTC0;
    else if (pipeId == HWC_DISPLAY_EXTERNAL)
        return m_caps_info.pq_support_flag & DRM_PQ_SUPPORT_CRTC1;
    else if (pipeId == HWC_DISPLAY_VIRTUAL)
        return m_caps_info.pq_support_flag & DRM_PQ_SUPPORT_CRTC2;
    else if (pipeId == HWC_DISPLAY_EXTERNAL_1)
        return m_caps_info.pq_support_flag & DRM_PQ_SUPPORT_CRTC3;
    else
        return false;
}

bool DrmDevice::isSupportWb(uint32_t drm_id_crtc)
{
    if (drm_id_crtc == UINT32_MAX)
    {
        return false;
    }

    const DrmModeCrtc* crtc = m_drm->getCrtc(drm_id_crtc);
    if (!crtc)
    {
        return false;
    }

    return crtc->isSupportWb();
}

bool DrmDevice::isSupportWbParams(uint32_t drm_id_crtc, uint32_t drm_id_connector, int dump_point,
                                  unsigned int src_w, unsigned int src_h,
                                  unsigned int dst_w, unsigned int dst_h,
                                  hwc2_config_t config)
{
    HWC_LOGI("%s(), id_crtc %d, id_connector %d, dump_point %d, src(%d, %d), dst(%d, %d), config %d",
             __FUNCTION__, drm_id_crtc, drm_id_connector, dump_point, src_w, src_h, dst_w, dst_h, config);

    if (drm_id_crtc == UINT32_MAX || drm_id_connector == UINT32_MAX)
    {
        return false;
    }

    const DrmModeCrtc* crtc = m_drm->getCrtc(drm_id_crtc);
    if (!crtc)
    {
        return false;
    }

    unsigned int disp_w = static_cast<unsigned int>(m_drm->getWidth(drm_id_connector, config));
    unsigned int disp_h = static_cast<unsigned int>(m_drm->getHeight(drm_id_connector, config));

    HWC_LOGE("%s(), disp(%d, %d)",__FUNCTION__, disp_w, disp_h);

    return crtc->isSupportWbParams(mapDispDumpPoint(dump_point), disp_w, disp_h, src_w, src_h, dst_w, dst_h);
}

bool DrmDevice::isSupportSeparateHrt()
{
    return m_caps_info.disp_feature_flag & DRM_DISP_FEATURE_SPHRT;
}

bool DrmDevice::isSupportDynamicSwitch()
{
    return m_caps_info.disp_feature_flag & DRM_DISP_FEATURE_DYNAMIC_OUTPUT;
}

unsigned int DrmDevice::getMaxOverlayInputNum()
{
    return m_max_overlay_num;
}

bool DrmDevice::isDisplaySupportedWidthAndHeight(unsigned int width, unsigned int height)
{
    if (static_cast<uint64_t>(width) > static_cast<uint64_t>(m_drm_max_support_width) ||
        static_cast<uint64_t>(height) > static_cast<uint64_t>(m_drm_max_support_height))
    {
        return false;
    }
    return true;
}

status_t DrmDevice::createOverlaySession(uint64_t dpy, uint32_t drm_id_crtc, uint32_t width, uint32_t height,
                                         HWC_DISP_MODE mode)
{
    CHECK_DPY_RET_STATUS(dpy);

    int err = m_display_state.checkDisplayStateMachine(dpy, DISPLAY_STATE_WAIT_TO_CREATE);
    if (err != NO_ERROR)
    {
        DLOGW(dpy, "create session with wrong state: %d->%d",
                m_display_state.getState(dpy), DISPLAY_STATE_WAIT_TO_CREATE);
        return INVALID_OPERATION;
    }

    DrmModeCrtc *crtc = m_drm->getCrtc(drm_id_crtc);
    if (crtc == nullptr)
    {
        HWC_LOGE("(%" PRIu64 ":%u) Failed to find crtc when %s", dpy, drm_id_crtc, __FUNCTION__);
        return INVALID_OPERATION;
    }

    if (crtc->getSessionId() != MTK_DRM_INVALID_SESSION_ID)
    {
        HWC_LOGW("(%" PRIu64 ") Failed to create existed DispSession (id=0x%x)", dpy, crtc->getSessionId());
        return INVALID_OPERATION;
    }

    drm_mtk_session config;
    memset(&config, 0, sizeof(config));

    config.type       = g_session_type[crtc->getPipe()];
    config.device_id  = getDeviceId(dpy);
    config.mode       = mapHwcDispMode2Drm(mode);
    config.session_id = MTK_DRM_INVALID_SESSION_ID;

    err = DRM_WDT_IOCTL(DRM_IOCTL_MTK_SESSION_CREATE, &config);
    if (err < 0)
    {
        crtc->setSessionId(MTK_DRM_INVALID_SESSION_ID);
        crtc->setSessionMode(MTK_DRM_SESSION_INVALID);

        IOLOGE(dpy, err, "DISP_IOCTL_CREATE_SESSION (%s), sid:0x%x",
               getSessionModeString(mode).string(), crtc->getSessionId());
        return BAD_VALUE;
    }

    crtc->setSessionId(config.session_id);
    crtc->setSessionMode(config.mode);

    m_display_state.changeDisplayState(dpy, DISPLAY_STATE_WAIT_TO_CREATE);
    m_display_state.setDrmIdCrtc(dpy, drm_id_crtc);

    crtc->setReqSize(width, height);

    // set overlay session mode for internal display (always plugin display)
    DrmModeEncoder *encoder = crtc->getEncoder();
    if (encoder)
    {
        DrmModeConnector *connector = encoder->getConnector();
        if (connector)
        {
            if (isSupportSeparateHrt())
            {
                activateDisplay(dpy, drm_id_crtc);
            }
            else
            {
                if (connector->getConnectorType() == DRM_MODE_CONNECTOR_DSI)
                {
                    setOverlaySessionModeInternal(dpy, HWC_DISP_SESSION_DIRECT_LINK_MODE, drm_id_crtc);
                }
            }
        }
        else
        {
            DLOGW(dpy, "%s(), no connector", __FUNCTION__);
        }
    }
    else
    {
        DLOGW(dpy, "%s(), no encoder", __FUNCTION__);
    }

    DLOGI(dpy, "Create Session (%s), sid:0x%x", getSessionModeString(mode).string(), crtc->getSessionId());

    return NO_ERROR;
}

void DrmDevice::destroyOverlaySession(uint64_t dpy, uint32_t drm_id_crtc)
{
    CHECK_DPY_RET_VOID(dpy);

    int err = m_display_state.checkDisplayStateMachine(dpy, DISPLAY_STATE_INACTIVE);
    if (err != NO_ERROR)
    {
        DLOGW(dpy, "destroy session with wrong state: %d->%d",
                m_display_state.getState(dpy), DISPLAY_STATE_INACTIVE);
        return;
    }

    err = m_drm->blankDisplay(dpy, drm_id_crtc, HWC_POWER_MODE_OFF);
    if (err)
    {
        HWC_LOGW("(%" PRIu64 ") Failed to power off display when destroy session", dpy);
    }

    DrmModeCrtc *crtc = m_drm->getCrtc(drm_id_crtc);
    if (crtc == nullptr)
    {
        HWC_LOGE("(%" PRIu64 ":%u) Failed to find crtc when %s", dpy, drm_id_crtc, __FUNCTION__);
        return;
    }

    unsigned int session_id = crtc->getSessionId();
    if (session_id == MTK_DRM_INVALID_SESSION_ID)
    {
        HWC_LOGW("(%" PRIu64 ") Failed to destroy invalid DispSession", dpy);
        return;
    }

    drm_mtk_session config;
    memset(&config, 0, sizeof(config));

    config.type       = g_session_type[dpy];
    config.device_id  = getDeviceId(dpy);
    config.session_id = session_id;

    err = DRM_WDT_IOCTL(DRM_IOCTL_MTK_SESSION_DESTROY, &config);
    if (err < 0)
    {
        IOLOGE(dpy, err, "DISP_IOCTL_DESTROY_SESSION, sid:0x%x", crtc->getSessionId());
    }

    crtc->setSessionId(MTK_DRM_INVALID_SESSION_ID);
    crtc->setSessionMode(MTK_DRM_SESSION_INVALID);

    m_display_state.changeDisplayState(dpy, DISPLAY_STATE_INACTIVE);
    m_display_state.setDrmIdCrtc(dpy, UINT32_MAX);

    // clear fb_id cache
    for (unsigned int i = 0; i < DisplayManager::MAX_DISPLAYS; i++)
    {
        for (unsigned int j = 0; j < NUM_DECOUPLE_FB_ID_BACKUP_SLOTS; j++)
        {
            if (m_prev_commit_dcm_out_fb_id[i][j].second != 0)
            {
                m_drm->removeFb(m_prev_commit_dcm_out_fb_id[i][j].second);
                HWC_LOGI("(%" PRIu64 ") remove plane[%d] fb_id:%d", dpy, i, m_prev_commit_dcm_out_fb_id[i][j].second);
                m_prev_commit_dcm_out_fb_id[i][j] = std::make_pair(UINT64_MAX, 0);
            }
        }
    }

    removeFbCacheDisplay(dpy);

    DLOGD(dpy, "Destroy DispSession, sid:0x%x", crtc->getSessionId());
}

bool DrmDevice::queryValidLayer(void* ptr)
{
    drm_mtk_layering_info* info = static_cast<drm_mtk_layering_info*>(ptr);
    int err = DRM_WDT_IOCTL(DRM_IOCTL_MTK_LAYERING_RULE, info);
    if (err < 0)
    {
        IOLOGE(uint64_t(HWC_DISPLAY_PRIMARY), err, "DRM_IOCTL_MTK_LAYERING_RULE: %d", err);
        return false;
    }

    return (info->hrt_num != -1) ? true : false;
}

status_t DrmDevice::triggerOverlaySession(uint64_t dpy, uint32_t drm_id_crtc, int present_fence_idx,
                                           int ovlp_layer_num, int /*prev_present_fence_fd*/,
                                           hwc2_config_t config, const uint32_t& /*hrt_weight*/,
                                           const uint32_t& hrt_idx, unsigned int num,
                                           OverlayPortParam* const* params,
                                           sp<ColorTransform> color_transform,
                                           TriggerOverlayParam trigger_param
                                          )
{
    CHECK_DPY_RET_STATUS(dpy);

    HWC_ATRACE_FORMAT_NAME("TrigerOVL:%d", present_fence_idx);
    status_t ret = NO_ERROR;

    DbgLogger logger(DbgLogger::TYPE_HWC_LOG, 'D', "(%" PRIu64 ") drmModeAtomicCommit: ", dpy);
    uint32_t blob_color_transform = 0;
    if (m_atomic_req[dpy] != nullptr)
    {
        uint32_t flags = DRM_MODE_ATOMIC_ALLOW_MODESET;

        DrmModeCrtc *crtc = m_drm->getCrtc(drm_id_crtc);
        if (!crtc)
        {
            HWC_LOGE("(%" PRIu64 ") drm state is wrong when triggerOverlaySession", dpy);
            return NO_INIT;
        }

        ret |= crtc->addProperty(m_atomic_req[dpy], DRM_PROP_CRTC_OVERLAP_LAYER_NUM,
                                 static_cast<uint64_t>(ovlp_layer_num)) < 0;
        ret |= crtc->addProperty(m_atomic_req[dpy], DRM_PROP_CRTC_LAYERING_IDX, hrt_idx) < 0;
        if (dpy == HWC_DISPLAY_VIRTUAL)
        {
            ret |= crtc->addProperty(m_atomic_req[dpy], DRM_PROP_CRTC_PRESENT_FENCE, 0) < 0;
        }
        else
        {
            ret |= crtc->addProperty(m_atomic_req[dpy], DRM_PROP_CRTC_PRESENT_FENCE,
                                     static_cast<uint64_t>(present_fence_idx)) < 0;
        }

        if (dpy != HWC_DISPLAY_VIRTUAL)
        {
            ret |= crtc->addProperty(m_atomic_req[dpy], DRM_PROP_CRTC_DISP_MODE_IDX, config) < 0;
            //HRT index is zero, that means HWC want to disable all plane, need to hint driver this behavior
            if (hrt_idx == 0)
            {
                ret |= crtc->addProperty(m_atomic_req[dpy], DRM_PROP_CRTC_USER_SCEN, 1) < 0;
            }
            else
            {
                ret |= crtc->addProperty(m_atomic_req[dpy], DRM_PROP_CRTC_USER_SCEN, 0) < 0;
            }

            if (m_caps_info.disp_feature_flag & DRM_DISP_FEATURE_MSYNC2_0)
            {
                updateMSyncEnable(dpy, trigger_param.package, trigger_param.late_package);
                ret |= crtc->addProperty(m_atomic_req[dpy],
                                         DRM_PROP_CRTC_MSYNC_2_0_ENABLE,
                                         static_cast<uint64_t>(m_msync2_enable[dpy])) < 0;
                updateMSyncParamTable(dpy, trigger_param.package, trigger_param.late_package);
            }
        }

#ifdef MTK_IN_DISPLAY_FINGERPRINT
        if (dpy == HWC_DISPLAY_PRIMARY)
        {
            ret |= crtc->addProperty(m_atomic_req[dpy], DRM_PROP_CRTC_HBM_ENABLE, trigger_param.is_HBM) < 0;
            logger.printf("is_hbm:%d ", trigger_param.is_HBM);
        }
#endif

        // ToDo: check HDR setting
#ifdef MTK_HDR_SET_DISPLAY_COLOR
        if (dpy == HWC_DISPLAY_PRIMARY)
        {
            ret |= crtc->addProperty(m_atomic_req[dpy], DRM_PROP_CRTC_HDR_ENABLE,
                                     static_cast<uint64_t>(trigger_param.is_HDR)) < 0;
            logger.printf("is_hdr:%d ", trigger_param.is_HDR);
        }
#endif

        if ( Platform::getInstance().m_config.support_color_transform &&
             dpy < DisplayManager::MAX_DISPLAYS)
        {
            if (color_transform != nullptr && color_transform->dirty)
            {
                status_t res = createColorTransformBlob(dpy, color_transform, &blob_color_transform);
                if (res < 0)
                {
                    HWC_LOGE("(%" PRIu64 ") failed to create a blob of color transform", dpy);
                }
            }

            if (blob_color_transform != 0)
            {
                ret |= crtc->addProperty(m_atomic_req[dpy], DRM_PROP_CRTC_COLOR_TRANSFORM, blob_color_transform) < 0;
            }
        }

        //used in PROFILE_DBG_WFD/ BWMonitor/ GPU Cache
        ret |= crtc->addProperty(m_atomic_req[dpy], DRM_PROP_CRTC_OVL_DSI_SEQ,
                          static_cast<uint32_t>(trigger_param.ovl_seq % UINT32_MAX));

        ret = m_drm->atomicCommit(m_atomic_req[dpy], flags, nullptr);
        if (ret)
        {

            HWC_LOGE("(%" PRIu64 ") failed to drmModeAtomicCommit: ret=%d job:%" PRIu64 " ovlp:%d pf_idx:%d hrt_idx:%d mode:%d",
                     dpy, ret, trigger_param.ovl_seq, ovlp_layer_num, present_fence_idx, hrt_idx, config);
        }
        else
        {
            logger.printf("sid:0x%x job:%" PRIu64 " ovlp:%d pf_idx:%d hrt_idx:%d mode:%d",
                    crtc->getSessionId(), trigger_param.ovl_seq, ovlp_layer_num, present_fence_idx,
                    hrt_idx, config);

        }

        releaseAtomicRequirement(dpy);
        createAtomicRequirement(dpy);
    }

    // handle pending remove cache
    trashAddFbId(m_fb_caches[dpy].fb_caches_pending_remove);
    m_fb_caches[dpy].fb_caches_pending_remove.clear();

    // remove unused cache
    if (trigger_param.package && trigger_param.package->m_need_free_fb_cache)
    {
        HWC_ATRACE_FORMAT_NAME("fb_id_remove_cache");
        std::lock_guard<std::mutex> l(m_layer_caches_mutex[dpy]);
        m_fb_caches[dpy].layer_caches.remove_if(
            [&](FbCacheInfo& cache)
            {
                for (unsigned int i = 0; params != NULL && i < getMaxOverlayInputNum() && i < num; i++)
                {
                    OverlayPortParam* param = params[i];
                    if (param->state == OVL_IN_PARAM_ENABLE && param->hwc_layer_id == cache.id)
                    {
                        // someone in this frame is still using the cache
                        return false;
                    }
                }
                HWC_ATRACE_FORMAT_NAME("remove_cache, hwc_layer_id %" PRIu64 ", size %zu",
                                       cache.id, cache.fb_caches.size());
                // no one use this cache in this frame, remove cache
                trashAddFbId(cache.fb_caches);
                return true;
            });
    }

    if (getMaxOverlayInputNum() < num)
    {
        HWC_LOGW("(%" PRIu64 ") triggerOverlaySession: params size(%u) is more than input num(%d)",
                dpy, num, getMaxOverlayInputNum());
    }

    if (blob_color_transform != 0 && blob_color_transform != m_prev_commit_color_transform[dpy])
    {
        status_t res = destroyBlob(m_prev_commit_color_transform[dpy]);
        if (res < 0)
        {
            HWC_LOGE("(%" PRIu64 ") failed to destroy blob(%u) of color transform: %d",
                    dpy, m_prev_commit_color_transform[dpy], res);
        }
        m_prev_commit_color_transform[dpy] = blob_color_transform;
    }

    return ret;
}

void DrmDevice::disableOverlaySession(
    uint64_t dpy, uint32_t drm_id_crtc, OverlayPortParam* const* /*params*/, unsigned int /*num*/)
{
    CHECK_DPY_RET_VOID(dpy);

    DrmModeCrtc *crtc = m_drm->getCrtc(drm_id_crtc);
    if (!crtc) {
        HWC_LOGE("(%" PRIu64 ") drm state is wrong when %s", dpy, __FUNCTION__);
        return;
    }

    {
        DbgLogger logger(DbgLogger::TYPE_HWC_LOG, 'D', "(%" PRIu64 ") %s ", dpy, __FUNCTION__);
        size_t plane_size = crtc->getPlaneNum();
        for (size_t i = 0; i < plane_size; i++) {
            auto plane = crtc->getPlane(i);
            if (CC_UNLIKELY(!plane)) {
                HWC_LOGE("%s(), (%" PRIu64 ") plane[%zu] == null",
                         __FUNCTION__, dpy, i);
                continue;
            }

            status_t ret = NO_ERROR;
            ret = disablePlane(m_atomic_req[dpy], plane);
            if (ret) {
                HWC_LOGE("(%" PRIu64 ") failed to disable plane[%zu]", dpy, i);
            }
            logger.printf("-%zu,pid=%d/ ", i, plane->getId());
        }
    }

    disableCrtcOutput(m_atomic_req[dpy], crtc);
    triggerOverlaySession(dpy, drm_id_crtc, 0, 0, -1, 0, 0, 0, 0, nullptr, nullptr, {});
    DLOGD(dpy, "Disable DispSession, sid:0x%x", crtc->getSessionId());
}

status_t DrmDevice::setOverlaySessionMode(uint64_t dpy, HWC_DISP_MODE mode)
{
    return setOverlaySessionModeInternal(dpy, mode, UINT32_MAX);
}

void DrmDevice::activateDisplay(uint64_t dpy, uint32_t set_display_id_crtc)
{
    CHECK_DPY_RET_VOID(dpy);

    if (m_display_state.getState(dpy) == DISPLAY_STATE_WAIT_TO_CREATE)
    {
        uint32_t id_crtc = (set_display_id_crtc == UINT32_MAX) ? m_display_state.getDrmIdCrtc(dpy) : set_display_id_crtc;
        int ret = m_drm->setDisplay(dpy, id_crtc, dpy == HWC_DISPLAY_VIRTUAL);
        if (ret)
        {
            DLOGE(dpy, "setDisplay_%" PRIu64 " fail, ret %d, id_crtc %u", dpy, ret, id_crtc);
            abort();
        }
        m_display_state.changeDisplayState(dpy, DISPLAY_STATE_ACTIVE);
    }
}

status_t DrmDevice::setOverlaySessionModeInternal(uint64_t dpy, HWC_DISP_MODE mode, uint32_t set_display_id_crtc)
{
    CHECK_DPY_RET_STATUS(dpy);

    status_t res = NO_ERROR;
    unsigned int session_mode = MTK_DRM_SESSION_INVALID;

    unsigned int total_display = m_display_state.getNumberActiveDisplay() +
                                 m_display_state.getNumberWaitedDisplay();
    if (mode == HWC_DISP_SESSION_DECOUPLE_MIRROR_MODE)
    {
        switch (total_display)
        {
            case 2:
                session_mode = MTK_DRM_SESSION_DC_MIRROR;
                break;

            default:
                DLOGW(dpy, "Do not define the behavior of MIRROR with %d displays", total_display);
                res = INVALID_OPERATION;
                break;
        }
    }
    else if (mode == HWC_DISP_SESSION_DIRECT_LINK_MODE)
    {
        switch (total_display)
        {
            case 1:
                session_mode = MTK_DRM_SESSION_DL;
                break;

            case 2:
                session_mode = MTK_DRM_SESSION_DOUBLE_DL;
                break;

            case 3:
                session_mode = MTK_DRM_SESSION_TRIPLE_DL;
                break;

            default:
                DLOGW(dpy, "Do not define the behavior of DL with %d displays", total_display);
                res = INVALID_OPERATION;
                break;
        }
    }
    else
    {
        DLOGE(dpy, "set session mode with invalid mode: %d", mode);
        res = INVALID_OPERATION;
    }

    // assume set overlay session is only for main crtc
    DrmModeCrtc* main_crtc = m_drm->getCrtcMain();
    if (!main_crtc)
    {
        DLOGW(dpy, "no main_crtc");
        return INVALID_OPERATION;
    }

    int err = 0;
    if (res != INVALID_OPERATION && (main_crtc->getSessionMode() != session_mode ||
            m_display_state.getNumberWaitedDisplay() != 0))
    {
        err = m_drm->ioctl(DRM_IOCTL_MTK_SET_DDP_MODE, &session_mode);
        if (err < 0)
        {
            IOLOGE(uint64_t(HWC_DISPLAY_PRIMARY), err, "DRM_IOCTL_MTK_SET_DDP_MODE");
            res = INVALID_OPERATION;
        }
        else
        {
            main_crtc->setSessionMode(session_mode);
        }

        if (set_display_id_crtc != UINT32_MAX)
        {
            // for internal display crtc, always plugin
            if (m_display_state.getState(dpy) == DISPLAY_STATE_WAIT_TO_CREATE)
            {
                int ret = m_drm->setDisplay(dpy, set_display_id_crtc, false);
                if (ret)
                {
                    DLOGE(dpy, "setDisplay_%" PRIu64 " fail, ret %d", dpy, ret);
                    abort();
                }
                m_display_state.changeDisplayState(dpy, DISPLAY_STATE_ACTIVE);
            }
        }
        else
        {
            // for non internal display, ex. virtua/external
            if (m_display_state.getNumberWaitedDisplay())
            {
                for (unsigned int i = 0; i < DisplayManager::MAX_DISPLAYS; i++)
                {
                    if (m_display_state.getState(i) == DISPLAY_STATE_WAIT_TO_CREATE)
                    {
                        uint32_t id_crtc = m_display_state.getDrmIdCrtc(i);
                        int ret = m_drm->setDisplay(i, id_crtc, (i == HWC_DISPLAY_VIRTUAL) ? true : false);
                        if (ret)
                        {
                            DLOGE(dpy, "setDisplay_%d fail, ret %d, id_crtc %u", i, ret, id_crtc);
                            abort();
                        }
                        m_display_state.changeDisplayState(i, DISPLAY_STATE_ACTIVE);
                    }
                }
            }
        }
    }

    return res;
}

HWC_DISP_MODE DrmDevice::getOverlaySessionMode(uint64_t dpy, uint32_t drm_id_crtc)
{
    if (!CHECK_DPY_VALID(dpy))
    {
        HWC_LOGE("%s(), invalid dpy %" PRIu64 "", __FUNCTION__, dpy);
        return HWC_DISP_INVALID_SESSION_MODE;
    }

    DrmModeCrtc *crtc = m_drm->getCrtc(drm_id_crtc);
    if (crtc == nullptr)
    {
        HWC_LOGE("(%" PRIu64 ":%u) Failed to find crtc when %s", dpy, drm_id_crtc, __FUNCTION__);
        return HWC_DISP_INVALID_SESSION_MODE;
    }

    return mapDispMode2Hwc(crtc->getSessionMode());
}

status_t DrmDevice::getOverlaySessionInfo(uint64_t dpy, uint32_t drm_id_crtc, SessionInfo* info)
{
    CHECK_DPY_RET_STATUS(dpy);

    DrmModeCrtc *crtc = m_drm->getCrtc(drm_id_crtc);
    if (!crtc)
    {
        HWC_LOGE("(%" PRIu64 ":%u) drm state is wrong when %s", dpy, drm_id_crtc, __FUNCTION__);
        return -ENODEV;
    }

    unsigned int session_id = crtc->getSessionId();
    if (session_id == MTK_DRM_INVALID_SESSION_ID)
    {
        HWC_LOGW("(%" PRIu64 ":%u) Failed to get invalid session info", dpy, drm_id_crtc);
        return INVALID_OPERATION;
    }

    drm_mtk_session_info session_info;
    memset(&session_info, 0, sizeof(session_info));

    session_info.session_id = session_id;
    int err = DRM_WDT_IOCTL(DRM_IOCTL_MTK_GET_SESSION_INFO, &session_info);
    if (err < 0)
    {
        IOLOGE(dpy, err, "DRM_IOCTL_MTK_GET_SESSION_INFO, sid:0x%x", session_id);
    }

    info->maxLayerNum = static_cast<unsigned int>(crtc->getPlaneNum());
    info->isHwVsyncAvailable = 1;
    info->displayType = HWC_DISP_IF_TYPE_DSI0;
    info->displayWidth = crtc->getVirWidth();
    info->displayHeight = crtc->getVirHeight();
    info->displayFormat = 2;
    info->displayMode = HWC_DISP_IF_MODE_VIDEO;
    info->vsyncFPS = session_info.vsyncFPS;
    info->physicalWidth = crtc->getPhyWidth();
    info->physicalHeight = crtc->getPhyHeight();
    info->physicalWidthUm = session_info.physicalWidthUm;
    info->physicalHeightUm = session_info.physicalHeightUm;
    info->density = 0;
    info->isConnected = 1;
    info->isHDCPSupported = 0;

    return NO_ERROR;
}

unsigned int DrmDevice::getAvailableOverlayInput(uint64_t dpy, uint32_t drm_id_crtc)
{
    DrmModeCrtc *crtc = m_drm->getCrtc(drm_id_crtc);
    if (!crtc)
    {
        HWC_LOGE("(%" PRIu64 ":%u), %s(), drm state is wrong", dpy, drm_id_crtc, __FUNCTION__);
        return 1;
    }

    return static_cast<unsigned int>(crtc->getPlaneNum());
}

void DrmDevice::submitMML(uint64_t dpy, struct mml_submit& params)
{
    int err = DRM_WDT_IOCTL(DRM_IOCTL_MTK_MML_GEM_SUBMIT, &params);
    if (err < 0)
    {
        IOLOGE(dpy, err, "DRM_IOCTL_MTK_MML_GEM_SUBMIT");
    }
}

const MSync2Data::ParamTable* DrmDevice::getMSyncDefaultParamTable()
{
    return &m_msync_param_table;
}

void DrmDevice::updateDisplayConnection(uint64_t dpy, uint32_t connector_type)
{
    m_drm->updateDisplayConnection(dpy, connector_type);
}

void DrmDevice::getCreateDisplayInfos(std::vector<CreateDisplayInfo>& create_display_infos)
{
    m_drm->getCreateDisplayInfos(create_display_infos);
}

void DrmDevice::prepareOverlayInput(
    uint64_t dpy, OverlayPrepareParam* param)
{
    CHECK_DPY_RET_VOID(dpy);
    if (param == nullptr)
    {
        HWC_LOGE("(%" PRIu64 ") OverlayPrepareParam is nullptr when %s", dpy, __FUNCTION__);
        return;
    }

    DrmModeCrtc *crtc = m_drm->getCrtc(param->drm_id_crtc);
    if (crtc == nullptr)
    {
        HWC_LOGE("(%" PRIu64 ":%u) Failed to find crtc when %s", dpy, param->drm_id_crtc, __FUNCTION__);
        return;
    }

    unsigned int session_id = crtc->getSessionId();
    struct drm_mtk_gem_submit submit;

    memset(&submit, 0, sizeof(submit));
    submit.type = MTK_SUBMIT_IN_FENCE;
    submit.session_id = session_id;
    submit.layer_id = param->id;
    submit.ion_fd = param->ion_fd;
    submit.layer_en = 1;
    submit.fb_id = 0;
    submit.index = static_cast<uint32_t>(-1);
    submit.fence_fd = -1;

    int err = DRM_WDT_IOCTL(DRM_IOCTL_MTK_GEM_SUBMIT, &submit);
    if (err < 0)
    {
        IOLOGE(dpy, err, "DRM_IOCTL_MTK_GEM_SUBMIT, sid:0x%x", session_id);
    }
    param->fence_index = submit.index;
    param->fence_fd = dupCloseAndSetFd(&submit.fence_fd);

    HWC_ATRACE_BUFFER_INFO("pre_input",
        dpy, param->id, param->fence_index, param->fence_fd);

    HWC_LOGV("DrmDevice::prepareOverlayInput() dpy:%" PRIu64 " id:%u fence_idx:%d fence_fd:%d ion_fd:%d",
        dpy, param->id, param->fence_index, param->fence_fd, param->ion_fd);

    if (getEthdrSupport(dpy) && param->ext_video_frame) {
        HWC_LOGD("EXT metadata send to DRM");
        HWC_LOGD("HDR_TYPE: %d", param->ethdr_meta.hdrType);
        HWC_LOGD("plane id: %d", param->ethdr_meta.planeId);
        HWC_LOGD("meta fd: %d", param->ethdr_meta.DVMetadataBufferFd);
        HWC_LOGD("meta fd: %d", param->ethdr_meta.isSecure);
        err = DRM_WDT_IOCTL(DRM_IOCTL_MTK_ETHDR_SET_METADATA, &param->ethdr_meta);
        if (err < 0)
        {
            IOLOGE(dpy, err, "DRM_MTK_ETHDR_SET_METADATA");
        }
    }
}

void DrmDevice::updateOverlayInputs(uint64_t dpy, uint32_t drm_id_crtc,
                                    OverlayPortParam* const* params,
                                    unsigned int num,
                                    sp<ColorTransform> /*color_transform*/)
{
    CHECK_DPY_RET_VOID(dpy);

    static bool bLogo = true;
    char value[PROPERTY_VALUE_MAX];

    HWC_ATRACE_CALL();
    DrmModeCrtc *crtc = m_drm->getCrtc(drm_id_crtc);
    if (!crtc)
    {
        HWC_LOGE("(%" PRIu64 ") drm state is wrong when updateOverlayInputs", dpy);
        return;
    }

    size_t i;
    if (bLogo)
    {
        property_get("service.bootanim.exit", value, "-1");
        if (-1 != atoi(value) && atoi(value) == 1)
        {
            for (i = 0; i < DisplayManager::MAX_DISPLAYS; i++)
            {
                uint32_t id_crtc = m_display_state.getDrmIdCrtc(i);
                if (id_crtc == UINT32_MAX)
                    continue;

                DrmModeCrtc *c = m_drm->getCrtc(id_crtc);
                if (c == nullptr)
                    continue;

                status_t ret = NO_ERROR;
                ret = c->addProperty(m_atomic_req[dpy], DRM_PROP_CRTC_BOOTLOGO_DISABLE, 1);
                if (ret)
                    HWC_LOGE("(%" PRIu64 ") failed to notify bootlog disable: ret=%d", dpy, ret);
            }
            bLogo = false;
            HWC_LOGI("Animation stop. notify bootlogo disable!");
        }
    }

    DbgLogger logger(DbgLogger::TYPE_HWC_LOG, 'D', "(%" PRIu64 ") Input: ", dpy);
    size_t plane_size = crtc->getPlaneNum();
    for (i = 0; i < num; i++)
    {
        status_t ret = NO_ERROR;
        OverlayPortParam* param = params[i];
        if (i >= plane_size)
        {
            HWC_LOGW("(%" PRIu64 ") layer number(%u) is over the number of planes(%zu)",
                    dpy, num, plane_size);
            break;
        }

        auto plane = crtc->getPlane(i);
        if (CC_UNLIKELY(!plane)) {
            HWC_LOGE("%s(), (%" PRIu64 ") plane[%zu] == null",
                     __FUNCTION__, dpy, i);
            continue;
        }

        if (param->state == OVL_IN_PARAM_DISABLE)
        {
            ret = disablePlane(m_atomic_req[dpy], plane);
            if (ret)
            {
                HWC_LOGE("(%" PRIu64 ") failed to disable plane[%zu] id:%d", dpy, i, plane->getId());
            }
            logger.printf("-%zu,fidx:%d/ ", i, param->fence_index);
            continue;
        }

        if (param->src_crop.getWidth() < 1 ||
            param->src_crop.getHeight() < 1 ||
            param->dst_crop.getWidth() < 1 ||
            param->dst_crop.getHeight() < 1)
        {
            HWC_LOGW("(%" PRIu64 ") disable plane[%zu] pid:%d fidx=%d, w/h=0",
                    dpy, i, plane->getId(), param->fence_index);
            ret = disablePlane(m_atomic_req[dpy], plane);
            if (ret)
            {
                HWC_LOGE("(%" PRIu64 ") failed to disable plane[%d]", dpy, plane->getId());
            }
            continue;
        }

        if (param->dim)
        {
            ret |= plane->addProperty(m_atomic_req[dpy], DRM_PROP_PLANE_FB_ID, m_drm->getDimFbId()) < 0;
            ret |= plane->addProperty(m_atomic_req[dpy], DRM_PROP_PLANE_DIM_COLOR, param->layer_color) < 0;
        }
        else
        {
            // getLayerCacheForId() does not modify layer_caches, so we do not use
            // m_layer_caches_mutex to protect layer_caches in here.
            FbCacheInfo *layer_cache = m_fb_caches[dpy].getLayerCacheForId(param->hwc_layer_id);
            {
                HWC_ATRACE_FORMAT_NAME("fb_id_from_cache %zu, hwc_layer_id %" PRIu64 "", i, param->hwc_layer_id);

                if (layer_cache)
                {
                    for (FbCacheEntry& entry : layer_cache->fb_caches)
                    {
                        if (entry.alloc_id == param->alloc_id &&
                            entry.format == param->format)
                        {
                            param->fb_id = entry.fb_id;
                            entry.used_at_count = layer_cache->count;
                            HWC_LOGV("cache[%zu] fb_id:%d", i, param->fb_id);
                            break;
                        }
                    }
                }
            }
            // handle fb cache life cycle
            if (layer_cache)
            {
                std::lock_guard<std::mutex> l(m_layer_caches_mutex[dpy]);
                // layer fps is lower than 1 fps, only cache 1 fb
                const nsecs_t now = systemTime(CLOCK_MONOTONIC);
                if (now - layer_cache->last_buf_update > s2ns(1))
                {
                    m_fb_caches[dpy].moveFbCachesToRemoveExcept(layer_cache, param->fb_id);
                }

                // buf has updated
                if (layer_cache->last_alloc_id != param->alloc_id)
                {
                    layer_cache->count++;
                    layer_cache->last_buf_update = systemTime(CLOCK_MONOTONIC);
                    layer_cache->last_alloc_id = param->alloc_id;

                    // remove long not used fb cache
                    uint64_t remove_threshold = layer_cache->fb_caches.size() * 2;
                    layer_cache->fb_caches.remove_if(
                        [&](FbCacheEntry& entry)
                        {
                            if (layer_cache->count - entry.used_at_count > remove_threshold)
                            {
                                trashAddFbId(entry);
                                return true;
                            }
                            return false;
                        });
                }
            }

            if (param->fb_id == 0)
            {
                createFbId(param, dpy, i);
                if (param->fb_id == 0)
                {
                    // Don't add plane when createFbIdFb with fb_id = 0
                    HWC_LOGW("(%" PRIu64 ") disable plane[%zu] pid:%d fidx=%d, fb_id=0",
                             dpy, i, plane->getId(), param->fence_index);
                    ret = disablePlane(m_atomic_req[dpy], plane);
                    if (ret)
                    {
                        HWC_LOGE("(%" PRIu64 ") failed to disable plane[%d]", dpy, plane->getId());
                    }
                    continue;
                }

                // cache the fb_id
                {
                    HWC_ATRACE_FORMAT_NAME("fb_id_add_cache %zu, hwc_layer_id %" PRIu64 "", i, param->hwc_layer_id);
                    std::lock_guard<std::mutex> l(m_layer_caches_mutex[dpy]);
                    if (!layer_cache)
                    {
                        m_fb_caches[dpy].layer_caches.emplace_back(param);
                        layer_cache = &m_fb_caches[dpy].layer_caches.back();
                    }

                    if (layer_cache->fb_caches.size() > 20)
                    {
                        HWC_LOGW("(%" PRIu64 ") hwc_layer_id %" PRIu64 ", fb_caches size %zu too big",
                                 dpy, param->hwc_layer_id, layer_cache->fb_caches.size());
                        m_fb_caches[dpy].moveFbCachesToRemove(layer_cache);
                    }

                    if (!layer_cache->paramIsSame(param))
                    {
                        layer_cache->updateParam(param);
                        m_fb_caches[dpy].moveFbCachesToRemove(layer_cache);
                    }

                    if (layer_cache->fb_caches.size() >= 1 &&
                        ((Platform::getInstance().m_config.plat_switch & HWC_PLAT_SWITCH_MULTIPLE_FB_CACHE) == 0 ||
                        param->secure))
                    {
                        m_fb_caches[dpy].moveFbCachesToRemove(layer_cache->fb_caches.back());
                        layer_cache->fb_caches.back() = {param->alloc_id, param->fb_id,
                                                         param->format, layer_cache->count};
                    }
                    else
                    {
                        layer_cache->fb_caches.emplace_back(FbCacheEntry{param->alloc_id, param->fb_id,
                                                                         param->format, layer_cache->count});
                    }
                }
            }
            ret |= plane->addProperty(m_atomic_req[dpy], DRM_PROP_PLANE_FB_ID, param->fb_id) < 0;
        }
        ret |= plane->addProperty(m_atomic_req[dpy], DRM_PROP_PLANE_CRTC_ID, crtc->getId()) < 0;
        ret |= plane->addProperty(m_atomic_req[dpy], DRM_PROP_PLANE_CRTC_X,
                                  static_cast<uint64_t>(param->dst_crop.left)) < 0;
        ret |= plane->addProperty(m_atomic_req[dpy], DRM_PROP_PLANE_CRTC_Y,
                                  static_cast<uint64_t>(param->dst_crop.top)) < 0;
        ret |= plane->addProperty(m_atomic_req[dpy], DRM_PROP_PLANE_CRTC_W,
                                  static_cast<uint64_t>(param->dst_crop.getWidth())) < 0;
        ret |= plane->addProperty(m_atomic_req[dpy], DRM_PROP_PLANE_CRTC_H,
                                  static_cast<uint64_t>(param->dst_crop.getHeight())) < 0;
        ret |= plane->addProperty(m_atomic_req[dpy], DRM_PROP_PLANE_SRC_X,
                                  static_cast<uint64_t>(param->src_crop.left) << 16) < 0;
        ret |= plane->addProperty(m_atomic_req[dpy], DRM_PROP_PLANE_SRC_Y,
                                  static_cast<uint64_t>(param->src_crop.top) << 16) < 0;
        ret |= plane->addProperty(m_atomic_req[dpy], DRM_PROP_PLANE_SRC_W,
                                  static_cast<uint64_t>(param->src_crop.getWidth()) << 16) < 0;
        ret |= plane->addProperty(m_atomic_req[dpy], DRM_PROP_PLANE_SRC_H,
                                  static_cast<uint64_t>(param->src_crop.getHeight()) << 16) < 0;
        ret |= plane->addProperty(m_atomic_req[dpy], DRM_PROP_PLANE_NEXT_BUFFER_IDX, param->fence_index) < 0;

        if (Platform::getInstance().m_config.is_bw_monitor_support)
        {
            ret |= plane->addProperty(m_atomic_req[dpy], DRM_PROP_PLANE_BUFFER_ALLOC_ID, param->alloc_id) < 0;// add for BW Monitor
        }

        ret |= plane->addProperty(m_atomic_req[dpy], DRM_PROP_PLANE_DATASPACE, static_cast<uint64_t>(param->dataspace)) < 0;

        ret |= plane->addProperty(m_atomic_req[dpy], DRM_PROP_PLANE_VPITCH, param->v_pitch) < 0;
        ret |= plane->addProperty(m_atomic_req[dpy], DRM_PROP_PLANE_COMPRESS, param->compress) < 0;
        ret |= plane->addProperty(m_atomic_req[dpy], DRM_PROP_PLANE_PLANE_ALPHA, param->alpha) < 0;
        ret |= plane->addProperty(m_atomic_req[dpy], DRM_PROP_PLANE_ALPHA_CON, param->alpha_enable) < 0;
        ret |= plane->addProperty(m_atomic_req[dpy], DRM_PROP_PLANE_IS_MML,
                                    (param->is_mml)? 1 : 0) < 0;
        if (param->is_mml)
        {
            void* addr = reinterpret_cast<void*>(param->mml_cfg);
            uint64_t addr2 = reinterpret_cast<uintptr_t>(addr);
            ret |= plane->addProperty(m_atomic_req[dpy], DRM_PROP_PLANE_MML_SUBMIT, addr2) < 0;
        }

        if (ret)
        {
            HWC_LOGE("(%" PRIu64 ") Failed to add plane[%zu] to set: pid:%d fence_idx:%d\n", dpy, i, plane->getId(), param->fence_index);
        }
        else
        {
            logger.printf("+%zu,pid:%d,fidx:%d,fb_id:%d/ ",
                    i, plane->getId(), param->fence_index, param->fb_id);
            DbgLogger* ovl_logger = &Debugger::getInstance().m_logger->ovlInput[static_cast<size_t>(dpy)][i];
            ovl_logger->printf("(%" PRIu64 ":%zu) Layer+ ("
                    "mva=%p/sec=%d/prot=%d/fmt=%x/"
                    "alpha=%d:0x%02x/blend=%04x/dim=%d/"
                    "x=%d y=%d w=%d h=%d s=%d -> x=%d y=%d w=%d h=%d/"
                    "ext_layer=%d ds=%d fbdc=%d is_mml=%d)",
                    dpy, i, param->mva, param->secure, param->protect, param->format,
                    param->alpha_enable, param->alpha, param->blending,
                    param->dim,
                    param->src_crop.left, param->src_crop.top,
                    param->src_crop.getWidth(), param->src_crop.getHeight(), param->pitch,
                    param->dst_crop.left, param->dst_crop.top,
                    param->dst_crop.getWidth(), param->dst_crop.getHeight(),
                    param->ext_sel_layer, param->dataspace, param->compress, param->is_mml);
            ovl_logger->tryFlush();
        }
    }

    // if some planes are not set by previous flow, disable they in here
    for (; i < plane_size; i++)
    {
        auto plane = crtc->getPlane(i);
        if (CC_UNLIKELY(!plane)) {
            HWC_LOGE("%s(), (%" PRIu64 ") plane[%zu] == null",
                     __FUNCTION__, dpy, i);
            continue;
        }

        status_t ret = NO_ERROR;
        ret = disablePlane(m_atomic_req[dpy], plane);
        if (ret)
        {
            HWC_LOGE("(%" PRIu64 ") failed to disable plane[%zu]: pid:%d", dpy, i, plane->getId());
        }
    }
}

void DrmDevice::prepareOverlayOutput(uint64_t dpy, OverlayPrepareParam* param)
{
    CHECK_DPY_RET_VOID(dpy);
    if (param == nullptr)
    {
        HWC_LOGE("(%" PRIu64 ") OverlayPrepareParam is nullptr when %s", dpy, __FUNCTION__);
        return;
    }

    DrmModeCrtc *crtc = m_drm->getCrtc(param->drm_id_crtc);
    if (crtc == nullptr)
    {
        HWC_LOGE("(%" PRIu64 ":%u) Failed to find crtc when %s", dpy, param->drm_id_crtc, __FUNCTION__);
        return;
    }

    unsigned int session_id = crtc->getSessionId();
    struct drm_mtk_gem_submit submit;

    memset(&submit, 0, sizeof(submit));
    submit.type = MTK_SUBMIT_OUT_FENCE;
    submit.session_id = session_id;
    submit.layer_id = param->id;
    submit.ion_fd = param->ion_fd;
    submit.layer_en = 1;
    submit.fb_id = 0;
    submit.index = static_cast<uint32_t>(-1);
    submit.fence_fd = -1;
    submit.interface_index = static_cast<uint32_t>(-1);
    submit.interface_fence_fd = -1;

    int err = DRM_WDT_IOCTL(DRM_IOCTL_MTK_GEM_SUBMIT, &submit);
    if (err < 0)
    {
        IOLOGE(dpy, err, "DRM_IOCTL_MTK_GEM_SUBMIT, sid:0x%x", session_id);
    }
    param->fence_index = submit.index;
    param->fence_fd = dupCloseAndSetFd(&submit.fence_fd);
    param->if_fence_index = submit.interface_index;
    param->if_fence_fd = dupCloseAndSetFd(&submit.interface_fence_fd);

    HWC_ATRACE_BUFFER_INFO("pre_output",
        dpy, param->id, param->fence_index, param->fence_fd);

    HWC_LOGV("DrmDevice::prepareOverlayOutput() dpy:%" PRIu64 " id:%u fence_idx:%d fence_fd:%d if_fence_index:%d if_fence_fd:%d ion_fd:%d",
             dpy, param->id, param->fence_index, param->fence_fd,
             param->if_fence_index, param->if_fence_fd, param->ion_fd);
}

void DrmDevice::disableOverlayOutput(uint64_t dpy, uint32_t drm_id_crtc)
{
    CHECK_DPY_RET_VOID(dpy);

    DrmModeCrtc *crtc = m_drm->getCrtc(drm_id_crtc);
    if (!crtc)
    {
        HWC_LOGE("(%" PRIu64 ") drm state is wrong when disableOverlayOutput", dpy);
        return;
    }

    status_t ret = disableCrtcOutput(m_atomic_req[dpy], crtc);
    if (ret)
    {
        HWC_LOGE("(%" PRIu64 ") failed to disable crtc output id:%d", dpy, crtc->getId());
    }
}

void DrmDevice::enableOverlayOutput(uint64_t dpy, uint32_t drm_id_crtc, OverlayPortParam* param)
{
    CHECK_DPY_RET_VOID(dpy);

    DrmModeCrtc *crtc = m_drm->getCrtc(drm_id_crtc);
    if (!crtc)
    {
        HWC_LOGE("(%" PRIu64 ") drm state is wrong when enableOverlayOutput", dpy);
        return;
    }

    status_t ret = NO_ERROR;
    ret |= crtc->addProperty(m_atomic_req[dpy], DRM_PROP_CRTC_OUTPUT_ENABLE, 1) < 0;
    ret |= crtc->addProperty(m_atomic_req[dpy], DRM_PROP_CRTC_OUTPUT_BUFF_IDX, param->fence_index) < 0;
    ret |= crtc->addProperty(m_atomic_req[dpy], DRM_PROP_CRTC_INTF_BUFF_IDX, param->if_fence_index) < 0;
    ret |= crtc->addProperty(m_atomic_req[dpy], DRM_PROP_CRTC_OUTPUT_X,
                             static_cast<uint64_t>(param->src_crop.left)) < 0;
    ret |= crtc->addProperty(m_atomic_req[dpy], DRM_PROP_CRTC_OUTPUT_Y,
                             static_cast<uint64_t>(param->src_crop.top)) < 0;
    ret |= crtc->addProperty(m_atomic_req[dpy], DRM_PROP_CRTC_OUTPUT_WIDTH,
                             static_cast<uint64_t>(param->src_crop.getWidth())) < 0;
    ret |= crtc->addProperty(m_atomic_req[dpy], DRM_PROP_CRTC_OUTPUT_HEIGHT,
                             static_cast<uint64_t>(param->src_crop.getHeight())) < 0;
    ret |= crtc->addProperty(m_atomic_req[dpy], DRM_PROP_CRTC_OUTPUT_DST_WIDTH,
                             static_cast<uint64_t>(param->dst_crop.getWidth())) < 0;
    ret |= crtc->addProperty(m_atomic_req[dpy], DRM_PROP_CRTC_OUTPUT_DST_HEIGHT,
                             static_cast<uint64_t>(param->dst_crop.getHeight())) < 0;


    if (param->dump_point != -1)
    {
        ret |= crtc->addProperty(m_atomic_req[dpy], DRM_PROP_CRTC_OUTPUT_SCENARIO, mapDispDumpPoint(param->dump_point)) < 0;
    }

    unsigned int index = static_cast<unsigned int>(param->queue_idx);
    if (index < NUM_DECOUPLE_FB_ID_BACKUP_SLOTS)
    {
        HWC_ATRACE_FORMAT_NAME("fb_id_from_cache");
        HWC_LOGV("(%" PRIu64 ") DrmDevice::enableOverlayOutput() cache[%u] alloc:%" PRIu64 " fb_id:%d ",
                 dpy, index, m_prev_commit_dcm_out_fb_id[dpy][index].first,
                 m_prev_commit_dcm_out_fb_id[dpy][index].second);
        if (m_prev_commit_dcm_out_fb_id[dpy][index].first != UINT64_MAX)
        {
            if (m_prev_commit_dcm_out_fb_id[dpy][index].first == param->alloc_id)
            {
                HWC_LOGV("cache[%u] alloc:%" PRIu64 " fb_id:%d",
                    index, param->alloc_id, m_prev_commit_dcm_out_fb_id[dpy][index].second);
                param->fb_id = m_prev_commit_dcm_out_fb_id[dpy][index].second;
            }
            else
            {
                m_drm->removeFb(m_prev_commit_dcm_out_fb_id[dpy][index].second);
                HWC_LOGD("(%" PRIu64 ") remove plane[%u] fb_id:%d", dpy, index,
                    m_prev_commit_dcm_out_fb_id[dpy][index].second);
                m_prev_commit_dcm_out_fb_id[dpy][index] = std::make_pair(UINT64_MAX, 0);
            }
        }
    }

    if (param->fb_id == 0)
    {
        createFbId(param, dpy, index);
        if (param->fb_id != 0 )
        {
            m_prev_commit_dcm_out_fb_id[dpy][index] = std::make_pair(param->alloc_id, param->fb_id);
        }
    }

    ret |= crtc->addProperty(m_atomic_req[dpy], DRM_PROP_CRTC_OUTPUT_FB_ID, param->fb_id) < 0;
    if (ret)
    {
        HWC_LOGE("(%" PRIu64 ") Failed to set crtc output: crtc_id:%d", dpy, crtc->getId());
    }
    else
    {
        HWC_LOGD("(%" PRIu64 ") Output+ (fb_id=%d/idx=%d/if_idx=%d/sec=%d/fmt=%d)/x=%d y=%d src_w=%d src_h=%d dst_w=%d dst_h=%d s=%d ds=%d)",
                 dpy, param->fb_id, param->fence_index, param->if_fence_index, param->secure,
                 param->format, param->src_crop.left, param->src_crop.top,
                 param->src_crop.getWidth(), param->src_crop.getHeight(),
                 param->dst_crop.getWidth(), param->dst_crop.getHeight(),
                 param->pitch, param->dataspace);
    }
}

void DrmDevice::prepareOverlayPresentFence(uint64_t dpy, OverlayPrepareParam* param)
{
    CHECK_DPY_RET_VOID(dpy);

    DrmModeCrtc *crtc = m_drm->getCrtc(param->drm_id_crtc);
    if (!crtc)
    {
        HWC_LOGE("(%" PRIu64 ":%u) drm state is wrong when prepareOverlayPresentFence", dpy, param->drm_id_crtc);
        return;
    }

    struct drm_mtk_fence fence_req;
    fence_req.crtc_id = crtc->getId();

    int res = DRM_WDT_IOCTL(DRM_IOCTL_MTK_CRTC_GETFENCE, &fence_req);
    if (res < 0)
    {
        IOLOGE(dpy, res, "DRM_IOCTL_MTK_CRTC_GETFENCE, id_crtc %u, sid:0x%x", param->drm_id_crtc, crtc->getSessionId());
        param->fence_index = 0;
        param->fence_fd = -1;
    }
    else
    {
        param->fence_index = fence_req.fence_idx;
        param->fence_fd = fence_req.fence_fd;
    }

    HWC_LOGV("(%" PRIu64 ":%u) DrmDevice::prepareOverlayPresentFence() crtc_id:%d fence_index:%d fence_fd:%d",
             dpy, param->drm_id_crtc, fence_req.crtc_id, param->fence_index, param->fence_fd);
}

status_t DrmDevice::waitVSync(uint64_t dpy, uint32_t drm_id_crtc, nsecs_t* ts)
{
    CHECK_DPY_RET_STATUS(dpy);

    int err = m_drm->waitNextVsync(drm_id_crtc, ts);
    if (err < 0)
    {
        HWC_LOGI("(%" PRIu64 ":%u) failed to waitNextVsync: %d", dpy, drm_id_crtc, err);
        return BAD_VALUE;
    }

    return NO_ERROR;
}

void DrmDevice::setPowerMode(uint64_t dpy, uint32_t drm_id_crtc, int mode)
{
    CHECK_DPY_RET_VOID(dpy);

    HWC_LOGD("%s() dpy:%" PRIu64 " mode:%d", __FUNCTION__, dpy, mode);
#ifdef USE_SWWATCHDOG
    AUTO_WDT(1000);
#endif
    std::string filename("/dev/graphics/fb");
    filename += std::to_string(dpy);
    int fb_fd = open(filename.c_str(), O_RDWR);
    if (fb_fd < 0)
    {
        HWC_LOGW("Failed to open fb%" PRIu64 " device: %s", dpy, strerror(errno));
    }

    // check is internal
    bool is_internal = false;
    DrmModeCrtc* crtc = m_drm->getCrtc(drm_id_crtc);
    if (crtc)
    {
        DrmModeEncoder* encoder = crtc->getEncoder();
        if (encoder)
        {
            DrmModeConnector *connector = encoder->getConnector();
            if (connector)
            {
                if (connector->getConnectorType() == DRM_MODE_CONNECTOR_DSI)
                {
                    is_internal = true;
                }
            }
            else
            {
                DLOGW(dpy, "%s(), no connector", __FUNCTION__);
            }
        }
        else
        {
            DLOGW(dpy, "%s(), no encoder", __FUNCTION__);
        }
    }
    else
    {
        DLOGW(dpy, "%s(), no crtc (id=%u)", __FUNCTION__, drm_id_crtc);
    }

    int err = NO_ERROR;
    switch (mode)
    {
        case HWC_POWER_MODE_OFF:
        {
            err |= m_drm->blankDisplay(dpy, drm_id_crtc, mode);
            if (fb_fd > 0)
            {
                err |= WDT_IOCTL(fb_fd, FBIOBLANK, FB_BLANK_POWERDOWN);
            }
#ifdef USE_SWWATCHDOG
            if (is_internal)
                SWWatchDog::suspend(dpy);
#endif
            break;
        }
        case HWC_POWER_MODE_NORMAL:
        {
#ifdef USE_SWWATCHDOG
            if (is_internal)
                SWWatchDog::resume(dpy);
#endif
            err |= m_drm->blankDisplay(dpy, drm_id_crtc, mode);
            if (fb_fd > 0)
            {
                err |= WDT_IOCTL(fb_fd, FBIOBLANK, FB_BLANK_UNBLANK);
            }
            break;
        }
        case HWC_POWER_MODE_DOZE:
        {
            if (HwcFeatureList::getInstance().getFeature().aod)
            {
                err = m_drm->blankDisplay(dpy, drm_id_crtc, mode);
            }
            else
            {
                // the project does not support AOD, so we keep it in normal mode
                err = m_drm->blankDisplay(dpy, drm_id_crtc, HWC_POWER_MODE_NORMAL);
                HWC_LOGE("(%" PRIu64 ") %s: receive HWC_POWER_MODE_DOZE without aod enabled",
                         dpy, __FUNCTION__);
            }
            break;
        }
        case HWC_POWER_MODE_DOZE_SUSPEND:
        {
            if (HwcFeatureList::getInstance().getFeature().aod)
            {
                err = m_drm->blankDisplay(dpy, drm_id_crtc, mode);
            }
            else
            {
                // the project does not support AOD, so we keep it in normal mode
                err = m_drm->blankDisplay(dpy, drm_id_crtc, HWC_POWER_MODE_NORMAL);
                HWC_LOGE("(%" PRIu64 ") %s: receive HWC_POWER_MODE_DOZE_SUSPEND without aod enabled",
                         dpy, __FUNCTION__);
            }
            break;
        }

        default:
            HWC_LOGE("(%" PRIu64 ") %s: receive unknown power mode: %d", dpy, __FUNCTION__, mode);
    }

    if (err != NO_ERROR)
    {
        HWC_LOGE("Failed to set power(%d) to fb%" PRIu64 " device: %s", mode, dpy, strerror(errno));
    }

    protectedClose(fb_fd);
}

unsigned int DrmDevice::getDeviceId(uint64_t dpy)
{
    return static_cast<unsigned int>(dpy);
}

status_t DrmDevice::waitAllJobDone(const uint64_t /*dpy*/)
{
    return NO_ERROR;
}

status_t DrmDevice::waitRefreshRequest(unsigned int* type)
{
    int err = m_drm->ioctl(DRM_IOCTL_MTK_WAIT_REPAINT, type);
    if (err < 0)
    {
        IOLOGE(uint64_t(HWC_DISPLAY_PRIMARY), err, "DRM_MTK_WAIT_REPAINT");
        return BAD_VALUE;
    }

    return NO_ERROR;
}

status_t DrmDevice::disablePlane(drmModeAtomicReqPtr req_ptr, const DrmModePlane* plane)
{
    if (req_ptr == nullptr)
    {
        return BAD_VALUE;
    }

    status_t ret = NO_ERROR;
    ret |= plane->addProperty(req_ptr, DRM_PROP_PLANE_CRTC_ID, 0) < 0;
    ret |= plane->addProperty(req_ptr, DRM_PROP_PLANE_FB_ID, 0) < 0;
    return ret;
}

void DrmDevice::createAtomicRequirement(uint64_t dpy)
{
    CHECK_DPY_RET_VOID(dpy);

    if (m_atomic_req[dpy] != nullptr)
    {
        HWC_LOGW("(%" PRIu64 ") atomic requirement is non-null when create a new one", dpy);
        drmModeAtomicFree(m_atomic_req[dpy]);
        m_atomic_req[dpy] = nullptr;
    }

    m_atomic_req[dpy] = drmModeAtomicAlloc();
    if (m_atomic_req[dpy] == nullptr)
    {
        HWC_LOGW("(%" PRIu64 ") failed to allocate atomic requirement", dpy);
    }
}

void DrmDevice::releaseAtomicRequirement(uint64_t dpy)
{
    CHECK_DPY_RET_VOID(dpy);

    if (m_atomic_req[dpy] != nullptr)
    {
        drmModeAtomicFree(m_atomic_req[dpy]);
        m_atomic_req[dpy] = nullptr;
    }
}

bool DrmDevice::isDisp3X4DisplayColorTransformSupported()
{
    return m_caps_info.disp_feature_flag & DRM_DISP_FEATURE_PQ_34_COLOR_MATRIX;
}

bool DrmDevice::isDispAodForceDisable()
{
    return m_caps_info.disp_feature_flag & DRM_DISP_FEATURE_FORCE_DISABLE_AOD;
}

uint32_t DrmDevice::getMaxOverlayHeight()
{
    return m_drm_max_support_width;
}

uint32_t DrmDevice::getMaxOverlayWidth()
{
    return m_drm_max_support_height;
}

int32_t DrmDevice::getSupportedColorMode()
{
    return m_caps_info.lcm_color_mode;
}

int32_t DrmDevice::getDisplayOutputRotated()
{
    return (m_caps_info.lcm_degree == MTK_LCM_DEGREE_180 ? 1 : 0);
}

uint32_t DrmDevice::getRszMaxWidthInput()
{
    return m_caps_info.rsz_in_max[0];
}

uint32_t DrmDevice::getRszMaxHeightInput()
{
    return m_caps_info.rsz_in_max[1];
}

void DrmDevice::enableDisplayFeature(uint32_t flag)
{
    m_caps_info.disp_feature_flag |= mapHwcFeatureFlag(flag);
}

void DrmDevice::disableDisplayFeature(uint32_t flag)
{
    m_caps_info.disp_feature_flag &= ~mapHwcFeatureFlag(flag);
}

void DrmDevice::enableDisplayDriverLog(uint32_t param)
{
    int err = DRM_WDT_IOCTL(DRM_IOCTL_MTK_DEBUG_LOG, &param);
    if (err < 0)
    {
        IOLOGE(uint64_t(HWC_DISPLAY_PRIMARY), err, "DRM_IOCTL_MTK_DEBUG_LOG");
    }
}

status_t DrmDevice::disableCrtcOutput(drmModeAtomicReqPtr req_ptr, const DrmModeCrtc* crtc)
{
    if (req_ptr == nullptr)
    {
        return BAD_VALUE;
    }

    status_t ret = NO_ERROR;
    ret |= crtc->addProperty(req_ptr, DRM_PROP_CRTC_OUTPUT_ENABLE, 0) < 0;
    return ret;
}

uint32_t DrmDevice::getDrmSessionMode(uint32_t drm_id_crtc)
{
    DrmModeCrtc *crtc = m_drm->getCrtc(drm_id_crtc);
    if (crtc == nullptr)
    {
        HWC_LOGE("Failed to find crtc(%u) when %s", drm_id_crtc, __FUNCTION__);
        return MTK_DRM_SESSION_INVALID;
    }

    return crtc->getSessionMode();
}

uint32_t DrmDevice::getHrtIndex(uint32_t drm_id_crtc)
{
    uint32_t pos = 0xffff;
    DrmModeCrtc *crtc = m_drm->getCrtc(drm_id_crtc);
    if (crtc != nullptr)
    {
        pos = crtc->getPipe();
    }
    else
    {
        HWC_LOGW("%s(), failed to get HRT index, crtc is null, drm_id_crtc %u", __FUNCTION__, drm_id_crtc);
    }
    return pos;
}

void DrmDevice::createFbId(OverlayPortParam* param, const uint64_t& dpy, const uint64_t& id)
{
    uint32_t gem_handle = 0;
    uint32_t fb_id = 0;
    status_t err = NO_ERROR;
    HWC_LOGV("DrmDevice::createFbId() w:%u h:%u s:%u ble:%d sec:%d f:%d",
             param->src_buf_width, param->src_buf_height, param->pitch,
             param->blending, param->secure, mapDispColorFormat(param->format));

    std::lock_guard<std::mutex> lock(m_createFbId_mutex);

    if (param->secure && !isSupportDmaBuf())
    {
        drm_mtk_sec_gem_hnd hnd;
        hnd.sec_hnd = static_cast<uint32_t>(param->ion_fd);
        hnd.gem_hnd = 0;
        err = m_drm->ioctl(DRM_IOCTL_MTK_SEC_HND_TO_GEM_HND, &hnd);
        gem_handle = hnd.gem_hnd;
    }
    else
    {
        err = m_drm->getHandleFromPrimeFd(param->ion_fd, &gem_handle);
    }
    err = m_drm->addFb(gem_handle, param->src_buf_width, param->src_buf_height,
                     param->pitch, mapDispColorFormat(param->format),
                     param->blending, param->secure, &fb_id);
    if (err < 0)
    {
        IOLOGE(dpy, err, "addFb");
        std::ostringstream ss;
        getFdInfo(param->ion_fd, &ss);
        HWC_LOGI("DrmDevice::createFbId() addFb fail dpy:%" PRIu64 " id:%" PRIu64 " fbid:%d ion_fd:%d [%s]",
                dpy, id, param->fb_id, param->ion_fd, ss.str().c_str());
    }
    param->fb_id = fb_id;
    HWC_LOGV("DrmDevice::createFbId() dpy:%" PRIu64 " id:%" PRIu64 " fbid:%d ion:%d",
             dpy, id, param->fb_id, param->ion_fd);

    struct drm_gem_close close_param;
    memset(&close_param, 0, sizeof(close_param));
    close_param.handle = gem_handle;
    err = DRM_WDT_IOCTL(DRM_IOCTL_GEM_CLOSE, &close_param);
    if (err < 0)
    {
        IOLOGE(dpy, err, "DRM_IOCTL_GEM_CLOSE");
    }
}

int32_t DrmDevice::getWidth(uint64_t /*dpy*/, uint32_t drm_id_connector, hwc2_config_t config)
{
    return m_drm->getWidth(drm_id_connector, config);
}

int32_t DrmDevice::getHeight(uint64_t /*dpy*/, uint32_t drm_id_connector, hwc2_config_t config)
{
    return m_drm->getHeight(drm_id_connector, config);
}

int32_t DrmDevice::getRefresh(uint64_t /*dpy*/, uint32_t drm_id_connector, hwc2_config_t config)
{
    return m_drm->getRefresh(drm_id_connector, config);
}

uint32_t DrmDevice::getNumConfigs(uint64_t /*dpy*/, uint32_t drm_id_connector)
{
    return m_drm->getNumConfigs(drm_id_connector);
}

void DrmDevice::dump(const uint64_t& dpy, String8* dump_str)
{
    CHECK_DPY_RET_VOID(dpy);

    dump_str->appendFormat("----------DRMDEV Color Matrix----------\n");
    dump_str->appendFormat("Result Error No: %d\n", m_crtc_colortransform_res[dpy]);
    for (unsigned int i = 0; i < COLOR_MATRIX_DIM; ++i)
    {
        dump_str->appendFormat("%5d, %5d, %5d, %5d\n",
                m_last_color_config[dpy].color_matrix[i * COLOR_MATRIX_DIM],
                m_last_color_config[dpy].color_matrix[i * COLOR_MATRIX_DIM + 1],
                m_last_color_config[dpy].color_matrix[i * COLOR_MATRIX_DIM + 2],
                m_last_color_config[dpy].color_matrix[i * COLOR_MATRIX_DIM + 3]);
    }
    dump_str->appendFormat("---------------------------------------\n");

    for (unsigned int i = 0; i < DisplayManager::MAX_DISPLAYS; i++)
    {
        dump_str->appendFormat("dpy %u, fb_cache:\n", i);
        std::lock_guard<std::mutex> l(m_layer_caches_mutex[i]);
        m_fb_caches[i].dump(dump_str);
    }
    m_drm->dump(dump_str);

    return;
}

status_t DrmDevice::createColorTransformBlob(const uint64_t& dpy, sp<ColorTransform> color_transform, uint32_t* id)
{
    CHECK_DPY_RET_STATUS(dpy);

    if (color_transform != nullptr && id != nullptr)
    {
        struct disp_ccorr_config config;
        memset(&config, 0, sizeof(config));
        config.mode = color_transform->hint;
        for (unsigned int i = 0; i < COLOR_MATRIX_DIM * COLOR_MATRIX_DIM ; i++)
        {
            config.color_matrix[i] = transFloatToIntForCCORR(color_transform->matrix[i / 4][i % 4]);
        }
        config.feature_flag = color_transform->force_disable_color;
        int res = m_drm->createPropertyBlob(&config, sizeof(config), id);
        //Keep ColorTransform result and matrix for dump
        m_crtc_colortransform_res[dpy] = res;
        m_last_color_config[dpy] = config;

        DbgLogger logger(DbgLogger::TYPE_HWC_LOG, 'D', "[%s] ", DEBUG_LOG_TAG);
        logger.printf("create blob id of color transform[%s] mode=%d f=%d mat=",
            res < 0 ? "fail" : "success",
            config.mode,
            config.feature_flag);
        for (unsigned int i = 0; i < COLOR_MATRIX_DIM; ++i)
        {
                    logger.printf("%4d %4d %4d %4d",
                    config.color_matrix[i * COLOR_MATRIX_DIM],
                    config.color_matrix[i * COLOR_MATRIX_DIM + 1],
                    config.color_matrix[i * COLOR_MATRIX_DIM + 2],
                    config.color_matrix[i * COLOR_MATRIX_DIM + 3]);
        }
        return res;
    }
    return BAD_VALUE;
}

status_t DrmDevice::destroyBlob(uint32_t id)
{
    return m_drm->destroyPropertyBlob(id);
}

int32_t DrmDevice::updateDisplayResolution(uint64_t /*dpy*/)
{
    return m_drm->updateCrtcToPreferredModeInfo();
}

int32_t DrmDevice::getCurrentRefresh(uint64_t /*dpy*/, uint32_t drm_id_crtc)
{
    return m_drm->getCurrentRefresh(drm_id_crtc);
}

int32_t DrmDevice::updateConnectorMode(uint64_t dpy, uint32_t drm_id_crtc)
{
    return m_drm->updateConnectorModeToMaximumResolution(dpy, drm_id_crtc);
}

int32_t DrmDevice::getSpportedConnectorMode(uint64_t dpy, uint32_t drm_id_crtc, uint32_t *modeinfo)
{
    return m_drm->getSpportedConnectorMode(dpy, drm_id_crtc, modeinfo);
}

int32_t DrmDevice::getConnectorEDID(uint64_t dpy, uint32_t drm_id_crtc, uint32_t *edid)
{
    return m_drm->getConnectorEDID(dpy, drm_id_crtc, edid);
}

int32_t DrmDevice::setHDMIMode(uint64_t dpy, uint32_t drm_id_crtc, uint32_t mode, uint32_t value)
{
    switch(mode)
    {
        case ENABLE_HDMI_MODE:
            return m_drm->setHDMIMode(dpy, drm_id_crtc, value);
        case ENABLE_HDR_DV_MODE:
        {
            int err = 0;
            enum MTK_ETHDR_ON_OFF_STATUS hdr_status;
            hdr_status = (enum MTK_ETHDR_ON_OFF_STATUS)value;
            err = m_drm->ioctl(DRM_IOCTL_MTK_ETHDR_SET_HDR_ON_OFF, &hdr_status);
            if (err)
            {
                HWC_LOGE("%s DRM_IOCTL_MTK_ETHDR_SET_HDR_ON_OFF failed err %d", __func__, err);
            }
            return err;
        }
        case SET_COLOR_FORMAT_MODE:
            // DUALTX TODO need to check driver ready
            return m_drm->setConnectorColorspaceAndDepth(dpy, drm_id_crtc, value);
        default:
            HWC_LOGW("%s: not support mode: %d", __func__, mode);
            break;
    }

    return -1;
}

int32_t DrmDevice::getHDMIMode(uint64_t dpy, uint32_t drm_id_crtc, uint32_t mode, uint32_t *value)
{
    switch(mode)
    {
        case ENABLE_HDMI_MODE:
            *value = m_drm->getHDMIMode(dpy, drm_id_crtc);
            return 0;
        case SET_COLOR_FORMAT_MODE:
            // DUALTX TODO need to check driver ready
            return m_drm->getConnectorColorspaceAndDepth(dpy, drm_id_crtc, value);
        default:
            HWC_LOGW("%s: not support mode: %d", __func__, mode);
            break;
    }

    return -1;
}

void DrmDevice::trashCleanerLoop()
{
    while (true)
    {
        if (m_trash_request_add_fb_id) {
            usleep(50); // sleep some time to let trashAddFbId() get lock
        }

        std::unique_lock<std::mutex> lock(m_trash_mutex);

        if (m_trash_fb_id_list.empty())
        {
            if (m_trash_cleaner_thread_stop)
            {
                break;
            }

            m_condition.wait(lock);
            continue;
        }

        while (m_trash_fb_id_list.size())
        {
            // let trashAddFbId has higher priority
            if (m_trash_request_add_fb_id)
            {
                break;
            }
            m_drm->removeFb(m_trash_fb_id_list.front());
            m_trash_fb_id_list.pop_front();
        }
    }
}

void DrmDevice::trashAddFbId(const std::list<FbCacheEntry>& fb_caches)
{
    if (fb_caches.empty())
    {
        return;
    }
    std::lock_guard<std::mutex> l(m_add_trash_mutex);
    m_trash_request_add_fb_id = true;
    std::lock_guard<std::mutex> lock(m_trash_mutex);
    for (const FbCacheEntry& entry : fb_caches)
    {
        m_trash_fb_id_list.push_back(entry.fb_id);
    }
    m_trash_request_add_fb_id = false;
    m_condition.notify_all();
}

void DrmDevice::trashAddFbId(FbCacheEntry& entry)
{
    std::lock_guard<std::mutex> l(m_add_trash_mutex);
    m_trash_request_add_fb_id = true;
    std::lock_guard<std::mutex> lock(m_trash_mutex);
    m_trash_fb_id_list.push_back(entry.fb_id);
    m_trash_request_add_fb_id = false;
    m_condition.notify_all();
}

void DrmDevice::removeFbCacheDisplay(uint64_t dpy)
{
    // remove every layer's fb cache in this display
    for (FbCacheInfo& layer_cache : m_fb_caches[dpy].layer_caches)
    {
        for (FbCacheEntry& entry : layer_cache.fb_caches)
        {
            m_drm->removeFb(entry.fb_id);
        }
    }
    std::lock_guard<std::mutex> l(m_layer_caches_mutex[dpy]);
    m_fb_caches[dpy].layer_caches.clear();
}

void DrmDevice::removeFbCacheAllDisplay()
{
    for (unsigned int dpy = 0; dpy < DisplayManager::MAX_DISPLAYS; dpy++)
    {
        removeFbCacheDisplay(dpy);
    }
}

bool DrmDevice::isHwcFeatureSupported(uint32_t hwc_feature_flag)
{
    if (hwc_feature_flag == HWC_FEATURE_COLOR_HISTOGRAM)
    {
        return m_drm_histogram.isHwSupport();
    }

    return m_caps_info.disp_feature_flag & mapHwcFeatureFlag(hwc_feature_flag);
}

void DrmDevice::updateMSyncEnable(uint64_t dpy, DataPackage* package, DataPackage* late_package)
{
    std::optional<bool> msync2_enable;
    if (package &&
        package->m_msync2_data &&
        package->m_msync2_data->m_msync2_enable)
    {
        // use latest as first priority
        msync2_enable = package->m_msync2_data->m_msync2_enable;
    }
    else if (late_package &&
             late_package->m_msync2_data &&
             late_package->m_msync2_data->m_msync2_enable)
    {
        // need set previous frame's msync state, probably skipped
        HWC_LOGW("msync enable use late_package");
        msync2_enable = late_package->m_msync2_data->m_msync2_enable;
    }

    if (msync2_enable)
    {
        HWC_LOGI("msync enable %d -> %d", m_msync2_enable[dpy], *msync2_enable);
        m_msync2_enable[dpy] = *msync2_enable;
    }
}

void DrmDevice::updateMSyncParamTable(uint64_t dpy, DataPackage* package, DataPackage* late_package)
{
    std::optional<std::shared_ptr<MSync2Data::ParamTable>> param_table;
    if (package &&
        package->m_msync2_data &&
        package->m_msync2_data->m_param_table)
    {
        // use latest as first priority
        param_table = package->m_msync2_data->m_param_table;
    }
    else if (late_package &&
             late_package->m_msync2_data &&
             late_package->m_msync2_data->m_param_table)
    {
        // need set previous frame's msync state, probably skipped
        HWC_LOGW("msync param table use late_package");
        param_table = late_package->m_msync2_data->m_param_table;
    }

    if (param_table)
    {
        HWC_LOGI("%s(), %u, %u, %u",
                 __FUNCTION__,
                 param_table.value()->max_fps,
                 param_table.value()->min_fps,
                 param_table.value()->level_num);
        for (uint32_t i = 0; i < param_table.value()->level_num; i++)
        {
            HWC_LOGI("\t%u, %u, %u, %u",
                     param_table.value()->level_tables[i].level_id,
                     param_table.value()->level_tables[i].level_fps,
                     param_table.value()->level_tables[i].max_fps,
                     param_table.value()->level_tables[i].min_fps);
        }

        msync_parameter_table drm_param_table;

        drm_param_table.msync_max_fps = param_table.value()->max_fps;
        drm_param_table.msync_min_fps = param_table.value()->min_fps;
        drm_param_table.msync_level_num = param_table.value()->level_num;

        msync_level_table* table = new msync_level_table[drm_param_table.msync_level_num];
        if (!table)
        {
            HWC_LOGW("%s(), table == nullptr", __FUNCTION__);
            return;
        }
        drm_param_table.level_tb = table;

        for (uint32_t i = 0; i < drm_param_table.msync_level_num; i++)
        {
            table[i].level_id = param_table.value()->level_tables[i].level_id;
            table[i].level_fps = param_table.value()->level_tables[i].level_fps;
            table[i].max_fps = param_table.value()->level_tables[i].max_fps;
            table[i].min_fps = param_table.value()->level_tables[i].min_fps;
        }

        int err = DRM_WDT_IOCTL(DRM_IOCTL_MTK_SET_MSYNC_PARAMS, &drm_param_table);
        if (err < 0)
        {
            IOLOGE(dpy, err, "DRM_IOCTL_MTK_SET_MSYNC_PARAMS");
        }

        delete[] drm_param_table.level_tb;
    }
}

void DrmDevice::getMSyncDefaultParamTableInternal()
{
    if ((m_caps_info.disp_feature_flag & DRM_DISP_FEATURE_MSYNC2_0) == 0)
    {
        return;
    }

    msync_parameter_table drm_param_table;

    drm_param_table.msync_level_num = m_caps_info.msync_level_num;
    // need allocate level_num for kernel to fill
    drm_param_table.level_tb = new msync_level_table[drm_param_table.msync_level_num];
    if (!drm_param_table.level_tb)
    {
        HWC_LOGD("drm_param_table.level_tb == nullptr");
        return;
    }

    // still in constructor, should not use watch dog, platform config
    int err = DRM_IOCTL(DRM_IOCTL_MTK_GET_MSYNC_PARAMS, &drm_param_table);
    if (err < 0)
    {
        HWC_LOGD("DRM_IOCTL_MTK_GET_MSYNC_PARAMS fail, err %d", err);
        m_msync_param_table.max_fps = 0;
        m_msync_param_table.min_fps = 0;
        m_msync_param_table.level_num = 0;
    }
    else
    {
        m_msync_param_table.max_fps = drm_param_table.msync_max_fps;
        m_msync_param_table.min_fps = drm_param_table.msync_min_fps;
        m_msync_param_table.level_num = drm_param_table.msync_level_num;

        m_msync_param_table.level_tables.resize(m_msync_param_table.level_num);

        for (size_t i = 0; i < m_msync_param_table.level_tables.size(); i++)
        {
            m_msync_param_table.level_tables[i].level_id = drm_param_table.level_tb[i].level_id;
            m_msync_param_table.level_tables[i].level_fps = drm_param_table.level_tb[i].level_fps;
            m_msync_param_table.level_tables[i].max_fps = drm_param_table.level_tb[i].max_fps;
            m_msync_param_table.level_tables[i].min_fps = drm_param_table.level_tb[i].min_fps;
        }
    }

    delete[] drm_param_table.level_tb;
}

int32_t DrmDevice::getHistogramAttribute(int32_t* color_format, int32_t* data_space, uint8_t* mask,
        uint32_t* max_bin)
{
    return m_drm_histogram.getAttribute(color_format, data_space, mask, max_bin);
}

int32_t DrmDevice::enableHistogram(const bool enable, const int32_t format,
        const uint8_t format_mask, const int32_t dataspace, const uint32_t bin_count)
{
    return m_drm_histogram.enableHistogram(enable, format, format_mask, dataspace, bin_count);
}

int32_t DrmDevice::collectHistogram(uint32_t* fence_index,
        uint32_t* histogram_ptr[NUM_FORMAT_COMPONENTS])
{
    return m_drm_histogram.collectHistogram(fence_index, histogram_ptr);
}

int32_t DrmDevice::getCompressionDefine(const char* name, uint64_t* type, uint64_t* modifier)
{
    if (name == NULL)
    {
        return 0;
    }

    if (!strcmp(AFBC_COMPRESSION_NAME, name))
    {
        *type = HWC_COMPRESSION_TYPE_AFBC;
        *modifier = DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_32x8 | AFBC_FORMAT_MOD_SPLIT);
    }

    if (!strcmp(PVRIC_COMPRESSION_NAME, name))
    {
        HWC_LOGW("Fourcc not define for PVRIC");
    }

    return 0;
}

bool DrmDevice::getEthdrSupport(uint64_t dpy)
{
    if (Platform::getInstance().m_config.ethdr_meta_support)
    {
        // check sub as main case
        if (dpy == HWC_DISPLAY_PRIMARY)
        {
            uint32_t drm_id_crtc = m_display_state.getDrmIdCrtc(dpy);

            if (drm_id_crtc == UINT32_MAX)
                return false;

            DrmModeCrtc* crtc = m_drm->getCrtc(drm_id_crtc);
            if (crtc != nullptr && crtc->getPipe() == 1)
                return true;
            else
                return false;

        }
        else
            return dpy == HWC_DISPLAY_EXTERNAL ? true : false;
    }

    return false;
}

int32_t DrmDevice::getDolbyInfo()
{
#if DV_STATUS_SUPPORT
    uint32_t dv_stat = 0;

    int err = m_drm.ioctl(DRM_IOCTL_MTK_ETHDR_GET_DV_STATUS, &dv_stat);

    if (err < 0)
    {
        HWC_LOGE("GET DV Info failed: %d", err);
    }

    return (int32_t)dv_stat;
#else
    return true;
#endif
}

int32_t DrmDevice::getKernelLogoSize(int32_t *width, int32_t *height)
{
    return m_drm->getKernelLogoSize(width, height);
}

#ifdef MTK_DRM_PIXEL_SHIFT
int32_t DrmDevice::setPixelShift(struct mtk_pixel_shift_user_config *cfg)
{
    return DRM_IOCTL(DRM_IOCTL_MTK_PIXEL_SHIFT_CONFIG, cfg);
}
#endif
