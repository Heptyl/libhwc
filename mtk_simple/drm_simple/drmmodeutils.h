#pragma once
#include <stdint.h>

#include <hardware/hwcomposer2.h>

namespace simplehwc {

struct hwc_drm_bo {
    uint32_t width;
    uint32_t height;
    uint32_t format; /* DRM_FORMAT_* from drm_fourcc.h */
    uint32_t pitches[4];
    uint32_t offsets[4];
    uint32_t gem_handles[4];
    uint64_t modifier[4];
    uint32_t fb_id;
    int acquire_fence_fd;
    void *priv;
    int fd;
    size_t size_page;
};

uint32_t getDrmBitsPerPixel(uint32_t format);

uint32_t mapDispColorFormat(unsigned int format);

uint32_t mapDispInputColorFormat(unsigned int format);

uint32_t getPlaneNumberOfDispColorFormat(uint32_t format);

uint32_t getHorizontalSubSampleOfDispColorFormat(uint32_t format);

uint32_t getVerticalSubSampleOfDispColorFormat(uint32_t format);

}  // namespace simplehwc

