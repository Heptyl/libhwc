#ifndef __MTK_HWC_DRM_MODE_INFO_H__
#define __MTK_HWC_DRM_MODE_INFO_H__

#include <stdint.h>
#include <string>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#include <xf86drmMode.h>
#pragma clang diagnostic pop

class DrmModeInfo {
public:
    DrmModeInfo() = default;
    DrmModeInfo(drmModeModeInfoPtr mode);
    ~DrmModeInfo() {}

    bool operator==(const drmModeModeInfo &mode) const;
    DrmModeInfo& operator=(const DrmModeInfo &mode);

    uint32_t getDisplayH() const;
    uint32_t getDisplayV() const;
    uint32_t getVRefresh() const;
    uint32_t getType() const;
    void getModeInfo(drmModeModeInfo* mode);

private:
    drmModeModeInfo m_info;
};

#endif
