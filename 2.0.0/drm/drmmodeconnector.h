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

class DrmModeEncoder;

enum DRM_PROP_CONNECTOR_ENUM
{
    DRM_PROP_CONNECTOR_DPMS = 0,
    DRM_PROP_CONNECTOR_CRTC_ID,
    DRM_PROP_CONNECTOR_MAX,
};

enum DRM_CONNECTOR_EDID_ENUM
{
    DRM_CONNECTOR_EDID_2K_0 = 0,
    DRM_CONNECTOR_EDID_2K_1,
    DRM_CONNECTOR_EDID_4K_0,
    DRM_CONNECTOR_EDID_COLORIMETRY,
    DRM_CONNECTOR_EDID_RGB_COLOR_BIT,
    DRM_CONNECTOR_EDID_YCBCR_COLOR_BIT,
    DRM_CONNECTOR_EDID_DC420_COLOR_BIT,
    DRM_CONNECTOR_EDID_DYNAMIC_HDR,
    DRM_CONNECTOR_EDID_TMDS_RATE,
    DRM_CONNECTOR_EDID_TMDS_CLK,
    DRM_CONNECTOR_EDID_MAX
};

enum DRM_CONNECTOR_INFO_ENUM
{
    DRM_CONNECTOR_INFO_WIDTH = 0,
    DRM_CONNECTOR_INFO_HEIGHT,
    DRM_CONNECTOR_INFO_REFRESH,
    DRM_CONNECTOR_INFO_MAX
};

class DrmModeConnector : public DrmObject
{
public:
    DrmModeConnector(drmModeConnectorPtr c);
    ~DrmModeConnector();

    int init(int fd);
    uint32_t getEncoderId();
    uint32_t getMmWidth();
    uint32_t getMmHeight();
    uint32_t getModeWidth(uint32_t index = 0);
    uint32_t getModeHeight(uint32_t index = 0);
    uint32_t getModeRefresh(uint32_t index = 0);
    bool getMode(DrmModeInfo* mode, uint32_t index = 0, bool use_preferred = 0);
    uint32_t getModeNum() {return static_cast<uint32_t>(m_modes.size());};
    uint32_t getConnectorType();
    bool isConnectorTypeExternal();

    void arrangeEncoder(std::vector<DrmModeEncoder*>& encoders);
    int connectEncoder(DrmModeEncoder *encoder);

    int setDrmModeInfo(drmModeConnectorPtr c);

    void dump();

    int32_t getSupportedMode(uint32_t *modeinfo);
    int32_t getEDID(int fd, uint32_t *edid);
    int32_t mapModeToEDID(const uint32_t w, const uint32_t h, const uint32_t vrefresh);
    int32_t getColorspaceAndDepth(int fd, uint32_t *colordepth);
    int32_t setColorspaceAndDepth(int fd, uint32_t colordepth);

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
    uint32_t m_changed_modeinfo[DRM_CONNECTOR_INFO_MAX];
    uint32_t m_edid[DRM_CONNECTOR_EDID_MAX];
    uint32_t m_cur_mode;
};

#endif
