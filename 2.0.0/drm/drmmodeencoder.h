#ifndef __MTK_HWC_DRM_MODE_ENCODER_H__
#define __MTK_HWC_DRM_MODE_ENCODER_H__

#include "drmobject.h"
#include "drmmodecrtc.h"

#include <stdint.h>
#include <vector>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#include <xf86drmMode.h>
#pragma clang diagnostic pop

class DrmModeCrtc;
class DrmModeConnector;

class DrmModeEncoder : public DrmObject
{
public:
    DrmModeEncoder(drmModeEncoderPtr e);
    ~DrmModeEncoder();

    int init(int fd);

    uint32_t getCrtcId();
    uint32_t getPossibleCrtc();

    void arrangeCrtc(std::vector<DrmModeCrtc*>& crtcs);
    int connectCrtc(DrmModeCrtc *crtc);
    void dump();
    void setConnector(DrmModeConnector *connector);
    DrmModeConnector* getConnector();

protected:
    virtual void initObject();

private:
    uint32_t m_type;
    uint32_t m_crtc_id;
    uint32_t m_possible_crtcs;

    DrmModeCrtc *m_crtc;
    std::vector<DrmModeCrtc*> m_possible_crtc_list;

    DrmModeConnector *m_connector;
};

#endif
