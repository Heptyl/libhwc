#ifndef __MTK_HWC_DRM_MODE_PLANE_H__
#define __MTK_HWC_DRM_MODE_PLANE_H__

#include "drmobject.h"
#include "drmmodecrtc.h"
#include "drmmodeproperty.h"

#include <stdint.h>
#include <xf86drmMode.h>
#include <vector>

class DrmModeCrtc;

enum DRM_PROP_PLANE_ENUM
{
    DRM_PROP_PLANE_CRTC_ID = 0,
    DRM_PROP_PLANE_FB_ID,
    DRM_PROP_PLANE_CRTC_X,
    DRM_PROP_PLANE_CRTC_Y,
    DRM_PROP_PLANE_CRTC_W,
    DRM_PROP_PLANE_CRTC_H,
    DRM_PROP_PLANE_SRC_X,
    DRM_PROP_PLANE_SRC_Y,
    DRM_PROP_PLANE_SRC_W,
    DRM_PROP_PLANE_SRC_H,
    DRM_PROP_PLANE_NEXT_BUFFER_IDX,
    DRM_PROP_PLANE_DATASPACE,
    DRM_PROP_PLANE_VPITCH,
    DRM_PROP_PLANE_COMPRESS,
    DRM_PROP_PLANE_PLANE_ALPHA,
    DRM_PROP_PLANE_ALPHA_CON,
    DRM_PROP_PLANE_DIM_COLOR,
    DRM_PROP_PLANE_IS_MML,
    DRM_PROP_PLANE_MML_SUBMIT,
    DRM_PROP_PLANE_BUFFER_ALLOC_ID,
    DRM_PROP_PLANE_MAX,
};

class DrmModePlane : public DrmObject
{
public:
    DrmModePlane(drmModePlanePtr p);
    ~DrmModePlane();

    int init(int fd);
    uint32_t getCrtcId() const;

    void dump();

    void arrangeCrtc(std::vector<DrmModeCrtc*>& crtcs);
    int connectCrtc(DrmModeCrtc *crtc);

protected:
    virtual void initObject();

private:
    uint32_t m_crtc_id;
    uint32_t m_possible_crtcs;

    DrmModeProperty m_prop[DRM_PROP_PLANE_MAX];
    std::pair<int, std::string> m_prop_table[DRM_PROP_PLANE_MAX] = {
        {DRM_PROP_PLANE_CRTC_ID, std::string("CRTC_ID")},
        {DRM_PROP_PLANE_FB_ID, std::string("FB_ID")},
        {DRM_PROP_PLANE_CRTC_X, std::string("CRTC_X")},
        {DRM_PROP_PLANE_CRTC_Y, std::string("CRTC_Y")},
        {DRM_PROP_PLANE_CRTC_W, std::string("CRTC_W")},
        {DRM_PROP_PLANE_CRTC_H, std::string("CRTC_H")},
        {DRM_PROP_PLANE_SRC_X, std::string("SRC_X")},
        {DRM_PROP_PLANE_SRC_Y, std::string("SRC_Y")},
        {DRM_PROP_PLANE_SRC_W, std::string("SRC_W")},
        {DRM_PROP_PLANE_SRC_H, std::string("SRC_H")},
        {DRM_PROP_PLANE_NEXT_BUFFER_IDX, std::string("NEXT_BUFF_IDX")},
        {DRM_PROP_PLANE_DATASPACE, std::string("DATASPACE")},
        {DRM_PROP_PLANE_VPITCH, std::string("VPITCH")},
        {DRM_PROP_PLANE_COMPRESS, std::string("COMPRESS")},
        {DRM_PROP_PLANE_PLANE_ALPHA, std::string("PLANE_PROP_PLANE_ALPHA")},
        {DRM_PROP_PLANE_ALPHA_CON, std::string("PLANE_PROP_ALPHA_CON")},
        {DRM_PROP_PLANE_DIM_COLOR, std::string("DIM_COLOR")},
        {DRM_PROP_PLANE_IS_MML, std::string("IS_MML")},
        {DRM_PROP_PLANE_MML_SUBMIT, std::string("MML_SUBMIT")},
        {DRM_PROP_PLANE_BUFFER_ALLOC_ID, std::string("BUFFER_ALLOC_ID")},
    };

    DrmModeCrtc *m_crtc;
    std::vector<DrmModeCrtc*> m_possible_crtc_list;
};

#endif
