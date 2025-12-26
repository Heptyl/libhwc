#ifndef __MTK_HWC_DRM_MODE_CRTC_H__
#define __MTK_HWC_DRM_MODE_CRTC_H__

#include "drmobject.h"
#include "drmmodeinfo.h"
#include "drmmodeproperty.h"
#include "drmmodeutils.h"

#include <stdint.h>
#include <vector>

#include <linux/mediatek_drm.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#include <xf86drmMode.h>
#pragma clang diagnostic pop

class DrmModeResource;
class DrmModePlane;
class DrmModeEncoder;

enum DRM_PROP_CRTC_ENUM
{
    DRM_PROP_CRTC_ACTIVE = 0,
    DRM_PROP_CRTC_MODE_ID,
    DRM_PROP_CRTC_OVERLAP_LAYER_NUM,
    DRM_PROP_CRTC_LAYERING_IDX,
    DRM_PROP_CRTC_PRESENT_FENCE,
    DRM_PROP_CRTC_DOZE_ACTIVE,
    DRM_PROP_CRTC_OUTPUT_ENABLE,
    DRM_PROP_CRTC_OUTPUT_BUFF_IDX,
    DRM_PROP_CRTC_OUTPUT_X,
    DRM_PROP_CRTC_OUTPUT_Y,
    DRM_PROP_CRTC_OUTPUT_WIDTH,
    DRM_PROP_CRTC_OUTPUT_HEIGHT,
    DRM_PROP_CRTC_OUTPUT_DST_WIDTH,
    DRM_PROP_CRTC_OUTPUT_DST_HEIGHT,
    DRM_PROP_CRTC_INTF_BUFF_IDX,
    DRM_PROP_CRTC_OUTPUT_FB_ID,
    DRM_PROP_CRTC_DISP_MODE_IDX,
    DRM_PROP_CRTC_COLOR_TRANSFORM,
    DRM_PROP_CRTC_USER_SCEN,
#ifdef MTK_IN_DISPLAY_FINGERPRINT
    DRM_PROP_CRTC_HBM_ENABLE,
#endif
#ifdef MTK_HDR_SET_DISPLAY_COLOR
    DRM_PROP_CRTC_HDR_ENABLE,
#endif
    DRM_PROP_CRTC_MSYNC_2_0_ENABLE,
    DRM_PROP_CRTC_OVL_DSI_SEQ,
    DRM_PROP_CRTC_OUTPUT_SCENARIO,
    DRM_PROP_CRTC_CAPS_BLOB_ID,
    DRM_PROP_CRTC_BOOTLOGO_DISABLE,
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
    uint32_t getFbId();

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
    struct hwc_drm_bo getDumbBuffer();
    uint32_t getCurrentModeRefresh();

    void setMainCrtc();
    bool isMainCrtc() const;
    void setSupportMml();
    bool isSupportMml() const;
    void setSupportRpo();
    bool isSupportRpo() const;

    unsigned int getSessionId() const;
    void setSessionId(unsigned int session_id);
    unsigned int getSessionMode() const;
    void setSessionMode(unsigned int session_mode);

    bool isSupportWb() const;
    bool isSupportWbParams(uint64_t dump_point,
                           unsigned int disp_w, unsigned int disp_h,
                           unsigned int src_w, unsigned int src_h,
                           unsigned int dst_w, unsigned int dst_h) const;

protected:
    virtual void initObject();

private:
    void getCaps(int fd);

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
        {DRM_PROP_CRTC_OVERLAP_LAYER_NUM, std::string("OVERLAP_LAYER_NUM")},
        {DRM_PROP_CRTC_LAYERING_IDX, std::string("LAYERING_IDX")},
        {DRM_PROP_CRTC_PRESENT_FENCE, std::string("PRESENT_FENCE")},
        {DRM_PROP_CRTC_DOZE_ACTIVE, std::string("DOZE_ACTIVE")},
        {DRM_PROP_CRTC_OUTPUT_ENABLE, std::string("OUTPUT_ENABLE")},
        {DRM_PROP_CRTC_OUTPUT_BUFF_IDX, std::string("OUTPUT_BUFF_IDX")},
        {DRM_PROP_CRTC_OUTPUT_X, std::string("OUTPUT_X")},
        {DRM_PROP_CRTC_OUTPUT_Y, std::string("OUTPUT_Y")},
        {DRM_PROP_CRTC_OUTPUT_WIDTH, std::string("OUTPUT_WIDTH")},
        {DRM_PROP_CRTC_OUTPUT_HEIGHT, std::string("OUTPUT_HEIGHT")},
        {DRM_PROP_CRTC_OUTPUT_DST_WIDTH, std::string("OUTPUT_DST_WIDTH")},
        {DRM_PROP_CRTC_OUTPUT_DST_HEIGHT, std::string("OUTPUT_DST_HEIGHT")},
        {DRM_PROP_CRTC_INTF_BUFF_IDX, std::string("INTF_BUFF_IDX")},
        {DRM_PROP_CRTC_OUTPUT_FB_ID, std::string("OUTPUT_FB_ID")},
        {DRM_PROP_CRTC_DISP_MODE_IDX, std::string("DISP_MODE_IDX")},
        {DRM_PROP_CRTC_COLOR_TRANSFORM, std::string("COLOR_TRANSFORM")},
        {DRM_PROP_CRTC_USER_SCEN, std::string("USER_SCEN")},
#ifdef MTK_IN_DISPLAY_FINGERPRINT
        {DRM_PROP_CRTC_HBM_ENABLE, std::string("HBM_ENABLE")},
#endif
#ifdef MTK_HDR_SET_DISPLAY_COLOR
        {DRM_PROP_CRTC_HDR_ENABLE, std::string("HDR_ENABLE")},
#endif
        {DRM_PROP_CRTC_MSYNC_2_0_ENABLE, std::string("MSYNC2_0_ENABLE")},
        {DRM_PROP_CRTC_OVL_DSI_SEQ, std::string("OVL_DSI_SEQ")},
        {DRM_PROP_CRTC_OUTPUT_SCENARIO, std::string("OUTPUT_SCENARIO")},
        {DRM_PROP_CRTC_CAPS_BLOB_ID, std::string("CAPS_BLOB_ID")},
        {DRM_PROP_CRTC_BOOTLOGO_DISABLE, std::string("BOOTLOGO_DISABLE")},
    };

    std::vector<DrmModePlane*> m_planes;
    DrmModeEncoder *m_encoder;

    struct hwc_drm_bo m_fb_bo;

    bool m_main_crtc;
    bool m_is_support_mml;
    bool m_is_support_rpo;

    unsigned int m_session_id;
    unsigned int m_session_mode;

    struct mtk_drm_crtc_caps m_caps;
};

#endif
