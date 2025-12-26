#pragma once
#include <stdint.h>
#include <string>
#include <vector>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#include <xf86drmMode.h>
#pragma clang diagnostic pop

namespace simplehwc {

class DrmModePropertyEnum
{
public:
    DrmModePropertyEnum(drm_mode_property_enum *e);
    ~DrmModePropertyEnum();

public:
    std::string m_name;
    uint64_t m_value;
};

class DrmModeProperty
{
public:
    DrmModeProperty();
    DrmModeProperty(drmModePropertyPtr p, uint64_t value);
    ~DrmModeProperty() {}

    void init(drmModePropertyPtr p, uint64_t value);
    bool hasInit();

    int getValue(uint64_t *value) const;
    uint32_t getId() const;
    std::string getName() const;

private:
    uint32_t m_id;
    uint32_t m_type;
    bool m_is_immutable;
    bool m_is_atomic;
    bool m_init;

    std::string m_name;
    uint32_t m_flags;
    uint64_t m_value;

    std::vector<uint64_t> m_values;
    std::vector<DrmModePropertyEnum> m_enums;
    std::vector<uint32_t> m_blob_ids;
};

}  // namespace simplehwc

