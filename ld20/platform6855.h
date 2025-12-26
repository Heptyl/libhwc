#ifndef HWC_PLATFORM_MT6855_H_
#define HWC_PLATFORM_MT6855_H_

#include "platform_common.h"

// Device-dependent code should be placed in the Platform. If adding a function into
// Platform, we should also add a condidate into PlatformCommon to avoid build error.
class Platform_MT6855 : public PlatformCommon
{
public:
    Platform_MT6855();
    ~Platform_MT6855() {}

    size_t getLimitedExternalDisplaySize();

    // isUILayerValid() is ued to verify
    // if ui layer could be handled by hwcomposer
    bool isUILayerValid(const sp<HWCLayer>& layer, int32_t* line);

    // isMMLayerValid() is used to verify
    // if mm layer could be handled by hwcomposer
    bool isMMLayerValid(const sp<HWCLayer>& layer, const int32_t pq_mode_id, int32_t* line);
};

#endif // HWC_PLATFORM_MT6855_H_
