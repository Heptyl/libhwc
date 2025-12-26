#ifndef __HWC_PQ_DEV_LEGACY_H__
#define __HWC_PQ_DEV_LEGACY_H__

#include "pq_interface.h"

using namespace android;

class PqDeviceLegacy : public IPqDevice
{
public:
    static PqDeviceLegacy& getInstance();
private:
    PqDeviceLegacy();
    void checkAndOpenIoctl();
    bool setColorTransformViaIoctl(const float* matrix, const int32_t& hint);
};

#endif
