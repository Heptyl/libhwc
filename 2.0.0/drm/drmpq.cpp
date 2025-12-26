#define DEBUG_LOG_TAG "PqDeviceDrm"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include "drmpq.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <fcntl.h>
#include <sys/ioctl.h>

#include <ddp_pq.h>
#include <ddp_drv.h>

#include "platform_wrap.h"
#include "utils/debug.h"

#define MTK_PQ_DEVICE_DRM "/dev/dri/card0"

#ifdef USE_SWWATCHDOG
#include "utils/swwatchdog.h"
#define WDT_IOCTL(fd, CMD, ...)                                                             \
({                                                                                          \
    int err = 0;                                                                            \
    if (Platform::getInstance().m_config.wdt_trace)                                         \
    {                                                                                       \
        ATRACE_NAME(#CMD);                                                                  \
        SWWatchDog::AutoWDT _wdt("[PqDev] ioctl(" #CMD "):" STRINGIZE(__LINE__), 500);      \
        err = ioctl(fd, CMD, ##__VA_ARGS__);                                                \
    }                                                                                       \
    else                                                                                    \
    {                                                                                       \
        SWWatchDog::AutoWDT _wdt("[PqDev] ioctl(" #CMD "):" STRINGIZE(__LINE__), 500);      \
        err = ioctl(fd, CMD, ##__VA_ARGS__);                                                \
    }                                                                                       \
    err;                                                                                    \
})
#else
#define WDT_IOCTL(fd, CMD, ...)                                                             \
({                                                                                          \
    int err = 0;                                                                            \
    if (Platform::getInstance().m_config.wdt_trace)                                         \
    {                                                                                       \
        ATRACE_NAME(#CMD);                                                                  \
        err = ioctl(fd, CMD, ##__VA_ARGS__);                                                \
    }                                                                                       \
    else                                                                                    \
    {                                                                                       \
        err = ioctl(fd, CMD, ##__VA_ARGS__);                                                \
    }                                                                                       \
    err;                                                                                    \
})
#endif

#ifndef DISP_IOCTL_SUPPORT_COLOR_TRANSFORM
#define DISP_IOCTL_SUPPORT_COLOR_TRANSFORM 0x0
struct DISP_COLOR_TRANSFORM {
    int matrix[4][4];
};
#endif

PqDeviceDrm& PqDeviceDrm::getInstance()
{
    static PqDeviceDrm gInstance;
    return gInstance;
}

PqDeviceDrm::PqDeviceDrm()
    : m_ccorr_identity_val(0)
{
    checkAndOpenIoctl();
    initPqCapability();
}

void PqDeviceDrm::openPqDevice()
{
    m_pq_fd = open(MTK_PQ_DEVICE_DRM, O_WRONLY);
    if (m_pq_fd == -1)
    {
        HWC_LOGW("failed to open pq device node(%d): %s", errno, strerror(errno));
    }
}

void PqDeviceDrm::checkAndOpenIoctl()
{
    if (DISP_IOCTL_SUPPORT_COLOR_TRANSFORM != 0)
    {
        m_use_ioctl = true;
    }
    openPqDevice();
}

bool PqDeviceDrm::setColorTransformViaIoctl(const float* matrix, const int32_t& /*hint*/)
{
    bool res = false;
    int32_t ret = 0;

    if (m_pq_fd != -1)
    {
        struct DISP_COLOR_TRANSFORM transform;
        const unsigned int dimension = 4;
        static int identity_value = getCcorrIdentityValue();
        for (unsigned int i = 0; i < dimension; ++i)
        {
            DbgLogger logger(DbgLogger::TYPE_HWC_LOG, 'D', "matrix ");
            for (unsigned int j = 0; j < dimension; ++j)
            {
                transform.matrix[i][j] = static_cast<int>(matrix[i * dimension + j] *
                        identity_value + 0.5f);
                logger.printf("%d,", transform.matrix[i][j]);
            }
        }
        ret = WDT_IOCTL(m_pq_fd, DRM_IOCTL_MTK_SUPPORT_COLOR_TRANSFORM, &transform);
        if (ret == 0)
        {
            res = true;
        }
    }
    else
    {
        HWC_LOGW("failed to set color transform, because pq fd is invalid");
    }

    return res;
}


int PqDeviceDrm::getCcorrIdentityValue()
{
    return m_ccorr_identity_val;
}

int PqDeviceDrm::calculateCcorrIdentityValue(unsigned int bit)
{
    if (bit < 12)
    {
        HWC_LOGE("calculate identity value with a invalid bit number[%u], use defaulr value", bit);
        return DEFAULT_IDENTITY_VALUE;
    }
    int identity = static_cast<int>(std::pow(2.0f, bit - 2));
    return identity;
}

void PqDeviceDrm::initPqCapability()
{
    if (m_pq_fd != -1)
    {
        mtk_drm_pq_caps_info pq_caps;
        memset(&pq_caps, 0, sizeof(pq_caps));
        int res = WDT_IOCTL(m_pq_fd, DRM_IOCTL_MTK_GET_PQ_CAPS, &pq_caps);
        if (res < 0)
        {
            HWC_LOGW("failed to get pq capability: %d", res);
        }
        HWC_LOGI("ccorr caps: bit[%u]", pq_caps.ccorr_caps.ccorr_bit);
        m_ccorr_identity_val = calculateCcorrIdentityValue(pq_caps.ccorr_caps.ccorr_bit);
    }
    else
    {
        HWC_LOGW("%s: failed to get pq capability, because pq fd is invalid", __func__);
    }
}
