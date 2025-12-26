#pragma once

#include <utils/StrongPointer.h>
#include <utils/Timers.h>

#include <mutex>

#include "bliter_ultra.h"

// ---------------------------------------------------------------------------

namespace android
{
class String8;
}

class BliterNode;
class DispatcherJob;
class DisplayBufferQueue;
class IOverlayDevice;

class AiBluLightDefender
{
public:
    static AiBluLightDefender& getInstance();

    ~AiBluLightDefender();

    void dump(android::String8* dump_str) const;

    int setEnable(bool enable,
                  uint32_t fps = 0,
                  uint32_t w = 256,
                  uint32_t h = 256,
                  uint32_t format = HAL_PIXEL_FORMAT_RGB_888);

    // dequeue dbq, set buf to kernel, kernel ret fence
    void onSetJob(DispatcherJob* job,
                  const sp<OverlayEngine>& ovl_device,
                  const sp<HWCDisplay>& display);

    void onProcess(DispatcherJob* job,
                   const sp<OverlayEngine>& ovl_device);

    void setDumpEnable(bool enable) { m_dump_enable = enable; }

    uint32_t calculateProxHrtCost(const sp<HWCDisplay>& display);

    void fillWdmaConfig(const sp<HWCDisplay>& display, DispatcherJob* job,
                        Rect& out_src_rect, uint32_t& out_dst_w,
                        uint32_t& out_dst_h, uint32_t& out_format, int& out_dump_point);

protected:
    AiBluLightDefender();

    void freeResources();

    void threadLoop();

protected:

    struct Job
    {
        uint32_t mdp_job_id;
        sp<DisplayBufferQueue> queue;
        std::shared_ptr<BliterNode> bliter_node;
        int mdp_in_ion_fd;
        buffer_handle_t src_handle; // for disp out buf
        uint32_t pf_fence_idx;

        bool dump_enable;
        // for dump, only assign if need dump
        PrivateHandle mdp_in_priv_handle;
    };

    mutable std::mutex m_mutex;
    bool m_enable = false;
    nsecs_t m_sample_period = INT64_MAX;
    nsecs_t m_prev_sample_ts = 0;
    uint32_t m_width = 256;
    uint32_t m_height = 256;
    uint32_t m_format = HAL_PIXEL_FORMAT_RGB_888;
    int m_dump_point;

    std::shared_ptr<BliterNode> m_bliter_node;

    sp<DisplayBufferQueue> m_disp_queue;
    sp<DisplayBufferQueue> m_mdp_queue;

    BufferConfig m_dp_config;

    std::thread m_thread;
    mutable std::mutex m_thread_mutex;
    mutable std::condition_variable m_condition;
    bool m_thread_stop = false;
    std::list<Job> m_job_list;

    bool m_dump_enable = false;

    sp<FenceDebugger> m_fence_debugger;
};

