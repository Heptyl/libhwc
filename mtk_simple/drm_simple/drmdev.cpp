#define DEBUG_LOG_TAG "drmdev"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include "drmdev.h"

#include <cutils/log.h>
#include <utils/Errors.h>
#include <inttypes.h>

#include "drmmoderesource.h"
#include "drmmodecrtc.h"
#include "hwc_ui/GraphicBufferMapper.h"
#include "hwc_ui/Rect.h"
#include <cutils/properties.h>
#include <stdio.h>
#include <string>

#include "debug_simple.h"

namespace simplehwc {

using namespace android;
using hwc::Rect;
using hwc::GraphicBufferMapper;

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

DrmDevice& DrmDevice::getInstance()
{
//    HWC_LOGD("simple_hwc DrmDevice::getInstance()");
    static DrmDevice gInstance;
    return gInstance;
}

DrmDevice::DrmDevice()
{
    HWC_LOGD("simple_hwc DrmDevice()");

    memset(&m_caps_info, 0, sizeof(m_caps_info));
    m_prev_cached_fb_id = new std::pair<uint64_t, uint32_t>[MAX_CACHED_FB_ID_SIZE];
    for (unsigned int i = 0; i < MAX_CACHED_FB_ID_SIZE; i++)
    {
        m_prev_cached_fb_id[i] = std::make_pair(UINT64_MAX, 0);
    }
    m_prev_commit_fb_id = std::make_pair(UINT64_MAX, 0);

    m_drm.init();
    m_drm.connectAllDisplay();
    m_drm.dumpResourceInfo();
    mPinpon = 0;
    for (uint32_t i = 0; i < MAX_DISPLAY_NUM; i++)
    {
        for (uint32_t j = 0; j < MAX_BO_SIZE; j++)
        {
            m_fb_vaddr[i][j] = nullptr;
        }
    }

    int err = queryCapsInfo();
    if (NO_ERROR != err)
    {
        HWC_LOGE("QueryCaps fail err:%d", err);
    }

}

DrmDevice::~DrmDevice()
{
    for (uint32_t i = 0; i < MAX_DISPLAY_NUM; i++)
    {
        for (uint32_t j = 0; j < MAX_BO_SIZE; j++)
        {
            if (m_fb_vaddr[i][j])
            {
                DrmModeCrtc* crtc = m_drm.getDisplay(i);
                if (!crtc)
                {
                    HWC_LOGW("display(%u) crtc is null",i);
                    continue;
                }
                hwc_drm_bo bo = crtc->getDumbBuffer(j);
                int res = munmap(m_fb_vaddr[i][j], bo.size_page);
                if (res)
                {
                    HWC_LOGW("(%u): failed to unmap framebuffer pinpon %u", i, j);
                }
            }
        }
    }

    delete[] m_prev_cached_fb_id;
}

void DrmDevice::createFbId(uint64_t display, PrivateHnd *priv_handle)
{
    uint32_t gem_handle = 0;
    status_t err = NO_ERROR;
    int blending = HWC2_BLEND_MODE_PREMULTIPLIED;
    HWC_LOGV(" w:%u h:%u s:%u ble:%d f:%d",
            priv_handle->width, priv_handle->height, priv_handle->y_stride,
            blending, mapDispColorFormat(priv_handle->format));

    err = m_drm.getHandleFromPrimeFd(priv_handle->ion_fd, &gem_handle);

    err = m_drm.addFb(gem_handle, priv_handle->width, priv_handle->height,
            priv_handle->y_stride, mapDispColorFormat(priv_handle->format),
            blending, &priv_handle->fb_id);
    if (err < 0)
    {
        HWC_LOGE("addFb fail dpy:%" PRIu64 " fbid:%d ion_fd:%d gem_hnd:%d",
               display,priv_handle->fb_id, priv_handle->ion_fd, gem_handle);
    }
    else
    {
        HWC_LOGD(" dpy:%" PRIu64 " fbid:%d ion:%d gem_hnd:%d",
                display, priv_handle->fb_id, priv_handle->ion_fd, gem_handle);
    }

    struct drm_gem_close close_param;
    memset(&close_param, 0, sizeof(close_param));
    close_param.handle = gem_handle;
    err = m_drm.ioctl(DRM_IOCTL_GEM_CLOSE, &close_param);
    if (err < 0)
    {
        HWC_LOGE("(%" PRIu64 "): failed to call ioctl:DRM_IOCTL_GEM_CLOSE", display);
    }

}

void DrmDevice::postBuffer(uint64_t display, buffer_handle_t buffer, PrivateHnd priv_handle)
{
    HWC_ATRACE_CALL();
    DrmModeCrtc* crtc = m_drm.getDisplay(display);
    if (crtc == nullptr)
    {
        HWC_LOGW("failed to postBuffer display_%" PRIu64 ": no crtc", display);
        return;
    }
    hwc_drm_bo bo = crtc->getDumbBuffer(mPinpon);

    //if (priv_handle.ion_fd > 0)
    if (!isForceMemcpy() && (priv_handle.ion_fd > 0) && (priv_handle.fence_idx > 0))
    {
        priv_handle.fb_id = 0;

#ifdef FB_CACHED_ENABLE
        {//fb_Cached +, if FB_CACHED_ENABLE
            static int index = 0;

            for (unsigned int i = 0; i < MAX_CACHED_FB_ID_SIZE; i++)
            {
                if (m_prev_cached_fb_id[i].first != UINT64_MAX)
                {
                    if (m_prev_cached_fb_id[i].first == priv_handle.alloc_id)
                    {
                        HWC_LOGD("cache[%u] alloc:%" PRIu64 " fb_id:%d",
                                i, m_prev_cached_fb_id[i].first, m_prev_cached_fb_id[i].second);
                        priv_handle.fb_id = m_prev_cached_fb_id[i].second;
#ifdef USE_SYSTRACE
                        char atrace_tag[128];
                        if (snprintf(atrace_tag, sizeof(atrace_tag), "fb_id_from_cache %u, alloc:%" PRIu64 " fb_id:%d", i, m_prev_cached_fb_id[i].first, m_prev_cached_fb_id[i].second) > 0)
                        {
                            HWC_ATRACE_NAME(atrace_tag);
                        }
#endif
                        break;
                    }
                }

            }

            if (priv_handle.fb_id == 0)
            { //fb Cached false, then createFbId, and update the m_prev_cached_fb_id[index]
                HWC_LOGD("(%" PRIu64 ") postbuffer-createFbId+", display);
                createFbId(display, &priv_handle);

                {// remove unused cached fb
                    if (index >= MAX_CACHED_FB_ID_SIZE)
                        index = index % MAX_CACHED_FB_ID_SIZE;

                    if (m_prev_cached_fb_id[index].second == m_prev_commit_fb_id.second)
                    {
                        index++;
                        index = index % MAX_CACHED_FB_ID_SIZE;
                    }

                    if (m_prev_cached_fb_id[index].second != 0)
                    {
                        m_drm.removeFb(m_prev_cached_fb_id[index].second);
                        HWC_LOGD("(%" PRIu64 ") removeFb()-m_prev_cached_fb_id[%d]:%d", display, index, m_prev_cached_fb_id[index].second);
                        m_prev_cached_fb_id[index] = std::make_pair(UINT64_MAX, 0);
                    }

                }

                if (priv_handle.fb_id != 0 )
                {   // update the cached fb
                    m_prev_cached_fb_id[index] = std::make_pair(priv_handle.alloc_id, priv_handle.fb_id);
                    HWC_LOGD("(%" PRIu64 ") update m_prev_cached_fb_id[%d]:%d", display, index, m_prev_cached_fb_id[index].second);
                    index++;
                    index = index % MAX_CACHED_FB_ID_SIZE;
                }
            }
        }//fb_Cached -
#else
        {//if (!FB_CACHED_ENABLE), then createFbId
            HWC_LOGD("(%" PRIu64 ") postbuffer-createFbId+", display);
            createFbId(display, &priv_handle);
            //HWC_LOGD("(%" PRIu64 ")%s: postbuffer[%p]-add.fb_id[%d]", display, __func__, buffer, priv_handle.fb_id);
        }
#endif

#ifdef USE_SYSTRACE
        char atrace_tag[128];
        if (snprintf(atrace_tag, sizeof(atrace_tag), "atomicPostBuffer(%" PRIu64 "): buffer:%p, fb_id:%d,fence_idx:%d", display, buffer, priv_handle.fb_id, priv_handle.fence_idx) > 0)
        {
            HWC_ATRACE_NAME(atrace_tag);
        }
#endif
        HWC_LOGI("(%" PRIu64 ") atomicPostBuffer[%p]-fb_id:%d, fence_idx:%d", display, buffer, priv_handle.fb_id, priv_handle.fence_idx);
        m_drm.atomicPostBuffer(display, priv_handle);

#ifndef FB_CACHED_ENABLE
        if (m_prev_commit_fb_id.second != 0)
        {
          m_drm.removeFb(m_prev_commit_fb_id.second);
          HWC_LOGD("(%" PRIu64 ") removeFb()-m_prev_commit_fb_id: %d", display, m_prev_commit_fb_id.second);
          m_prev_commit_fb_id = std::make_pair(UINT64_MAX, 0);
        }
#endif

        if (priv_handle.fb_id != 0) {
            m_prev_commit_fb_id = std::make_pair(priv_handle.alloc_id, priv_handle.fb_id);
        }

    }
    else
    {//by memcpy
        if (m_fb_vaddr[display][mPinpon] == nullptr)
        {
            m_fb_vaddr[display][mPinpon] = mmap(0, bo.size_page, PROT_WRITE, MAP_SHARED, bo.fd, 0);
        }
        if (m_fb_vaddr[display][mPinpon])
        {
            auto& mapper = GraphicBufferMapper::getInstance();
            void* source_vaddr = nullptr;
            Rect rect(crtc->getVirWidth(), crtc->getVirHeight());
            //HWC_ATRACE_NAME("mapper.lock()");
            int res = mapper.lock(buffer, GRALLOC_USAGE_SW_READ_OFTEN, rect, &source_vaddr);
            if (res == NO_ERROR)
            {
                //usleep(static_cast<unsigned int>(isForceMemcpy() - 1)*1000); //sleep n ms to wait for...
                if (!isSelfRefresh())
                {
                    if (bo.pitches[0] / 4 == priv_handle.y_stride)
                    {
                        memcpy(m_fb_vaddr[display][mPinpon], source_vaddr, bo.size_page);
                    }
                    else
                    {
                        uint32_t* fb_shifted_vaddr = static_cast<uint32_t*>(m_fb_vaddr[display][mPinpon]);
                        uint32_t* source_shifted_vaddr = static_cast<uint32_t*>(source_vaddr);
                        for (uint32_t i = 0; i < bo.height; ++i)
                        {
                            memcpy(fb_shifted_vaddr, source_shifted_vaddr, bo.width * 4);
                            fb_shifted_vaddr += bo.pitches[0] / 4;
                            source_shifted_vaddr += priv_handle.y_stride;
                        }
                    }
                }

                if (UNLIKELY(isEnableDumpBuf() > 0))
                {//dump buf+
                    String8 path;
                    static int count = 0;
                    path.appendFormat("/data/SF_dump/FrameBuffer_w%d_h%d_s%d_vs%d_f0x%x_C%d.rgba",
                            bo.width, bo.height, (bo.pitches[0] / 4), bo.height, bo.format, count);

                    FILE *fp = fopen(path.string(), "wb");
                    if (fp)
                    {
                        size_t written_count = fwrite(m_fb_vaddr[display][mPinpon], static_cast<size_t>(bo.size_page), 1, fp);
                        if (written_count == 1)
                        {
                            count++;
                            HWC_LOGD("[simplehwc] @%s: fwrite(%s) successed", __func__, path.string());
                        }
                        else
                        {
                            HWC_LOGE("[simplehwc] @%s: fwrite(%s) failed", __func__, path.string());
                        }
                        int ret = fclose(fp);
                        if (CC_UNLIKELY(ret != 0))
                        {
                            HWC_LOGE("fclose fail: %s", strerror(errno));
                        }
                        fp = nullptr;
                        }
                    else
                    {
                        HWC_LOGE("[simplehwc] @%s: fopen(%s) failed, %s", __func__, path.string(), strerror(errno));
                    }
                    int cnt = isEnableDumpBuf();
                    setDumpBuf(--cnt);
                }//dump buf-

                mapper.unlock(buffer);
            }
            else
            {
                HWC_LOGW("(%" PRIu64 ")%s: failed to lock client target", display, __func__);
            }
        }

        HWC_LOGI("(%" PRIu64 ") postbuffer[%p]-bo.fb_id[%d]", display, buffer, bo.fb_id);
        m_drm.postBuffer(display, bo.fb_id);
        mPinpon = (mPinpon + 1) % MAX_BO_SIZE;

    }


}

void DrmDevice::getDisplyResolution(uint64_t display, uint32_t* width, uint32_t* height)
{
    DrmModeCrtc* crtc = m_drm.getDisplay(display);
    if (crtc == nullptr)
    {
        HWC_LOGW("failed to getDisplyResolution display_%" PRIu64 ": no crtc", display);
        return;
    }
    if (width)
    {
        *width = crtc->getVirWidth();
    }
    if (height)
    {
        *height = crtc->getVirHeight();
    }
}

void DrmDevice::getDisplyPhySize(uint64_t display, uint32_t* width, uint32_t* height)
{
    DrmModeCrtc* crtc = m_drm.getDisplay(display);
    if (crtc == nullptr)
    {
        HWC_LOGW("failed to getDisplyPhySize display_%" PRIu64 ": no crtc", display);
        return;
    }
    if (width)
    {
        *width = crtc->getPhyWidth();
    }
    if (height)
    {
        *height = crtc->getPhyHeight();
    }
}

void DrmDevice::getDisplayRefresh(uint64_t display, uint32_t* refresh)
{
    DrmModeCrtc* crtc = m_drm.getDisplay(display);
    if (crtc == nullptr)
    {
        HWC_LOGW("failed to getDisplayRefresh display_%" PRIu64 ": no crtc", display);
        return;
    }
    if (refresh)
    {
        *refresh = crtc->getCurrentModeRefresh();
    }
}

void DrmDevice::initDisplay(uint64_t display)
{
    m_drm.setDisplay(display, false);
}

void DrmDevice::setPowerMode(uint64_t display, int32_t mode)
{
    HWC_LOGI("(%" PRIu64 ")%s: mode=%d", display, __func__, mode);
    int err = NO_ERROR;

    switch (mode)
    {
        case HWC_POWER_MODE_OFF:
        {
            err |= m_drm.blankDisplay(display, mode);
            break;
        }

        case HWC_POWER_MODE_NORMAL:
        {
            err |= m_drm.blankDisplay(display, mode);
            break;
        }

        default:
            HWC_LOGW("(%" PRIu64 ") setPowerMode: receive unknown power mode: %d", display, mode);
    }

    if (err != NO_ERROR)
    {
         HWC_LOGE("Failed to set power(%d) to fb%" PRIu64 " device: %s", mode, display, strerror(errno));
    }
}

int DrmDevice::queryCapsInfo()
{
    memset(&m_caps_info, 0, sizeof(m_caps_info));
    int err = m_drm.ioctl(DRM_IOCTL_MTK_GET_DISPLAY_CAPS, &m_caps_info);
    HWC_LOGD("CapsInfo [0x%x] hw_ver", m_caps_info.hw_ver);
    //HWC_LOGD("CapsInfo [%d] output_rotated",  m_caps_info.is_output_rotated);
    HWC_LOGD("CapsInfo [0x%x] disp_feature",      m_caps_info.disp_feature_flag);
    HWC_LOGD("CapsInfo is_disp_feature_hrt_support:%d",      isDispFeatureSupported(DRM_DISP_FEATURE_HRT));
    HWC_LOGI("CapsInfo is_disp_feature_iommu_support:%d",    isDispFeatureSupported(DRM_DISP_FEATURE_IOMMU));
    HWC_LOGI("CapsInfo is_disp_feature_virtual_display_support:%d",    isDispFeatureSupported(DRM_DISP_FEATURE_VIRUTAL_DISPLAY));
    return err;
  }

bool DrmDevice::isDispFeatureSupported(unsigned int feature_mask)
{
    return m_caps_info.disp_feature_flag & feature_mask;
}

void DrmDevice::prepareOverlayPresentFence(uint64_t display, int32_t* fence_fd, uint32_t* fence_index)
{

    DrmModeCrtc* crtc = m_drm.getDisplay(display);
    if (!crtc)
    {
        HWC_LOGE("(%" PRIu64 ") drm state is wrong when prepareOverlayPresentFence", display);
        return;
    }

    struct drm_mtk_fence fence_req;
    fence_req.crtc_id = crtc->getId();

    int res = m_drm.ioctl(DRM_IOCTL_MTK_CRTC_GETFENCE, &fence_req);
    if (res < 0)
    {
        HWC_LOGE("call ioctl-DRM_IOCTL_MTK_CRTC_GETFENCE Failed!");
        *fence_index = 0;
        *fence_fd = -1;
    }
    else
    {
        *fence_index = fence_req.fence_idx;
        *fence_fd = fence_req.fence_fd;
    }

    HWC_LOGD("(%" PRIu64 ") crtc_id:%d fence_index:%d fence_fd:%d",
             display, fence_req.crtc_id, fence_req.fence_idx, fence_req.fence_fd);
}

int DrmDevice::createOverlaySession(uint64_t display, HWC_DISP_MODE mode)
{
    drm_mtk_session config;
    memset(&config, 0, sizeof(config));
    config.type       = HWC_DISP_SESSION_PRIMARY;
    config.device_id  = static_cast<unsigned int>(display);
    config.mode       = mapHwcDispMode2Drm(mode);
    config.session_id = MTK_DRM_INVALID_SESSION_ID;
    int err = m_drm.ioctl(DRM_IOCTL_MTK_SESSION_CREATE, &config);
    if (err < 0)
    {
        HWC_LOGE("call ioctl DRM_IOCTL_MTK_SESSION_CREATE failed!");
        return BAD_VALUE;
    }
    HWC_LOGI("Create Session (%d)", config.session_id);

    return err;
}


}  // namespace simplehwc

