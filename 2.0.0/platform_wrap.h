#ifndef HWC_PLATFORM_WRAP_H_
#define HWC_PLATFORM_WRAP_H_

#include <mutex>

#include "platform_common.h"

class Platform
{
public:
    static PlatformCommon& getInstance();
    Platform() = default;
    ~Platform() {}

private:
    static PlatformCommon* createPlatformCommon();
};

#endif // HWC_PLATFORM_WRAP_H_
