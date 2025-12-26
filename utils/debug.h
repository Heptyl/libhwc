#ifndef UTILS_DEBUG_H_
#define UTILS_DEBUG_H_

#include <map>

#include <cutils/log.h>

#include <utils/StrongPointer.h>
#include <utils/LightRefBase.h>
#include <utils/String8.h>
#include <utils/Timers.h>
#include <utils/Mutex.h>
#include <ged/ged_log.h>
#include <hardware/hwcomposer_defs.h>

#include <unordered_map>
#include <vector>

#include "hwc_ui/Rect.h"

using hwc::Rect;

#ifndef DEBUG_LOG_TAG
#error "DEBUG_LOG_TAG is not defined!!"
#endif

#pragma GCC diagnostic error "-Wformat"
#pragma GCC diagnostic error "-Wformat-extra-args"
void check_args(const char* fmt, ...) __attribute__ ((format (printf, 1, 2)));

#define HWC_LOGV(x, ...)                                    \
        {                                                   \
            if (Debugger::m_skip_log != 1) { \
                if (false) check_args(x, ##__VA_ARGS__);        \
                DbgLogger logger(DbgLogger::TYPE_HWC_LOG,       \
                            'V',                            \
                            "[%s] " x, DEBUG_LOG_TAG, ##__VA_ARGS__);        \
            } \
        }

#define HWC_LOGD(x, ...)                                    \
        {                                                   \
            if (Debugger::m_skip_log != 1) { \
                if (false) check_args(x, ##__VA_ARGS__);        \
                DbgLogger logger(DbgLogger::TYPE_HWC_LOG,       \
                            'D',                            \
                            "[%s] " x, DEBUG_LOG_TAG, ##__VA_ARGS__);        \
            } \
        }

#define HWC_LOGI(x, ...)                                    \
        {                                                   \
            if (false) check_args(x, ##__VA_ARGS__);        \
            DbgLogger logger(DbgLogger::TYPE_HWC_LOG,       \
                            'I',                            \
                            "[%s] " x, DEBUG_LOG_TAG, ##__VA_ARGS__);        \
        }

#define HWC_LOGW(x, ...)                                    \
        {                                                   \
            if (false) check_args(x, ##__VA_ARGS__);        \
            DbgLogger logger(DbgLogger::TYPE_HWC_LOG,       \
                            'W',                            \
                            "[%s] " x, DEBUG_LOG_TAG, ##__VA_ARGS__);        \
        }

#define HWC_LOGE(x, ...)                                    \
        {                                                   \
            if (false) check_args(x, ##__VA_ARGS__);        \
            DbgLogger logger(DbgLogger::TYPE_HWC_LOG,       \
                            'E',                            \
                            "[%s] " x, DEBUG_LOG_TAG, ##__VA_ARGS__);        \
        }

#define ATRACE_TAG ATRACE_TAG_GRAPHICS
#include <utils/Trace.h>

#define LIKELY( exp )       (__builtin_expect( (exp) != 0, true  ))
#define UNLIKELY( exp )     (__builtin_expect( (exp) != 0, false ))

#ifdef MTK_HWC_HAVE_AEE_FEATURE
#include <aee.h>

#define HWC_ASSERT_OPT(exp, db_opt) \
    do { \
        if (!(exp)) { \
            const char* filename = NULL; \
            const char* slash = strrchr(__FILE__, '/'); \
            if (slash != NULL) { \
                filename = slash + 1; \
            } else { \
                filename = __FILE__; \
            } \
            HWC_LOGE("HWC_ASSERT("#exp") fail: \""  __FILE__ "\", %uL", __LINE__); \
            aee_system_exception("[HWC]", NULL, (db_opt), " %s, %uL", filename, __LINE__); \
        } \
    } while(0)

#define HWC_ASSERT(exp)    HWC_ASSERT_OPT(exp, DB_OPT_DEFAULT)
#define HWC_ASSERT_FT(exp) HWC_ASSERT_OPT(exp, DB_OPT_DEFAULT | DB_OPT_FTRACE)

#define HWC_WARNING_OPT(string, db_opt) \
    do { \
        const char* filename = NULL; \
        const char* slash = strrchr(__FILE__, '/'); \
        if (slash != NULL) { \
            filename = slash + 1; \
        } else { \
            filename = __FILE__; \
        } \
        HWC_LOGW("HWC_WARNING(" string"): \""  __FILE__ "\", %uL", __LINE__); \
        aee_system_warning("[HWC]", NULL, (db_opt), string"! %s, %uL", filename, __LINE__); \
    } while(0)

#define HWC_WARNING(string)    HWC_WARNING_OPT(string, DB_OPT_DEFAULT)
#define HWC_WARNING_FT(string) HWC_WARNING_OPT(string, DB_OPT_DEFAULT | DB_OPT_FTRACE)
#else // MTK_HWC_HAVE_AEE_FEATURE
#define HWC_ASSERT_OPT(exp, db_opt)
#define HWC_ASSERT(exp)
#define HWC_ASSERT_FT(exp)
#define HWC_WARNING_OPT(string, db_opt)
#define HWC_WARNING(string)
#define HWC_WARNING_FT(string)
#endif // MTK_HWC_HAVE_AEE_FEATURE

enum HWC_DEBUG_COMPOSE_LEVEL
{
    COMPOSE_ENABLE_ALL  = 0,
    COMPOSE_DISABLE_MM  = 1 << 0,
    COMPOSE_DISABLE_UI  = 1 << 1,
    COMPOSE_DISABLE_GLAI  = 1 << 2,
    COMPOSE_DISABLE_ALL = COMPOSE_DISABLE_MM | COMPOSE_DISABLE_UI | COMPOSE_DISABLE_GLAI
};

enum HWC_DEBUG_DUMP_LEVEL
{
    DUMP_NONE = 0,
    DUMP_MM   = 1,
    DUMP_UI   = 2,
    DUMP_SYNC = 4,
    DUMP_ALL  = 7
};

enum HWC_PROFILING_LEVEL
{
    PROFILE_NONE = 0,
    PROFILE_COMP = 1,
    PROFILE_BLT  = 2,
    PROFILE_TRIG = 4,
    PROFILE_DBG_WFD = 8,
};

#ifdef USE_SYSTRACE
#define HWC_ATRACE_CALL() android::ScopedTrace ___tracer(ATRACE_TAG, __FUNCTION__)
#define HWC_ATRACE_NAME(name) android::ScopedTrace ___tracer(ATRACE_TAG, name)
#define HWC_ATRACE_INT(name, value) atrace_int(ATRACE_TAG, name, value)
#define HWC_ATRACE_INT64(name, value) atrace_int64(ATRACE_TAG, name, value)
#define HWC_ATRACE_ASYNC_BEGIN(name, cookie) atrace_async_begin(ATRACE_TAG, name, static_cast<int32_t>(cookie))
#define HWC_ATRACE_ASYNC_END(name, cookie) atrace_async_end(ATRACE_TAG, name, static_cast<int32_t>(cookie))

#ifndef MTK_BASIC_PACKAGE
#ifdef ATRACE_TAG_PERF
#define HWC_ATRACE_FORMAT_NAME(name, ...)                                           \
    if (false) check_args(name, ##__VA_ARGS__);                                     \
    FormatScopedTrace ___tracer(ATRACE_TAG|ATRACE_TAG_PERF, name, ##__VA_ARGS__);
#else
#define HWC_ATRACE_FORMAT_NAME(name, ...)                                           \
    if (false) check_args(name, ##__VA_ARGS__);                                     \
    FormatScopedTrace ___tracer(ATRACE_TAG, name, ##__VA_ARGS__);
#endif
#else // MTK_BASIC_PACKAGE
#define HWC_ATRACE_FORMAT_NAME(name, ...)                                           \
    if (false) check_args(name, ##__VA_ARGS__);                                     \
    FormatScopedTrace ___tracer(ATRACE_TAG, name, ##__VA_ARGS__);
#endif // MTK_BASIC_PACKAGE

#else // USE_SYSTRACE
#define HWC_ATRACE_CALL()
#define HWC_ATRACE_NAME(name)
#define HWC_ATRACE_INT(name, value)
#define HWC_ATRACE_INT64(name, value)
#define HWC_ATRACE_ASYNC_BEGIN(name, cookie)
#define HWC_ATRACE_ASYNC_END(name, cookie)
#define HWC_ATRACE_FORMAT_NAME(name, ...)
#endif // USE_SYSTRACE

struct dump_buff
{
    char *msg;
    int msg_len;
    int len;
};

//=================================================================================================
//  DbgLogBufManager, DbgLogger and Debugger
//=================================================================================================

#define DBG_LOGGER_BUF_LEN 256

using namespace android;

//=================================================================================================
class DbgLogBufManager
{
public:
    struct DBG_BUF
    {
        DBG_BUF()
            : addr(NULL)
            , len(0)
            , idx(UNDEFINED)
        {}
        char* addr;
        unsigned int len;
        int idx;
    };

    static DbgLogBufManager& getInstance();
    ~DbgLogBufManager();
    void getLogBuf(DBG_BUF* dbg_buf);
    void releaseLogBuf(DBG_BUF* dbg_buf);
    void dump(String8* dump_str);

private:
    enum PREALLOCAED_LOG_BUF
    {
        SELF_ALLOCATED = -1,
        UNDEFINED = -2,
        TMP_BUF_CNT = 99,
    };

    DbgLogBufManager();

    int m_log_buf_uesd;
    unsigned int m_log_buf_slot[TMP_BUF_CNT];
    char* m_log_pool[TMP_BUF_CNT];
    Mutex m_mutex;
};

//=================================================================================================
class FormatScopedTrace
{
public:

    template<typename ...Args>
    inline FormatScopedTrace(uint64_t tag, const char* format, Args... values)
    {
        m_tag = tag;
        m_atrace_enabled = ATRACE_ENABLED();
        if (m_atrace_enabled)
        {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-security"
            if (snprintf(m_str, sizeof(m_str), format, values...) > 0)
#pragma GCC diagnostic pop
            {
                atrace_begin(m_tag, m_str);
            }
        }
    }
    inline ~FormatScopedTrace()
    {
        if (m_atrace_enabled)
        {
            atrace_end(m_tag);
        }
    }
private:
    char m_str[128];
    uint64_t m_tag;
    bool m_atrace_enabled = false;
};

//=================================================================================================
class DbgLogger;
class Debugger
{
public:
    struct LOGGER : public android::LightRefBase<LOGGER>
    {
        LOGGER(size_t num_displays);

        ~LOGGER();

        DbgLogger* dumpsys;
        std::vector<DbgLogger> set_info;
        std::vector<std::vector<DbgLogger>> ovlInput;
    };

    static Debugger& getInstance();
    ~Debugger();
    void dump(String8* dump_str);
#ifndef MTK_USER_BUILD
    inline GED_LOG_HANDLE getGedHandleHWCErr() { return m_ged_log_handle_hwc_err; }
    inline GED_LOG_HANDLE getGedHandleHWC() { return m_ged_log_handle_hwc; }
    inline GED_LOG_HANDLE getGedHandleFENCE() { return m_ged_log_handle_fence; }
#endif
    static int m_skip_log;

    sp<LOGGER> m_logger;

    uint32_t statistics_displayFrame_over_range;
    bool checkLevel(const unsigned char& level);
    void setLogThreshold(const unsigned char& log_threshold);
    unsigned char getLogThreshold();
private:
    Debugger();
#ifndef MTK_USER_BUILD
    GED_LOG_HANDLE m_ged_log_handle_fence;
    GED_LOG_HANDLE m_ged_log_handle_hwc_err;
    GED_LOG_HANDLE m_ged_log_handle_hwc;
#endif

    unsigned char m_log_threshold;
    std::map<unsigned char, int32_t> m_log_level_precedence;
};

//=================================================================================================
class DbgLogger
{
public:
    enum DBG_LOGGER_TYPE
    {
        TYPE_NONE       = 0x00,
        TYPE_HWC_LOG    = 0x01 << 0,
        TYPE_HWC_LOGE   = 0x01 << 1,
        TYPE_GED_LOG    = 0x01 << 2,
        TYPE_DUMPSYS    = 0x01 << 3,
        TYPE_STATIC     = 0x01 << 4,
        TYPE_FENCE      = 0x01 << 5,
        TYPE_HIDE       = 0x01 << 6,
        TYPE_PERIOD     = 0x01 << 7,
    };

    template<typename ...Args>
    DbgLogger(const uint32_t& type, const unsigned char& level, const char *fmt, Args... values);
    ~DbgLogger();

    template<typename ...Args>
    void printf(const char *fmt, Args... values);
    void getBuffer();
    void flushOut(char mark = ' ');
    void tryFlush();
    char* getLogString();
    unsigned int getLen() const { return m_len; };

private:
    unsigned char getLogLevel() const;
    bool needPrintLog() const;

    DbgLogBufManager::DBG_BUF m_buf;
    DbgLogBufManager::DBG_BUF m_bak_buf;
    unsigned int m_len;
    uint32_t m_type;
    nsecs_t m_last_flush_out;
    unsigned char m_level;
    bool m_has_ged;
};

template<typename ...Args>
DbgLogger::DbgLogger(const uint32_t& type, const unsigned char& level, const char *fmt, Args... values)
    : m_len(0)
    , m_type(type)
    , m_last_flush_out(0)
    , m_level(level)
    , m_has_ged(false)
{
    // when level is under threshold, hwc should skip snprintf() because of performance
    if (Debugger::getInstance().getLogThreshold() != 'V' && getLogLevel() == 'V')
        return;

#ifndef MTK_USER_BUILD
    if (Debugger::getInstance().getGedHandleHWCErr() != NULL ||
            Debugger::getInstance().getGedHandleHWC() != NULL ||
            Debugger::getInstance().getGedHandleFENCE() != NULL)
    {
        m_has_ged = true;
    }
#endif

    getBuffer();

    DbgLogger::printf(fmt, values...);
}

template<typename ...Args>
void DbgLogger::printf(const char *fmt, Args... values)
{
    if (NULL == m_buf.addr)
        return;

    if (m_len > m_buf.len - 1)
        return;

    if (!needPrintLog())
        return;

    if (fmt != nullptr)
    {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-security"
        int len = snprintf(m_buf.addr + m_len, m_buf.len - m_len, fmt, values...);
#pragma GCC diagnostic pop
        if (len > 0)
        {
            m_len += static_cast<unsigned int>(len);
        }
    }
}

void dump_buf(
    const uint32_t& format,
    const bool& compress,
    const int& dataspace,
    const int32_t& ion_fd,
    const unsigned int& src_buf_width,
    const unsigned int& src_buf_height,
    const unsigned int& stride,
    const unsigned int& v_stride,
    const int& size,
    const Rect& crop,
    const unsigned int& downsample,
    const char* prefix,
    const bool& log_enable);

struct FrameFenceInfo
{
    FrameFenceInfo()
        : job_id(0)
        , acquire_fence_fd(-1)
        , present_fence_fd(-1)
        , HWC_config_MDP_time(0)
        , HWC_expect_MDP_finish_time(0)
    {
    }

    uint64_t job_id;
    int acquire_fence_fd;
    int present_fence_fd;
    nsecs_t HWC_config_MDP_time;
    nsecs_t HWC_expect_MDP_finish_time;
};

class MDPFrameInfoDebugger
{
public:
    static MDPFrameInfoDebugger& getInstance();
    ~MDPFrameInfoDebugger();

    void insertJob(const uint64_t& job_id);
    void setJobAcquireFenceFd(const uint64_t& job_id, const int& fd);
    void setJobPresentFenceFd(const uint64_t& job_id, const int& fd);
    void setJobHWCExpectMDPFinsihTime(const uint64_t& job_id, const nsecs_t& time);
    void setJobHWCConfigMDPTime(const uint64_t& job_id, const nsecs_t& time);
    void checkMDPLayerExecTime();

private:
    MDPFrameInfoDebugger();

    mutable Mutex m_layer_frame_fence_info_lock;
    std::unordered_map<uint64_t, FrameFenceInfo> m_layer_frame_fence_info;

    nsecs_t m_avg_pre_minus_acq_time;
    uint32_t m_avg_count;
    uint32_t m_error_count;
    uint32_t m_sample_count;
};

#endif // UTILS_DEBUG_H_
