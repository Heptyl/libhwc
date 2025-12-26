#define DEBUG_LOG_TAG "SYNC"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS
//#define LOG_NDEBUG 0

#include <algorithm>
#include <string>

#include <android/sync.h>

#include "utils/debug.h"
#include <utils/tools.h>

#include "sync.h"

#define SYNC_LOGV(x, ...) HWC_LOGV("(%p) " x, this, ##__VA_ARGS__)
#define SYNC_LOGD(x, ...) HWC_LOGD("(%p) " x, this, ##__VA_ARGS__)
#define SYNC_LOGI(x, ...) HWC_LOGI("(%p) " x, this, ##__VA_ARGS__)
#define SYNC_LOGW(x, ...) HWC_LOGW("(%p) " x, this, ##__VA_ARGS__)
#define SYNC_LOGE(x, ...)                                           \
        {                                                           \
            DbgLogger logger(DbgLogger::TYPE_HWC_LOG                \
                            | DbgLogger::TYPE_FENCE,                \
                            'E',                                    \
                            " ! " x, ##__VA_ARGS__);                \
        }

#define SYNC_DBG_FENCE_TYPE_SHIFT 28U
#define SYNC_DBG_ID1_SHIFT 8U
#define SYNC_DBG_ID2_SHIFT 0U

static unsigned int getSyncDbgInt(unsigned int fence_type, uint64_t id_1, uint64_t id_2)
{
    return (fence_type << SYNC_DBG_FENCE_TYPE_SHIFT) |
           (static_cast<unsigned int>(id_1) << SYNC_DBG_ID1_SHIFT) |
           (static_cast<unsigned int>(id_2) << SYNC_DBG_ID2_SHIFT);
}

// ---------------------------------------------------------------------------
SyncFence::SyncFence(uint64_t client)
    : m_client(client)
{
}

SyncFence::~SyncFence()
{
}

status_t SyncFence::wait(int fd, int timeout, const char* log_name,
                         unsigned int fence_type, uint64_t id_1, uint64_t id_2)
{
    HWC_ATRACE_FORMAT_NAME("wait_fence(%d)", fd);

    if (fd == -1) return NO_ERROR;

    int err = sync_wait(fd, timeout);
    if (err < 0 && errno == ETIME)
    {
        HWC_ATRACE_NAME("timeout");

        if (log_name)
        {
            SYNC_LOGE("[%s] (%" PRIu64 ") fence %d didn't signal in %u ms",
                      log_name, m_client, fd, timeout);
        }
        else
        {
            SYNC_LOGE("[0x%x] (%" PRIu64 ") fence %d didn't signal in %u ms",
                      getSyncDbgInt(fence_type, id_1, id_2), m_client, fd, timeout);
        }

        dump(fd);
    }

    protectedClose(fd);

    if (log_name)
    {
        SYNC_LOGV("[%s] (%" PRIu64 ") wait and close fence %d within %d",
                  log_name, m_client, fd, timeout);
    }
    else
    {
        SYNC_LOGV("[0x%x] (%" PRIu64 ") wait and close fence %d within %d",
                  getSyncDbgInt(fence_type, id_1, id_2), m_client, fd, timeout);
    }

    return err < 0 ? -errno : status_t(NO_ERROR);
}

status_t SyncFence::waitForever(int fd, int warning_timeout, const char* log_name)
{
    if (fd == -1) return NO_ERROR;

    int err = sync_wait(fd, warning_timeout);
    if (err < 0 && errno == ETIME)
    {
        dump(fd);
        err = sync_wait(fd, TIMEOUT_NEVER);
    }

    protectedClose(fd);

    SYNC_LOGV("[%s] (%" PRIu64 ") wait and close fence %d", log_name, m_client, fd);

    return err < 0 ? -errno : status_t(NO_ERROR);
}

void SyncFence::dump(int fd)
{
    if (-1 == fd) return;

    // sync point info
    struct sync_file_info *finfo = sync_file_info(fd);
    if (finfo)
    {
        // status: active(0) signaled(1) error(<0)
        SYNC_LOGE("fence(%s) status(%d)\n", finfo->name, finfo->status);
        HWC_ATRACE_FORMAT_NAME("fence(%s)", finfo->name);

        // iterate all sync points
        struct sync_fence_info* pinfo = sync_get_fence_info(finfo);
        for (size_t i = 0; i < finfo->num_fences; i++)
        {
            int ts_sec = static_cast<int>(pinfo[i].timestamp_ns / 1000000000LL);
            int ts_usec = (pinfo[i].timestamp_ns % 1000000000LL) / 1000LL;

            SYNC_LOGE("sync point: timeline(%s) drv(%s) status(%d) timestamp(%d.%06d)",
                    pinfo[i].obj_name,
                    pinfo[i].driver_name,
                    pinfo[i].status,
                    ts_sec, ts_usec);
        }
        sync_file_info_free(finfo);
    }
}

int SyncFence::merge(int fd1, int fd2, const char* name)
{
    int fd3;

    if (fd1 >= 0 && fd2 >= 0)
    {
        fd3 = sync_merge(name, fd1, fd2);
    }
    else if (fd1 >= 0)
    {
        fd3 = sync_merge(name, fd1, fd1);
    }
    else if (fd2 >= 0)
    {
        fd3 = sync_merge(name, fd2, fd2);
    }
    else
    {
        return -1;
    }

    // check status of merged fence
    if (fd3 < 0)
    {
        HWC_LOGE("merge fences[%s](%d, %d) fail: %s (%d)", (name != nullptr) ? name : "no_name",
                fd1, fd2, strerror(errno), -errno);
        return -1;
    }

    HWC_LOGD("merge fences[%s](%d, %d) into fence(%d)", (name != nullptr) ? name : "no_name",
            fd1, fd2, fd3);

    return fd3;
}

int SyncFence::queryFenceStatus(int fd)
{
    struct sync_file_info *finfo = sync_file_info(fd);
    int ret = -1;
    if (finfo)
    {
        // status: active(0) signaled(1) error(<0)
        HWC_LOGV("fence(%s) status(%d)\n", finfo->name, finfo->status);
        ret = finfo->status;
        sync_file_info_free(finfo);
    }
    return ret;
}

uint64_t SyncFence::getSignalTime(int fd)
{
    if (fd == -1) {
        return static_cast<uint64_t>(SIGNAL_TIME_INVALID);
    }

    struct sync_file_info* finfo = sync_file_info(fd);
    if (finfo == nullptr) {
        HWC_LOGE("sync_file_info returned NULL for fd %d", fd);
        return static_cast<uint64_t>(SIGNAL_TIME_INVALID);
    }
    if (finfo->status != 1) {
        sync_file_info_free(finfo);
        return static_cast<uint64_t>(SIGNAL_TIME_PENDING);
    }

    uint64_t timestamp = 0;
    struct sync_fence_info* pinfo = sync_get_fence_info(finfo);
    for (size_t i = 0; i < finfo->num_fences; i++) {
        if (pinfo[i].timestamp_ns > timestamp) {
            timestamp = pinfo[i].timestamp_ns;
        }
    }

    sync_file_info_free(finfo);
    return timestamp;
}

status_t SyncFence::waitWithoutCloseFd(int fd, int timeout, const char* log_name)
{
    HWC_ATRACE_FORMAT_NAME("wait_fence(%d)", fd);

    if (fd == -1) return NO_ERROR;

    int err = sync_wait(fd, timeout);
    if (err < 0 && errno == ETIME)
    {
        HWC_ATRACE_NAME("timeout");

        SYNC_LOGE("[%s] fence %d didn't signal in %u ms",
            log_name, fd, timeout);
        dump(fd);
    }

    return err < 0 ? -errno : status_t(NO_ERROR);
}

status_t SyncFence::waitPeriodicallyWoCloseFd(int fd, int period_timeout, int timeout, const char* log_name)
{
    std::string atrace_tag("wait_fence(");
    atrace_tag += std::to_string(fd) + ")";
    HWC_ATRACE_NAME(atrace_tag.c_str());

    if (fd == -1) return NO_ERROR;

    const nsecs_t tic = systemTime();
    period_timeout = std::min(period_timeout, timeout);

    int err = 0;
    do
    {
        int cur_timeout = period_timeout;
        nsecs_t timeout_left = (tic + ms2ns(timeout)) - systemTime();
        cur_timeout = std::min(cur_timeout, static_cast<int>(ns2ms(timeout_left)));

        err = sync_wait(fd, cur_timeout);
        if (err != 0 && errno == ETIME)
        {
            HWC_ATRACE_NAME("period_timeout");

            SYNC_LOGE("[%s] fence %d didn't signal in %" PRId64 " ms, period_timeout %d",
                log_name, fd, ns2ms(systemTime() - tic), period_timeout);
            dump(fd);
        }
    }
    while ((err != 0 && errno == ETIME) && (systemTime() - tic < ms2ns(timeout)));

    if (err < 0 && errno == ETIME)
    {
        HWC_ATRACE_NAME("timeout");

        SYNC_LOGE("[%s] fence %d didn't signal in %u ms",
            log_name, fd, timeout);
        dump(fd);
    }

    return err < 0 ? -errno : status_t(NO_ERROR);
}
