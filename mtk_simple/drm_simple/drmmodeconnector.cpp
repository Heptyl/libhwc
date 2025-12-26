#define DEBUG_LOG_TAG "drmmodeconnector"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include "drmmodeconnector.h"

#include <cutils/log.h>
#include <errno.h>
#include <sstream>

#include "drmmodeencoder.h"

namespace simplehwc {

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
        ALOGE("failed to init connector[%d] property: errno[%d]", m_id, res);
        return res;
    }

    res = checkProperty();
    if (res)
    {
        ALOGE("failed to check connector[%d] property: error=%d", m_id, res);
        return res;
    }

    return res;
}

uint32_t DrmModeConnector::getId()
{
    return m_id;
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
    if (use_preferred && !m_modes.empty())
    {
        size_t current_index = 0;
        uint32_t current_res_product = 0;
        uint32_t current_refresh = 0;
        bool current_is_preferred = false;
        // if there are preferred modes, try to find the maximum resolution from them.
        // otherwise find the maximum resolution from all modes.
        for (size_t i = 0; i <  m_modes.size(); i++)
        {
            DrmModeInfo& drmMode = m_modes[i];
            uint32_t product = drmMode.getDisplayH() * drmMode.getDisplayV();
            uint32_t refresh = drmMode.getVRefresh();
            bool is_preferred = drmMode.getType() & DRM_MODE_TYPE_PREFERRED;
            // the resolution of new mode is larger than current
            if ((product > current_res_product) ||
                    ((product == current_res_product) && (refresh > current_refresh)))
            {
                // if new mode is preferred, we choose it directly.
                // if current mode is not preferred, we also choose it directly.
                if ((is_preferred == true) || (current_is_preferred == false))
                {
                    current_index = i;
                    current_res_product = product;
                    current_refresh = refresh;
                    // first time to find a preferred mode
                    if (current_is_preferred != is_preferred)
                    {
                        current_is_preferred = true;
                    }
                }
            }
        }
        *mode = m_modes[current_index];
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
        ALOGW("try to connect connector_%d with nullptr encoder", m_id);
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

    ALOGI("DrmModeConnector: id=%d encoder_id=%d type=%d type_id=%d mm_size=%dx%d count=%d possible_encoder_id=%s mode_count=%zu",
            m_id, m_encoder_id, m_connector_type, m_connector_type_id,
            m_mm_width, m_mm_height, m_count_encoders, ss.str().c_str(), m_modes.size());

    for (size_t i = 0 ; i < m_modes.size(); i++)
    {
        DrmModeInfo mode = m_modes[i];
        drmModeModeInfo modeInfo;
        mode.getModeInfo(&modeInfo);
        ALOGI("DrmModeConnector mode index:%zu clock:%d w:%d h:%d vrefresh:%d type:0x%x",
        i, modeInfo.clock, modeInfo.hdisplay, modeInfo.vdisplay, modeInfo.vrefresh, modeInfo.type);
    }
}

uint32_t DrmModeConnector::getConnectorType()
{
    return m_connector_type;
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

}  // namespace simplehwc

