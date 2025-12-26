#pragma once

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include <utils/Timers.h>
#include <hardware/hwcomposer2.h>

namespace simplehwc {

class FakeVsyncThread
{
public:
    FakeVsyncThread(uint64_t display_id);
    ~FakeVsyncThread();

    void start(int64_t period);
    void stop();

    void setCallback(HWC2_PFN_VSYNC callback, hwc2_callback_data_t data);
    void setCallback_2_4(HWC2_PFN_VSYNC_2_4 callback, hwc2_callback_data_t data);

    void enableVsync(bool enable);

private:
    void threadLoop();

    std::thread m_thread;
    uint64_t m_display_id;
    int64_t m_period;

    std::mutex m_mutex;
    std::condition_variable m_condition;

    bool m_stop;
    bool m_enable;

    HWC2_PFN_VSYNC m_callback;
    hwc2_callback_data_t m_callback_data;

    HWC2_PFN_VSYNC_2_4 m_callback_2_4;
    hwc2_callback_data_t m_callback_data_2_4;

    nsecs_t m_vsync_timestamp;
};

}  // namespace simplehwc

