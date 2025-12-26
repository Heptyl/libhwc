#define DEBUG_LOG_TAG "DISP_DUMP"

#include "display_dump.h"

#include "dev_interface.h"
#include "dispatcher.h"
#include "overlay.h"
#include "queue.h"
#include "utils/debug.h"

#include <utils/String8.h>

#include <sstream>
#include <sys/stat.h>
#include <vector>

#ifdef USE_SWWATCHDOG
#include "utils/swwatchdog.h"
#endif

using std::endl;

DisplayDump::DisplayDump(const uint64_t& dpy)
    : m_disp_id(dpy)
    , m_dump_point(HWC_WDMA_DUMP_POINT_PQ_OUT)
{
    HWC_LOGI("%s(), created with dpy(%" PRIu64 ")",__FUNCTION__ ,dpy)
}

DisplayDump::~DisplayDump()
{
}

int DisplayDump::setEnable(bool enable,
                           Rect src,
                           uint32_t dst_w, uint32_t dst_h,
                           unsigned int hal_format)
{
    HWC_LOGI("%s(), dpy(%" PRId64 "), enable %d, src(%d, %d, %d, %d), dst(%d, %d), fmt 0x%x",
             __FUNCTION__,
             m_disp_id,
             enable,
             src.left, src.top, src.right, src.bottom,
             dst_w, dst_h,
             hal_format);

    std::lock_guard<std::mutex> lock(m_mutex);

    if (enable)
    {
        m_prev_dump_ts = 0;
        m_src_rect = src;
        m_dst_w = dst_w;
        m_dst_h = dst_h;
        m_format = hal_format;
    }
    else
    {
        if (m_has_dump_buf)
        {
            freeDuppedBufferHandle(m_buffer);
            m_has_dump_buf = false;
        }
    }

    m_enable = enable;

    return 0;
}

int DisplayDump::setDumpBuf(Rect src,
                            uint32_t dst_w, uint32_t dst_h,
                            int dump_point,
                            int repaint_mode,
                            int64_t min_period_before_prev_get_ns,
                            const native_handle_t* buffer,
                            int /*release_fence*/,                          // TODO: handle?
                            FuncReadyCallback cb)
{
    HWC_LOGI("%s(), dpy(%" PRId64 "), src(%d, %d, %d, %d), dst(%d, %d), (%d, %d), %" PRId64 ", (%d), (%p)",
             __FUNCTION__,
             m_disp_id,
             src.left, src.top, src.right, src.bottom,
             dst_w, dst_h,
             dump_point, repaint_mode,
             min_period_before_prev_get_ns,
             -1,
             buffer);

    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_enable)
    {
        HWC_LOGW("%s(), need enable before set buf", __FUNCTION__);
        return -EACCES;
    }

    if (m_has_dump_buf)
    {
        return -EBUSY;
    }

    if (buffer == nullptr)
    {
        HWC_LOGE("%s(), buffer == nulltpr", __FUNCTION__);
        return -EINVAL;
    }

    if (cb == nullptr)
    {
        HWC_LOGE("%s(), cb == nulltpr", __FUNCTION__);
        return -EINVAL;
    }

    if (systemTime() - m_prev_dump_ts < min_period_before_prev_get_ns)
    {
        HWC_LOGI("%s(), period less than min_period_before_prev_get_ns", __FUNCTION__);
        return -EAGAIN;
    }

    // check input valid (compare to setEnable())
    dupBufferHandle(buffer, &m_buffer);
    status_t err = 0;
    err = getPrivateHandle(m_buffer, &m_priv_handle, nullptr, true);
    err |= getAllocId(m_buffer, &m_priv_handle);
    if (err != NO_ERROR)
    {
        HWC_LOGE("Failed to get private handle !! (outbuf=%p) !!", m_buffer);
        freeDuppedBufferHandle(m_buffer);
        m_buffer = nullptr;
        return -EINVAL;
    }

    if (m_priv_handle.format != m_format ||
        m_priv_handle.width != m_dst_w ||
        m_priv_handle.height != m_dst_h)
    {
        HWC_LOGE("buffer w %d, h %d, format 0x%x not match",
                 m_priv_handle.width,
                 m_priv_handle.height,
                 m_priv_handle.format);
        freeDuppedBufferHandle(m_buffer);
        m_buffer = nullptr;
        return -EINVAL;
    }

    m_has_dump_buf = true;
    m_src_rect = src;
    m_release_fence = -1;
    m_ready_callback = cb;
    m_dump_point = dump_point;

    // trigger repaint
    if (repaint_mode == REPAINT_ALWAYS ||
        (repaint_mode == REPAINT_AUTO && m_last_refresh_ts > m_prev_dump_ts)) {
        DisplayManager::getInstance().refreshForDisplay(m_disp_id,
                                                        HWC_REFRESH_FOR_DISPLAY_DUMP);
    }

    return 0;
}

void DisplayDump::onSetJob(DispatcherJob* job,
                           const sp<OverlayEngine>& ovl_device)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    const nsecs_t cur_time = systemTime();

    m_last_refresh_ts = cur_time;

    if (!m_enable)
    {
        return;
    }

    if (!m_has_dump_buf)
    {
        return;
    }

    if (CC_UNLIKELY(!job))
    {
        HWC_LOGW("job is nullptr");
        return;
    }

    if (CC_UNLIKELY(!ovl_device))
    {
        HWC_LOGW("ovl_device is nullptr");
        return;
    }

    ATRACE_NAME(__FUNCTION__);

    m_has_dump_buf = false;
    m_prev_dump_ts = cur_time;
    job->display_dump_enable = true;

    // fill hw buffer
    buffer_handle_t outbuf_hnd = m_buffer;

    HWBuffer* hw_outbuf = &job->hw_outbuf;
    hw_outbuf->handle = outbuf_hnd;
    hw_outbuf->wdma_dump_roi = m_src_rect;
    hw_outbuf->wdma_dump_point = m_dump_point;

    memcpy(&hw_outbuf->priv_handle, &m_priv_handle, sizeof(PrivateHandle));

    // get out fence
    OverlayPrepareParam prepare_param;
    prepare_param.ion_fd = hw_outbuf->priv_handle.ion_fd;
    prepare_param.is_need_flush = 0;
    prepare_param.blending = HWC2_BLEND_MODE_NONE;
    prepare_param.drm_id_crtc = job->drm_id_cur_crtc;

    status_t err = ovl_device->prepareOutput(prepare_param);
    if (err != NO_ERROR)
    {
        prepare_param.fence_index = 0;
        prepare_param.fence_fd = -1;
        HWC_LOGE("prepareOutput failed, err %d", err);
    }
    if (prepare_param.fence_fd <= 0)
    {
        HWC_LOGE("get out fence failed, %d", prepare_param.fence_fd);
    }

    hw_outbuf->out_acquire_fence_fd = m_release_fence;
    hw_outbuf->out_retire_fence_idx = prepare_param.fence_index;

    int64_t vsync_period_ns = static_cast<int64_t>(job->disp_data->refresh);
    m_ready_callback(m_disp_id, outbuf_hnd, prepare_param.fence_fd, vsync_period_ns);
    m_buffer = nullptr;
    protectedClose(prepare_param.fence_fd);
}

void DisplayDump::onProcess(DispatcherJob* job,
                            const sp<OverlayEngine>& ovl_device)
{
    if (!job->display_dump_enable)
    {
        return;
    }

    if (!job->hw_outbuf.handle)
    {
        return;
    }

    const HWBuffer* hw_outbuf = &job->hw_outbuf;
    const PrivateHandle* priv_handle = &hw_outbuf->priv_handle;

    OverlayPortParam param;

    param.pitch             = priv_handle->y_stride;
    param.format            = priv_handle->format;
    param.src_crop          = hw_outbuf->wdma_dump_roi;
    param.dst_crop          = param.src_crop;
    // TODO: src_crop for dump ROI, dst_crop for output w/h
    param.fence_index       = hw_outbuf->out_retire_fence_idx;
    param.secure            = isSecure(priv_handle);
    param.sequence          = job->sequence;
    param.ion_fd            = priv_handle->ion_fd;
    param.fence             = hw_outbuf->out_acquire_fence_fd;
    param.dataspace         = hw_outbuf->dataspace;
    param.fb_id             = 0;
    param.src_buf_width     = priv_handle->width;
    param.src_buf_height    = priv_handle->height;
    param.alloc_id          = priv_handle->alloc_id;
    param.queue_idx         = hw_outbuf->queue_idx;
    param.dump_point        = hw_outbuf->wdma_dump_point;

    ovl_device->setOutput(&param);
}

uint32_t DisplayDump::calculateProxHrtCost()
{
    return calculateWdmaProxCost(m_disp_id, m_format, m_src_rect, m_dst_w, m_dst_h);
}

void DisplayDump::fillWdmaConfig(Rect& out_src_rect, uint32_t& out_dst_w,
                                 uint32_t& out_dst_h, uint32_t& out_format, int& out_dump_point)
{
    out_src_rect = m_src_rect;
    out_dst_w = m_dst_w;
    out_dst_h = m_dst_h;
    out_format = m_format;
    out_dump_point = m_dump_point;
}

