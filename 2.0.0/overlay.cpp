#define DEBUG_LOG_TAG "OVL"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include <stdlib.h>
#include <string>

#include "utils/debug.h"
#include "utils/tools.h"
#include "utils/mm_buf_dump.h"

#include "hwc2_api.h"
#include "hwc2_defs.h"
#include "overlay.h"
#include "dispatcher.h"
#include "display.h"
#include "queue.h"
#include "composer.h"
#include "sync.h"
#include "platform_wrap.h"
#include "index_buffer_generator.h"


#define OLOGV(x, ...) HWC_LOGV("(%" PRIu64 ") " x, m_disp_id, ##__VA_ARGS__)
#define OLOGD(x, ...) HWC_LOGD("(%" PRIu64 ") " x, m_disp_id, ##__VA_ARGS__)
#define OLOGI(x, ...) HWC_LOGI("(%" PRIu64 ") " x, m_disp_id, ##__VA_ARGS__)
#define OLOGW(x, ...) HWC_LOGW("(%" PRIu64 ") " x, m_disp_id, ##__VA_ARGS__)
#define OLOGE(x, ...) HWC_LOGE("(%" PRIu64 ") " x, m_disp_id, ##__VA_ARGS__)

// ---------------------------------------------------------------------------

OverlayEngine::OverlayInput::OverlayInput()
{ }

OverlayEngine::OverlayOutput::OverlayOutput()
    : connected_state(OVL_PORT_DISABLE)
    , queue(NULL)
{ }

OverlayEngine::OverlayEngine(const uint64_t& dpy, uint32_t drm_id_crtc, uint32_t drm_id_connector,
                             uint32_t width, uint32_t height)
    : m_disp_id(dpy)
    , m_drm_id_cur_crtc(drm_id_crtc)
    , m_handling_job(NULL)
    , m_sync_fence(new SyncFence(dpy))
    , m_need_wakeup(false)
    , m_prev_power_mode(0)
    , m_power_mode_changed(0)
    , m_hdmirx_black_screen_countdown(0)
{
    // create overlay session
    status_t err = HWCMediator::getInstance().getOvlDevice(m_disp_id)->createOverlaySession(m_disp_id,
                                                                                            m_drm_id_cur_crtc,
                                                                                            width, height);

    if (err != NO_ERROR)
    {
        m_engine_state = OVL_ENGINE_DISABLED;
        m_max_inputs = 0;

        OLOGE("Failed to create display session");
    }
    else
    {
        m_engine_state = OVL_ENGINE_ENABLED;
        m_max_inputs = HWCMediator::getInstance().getOvlDevice(m_disp_id)->getMaxOverlayInputNum();

        for (unsigned int id = 0; id < m_max_inputs; id++)
        {
            // init input configurations
            OverlayInput* input = new OverlayInput();
            m_inputs.add(input);
            m_input_params.add(&input->param);
        }

        // initialize display data for physical display
        if (m_disp_id != HWC_DISPLAY_VIRTUAL)
            DisplayManager::getInstance().setDisplayDataForPhy(dpy, m_drm_id_cur_crtc, drm_id_connector);
    }

    m_thread_name = std::string("OverlayEngine_") + std::to_string(dpy);
    m_queue_name = std::string("FrameQueue_") + std::to_string(dpy);
    m_semaphore_name = std::string("Semaphore_of_OverlayEngine_") + std::to_string(dpy);

    std::string pool_name(m_thread_name);
    pool_name += "_FrameInfo";
    m_pool = new ObjectPool<FrameInfo>(pool_name, 5);

    m_trace_delay_name = std::string("present_after_ts_") + std::to_string(dpy);
    m_trace_delay_counter = 0;
    m_trace_decoulpe_delay_name = std::string("decoulpe_delay_") + std::to_string(dpy);
    m_trace_decoulpe_delay_ns_name = std::string("decoulpe_delay_ns") + std::to_string(dpy);

    m_perf_remain_time_str = std::string("po_remain_time_") + std::to_string(dpy);
    m_perf_target_cpu_mhz_str = std::string("po_target_cpu_mhz_") + std::to_string(dpy);
    m_perf_uclamp_str = std::string("po_uclamp_") + std::to_string(dpy);
    m_perf_extension_time_str = std::string("po_extension_time_") + std::to_string(dpy);
}

OverlayEngine::~OverlayEngine()
{
    size_t size = m_frame_queue.size();
    for (size_t i = 0; i < size; i++)
    {
        closeAllFenceFd(m_frame_queue[i]);
        closeAllIonFd(m_frame_queue[i]);
    }
    if (m_last_frame_info != nullptr)
    {
        closeAllIonFd(m_last_frame_info);
    }
    m_last_frame_info = nullptr;
    m_frame_queue.clear();
    delete m_pool;

    for (unsigned int id = 0; id < m_max_inputs; id++)
    {
        delete m_inputs[id];
    }
    m_inputs.clear();
    m_input_params.clear();

    m_output.queue = NULL;

    HWCMediator::getInstance().getOvlDevice(m_disp_id)->destroyOverlaySession(m_disp_id, m_drm_id_cur_crtc);

    DataExpress::getInstance().deletePackageAll(m_disp_id);
}

unsigned int OverlayEngine::getAvailableInputNum()
{
    SessionInfo info;
    unsigned int avail_inputs;

    avail_inputs = HWCMediator::getInstance().getOvlDevice(m_disp_id)->getAvailableOverlayInput(m_disp_id,
                                                                                                m_drm_id_cur_crtc);

    // only main display need to check if fake display exists
    if (HWC_DISPLAY_PRIMARY == m_disp_id)
    {
        unsigned int fake_num = DisplayManager::getInstance().getFakeDispNum();
        avail_inputs = (avail_inputs > fake_num) ? (avail_inputs - fake_num) : avail_inputs;
    }

    return avail_inputs;
}

bool OverlayEngine::waitUntilAvailable()
{
#ifndef MTK_USER_BUILD
    char atrace_tag[128];
    if (snprintf(atrace_tag, sizeof(atrace_tag), "wait_ovl_avail(%" PRIu64 ")", m_disp_id) > 0)
    {
        HWC_ATRACE_NAME(atrace_tag);
    }
#endif

    unsigned int avail_inputs;
    int try_count = 0;

    // TODO: use synchronous ioctl instead of busy-waiting
    do
    {
        avail_inputs = getAvailableInputNum();

        if (avail_inputs != 0) break;

        try_count++;

        OLOGW("Waiting for available OVL (cnt=%d)", try_count);

        usleep(5000);
    } while (try_count < 1000);

    if (avail_inputs == 0)
    {
        OLOGE("Timed out waiting for OVL (cnt=%d)", try_count);
        return false;
    }

    return true;
}

status_t OverlayEngine::prepareInput(OverlayPrepareParam& param)
{
    AutoMutex l(m_lock);

    unsigned int id = param.id;

    if (id >= m_max_inputs)
    {
        OLOGE("Failed to prepare invalid overlay input(%u)", id);
        return BAD_INDEX;
    }

    HWCMediator::getInstance().getOvlDevice(m_disp_id)->prepareOverlayInput(m_disp_id, &param);

    return NO_ERROR;
}

status_t OverlayEngine::disableInput(unsigned int id)
{
    AutoMutex l(m_lock);

    if (id >= m_max_inputs)
    {
        OLOGE("Failed to disable invalid overlay input(%u)", id);
        return BAD_INDEX;
    }

    disableInputLocked(id);

    return NO_ERROR;
}

status_t OverlayEngine::disableOutput()
{
    AutoMutex l(m_lock);

    disableOutputLocked();

    return NO_ERROR;
}

void OverlayEngine::disableInputLocked(unsigned int id)
{
    // set overlay params
    m_input_params[id]->state = OVL_IN_PARAM_DISABLE;
    m_input_params[id]->sequence = HWC_SEQUENCE_INVALID;
    m_input_params[id]->ion_fd = -1;
    m_input_params[id]->is_mml = false;
}

void OverlayEngine::disableOutputLocked()
{
    // clear output infomation
    m_output.connected_state = OVL_PORT_DISABLE;
    memset(&m_output.param, 0, sizeof(OverlayPortParam));
    m_output.param.ion_fd = -1;
    m_output.param.fence = -1;
    m_output.param.mir_rel_fence_fd = -1;
    m_output.param.queue_idx = -1;
    m_output.param.dump_point = -1;
}

status_t OverlayEngine::prepareOutput(OverlayPrepareParam& param)
{
    AutoMutex l(m_lock);

    param.id = m_max_inputs;
    HWCMediator::getInstance().getOvlDevice(m_disp_id)->prepareOverlayOutput(m_disp_id, &param);

    return NO_ERROR;
}

status_t OverlayEngine::setOutput(OverlayPortParam* param, bool need_output_buffer)
{
    AutoMutex l(m_lock);

    if (CC_UNLIKELY(param == NULL))
    {
        OLOGE("HWC->OVL: output param is NULL, disable output");
        disableOutputLocked();
        return INVALID_OPERATION;
    }

    if (need_output_buffer && (m_output.queue != NULL))
    {
        DisplayBufferQueue::DisplayBuffer mir_buffer;
        m_output.queue->acquireBuffer(&mir_buffer, true);
        if (m_output.queue->releaseBuffer(mir_buffer.index, param->mir_rel_fence_fd) != NO_ERROR)
        {
            OLOGW("%s(), releaseBuffer failed, idx %d", __FUNCTION__, mir_buffer.index);
            ::protectedClose(param->mir_rel_fence_fd);
        }
        param->queue_idx = mir_buffer.index;
        m_cond.signal();
    }
    else if (param->queue_idx < 0)
    {
        param->queue_idx = (param->fence_index > 0) ? param->fence_index % NUM_DECOUPLE_FB_ID_BACKUP_SLOTS : -1;
    }

    m_output.connected_state = OVL_PORT_ENABLE;

    memcpy(&m_output.param, param, sizeof(OverlayPortParam));

    return NO_ERROR;
}

status_t OverlayEngine::preparePresentFence(OverlayPrepareParam& param)
{
    AutoMutex l(m_lock);

    if (m_disp_id != HWC_DISPLAY_VIRTUAL)
    {
        HWCMediator::getInstance().getOvlDevice(m_disp_id)->prepareOverlayPresentFence(m_disp_id, &param);
    }
    else
    {
        param.fence_fd = -1;
        param.fence_index = 0;
    }

    return NO_ERROR;
}

status_t OverlayEngine::createOutputQueue(unsigned int format, bool secure)
{
    AutoMutex l(m_lock);
    return createOutputQueueLocked(format, secure);
}

status_t OverlayEngine::createOutputQueueLocked(unsigned int format, bool secure)
{
#ifndef MTK_USER_BUILD
    char atrace_tag[128];
    if (snprintf(atrace_tag, sizeof(atrace_tag), "create_out_queue(%" PRIu64 ")", m_disp_id) > 0)
    {
        HWC_ATRACE_NAME(atrace_tag);
    }
#endif

    bool need_init = false;

    // verify if need to create output queue
    if (m_output.queue == NULL)
    {
        need_init = true;

        m_output.queue = new DisplayBufferQueue(DisplayBufferQueue::QUEUE_TYPE_OVL);
        m_output.queue->setSynchronousMode(true);

        OLOGD("Create output queue");
    }

    unsigned int bpp = getBitsPerPixel(format);

    const DisplayData* disp_data = DisplayManager::getInstance().getDisplayData(m_disp_id);
    DisplayBufferQueue::BufferParam buffer_param;
    buffer_param.width  = static_cast<unsigned int>(disp_data->width);
    buffer_param.height = static_cast<unsigned int>(disp_data->height);
    buffer_param.format = mapGrallocFormat(format);
    buffer_param.size   = static_cast<unsigned int>(disp_data->width * disp_data->height) * bpp / 8;
    buffer_param.dequeue_block = false;
    m_output.queue->setBufferParam(buffer_param);

    if (need_init)
    {
        // allocate buffers
        const size_t buffer_slots = DisplayBufferQueue::NUM_BUFFER_SLOTS;
        DisplayBufferQueue::DisplayBuffer mir_buffer[buffer_slots];
        for (size_t i = 0; i < buffer_slots; i++)
        {
            m_output.queue->dequeueBuffer(&mir_buffer[i], false, secure);
        }

        for (size_t i = 0; i < buffer_slots; i++)
        {
            m_output.queue->cancelBuffer(mir_buffer[i].index);
        }

        OLOGD("Initialize buffers for output queue");
    }

    return NO_ERROR;
}

status_t OverlayEngine::releaseOutputQueue()
{
    AutoMutex l(m_lock);

    m_output.connected_state = OVL_PORT_DISABLE;
    m_output.queue = NULL;

    OLOGD("Output buffer queue is relased");

    return NO_ERROR;
}

status_t OverlayEngine::configMirrorOutput(DispatcherJob* job)
{
#ifndef MTK_USER_BUILD
    char atrace_tag[128];
    if (snprintf(atrace_tag, sizeof(atrace_tag),"set_mirror(%" PRIu64 ")", m_disp_id) > 0)
    {
        HWC_ATRACE_NAME(atrace_tag);
    }
#endif

    AutoMutex l(m_lock);

    HWBuffer* outbuf = &job->hw_outbuf;

    // if virtial display is used as mirror source
    // no need to use extra buffer since it already has its own output buffer
    if ((HWC_DISPLAY_VIRTUAL == m_disp_id) || (outbuf == NULL))
        return NO_ERROR;

    // it can happen if SurfaceFlinger tries to access output buffer queue
    // right after hotplug thread just released it (e.g. via onPlugOut())
    if (CC_UNLIKELY(m_output.queue == NULL))
    {
        OLOGW("output buffer queue has been released");
        return INVALID_OPERATION;
    }

    // prepare overlay output buffer
    DisplayBufferQueue::DisplayBuffer out_buffer;
    unsigned int acq_fence_idx = 0;
    int if_fence_fd = -1;
    unsigned int if_fence_idx = 0;
    {
        status_t err;
        m_output.queue->updateBufferSize(static_cast<unsigned int>(job->disp_data->width),
                                         static_cast<unsigned int>(job->disp_data->height));

        do
        {
            err = m_output.queue->dequeueBuffer(&out_buffer, true, job->secure);
            if (NO_ERROR != err)
            {
                OLOGW("cannot find available buffer, wait...");
                m_cond.wait(m_lock);
                OLOGW("wake up to find available buffer");
            }
        } while (NO_ERROR != err);

        OverlayPrepareParam prepare_param;
        prepare_param.id            = m_max_inputs;
        prepare_param.ion_fd        = out_buffer.out_ion_fd;
        prepare_param.is_need_flush = 0;

        prepare_param.blending = HWC2_BLEND_MODE_NONE;

        HWCMediator::getInstance().getOvlDevice(m_disp_id)->prepareOverlayOutput(m_disp_id, &prepare_param);
        if (prepare_param.fence_fd <= 0)
        {
            OLOGW("Failed to get mirror acquireFence !!");
        }

        out_buffer.acquire_fence = prepare_param.fence_fd;
        acq_fence_idx            = prepare_param.fence_index;
        if_fence_fd              = prepare_param.if_fence_fd;
        if_fence_idx             = prepare_param.if_fence_index;

        m_output.queue->queueBuffer(&out_buffer);
    }

    // fill mirror output buffer info
    outbuf->mir_out_sec_handle    = out_buffer.out_sec_handle;
    outbuf->mir_out_rel_fence_fd  = out_buffer.release_fence;
    outbuf->mir_out_acq_fence_fd  = out_buffer.acquire_fence;
    outbuf->mir_out_acq_fence_idx = acq_fence_idx;
    outbuf->mir_out_if_fence_fd   = if_fence_fd;
    outbuf->mir_out_if_fence_idx  = if_fence_idx;
    outbuf->handle                = out_buffer.out_handle;
    int32_t err = getPrivateHandle(outbuf->handle, &outbuf->priv_handle);
    err |= getAllocId(outbuf->handle, &outbuf->priv_handle);
    if (err != NO_ERROR)
    {
        OLOGE("%s: Failed to get private handle of outbuf(%d)!", __func__, err);
    }

    OLOGV("%s if_fence_fd:%d", __func__, if_fence_fd);
    if (DisplayManager::m_profile_level & PROFILE_TRIG)
    {
        OLOGI("HWC->OVL: config output (rel_fd=%d acq_fd=%d/idx=%u)",
            out_buffer.release_fence, out_buffer.acquire_fence, acq_fence_idx);
    }

    return NO_ERROR;
}

status_t OverlayEngine::setOverlaySessionMode(HWC_DISP_MODE mode)
{
    return HWCMediator::getInstance().getOvlDevice(m_disp_id)->setOverlaySessionMode(m_disp_id, mode);
}

HWC_DISP_MODE OverlayEngine::getOverlaySessionMode()
{
    return HWCMediator::getInstance().getOvlDevice(m_disp_id)->getOverlaySessionMode(m_disp_id, m_drm_id_cur_crtc);
}

void OverlayEngine::trigger(const unsigned int& present_fence_idx,
                            const int& prev_present_fence,
                            const int& pq_fence_fd)
{
    sp<FrameInfo> frame_info = m_pool->getFreeObject();
    if (frame_info)
    {
        AutoMutex l(m_lock);

        // start to duplicate the parameters of this frame
        packageFrameInfo(frame_info, present_fence_idx,
                         prev_present_fence, pq_fence_fd);

        m_frame_queue.add(frame_info);
        int32_t num_of_frame = static_cast<int32_t>(m_frame_queue.size());
        HWC_ATRACE_INT(m_queue_name.c_str(), num_of_frame);
        m_state = HWC_THREAD_TRIGGER;
        sem_post(&m_event);
        if (Platform::getInstance().m_config.dbg_switch & HWC_DBG_SWITCH_DEBUG_SEMAPHORE)
        {
            int32_t sem_value;
            sem_getvalue(&m_event, &sem_value);
            HWC_ATRACE_INT(m_semaphore_name.c_str(), sem_value);
            // sem_value should not less then num_of_frame minus 1
            // (immediately decrease by sem_wait before check, but not consume frame queue yet)
            if (sem_value < (num_of_frame - 1) ||
                sem_value > num_of_frame)
            {
                if (isUserLoad())
                {
                    HWC_LOGE("(%" PRIu64 ") OverlayEngine trigger when sem_value(%d) do not match num_of_frame(%d)", m_disp_id, sem_value, num_of_frame);
                }
                else
                {
                    LOG_FATAL("(%" PRIu64 ") OverlayEngine trigger when sem_value(%d) do not match num_of_frame(%d)", m_disp_id, sem_value, num_of_frame);
                }
            }
        }
    }
    else
    {
        LOG_FATAL("%s(), frame_info == nullptr", __FUNCTION__);
    }
}

status_t OverlayEngine::loopHandler(sp<FrameInfo>& info)
{
    if (m_disp_id == HWC_DISPLAY_VIRTUAL)
    {
        if (info->overlay_info.enable_output == false)
        {
            if (DisplayManager::m_profile_level & PROFILE_TRIG)
            {
                char atrace_tag[128];
                if (snprintf(atrace_tag, sizeof(atrace_tag), "trig_ovl(%" PRIu64 "): fail w/o output", m_disp_id) > 0)
                {
                    HWC_ATRACE_NAME(atrace_tag);
                }
            }

            OLOGE("Try to trigger w/o set output port !!");
            return -EINVAL;
        }
    }

    status_t err;

    int fence_idx = info->present_fence_idx;
    int ovlp_layer_num = info->ovlp_layer_num;
    const uint32_t& hrt_weight = info->hrt_weight;
    const uint32_t& hrt_idx = info->hrt_idx;
    hwc2_config_t config = info->active_config;
    int prev_present_fence = info->prev_present_fence;

    {
        DbgLogger logger(DbgLogger::TYPE_HWC_LOG, 'D', nullptr);
        logger.printf("(%" PRIu64 ") triggerOverlaySession ovlp:%d idx:%d prev_present_fence:%d",
            m_disp_id, ovlp_layer_num, fence_idx, prev_present_fence);
    }

    if (DisplayManager::m_profile_level & PROFILE_TRIG)
    {
        char atrace_tag[128];
        if (snprintf(atrace_tag, sizeof(atrace_tag), "trig_ovl(%" PRIu64 ")", m_disp_id) > 0)
        {
            HWC_ATRACE_NAME(atrace_tag);
        }
        OLOGV("HWC->OVL: trig");
    }

    err = HWCMediator::getInstance().getOvlDevice(m_disp_id)->triggerOverlaySession(
        m_disp_id, info->drm_id_crtc, fence_idx, ovlp_layer_num, prev_present_fence, config, hrt_weight, hrt_idx,
        info->overlay_info.num_layers, info->overlay_info.input.array(), info->overlay_info.color_transform,
        {
#ifdef MTK_IN_DISPLAY_FINGERPRINT
            .is_HBM = info->is_HBM,
#endif
#ifdef MTK_HDR_SET_DISPLAY_COLOR
            .is_HDR = info->is_HDR,
#endif
            .ovl_seq = info->frame_seq,
            .package = info->package,
            .late_package = info->late_package,
        }
        );

    return err;
}

OverlayPortParam* const* OverlayEngine::getInputParams()
{
    return m_input_params.array();
}

void OverlayEngine::setPowerMode(uint32_t drm_id_crtc, int mode)
{
    AutoMutex l(m_lock);

    switch (mode)
    {
        case HWC2_POWER_MODE_OFF:
            {
                m_engine_state = OVL_ENGINE_PAUSED;
                unsigned int num = HWCMediator::getInstance().getOvlDevice(m_disp_id)->getAvailableOverlayInput(m_disp_id,
                                                                                                                drm_id_crtc);

                if (m_disp_id != HWC_DISPLAY_VIRTUAL)
                {
                    HWCMediator::getInstance().getOvlDevice(m_disp_id)->disableOverlaySession(
                        m_disp_id, drm_id_crtc, m_input_params.array(), num);
                }

                for (unsigned int id = 0; id < m_max_inputs; id++)
                {
                    if (m_inputs[id]->param.state == OVL_IN_PARAM_ENABLE)
                        disableInputLocked(id);
                }
            }
            break;

        case HWC2_POWER_MODE_DOZE:
        case HWC2_POWER_MODE_ON:
            m_engine_state = OVL_ENGINE_ENABLED;
            break;

        case HWC2_POWER_MODE_DOZE_SUSPEND:
            m_engine_state = OVL_ENGINE_PAUSED;
            break;
    }

    if (m_disp_id != HWC_DISPLAY_VIRTUAL)
    {
        HWCMediator::getInstance().getOvlDevice(m_disp_id)->setPowerMode(m_disp_id, drm_id_crtc, mode);
    }

    if (m_prev_power_mode != mode)
        m_power_mode_changed = POWER_MODE_CHANGED_DO_VALIDATE_NUM;

    m_prev_power_mode = mode;
}

static char getIdentitySymbol(const int32_t& layer_identity)
{
    char ret = '?';
    switch (layer_identity)
    {
        case HWLAYER_ID_DBQ:
            ret = 'M';
            break;

        case HWC_LAYER_TYPE_UI:
            ret = 'U';
            break;

        case HWC_LAYER_TYPE_FBT:
            ret = 'C';
            break;

        default:
            ret = '?';
            break;
    }
    return ret;
}

void OverlayEngine::dump(String8* dump_str)
{
    AutoMutex l(m_lock_dump);

    if (Platform::getInstance().m_config.dump_buf)
    {
        waitAllFence(m_last_frame_info);
        const unsigned int downsample = static_cast<unsigned int>(Platform::getInstance().m_config.dump_buf > 0 ?
                                                                  Platform::getInstance().m_config.dump_buf : 1);
        const unsigned int num_layers = m_last_frame_info->overlay_info.num_layers;
        for (unsigned int i = 0; i < num_layers; i++)
        {
            OverlayPortParam* layer = m_last_frame_info->overlay_info.input.editItemAt(i);
            if (layer->state)
            {
                char identity = getIdentitySymbol(layer->identity);
                if (Platform::getInstance().m_config.dump_buf_type == 'A' ||
                    Platform::getInstance().m_config.dump_buf_type == identity)
                {
                    String8 path;
                    path.appendFormat("/data/SF_dump/%" PRIu64 "_%u_%c", m_disp_id, i, identity);
                    dump_buf(layer->format, layer->compress, layer->dataspace, layer->ion_fd,
                             layer->src_buf_width, layer->src_buf_height, layer->pitch, layer->v_pitch,
                             layer->size, layer->src_crop, downsample, path.string(),
                             Platform::getInstance().m_config.dump_buf_log_enable);
                }
            }
        }
    }

    int total_size = 0;

    dump_str->appendFormat("\n[HWC Compose State (%" PRIu64 ")]\n", m_disp_id);
    for (unsigned int id = 0; id < m_max_inputs; id++)
    {
        OverlayPortParam* param = m_input_params[id];
        if (param->state == OVL_IN_PARAM_ENABLE)
        {
            dump_str->appendFormat("  (%d) f=%#x x=%d y=%d w=%d h=%d -> x=%d y=%d w=%d h=%d\n",
                id, param->format,
                param->src_crop.left, param->src_crop.top,
                param->src_crop.getWidth(), param->src_crop.getHeight(),
                param->dst_crop.left, param->dst_crop.top,
                param->dst_crop.getWidth(), param->dst_crop.getHeight());

            int layer_size = param->dst_crop.getWidth() * param->dst_crop.getHeight() *
                             static_cast<int>(getBitsPerPixel(param->format) / 8);
            total_size += layer_size;

#ifdef MTK_HWC_PROFILING
            if (HWC_LAYER_TYPE_FBT == param->identity)
            {
                dump_str->appendFormat("  FBT(n=%d, bytes=%d)\n",
                    param->fbt_input_layers, param->fbt_input_bytes + layer_size);
            }
#endif
        }
    }

    dump_str->appendFormat("  Total size: %d bytes\n", total_size);
}

bool OverlayEngine::threadLoop()
{
    sem_wait(&m_event);

    if (Platform::getInstance().m_config.dbg_switch & HWC_DBG_SWITCH_DEBUG_SEMAPHORE)
    {
        AutoMutex l(m_lock);
        int32_t sem_value;
        sem_getvalue(&m_event, &sem_value);
        HWC_ATRACE_INT(m_semaphore_name.c_str(), sem_value);
    }

    pid_t tid = gettid();

    HWC_ATRACE_NAME("OE_thread");
    sp<FrameInfo> frame_info = NULL;
    {
        AutoMutex l(m_lock);

        if (m_frame_queue.empty())
        {
            OLOGW("(%" PRIu64 ") Frame queue is empty, it should only be printed when plug out", m_disp_id);
            m_state = HWC_THREAD_IDLE;
            m_condition.signal();
            return true;
        }
        Vector< sp<FrameInfo> >::iterator front(m_frame_queue.begin());
        if (front)
        {
            frame_info = *front;
        }
        else
        {
            LOG_FATAL("%s(), front invalid", __FUNCTION__);
            return true;
        }
        m_frame_queue.erase(front);
        HWC_ATRACE_INT(m_queue_name.c_str(), static_cast<int32_t>(m_frame_queue.size()));
    }

    const nsecs_t config_period = DisplayManager::getInstance().getDisplayData(m_disp_id, frame_info->active_config)->refresh;

    calculatePerf(frame_info, config_period, tid, false);
    // change the cpu set after set the uclamp. it can avoid that use little core with
    // high frequence.
    updateCpuSet(tid, frame_info->cpu_set);

    DataExpress::getInstance().findPackage(m_disp_id,
                                           frame_info->frame_seq,
                                           &frame_info->package,
                                           &frame_info->late_package);

    FrameOverlayInfo* overlay_info = &frame_info->overlay_info;

    if (frame_info->av_grouping)
    {
        AutoMutex l(m_lock_av_grouping);
        m_need_wakeup = true;
        DisplayManager::getInstance().requestNextVSync(m_disp_id);
        if (m_cond_threadloop.waitRelative(m_lock_av_grouping, config_period + ms2ns(4)) == TIMED_OUT)
        {
            OLOGW("timeout to wait vsync to trigger display driver");
        }
    }

    // if vds, and dynamic swith on, vds 1th job need wait primary 1th job done
    if (Platform::getInstance().m_config.dynamic_switch_path && m_disp_id == HWC_DISPLAY_VIRTUAL
        && !DisplayManager::getInstance().display_get_commit_done_state(HWC_DISPLAY_VIRTUAL)
        && !DisplayManager::getInstance().primary_is_power_off())
    {
#ifndef MTK_USER_BUILD
        HWC_ATRACE_NAME("wait_job");
#endif
        while (!DisplayManager::getInstance().display_get_commit_done_state(HWC_DISPLAY_PRIMARY))
        {
            usleep(static_cast<uint32_t>(ns2us(config_period)));
        }
    }

    const unsigned int num_layers = frame_info->overlay_info.num_layers;
    if (Platform::getInstance().m_config.dump_buf_cont)
    {
        waitAllFence(frame_info);

        static int32_t cnt = 0;
        const unsigned int downsample = static_cast<unsigned int>(Platform::getInstance().m_config.dump_buf_cont > 0 ?
                                                                  Platform::getInstance().m_config.dump_buf_cont : 1);
        for (unsigned int i = 0; i < num_layers; i++)
        {
            OverlayPortParam* layer = frame_info->overlay_info.input.editItemAt(i);
            if (layer->state)
            {
                char identity = getIdentitySymbol(layer->identity);
                if (Platform::getInstance().m_config.dump_buf_cont_type == 'A' ||
                    Platform::getInstance().m_config.dump_buf_cont_type == identity)
                {
                    String8 path;
                    path.appendFormat("/data/SF_dump/%05d_%" PRIu64 "_%u_%c", cnt, m_disp_id, i, identity);
                    Rect crop;
                    if (layer->identity == HWLAYER_ID_DBQ)
                    {
                        crop = Rect(layer->src_buf_width, layer->src_buf_height);
                    }
                    else
                    {
                        crop = layer->src_crop;
                    }
                    dump_buf(layer->format, layer->compress, layer->dataspace, layer->ion_fd,
                             layer->src_buf_width, layer->src_buf_height, layer->pitch, layer->v_pitch,
                             layer->size, crop, downsample, path.string(),
                             Platform::getInstance().m_config.dump_buf_log_enable);
                }
            }
        }
        ++cnt;
    }
    preparePresentIndexBuffer(frame_info);

    // check need set dim buf for hdmi rx buf
    for (unsigned int i = 0; i < num_layers; i++)
    {
        OverlayPortParam* layer = frame_info->overlay_info.input.editItemAt(i);
        if (layer->hdmi_rx)
        {
            if (!DisplayManager::getInstance().checkHDMIRxLocked())
            {
                // real black screen time depened on SF present frame speed
                m_hdmirx_black_screen_countdown = HDMIRX_BLACK_SCREEN_FRAME_COUNT;
            }

            if (m_hdmirx_black_screen_countdown)
            {
                HWC_LOGV("[%s][HDMIRX] signal not stable!!!!! force dim for %u frame", __func__, m_hdmirx_black_screen_countdown);
                layer->dim = true;
                m_hdmirx_black_screen_countdown--;
            }
        }

        // Do not wait previous fence for HDMIRX latency
        if ((layer->hdmi_rx) && (frame_info->prev_present_fence != -1))
        {
            protectedClose(frame_info->prev_present_fence);
            frame_info->prev_present_fence = -1;
        }
    }
    setInputsAndOutput(overlay_info);
    if (!HWCMediator::getInstance().getOvlDevice(m_disp_id)->isFenceWaitSupported())
    {
        waitAllFence(frame_info);
    }
    waitPresentAfterTs(frame_info);

    if (frame_info->mm_ion_fd > 0)
    {
        IONDevice::getInstance().ionClose(frame_info->mm_ion_fd);
    }

    calculatePerf(frame_info, config_period, tid, true);
    loopHandler(frame_info);
    releasePresentIndexBuffer(frame_info);

    checkPresentAfterTs(frame_info, config_period);

    if (HWCMediator::getInstance().getOvlDevice(m_disp_id)->isFenceWaitSupported())
    {
        // because display driver uses the reference count of fence, it does not need fence fd,
        // and therefore let us close all fence fd in here
        closeAllFenceFd(frame_info);
    }

    if (DisplayManager::m_profile_level & PROFILE_DBG_WFD)
    {
        char atrace_tag[256] = "";
        uint64_t sequence = frame_info->frame_seq;
        if (snprintf(atrace_tag, sizeof(atrace_tag), "SET-OVL%" PRIu64, m_disp_id) > 0)
        {
            HWC_ATRACE_ASYNC_END(atrace_tag, sequence);
        }
        if (m_disp_id == HWC_DISPLAY_VIRTUAL)
        {
            if (snprintf(atrace_tag, sizeof(atrace_tag), "OVL2-SMS") > 0)
            {
                HWC_ATRACE_ASYNC_BEGIN(atrace_tag, sequence);
            }
        }
        // sync with WFD log
        OLOGD("OVL%" PRIu64 " atomic commit  done mToken = %" PRIu64, m_disp_id, sequence);
    }

    if (Platform::getInstance().m_config.enable_mm_buffer_dump)
    {
        doMmBufferDump(frame_info);
    }

    // clean up DataPackage
    DataExpress::getInstance().deletePackage(m_disp_id, frame_info->frame_seq);
    frame_info->package = nullptr; // remove just to prevent misuse
    frame_info->late_package = nullptr;

    {
        AutoMutex l(m_lock_dump);
        if (m_last_frame_info != nullptr)
        {
            closeAllIonFd(m_last_frame_info);
            closeMMLCfgFd(m_last_frame_info);
        }
        m_last_frame_info = frame_info;
    }

    // display HW has applied the new config, so we change the period of VSyncThread in here
    DisplayManager::getInstance().updateVsyncThreadPeriod(m_disp_id, config_period);

    // if vds, and 4+2 swith on, vds 1th job need wait primary 1th job done
    if (Platform::getInstance().m_config.dynamic_switch_path &&
       !DisplayManager::getInstance().display_get_commit_done_state(m_disp_id))
    {
        DisplayManager::getInstance().display_set_commit_done_state(m_disp_id, true);
    }

    {
        AutoMutex l(m_lock);
        if (m_frame_queue.empty())
        {
            m_state = HWC_THREAD_IDLE;
            m_condition.signal();
        }
    }

    return true;
}

void OverlayEngine::closeOverlayFenceFd(FrameOverlayInfo* info)
{
    for (unsigned int i = 0; i < info->num_layers; i++)
    {
        OverlayPortParam* layer = info->input.editItemAt(i);
        if (layer->fence != -1)
        {
            protectedClose(layer->fence);
            layer->fence = -1;
        }
    }

    if (info->enable_output && info->output.fence != -1)
    {
        protectedClose(info->output.fence);
        info->output.fence = -1;
    }
}

void OverlayEngine::closeAllFenceFd(const sp<FrameInfo>& info)
{
    closeOverlayFenceFd(&info->overlay_info);
    if (info->prev_present_fence != -1)
    {
        protectedClose(info->prev_present_fence);
        info->prev_present_fence = -1;
    }
}

void OverlayEngine::closeAllIonFd(const sp<FrameInfo>& info)
{
    FrameOverlayInfo* overlay_info = &info->overlay_info;
    for (unsigned int i = 0; i < overlay_info->num_layers; i++)
    {
        if (overlay_info->input[i]->state == OVL_IN_PARAM_ENABLE && overlay_info->input[i]->ion_fd > 0)
        {
            IONDevice::getInstance().ionCloseAndSet(&overlay_info->input[i]->ion_fd, -1, false);

            if (overlay_info->input[i]->is_mml)
            {
                if (overlay_info->input[i]->mml_cfg)
                {
                    IONDevice::getInstance().ionCloseAndSet(
                    &(overlay_info->input[i]->mml_cfg->buffer.src.fd[0]), -1, false);
                }
            }
        }
    }

    if (overlay_info->enable_output)
    {
        IONDevice::getInstance().ionCloseAndSet(&overlay_info->output.ion_fd, -1, false);
    }
}


void OverlayEngine::closeMMLCfgFd(const sp<FrameInfo>& info)
{
    FrameOverlayInfo* overlay_info = &info->overlay_info;
    for (unsigned int i = 0; i < overlay_info->num_layers; i++)
    {
        if (overlay_info->input[i]->state == OVL_IN_PARAM_ENABLE)
        {
            if (!overlay_info->input[i]->is_mml)
                continue;

            if (!overlay_info->input[i]->mml_cfg)
                continue;

            struct mml_submit* config = overlay_info->input[i]->mml_cfg;
            if (-1 != config->buffer.src.fence)
            {
                ::protectedClose(config->buffer.src.fence);
                config->buffer.src.fence = -1;
            }

            for (uint32_t output = 0; output < MML_MAX_OUTPUTS; ++output)
            {
                if (output >= config->buffer.dest_cnt)
                    continue;

                if (-1 != config->buffer.dest[output].fence)
                {
                    ::protectedClose(config->buffer.dest[output].fence);
                    config->buffer.dest[output].fence = -1;
                }

            }

            // If PQ return MMLPQ2ndOutputInfo for AI SDR to HDR, we need to close dest[1] fd
            // dest[0] -> MML final output
            // dest[1] -> resize for AI model.
            if ((2 == config->info.dest_cnt) && (2 == config->buffer.dest_cnt))
            {
                if (-1 != config->buffer.dest[1].fd[0])
                {
                    ::protectedClose(config->buffer.dest[1].fd[0]);
                    config->buffer.dest[1].fd[0] = -1;
                }
            }
        }
    }
}


void OverlayEngine::waitAllFence(sp<FrameInfo>& info)
{
    char tag[128];
    DbgLogger logger(DbgLogger::TYPE_HWC_LOG, 'D', nullptr);
    logger.printf("(%" PRIu64 ") Wait present fence for idx: %d", m_disp_id, info->present_fence_idx);

    waitOverlayFence(info);

    if (info->prev_present_fence != -1)
    {

#ifdef FENCE_DEBUG
        HWC_LOGD("+ OverlayEngine::waitPresentFence prev_present_fence:%d", info->prev_present_fence);
#endif
        HWC_ATRACE_NAME("PF");
        if (!isUserLoad() && snprintf(tag, sizeof(tag), "%s-PF", DEBUG_LOG_TAG) > 0)
        {
            m_sync_fence->wait(info->prev_present_fence, 1000, tag);
        }
        else
        {
            m_sync_fence->wait(info->prev_present_fence, 1000, nullptr, SYNC_FENCE_PF, m_disp_id);
        }
#ifdef FENCE_DEBUG
        HWC_LOGD("- OverlayEngine::waitPresentFence");
#endif
        info->prev_present_fence = -1;
    }

    if (info->pq_fence_fd != -1)
    {
        HWC_ATRACE_NAME("PQF");
        if (!isUserLoad() && snprintf(tag, sizeof(tag), "%s-PQF", DEBUG_LOG_TAG) > 0)
        {
            m_sync_fence->wait(info->pq_fence_fd, 1000, tag);
        }
        else
        {
            m_sync_fence->wait(info->pq_fence_fd, 1000, nullptr, SYNC_FENCE_PQ, m_disp_id);
        }
    }
}

void OverlayEngine::waitOverlayFence(sp<FrameInfo>& frame_info)
{
    char tag[128];
    FrameOverlayInfo* info = &frame_info->overlay_info;

    for (unsigned int i = 0; i < info->num_layers; i++)
    {
        OverlayPortParam* layer = info->input.editItemAt(i);
        if ((layer->hdmi_rx) && (layer->fence != -1))
        {
            const DisplayData* disp_data = DisplayManager::getInstance().getDisplayData(m_disp_id);
            if ((disp_data != NULL) && ((uint64_t)disp_data->refresh != 0))
            {
                uint32_t tx_fps = (uint32_t)((1000000000+disp_data->refresh-1)/disp_data->refresh);
                if (tx_fps == DisplayManager::getInstance().getHDMIRxFrameRate()) // rx fps == tx fps
                {
                   protectedClose(layer->fence);
                   layer->fence = -1;
                   HWC_LOGV("Ignore hdmi_rx layer fence for low rx latency");
                }
                else if (!DisplayManager::getInstance().checkHDMIRxLocked())
                {
                    protectedClose(layer->fence);
                    layer->fence = -1;
                    HWC_LOGV("Ignore hdmi_rx layer fence for fast black screen");
                }
            }
            else
                HWC_LOGE(" get tx fps fail");
        }
        if (layer->fence != -1)
        {
#ifdef FENCE_DEBUG
            HWC_LOGD("+ OverlayEngine::waitOverlayFence() layer->fence:%d", layer->fence);
#endif
        if (!isUserLoad() && snprintf(tag, sizeof(tag), "%s-IN-%u", DEBUG_LOG_TAG, i) > 0)
        {
            m_sync_fence->wait(layer->fence, 1000, tag);
        }
        else
        {
            m_sync_fence->wait(layer->fence, 1000, nullptr, SYNC_FENCE_OVL_IN, m_disp_id, i);
        }
#ifdef FENCE_DEBUG
            HWC_LOGD("- OverlayEngine::waitOverlayFence()");
#endif
            layer->fence = -1;
        }

        if (layer->identity == HWLAYER_ID_DBQ && frame_info->decouple_target_ts > 0)
        {
            const nsecs_t cur_time = systemTime();
            nsecs_t diff = cur_time - frame_info->decouple_target_ts;

            if (diff > 0)
            {
                HWC_ATRACE_INT(m_trace_decoulpe_delay_name.c_str(), 1);
                HWC_ATRACE_INT(m_trace_decoulpe_delay_name.c_str(), 0);
                HWC_ATRACE_INT64(m_trace_decoulpe_delay_ns_name.c_str(), static_cast<int64_t>(diff));
            }
        }
    }

    if (info->enable_output && info->output.fence != -1)
    {
#ifdef FENCE_DEBUG
        HWC_LOGD("+ OverlayEngine::waitOverlayFence() output.fence:%d", info->output.fence);
#endif
        if (!isUserLoad() && snprintf(tag, sizeof(tag), "%s-OUT", DEBUG_LOG_TAG) > 0)
        {
            m_sync_fence->wait(info->output.fence, 1000, tag);
        }
        else
        {
            m_sync_fence->wait(info->output.fence, 1000, nullptr, SYNC_FENCE_OVL_OUT, m_disp_id);
        }
#ifdef FENCE_DEBUG
        HWC_LOGD("- OverlayEngine::waitOverlayFence()");
#endif
        info->output.fence = -1;
    }
}

void OverlayEngine::onFirstRef()
{
    run(m_thread_name.c_str(), PRIORITY_URGENT_DISPLAY);
}

void OverlayEngine::packageFrameInfo(sp<FrameInfo>& info, const unsigned int& present_fence_idx,
                                     const int& prev_present_fence,
                                     const int& pq_fence_fd)
{
    DbgLogger logger(DbgLogger::TYPE_HWC_LOG, 'D', nullptr);
    logger.printf("(%" PRIu64 ") Trigger with idx: %u", m_disp_id, present_fence_idx);

    info->ovlp_layer_num = m_handling_job->layer_info.max_overlap_layer_num;
    info->hrt_weight = m_handling_job->layer_info.hrt_weight;
    info->hrt_idx = m_handling_job->layer_info.hrt_idx;
    info->active_config = m_handling_job->active_config;
    info->present_fence_idx = static_cast<int>(present_fence_idx);
    info->prev_present_fence = prev_present_fence;
    info->av_grouping = m_handling_job->need_av_grouping;
    info->pq_fence_fd = pq_fence_fd;
    info->present_after_ts = m_handling_job->present_after_ts;
    info->decouple_target_ts = m_handling_job->decouple_target_ts;
    info->ovl_mc = m_handling_job->ovl_mc;
    info->ovl_mc_atomic_ratio = m_handling_job->ovl_mc_atomic_ratio;
    info->ovl_wo_atomic_work_time = m_handling_job->ovl_wo_atomic_work_time;
    info->cpu_set = m_handling_job->cpu_set;
    info->drm_id_crtc = m_handling_job->drm_id_cur_crtc;
    info->mm_ion_fd = -1;
    logger.printf("/ PF: %d", info->prev_present_fence);

    FrameOverlayInfo* overlay_info = &info->overlay_info;
    overlay_info->ovl_valid = m_handling_job->ovl_valid;
    overlay_info->num_layers = m_handling_job->num_layers;
    overlay_info->drm_id_crtc = m_handling_job->drm_id_cur_crtc;
    overlay_info->color_transform = m_handling_job->color_transform;
    for (unsigned int i = 0; i < m_handling_job->num_layers; i++)
    {
        OverlayPortParam* layer = overlay_info->input.editItemAt(i);
        copyOverlayPortParam(*(m_input_params[i]), layer);
        m_input_params[i]->fence = -1;
        if (m_input_params[i]->state == OVL_IN_PARAM_ENABLE && m_input_params[i]->ion_fd > 0)
        {
            IONDevice::getInstance().ionImport(&layer->ion_fd, false);
        }

        if (m_input_params[i]->state == OVL_IN_PARAM_DISABLE && m_input_params[i]->ion_fd < 0)
        {
            if (layer->is_mml == true)
            {
                HWC_LOGW("Can not enable is_mml when this layse disable !");
                layer->is_mml = false;
            }
        }

        layer->fb_id = 0;
        logger.printf("/ %u,s:%d,f_fd:%d,ion:%d,is_mml_ir:%d", i, layer->state, layer->fence, layer->ion_fd, layer->is_mml);

        HWLayer* hw_layer = &m_handling_job->hw_layers[i];
        if ((HWC_LAYER_TYPE_MM == hw_layer->type) && (!(HWC_MML_DISP_DIRECT_DECOUPLE_LAYER & hw_layer->layer_caps)) && hw_layer->is_do_blit)
        {
            info->mm_ion_fd = hw_layer->priv_handle.ion_fd;
        }
    }

#ifdef MTK_IN_DISPLAY_FINGERPRINT
    info->is_HBM = (m_handling_job->is_HBM) ? true : false ;
#endif

#ifdef MTK_HDR_SET_DISPLAY_COLOR
    info->is_HDR = m_handling_job->is_HDR;
#endif

    info->frame_seq = m_handling_job->sequence;

    overlay_info->enable_output = (m_output.connected_state == OVL_PORT_ENABLE)? true : false;
    memcpy(&overlay_info->output, &m_output.param, sizeof(OverlayPortParam));
    overlay_info->output.fb_id = 0;
    if (overlay_info->enable_output)
    {
        IONDevice::getInstance().ionImport(&overlay_info->output.ion_fd, false);
    }
    m_output.param.fence = -1;
}

void OverlayEngine::setInputsAndOutput(FrameOverlayInfo* info)
{
    // wait untill HW status has been switched to split mode
    if (!info->ovl_valid)
    {
        info->ovl_valid = waitUntilAvailable();
        if (!info->ovl_valid) return;
    }

    // set input parameter to display driver
    if (DisplayManager::m_profile_level & PROFILE_TRIG)
    {
        char atrace_tag[128];

        if (snprintf(atrace_tag, sizeof(atrace_tag), "set_ovl(%" PRIu64 "): set inputs", m_disp_id) > 0)
        {
            HWC_ATRACE_NAME(atrace_tag);
        }
        OLOGV("HWC->OVL: set inputs (max=%u)", info->num_layers);
    }
    HWCMediator::getInstance().getOvlDevice(m_disp_id)->updateOverlayInputs(
        m_disp_id, info->drm_id_crtc, info->input.array(), info->num_layers, info->color_transform);

    // set output parameter to display driver
    if (info->enable_output)
    {
        if (DisplayManager::m_profile_level & PROFILE_TRIG)
        {
            char atrace_tag[128];
            if (snprintf(atrace_tag, sizeof(atrace_tag), "set_ovl(%" PRIu64 "): set output", m_disp_id) > 0)
            {
                HWC_ATRACE_NAME(atrace_tag);
            }
            OLOGD("HWC->OVL: set output");

            HWCMediator::getInstance().getOvlDevice(m_disp_id)->enableOverlayOutput(m_disp_id, info->drm_id_crtc,
                                                                                    &info->output);
        }
        else
        {
            HWCMediator::getInstance().getOvlDevice(m_disp_id)->enableOverlayOutput(m_disp_id, info->drm_id_crtc,
                                                                                    &info->output);
        }
    }
    else
    {
        HWCMediator::getInstance().getOvlDevice(m_disp_id)->disableOverlayOutput(m_disp_id, info->drm_id_crtc);
    }
}

void OverlayEngine::wakeup()
{
    AutoMutex l(m_lock_av_grouping);
    if (m_need_wakeup)
    {
        m_need_wakeup = false;
        m_cond_threadloop.signal();
    }
}

void OverlayEngine::onVSync()
{
    wakeup();
}

void OverlayEngine::doMmBufferDump(sp<FrameInfo>& info)
{
    const unsigned int num_layer = info->overlay_info.num_layers;
    uint32_t filter = Platform::getInstance().m_config.dump_ovl_bits;
    const uint32_t mask = 0x01;
    if (filter == 0)
    {
        filter = ~0U;
    }
    for (size_t i = 0; i < num_layer; i++)
    {
        const OverlayPortParam* param = info->overlay_info.input[i];
        if ((param->state == OVL_IN_PARAM_ENABLE) && (filter & (mask << i)) && !param->dim)
        {
            char module_name[256] = {0};
            if (snprintf(module_name, sizeof(module_name), "Disp_%" PRIu64 "-OVL-IN_%02zu", m_disp_id, i) > 0)
            {
                MmBufDump::getInstance().dump(param->ion_fd, param->alloc_id, static_cast<uint32_t>(param->size),
                                              param->src_buf_width, param->src_buf_height,
                                              static_cast<uint32_t>(param->dataspace), param->format, param->pitch,
                                              param->v_pitch, module_name);
            }
            else
            {
                OLOGE("%s failed to get module name, give up to dump.", __FUNCTION__);
            }
        }
    }
}

void OverlayEngine::waitPresentAfterTs(sp<FrameInfo>& info)
{
    if (info->present_after_ts <= 0 || m_disp_id == HWC_DISPLAY_VIRTUAL)
    {
        return;
    }

    const nsecs_t cur_time = systemTime();
    const nsecs_t diff_time = info->present_after_ts - cur_time;

    if (diff_time > 0)
    {
        if (diff_time > ms2ns(50))
        {
            HWC_LOGW("diff_time %" PRId64 " too big, no sleep", diff_time);
        }
        HWC_ATRACE_NAME((std::string(__FUNCTION__) + std::to_string(diff_time)).c_str());

        usleep(static_cast<uint32_t>(ns2us(diff_time)));
    }
}

void OverlayEngine::checkPresentAfterTs(sp<FrameInfo>& info, nsecs_t period)
{
    if (info->present_after_ts <= 0 || m_disp_id == HWC_DISPLAY_VIRTUAL)
    {
        return;
    }

    const nsecs_t cur_time = systemTime();

    if (cur_time > info->present_after_ts + period)
    {
        HWC_ATRACE_INT(m_trace_delay_name.c_str(), m_trace_delay_counter++ % 2);
    }
}

void OverlayEngine::calculatePerf(sp<FrameInfo>& info, nsecs_t period, pid_t tid, bool is_atomic)
{
    if (info->ovl_mc == FLT_MAX)
    {
        return;
    }

    if (info->cpu_set != HWC_CPUSET_LITTLE && info->cpu_set != HWC_CPUSET_NONE)
    {
        return;
    }

    const nsecs_t cur_time = systemTime();
    nsecs_t sf_target_ts = info->present_after_ts + period;
    nsecs_t remain_time = sf_target_ts - cur_time;
    nsecs_t extension_time = 0;

    if (remain_time <= 0)
    {
        nsecs_t dividend = -remain_time;
        extension_time = ((dividend / period) + 1) * period;
        remain_time += extension_time;
    }
    HWC_ATRACE_INT64(m_perf_extension_time_str.c_str(), extension_time);
    HWC_ATRACE_INT64(m_perf_remain_time_str.c_str(), static_cast<int64_t>(remain_time));

    // get work mc
    float work_mc;
    nsecs_t work_time;
    if (is_atomic)
    {
        work_mc = info->ovl_mc * info->ovl_mc_atomic_ratio;
        work_time = remain_time;
    }
    else
    {
        work_mc = info->ovl_mc * (1 - info->ovl_mc_atomic_ratio);
        if (info->ovl_wo_atomic_work_time <= 0)
        {
            work_time = remain_time;
        }
        else
        {
            work_time = std::min(remain_time, info->ovl_wo_atomic_work_time);
        }
    }

    uint32_t ovl_uclamp = UINT32_MAX;
    if (work_time <= 0)
    {
        ovl_uclamp = Platform::getInstance().m_config.uclamp_cpu_table.back().uclamp;
    }
    else
    {
        uint32_t target_cpu_mhz = calculateCpuMHz(work_mc, work_time);

        ovl_uclamp = cpuMHzToUClamp(target_cpu_mhz);

        HWC_ATRACE_INT(m_perf_target_cpu_mhz_str.c_str(), static_cast<int32_t>(target_cpu_mhz));
    }

    // set uclamp for overlay
    if (ovl_uclamp != m_prev_uclamp_min)
    {
        bool apply_uclamp = true;
        if (!is_atomic)
        {
            // non atomic part is small, we want to avoid keep setting uclamp_task, if only small changes
            if (ovl_uclamp < m_prev_uclamp_min &&
                m_prev_uclamp_min - ovl_uclamp < 25)
            {
                apply_uclamp = false;
            }
        }

        if (apply_uclamp)
        {
            m_prev_uclamp_min = ovl_uclamp;

            uclamp_task(tid, ovl_uclamp);
            HWC_ATRACE_INT(m_perf_uclamp_str.c_str(), static_cast<int32_t>(ovl_uclamp));
        }
    }
}

void OverlayEngine::updateCpuSet(pid_t tid, unsigned int cpu_set)
{
    if (cpu_set != m_cpu_set)
    {
        m_cpu_set = cpu_set;
        changeCpuSet(tid, m_cpu_set);
    }
}

size_t OverlayEngine::getQueueSize()
{
    AutoMutex l(m_lock);
    return m_frame_queue.size();
}

void OverlayEngine::preparePresentIndexBuffer(sp<FrameInfo>& info)
{
    const unsigned int num_layers = info->overlay_info.num_layers;
    for (unsigned int i = 0; i < num_layers; i++)
    {
        OverlayPortParam* param = info->overlay_info.input.editItemAt(i);
        if (param->state == OVL_IN_PARAM_ENABLE &&
                param->debug_type == HWC_DEBUG_LAYER_TYPE_PRESENT_IDX)
        {
            int fd = -1;
            uint64_t alloc_id = 0;
            IndexBufferGenerator::getInstance().getBuffer(info->present_fence_idx,
                    static_cast<uint32_t>(param->src_crop.getWidth()),
                    static_cast<uint32_t>(param->src_crop.getHeight()), &fd, &alloc_id);
            ::protectedClose(param->ion_fd);
            param->ion_fd = fd;
            param->alloc_id = alloc_id;
        }
    }
}

void OverlayEngine::releasePresentIndexBuffer(sp<FrameInfo>& info)
{
    IndexBufferGenerator::getInstance().cancelBuffer(info->present_fence_idx - 1);
}

//-----------------------------------------------------------------------------

FrameOverlayInfo::FrameOverlayInfo()
    : color_transform(nullptr)
{
    unsigned int max_inputs = getHwDevice()->getMaxOverlayInputNum();
    for (unsigned int i = 0; i < max_inputs; i++)
    {
        OverlayPortParam* tmp = new OverlayPortParam;
        if (tmp == NULL)
        {
            HWC_LOGE("failed to allocate OverlayPortParam %u/%u", i, max_inputs);
            // TODO find a suitable way to check allocate memory
            // This class is created by pool when construct HWC
            // If we can not allocate it when device boot, I think that system is abnormal.
            abort();
        }
        else
        {
            input.add(tmp);
        }
    }

    initData();
}

FrameOverlayInfo::~FrameOverlayInfo()
{
    for (size_t i = 0; i < input.size(); i++)
    {
        delete input[i];
    }
    input.clear();
}

void FrameOverlayInfo::initData()
{
    ovl_valid = false;
    num_layers = 0;
    drm_id_crtc = UINT32_MAX;
    enable_output = false;

    size_t size = input.size();
    for (size_t i = 0; i < size; i++)
    {
        resetOverlayPortParam(input[i]);
    }
    resetOverlayPortParam(&output);
}

void FrameOverlayInfo::resetOverlayPortParam(OverlayPortParam* param)
{
    //reset and save mml_cfg
    mml_submit* mml_cfg_p = param->mml_cfg;

    memset(param, 0, sizeof(OverlayPortParam));

    param->state = OVL_IN_PARAM_DISABLE;
    param->identity = HWLAYER_ID_NONE;
    param->alpha = 0xff;
    param->ion_fd = -1;
    param->fence = -1;
    param->mir_rel_fence_fd = -1;

    //set mml_cfg addresss back
    param->mml_cfg = mml_cfg_p;
    // reset mml config if it has allocated
    if (NULL != param->mml_cfg)
    {
        param->resetMMLCfg();
    }
}

void FrameInfo::initData()
{
    present_fence_idx = -1;
    ovlp_layer_num = 0;
    prev_present_fence = -1;
    overlay_info.initData();
    av_grouping = false;
    hrt_weight = 0;
    hrt_idx = 0;
    active_config = 0;
#ifdef MTK_HDR_SET_DISPLAY_COLOR
    is_HDR = 0;
#endif
    package = nullptr;
    late_package = nullptr;
    pq_fence_fd = -1;
    present_after_ts = -1;
    decouple_target_ts = -1;
    ovl_mc = FLT_MAX;
    ovl_mc_atomic_ratio = 1.f;
    ovl_wo_atomic_work_time = -1;
    cpu_set = HWC_CPUSET_NONE;
    drm_id_crtc = UINT32_MAX;
}
