#ifndef HWC_PLATFORM_H_
#define HWC_PLATFORM_H_

#include "platform_common.h"

// Device-dependent code should be placed in the Platform. If adding a function into
// Platform, we should also add a condidate into PlatformCommon to avoid build error.

#define HWC_PLATFORM _MT8195

class Platform_MT8195 : public PlatformCommon
{
public:
    Platform_MT8195();
    ~Platform_MT8195() { };

    size_t getLimitedExternalDisplaySize();

    // isUILayerValid() is ued to verify
    // if ui layer could be handled by hwcomposer
    bool isUILayerValid(const sp<HWCLayer>& layer, int32_t* line);

    // isMMLayerValid() is used to verify
    // if mm layer could be handled by hwcomposer
    bool isMMLayerValid(const sp<HWCLayer>& layer, const int32_t pq_mode_id, int32_t* line);
    
};

#endif // HWC_PLATFORM_H_
