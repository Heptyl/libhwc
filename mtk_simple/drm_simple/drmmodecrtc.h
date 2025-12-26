#pragma once
#include "drmobject.h"
#include "drmmodeinfo.h"
#include "drmmodeproperty.h"
#include "drmmodeutils.h"
#include "dev_interface_simple.h"

#include <stdint.h>
#include <vector>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#include <xf86drmMode.h>
#pragma clang diagnostic pop

namespace simplehwc {

class DrmModeResource;
class DrmModePlane;
class DrmModeEncoder;

enum DRM_PROP_CRTC_ENUM
{
    DRM_PROP_CRTC_ACTIVE = 0,
    DRM_PROP_CRTC_MODE_ID,
    DRM_PROP_CRTC_PRESENT_FENCE,
    DRM_PROP_CRTC_MAX,
};

class DrmModeCrtc : public DrmObject
{
public:
    DrmModeCrtc(DrmModeResource *drm, drmModeCrtcPtr c);
    ~DrmModeCrtc();

    int init(int fd, uint32_t pipe);
    int prepareFb();
    int destroyFb();
    int destroyFb(struct hwc_drm_bo &fb_bo);
    uint32_t getFbId(uint32_t pinpon = 0);

    uint32_t getId();
    uint32_t getPipe();

    void dump();

    void setEncoder(DrmModeEncoder *encoder);
    DrmModeEncoder* getEncoder();
    void addPlane(DrmModePlane *plane);
    void clearPlane();
    void setMode(DrmModeInfo& mode);
    uint32_t getVirWidth();
    uint32_t getVirHeight();
    uint32_t getPhyWidth();
    uint32_t getPhyHeight();
    size_t getPlaneNum();
    const DrmModePlane* getPlane(size_t index);
    void setReqSize(uint32_t width, uint32_t height);
    uint32_t getReqWidth();
    uint32_t getReqHeight();
    struct hwc_drm_bo getDumbBuffer(uint32_t pinpon = 0);
    uint32_t getCurrentModeRefresh();
protected:
    virtual void initObject();

private:
    DrmModeResource* m_drm;
    uint32_t m_pipe;

    uint32_t m_width;
    uint32_t m_height;
    uint32_t m_req_width;
    uint32_t m_req_height;

    DrmModeInfo m_mode;
    bool m_mode_valid;

    DrmModeProperty m_prop[DRM_PROP_CRTC_MAX];
    std::pair<int, std::string> m_prop_table[DRM_PROP_CRTC_MAX] = {
        {DRM_PROP_CRTC_ACTIVE, std::string("ACTIVE")},
        {DRM_PROP_CRTC_MODE_ID, std::string("MODE_ID")},
        {DRM_PROP_CRTC_PRESENT_FENCE, std::string("PRESENT_FENCE")},
    };

    std::vector<DrmModePlane*> m_planes;
    DrmModeEncoder *m_encoder;

    struct hwc_drm_bo m_fb_bo[MAX_BO_SIZE];
};

}  // namespace simplehwc

