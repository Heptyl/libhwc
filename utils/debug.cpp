#define DEBUG_LOG_TAG "DBG"
#define LOG_NDEBUG 0

#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>

#include <cutils/properties.h>
#include "graphics_mtk_defs.h"
#include <gm/gm_pixel_format.h>
#include "utils/debug.h"
#include "utils/tools.h"

using namespace mediatek::graphics::common;

int Debugger::m_skip_log = 1;

//=================================================================================================
DbgLogBufManager& DbgLogBufManager::getInstance()
{
    static DbgLogBufManager gInstance;
    return gInstance;
}

DbgLogBufManager::DbgLogBufManager()
{
    m_log_buf_uesd = 0;
    for (unsigned int i = 0; i < TMP_BUF_CNT; i++)
    {
        m_log_buf_slot[i] = i;
        m_log_pool[i] = NULL;
    }
}

DbgLogBufManager::~DbgLogBufManager()
{
    for (int i = 0; i < TMP_BUF_CNT; i++)
    {
        if (NULL != m_log_pool[i])
            free(m_log_pool[i]);
    }
}

void DbgLogBufManager::getLogBuf(DBG_BUF* dbg_buf)
{
    if (dbg_buf->addr != NULL)
        return;

    Mutex::Autolock _l(m_mutex);
    if (m_log_buf_uesd < TMP_BUF_CNT)
    {
        unsigned int id = m_log_buf_slot[m_log_buf_uesd++];

        if (NULL == m_log_pool[id])
        {
            m_log_pool[id] = (char*)malloc(DBG_LOGGER_BUF_LEN * sizeof(char));
            LOG_ALWAYS_FATAL_IF(m_log_pool[id] == nullptr, "DbgLog pool malloc(%zu) fail",
                DBG_LOGGER_BUF_LEN * sizeof(char));
        }

        dbg_buf->idx = static_cast<int>(id);
        dbg_buf->addr = m_log_pool[id];
    }
    else
    {
        dbg_buf->idx = SELF_ALLOCATED;
        dbg_buf->addr = (char*)malloc(DBG_LOGGER_BUF_LEN * sizeof(char));
        LOG_ALWAYS_FATAL_IF(dbg_buf->addr == nullptr, "Dbg buf malloc(%zu) fail",
            DBG_LOGGER_BUF_LEN * sizeof(char));
    }

    dbg_buf->len = DBG_LOGGER_BUF_LEN;
    return;
}

void DbgLogBufManager::releaseLogBuf(DBG_BUF* dbg_buf)
{
    if (dbg_buf->addr == NULL)
        return;

    if (SELF_ALLOCATED == dbg_buf->idx)
    {
        free(dbg_buf->addr);
    }
    else
    {
        Mutex::Autolock _l(m_mutex);
        m_log_buf_slot[--m_log_buf_uesd] = static_cast<unsigned int>(dbg_buf->idx);
    }

    dbg_buf->addr = NULL;
    dbg_buf->len = 0;
}

void DbgLogBufManager::dump(String8* dump_str)
{
    Mutex::Autolock _l(m_mutex);
    dump_str->appendFormat("  DBGLBM: %d/%d used\n", m_log_buf_uesd, TMP_BUF_CNT);
}

DbgLogger::~DbgLogger()
{
    if (NULL == m_buf.addr)
        return;

    flushOut();
    DbgLogBufManager::getInstance().releaseLogBuf(&m_buf);
}

void DbgLogger::getBuffer()
{
    if (TYPE_NONE == m_type)
        return;

    DbgLogBufManager::getInstance().getLogBuf(&m_buf);
    if (m_type & TYPE_PERIOD)
    {
        DbgLogBufManager::getInstance().getLogBuf(&m_bak_buf);
    }
    m_len = 0;
    m_buf.addr[0] = '\0';
}

void DbgLogger::flushOut(char mark)
{
    if (NULL == m_buf.addr)
        return;

    if (m_len == 0)
    {
        return;
    }

    bool output_main_log = false;

    if (0 != (m_type & TYPE_HWC_LOG))
    {
#ifndef MTK_USER_BUILD
        if ((Debugger::getInstance().getGedHandleHWCErr() != nullptr) &&
                (getLogLevel() == 'I' || getLogLevel() == 'W' || getLogLevel() == 'E' || getLogLevel() == 'F'))
        {
            GED_ERROR ret = ged_log_tpt_print(Debugger::getInstance().getGedHandleHWCErr(), "%c %s %c", m_level, m_buf.addr, mark);
            if (CC_UNLIKELY(ret != GED_OK)) {
                ALOGW("%s(), ged_log_tpt_print fail, ret %x", __FUNCTION__, ret);
            }
        }
#endif

        output_main_log = (m_type & TYPE_PERIOD) ?
#ifdef MTK_USER_BUILD
            Debugger::m_skip_log != 1 : Debugger::getInstance().checkLevel(getLogLevel());
#else
            mark != '@' || Debugger::m_skip_log != 1 : Debugger::getInstance().checkLevel(getLogLevel());
#endif

        if (output_main_log)
        {
            switch (getLogLevel())
            {
                case 'F':
                case 'E':
                    ALOGE("%s %c", m_buf.addr, mark);
                    break;

                case 'W':
                    ALOGW("%s %c", m_buf.addr, mark);
                    break;

                case 'I':
                    ALOGI("%s %c", m_buf.addr, mark);
                    break;

                case 'D':
                    ALOGD("%s %c", m_buf.addr, mark);
                    break;

                case 'V':
                    ALOGV("%s %c", m_buf.addr, mark);
                    break;

                default:
                    ALOGE("unknown log level(%c) %s", m_level, m_buf.addr);
            }
        }
#ifndef MTK_USER_BUILD
        if ((Debugger::getInstance().getGedHandleHWC() != nullptr) &&
                (getLogLevel() != 'V' || Debugger::getInstance().getLogThreshold() == 'V'))
            ged_log_tpt_print(Debugger::getInstance().getGedHandleHWC(), "%c %s %c", m_level, m_buf.addr, mark);
#endif
    }

    if (0 != (m_type & TYPE_DUMPSYS))
        Debugger::getInstance().m_logger->dumpsys->printf("\n  %s", m_buf.addr);

#ifndef MTK_USER_BUILD
    if ((Debugger::getInstance().getGedHandleFENCE() != nullptr) &&
            (0 != (m_type & TYPE_FENCE)))
        ged_log_tpt_print(Debugger::getInstance().getGedHandleFENCE(),"%s", m_buf.addr);
#endif

    if ((m_type & TYPE_PERIOD))
    {
        if (mark == '!')
        {
            strncpy(m_bak_buf.addr, m_buf.addr, m_bak_buf.len - 1);
            m_bak_buf.addr[m_bak_buf.len - 1] = '\0';
        }

        if (output_main_log)
            m_last_flush_out = systemTime();
    }
    m_len = 0;
    m_buf.addr[0] = '\0';
}

void DbgLogger::tryFlush()
{
    const nsecs_t PERIOD_THRESHOLD = 5e+8;
    if (NULL == m_buf.addr)
        return;

    nsecs_t now = systemTime();
    char mark = ' ';
    if ((m_type & TYPE_PERIOD) && (1 == Debugger::m_skip_log))
    {
        if (NULL == m_bak_buf.addr)
            return;

        if (0 == strcmp(m_bak_buf.addr, m_buf.addr))
        {
            mark = (now - m_last_flush_out) < PERIOD_THRESHOLD ? '@' : '=';
        }
        else
        {
            mark = '!';
        }
    }
    flushOut(mark);
}

char* DbgLogger::getLogString()
{
    return m_buf.addr;
}

unsigned char DbgLogger::getLogLevel() const
{
    return m_level;
}

bool DbgLogger::needPrintLog() const
{
    if (m_has_ged)
    {
        return true;
    }

    bool output_main_log = false;
    if (m_type & TYPE_PERIOD)
    {
#ifdef MTK_USER_BUILD
        output_main_log = Debugger::m_skip_log != 1;
#else
        output_main_log = true;
#endif
    }
    else
    {
        output_main_log = Debugger::getInstance().checkLevel(getLogLevel());
    }

    if (output_main_log)
    {
        return true;
    }

    return false;
}
//=================================================================================================
Debugger& Debugger::getInstance()
{
    static Debugger gInstance;
    return gInstance;
}

Debugger::Debugger()
    : statistics_displayFrame_over_range(0)
    , m_log_threshold('I')
{
    m_log_level_precedence['V'] = 0;
    m_log_level_precedence['D'] = 1;
    m_log_level_precedence['I'] = 2;
    m_log_level_precedence['W'] = 3;
    m_log_level_precedence['E'] = 4;
    m_log_level_precedence['F'] = 5;

#ifndef MTK_USER_BUILD
    m_ged_log_handle_hwc_err = ged_log_connect("HWC_err");
    m_ged_log_handle_hwc = ged_log_connect("HWC");
    m_ged_log_handle_fence = ged_log_connect("FENCE");
#endif
}

Debugger::~Debugger()
{
#ifndef MTK_USER_BUILD
    ged_log_disconnect(m_ged_log_handle_hwc_err);
    ged_log_disconnect(m_ged_log_handle_hwc);
    ged_log_disconnect(m_ged_log_handle_fence);
#endif
}

void Debugger::dump(String8* dump_str)
{
    dump_str->appendFormat("[HWC DBG Dump] log threshold:%c", getLogThreshold());

    char* string_dumpsys = m_logger->dumpsys->getLogString();
    if (NULL == string_dumpsys)
        dump_str->appendFormat("%s\n", "NULL STRING");
    else
        dump_str->appendFormat("%s\n", string_dumpsys);

    DbgLogBufManager::getInstance().dump(dump_str);

    dump_str->appendFormat("\n[HWC Statistics]\n");
    dump_str->appendFormat("  %d - displayFrame over range\n", statistics_displayFrame_over_range);
}

bool Debugger::checkLevel(const unsigned char& level)
{
    return m_log_level_precedence[level] >= m_log_level_precedence[getLogThreshold()];
}

void Debugger::setLogThreshold(const unsigned char& log_threshold)
{
    m_log_threshold = log_threshold;
}

unsigned char Debugger::getLogThreshold()
{
    static bool is_first_called = true;
    if (UNLIKELY(is_first_called))
    {
        char value[PROPERTY_VALUE_MAX] = {0};
        property_get("persist.vendor.debug.hwc.log", value, "0");
        if (value[0] != '0')
        {
            setLogThreshold(value[0]);
        }
        else
        {
            // For a fps-related demo, someone asks us to give a non-log hwcomposer.
            // In that case, a better way is to modify the string by a binary file editor.
            if (snprintf(value, PROPERTY_VALUE_MAX - 1, "persist.vendor.debug.hwc.log=I") > 0)
            {
                setLogThreshold(value[strlen(value) - 1]);
            }
        }
        is_first_called = false;
    }
    return m_log_threshold;
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
    const bool& log_enable)
{
    if (compress && downsample > 1)
    {
        HWC_LOGE("%s do not support downsample when the buffer is compressed", __func__);
        return;
    }

    const unsigned int height = compress ? v_stride : static_cast<unsigned int>(HEIGHT(crop));
    const unsigned int width = compress ? stride : static_cast<unsigned int>(WIDTH(crop));
    const unsigned int Bpp = getBitsPerPixel(format) / 8;
    const unsigned int mmap_size = compress ? static_cast<unsigned int>(size) : stride * static_cast<unsigned int>(crop.bottom) * Bpp;
    const String8 path = String8::format("%s_w%uh%u_B%u_c%d_C%d_d%d_D%u_bw%ubh%u_s%uvs%u_[x%d,y%d,w%d,h%d].%s",
        prefix,
        (width % downsample == 0) ? width / downsample : width / downsample + 1,
        (height % downsample == 0) ? height / downsample : height / downsample + 1,
        Bpp,
        compress,
        !compress, // dump cropped image when the buffer is not compressed
        dataspace,
        downsample,
        src_buf_width,
        src_buf_height,
        stride,
        v_stride,
        crop.left, crop.top, WIDTH(crop), HEIGHT(crop), // sorce crop
        getFormatString(format).string());

    if (Bpp == 0)
    {
        HWC_LOGE("%s wrong Bpp(%u) path:%s", __func__, Bpp, path.string());
        return;
    }

    int shared_fd = -1;
    int res = IONDevice::getInstance().ionImport(ion_fd, &shared_fd);
    if (res != 0 || shared_fd < 0)
    {
        HWC_LOGE("dump_buf: ionImport is failed: %s(%d), ion_fd(%d)", strerror(errno), res, ion_fd);
        return;
    }

    void *ptr = nullptr;
    if (ion_fd != -1 && shared_fd != -1)
    {
        ptr = IONDevice::getInstance().ionMMap(ion_fd, mmap_size, PROT_READ, MAP_SHARED,
                                               shared_fd);
    }

    if (ptr == nullptr || ptr == MAP_FAILED)
    {
        HWC_LOGE("dump_buf: ion mmap fail");
    }
    else
    {
        FILE* fp = fopen(path.string(), "wb");
        if (fp)
        {
            if (compress)
            {
                size_t written_count = fwrite(static_cast<void*>(ptr), 1, static_cast<size_t>(mmap_size), fp);
                if (CC_UNLIKELY(written_count != static_cast<size_t>(mmap_size)))
                {
                    HWC_LOGE("%s(), fwrite fail", __FUNCTION__);
                }
            }
            else
            {
                unsigned int top = static_cast<unsigned int>(crop.top);
                unsigned int left = static_cast<unsigned int>(crop.left);
                unsigned char* pptr = (unsigned char*)(ptr);
                unsigned char* pptr_offset = pptr + (top * stride + left) * Bpp;
                uint32_t curr_dump_height = 0;
                int32_t print_cnt = 0;

                while(pptr_offset < pptr + mmap_size)
                {
                    if (log_enable && print_cnt < 3)
                    {
                        HWC_LOGI("pptr_offset-ptr:%" PRIu64 " crop[%d,%d,%d,%d] w:%u h:%u s:%d downsample:%u mmap_size:%u Bpp:%u",
                                static_cast<uint64_t>(pptr_offset - pptr),
                                crop.left, crop.top, crop.right, crop.bottom, width, height, stride,
                                downsample, mmap_size, Bpp);
                        ++print_cnt;
                    }

                    size_t write_count = 1;
                    size_t written_count = fwrite(pptr_offset, Bpp, write_count, fp);
                    if (CC_UNLIKELY(written_count != write_count))
                    {
                        HWC_LOGE("%s(), fwrite fail", __FUNCTION__);
                    }
                    pptr_offset += downsample * Bpp;
                    if ((pptr_offset - pptr) >= static_cast<int32_t>(((top + curr_dump_height) * stride + left + width) * Bpp))
                    {
                        curr_dump_height += downsample;
                        pptr_offset = pptr + ((top + curr_dump_height) * stride + left) * Bpp;
                        print_cnt = 0;
                    }
                }
            }
            int ret = fclose(fp);
            if (CC_UNLIKELY(ret != 0)) {
                HWC_LOGE("%s(), fclose fail", __FUNCTION__);
            }
            fp = nullptr;
        }
        else
        {
            HWC_LOGE("open file failed [%s]", path.string());
        }

        if (IONDevice::getInstance().ionMUnmap(ion_fd, ptr, mmap_size) != 0)
        {
            HWC_LOGW("dump_buf: failed to unmap buffer");
        }
        ptr = nullptr;
    }

    if (shared_fd != -1 && IONDevice::getInstance().ionClose(shared_fd))
    {
        HWC_LOGW("dump_buf: ionClose is failed: %s , share_fd(%d)", strerror(errno), shared_fd);
        shared_fd = -1;
    }

    HWC_LOGI("dump buf: %s format:%d compress:%d dataspace%d Bpp:%u src_buf_width:%u src_buf_height:%u \
              stride:%u v_stride:%u width:%u height:%u downsample:%u",
              path.string(), format, compress, dataspace, Bpp, src_buf_width, src_buf_height,
              stride, v_stride, width, height, downsample);
}

Debugger::LOGGER::LOGGER(size_t num_displays)
{
    dumpsys = new DbgLogger(DbgLogger::TYPE_STATIC, 'D', nullptr);

    ovlInput.resize(num_displays);
    for (size_t i = 0; i < num_displays; i++)
    {
        set_info.push_back(DbgLogger(DbgLogger::TYPE_HWC_LOG, 'D', nullptr));

        for (unsigned int j = 0; j < getHwDevice()->getMaxOverlayInputNum(); j++)
        {
            ovlInput[i].push_back(DbgLogger(DbgLogger::TYPE_HWC_LOG | DbgLogger :: TYPE_PERIOD, 'D', nullptr));
        }
    }
}

Debugger::LOGGER::~LOGGER()
{
    delete dumpsys;
}

MDPFrameInfoDebugger& MDPFrameInfoDebugger::getInstance()
{
    static MDPFrameInfoDebugger gInstance;
    return gInstance;
}

MDPFrameInfoDebugger::MDPFrameInfoDebugger()
{
    m_layer_frame_fence_info.clear();
    m_avg_pre_minus_acq_time = 0;
    m_avg_count = 0;
    m_error_count = 0;
    m_sample_count = 0;
}

MDPFrameInfoDebugger::~MDPFrameInfoDebugger()
{
}

void MDPFrameInfoDebugger::insertJob(const uint64_t& job_id)
{
    AutoMutex l(m_layer_frame_fence_info_lock);
    FrameFenceInfo temp;
    temp.job_id = job_id;
    m_layer_frame_fence_info.insert({job_id, temp});
}

void MDPFrameInfoDebugger::setJobAcquireFenceFd(const uint64_t& job_id, const int& fd)
{
    AutoMutex l(m_layer_frame_fence_info_lock);
    auto temp = m_layer_frame_fence_info.find(job_id);
    if (temp != m_layer_frame_fence_info.end())
    {
        temp->second.acquire_fence_fd = fd;
    }
}

void MDPFrameInfoDebugger::setJobPresentFenceFd(const uint64_t& job_id, const int& fd)
{
    AutoMutex l(m_layer_frame_fence_info_lock);
    auto temp = m_layer_frame_fence_info.find(job_id);
    if (temp != m_layer_frame_fence_info.end())
    {
        temp->second.present_fence_fd = fd;
    }

}

void MDPFrameInfoDebugger::setJobHWCConfigMDPTime(const uint64_t& job_id, const nsecs_t& time)
{
    AutoMutex l(m_layer_frame_fence_info_lock);
    auto temp = m_layer_frame_fence_info.find(job_id);
    if (temp != m_layer_frame_fence_info.end())
    {
        temp->second.HWC_config_MDP_time = time;
    }
}

void MDPFrameInfoDebugger::setJobHWCExpectMDPFinsihTime(const uint64_t& job_id, const nsecs_t& time)
{
    AutoMutex l(m_layer_frame_fence_info_lock);
    auto temp = m_layer_frame_fence_info.find(job_id);
    if (temp != m_layer_frame_fence_info.end())
    {
        temp->second.HWC_expect_MDP_finish_time = time;
    }
}

void MDPFrameInfoDebugger::checkMDPLayerExecTime()
{
    AutoMutex l(m_layer_frame_fence_info_lock);

    nsecs_t curTime = systemTime();
    uint64_t removeStartJobID = 0;
    std::unordered_map<uint64_t, FrameFenceInfo>::iterator it;

    for (auto& info: m_layer_frame_fence_info)
    {
        nsecs_t acqTime = getFenceSignalTime(info.second.acquire_fence_fd);
        nsecs_t preTime = getFenceSignalTime(info.second.present_fence_fd);
        if (acqTime != SIGNAL_TIME_INVALID && acqTime != SIGNAL_TIME_PENDING &&
            preTime != SIGNAL_TIME_INVALID && preTime != SIGNAL_TIME_PENDING)
        {
            nsecs_t diff_pres_HWCExpectTime = (preTime > info.second.HWC_expect_MDP_finish_time)?
                    preTime - info.second.HWC_expect_MDP_finish_time:info.second.HWC_expect_MDP_finish_time-preTime;
            HWC_LOGI("[%" PRIu64 "] p:%" PRIu64 ",m_HWCExpectDisplayTime:%" PRIu64 ",p-a diff:%" PRIu64 ",a-s diff:%" PRIu64 ",p-s diff:%" PRIu64 ",p-hwc diff:%" PRIu64 ,
                info.second.job_id,
                preTime,
                info.second.HWC_expect_MDP_finish_time,
                preTime - acqTime,
                acqTime - info.second.HWC_config_MDP_time,
                preTime - info.second.HWC_config_MDP_time,
                diff_pres_HWCExpectTime);

            if (m_avg_count < 100)
                m_avg_count++;

            m_avg_pre_minus_acq_time = (m_avg_pre_minus_acq_time*(m_avg_count -1 ) + (preTime - acqTime))/(m_avg_count);

            if (diff_pres_HWCExpectTime > 16 * 1000 * 1000)
                m_error_count++;

            m_sample_count++;
        }

        if (curTime > preTime && (preTime != SIGNAL_TIME_INVALID && preTime != SIGNAL_TIME_PENDING))
        {
            if (removeStartJobID == 0) {
                removeStartJobID = info.first;
            }
        }
    }

    HWC_LOGI("m_avg_pre_minus_acq_Time:%" PRIu64 ",m_count:%d,(%d/%d)", m_avg_pre_minus_acq_time, m_avg_count, m_error_count, m_sample_count);

    for (it = m_layer_frame_fence_info.find(removeStartJobID); it != m_layer_frame_fence_info.end(); ++it)
    {
        if ((*it).second.acquire_fence_fd != -1) {
            ::protectedClose((*it).second.acquire_fence_fd);
            (*it).second.acquire_fence_fd = -1;
        }
        if ((*it).second.present_fence_fd != -1) {
            ::protectedClose((*it).second.present_fence_fd);
            (*it).second.present_fence_fd = -1;
        }
    }

    if (m_layer_frame_fence_info.find(removeStartJobID) != m_layer_frame_fence_info.end()) {
        m_layer_frame_fence_info.erase(m_layer_frame_fence_info.find(removeStartJobID),
                                       m_layer_frame_fence_info.end());
    }
}


