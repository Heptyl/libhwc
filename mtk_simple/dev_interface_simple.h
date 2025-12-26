#pragma once
#include <stdint.h>

#include <cutils/native_handle.h>
#include "hwcdisplay_simple.h"

namespace simplehwc {

#define MAX_DISPLAY_NUM 3
#define MAX_BO_SIZE 2

typedef enum {
    HWC_DISP_INVALID_SESSION_MODE = 0,
    HWC_DISP_SESSION_DIRECT_LINK_MODE = 1,
    HWC_DISP_SESSION_DECOUPLE_MODE = 2,
    HWC_DISP_SESSION_DIRECT_LINK_MIRROR_MODE = 3,
    HWC_DISP_SESSION_DECOUPLE_MIRROR_MODE = 4,
    HWC_DISP_SESSION_RDMA_MODE,
    HWC_DISP_SESSION_DUAL_DIRECT_LINK_MODE,
    HWC_DISP_SESSION_DUAL_DECOUPLE_MODE,
    HWC_DISP_SESSION_DUAL_RDMA_MODE,
    HWC_DISP_SESSION_TRIPLE_DIRECT_LINK_MODE,
    HWC_DISP_SESSION_MODE_NUM,
} HWC_DISP_MODE;

enum HWC_DISP_SESSION_TYPE {
    HWC_DISP_SESSION_PRIMARY = 1,
    HWC_DISP_SESSION_EXTERNAL = 2,
    HWC_DISP_SESSION_MEMORY = 3
};

class IOverlayDevice_simple
{
public:
    virtual ~IOverlayDevice_simple() {}

    virtual void postBuffer(uint64_t display, buffer_handle_t buffer, PrivateHnd priv_handle) = 0;

    virtual void getDisplyResolution(uint64_t display, uint32_t* width, uint32_t* height) = 0;

    virtual void getDisplyPhySize(uint64_t display, uint32_t* width, uint32_t* height) = 0;

    virtual void getDisplayRefresh(uint64_t display, uint32_t* refresh) = 0;

    virtual void initDisplay(uint64_t display) = 0;

    virtual void setPowerMode(uint64_t display, int32_t mode) = 0;
    virtual int createOverlaySession(uint64_t display, HWC_DISP_MODE mode = HWC_DISP_SESSION_DIRECT_LINK_MODE) = 0;
    virtual void prepareOverlayPresentFence(uint64_t display, int32_t* fence_fd, uint32_t* fence_index) = 0;
    virtual bool isDispFeatureSupported(unsigned int feature_mask) = 0;
};

IOverlayDevice_simple* getHwDevice_simple();

}  // namespace simplehwc

