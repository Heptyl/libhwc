#define DEBUG_LOG_TAG "DRMDEV"
#include "drmobject.h"

#include <cutils/log.h>
#include <errno.h>

#include "utils/debug.h"

DrmObject::DrmObject(uint32_t type, uint32_t id)
    : m_obj_type(type)
    , m_id(id)
    , m_prop_size(0)
    , m_prop_list(NULL)
    , m_property(NULL)
{
}

const DrmModeProperty& DrmObject::getProperty(int prop) const
{
    return m_property[prop];
}

int DrmObject::addProperty(drmModeAtomicReqPtr req, int prop, uint64_t value) const
{
    if (m_property[prop].hasInit())
    {
        return drmModeAtomicAddProperty(req, m_id, m_property[prop].getId(), value);
    }
    else
    {
        std::pair<int, std::string> item = m_prop_list[prop];
        HWC_LOGW("0x%x[%d] property[%s] does not do initialize, so ignore adding property",
                m_obj_type, m_id, item.second.c_str());
    }
    return 0;
}

uint32_t DrmObject::getId() const
{
    return m_id;
}

int DrmObject::checkProperty()
{
    int res = 0;

    for (size_t i = 0; i < m_prop_size; i++)
    {
        if (!m_property[i].hasInit())
        {
            HWC_LOGW("0x%x[%d] property[%s] does not do initialize", m_obj_type, m_id, m_prop_list[i].second.c_str());
            res = -EINVAL;
        }
    }

    return res;
}

int DrmObject::initProperty(int fd)
{
    drmModeObjectPropertiesPtr props;
    props = drmModeObjectGetProperties(fd, m_id, m_obj_type);
    if (props == nullptr)
    {
        HWC_LOGW("failed to get 0x%x[%d] properties", m_obj_type, m_id);
        return -ENODEV;
    }

    int res = 0;
    for (uint32_t i = 0; i < props->count_props; i++)
    {
        drmModePropertyPtr p = drmModeGetProperty(fd, props->props[i]);
        if (!p)
        {
            HWC_LOGW("failed to get 0x%x[%d] property[%x]", m_obj_type, m_id, props->props[i]);
            res = -ENODEV;
        }
        else
        {
            for (size_t j = 0; j < m_prop_size; j++)
            {
                std::pair<int, std::string> item = m_prop_list[j];
                if (!strcmp(item.second.c_str(), p->name))
                {
                    m_property[item.first].init(p, props->prop_values[i]);
                }
            }
            drmModeFreeProperty(p);
        }
    }
    drmModeFreeObjectProperties(props);

    return res;
}

