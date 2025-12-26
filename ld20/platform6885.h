#ifndef HWC_PLATFORM_MT6885_H_
#define HWC_PLATFORM_MT6885_H_

#include "platform_common.h"

// Device-dependent code should be placed in the Platform. If adding a function into
// Platform, we should also add a condidate into PlatformCommon to avoid build error.
class Platform_MT6885 : public PlatformCommon
{
public:
    Platform_MT6885();
    ~Platform_MT6885() {}

    size_t getLimitedExternalDisplaySize();

    // isUILayerValid() is ued to verify
    // if ui layer could be handled by hwcomposer
    bool isUILayerValid(const sp<HWCLayer>& layer, int32_t* line);

    // isMMLayerValid() is used to verify
    // if mm layer could be handled by hwcomposer
    bool isMMLayerValid(const sp<HWCLayer>& layer, const int32_t pq_mode_id, int32_t* line);
};

#endif // HWC_PLATFORM_MT6885_H_
