#define DEBUG_LOG_TAG "drmmodeproperty"

#include "drmmodeproperty.h"
#include <cutils/log.h>

namespace simplehwc {

DrmModePropertyEnum::DrmModePropertyEnum(drm_mode_property_enum *e)
    : m_name(e->name)
    , m_value(e->value)
{
}

DrmModePropertyEnum::~DrmModePropertyEnum()
{
}

DrmModeProperty::DrmModeProperty()
    : m_id(0)
    , m_type(0)
    , m_is_immutable(false)
    , m_is_atomic(false)
    , m_init(false)
    , m_name("")
    , m_flags(0)
    , m_value(0)
{
}

DrmModeProperty::DrmModeProperty(drmModePropertyPtr p, uint64_t value)
    : m_id(p->prop_id)
    , m_type(0)
    , m_is_immutable(false)
    , m_is_atomic(false)
    , m_init(false)
    , m_name(p->name)
    , m_flags(p->flags)
    , m_value(value)
{
    init(p, value);
}

void DrmModeProperty::init(drmModePropertyPtr p, uint64_t value)
{
    m_init = true;
    m_id = p->prop_id;
    m_name = p->name;
    m_flags = p->flags;
    m_value = value;

    if (p->flags & DRM_MODE_PROP_RANGE)
    {
        m_type = DRM_MODE_PROP_RANGE;
    }
    else if (p->flags & DRM_MODE_PROP_ENUM)
    {
        m_type = DRM_MODE_PROP_ENUM;
    }
    else if (p->flags & DRM_MODE_PROP_BLOB)
    {
        m_type = DRM_MODE_PROP_BLOB;
    }
    else if (p->flags & DRM_MODE_PROP_BITMASK)
    {
        m_type = DRM_MODE_PROP_BITMASK;
    }
    else if (p->flags & DRM_MODE_PROP_OBJECT)
    {
        m_type = DRM_MODE_PROP_OBJECT;
    }
    else if (p->flags & DRM_MODE_PROP_SIGNED_RANGE)
    {
        m_type = DRM_MODE_PROP_SIGNED_RANGE;
    }

    if (p->flags & DRM_MODE_PROP_IMMUTABLE)
    {
        m_is_immutable = true;
    }
    if (p->flags & DRM_MODE_PROP_ATOMIC)
    {
        m_is_atomic = true;
    }

    for (int i = 0; i < p->count_values; i++)
    {
        m_values.push_back(p->values[i]);
    }

    for (int i = 0; i < p->count_enums; i++)
    {
        DrmModePropertyEnum prop_enum(&p->enums[i]);
        m_enums.push_back(prop_enum);
    }

    for (int i = 0; i < p->count_blobs; i++)
    {
        m_blob_ids.push_back(p->blob_ids[i]);
    }
}

bool DrmModeProperty::hasInit()
{
    return m_init;
}

int DrmModeProperty::getValue(uint64_t *value) const
{
    *value = m_value;
    return 0;
}

uint32_t DrmModeProperty::getId() const
{
    return m_id;
}

std::string DrmModeProperty::getName() const
{
    return m_name;
}

}  // namespace simplehwc

