#ifndef HWC_SYNC_H_
#define HWC_SYNC_H_

#include <utils/threads.h>
#include <utils/Timers.h>

using namespace android;

// ---------------------------------------------------------------------------
#define SYNC_DBG_FENCE_TYPE_MAX 0xf
#define SYNC_DBG_ID1_MAX 0xff
#define SYNC_DBG_ID2_MAX 0xff

enum
{
    SYNC_FENCE_UNKNOWN = 0,
    SYNC_FENCE_PF,
    SYNC_FENCE_OVL_IN,
    SYNC_FENCE_OVL_OUT,
    SYNC_FENCE_PQ,
};

class SyncFence : public LightRefBase<SyncFence>
{
public:
    // TIMEOUT_NEVER may be passed to wait() to indicate that it
    // should wait indefinitely for the fence to signal.
    enum { TIMEOUT_NEVER = -1 };

    // SyncFence constructs a new SynceFence object.
    SyncFence(uint64_t client);

    ~SyncFence();

    // wait() waits for fd to be signaled or have an error
    // waits indefinitely if warning_timeout < 0
    // return NO_ERROR on success, otherwise, -errno is returned.
    //
    // the log_name argument should be a string identifying the caller
    // and will be included in the log message.
    //
    // <fd> will be closed implicitly before exiting wait()
    status_t wait(int fd, int timeout, const char* log_name = "",
                  unsigned int fence_type = SYNC_FENCE_UNKNOWN,
                  uint64_t id_1 = SYNC_DBG_ID1_MAX,
                  uint64_t id_2 = SYNC_DBG_ID2_MAX);

    // waitForever() is a convenience function for waiting forever for a fence
    // to signal (just like wait(TIMEOUT_NEVER)), but issuing an error to the
    // system log and fence state to the kernel log if the wait lasts longer
    // than warning_timeout.
    //
    // the log_name argument should be a string identifying the caller
    // and will be included in the log message.
    //
    // <fd> will be closed implicitly before exiting waitForever()
    status_t waitForever(int fd, int warning_timeout, const char* log_name = "");

    // merge() merges two sync fences into a new one sync fence
    // return a valid file descriptor on success; otherwise, -1 is returned
    static int merge(int fd1, int fd2, const char* name = "merged_fence");

    // query fence state
    static int queryFenceStatus(int fd);

    // get the signal time of fence
    static uint64_t getSignalTime(int fd);

    // wait for fd to be signaled without close fence fd
    static status_t waitWithoutCloseFd(int fd, int timeout, const char* log_name = "");

    // wait periodically for fd to be signaled, without close fence fd
    static status_t waitPeriodicallyWoCloseFd(int fd, int period_timeout, int timeout, const char* log_name = "");

private:
    static void dump(int fd);

    uint64_t m_client;
};

#endif // HWC_SYNC_H_
