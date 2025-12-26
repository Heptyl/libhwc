#pragma once

#include <utils/Timers.h>

#include <mutex>

#include "utils/tools.h"

// ---------------------------------------------------------------------------

class DispatcherJob;
class DisplayBufferQueue;
class IOverlayDevice;

class DisplayDump
{
public:
    using FuncReadyCallback = std::function<void(const uint64_t, const native_handle_t*, const int, const int64_t)>;

    enum
    {
        REPAINT_NEVER,
        REPAINT_ALWAYS, // trigger repaint if setDumpBuf() called
        REPAINT_AUTO,   // trigger repaint if there is new refresh after prev setDumpBuf()
    };

public:
    DisplayDump(const uint64_t& dpy);

    ~DisplayDump();

    int setEnable(bool enable,
                  Rect src,
                  uint32_t dst_w, uint32_t dst_h,
                  unsigned int hal_format);

    int setDumpBuf(Rect src,
                   uint32_t dst_w, uint32_t dst_h,
                   int dump_point,
                   int repaint_mode,
                   int64_t min_period_before_prev_get_ns,
                   const native_handle_t* buffer,
                   int release_fence,
                   FuncReadyCallback cb);

    void onSetJob(DispatcherJob* job,
                  const sp<OverlayEngine>& ovl_device);

    void onProcess(DispatcherJob* job,
                   const sp<OverlayEngine>& ovl_device);

    uint32_t calculateProxHrtCost();

    void fillWdmaConfig(Rect& out_src_rect, uint32_t& out_dst_w,
                        uint32_t& out_dst_h, uint32_t& out_format, int& out_dump_point);

protected:
    mutable std::mutex m_mutex;

    // m_disp_id is display id
    uint64_t m_disp_id;

    bool m_enable = false;
    bool m_has_dump_buf = false;

    nsecs_t m_prev_dump_ts = 0;
    nsecs_t m_last_refresh_ts = 0;

    Rect m_src_rect;
    uint32_t m_dst_w = 0;
    uint32_t m_dst_h = 0;
    uint32_t m_format = HAL_PIXEL_FORMAT_RGB_888;
    int m_release_fence = -1;

    int m_dump_point;

    buffer_handle_t m_buffer = nullptr;
    PrivateHandle m_priv_handle;

    FuncReadyCallback m_ready_callback = nullptr;
};

