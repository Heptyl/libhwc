#define DEBUG_LOG_TAG "DRMDEV"
#include "drmmodeconnector.h"

#include <cutils/log.h>
#include <cutils/properties.h>
#include <errno.h>
#include <sstream>

#include "utils/debug.h"

#include "drmmodeencoder.h"
#include "hwc2.h"

#ifdef USES_HDMISERVICE
#include "hdmi_interface.h"
#endif

DrmModeConnector::DrmModeConnector(drmModeConnectorPtr c)
    : DrmObject(DRM_MODE_OBJECT_CONNECTOR, c->connector_id)
    , m_encoder_id(c->encoder_id)
    , m_connector_type(c->connector_type)
    , m_connector_type_id(c->connector_type_id)
    , m_mm_width(c->mmWidth)
    , m_mm_height(c->mmHeight)
    , m_count_encoders(c->count_encoders)
    , m_possible_encoder_id(nullptr)
    , m_encoder(nullptr)
    , m_cur_mode(0)
{
    initObject();

    if (m_count_encoders > 0)
    {
        unsigned int count_encoders = static_cast<unsigned int>(m_count_encoders);
        m_possible_encoder_id = new uint32_t[count_encoders];
        for (unsigned int i = 0; i < count_encoders; i++)
        {
            m_possible_encoder_id[i] = c->encoders[i];
        }
    }

    if (c->count_modes > 0)
    {
        for (unsigned int i = 0; i < static_cast<unsigned int>(c->count_modes); i++)
        {
            m_modes.push_back(&c->modes[i]);
        }
    }

    memset(m_changed_modeinfo, 0, sizeof(m_changed_modeinfo));
    if (HwcFeatureList::getInstance().getFeature().hdmi_service_support &&
        (m_connector_type == DRM_MODE_CONNECTOR_HDMIA))
    {
        char value[PROPERTY_VALUE_MAX];
        uint32_t mode;
        property_get("persist.vendor.sys.hdmi_hidl.resolution", value, "-1");
        mode = (uint32_t)atoi(value);

        uint32_t modeinfo[4];

        mapHdmiModeToConnectorMode(mode, modeinfo);
        m_changed_modeinfo[DRM_CONNECTOR_INFO_WIDTH] = modeinfo[DRM_CONNECTOR_INFO_WIDTH];
        m_changed_modeinfo[DRM_CONNECTOR_INFO_HEIGHT] = modeinfo[DRM_CONNECTOR_INFO_HEIGHT];
        m_changed_modeinfo[DRM_CONNECTOR_INFO_REFRESH] = modeinfo[DRM_CONNECTOR_INFO_REFRESH];

        HWC_LOGI("DrmModeConnector persist.vendor.sys.hdmi_hidl.resolution=%d", mode);
     }
}

DrmModeConnector::~DrmModeConnector()
{
    if (m_possible_encoder_id)
    {
        delete[] m_possible_encoder_id;
    }
}

void DrmModeConnector::initObject()
{
    m_prop_size = sizeof(m_prop) / sizeof(*m_prop);
    m_prop_list = m_prop_table;
    m_property = m_prop;
}

int DrmModeConnector::init(int fd)
{
    int res = 0;

    res = initProperty(fd);
    if (res)
    {
        HWC_LOGE("failed to init connector[%d] property: errno[%d]", m_id, res);
        return res;
    }

    res = checkProperty();
    if (res)
    {
        HWC_LOGE("failed to check connector[%d] property: error=%d", m_id, res);
        return res;
    }

    return res;
}

uint32_t DrmModeConnector::getEncoderId()
{
    return m_encoder_id;
}

uint32_t DrmModeConnector::getMmWidth()
{
    return m_mm_width;
}

uint32_t DrmModeConnector::getMmHeight()
{
    return m_mm_height;
}

uint32_t DrmModeConnector::getModeWidth(uint32_t index)
{
    if (index == 0 && !m_modes.empty())
    {
        return m_modes[index].getDisplayH();
    }
    else if (index != 0 && m_modes.size() > index)
    {
        return m_modes[index].getDisplayH();
    }
    return 0;
}

uint32_t DrmModeConnector::getModeHeight(uint32_t index)
{
    if (index == 0 && !m_modes.empty())
    {
        return m_modes[index].getDisplayV();
    }
    else if (index != 0 && m_modes.size() > index)
    {
        return m_modes[index].getDisplayV();
    }
    return 0;
}

uint32_t DrmModeConnector::getModeRefresh(uint32_t index)
{
    if (index == 0 && !m_modes.empty())
    {
        return m_modes[index].getVRefresh();
    }
    else if (index != 0 && m_modes.size() > index)
    {
        return m_modes[index].getVRefresh();
    }
    return 0;
}

bool DrmModeConnector::getMode(DrmModeInfo* mode, uint32_t index, bool use_preferred)
{
    bool mode_funnd = false;

    if (use_preferred && !m_modes.empty())
    {
        size_t currentIndex = 0;
        for (size_t i = 0 ; i < m_modes.size(); i++)
        {
            if (m_changed_modeinfo[DRM_CONNECTOR_INFO_WIDTH] == m_modes[i].getDisplayH() &&
                m_changed_modeinfo[DRM_CONNECTOR_INFO_HEIGHT] == m_modes[i].getDisplayV() &&
                m_changed_modeinfo[DRM_CONNECTOR_INFO_REFRESH] == m_modes[i].getVRefresh())
            {
                if (!mode_funnd)
                {
                    /* select first hit setting */
                    currentIndex = i;
                    mode_funnd = true;
                }
            }
        }
        *mode = m_modes[currentIndex];
        m_cur_mode = (uint32_t)currentIndex;
        return true;
    }
    else if (index == 0 && !m_modes.empty())
    {
        *mode = m_modes[index];
        return true;
    }
    else if (index != 0 && m_modes.size() > index)
    {
        *mode = m_modes[index];
        return true;
    }
    return false;
}

void DrmModeConnector::arrangeEncoder(std::vector<DrmModeEncoder*>& encoders)
{
    for (size_t i = 0; i < encoders.size(); i++)
    {
        if (encoders[i]->getId() == m_encoder_id)
        {
            m_encoder = encoders[i];
        }

        if (m_count_encoders > 0)
        {
            unsigned int count_encoders = static_cast<unsigned int>(m_count_encoders);

            for (unsigned int j = 0; j < count_encoders; j++)
            {
                if (m_possible_encoder_id[j] == encoders[i]->getId())
                {
                    m_possible_encoder_list.push_back(encoders[i]);
                }
            }
        }
    }
}

int DrmModeConnector::connectEncoder(DrmModeEncoder *encoder)
{
    int res = -1;
    if (encoder == nullptr)
    {
        HWC_LOGW("try to connect connector_%d with nullptr encoder", m_id);
        return res;
    }

    if (m_count_encoders > 0)
    {
        unsigned int count_encoders = static_cast<unsigned int>(m_count_encoders);
        for (unsigned int i = 0; i < count_encoders; i++)
        {
            if (m_possible_encoder_id[i] == encoder->getId())
            {
                m_encoder = encoder;
                m_encoder_id = encoder->getId();
                m_encoder->setConnector(this);
                res = 0;
                break;
            }
        }
    }

    return res;
}

void DrmModeConnector::dump()
{
    std::ostringstream ss;
    if (m_count_encoders > 0)
    {
        unsigned int count_encoders = static_cast<unsigned int>(m_count_encoders);

        for (unsigned int i = 0; i < count_encoders; i++)
        {
            ss << m_possible_encoder_id[i] << ", " << std::endl;
        }
    }

    HWC_LOGI("DrmModeConnector: id=%d encoder_id=%d type=%d type_id=%d mm_size=%dx%d count=%d possible_encoder_id=%s mode_count=%zu",
            m_id, m_encoder_id, m_connector_type, m_connector_type_id,
            m_mm_width, m_mm_height, m_count_encoders, ss.str().c_str(), m_modes.size());

    for (size_t i = 0 ; i < m_modes.size(); i++)
    {
        DrmModeInfo mode = m_modes[i];
        drmModeModeInfo modeInfo;
        mode.getModeInfo(&modeInfo);
        HWC_LOGI("DrmModeConnector mode index:%zu clock:%d w:%d h:%d vrefresh:%d type:0x%x",
        i, modeInfo.clock, modeInfo.hdisplay, modeInfo.vdisplay, modeInfo.vrefresh, modeInfo.type);
    }
}

uint32_t DrmModeConnector::getConnectorType()
{
    return m_connector_type;
}

bool DrmModeConnector::isConnectorTypeExternal()
{
    return m_connector_type == DRM_MODE_CONNECTOR_eDP ||
           m_connector_type == DRM_MODE_CONNECTOR_DPI ||
           m_connector_type == DRM_MODE_CONNECTOR_DisplayPort ||
           m_connector_type == DRM_MODE_CONNECTOR_HDMIA;
}

int DrmModeConnector::setDrmModeInfo(drmModeConnectorPtr c)
{
    if (c->count_modes > 0)
    {
        m_modes.clear();
        for (unsigned int i = 0; i < static_cast<unsigned int>(c->count_modes); i++)
        {
            m_modes.push_back(&c->modes[i]);
        }
    }
    dump();
    return 0;
}

int32_t DrmModeConnector::getSupportedMode(uint32_t *modeinfo)
{
    int32_t mode_id = -1;

    if (m_modes.empty())
    {
        HWC_LOGW("%s: modes is empty", __func__);
        return mode_id;
    }

    for (size_t i = 0 ; i < m_modes.size(); i++)
    {
        if (modeinfo[DRM_CONNECTOR_INFO_WIDTH] == m_modes[i].getDisplayH() &&
            modeinfo[DRM_CONNECTOR_INFO_HEIGHT] == m_modes[i].getDisplayV() &&
            modeinfo[DRM_CONNECTOR_INFO_REFRESH] == m_modes[i].getVRefresh())
        {
            m_changed_modeinfo[DRM_CONNECTOR_INFO_WIDTH] = modeinfo[DRM_CONNECTOR_INFO_WIDTH];
            m_changed_modeinfo[DRM_CONNECTOR_INFO_HEIGHT] = modeinfo[DRM_CONNECTOR_INFO_HEIGHT];
            m_changed_modeinfo[DRM_CONNECTOR_INFO_REFRESH] = modeinfo[DRM_CONNECTOR_INFO_REFRESH];
            mode_id = (int32_t)i;
            break;
        }
    }

    /* if current mode is equal to changed mode, no need to change */
    if ((int32_t)m_cur_mode == mode_id) mode_id = -1;

    if (modeinfo[DRM_CONNECTOR_INFO_WIDTH] == m_modes[m_cur_mode].getDisplayH() &&
        modeinfo[DRM_CONNECTOR_INFO_HEIGHT] == m_modes[m_cur_mode].getDisplayV() &&
        modeinfo[DRM_CONNECTOR_INFO_REFRESH] == m_modes[m_cur_mode].getVRefresh())
    {
        mode_id = -2;
    }

    return mode_id;
}

int32_t DrmModeConnector::getEDID(int fd, uint32_t *edid)
{
    memset(m_edid, 0, sizeof(m_edid));
    if (!m_modes.empty())
    {
        for (size_t i = 0 ; i < m_modes.size(); i++)
        {
            HWC_LOGI("DrmModeConnector mode index:%zu w:%u h:%u vrefresh:%u",
                    i, m_modes[i].getDisplayH(), m_modes[i].getDisplayV(),
                    m_modes[i].getVRefresh());
            mapModeToEDID(m_modes[i].getDisplayH(), m_modes[i].getDisplayV(), m_modes[i].getVRefresh());
        }
    }
    drmModeObjectPropertiesPtr props;
    props = drmModeObjectGetProperties(fd, m_id, m_obj_type);
    if (props == nullptr)
    {
        HWC_LOGW("%s failed to get 0x%x[%d] properties",__func__, m_obj_type, m_id);
        return -ENODEV;
    }

    int res = 0;
    for (uint32_t i = 0; i < props->count_props; i++)
    {
        drmModePropertyPtr p = drmModeGetProperty(fd, props->props[i]);
        if (!p)
        {
            HWC_LOGW("failed to get 0x%x[%d] property[%x]", m_obj_type, m_id, props->props[i]);
            drmModeFreeProperty(p);
            drmModeFreeObjectProperties(props);
            return -ENODEV;
        }
        if (!strcmp("HDMI_INFO", p->name))
        {
            if (p->flags & DRM_MODE_PROP_BLOB)
            {
                HWC_LOGD("HDMI_INFO prop is BLOB type");
                res = -1;
            }
            drmModePropertyBlobPtr blob;

            uint32_t blob_id = static_cast<uint32_t>(props->prop_values[i]);
            blob = drmModeGetPropertyBlob(fd, blob_id);
            if (!blob)
            {
                HWC_LOGE("cannot get HDMI_INFO blob");
                res = -1;
            }
            else
            {
                struct mtk_hdmi_info hdmi_info;
                memcpy(&hdmi_info, blob->data, sizeof(hdmi_info));

                m_edid[DRM_CONNECTOR_EDID_COLORIMETRY]     = hdmi_info.edid_sink_colorimetry;
                m_edid[DRM_CONNECTOR_EDID_RGB_COLOR_BIT]   = hdmi_info.edid_sink_rgb_color_bit;
                m_edid[DRM_CONNECTOR_EDID_YCBCR_COLOR_BIT] = hdmi_info.edid_sink_ycbcr_color_bit;
                m_edid[DRM_CONNECTOR_EDID_DC420_COLOR_BIT] = hdmi_info.ui1_sink_dc420_color_bit;
                m_edid[DRM_CONNECTOR_EDID_TMDS_CLK]        = hdmi_info.edid_sink_max_tmds_clock;
                m_edid[DRM_CONNECTOR_EDID_TMDS_RATE]       = hdmi_info.edid_sink_max_tmds_character_rate;
                m_edid[DRM_CONNECTOR_EDID_DYNAMIC_HDR]     = hdmi_info.edid_sink_support_dynamic_hdr;
            }

            drmModeFreePropertyBlob(blob);
        }
        drmModeFreeProperty(p);
    }
    drmModeFreeObjectProperties(props);

    memcpy(edid, m_edid, sizeof(m_edid));

    return res;
}

int32_t DrmModeConnector::getColorspaceAndDepth(int fd, uint32_t *colordepth)
{
    bool find_prop = false;
    drmModeObjectPropertiesPtr props;

    props = drmModeObjectGetProperties(fd, m_id, m_obj_type);
    if (props == nullptr)
    {
        HWC_LOGW("%s failed to get 0x%x[%d] properties",__func__, m_obj_type, m_id);
        *colordepth = 0;
        return -ENODEV;
    }

    for (uint32_t i = 0; i < props->count_props; i++)
    {
        drmModePropertyPtr p = drmModeGetProperty(fd, props->props[i]);
        if (!p)
        {
            HWC_LOGW("failed to get 0x%x[%d] property[%x]", m_obj_type, m_id, props->props[i]);
            *colordepth = 0;
            drmModeFreeProperty(p);
            drmModeFreeObjectProperties(props);
            return -ENODEV;
        }
        if (!strcmp("hdmi_colorspace_depth", p->name))
        {
            find_prop = true;
            *colordepth = static_cast<uint32_t>(props->prop_values[i]);
        }
        drmModeFreeProperty(p);
    }
    drmModeFreeObjectProperties(props);

    if (!find_prop)
        *colordepth = 0;

    return 0;
}

int32_t DrmModeConnector::setColorspaceAndDepth(int fd, uint32_t colordepth)
{
    drmModeObjectPropertiesPtr props;

    props = drmModeObjectGetProperties(fd, m_id, m_obj_type);
    if (props == nullptr)
    {
        HWC_LOGW("%s failed to get 0x%x[%d] properties",__func__, m_obj_type, m_id);
        return -ENODEV;
    }

    for (uint32_t i = 0; i < props->count_props; i++)
    {
        drmModePropertyPtr p = drmModeGetProperty(fd, props->props[i]);
        if (!p)
        {
            HWC_LOGW("failed to get 0x%x[%d] property[%x]", m_obj_type, m_id, props->props[i]);
            drmModeFreeProperty(p);
            drmModeFreeObjectProperties(props);
            return -ENODEV;
        }
        if (!strcmp("hdmi_colorspace_depth", p->name))
        {
            drmModeObjectSetProperty(fd, m_id, DRM_MODE_OBJECT_CONNECTOR, props->props[i], colordepth);
        }
        drmModeFreeProperty(p);
    }
    drmModeFreeObjectProperties(props);

    return 0;
}

int32_t DrmModeConnector::mapModeToEDID(const uint32_t w, const uint32_t h, const uint32_t vrefresh)
{
    if ((w == 720) && (h == 480) && (vrefresh == 60))
    {
        m_edid[DRM_CONNECTOR_EDID_2K_0] |= SINK_480P;
        return 0;
    }

    if ((w == 1280) && (h == 720) && (vrefresh == 60))
    {
        m_edid[DRM_CONNECTOR_EDID_2K_0] |= SINK_720P60;
        return 0;
    }

    if ((w == 1280) && (h == 720) && (vrefresh == 50))
    {
        m_edid[DRM_CONNECTOR_EDID_2K_0] |= SINK_720P50;
        return 0;
    }

    if ((w == 720) && (h == 576) && (vrefresh == 60))
    {
        m_edid[DRM_CONNECTOR_EDID_2K_0] |= SINK_576P;
        return 0;
    }

    if ((w == 1920) && (h == 1080) && (vrefresh == 60))
    {
        m_edid[DRM_CONNECTOR_EDID_2K_0] |= SINK_1080P60;
        return 0;
    }

    if ((w == 1920) && (h == 1080) && (vrefresh == 50))
    {
        m_edid[DRM_CONNECTOR_EDID_2K_0] |= SINK_1080P50;
        return 0;
    }

    if ((w == 1920) && (h == 1080) && (vrefresh == 30))
    {
        m_edid[DRM_CONNECTOR_EDID_2K_0] |= SINK_1080P30;
        return 0;
    }

    if ((w == 1920) && (h == 1080) && (vrefresh == 25))
    {
        m_edid[DRM_CONNECTOR_EDID_2K_0] |= SINK_1080P25;
        return 0;
    }

    if ((w == 1920) && (h == 1080) && (vrefresh == 24))
    {
        m_edid[DRM_CONNECTOR_EDID_2K_0] |= SINK_1080P24;
        return 0;
    }

    if ((w == 1920) && (h == 1080) && (vrefresh == 23))
    {
        m_edid[DRM_CONNECTOR_EDID_2K_0] |= SINK_1080P23976;
        return 0;
    }

    if ((w == 1920) && (h == 1080) && (vrefresh == 29))
    {
        m_edid[DRM_CONNECTOR_EDID_2K_0] |= SINK_1080P2997;
        return 0;
    }

    if ((w == 3840) && (h == 2160) && (vrefresh == 23))
    {
        m_edid[DRM_CONNECTOR_EDID_4K_0] |= SINK_2160P_23_976HZ;
        return 0;
    }

    if ((w == 3840) && (h == 2160) && (vrefresh == 24))
    {
        m_edid[DRM_CONNECTOR_EDID_4K_0] |= SINK_2160P_24HZ;
        return 0;
    }

    if ((w == 3840) && (h == 2160) && (vrefresh == 25))
    {
        m_edid[DRM_CONNECTOR_EDID_4K_0] |= SINK_2160P_25HZ;
        return 0;
    }

    if ((w == 3840) && (h == 2160) && (vrefresh == 29))
    {
        m_edid[DRM_CONNECTOR_EDID_4K_0] |= SINK_2160P_29_97HZ;
        return 0;
    }

    if ((w == 3840) && (h == 2160) && (vrefresh == 30))
    {
        m_edid[DRM_CONNECTOR_EDID_4K_0] |= SINK_2160P_30HZ;
        return 0;
    }

    if ((w == 3840) && (h == 2160) && (vrefresh == 50))
    {
        m_edid[DRM_CONNECTOR_EDID_4K_0] |= SINK_2160P_50HZ;
        return 0;
    }

    if ((w == 3840) && (h == 2160) && (vrefresh == 60))
    {
        m_edid[DRM_CONNECTOR_EDID_4K_0] |= SINK_2160P_60HZ;
        return 0;
    }

    return 0;
}
