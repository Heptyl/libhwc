#pragma once
#include <stdint.h>
#include <thread>

#include "dev_interface_simple.h"
#include "drmmoderesource.h"
#include "drmmodeutils.h"
#include "hwcdisplay_simple.h"
#include <linux/mediatek_drm.h>

namespace simplehwc {

enum { MAX_CACHED_FB_ID_SIZE = 3 };
enum
{
    MTK_DRM_INVALID_SESSION_ID = static_cast<unsigned int>(-1),
};

class DrmDevice : public IOverlayDevice_simple
{
public:
    static DrmDevice& getInstance();
    ~DrmDevice();

    void createFbId(uint64_t display, PrivateHnd *priv_handle);

    void postBuffer(uint64_t display, buffer_handle_t buffer, PrivateHnd priv_handle);

    void getDisplyResolution(uint64_t display, uint32_t* width, uint32_t* height);

    void getDisplyPhySize(uint64_t display, uint32_t* width, uint32_t* height);

    void getDisplayRefresh(uint64_t display, uint32_t* refresh);

    void initDisplay(uint64_t display);

    void setPowerMode(uint64_t display, int32_t mode);

    // query hw capabilities through ioctl and store in m_caps_info
    int queryCapsInfo();

    // isisDispFeatureSupported() is used to query if the flg feature is supported
    bool isDispFeatureSupported(unsigned int flg_mask);
    void prepareOverlayPresentFence(uint64_t display, int32_t* fence_fd, uint32_t* fence_index);
    // createOverlaySession() creates overlay composition session
    int createOverlaySession(uint64_t display, HWC_DISP_MODE mode = HWC_DISP_SESSION_DIRECT_LINK_MODE);

    // destroyOverlaySession() destroys overlay composition session
    void destroyOverlaySession(uint64_t display);

private:
    DrmDevice();

    DrmModeResource m_drm;

    void* m_fb_vaddr[MAX_DISPLAY_NUM][MAX_BO_SIZE];
    uint32_t mPinpon;
    //std::pair<uint64_t, uint32_t > m_prev_cached_fb_id[MAX_CACHED_FB_ID_SIZE];
    std::pair<uint64_t, uint32_t > *m_prev_cached_fb_id;
    std::pair<uint64_t, uint32_t > m_prev_commit_fb_id;
    mtk_drm_disp_caps_info m_caps_info;
};

}  // namespace simplehwc

