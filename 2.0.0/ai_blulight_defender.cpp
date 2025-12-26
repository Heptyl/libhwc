#define DEBUG_LOG_TAG "AIBLD"

#include "ai_blulight_defender.h"

#include "dev_interface.h"
#include "dispatcher.h"
#include "overlay.h"
#include "queue.h"
#include "utils/debug.h"

#include "display_dump.h"
#include "hwc2.h"

#include <utils/String8.h>

#include <sstream>
#include <sys/stat.h>
#include <vector>

#ifdef USE_SWWATCHDOG
#include "utils/swwatchdog.h"
#endif

#define WDT_BL_NODE(fn, ...)        \
({                                  \
    ATRACE_NAME(#fn);               \
    bliter_node->fn(__VA_ARGS__);   \
})

using std::endl;

AiBluLightDefender& AiBluLightDefender::getInstance()
{
    static AiBluLightDefender gInstance;
    return gInstance;
}

AiBluLightDefender::AiBluLightDefender()
    : m_dump_point(HWC_WDMA_DUMP_POINT_OVL_OUT)
{
    memset(&m_dp_config, 0, sizeof(BufferConfig));
}

AiBluLightDefender::~AiBluLightDefender()
{
    if (m_enable)
    {
        {
            std::lock_guard<std::mutex> lock(m_thread_mutex);
            m_thread_stop = true;
            m_condition.notify_all();
        }
        m_thread.join();
    }
}

int AiBluLightDefender::setEnable(bool enable,
                                  uint32_t fps,
                                  uint32_t w,
                                  uint32_t h,
                                  uint32_t format)
{
    HWC_LOGI("%s(), enable %d, fps %d, w %d, h %d, format %d", __FUNCTION__, enable, fps, w, h, format);

    std::lock_guard<std::mutex> lock(m_mutex);

    if (enable)
    {
        bool m_enable_fail = false;

        if (!m_bliter_node)
        {
            // ai blulight defender applies only on primary, use primary mdp
            m_bliter_node = std::make_shared<BliterNode>(HWC_DISPLAY_PRIMARY);
            if (!m_bliter_node)
            {
                HWC_LOGE("m_bliter_node is nullptr");
                m_enable_fail = true;
            }
        }

        if (!m_disp_queue && !m_enable_fail)
        {
            m_disp_queue = new DisplayBufferQueue(DisplayBufferQueue::QUEUE_TYPE_AI_BLD_DISP);
            if (!m_disp_queue)
            {
                HWC_LOGE("m_disp_queue is nullptr");
                m_enable_fail = true;
            }
            else
            {
                m_disp_queue->setSynchronousMode(false);
            }
        }

        if (!m_mdp_queue && !m_enable_fail)
        {
            m_mdp_queue = new DisplayBufferQueue(DisplayBufferQueue::QUEUE_TYPE_AI_BLD_MDP);
            if (!m_mdp_queue)
            {
                HWC_LOGE("m_mdp_queue is nullptr");
                m_enable_fail = true;
            }
            else
            {
                // prevent drop buffer, since disp wdma may lag, causing not only 1 buf depth
                m_mdp_queue->setSynchronousMode(true);
            }
        }

        if (!m_fence_debugger && !m_enable_fail)
        {
            m_fence_debugger = new FenceDebugger("aibld_d_", FenceDebugger::WAIT_PERIODICALLY);
            if (m_fence_debugger == NULL)
            {
                HWC_LOGW("cannot new FenceDebugger");
            }
            else
            {
                m_fence_debugger->initialize();
            }
        }

        m_sample_period = s2ns(1) / fps;
        m_prev_sample_ts = 0;
        m_width = w;
        m_height = h;
        m_format = format;

        if (m_enable_fail)
        {
            freeResources();
            return -ENOMEM;
        }
    }
    else
    {
        freeResources();
    }

    if (m_enable != enable)
    {
        if (enable)
        {
            m_thread_stop = false;
            m_thread = std::thread(&AiBluLightDefender::threadLoop, this);
            if (pthread_setname_np(m_thread.native_handle(), "AIBLD"))
            {
                ALOGI("pthread_setname_np AIBLD fail");
            }

            DisplayManager::getInstance().refreshForDisplay(HWC_DISPLAY_PRIMARY,
                                                            HWC_REFRESH_FOR_AI_BLULIGHT_DEFENDER);
        }
        else
        {
            {
                std::lock_guard<std::mutex> lock(m_thread_mutex);
                m_thread_stop = true;
                m_condition.notify_all();
            }
            m_thread.join();
        }
    }

    HWCMediator::getInstance().getHWCDisplay(HWC_DISPLAY_PRIMARY)->setWdmaEnable(HWC_WDMA_STATUS_AIBLD, enable);

    m_enable = enable;

    return 0;
}

void AiBluLightDefender::freeResources()
{
    std::unique_lock<std::mutex> lock(m_thread_mutex);

    for (Job& job : m_job_list)
    {
        job.bliter_node->cancelJob(job.mdp_job_id);

        if (job.mdp_in_ion_fd > 0)
        {
            IONDevice::getInstance().ionClose(job.mdp_in_ion_fd);
        }

        freeDuppedBufferHandle(job.src_handle);
    }
    m_job_list.clear();

    m_bliter_node = nullptr;
    m_disp_queue = nullptr;
    m_mdp_queue = nullptr;

    m_fence_debugger = nullptr;
}

void configDisplayBufferQueue(sp<DisplayBufferQueue>& queue, BufferConfig* config,
                              const uint32_t width, const uint32_t height,
                              const uint32_t& format, const bool& assigned_compression)
{
    unsigned int target_format = config ? convertFormat4Bliter(format) : format;
    unsigned int bpp = getBitsPerPixel(target_format);

    uint32_t buffer_w = ALIGN_CEIL(width, 2);
    uint32_t buffer_h = ALIGN_CEIL(height, 2);

    DisplayBufferQueue::BufferParam buffer_param;
    buffer_param.disp_id = HWC_DISPLAY_PRIMARY;
    buffer_param.width   = buffer_w;
    buffer_param.height  = buffer_h;
    buffer_param.format  = target_format;
    buffer_param.size    = (buffer_w * buffer_h * bpp / 8);
    buffer_param.protect = false;
    buffer_param.compression = assigned_compression;
    buffer_param.dequeue_block = false;
    queue->setBufferParam(buffer_param);

    if (config)
    {
        config->dst_width = buffer_w;
        config->dst_height = buffer_h;
        config->dst_pitch_uv = 0;
        config->dst_compression = buffer_param.compression;
    }
}

int setDpConfig(PrivateHandle* priv_handle,
                const DisplayBufferQueue::DisplayBuffer* disp_buf,
                int32_t src_dataspace, int32_t dst_dataspace,
                BufferConfig* config,
                const uint32_t& assigned_output_format)
{
    const bool src_compression = isCompressData(priv_handle);

    if (config->gralloc_prexform == priv_handle->prexform &&
        config->gralloc_width  == priv_handle->width  &&
        config->gralloc_height == priv_handle->height &&
        config->gralloc_stride  == priv_handle->y_stride &&
        config->gralloc_vertical_stride == priv_handle->vstride &&
        config->gralloc_cbcr_align == priv_handle->cbcr_align &&
        config->gralloc_format == priv_handle->format &&
        config->src_compression == src_compression &&
        config->src_dataspace == src_dataspace)
    {
        // data format is not changed
        if (!config->is_valid)
        {
            HWC_LOGW("Format is not changed, but config in invalid !");
            return -EINVAL;
        }

        return 0;
    }

    HWC_LOGD("Format Change (w=%d h=%d s:%d vs:%d f=0x%x c=%d ds=0x%x) -> (w=%d h=%d s:%d vs:%d f=0x%x af=0x%x c=%d ds=0x%x)",
             config->gralloc_width, config->gralloc_height, config->gralloc_stride,
             config->gralloc_vertical_stride, config->gralloc_format, config->src_compression,
             config->src_dataspace,
             priv_handle->width, priv_handle->height, priv_handle->y_stride,
             priv_handle->vstride, priv_handle->format, assigned_output_format, src_compression,
             src_dataspace);

    // remember current buffer data format for next comparison
    config->gralloc_prexform = priv_handle->prexform;
    config->gralloc_width  = priv_handle->width;
    config->gralloc_height = priv_handle->height;
    config->gralloc_stride = priv_handle->y_stride;
    config->gralloc_cbcr_align = priv_handle->cbcr_align;
    config->gralloc_vertical_stride = priv_handle->vstride;
    config->gralloc_format = priv_handle->format;
    config->src_compression = src_compression;

    // set DpFramework configuration
//    unsigned int width  = priv_handle->width;
    unsigned int height = priv_handle->height;
    unsigned int y_stride = priv_handle->y_stride;
//    unsigned int vertical_stride = priv_handle->vstride;

    // reset uv pitch since RGB does not need it
    config->src_pitch_uv = 0;

    unsigned int input_format = priv_handle->format;

    uint32_t output_format = assigned_output_format != 0 ? assigned_output_format : input_format;
    // remember real height since height should be aligned with 32 for NV12_BLK
    config->src_width = y_stride;
    config->src_height = height;
    config->deinterlace = false;
    config->dst_pitch = (disp_buf != nullptr ? disp_buf->handle_stride * (getBitsPerPixel(disp_buf->data_format) / 8) : config->dst_pitch);
    config->dst_v_pitch = 0;
    config->dst_dataspace = dst_dataspace;

    switch (input_format)
    {
        case HAL_PIXEL_FORMAT_RGBA_8888:
            config->src_pitch = y_stride * 4;
            config->src_plane = 1;
            config->src_size[0] = config->src_pitch * height;
            config->src_dpformat = DP_COLOR_RGBA8888;
            config->dst_dpformat = mapDpFormat(convertFormat4Bliter(output_format));
            config->dst_plane = 1;
            config->dst_v_pitch = (disp_buf != nullptr ? disp_buf->data_v_pitch : config->dst_v_pitch);
            break;

        case HAL_PIXEL_FORMAT_RGBX_8888:
            config->src_pitch = y_stride * 4;
            config->src_plane = 1;
            config->src_size[0] = config->src_pitch * height;
            config->src_dpformat = DP_COLOR_RGBA8888;
            config->dst_dpformat = mapDpFormat(convertFormat4Bliter(output_format));
            config->dst_plane = 1;
            config->dst_v_pitch = (disp_buf != nullptr ? disp_buf->data_v_pitch : config->dst_v_pitch);
            break;

        case HAL_PIXEL_FORMAT_BGRA_8888:
            config->src_pitch = y_stride * 4;
            config->src_plane = 1;
            config->src_size[0] = config->src_pitch * height;
            config->src_dpformat = DP_COLOR_BGRA8888;
            config->dst_dpformat = mapDpFormat(convertFormat4Bliter(output_format));
            config->dst_plane    = 1;
            break;

        case HAL_PIXEL_FORMAT_BGRX_8888:
        case HAL_PIXEL_FORMAT_IMG1_BGRX_8888:
            config->src_pitch = y_stride * 4;
            config->src_plane = 1;
            config->src_size[0] = config->src_pitch * height;
            config->src_dpformat = DP_COLOR_BGRA8888;
            config->dst_dpformat = mapDpFormat(convertFormat4Bliter(output_format));
            config->dst_plane    = 1;
            break;

        case HAL_PIXEL_FORMAT_RGBA_1010102:
            config->src_pitch = y_stride * 4;
            config->src_plane = 1;
            config->src_size[0] = config->src_pitch * height;
            config->src_dpformat = DP_COLOR_RGBA1010102;
            config->dst_dpformat = mapDpFormat(convertFormat4Bliter(output_format));
            config->dst_plane    = 1;
            config->dst_v_pitch = (disp_buf != nullptr ? disp_buf->data_v_pitch : config->dst_v_pitch);
            break;
        case HAL_PIXEL_FORMAT_RGB_888:
            config->src_pitch = y_stride * 3;
            config->src_plane = 1;
            config->src_size[0] = config->src_pitch * height;
            config->src_dpformat = DP_COLOR_RGB888;
            config->dst_dpformat = mapDpFormat(convertFormat4Bliter(output_format));
            config->dst_plane    = 1;
            break;
        case HAL_PIXEL_FORMAT_BGR_888:
            config->src_pitch = y_stride * 3;
            config->src_plane = 1;
            config->src_size[0] = config->src_pitch * height;
            config->src_dpformat = DP_COLOR_BGR888;
            config->dst_dpformat = mapDpFormat(convertFormat4Bliter(output_format));
            config->dst_plane    = 1;
            break;
        default:
            HWC_LOGE("Color format for DP is invalid (0x%x)", input_format);
            //config->is_valid = false;
            memset(config, 0, sizeof(BufferConfig));
            return -EINVAL;
    }

    config->dst_size[0] = config->dst_pitch * config->dst_height;

    config->is_valid = true;
    return 0;
}

void AiBluLightDefender::onSetJob(DispatcherJob* job,
                                  const sp<OverlayEngine>& ovl_device,
                                  const sp<HWCDisplay>& display)
{
    const nsecs_t cur_time = systemTime();

    sp<FenceDebugger> fence_debugger;
    sp<DisplayBufferQueue> mdp_queue;
    sp<DisplayBufferQueue> disp_queue;
    std::shared_ptr<BliterNode> bliter_node;
    uint32_t pf_fence_idx;

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_enable)
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

        if (cur_time - m_prev_sample_ts < m_sample_period)
        {
            return;
        }
        // TODO: if no screen update, skip
        // TODO: secure?

        bliter_node = m_bliter_node;
        disp_queue = m_disp_queue;
        mdp_queue = m_mdp_queue;
        fence_debugger = m_fence_debugger;
        job->aibld_enable = true;
        pf_fence_idx = job->hw_outbuf.phy_present_fence_idx;
    }

    ATRACE_NAME(__FUNCTION__);

    // config buffer queue
    uint32_t disp_w = static_cast<uint32_t>(display->getWidth(job->active_config));
    uint32_t disp_h = static_cast<uint32_t>(display->getHeight(job->active_config));
    configDisplayBufferQueue(disp_queue, nullptr,
                             disp_w,
                             disp_h,
                             m_format,
                             false);

    DisplayBufferQueue::DisplayBuffer disp_buffer;
    status_t err = disp_queue->dequeueBuffer(&disp_buffer, true, false);
    if (err != NO_ERROR)
    {
        HWC_LOGE("%s(): dequeueBuffer() goes wrong err=%d", __func__, err);
        DisplayManager::getInstance().refreshForDisplay(job->disp_ori_id,
                                                        HWC_REFRESH_FOR_AI_BLULIGHT_DEFENDER_AGAIN);
        return;
    }

    // fill hw buffer
    buffer_handle_t outbuf_hnd = nullptr;
    dupBufferHandle(disp_buffer.out_handle, &outbuf_hnd);

    HWBuffer* hw_outbuf = &job->hw_outbuf;
    PrivateHandle* priv_handle = &hw_outbuf->priv_handle;
    priv_handle->ion_fd = HWC_NO_ION_FD;

    err = getPrivateHandle(outbuf_hnd, priv_handle, nullptr, true);
    priv_handle->format = m_format; // TODO: since we cannot alloc BGR888 now, we allocate as RGB888, and change fmt here
    err |= getAllocId(outbuf_hnd, priv_handle);
    if (err != NO_ERROR)
    {
        HWC_LOGE("Failed to get private handle !! (outbuf=%p) !!", outbuf_hnd);
        freeDuppedBufferHandle(outbuf_hnd);
        disp_queue->cancelBuffer(disp_buffer.index);
        return;
    }

    // get out fence
    OverlayPrepareParam prepare_param;
    prepare_param.ion_fd = priv_handle->ion_fd;
    prepare_param.is_need_flush = 0;
    prepare_param.blending = HWC2_BLEND_MODE_NONE;
    prepare_param.drm_id_crtc = job->drm_id_cur_crtc;

    err = ovl_device->prepareOutput(prepare_param);
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

    hw_outbuf->out_acquire_fence_fd = disp_buffer.release_fence;
    hw_outbuf->out_retire_fence_idx = prepare_param.fence_index;
    hw_outbuf->handle = outbuf_hnd;
    hw_outbuf->queue_idx = disp_buffer.index;
    hw_outbuf->wdma_dump_point = m_dump_point;

    if (fence_debugger)
    {
        fence_debugger->dupAndStoreFence(prepare_param.fence_fd, prepare_param.fence_index);
    }

    // queue to bq
    passFenceFd(&disp_buffer.acquire_fence, &prepare_param.fence_fd);

    disp_buffer.alpha_enable = false;                               // TODO: are these any usage?
    disp_buffer.alpha = 0xff;
    disp_buffer.blending = prepare_param.blending;
    disp_buffer.sequence = job->sequence;

    disp_queue->queueBuffer(&disp_buffer);

    // acquire for mdp
    DisplayBufferQueue::DisplayBuffer* acqBuffer = disp_queue->getLastAcquiredBufEditable();
    status_t acq_status = disp_queue->acquireBuffer(acqBuffer, true);
    if (acq_status != NO_ERROR)
    {
        HWC_LOGE("Failed to acquireBuffer");
        freeDuppedBufferHandle(outbuf_hnd);
        return;
    }

    int disp_dbq_rel_fence = -1;

    // set disp wdma out buf to mdp input
    const uint32_t mdp_output_format = m_format;
    configDisplayBufferQueue(mdp_queue, &m_dp_config, m_width, m_height,
                             mdp_output_format, false);

    DisplayBufferQueue::DisplayBuffer mdp_buffer;
    err = mdp_queue->dequeueBuffer(&mdp_buffer, true, false);
    if (err != NO_ERROR)
    {
        HWC_LOGE("%s(), dequeueBuffer() goes wrong err=%d", __func__, err);

        freeDuppedBufferHandle(outbuf_hnd);
        if (disp_queue->releaseBuffer(acqBuffer->index, -1) != NO_ERROR)
        {
            HWC_LOGE("%s(), releaseBuffer to disp_queue fail", __FUNCTION__);
        }
        return;
    }

    int release_fence_fd = -1;
    uint32_t ai_bld_mdp_job_id = 0;
    WDT_BL_NODE(createJob, ai_bld_mdp_job_id, release_fence_fd,
                BliterNode::HWC_2D_BLITER_PROCESSER_MDP);

    disp_dbq_rel_fence = dup(release_fence_fd);

    if (priv_handle->ion_fd > 0)
    {
        IONDevice::getInstance().ionImport(&priv_handle->ion_fd);       // TODO: can be remove?
    }

    int32_t dataspace = hw_outbuf->dataspace;
    if (dataspace == HAL_DATASPACE_UNKNOWN)
    {
        // use BT601 NARROW as default
        dataspace = HAL_DATASPACE_V0_BT601_625;
    }

    err = setDpConfig(priv_handle, &mdp_buffer,
                      dataspace, dataspace,
                      &m_dp_config, mdp_output_format);
    if (err != NO_ERROR)
    {
        HWC_LOGE("%s(): setDpConfig() goes wrong err=%d", __func__, err);
    }

    if (err == NO_ERROR)
    {
        Rect src_roi = Rect(disp_w, disp_h);
        Rect dst_roi = Rect(m_width, m_height);
        uint32_t xform = 0;
        uint32_t pq_enhance = 0;

        BliterNode::Parameter param = {src_roi, dst_roi, &m_dp_config, xform, pq_enhance, mdp_buffer.secure};
        Rect mdp_cal_dst_crop;

        WDT_BL_NODE(setSrc, ai_bld_mdp_job_id, &m_dp_config, *priv_handle, &acqBuffer->acquire_fence,
                    std::vector<int32_t>(), std::vector<float>(), std::vector<uint8_t>(),
                    HWC_LAYER_HDR_NONE, false, job->pq_mode_id);
        WDT_BL_NODE(setDst, ai_bld_mdp_job_id, &param, mdp_buffer.out_ion_fd,
                    mdp_buffer.out_sec_handle,
                    &mdp_buffer.release_fence);
        WDT_BL_NODE(calculateAllROI, ai_bld_mdp_job_id, &mdp_cal_dst_crop);

        passFenceFd(&mdp_buffer.acquire_fence, &release_fence_fd);

        mdp_buffer.data_info.src_crop   = mdp_cal_dst_crop;
        mdp_buffer.data_info.dst_crop.left   = 0;
        mdp_buffer.data_info.dst_crop.top    = 0;
        mdp_buffer.data_info.dst_crop.right  = static_cast<int32_t>(m_width);
        mdp_buffer.data_info.dst_crop.bottom = static_cast<int32_t>(m_height);

        mdp_buffer.src_handle           = priv_handle->handle;
        mdp_buffer.dataspace            = m_dp_config.dst_dataspace;
        mdp_buffer.sequence = job->sequence;

        mdp_queue->queueBuffer(&mdp_buffer);

        // set to job thread
        std::lock_guard<std::mutex> l(m_thread_mutex);
        Job aibld_job{.mdp_job_id = ai_bld_mdp_job_id,
                      .queue = mdp_queue,
                      .bliter_node = bliter_node,
                      .mdp_in_ion_fd = priv_handle->ion_fd,
                      .src_handle = outbuf_hnd,
                      .pf_fence_idx = pf_fence_idx,
                      .dump_enable = m_dump_enable};

        if (m_dump_enable)
        {
            aibld_job.mdp_in_priv_handle = *priv_handle;
        }
        m_job_list.push_back(aibld_job);

        HWC_LOGD("add job, mdp job id %d, prepare_param.fence_index %d",
                 aibld_job.mdp_job_id, prepare_param.fence_index);

        m_condition.notify_all();
    }
    else
    {
        WDT_BL_NODE(cancelJob, ai_bld_mdp_job_id);
        freeDuppedBufferHandle(outbuf_hnd);
        mdp_queue->cancelBuffer(mdp_buffer.index);
        protectedClose(disp_dbq_rel_fence);

        if (priv_handle->ion_fd > 0)
        {
            IONDevice::getInstance().ionClose(priv_handle->ion_fd);
        }

        HWC_LOGE("something wrong, cancel mdp job ...");
    }

    // release back to disp dbq
    if (disp_queue->releaseBuffer(acqBuffer->index, disp_dbq_rel_fence) != NO_ERROR)
    {
        if (disp_dbq_rel_fence >= 0)
        {
            ::protectedClose(disp_dbq_rel_fence);
        }
    }

    m_prev_sample_ts = cur_time;
}

void AiBluLightDefender::onProcess(DispatcherJob* job,
                                   const sp<OverlayEngine>& ovl_device)
{
    if (!job->aibld_enable)
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
    param.src_crop          = Rect(priv_handle->width, priv_handle->height);
    param.dst_crop          = param.src_crop;
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

void AiBluLightDefender::dump(String8* dump_str) const
{
    std::ostringstream ss;
    ss << "AiBluLightDefender:" << endl;
/*    ss << "id: " << m_model.agent_id << endl;
    ss << "format: in " << m_model.in_format << " out " << m_model.out_format << endl;
    ss << "compress: in " << m_model.in_compress << " out " << m_model.out_compress << endl;
    ss << "in: w " << m_model.in_w << " h " << m_model.in_h << " stride " << m_model.in_stride << endl;
    ss << "out: w " << m_model.out_w << " h " << m_model.out_h << " stride " << m_model.out_stride << endl;*/
    ss << endl;

    if (dump_str)
    {
        dump_str->append(ss.str().c_str());
    }
    else
    {
        HWC_LOGI("%s", ss.str().c_str());
    }
}

void AiBluLightDefender::threadLoop()
{
    while (true)
    {
        Job job;
        {
            ATRACE_NAME("wait_job");
            std::unique_lock<std::mutex> lock(m_thread_mutex);

            if (m_job_list.empty())
            {
                if (m_thread_stop)
                {
                    break;
                }

                m_condition.wait(lock);
                continue;
            }

            job = m_job_list.front();
            m_job_list.pop_front();
        }

        ATRACE_NAME(__FUNCTION__);
        HWC_LOGD("do mdp job %d", job.mdp_job_id);
        job.bliter_node->invalidate(job.mdp_job_id);

        // acquire and set to pq
        DisplayBufferQueue::DisplayBuffer* buffer = job.queue->getLastAcquiredBufEditable();
        status_t acq_status = job.queue->acquireBuffer(buffer, true);
        if (acq_status == NO_ERROR)
        {
            SyncFence::waitPeriodicallyWoCloseFd(buffer->acquire_fence, 100, 1000,
                                                 (std::string("aibld_") + std::to_string(job.mdp_job_id)).c_str());
            protectedClose(buffer->acquire_fence);
            buffer->acquire_fence = -1;

            if (job.dump_enable)
            {
                // dump buffer
                String8 path;
                path.appendFormat("/data/SF_dump/%05d_%d_%u_%c", job.mdp_job_id, 0, 0, 'U');

                dump_buf(m_format, false, 0, buffer->out_ion_fd,
                         m_width, m_height, buffer->data_pitch, buffer->data_v_pitch,
                         buffer->buffer_size, Rect(m_width, m_height), 1, path.string(),
                         false);
            }

            // dump wdma out buf
            if (job.dump_enable)
            {
                // dump buffer
                String8 path;
                path.appendFormat("/data/SF_dump/%05d_%d_%u_%c", job.mdp_job_id, 0, 0, 'U');

                dump_buf(job.mdp_in_priv_handle.format, false, 0, job.mdp_in_ion_fd,
                         job.mdp_in_priv_handle.width, job.mdp_in_priv_handle.height,
                         job.mdp_in_priv_handle.y_stride, job.mdp_in_priv_handle.vstride,
                         job.mdp_in_priv_handle.size,
                         Rect(job.mdp_in_priv_handle.width, job.mdp_in_priv_handle.height),
                         1, path.string(),
                         false);
            }

            // set to pq
            getPqDevice()->setAiBldBuffer(buffer->out_handle, job.pf_fence_idx);

            // release
            if (job.queue->releaseBuffer(buffer->index, -1) != NO_ERROR)
            {
                HWC_LOGW("%s(), releaseBuffer failed, idx %d", __FUNCTION__, buffer->index);
            }
        }

        if (job.mdp_in_ion_fd > 0)
        {
            IONDevice::getInstance().ionClose(job.mdp_in_ion_fd);
        }

        freeDuppedBufferHandle(job.src_handle);
    }
}

uint32_t AiBluLightDefender::calculateProxHrtCost(const sp<HWCDisplay>& display)
{
    uint32_t disp_w = static_cast<uint32_t>(display->getWidth(display->getActiveConfig()));
    uint32_t disp_h = static_cast<uint32_t>(display->getHeight(display->getActiveConfig()));

    return calculateWdmaProxCost(HWC_DISPLAY_PRIMARY, m_format, Rect(disp_w, disp_h), disp_w, disp_h);
}

void AiBluLightDefender::fillWdmaConfig(const sp<HWCDisplay>& display, DispatcherJob* job,
                                        Rect&  out_src_rect, uint32_t& out_dst_w,
                                        uint32_t& out_dst_h, uint32_t& out_format, int& out_dump_point)
{
    uint32_t disp_w = static_cast<uint32_t>(display->getWidth(job->active_config));
    uint32_t disp_h = static_cast<uint32_t>(display->getHeight(job->active_config));

    out_src_rect = Rect(disp_w, disp_h);
    out_dst_w = disp_w;
    out_dst_h = disp_h;
    out_format = m_format;
    out_dump_point = m_dump_point;
}

