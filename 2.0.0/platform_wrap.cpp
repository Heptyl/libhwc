#include "platform_wrap.h"

#undef DEBUG_LOG_TAG
#define DEBUG_LOG_TAG "Platform"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include <utility>

#include <cutils/properties.h>

#include "dev_interface.h"
#ifdef MTK_GENERIC_HAL
#include "ld20/platform6885.h"
#include "ld20/platform6983.h"
#include "ld20/platform6879.h"
#include "ld20/platform6895.h"
#include "ld20/platform6855.h"
#include "ld20/platform6985.h"
#include "ld20/platform6886.h"
#else
#include "platform.h"
#endif

#define PLATFORM_INFO(id) Platform##id

#define CREATE_PLATFORM(id) new PLATFORM_INFO(id)()

PlatformCommon& Platform::getInstance()
{
    static PlatformCommon *gInstancePtr = createPlatformCommon();
    if (gInstancePtr == nullptr)
    {
        HWC_LOGI("failed to create the PlatformCommon");
    }
    return *gInstancePtr;
}

PlatformCommon* Platform::createPlatformCommon()
{
    PlatformCommon* ptr = nullptr;
#ifdef MTK_GENERIC_HAL
    unsigned int version = IOverlayDevice::getHwVersion();
    switch(version) {
        case PLATFORM_MT6885:
            ptr = new Platform_MT6885();
            break;
        case PLATFORM_MT6983:
            ptr = new Platform_MT6983();
            break;
        case PLATFORM_MT6879:
            ptr = new Platform_MT6879();
            break;
        case PLATFORM_MT6895:
            ptr = new Platform_MT6895();
            break;
        case PLATFORM_MT6855:
            ptr = new Platform_MT6855();
            break;
        case PLATFORM_MT6985:
            ptr = new Platform_MT6985();
            break;
        case PLATFORM_MT6886:
            ptr = new Platform_MT6886();
            break;
        default:
            ptr = nullptr;
            break;
    }
#else
    ptr = CREATE_PLATFORM(HWC_PLATFORM);
#endif
    if (ptr)
    {
        ptr->updateConfigFromProperty();
    }
    return ptr;
}
