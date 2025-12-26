#ifndef __MTK_HWC_DRM_MODE_RESOURCE_H__
#define __MTK_HWC_DRM_MODE_RESOURCE_H__

#include <stdint.h>
#include <vector>

#include <xf86drm.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#include <xf86drmMode.h>
#pragma clang diagnostic pop

#include <linux/mediatek_drm.h>

#include <mutex>
#include <utils/Timers.h>
#include <unordered_set>

#include "dev_interface.h"
#include "drmmodeutils.h"

class DrmModeCrtc;
class DrmModeEncoder;
class DrmModeConnector;
class DrmModePlane;

struct hwc_drm_bo;

enum
{
    MTK_DRM_INVALID_SESSION_ID = static_cast<unsigned int>(-1),
};

class DrmModeResource
{
public:
    static DrmModeResource& getInstance();
    ~DrmModeResource();

    int getFd();

    void dumpResourceInfo();
    void updateDisplayConnection(uint64_t dpy, uint32_t connector_type);
    void connectAllDisplay();
    void getCreateDisplayInfos(std::vector<CreateDisplayInfo>& create_display_infos);

    int setDisplay(uint64_t dpy, uint32_t id_crtc, bool use_req_size);
    int blankDisplay(uint64_t dpy, uint32_t id_crtc, int mode);

    int addFb(struct hwc_drm_bo *fb_bo);
    int addFb(uint32_t gem_handle, unsigned int width, unsigned int height,
              unsigned int stride, unsigned int format, int blending, bool secure,
              uint32_t *fb_id);
    int removeFb(uint32_t fb_id);
    int allocateBuffer(struct hwc_drm_bo *fb_bo);
    int freeBuffer(struct hwc_drm_bo &fb_bo);
    int getHandleFromPrimeFd(int fd, uint32_t* gem_handle);

    DrmModeCrtc* getCrtc(uint32_t id_crtc);
    DrmModeCrtc* getCrtcMain();
    DrmModeConnector* getConnector(uint32_t id_connector);

    int waitNextVsync(uint32_t id_crtc, nsecs_t* ts);

    int atomicCommit(drmModeAtomicReqPtr req, uint32_t flags, void *user_data);
    inline int ioctl(unsigned long request, void *arg) { return ::ioctl(m_fd, static_cast<unsigned int>(request), arg); }
    inline int drmIoctl(unsigned long request, void *arg) { return ::drmIoctl(m_fd, request, arg); }

    uint32_t getDimFbId() { return m_dim_fb_id; }

    int32_t getWidth(uint32_t id_connector, uint32_t config);
    int32_t getHeight(uint32_t id_connector, uint32_t config);
    int32_t getRefresh(uint32_t id_connector, uint32_t config);
    uint32_t getNumConfigs(uint32_t id_connector);

    int createPropertyBlob(const void *data, size_t length, uint32_t *id);
    int destroyPropertyBlob(uint32_t id);

    uint32_t getMaxSupportWidth();
    uint32_t getMaxSupportHeight();
    unsigned int getMaxPlaneNum() const;

    int32_t updateCrtcToPreferredModeInfo();

    // getCurrentRefresh() gets the current mode fps
    // this is only used for external display
    int32_t getCurrentRefresh(uint32_t id_crtc);

    const mtk_drm_disp_caps_info& getCapsInfo();

    void dump(String8* str);

    int32_t updateConnectorModeToMaximumResolution(uint64_t dpy, uint32_t id_crtc);
    // getSpportedConnectorMode use for get connector's supported mode for changing mode
    int32_t getSpportedConnectorMode(uint64_t dpy, uint32_t id_crtc, uint32_t *modeinfo);
    // getConnectorEDID use for get connector's edid
    int32_t getConnectorEDID(uint64_t dpy, uint32_t id_crtc, uint32_t *edid);
    // setHDMIMode to turn on or off hdmi driver
    int32_t setHDMIMode(uint64_t dpy, uint32_t id_crtc, uint32_t enable);
    uint32_t getHDMIMode(uint64_t dpy, uint32_t id_crtc);
    int32_t getConnectorColorspaceAndDepth(uint64_t dpy, uint32_t id_crtc, uint32_t *colordepth);
    int32_t setConnectorColorspaceAndDepth(uint64_t dpy, uint32_t id_crtc, uint32_t colordepth);
    // getKernelLogoSize() gets the width and height from kernel logo
    int32_t getKernelLogoSize(int32_t *width, int32_t *height);
private:
    DrmModeResource();
    int init();
    void queryCapsInfo();

    int initDrmCap();
    int initDrmResource();
    int initDrmCrtc(drmModeResPtr r);
    int initDrmEncoder(drmModeResPtr r);
    int initDrmConnector(drmModeResPtr r);
    int initDrmPlane();
    void arrangeResource();
    void initDimFbId();

private:
    int m_fd;

    std::vector<DrmModeCrtc*> m_crtc_list;
    std::vector<DrmModeEncoder*> m_encoder_list;
    std::vector<DrmModeConnector*> m_connector_list;
    std::vector<DrmModePlane*> m_plane_list;

    std::vector<CreateDisplayInfo> m_create_display_infos;

    uint32_t m_dim_fb_id;
    uint32_t m_max_support_width;
    uint32_t m_max_support_height;

    std::unordered_set<uint32_t> m_cur_fb_ids;
    std::mutex m_cur_fb_lock;

    mtk_drm_disp_caps_info m_caps_info;

    uint32_t m_hdmi_enable;
    uint32_t m_dp_enable;
    uint32_t m_edp_enable;

    bool m_edp_initialized;
};

#endif
