#define DEBUG_LOG_TAG "PqDeviceLegacy"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include "pqdev_legacy.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <fcntl.h>
#include <sys/ioctl.h>

#include <ddp_pq.h>
#include <ddp_drv.h>

#include "platform_wrap.h"
#include "utils/debug.h"

#define MTK_PQ_DEVICE "/dev/mtk_disp_mgr"

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

PqDeviceLegacy& PqDeviceLegacy::getInstance()
{
    static PqDeviceLegacy gInstance;
    return gInstance;
}

PqDeviceLegacy::PqDeviceLegacy()
{
    checkAndOpenIoctl();
}

void PqDeviceLegacy::checkAndOpenIoctl()
{
    if (DISP_IOCTL_SUPPORT_COLOR_TRANSFORM != 0)
    {
        m_use_ioctl = true;
        m_pq_fd = open(MTK_PQ_DEVICE, O_WRONLY);
        if (m_pq_fd == -1)
        {
            HWC_LOGW("failed to open pq device node(%d): %s", errno, strerror(errno));
        }
    }
}

bool PqDeviceLegacy::setColorTransformViaIoctl(const float* matrix, const int32_t& /*hint*/)
{
    bool res = false;
    int32_t ret = 0;

    if (m_pq_fd != -1)
    {
        struct DISP_COLOR_TRANSFORM transform;
        const int32_t dimension = 4;
        for (int32_t i = 0; i < dimension; ++i)
        {
            DbgLogger logger(DbgLogger::TYPE_HWC_LOG, 'D', "matrix ");
            for (int32_t j = 0; j < dimension; ++j)
            {
                transform.matrix[i][j] = static_cast<int>(matrix[i * dimension + j] * 1024.f + 0.5f);
                logger.printf("%d,", transform.matrix[i][j]);
            }
        }
        ret = WDT_IOCTL(m_pq_fd, DISP_IOCTL_SUPPORT_COLOR_TRANSFORM, &transform);
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

