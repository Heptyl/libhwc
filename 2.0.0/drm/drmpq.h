#ifndef __HWC_PQ_DEV_DRM_H__
#define __HWC_PQ_DEV_DRM_H__

#include "pq_interface.h"
#include <linux/mediatek_drm.h>

using namespace android;

class PqDeviceDrm : public IPqDevice
{
public:
    static PqDeviceDrm& getInstance();

public:
    int getCcorrIdentityValue();

private:
    PqDeviceDrm();
    ~PqDeviceDrm() {}
    void openPqDevice();
    void checkAndOpenIoctl();
    bool setColorTransformViaIoctl(const float* matrix, const int32_t& hint);
    void initPqCapability();
    int calculateCcorrIdentityValue(unsigned int bit);

private:
    int m_ccorr_identity_val;
};

#endif
