#define DEBUG_LOG_TAG "dev_interface_simple"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS
#include <cutils/log.h>
#include <utils/Errors.h>
#include <inttypes.h>

#include "dev_interface_simple.h"

#include "drm_simple/drmdev.h"

namespace simplehwc {

//using ::vendor::mediatek::hardware::simplehwc::DrmDevice;
//using simplehwc::DrmDevice;
//using namespace simplehwc;

//namespace NS_SIMPLE = simplehwc;

IOverlayDevice_simple* getHwDevice_simple()
{
    //ALOGI("simple_hwc getHwDevice_simple()");
    return &DrmDevice::getInstance();
}

}  // namespace simplehwc

