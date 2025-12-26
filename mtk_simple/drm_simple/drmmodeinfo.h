#pragma once
#include <stdint.h>
#include <string>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#include <xf86drmMode.h>
#pragma clang diagnostic pop

namespace simplehwc {

class DrmModeInfo {
public:
    DrmModeInfo() = default;
    DrmModeInfo(drmModeModeInfoPtr mode);
    ~DrmModeInfo() {}

    bool operator==(const drmModeModeInfo &mode) const;
    DrmModeInfo& operator=(const DrmModeInfo &mode);

    uint32_t getDisplayH();
    uint32_t getDisplayV();
    uint32_t getVRefresh();
    uint32_t getType();
    void getModeInfo(drmModeModeInfo* mode);

private:
    drmModeModeInfo m_info;
};

}  // namespace simplehwc

