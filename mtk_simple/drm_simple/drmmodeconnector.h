#ifndef __MTK_HWC_DRM_MODE_CONNECTOR_H__
#define __MTK_HWC_DRM_MODE_CONNECTOR_H__

#include "drmobject.h"
#include "drmmodeinfo.h"
#include "drmmodeproperty.h"

#include <stdint.h>
#include <vector>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#include <xf86drmMode.h>
#pragma clang diagnostic pop

namespace simplehwc {

class DrmModeEncoder;

enum DRM_PROP_CONNECTOR_ENUM
{
    DRM_PROP_CONNECTOR_DPMS = 0,
    DRM_PROP_CONNECTOR_CRTC_ID,
    DRM_PROP_CONNECTOR_MAX,
};

class DrmModeConnector : public DrmObject
{
public:
    DrmModeConnector(drmModeConnectorPtr c);
    ~DrmModeConnector();

    int init(int fd);
    uint32_t getId();
    uint32_t getEncoderId();
    uint32_t getMmWidth();
    uint32_t getMmHeight();
    uint32_t getModeWidth(uint32_t index = 0);
    uint32_t getModeHeight(uint32_t index = 0);
    uint32_t getModeRefresh(uint32_t index = 0);
    bool getMode(DrmModeInfo* mode, uint32_t index = 0, bool use_preferred = 0);
    uint32_t getModeNum() {return static_cast<uint32_t>(m_modes.size());};
    uint32_t getConnectorType();

    void arrangeEncoder(std::vector<DrmModeEncoder*>& encoders);
    int connectEncoder(DrmModeEncoder *encoder);

    int setDrmModeInfo(drmModeConnectorPtr c);

    void dump();

protected:
    virtual void initObject();

private:
    uint32_t m_encoder_id;
    uint32_t m_connector_type;
    uint32_t m_connector_type_id;
    uint32_t m_mm_width;
    uint32_t m_mm_height;
    int m_count_encoders;
    uint32_t *m_possible_encoder_id;

    DrmModeEncoder *m_encoder;
    std::vector<DrmModeEncoder*> m_possible_encoder_list;

    DrmModeProperty m_prop[DRM_PROP_CONNECTOR_MAX];
    std::pair<int, std::string> m_prop_table[DRM_PROP_CONNECTOR_MAX] = {
        {DRM_PROP_CONNECTOR_DPMS, std::string("DPMS")},
        {DRM_PROP_CONNECTOR_CRTC_ID, std::string("CRTC_ID")},
    };

    std::vector<DrmModeInfo> m_modes;
};

}  // namespace simplehwc

#endif
