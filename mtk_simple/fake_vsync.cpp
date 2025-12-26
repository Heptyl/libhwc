#define DEBUG_LOG_TAG "fake_vsync"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include "fake_vsync.h"
#include <cutils/log.h>
#include <inttypes.h>
#include "debug_simple.h"

namespace simplehwc {

FakeVsyncThread::FakeVsyncThread(uint64_t display_id)
    : m_display_id(display_id)
    , m_period(0)
    , m_stop(false)
    , m_enable(false)
    , m_callback(nullptr)
    , m_callback_data(nullptr)
    , m_callback_2_4(nullptr)
    , m_callback_data_2_4(nullptr)
    , m_vsync_timestamp(0)
{
}

FakeVsyncThread::~FakeVsyncThread()
{
}

void FakeVsyncThread::start(int64_t period)
{
    m_period = period;
    m_thread = std::thread(&FakeVsyncThread::threadLoop, this);
}

void FakeVsyncThread::stop()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_condition.notify_all();
    m_thread.join();
}

void FakeVsyncThread::setCallback(HWC2_PFN_VSYNC callback, hwc2_callback_data_t data)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_callback = callback;
    m_callback_data = data;
}

void FakeVsyncThread::setCallback_2_4(HWC2_PFN_VSYNC_2_4 callback, hwc2_callback_data_t data)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_callback_2_4 = callback;
    m_callback_data_2_4 = data;
}

void FakeVsyncThread::enableVsync(bool enable)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_enable = enable;
    }
    HWC_LOGD("%s: enableVsync: %d, refresh=[%" PRId64 "]", __func__, enable, m_period);
    m_condition.notify_all();
}

void FakeVsyncThread::threadLoop()
{
    while (true)
    {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (m_stop)
            {
                ALOGI("%s: stop thread loop", __func__);
                return;
            }
            if (!m_enable)
            {
                m_condition.wait(lock, [this] { return m_enable; });
            }
        }

        nsecs_t now = systemTime();
        if (now > m_vsync_timestamp)
        {
            int64_t count = (now - m_vsync_timestamp + m_period - 1) / m_period;
            m_vsync_timestamp += m_period * count;
            usleep(static_cast<unsigned int>((m_vsync_timestamp - now) / 1000));
        }
        HWC_LOGV("%s: FakeVsyncThread loop +", __func__);

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_callback || m_callback_2_4)
            {
                HWC_LOGI("fire a vsync with ts[%" PRId64 "]", m_vsync_timestamp);
                char atrace_tag[128];
                if (snprintf(atrace_tag, sizeof(atrace_tag), "FakeVsyncThread: fire a vsync with ts[%" PRId64 "]", m_vsync_timestamp) > 0)
                {
                    HWC_ATRACE_NAME(atrace_tag);
                }

                if (m_callback)
                {
                    m_callback(m_callback_data, m_display_id, m_vsync_timestamp);
                }
                else if (m_callback_2_4)
                {
                    m_callback_2_4(m_callback_data_2_4, m_display_id, m_vsync_timestamp,
                            static_cast<hwc2_vsync_period_t>(m_period));
                }
            }
        }
    }
}

}  // namespace simplehwc

