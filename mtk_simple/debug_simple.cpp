#define DEBUG_LOG_TAG "debug_simple"
#include <fcntl.h>
#include <stdio.h>
#include <cutils/properties.h>
#include "debug_simple.h"
#include "dev_interface_simple.h"
#include <linux/mediatek_drm.h>

namespace simplehwc {

static int mLogLevel = LOG_LEVEL_INFO;
static int mdump_buf_cont = 0;
//static int mForceMemcpy = 0;
//static int iommu_support = 0;
//static int presentfence_support = 1;
//static int gralloc_extra_support = 1;

#if 0
void setForceMemcpy(int enable)
{
    mForceMemcpy = enable;
    ALOGD("setForceMemcpy-mForceMemcpy:%d", mForceMemcpy);
}
#endif

int isForceMemcpy(void)
{
    static int mForceMemcpy = 1;
    static bool needReadOnce = true;
    if (UNLIKELY(needReadOnce)) {
        char value[PROPERTY_VALUE_MAX] = {0};
        property_get("vendor.debug.hwc.force_memcpy", value, "-1");
        HWC_LOGI("getprop [vendor.debug.hwc.force_memcpy] %s", value);
        if (-1 != atoi(value))
        {
            mForceMemcpy = atoi(value);
            HWC_LOGI("setForceMemcpy: %d", atoi(value));
        }
        needReadOnce = false;
    }

    //return mForceMemcpy || !gralloc_extra_support || !iommu_support || !presentfence_support;
    return !getHwDevice_simple()->isDispFeatureSupported(DRM_DISP_FEATURE_IOMMU) || mForceMemcpy;
}

int isSelfRefresh(void)
{
    static int self_refresh = 0;
    static bool need_read_once = true;
    if (need_read_once)
    {
        char value[PROPERTY_VALUE_MAX] = {0};
        property_get("vendor.debug.hwc.self_refresh", value, "-1");
        HWC_LOGI("getprop [vendor.debug.hwc.self_refresh] %s", value);
        if (-1 != atoi(value))
        {
            self_refresh = atoi(value);
            HWC_LOGI("set self-refresh: %d", atoi(value));
            if (self_refresh > 0 &&
                isForceMemcpy() <= 0)
            {
                HWC_LOGW("self-refresh only works when isForceMemcpy");
            }
        }
        need_read_once = false;
    }

    return (isForceMemcpy() > 0 && self_refresh > 0) ? 1 : 0;
}

int getLogLevel(void)
{
    //ALOGD("getLogLevel-mLogLevel: %d", mLogLevel);
    return mLogLevel;
}

void setLogLevel(int level)
{
    mLogLevel = level;
    HWC_LOGI("setLogLevel-level: %d, mLogLevel:%d", level, mLogLevel);
}
void setDumpBuf(int buf_cont)
{
    mdump_buf_cont = buf_cont;
    HWC_LOGI("setDumpBuf-buf_cont: %d, mdump_buf_cont:%d", buf_cont, mdump_buf_cont);
}
int isEnableDumpBuf(void)
{
    return mdump_buf_cont;
}

}  // namespace simplehwc

