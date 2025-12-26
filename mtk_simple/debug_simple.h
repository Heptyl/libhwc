#pragma once
#include <cutils/log.h>

#define ATRACE_TAG ATRACE_TAG_GRAPHICS
#include <utils/Trace.h>

namespace simplehwc {

#ifndef DEBUG_LOG_TAG
#error "DEBUG_LOG_TAG is not defined!!"
#endif

#pragma GCC diagnostic error "-Wformat"
#pragma GCC diagnostic error "-Wformat-extra-args"

enum {
    LOG_LEVEL_NONE = 0,
    LOG_LEVEL_ERROR = 1,
    LOG_LEVEL_WARN = 2,
    LOG_LEVEL_INFO = 3,
    LOG_LEVEL_DEBUG = 4,
    LOG_LEVEL_VERBOSE = 5
};

//void setForceMemcpy(int enable);
int isForceMemcpy(void);
int isSelfRefresh(void);
int getLogLevel(void);
void setLogLevel(int level);
void setDumpBuf(int buf_cont);
int isEnableDumpBuf(void);

#define HWC_LOGV(fmt, arg...) ALOGV_IF(getLogLevel() >= LOG_LEVEL_VERBOSE, "[%s] <%s()#%d> " fmt "\n", DEBUG_LOG_TAG, __FUNCTION__, __LINE__, ##arg)

#define HWC_LOGD(x, ...)      ALOGD_IF(getLogLevel() >= LOG_LEVEL_DEBUG, "[%s] <%s()#%d> " x "\n", DEBUG_LOG_TAG, __FUNCTION__, __LINE__, ##__VA_ARGS__)

#define HWC_LOGI(x, ...)      ALOGI_IF(getLogLevel() >= LOG_LEVEL_INFO, "[%s] <%s()#%d> " x "\n", DEBUG_LOG_TAG, __FUNCTION__, __LINE__, ##__VA_ARGS__)

#define HWC_LOGW(x, ...)      ALOGW("[%s] <%s()#%d> " x "\n", DEBUG_LOG_TAG, __FUNCTION__, __LINE__, ##__VA_ARGS__)

#define HWC_LOGE(x, ...)      ALOGE("[%s] <%s()#%d> " x "\n", DEBUG_LOG_TAG, __FUNCTION__, __LINE__, ##__VA_ARGS__)

#define LIKELY( exp )       (__builtin_expect( (exp) != 0, true  ))
#define UNLIKELY( exp )     (__builtin_expect( (exp) != 0, false ))

#ifdef USE_SYSTRACE
#define HWC_ATRACE_CALL() android::ScopedTrace ___tracer(ATRACE_TAG, __FUNCTION__)
#define HWC_ATRACE_NAME(name) android::ScopedTrace ___tracer(ATRACE_TAG, name)
#define HWC_ATRACE_INT(name, value) atrace_int(ATRACE_TAG, name, value)
#define HWC_ATRACE_ASYNC_BEGIN(name, cookie) atrace_async_begin(ATRACE_TAG, name, static_cast<int32_t>(cookie))
#define HWC_ATRACE_ASYNC_END(name, cookie) atrace_async_end(ATRACE_TAG, name, static_cast<int32_t>(cookie))

#else // USE_SYSTRACE
#define HWC_ATRACE_CALL()
#define HWC_ATRACE_NAME(name)
#define HWC_ATRACE_INT(name, value)
#define HWC_ATRACE_ASYNC_BEGIN(name, cookie)
#define HWC_ATRACE_ASYNC_END(name, cookie)
#define HWC_ATRACE_FORMAT_NAME(name, ...)
#endif // USE_SYSTRACE

}  // namespace simplehwc

