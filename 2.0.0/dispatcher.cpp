#define DEBUG_LOG_TAG "JOB"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include <android-base/stringprintf.h>
#include <sched.h>
#include <string>

#include <hwc_feature_list.h>

#include "utils/debug.h"
#include "utils/tools.h"

#include "ai_blulight_defender.h"
#include "display_dump.h"
#include "data_express.h"
#include "dispatcher.h"
#include "display.h"
#include "overlay.h"
#include "queue.h"
#include "sync.h"
#include "platform_wrap.h"
#include "hwc2.h"
#include "pq_interface.h"
#include <cutils/properties.h>

#define HWC_ATRACE_JOB(string, n1, n2, n3, n4, n5)                                             \
    if (ATRACE_ENABLED()) {                                                                \
        android::ScopedTrace ___bufTracer(ATRACE_TAG,                                      \
            android::base::StringPrintf("%s(%" PRIu64 "): %d %d %d %d",                       \
            (string), (n1), (n2), (n3), (n4), (n5)).c_str());      \
    }

// Since driver fence idx start from 1, we use "1" as initial val.
#define INIT_FENCE_IDX 1
// ---------------------------------------------------------------------------

HWLayer::HWLayer()
{
    resetData();
}

void HWLayer::resetData()
{
    enable = false;
    index = 0;
    type = 0;
    dirty = false;
    dirty_reason = HW_LAYER_DIRTY_NONE;
    mdp_job_id = 0;
    mdp_dst_roi.clear();
    mdp_skip_invalidate = false;
    glai_dst_roi.clear();
    glai_agent_id = -1;
    fence_index = 0;
    ext_sel_layer = 0;
    memset(&layer, 0, sizeof(layer));
    memset(surface_damage_rect, 0, sizeof(hwc_rect_t) * MAX_DIRTY_RECT_CNT);
    hwc2_layer_id = 0;
    layer_caps = 0;
    layer_color = 0;
    dataspace = HAL_DATASPACE_UNKNOWN;
    fb_id = 0;
    need_pq = false;
    is_ai_pq = false;
    hwc_layer = nullptr;
    queue = nullptr;
    mdp_output_compressed = false;
    mdp_output_format = 0;
    mdp_output_dataspace = 0;
    priv_handle.ion_fd = -1;
    hdr_static_metadata_keys.clear();
    hdr_static_metadata_values.clear();
    hdr_dynamic_metadata.clear();
    hdr_scenario = HWC_LAYER_HDR_NONE;
    debug_type = HWC_DEBUG_LAYER_TYPE_NONE;
    is_do_blit = false;
}

DispatcherJob::DispatcherJob(unsigned int max_ovl_inputs)
    : m_max_ovl_inputs(max_ovl_inputs)
    , hw_layers(new HWLayer[max_ovl_inputs])
    , prev_present_fence_fd(-1)
    , pq_fence_fd(-1)
{
    LOG_ALWAYS_FATAL_IF(!hw_layers, "new HWLayer * %u fail", max_ovl_inputs);
    resetData();
}

DispatcherJob::~DispatcherJob()
{
    if (hw_layers)
    {
        delete[] hw_layers;
    }
}

void DispatcherJob::resetData()
{
    enable = false;
    is_internal = false;
    secure = false;
    mirrored = false;
    need_output_buffer = false;
    disp_ori_id = 0;
    drm_id_cur_crtc = UINT32_MAX;
    disp_mir_id = HWC_MIRROR_SOURCE_INVALID;
    disp_ori_rot = 0;
    disp_mir_rot = 0;
    num_layers = 0;
    ovl_valid = false;
    fbt_exist = false;
    triggered = false;
    num_ui_layers = 0;
    num_mm_layers = 0;
    num_glai_layers = 0;
    post_state = HWC_POST_INVALID;
    sequence = HWC_SEQUENCE_INVALID;
    timestamp = 0;
    if (hw_layers)
    {
        for (unsigned int i = 0; i < m_max_ovl_inputs; i++)
        {
            hw_layers[i].resetData();
        }
    }
    hw_mirbuf.resetData();
    hw_outbuf.resetData();
    if (prev_present_fence_fd != -1)
    {
        HWC_LOGE("prev_present_fence_fd %d not close", prev_present_fence_fd);
        ::protectedClose(prev_present_fence_fd);
        prev_present_fence_fd = -1;
    }
    prev_present_fence_fd = -1;
    is_full_invalidate = true;
    mdp_job_output_buffer = 0;
    need_av_grouping = false;
    num_processed_mm_layers = 0;
    is_black_job = false;
    color_transform = nullptr;
    active_config = 0;
    disp_data = nullptr;
    mir_disp_data = nullptr;
#ifdef MTK_IN_DISPLAY_FINGERPRINT
    is_HBM = false;
#endif
#ifdef MTK_HDR_SET_DISPLAY_COLOR
    is_HDR = 0;
#endif
    dirty_pq_mode_id = false;
    pq_mode_id = 0;
    if (pq_fence_fd >= 0)
    {
        HWC_LOGE("pq_fence_fd %d not close", pq_fence_fd);
        ::protectedClose(pq_fence_fd);
    }
    pq_fence_fd = -1;
    sf_target_ts = -1;
    present_after_ts = -1;
    decouple_target_ts = -1;
    ovl_mc = FLT_MAX;
    ovl_mc_atomic_ratio = 1.f;
    ovl_wo_atomic_work_time = -1;
    mc_info = nullptr;

    aibld_enable = false;
    display_dump_enable = false;
    cpu_set = HWC_CPUSET_NONE;
    wdma_status = HWC_WDMA_STATUS_DISABLE;
}

HWCDispatcher::WorkerCluster::~WorkerCluster()
{
    if (m_job_pool)
    {
        delete m_job_pool;
    }
}

HWCDispatcher& HWCDispatcher::getInstance()
{
    static HWCDispatcher gInstance;
    return gInstance;
}

HWCDispatcher::HWCDispatcher()
    : m_sequence(1)
    , m_session_mode_changed(0)
{
    m_vsync_callbacks.resize(DisplayManager::MAX_DISPLAYS);

    for (uint32_t i = 0; i < DisplayManager::MAX_DISPLAYS; i++)
    {
        m_prev_createjob_time[i] = 0;
    }
}

HWCDispatcher::~HWCDispatcher()
{
    for (uint32_t i = 0; i < DisplayManager::MAX_DISPLAYS; i++)
    {
        if (m_alloc_disp_ids.test(i))
        {
            onPlugOut(i, m_workers[i].drm_id_cur_crtc);
        }
    }
}

int HWCDispatcher::getJob(uint64_t dpy)
{
#ifndef MTK_USER_BUILD
    HWC_ATRACE_CALL();
#endif

    if (dpy >= DisplayManager::MAX_DISPLAYS)
        return HWC_DISPACHERJOB_INVALID_DISPLAY;

    sp<DispatcherJob> job = nullptr;
    int dispatcher_job_status = HWC_DISPACHERJOB_VALID;
    {
        AutoMutex l(m_workers[dpy].plug_lock_main);
        if (DisplayManager::getInstance().getDisplayData(dpy)->trigger_by_vsync && m_workers[dpy].enable)
        {
            const nsecs_t cur_time = systemTime();
            bool should_drop_job = (cur_time - m_prev_createjob_time[dpy]) <
                                   (DisplayManager::getInstance().getDisplayData(dpy)->refresh -
                                    Platform::getInstance().m_config.tolerance_time_to_refresh);
            size_t queue_size = (isNoDispatchThread() && m_workers[dpy].ovl_engine != NULL) ?
                                m_workers[dpy].ovl_engine->getQueueSize() :
                                m_workers[dpy].dp_thread->getQueueSize();
            if (should_drop_job || queue_size > 2)
            {
                HWC_LOGD("Return null job, dpy(%" PRIu64 ") queue size(%zu)", dpy, queue_size);
                m_curr_jobs[dpy] = job;
                if (m_workers[dpy].idle_thread != NULL)
                {
                    m_workers[dpy].idle_thread->setEnabled(true);
                }
                return HWC_DISPACHERJOB_INVALID_DROPJOB;
            }
            m_prev_createjob_time[dpy] = cur_time;
        }
        if (!m_workers[dpy].enable)
        {
            HWC_LOGE("Failed to get job: dpy(%" PRIu64 ") is not enabled", dpy);
            dispatcher_job_status = HWC_DISPACHERJOB_INVALID_WORKERS;
        }
        else if (m_workers[dpy].ovl_engine != NULL && !m_workers[dpy].ovl_engine->isEnable())
        {
            if (DisplayManager::getInstance().getDisplayPowerState(dpy))
                HWC_LOGI("ovl_%" PRIu64 " is not enable, do not produce a job", dpy);
            dispatcher_job_status = HWC_DISPACHERJOB_INVALID_OVLENGINE;
        }
        else
        {
            if (!DisplayManager::getInstance().getDisplayPowerState(dpy))
            {
                HWC_LOGI("ovl_%" PRIu64 " is not power_on, do not produce a job", dpy);
                return HWC_DISPACHERJOB_INVALID_OVLENGINE;
            }

            job = m_workers[dpy].m_job_pool->getFreeObject();
            if (!job)
            {
                LOG_FATAL("%s(), job == nullptr", __FUNCTION__);
                return HWC_DISPACHERJOB_INVALID_JOB;
            }

            job->is_internal = m_workers[dpy].is_internal;
            job->disp_ori_id = dpy;
            job->sequence = m_sequence++;

            job->num_layers = m_workers[dpy].ovl_engine->getAvailableInputNum();
            if (job->num_layers == 0)
            {
                // reserve one dump layer for gpu compoisition with FBT
                // if there's np available input layers
                // then post handler would wait until OVL is available
                if (dpy != HWC_DISPLAY_VIRTUAL ||
                    m_workers[HWC_DISPLAY_PRIMARY].ovl_engine->getOverlaySessionMode() != HWC_DISP_SESSION_DECOUPLE_MIRROR_MODE)
                {
                    HWC_LOGW("No available overlay resource (input_num=%d)", job->num_layers);
                }

                job->num_layers = 1;
            }
            else
            {
                job->ovl_valid = true;
            }

            HWC_DISP_MODE display_session_mode = m_workers[dpy].ovl_engine->getOverlaySessionMode();
            if (display_session_mode == HWC_DISP_SESSION_DECOUPLE_MIRROR_MODE ||
                display_session_mode == HWC_DISP_SESSION_DIRECT_LINK_MIRROR_MODE) {
                job->need_output_buffer = true;
            }

            if (Platform::getInstance().m_config.is_support_mdp_pmqos_debug)
            {
                MDPFrameInfoDebugger::getInstance().checkMDPLayerExecTime();
                MDPFrameInfoDebugger::getInstance().insertJob(job->sequence);
            }
        }

        m_curr_jobs[dpy] = job;

        if (m_workers[dpy].idle_thread != NULL)
        {
            m_workers[dpy].idle_thread->setEnabled(false);
        }
    }

    dumpTimeToTrace();
    return dispatcher_job_status;
}

DispatcherJob* HWCDispatcher::getExistJob(uint64_t dpy)
{
    if (dpy >= DisplayManager::MAX_DISPLAYS)
        return NULL;

    {
        AutoMutex l(m_workers[dpy].plug_lock_main);
        return m_curr_jobs[dpy].get();
    }
}

bool HWCDispatcher::decideDirtyAndFlush(uint64_t dpy,
                                        unsigned int idx,
                                        sp<HWCLayer> hwc_layer,
                                        HWLayer& hw_layer)
{
    const uint64_t& hwc2_layer_id = hwc_layer->getId();

    // check HWCLayer
    if (hwc_layer->isBufferChanged())
    {
        hw_layer.dirty_reason |= HW_LAYER_DIRTY_BUFFER;
    }
    if (hwc_layer->isStateContentDirty())
    {
        hw_layer.dirty_reason |= HW_LAYER_DIRTY_HWC_LAYER_STATE;
    }
    if (hwc_layer->isPerFrameMetadataChanged())
    {
        hw_layer.dirty_reason |= HW_LAYER_DIRTY_HWC_LAYER_METADATA;
    }

    const WorkerCluster::PrevHwcLayer* prev_hwc_layer = nullptr;
    for (const WorkerCluster::PrevHwcLayer& l : m_workers[dpy].prev_hwc_layers)
    {
        if (l.hwc2_layer_id == hwc2_layer_id)
        {
            prev_hwc_layer = &l;
            break;
        }
    }

    if (!prev_hwc_layer)
    {
        HWC_LOGV("%s(), layer_id:%" PRIu64 ", new HWCLayer, ovl_id:%u", __FUNCTION__, hwc2_layer_id, idx);
        hw_layer.dirty_reason |= HW_LAYER_DIRTY_DISPATCHER;
        return true;
    }

    const PrivateHandle* priv_handle = &hwc_layer->getPrivateHandle();
    const int pool_id = priv_handle->ext_info.pool_id;
    const int32_t& layer_caps = hwc_layer->getLayerCaps();
    const bool& need_pq = hwc_layer->isNeedPQ(1);
    const bool& is_ai_pq = hwc_layer->isAIPQ();
    const int32_t& hdr_scenario = hwc_layer->getHdrScenario();

    if (prev_hwc_layer->pool_id != pool_id ||
        prev_hwc_layer->layer_caps != layer_caps ||
        prev_hwc_layer->pq_enable ||
        prev_hwc_layer->pq_pos != priv_handle->pq_pos ||
        prev_hwc_layer->pq_orientation != priv_handle->pq_orientation ||
        prev_hwc_layer->pq_table_idx != priv_handle->pq_table_idx ||
        prev_hwc_layer->need_pq != need_pq ||
        prev_hwc_layer->is_ai_pq != is_ai_pq ||
        prev_hwc_layer->hdr_scenario != hdr_scenario)
    {
        hw_layer.dirty_reason |= HW_LAYER_DIRTY_DISPATCHER;
    }

    HWC_LOGV("%s(), layer_id:%" PRIu64 ", ovl_id:%u, dirty_reason:0x%x, pool_id:%d(%d), needPQ(%d), buf drity %d, state dirty %d",
             __FUNCTION__,
             hwc2_layer_id, idx,
             hw_layer.dirty_reason,
             pool_id, prev_hwc_layer->pool_id,
             need_pq, hwc_layer->isBufferChanged(), hwc_layer->isStateContentDirty());

    return hw_layer.dirty_reason != 0;
}

void HWCDispatcher::WorkerCluster::PrevHwcLayer::update(const HWLayer& hw_layers)
{
    pool_id          = hw_layers.priv_handle.ext_info.pool_id;
    layer_caps       = hw_layers.layer_caps;
    pq_enable        = hw_layers.priv_handle.pq_enable;
    pq_pos           = hw_layers.priv_handle.pq_pos;
    pq_orientation   = hw_layers.priv_handle.pq_orientation;
    pq_table_idx     = hw_layers.priv_handle.pq_table_idx;
    is_ai_pq         = hw_layers.is_ai_pq;
    hdr_scenario = hw_layers.hdr_scenario;
}

void HWCDispatcher::fillPrevHwLayers(const sp<HWCDisplay>& display, DispatcherJob* job)
{
    const uint64_t disp_id = display->getId();
    AutoMutex l(m_workers[disp_id].plug_lock_main);
    if (CC_UNLIKELY(!m_workers[disp_id].enable)) return;

    bool already_in[job->num_layers];
    memset(already_in, 0, sizeof(already_in));

    auto& prev_hwc_layers = m_workers[disp_id].prev_hwc_layers;

    prev_hwc_layers.remove_if(
        [&](WorkerCluster::PrevHwcLayer& l)
        {
            for (unsigned int i = 0; i < job->num_layers; ++i)
            {
                if (l.hwc2_layer_id == job->hw_layers[i].hwc2_layer_id)
                {
                    // update and don't remove
                    l.update(job->hw_layers[i]);
                    already_in[i] = true;
                    return false;
                }
            }
            return true;
        });

    // add not in list
    for (unsigned int i = 0; i < job->num_layers; ++i)
    {
        if (already_in[i] || !job->hw_layers[i].enable)
        {
            continue;
        }

        WorkerCluster::PrevHwcLayer prev_hwc_layer(job->hw_layers[i].hwc2_layer_id);
        prev_hwc_layer.update(job->hw_layers[i]);
        prev_hwc_layers.push_back(prev_hwc_layer);
    }
}

void HWCDispatcher::setSessionMode(uint64_t dpy, bool mirrored)
{
    CHECK_DPY_RET_VOID(dpy);

    HWC_DISP_MODE prev_session_mode = HWC_DISP_INVALID_SESSION_MODE;
    HWC_DISP_MODE curr_session_mode = HWC_DISP_INVALID_SESSION_MODE;

    // get previous and current session mode
    {
        AutoMutex l(m_workers[dpy].plug_lock_main);
        if (m_workers[dpy].enable && m_workers[dpy].ovl_engine != NULL)
        {
            prev_session_mode = m_workers[dpy].ovl_engine->getOverlaySessionMode();
            if (HWC_DISP_INVALID_SESSION_MODE == prev_session_mode)
            {
                HWC_LOGW("Failed to set session mode: dpy(%" PRIu64 ")", dpy);
                return;
            }
        }

        curr_session_mode = mirrored ?
            HWC_DISP_SESSION_DECOUPLE_MIRROR_MODE : HWC_DISP_SESSION_DIRECT_LINK_MODE;
    }

    // flush all pending display jobs of displays before
    // session mode transition occurs (mirror <-> extension)
    // TODO: refine this logic if need to flush jobs for other session mode transition
    bool mode_transition =
            mirrored != ((HWC_DISP_SESSION_DECOUPLE_MIRROR_MODE == prev_session_mode) ||
                         (HWC_DISP_SESSION_DIRECT_LINK_MIRROR_MODE == prev_session_mode));
    if (mode_transition)
    {
        for (uint32_t i = 0; i < DisplayManager::MAX_DISPLAYS; i++)
        {
            AutoMutex l(m_workers[i].plug_lock_main);

            if (m_workers[i].enable)
            {
                if (m_workers[i].dp_thread)
                {
                    m_workers[i].dp_thread->wait();
                }
                m_workers[i].ovl_engine->wait();
                HWCMediator::getInstance().getOvlDevice(i)->waitAllJobDone(i);
            }
        }
    }

    // set current session mode
    // TODO: should set HWC_DISPLAY_EXTERNAL/HWC_DISPLAY_VIRTUAL separately?
    {
        AutoMutex l(m_workers[dpy].plug_lock_main);

        if (m_workers[dpy].enable && m_workers[dpy].ovl_engine != NULL)
            m_workers[dpy].ovl_engine->setOverlaySessionMode(curr_session_mode);
    }

    if (CC_UNLIKELY(prev_session_mode != curr_session_mode))
    {
        HWC_LOGD("change session mode (dpy=%" PRIu64 "/mir=%c/%s -> %s)",
            dpy, mirrored ? 'y' : 'n',
            getSessionModeString(prev_session_mode).string(),
            getSessionModeString(curr_session_mode).string());

        incSessionModeChanged();
    }
}

void HWCDispatcher::configMirrorOutput(DispatcherJob* job, const int& display_color_mode)
{
    uint64_t dpy = job->disp_ori_id;

    // temporary solution to prevent error of MHL plug-out
    if (dpy >= DisplayManager::MAX_DISPLAYS) return;

    AutoMutex l(m_workers[dpy].plug_lock_main);

    if (!m_workers[dpy].enable)
        return;

    if (job->need_output_buffer)
    {
        status_t err = m_workers[dpy].ovl_engine->configMirrorOutput(job);
        job->hw_outbuf.dataspace = mapColorMode2DataSpace(display_color_mode);
        if (CC_LIKELY(err == NO_ERROR))
        {
            if (!job->mirrored) {
                if (job->hw_outbuf.mir_out_acq_fence_fd != -1)
                {
                    ::protectedClose(job->hw_outbuf.mir_out_acq_fence_fd);
                    job->hw_outbuf.mir_out_acq_fence_fd = -1;
                }
            }
        }
        else
        {
            // error handling, cancel mirror mode if failing to configure mirror output
            job->need_output_buffer = false;
            m_workers[dpy].ovl_engine->setOverlaySessionMode(HWC_DISP_SESSION_DIRECT_LINK_MODE);
        }
    }

}

void HWCDispatcher::configMirrorJob(DispatcherJob* job)
{
    HWC_LOGV("configMirrorJob job->disp_mir_id:%d", job->disp_mir_id);
    int mir_dpy = job->disp_mir_id;
    uint64_t ori_dpy = job->disp_ori_id;

    if (mir_dpy < 0 || mir_dpy >= DisplayManager::MAX_DISPLAYS) return;
    // temporary solution to prevent error of MHL plug-out
    if (ori_dpy >= DisplayManager::MAX_DISPLAYS) return;

    AutoMutex lm(m_workers[ori_dpy].plug_lock_main);
    AutoMutex l(m_workers[mir_dpy].plug_lock_main);

    if (m_workers[ori_dpy].enable)
    {
        DispatcherJob* mir_job = m_curr_jobs[mir_dpy].get();

        if (NULL == mir_job)
        {
            HWC_LOGI("configMirrorJob mir_job is NULL");
            return;
        }

        // enable mirror source
        mir_job->mirrored = true;

        // keep the orientation of mirror source in mind
        job->disp_mir_rot = mir_job->disp_ori_rot;

        // sync secure setting from mirror source
        job->secure = mir_job->secure;
    }
}

void HWCDispatcher::disableMirror(const sp<HWCDisplay>& display, DispatcherJob* job)
{
    if (job->hw_mirbuf.mir_in_acq_fence_fd != -1)
    {
        ::protectedClose(job->hw_mirbuf.mir_in_acq_fence_fd);
        job->hw_mirbuf.mir_in_acq_fence_fd = -1;
    }
    if (job->fill_black.fence != -1)
    {
        ::protectedClose(job->fill_black.fence);
        job->fill_black.fence = -1;
    }
    if (job->hw_mirbuf.mir_in_rel_fence_fd != -1)
    {
        ::protectedClose(job->hw_mirbuf.mir_in_rel_fence_fd);
        job->hw_mirbuf.mir_in_rel_fence_fd = -1;
    }
    display->clearAllFences();
}

void HWCDispatcher::setJob(const sp<HWCDisplay>& display)
{
#ifndef MTK_USER_BUILD
    HWC_ATRACE_CALL();
#endif
    const uint64_t dpy = display->getId();

    if (dpy >= DisplayManager::MAX_DISPLAYS)
        return;

    {
        AutoMutex l(m_workers[dpy].plug_lock_main);

        if (CC_UNLIKELY(!m_workers[dpy].enable))
        {
            // it can happen if SurfaceFlinger tries to set display job
            // right after hotplug thread just removed a display (e.g. via onPlugOut())
            HWC_LOGW("Failed to set job: dpy(%" PRIu64 ") is not enabled", dpy);
            display->clearAllFences();
            return;
        }

        if (!m_workers[dpy].ovl_engine->isEnable())
        {
            HWC_LOGD("(%" PRIu64 ") SET/bypass/blank", dpy);
            display->clearAllFences();
            return;
        }

        DispatcherJob* job = m_curr_jobs[dpy].get();
        if (NULL == job)
        {
            if (display->getJobStatus() != HWC_DISPACHERJOB_INVALID_DROPJOB)
            {
                HWC_LOGW("(%" PRIu64 ") SET/bypass/nulljob", dpy);
            }
            else
            {
                HWC_LOGV("(%" PRIu64 ") SET/bypass/nulljob", dpy);
            }
            display->clearAllFences();
            return;
        }

        if (display->getId() == HWC_DISPLAY_VIRTUAL &&
            display->getClientTarget() != nullptr)
        {
            // SurfaceFlinger might not setClientTarget while VDS disconnect.
            int32_t gles_head = -1, gles_tail = -1;
            display->getGlesRange(&gles_head, &gles_tail);
            if (gles_head != -1 && display->getClientTarget()->getHandle() == nullptr)
            {
                display->clearAllFences();
                HWC_LOGW("(%" PRIu64 ") SET/bypass/no_client_target", display->getId());
                return;
            }
        }

        if (display->getId() == HWC_DISPLAY_VIRTUAL &&
            (display->getOutbuf() == nullptr ||
             display->getOutbuf()->getHandle() == nullptr))
        {
            // SurfaceFlinger might not set output buffer while VDS disconnect.
            display->clearAllFences();
            HWC_LOGW("(%" PRIu64 ") SET/bypass/no_outbuf", display->getId());
            return;
        }

        auto&& layers = display->getVisibleLayersSortedByZ();
        if (layers.size() == 0 && display->getId() == HWC_DISPLAY_VIRTUAL)
        {
            // bliter cannot handle buffer < 32x32, bypass current frame for VDS
            if (display->getOutbuf()->getPrivateHandle().width < 32 ||
                display->getOutbuf()->getPrivateHandle().height < 32)
            {
                display->clearAllFences();
                HWC_LOGW("(%" PRIu64 ") SET/bypass/no_visible_layers %d", display->getId(), __LINE__);
                return;
            }

            // if no black job for dev & is ovl device for virtual, don't need black job
            if (!((Platform::getInstance().m_config.plat_switch & HWC_PLAT_SWITCH_NO_BLACK_JOB_FOR_OVL_DEV) &&
                HWCMediator::getInstance().getOvlDevice(dpy)->getType() == OVL_DEVICE_TYPE_OVL))
            {
                // refresh black buffer to virtual display
                job->is_black_job = true;
            }
        }

        // TODO: remove this if MHL display can do partial update by DPI
        if (((HWC_DISPLAY_EXTERNAL == job->disp_ori_id) || (HWC_DISPLAY_EXTERNAL_1 == job->disp_ori_id)) &&
            (HWC_MIRROR_SOURCE_INVALID == job->disp_mir_id)) // extension mode
        {
            DispatcherJob* mir_job = m_curr_jobs[HWC_DISPLAY_PRIMARY].get();
            if (NULL != mir_job)
            {
                // record main display orientation in disp_mir_rot for MHL extension mode
                job->disp_mir_rot = mir_job->disp_ori_rot;
                if (job->disp_ori_rot != mir_job->disp_ori_rot)
                {
                    HWC_LOGD("change MHL ori %d->%d", job->disp_ori_rot, mir_job->disp_ori_rot);
                }
            }
        }

        // check if fbt handle is valid
        if (job->fbt_exist)
        {
            sp<HWCLayer>&& fbt_layer = display->getClientTarget();
            if (fbt_layer->getHandle() == NULL)
            {
                int idx = job->num_ui_layers + job->num_mm_layers + job->num_glai_layers;
                HWLayer* fbt_hw_layer = &job->hw_layers[idx];
                fbt_hw_layer->enable = false;
#ifdef MTK_HWC_PROFILING
                fbt_hw_layer->fbt_input_layers = 0;
                fbt_hw_layer->fbt_input_bytes  = 0;
#endif

                job->fbt_exist = false;

                if (0 == (job->num_mm_layers + job->num_ui_layers + job->num_glai_layers))
                {
                    display->clearAllFences();
                    job->enable = false;
                    HWC_LOGW("(%" PRIu64 ") SET/bypass/no layers", dpy);
                    return;
                }
            }
        }

#ifndef MTK_USER_BUILD
        HWC_ATRACE_JOB("set", dpy, job->fbt_exist, job->num_ui_layers, job->num_mm_layers, job->num_glai_layers);
#endif

        if (DisplayManager::m_profile_level & PROFILE_DBG_WFD)
        {
            char atrace_tag[256] = "";
            snprintf(atrace_tag, sizeof(atrace_tag), "SET-OVL%" PRIu64, dpy);
            HWC_ATRACE_ASYNC_BEGIN(atrace_tag, job->sequence);
        }

        DbgLogger* logger = &Debugger::getInstance().m_logger->set_info[static_cast<size_t>(dpy)];

        logger->printf("(%" PRIu64 ") SET job=%" PRIu64 "/max=%u/fbt=%d/ui=%d/mm=%d/glai=%d/del=%zu/mir=%d/black=%d",
            dpy, job->sequence, job->num_layers,
            job->fbt_exist, job->num_ui_layers, job->num_mm_layers, job->num_glai_layers,
            display->getInvisibleLayersSortedByZ().size(), job->disp_mir_id,
            job->is_black_job);

        // verify
        // 1. if outbuf should be used for virtual display
        // 2. if any input buffer is dirty for physical display
        m_workers[dpy].post_handler->set(display, job);

        logger->tryFlush();

        if ((job->post_state & HWC_POST_CONTINUE_MASK) == 0)
        {
            if (job->num_mm_layers)
            {
                if (HWC_MIRROR_SOURCE_INVALID != job->disp_mir_id)
                {
                    HWC_LOGD("(%" PRIu64 ") disable mirror because mirror dst not post", job->disp_ori_id);
                    disableMirror(display, job);
                }
            }
            // disable job
            job->enable = false;
            return;
        }

        // marked job that should be processed
        job->enable = true;

        if (!isNoDispatchThread())
        {
            dupInputBufferHandle(job);
        }

        if (display->getMirrorSrc() == -1)
        {
            m_workers[dpy].composer->set(display, job);
        }

        if (job->need_av_grouping && (job->num_processed_mm_layers == 0 || job->num_mm_layers > 1))
        {
            job->need_av_grouping = false;
        }

#ifdef MTK_IN_DISPLAY_FINGERPRINT
        if (display->getIsHBM()) {
            job->is_HBM = true;
        }
#endif

        if (job->wdma_status != HWC_WDMA_STATUS_DISABLE)
        {
            // ask AIBLD first since it has higher priority
            if (job->wdma_status & HWC_WDMA_STATUS_AIBLD)
            {
                AiBluLightDefender::getInstance().onSetJob(job, m_workers[dpy].ovl_engine, display);
            }
            // if AIBLD used it, display dump should not use it
            if (!job->aibld_enable && (job->wdma_status & HWC_WDMA_STATUS_DISPLAY_DUMP))
            {
                std::shared_ptr<DisplayDump> disp_dump = display->getDisplayDump();
                if (disp_dump != nullptr)
                {
                    disp_dump->onSetJob(job, m_workers[dpy].ovl_engine);
                }
            }
        }
    }
}

void HWCDispatcher::clearUsedJob(const hwc2_display_t& dpy, DispatcherJob* job)
{
    if (job == NULL)
    {
        return;
    }

    if (!isNoDispatchThread())
    {
        if (dpy == HWC_DISPLAY_VIRTUAL || job->display_dump_enable)
        {
            freeDuppedBufferHandle(job->hw_outbuf.handle);
        }

        if (job->enable)
        {
            freeDuppedInputBufferHandle(job);
        }
    }
    else if (job->display_dump_enable)
    {
        freeDuppedBufferHandle(job->hw_outbuf.handle);
    }

    job->resetData();
}

void HWCDispatcher::handleJob(const hwc2_display_t& dpy, sp<DispatcherJob>& job)
{
    WorkerCluster& worker(m_workers[dpy]);

    AutoMutex l(worker.plug_lock_loop);

    sp<OverlayEngine> ovl_engine = worker.ovl_engine;

    HWC_LOGD("(%" PRIu64 ") Handle job %" PRIu64, dpy, job->sequence);

    if (job->enable)
    {
        {
            if (ovl_engine != NULL) ovl_engine->setHandlingJob(job.get());

#ifndef MTK_USER_BUILD
            HWC_ATRACE_JOB("trigger",
                dpy, job->fbt_exist, job->num_ui_layers, job->num_mm_layers, job->num_glai_layers);
#endif

            sp<LayerComposer> composer = worker.composer;

            if (composer != NULL)
            {
                if (job->num_ui_layers || job->fbt_exist || job->num_mm_layers || job->num_glai_layers)
                {
                    composer->trigger(job.get());
                    worker.post_handler->process(job.get());
                }
                else if (job->is_black_job)
                {
                    HWC_LOGE("(%" PRIu64 ") Handle black job(%d)", dpy, job->is_black_job);
                    composer->trigger(job.get());
                    worker.post_handler->process(job.get());
                }
                else if (job->num_ui_layers == 0 &&
                         job->num_mm_layers == 0 &&
                         job->num_glai_layers == 0 &&
                         job->fbt_exist == 0)
                {
                    HWC_LOGE("(%" PRIu64 ") Handle a job with no visible layer", dpy);
                    composer->trigger(job.get());
                    worker.post_handler->process(job.get());
                }
                else
                {
                    composer->nullop();
                }
            }
        }
    }
#ifndef MTK_USER_BUILD
    else
    {
        HWC_ATRACE_NAME("dispatcher_bypass");
    }
#endif
    clearUsedJob(dpy, job.get());
}

void HWCDispatcher::trigger(const hwc2_display_t& dpy)
{
    AutoMutex l(m_workers[dpy].plug_lock_main);

    if (m_workers[dpy].enable && m_curr_jobs[dpy])
    {
        if (isNoDispatchThread())
        {
            handleJob(dpy, m_curr_jobs[dpy]);
        }
        else
        {
            if (m_workers[dpy].dp_thread->getQueueSize() > 5)
            {
                m_workers[dpy].force_wait = true;
                HWC_LOGW("(%" PRIu64 ") Jobs have piled up, wait for clearing!!", dpy);
            }

            m_workers[dpy].dp_thread->trigger(m_curr_jobs[dpy]);
        }

        // force job to return to pool
        m_curr_jobs[dpy] = NULL;
        m_workers[dpy].plug_cond_job_inuse.signal();
    }

    // [WORKAROUND]
    if (m_workers[dpy].force_wait)
    {
        m_workers[dpy].dp_thread->wait();
        m_workers[dpy].force_wait = false;
    }
}

void HWCDispatcher::releaseResourceLocked(uint64_t dpy, uint32_t drm_id_crtc)
{
    // wait until all threads are idle
    if (m_workers[dpy].idle_thread != NULL)
    {
        m_workers[dpy].idle_thread->requestExit();
        m_workers[dpy].idle_thread->setEnabled(false);
        m_workers[dpy].idle_thread->join();
        m_workers[dpy].idle_thread = NULL;
    }

    if (m_workers[dpy].dp_thread != NULL)
    {
        m_workers[dpy].dp_thread->wait();
        m_workers[dpy].dp_thread->stop();

        if (dpy != HWC_DISPLAY_VIRTUAL)
        {
            removeVSyncListener(dpy, m_workers[dpy].dp_thread);
        }
        else
        {
            removeVSyncListener(HWC_DISPLAY_PRIMARY, m_workers[dpy].dp_thread);
        }
        m_workers[dpy].dp_thread = NULL;
    }

    m_workers[dpy].composer = NULL;

    m_workers[dpy].post_handler = NULL;
    if (dpy != HWC_DISPLAY_VIRTUAL)
        removeVSyncListener(dpy, m_workers[dpy].ovl_engine);

    m_workers[dpy].ovl_engine->stop();
    m_workers[dpy].ovl_engine->setPowerMode(drm_id_crtc, HWC2_POWER_MODE_OFF);
    m_workers[dpy].ovl_engine = NULL;

    if (m_workers[dpy].m_job_pool)
    {
        delete m_workers[dpy].m_job_pool;
    }

    HWCMediator::getInstance().getOvlDevice(dpy)->waitAllJobDone(dpy);

    HWC_LOGD("Release resource (dpy=%" PRIu64 ")", dpy);

    SessionInfo info;
    if (HWCMediator::getInstance().getOvlDevice(dpy)->getOverlaySessionInfo(dpy, drm_id_crtc, &info) != INVALID_OPERATION)
    {
        // session still exists after destroying overlay engine
        // something goes wrong in display driver?
        HWC_LOGW("Session is not destroyed (dpy=%" PRIu64 ")", dpy);
    }
}

void HWCDispatcher::onPlugIn(uint64_t dpy, uint32_t drm_id_crtc, uint32_t drm_id_connector,
                             bool is_internal, uint32_t width, uint32_t height)
{
#ifndef MTK_USER_BUILD
    HWC_ATRACE_CALL();
#endif

    if (dpy >= DisplayManager::MAX_DISPLAYS)
    {
        HWC_LOGE("Invalid display(%" PRIu64 ") is plugged !!", dpy);
        return;
    }

    {
        AutoMutex ll(m_workers[dpy].plug_lock_loop);
        AutoMutex lm(m_workers[dpy].plug_lock_main);
        AutoMutex lv(m_workers[dpy].plug_lock_vsync);

        if (m_alloc_disp_ids.test(static_cast<size_t>(dpy)))
        {
            HWC_LOGE("Display(%" PRIu64 ") is already connected !!", dpy);
            return;
        }
        if (Platform::getInstance().m_config.dynamic_switch_path && dpy == HWC_DISPLAY_VIRTUAL)
        {
            if (m_workers[HWC_DISPLAY_PRIMARY].enable)
            {
                HWC_ATRACE_NAME("waitAllJobDone");
                if (m_workers[HWC_DISPLAY_PRIMARY].dp_thread)
                {
                    m_workers[HWC_DISPLAY_PRIMARY].dp_thread->wait();
                }
                m_workers[HWC_DISPLAY_PRIMARY].ovl_engine->wait();
                HWCMediator::getInstance().getOvlDevice(HWC_DISPLAY_PRIMARY)->waitAllJobDone(HWC_DISPLAY_PRIMARY);
                DisplayManager::getInstance().display_set_commit_done_state(HWC_DISPLAY_PRIMARY, false);
                DisplayManager::getInstance().display_set_commit_done_state(HWC_DISPLAY_VIRTUAL, false);
            }
            else
            {
                HWC_LOGW("HWC_DISPLAY_PRIMARY is not enable when create VDS!!!");
            }
        }

        m_workers[dpy].enable = false;
        m_workers[dpy].is_internal = is_internal;
        m_workers[dpy].ovl_engine = new OverlayEngine(dpy, drm_id_crtc, drm_id_connector, width, height);
        m_workers[dpy].drm_id_cur_crtc = drm_id_crtc;

        struct sched_param param = {0};
        param.sched_priority = 1;
        if (sched_setscheduler(m_workers[dpy].ovl_engine->getTid(), SCHED_FIFO, &param) != 0) {
            HWC_LOGE("Couldn't set SCHED_FIFO for ovl_engine");
        }

        if (m_workers[dpy].ovl_engine == NULL ||
            !m_workers[dpy].ovl_engine->isEnable())
        {
            m_workers[dpy].ovl_engine = NULL;
            HWC_LOGE("Failed to create OverlayEngine (dpy=%" PRIu64 ") !!", dpy);
            return;
        }

        if (dpy != HWC_DISPLAY_VIRTUAL)
        {
            m_workers[dpy].post_handler =
                new PhyPostHandler(this, dpy, m_workers[dpy].ovl_engine);
            registerVSyncListener(dpy, m_workers[dpy].ovl_engine);
        }
        else
        {
            m_workers[dpy].post_handler =
                new VirPostHandler(this, dpy, m_workers[dpy].ovl_engine);
        }

        if (m_workers[dpy].post_handler == NULL)
        {
            HWC_LOGE("Failed to create PostHandler (dpy=%" PRIu64 ") !!", dpy);
            releaseResourceLocked(dpy, drm_id_crtc);
            return;
        }

        if (!isNoDispatchThread())
        {
            m_workers[dpy].dp_thread = new DispatchThread(dpy);
            param.sched_priority = 1;
            if (sched_setscheduler(m_workers[dpy].dp_thread->getTid(), SCHED_FIFO, &param) != 0) {
                HWC_LOGE("Couldn't set SCHED_FIFO for dp_thread");
            }
            if (m_workers[dpy].dp_thread == NULL)
            {
                HWC_LOGE("Failed to create DispatchThread (dpy=%" PRIu64 ") !!", dpy);
                releaseResourceLocked(dpy, drm_id_crtc);
                return;
            }
        }

        if (!DisplayManager::getInstance().getDisplayData(dpy)->trigger_by_vsync)
        {
            if (m_workers[dpy].dp_thread)
            {
                if (is_internal)
                {
                    registerVSyncListener(dpy, m_workers[dpy].dp_thread);
                }
                else
                {
                    registerVSyncListener(HWC_DISPLAY_PRIMARY, m_workers[dpy].dp_thread);
                }
            }
        }
        else
        {
            if (m_workers[dpy].dp_thread)
            {
                registerVSyncListener(dpy, m_workers[dpy].dp_thread);
            }
            nsecs_t refresh = DisplayManager::getInstance().getDisplayData(dpy)->refresh;
            if (refresh > 0)
            {
                m_workers[dpy].idle_thread = new IdleThread(dpy);
                if (m_workers[dpy].idle_thread != NULL)
                {
                    m_workers[dpy].idle_thread->initialize(refresh);
                }
            }
            else
            {
                HWC_LOGI("idle thread doesn't create due to 0 refresh rate (dpy=%" PRIu64 ") !!", dpy);
            }
        }

        m_workers[dpy].composer = new LayerComposer(dpy, m_workers[dpy].ovl_engine);
        if (m_workers[dpy].composer == NULL)
        {
            HWC_LOGE("Failed to create LayerComposer (dpy=%" PRIu64 ") !!", dpy);
            releaseResourceLocked(dpy, drm_id_crtc);
            return;
        }

        unsigned int maxInputNum = m_workers[dpy].ovl_engine->getMaxInputNum();
        std::string poolName("dispatcher_job_");
        poolName += std::to_string(dpy);
        m_workers[dpy].m_job_pool = new ObjectPool<DispatcherJob>(poolName, 5,
                                                                  [maxInputNum](){
                                                                      return new DispatcherJob(maxInputNum);
                                                                  });
        if (m_workers[dpy].m_job_pool == NULL)
        {
            HWC_LOGE("Failed to create m_job_pool (dpy=%" PRIu64 ") !!", dpy);
            releaseResourceLocked(dpy, drm_id_crtc);
            return;
        }

        m_curr_jobs[dpy] = NULL;
        DisplayManager::getInstance().setDisplayPowerState(dpy, HWC2_POWER_MODE_OFF);

        m_alloc_disp_ids.set(static_cast<size_t>(dpy));
        m_workers[dpy].enable = true;

        // create mirror buffer of main display if needed
        if (HWC_DISPLAY_PRIMARY < dpy &&
            Platform::getInstance().m_config.mirror_state != MIRROR_DISABLED)
        {
            const DisplayData* disp_data = DisplayManager::getInstance().getDisplayData(dpy);
            unsigned int format = (HWC_DISPLAY_VIRTUAL == dpy && HWC_DISPLAY_WIRELESS != disp_data->subtype) ?
                HAL_PIXEL_FORMAT_RGB_888 : HAL_PIXEL_FORMAT_YUYV;
            {
                // force output RGB format for debuging
                char value[PROPERTY_VALUE_MAX] = {0};
                property_get("vendor.debug.hwc.force_rgb_output", value, "0");
                if (0 != atoi(value))
                {
                    HWC_LOGW("[DEBUG] force RGB format!!");
                    format = HAL_PIXEL_FORMAT_RGB_888;
                }
            }
            HWC_LOGD("set output format 0x%x !!", format);

            if (m_workers[HWC_DISPLAY_PRIMARY].ovl_engine != NULL)
            {
                m_workers[HWC_DISPLAY_PRIMARY].ovl_engine->createOutputQueue(format, false);
            }
        }
    }
}

void HWCDispatcher::onPlugOut(uint64_t dpy, uint32_t drm_id_crtc)
{
#ifndef MTK_USER_BUILD
    HWC_ATRACE_CALL();
#endif

    if (dpy >= DisplayManager::MAX_DISPLAYS)
    {
        HWC_LOGE("Invalid display(%" PRIu64 ") is unplugged !!", dpy);
        return;
    }

    {
        // lock main here to prevent get new job
        AutoMutex lm(m_workers[dpy].plug_lock_main);

        // if current job is still in use, should wait until it's consumed
        while (m_curr_jobs[dpy].get() != nullptr)
        {
            HWC_LOGI("%s(), dpy %" PRIu64 ", Waiting for current job to be consumed", __FUNCTION__, dpy);
            m_workers[dpy].plug_cond_job_inuse.wait(m_workers[dpy].plug_lock_main); // during wait will unlock
            HWC_LOGI("%s(), dpy %" PRIu64 ", job is consumed", __FUNCTION__, dpy);
        }

        AutoMutex lv(m_workers[dpy].plug_lock_vsync);

        // flush pending display job of mirror source before
        // destroy display session
        {
            DispatcherJob* ori_job = m_curr_jobs[dpy].get();

            if ( ori_job && (HWC_MIRROR_SOURCE_INVALID != ori_job->disp_mir_id))
            {
                const unsigned int mir_dpy = static_cast<unsigned int>(ori_job->disp_mir_id);

                AutoMutex l(m_workers[mir_dpy].plug_lock_main);

                if (m_workers[mir_dpy].enable)
                {
                    DispatcherJob* mir_job = m_curr_jobs[mir_dpy].get();

                    if (mir_job && mir_job->enable && mir_job->mirrored)
                    {
                        if (m_workers[mir_dpy].dp_thread)
                        {
                            m_workers[mir_dpy].dp_thread->trigger(mir_job);
                        }
                        m_curr_jobs[mir_dpy] = NULL;
                    }

                    if (m_workers[mir_dpy].dp_thread)
                    {
                        m_workers[mir_dpy].dp_thread->wait();
                    }
                    m_workers[mir_dpy].ovl_engine->wait();
                    HWCMediator::getInstance().getOvlDevice(mir_dpy)->waitAllJobDone(mir_dpy);
                }
            }
        }

        if (m_workers[dpy].enable)
        {
            releaseResourceLocked(dpy, drm_id_crtc);
        }
        else
        {
            HWC_LOGE("Failed to disconnect invalid display(%" PRIu64 ") !!", dpy);
        }

        AutoMutex ll(m_workers[dpy].plug_lock_loop);

        m_alloc_disp_ids.reset(static_cast<size_t>(dpy));
        m_workers[dpy].enable = false;

        // release mirror buffer of main display if needed
        if (HWC_DISPLAY_PRIMARY < dpy && (m_alloc_disp_ids.count() == 1) && Platform::getInstance().m_config.mirror_state != MIRROR_DISABLED)
        {
            if (m_workers[HWC_DISPLAY_PRIMARY].ovl_engine != NULL)
            {
                m_workers[HWC_DISPLAY_PRIMARY].ovl_engine->releaseOutputQueue();
            }
        }
    }
}

void HWCDispatcher::setPowerMode(uint64_t dpy, uint32_t drm_id_crtc, int mode)
{
#ifndef MTK_USER_BUILD
    HWC_ATRACE_CALL();
#endif

    if (!HWCMediator::getInstance().isValidateSeparate() &&
        (HWC2_POWER_MODE_OFF == mode || HWC2_POWER_MODE_DOZE_SUSPEND == mode))
    {
        setSessionMode(HWC_DISPLAY_PRIMARY, false);
    }

    if (dpy != HWC_DISPLAY_VIRTUAL)
    {
        AutoMutex l(m_workers[dpy].plug_lock_main);

        if (HWC2_POWER_MODE_OFF == mode || HWC2_POWER_MODE_DOZE_SUSPEND == mode)
        {
            if (m_workers[dpy].dp_thread != NULL)
            {
                m_workers[dpy].dp_thread->wait();
            }
            if (m_workers[dpy].ovl_engine != NULL)
            {
                m_workers[dpy].ovl_engine->wait();
            }
            HWCMediator::getInstance().getOvlDevice(dpy)->waitAllJobDone(dpy);
        }

        if (m_workers[dpy].enable)
        {
            m_workers[dpy].ovl_engine->setPowerMode(drm_id_crtc, mode);
        }

        if (HWC2_POWER_MODE_OFF == mode || HWC2_POWER_MODE_DOZE_SUSPEND == mode)
        {
            if (m_workers[dpy].composer != NULL) m_workers[dpy].composer->nullop();
        }
    }
}

void HWCDispatcher::onVSync(uint64_t dpy)
{
#ifndef MTK_USER_BUILD
    HWC_ATRACE_CALL();
#endif

    {
        AutoMutex l(m_vsync_lock);

        // dispatch vsync signal to listeners
        const size_t count = m_vsync_callbacks[static_cast<unsigned int>(dpy)].size();
        for (size_t i = 0; i < count; i++)
        {
            const sp<HWCVSyncListener>& callback(m_vsync_callbacks[static_cast<unsigned int>(dpy)][i]);
            callback->onVSync();
        }
    }
}

void HWCDispatcher::registerVSyncListener(uint64_t dpy, const sp<HWCVSyncListener>& listener)
{
    AutoMutex l(m_vsync_lock);

    m_vsync_callbacks.editItemAt(static_cast<unsigned int>(dpy)).add(listener);
    HWC_LOGD("(%" PRIu64 ") register HWCVSyncListener", dpy);
}

void HWCDispatcher::removeVSyncListener(uint64_t dpy, const sp<HWCVSyncListener>& listener)
{
    AutoMutex l(m_vsync_lock);

    m_vsync_callbacks.editItemAt(static_cast<unsigned int>(dpy)).remove(listener);
    HWC_LOGD("(%" PRIu64 ") remove HWCVSyncListener", dpy);
}

void HWCDispatcher::dump(String8 *dump_str)
{
    for (uint64_t dpy = 0; dpy < DisplayManager::MAX_DISPLAYS; dpy++)
    {
        AutoMutex l(m_workers[dpy].plug_lock_main);

        if (m_workers[dpy].ovl_engine != NULL && m_workers[dpy].enable)
        {
            m_workers[dpy].ovl_engine->dump(dump_str);
        }
    }
}

void HWCDispatcher::ignoreJob(uint64_t dpy, bool ignore)
{
    AutoMutex l(m_workers[dpy].plug_lock_loop);
    m_workers[dpy].ignore_job = ignore;
}

int HWCDispatcher::getOvlEnginePowerModeChanged(const uint64_t& dpy) const
{
    if(m_workers[dpy].ovl_engine != NULL)
        return m_workers[dpy].ovl_engine->getPowerModeChanged();
    else
        return 0;
}

void HWCDispatcher::decOvlEnginePowerModeChanged(const uint64_t& dpy) const
{
    if(m_workers[dpy].ovl_engine != NULL)
        return m_workers[dpy].ovl_engine->decPowerModeChanged();
}

void HWCDispatcher::dupInputBufferHandle(DispatcherJob* job)
{
    if (HWC_MIRROR_SOURCE_INVALID != job->disp_mir_id)
    {
        return;
    }
    uint32_t total_num = job->num_layers;
    for (uint32_t i = 0; i < total_num; i++)
    {
        HWLayer* hw_layer = &job->hw_layers[i];

        if (HWC_LAYER_TYPE_DIM == hw_layer->type) continue;
        // this layer is not enable
        if (!hw_layer->enable) continue;

        buffer_handle_t out_hnd = NULL;
        dupBufferHandle(hw_layer->priv_handle.handle, &out_hnd);
        getPrivateHandleBuff(out_hnd, &hw_layer->priv_handle);
        addBufToBufRecorder(hw_layer->priv_handle.handle);
    }
}

void HWCDispatcher::freeDuppedInputBufferHandle(DispatcherJob* job)
{
    if (HWC_MIRROR_SOURCE_INVALID != job->disp_mir_id)
    {
        return;
    }
    uint32_t total_num = job->num_layers;
    for (uint32_t i = 0; i < total_num; i++)
    {
        HWLayer* hw_layer = &job->hw_layers[i];

        if (HWC_LAYER_TYPE_DIM == hw_layer->type) continue;

        // this layer is not enable
        if (!hw_layer->enable) continue;

        removeBufFromBufRecorder(hw_layer->priv_handle.handle);
        freeDuppedBufferHandle(hw_layer->priv_handle.handle);
    }
}

void HWCDispatcher::prepareMirror(const std::vector<sp<HWCDisplay> >& displays)
{
    // In the function prepareMirror(), all of the flows and functions
    // like configMirrorJob(), configMirrorOutput() based on the result
    // of checkMirrorPath to decide do things or not

    for (auto& hwc_display : displays)
    {
        if (!hwc_display->isValid())
        continue;

        DispatcherJob* job = m_curr_jobs[hwc_display->getId()].get();
        if (NULL != job)
        {
            if (hwc_display->getMirrorSrc() != -1)
            {
                job->disp_mir_id = hwc_display->getMirrorSrc();
                //job->disp_ori_rot   = (getMtkFlags() & HWC_ORIENTATION_MASK) >> 16;
                job->mir_disp_data = m_curr_jobs[static_cast<unsigned int>(job->disp_mir_id)]->disp_data;
                configMirrorJob(job);
            }
            else
            {
                job->disp_mir_id    = HWC_MIRROR_SOURCE_INVALID;
                //job->disp_ori_rot   = (getMtkFlags() & HWC_ORIENTATION_MASK) >> 16;
                job->mir_disp_data  = nullptr;
            }
        }
    }

    for (auto& hwc_display : displays)
    {
        if (!hwc_display->isValid())
            continue;

        if (HWC_DISPLAY_VIRTUAL == hwc_display->getId())
            continue;

        DispatcherJob* job = m_curr_jobs[hwc_display->getId()].get();
        if (NULL != job)
        {
            configMirrorOutput(job, DisplayManager::getInstance().getSupportedColorMode(hwc_display->getId()));
        }
    }

    for (auto& hwc_display : displays)
    {
        if (!hwc_display->isValid())
            continue;

        if (hwc_display->getId() <= HWC_DISPLAY_PRIMARY)
            continue;

        DispatcherJob* job = m_curr_jobs[hwc_display->getId()].get();
        if (NULL != job && (HWC_MIRROR_SOURCE_INVALID != hwc_display->getMirrorSrc()))
        {
            const unsigned int mir_id = static_cast<unsigned int>(job->disp_mir_id);
            DispatcherJob* mir_src_job = m_curr_jobs[mir_id].get();
            if (NULL != mir_src_job && mir_src_job->need_output_buffer)
            {
                m_workers[hwc_display->getId()].composer->set(hwc_display, job);
                m_workers[mir_id].post_handler->setMirror(mir_src_job, job);
            }
            else
            {
                HWC_LOGD("%" PRIu64 " disable mirror because mirror src not avaliable", hwc_display->getId());
                hwc_display->setMirrorSrc(HWC_MIRROR_SOURCE_INVALID);
                job->disp_mir_id = HWC_MIRROR_SOURCE_INVALID;
                if (NULL != mir_src_job)
                {
                    mir_src_job->mirrored = false;
                }
            }
        }
    }
}
// ---------------------------------------------------------------------------

DispatchThread::DispatchThread(uint64_t dpy)
    : m_disp_id(dpy)
    , m_tid(0)
{
    m_thread_name = std::string("Dispatcher_") + std::to_string(dpy);
    m_queue_name = std::string("JobQueue_") + std::to_string(dpy);
    m_semaphore_name = std::string("Semaphore_of_Dispatcher_") + std::to_string(dpy);

    m_perf_mc_dispatcher_str = std::string("p_mc_dispatcher_") + std::to_string(dpy);
    m_perf_mc_ovl_str = std::string("p_mc_ovl_") + std::to_string(dpy);
    m_perf_scenario_str = std::string("p_scenario_") + std::to_string(dpy);
    m_perf_remain_time_str = std::string("pd_remain_time_") + std::to_string(dpy);
    m_perf_target_cpu_mhz_str = std::string("pd_target_cpu_mhz_") + std::to_string(dpy);
    m_perf_uclamp_str = std::string("pd_uclamp_") + std::to_string(dpy);
    m_perf_extension_time_str = std::string("pd_extension_time_") + std::to_string(dpy);
}

void DispatchThread::onFirstRef()
{
    run(m_thread_name.c_str(), PRIORITY_URGENT_DISPLAY);
}

void DispatchThread::trigger(sp<DispatcherJob> job)
{
#ifndef MTK_USER_BUILD
    HWC_ATRACE_NAME("dispatcher_set");
#endif

    AutoMutex l(m_lock);

    if (job != NULL)
    {
        m_job_queue.push_back(job);
        int32_t num_of_job = static_cast<int32_t>(m_job_queue.size());
        HWC_ATRACE_INT(m_queue_name.c_str(), num_of_job);
        m_state = HWC_THREAD_TRIGGER;
        sem_post(&m_event);
        if (Platform::getInstance().m_config.dbg_switch & HWC_DBG_SWITCH_DEBUG_SEMAPHORE)
        {
            int32_t sem_value;
            sem_getvalue(&m_event, &sem_value);
            HWC_ATRACE_INT(m_semaphore_name.c_str(), sem_value);
            // sem_value should not less then num_of_job minus 1
            // (immediately decrease by sem_wait before check, but not consume job queue yet)
            if (sem_value < (num_of_job - 1) ||
                sem_value > num_of_job)
            {
                if (isUserLoad())
                {
                    HWC_LOGE("(%" PRIu64 ") DispatchThread trigger when sem_value(%d) do not match num_of_job(%d)", m_disp_id, sem_value, num_of_job);
                }
                else
                {
                    LOG_FATAL("(%" PRIu64 ") DispatchThread trigger when sem_value(%d) do not match num_of_job(%d)", m_disp_id, sem_value, num_of_job);
                }
            }
        }
    }
}

bool DispatchThread::dropJob()
{
    bool res = false;

    HWCDispatcher::WorkerCluster& worker(
                HWCDispatcher::getInstance().m_workers[m_disp_id]);
    {
        // ignore_job is set by DisplayManager when disconnect this display, so we do not care
        // these jobs. They will be dropped in thread loop.
        AutoMutex l(worker.plug_lock_loop);
        res |= worker.ignore_job;
    }

    if (res)
    {
        sp<DispatcherJob> job = NULL;
        {
            AutoMutex l(m_lock);
            Fifo::iterator front(m_job_queue.begin());
            if (front)
            {
                job = *front;
                m_job_queue.erase(front);
                HWC_ATRACE_INT(m_queue_name.c_str(), static_cast<int32_t>(m_job_queue.size()));
            }
            else
            {
                LOG_FATAL("%s(), front invalid", __FUNCTION__);
                return false;
            }
        }
        HWC_LOGD("(%" PRIu64 ") Drop a job %" PRIu64, m_disp_id, job->sequence);

        if (job->enable)
        {
            AutoMutex l(worker.plug_lock_loop);
            if (job->num_mm_layers || job->num_ui_layers || job->num_glai_layers)
            {
                if (worker.composer != NULL)
                {
                    worker.composer->cancelLayers(job.get());
                }
                else
                {
                    HWC_LOGE("No LayerComposer");
                }
            }
        }
        HWCDispatcher::getInstance().clearUsedJob(m_disp_id, job.get());
    }

    return res;
}

size_t DispatchThread::getQueueSize()
{
    AutoMutex l(m_lock);
    return m_job_queue.size();
}

void DispatchThread::calculatePerf(DispatcherJob* job)
{
    if (!job)
    {
        return;
    }

    if ((Platform::getInstance().m_config.plat_switch & HWC_PLAT_SWITCH_USE_PERF) == 0)
    {
        return;
    }

    if (job->sf_target_ts <= 0)
    {
        return;
    }

    if (Platform::getInstance().m_config.uclamp_cpu_table.empty())
    {
        HWC_LOGW("uclamp_cpu_table empty");
        return;
    }

    if (Platform::getInstance().m_config.hwc_mcycle_table.empty())
    {
        HWC_LOGW("hwc_mcycle_table empty");
        return;
    }

    // we only calculate uclamp for litter core, so ignore the calculation when the cpu set
    // is not little core.
    if (job->cpu_set != HWC_CPUSET_LITTLE && job->cpu_set != HWC_CPUSET_NONE)
    {
        return;
    }

    // get million cpu cycle needed
    const HwcMCycleInfo& info = job->mc_info ? *job->mc_info : getScenarioMCInfo(job);
    float dispatcher_mc = info.dispatcher_mc;
    job->ovl_mc = info.ovl_mc;
    job->ovl_mc_atomic_ratio = info.ovl_mc_atomic_ratio;
    job->ovl_wo_atomic_work_time = info.ovl_wo_atomic_work_time;

    HWC_ATRACE_INT(m_perf_scenario_str.c_str(), info.id);
    HWC_ATRACE_INT(m_perf_mc_dispatcher_str.c_str(), static_cast<int32_t>(dispatcher_mc * 1000));
    HWC_ATRACE_INT(m_perf_mc_ovl_str.c_str(), static_cast<int32_t>(job->ovl_mc * 1000));

    // calculate dispatcher uclamp
    const nsecs_t cur_time = systemTime();
    nsecs_t remain_time = job->sf_target_ts - cur_time;

    uint32_t dispatcher_uclamp = UINT32_MAX;

    if (remain_time <= 0)
    {
        nsecs_t period = job->disp_data->refresh;
        nsecs_t dividend = -remain_time;
        nsecs_t extension_time = ((dividend / period) + 1) * period;
        remain_time += extension_time;
        HWC_ATRACE_INT64(m_perf_extension_time_str.c_str(), extension_time);
    }

    HWC_ATRACE_INT64(m_perf_remain_time_str.c_str(), static_cast<int64_t>(remain_time));

    remain_time = std::min(remain_time, info.dispatcher_target_work_time);

    uint32_t target_cpu_mhz = calculateCpuMHz(dispatcher_mc, remain_time);

    dispatcher_uclamp = cpuMHzToUClamp(target_cpu_mhz);

    HWC_ATRACE_INT(m_perf_target_cpu_mhz_str.c_str(), static_cast<int32_t>(target_cpu_mhz));

    // set uclamp for dispatcher
    if (dispatcher_uclamp != m_perf_prev_uclamp_min)
    {
        m_perf_prev_uclamp_min = dispatcher_uclamp;

        uclamp_task(m_tid, dispatcher_uclamp);
        HWC_ATRACE_INT(m_perf_uclamp_str.c_str(), static_cast<int32_t>(dispatcher_uclamp));
    }
}

bool DispatchThread::threadLoop()
{
    sem_wait(&m_event);

    if (Platform::getInstance().m_config.dbg_switch & HWC_DBG_SWITCH_DEBUG_SEMAPHORE)
    {
        AutoMutex l(m_lock);
        int32_t sem_value;
        sem_getvalue(&m_event, &sem_value);
        HWC_ATRACE_INT(m_semaphore_name.c_str(), sem_value);
    }

    m_tid = gettid();

    sp<DispatcherJob> job = NULL;

    {
        AutoMutex l(m_lock);
        if (m_job_queue.empty())
        {
            HWC_LOGW("(%" PRIu64 ") Job queue is empty, it should only be printed when plug out", m_disp_id);
            m_state = HWC_THREAD_IDLE;
            m_condition.signal();
            return true;
        }
    }

#ifndef MTK_USER_BUILD
    HWC_ATRACE_NAME("dispatcher_loop");
#endif

    if (dropJob())
    {
        AutoMutex l(m_lock);
        if (m_job_queue.empty())
        {
            m_state = HWC_THREAD_IDLE;
            m_condition.signal();
        }
        return true;
    }

    {
        AutoMutex l(m_lock);
        Fifo::iterator front(m_job_queue.begin());
        if (front)
        {
            job = *front;
        }
        else
        {
            LOG_FATAL("%s(), front invalid", __FUNCTION__);
            return true;
        }
        m_job_queue.erase(front);
        HWC_ATRACE_INT(m_queue_name.c_str(), static_cast<int32_t>(m_job_queue.size()));
    }

    calculatePerf(job.get());
    updateCpuSet(m_tid, job.get());

    // handle jobs
    // 1. set synchronization between composer threads
    // 2. trigger ui/mm threads to compose layers
    // 3. wait until the composition of ui/mm threads is done
    // 4. clear used job
    {
        HWCDispatcher::getInstance().handleJob(m_disp_id, job);
    }

    {
        AutoMutex l(m_lock);
        if (m_job_queue.empty())
        {
            m_state = HWC_THREAD_IDLE;
            m_condition.signal();
        }
    }

    return true;
}

void DispatchThread::waitNextVSyncLocked(uint64_t dpy)
{
    // TODO: pass display id to DisplayManager to get the corresponding vsync signal

    // It's a warning that how long HWC does not still receive the VSync
    const nsecs_t VSYNC_TIMEOUT_THRESHOLD_NS = 4000000;

    // request next vsync
    if (DisplayManager::getInstance().getDisplayData(dpy)->trigger_by_vsync)
    {
        DisplayManager::getInstance().requestNextVSync(dpy);
    }
    else
    {
        DisplayManager::getInstance().requestNextVSync(HWC_DISPLAY_PRIMARY);
    }

    // There are various VSync periods for each display.
    // Especially, the vsync rate of MHL is dynamical and can be 30fps or 60fps.
    const nsecs_t refresh = DisplayManager::getInstance().getDisplayData(dpy)->trigger_by_vsync ?
        DisplayManager::getInstance().getDisplayData(dpy)->refresh : ms2ns(16) ;
    if (m_vsync_cond.waitRelative(m_vsync_lock, refresh + VSYNC_TIMEOUT_THRESHOLD_NS) == TIMED_OUT)
    {
        HWC_LOGW("(%" PRIu64 ") Timed out waiting for vsync...", dpy);
    }
}

void DispatchThread::onVSync()
{
#ifndef MTK_USER_BUILD
    HWC_ATRACE_CALL();
#endif

    AutoMutex l(m_vsync_lock);
    m_vsync_cond.signal();
}

void DispatchThread::updateCpuSet(pid_t tid, DispatcherJob* job)
{
    if (job->cpu_set != m_cpu_set)
    {
        m_cpu_set = job->cpu_set;
        changeCpuSet(tid, m_cpu_set);
    }
}

// ---------------------------------------------------------------------------

#define PLOGD(x, ...) HWC_LOGD("(%" PRIu64 ") " x, m_disp_id, ##__VA_ARGS__)
#define PLOGI(x, ...) HWC_LOGI("(%" PRIu64 ") " x, m_disp_id, ##__VA_ARGS__)
#define PLOGW(x, ...) HWC_LOGW("(%" PRIu64 ") " x, m_disp_id, ##__VA_ARGS__)
#define PLOGE(x, ...) HWC_LOGE("(%" PRIu64 ") " x, m_disp_id, ##__VA_ARGS__)

HWCDispatcher::PostHandler::PostHandler(
    HWCDispatcher* dispatcher, uint64_t dpy, const sp<OverlayEngine>& ovl_engine)
    : m_dispatcher(dispatcher)
    , m_disp_id(dpy)
    , m_ovl_engine(ovl_engine)
    , m_curr_present_fence_fd(-1)
{ }

HWCDispatcher::PostHandler::~PostHandler()
{
    m_ovl_engine = NULL;
}

void HWCDispatcher::PostHandler::setOverlayInput(DispatcherJob* job)
{
    // disable unused input layer
    for (unsigned int i = 0; i < job->num_layers; i++)
    {
        if (!job->hw_layers[i].enable)
            m_ovl_engine->disableInput(i);
    }
    for (unsigned int i = job->num_layers; i < m_ovl_engine->getMaxInputNum(); i++)
    {
        m_ovl_engine->disableInput(i);
    }
}

// ---------------------------------------------------------------------------

void HWCDispatcher::PhyPostHandler::set(
    const sp<HWCDisplay>& display, DispatcherJob* job)
{
    job->hw_outbuf.phy_present_fence_fd = -1;
    job->hw_outbuf.phy_present_fence_idx = DISP_NO_PRESENT_FENCE;

    if (HWC_MIRROR_SOURCE_INVALID != job->disp_mir_id)
    {
        job->post_state = HWC_POST_MIRROR;
        return;
    }

    bool is_dirty = (job->post_state & HWC_POST_CONTINUE_MASK) != 0;

    HWC_ATRACE_FORMAT_NAME("BeginTransform");
    if (is_dirty)
    {
        // get present fence from display driver
        OverlayPrepareParam prepare_param;
        prepare_param.drm_id_crtc = job->drm_id_cur_crtc;
        status_t err = m_ovl_engine->preparePresentFence(prepare_param);
        if (NO_ERROR != err)
        {
            prepare_param.fence_index = 0;
            prepare_param.fence_fd = -1;
        }

        if (prepare_param.fence_fd <= 0)
        {
            PLOGE("Failed to get presentFence !!");
        }

        // add present fence info for color histogram and switch config with constraints
        display->addPresentInfo(prepare_param.fence_index, prepare_param.fence_fd,
                job->active_config);

        HWBuffer* hw_outbuf = &job->hw_outbuf;
        hw_outbuf->phy_present_fence_fd  = prepare_param.fence_fd;
        hw_outbuf->phy_present_fence_idx = prepare_param.fence_index;
        hw_outbuf->handle                = NULL;

        // try to get PQ fence and set PQ mode id to PQ service
        if (getPqDevice()->supportPqXml() && job->dirty_pq_mode_id)
        {
            job->pq_fence_fd = getPqDevice()->setDisplayPqMode(m_disp_id, job->drm_id_cur_crtc,
                    job->pq_mode_id, m_curr_present_fence_fd);
        }

        job->prev_present_fence_fd = m_curr_present_fence_fd;
        m_curr_present_fence_fd = (prepare_param.fence_fd >= 0) ? ::dup(prepare_param.fence_fd) : -1;
        if (m_disp_id == HWC_DISPLAY_PRIMARY &&
            Platform::getInstance().m_config.is_support_mdp_pmqos) {
            HWVSyncEstimator::getInstance().pushPresentFence(
                m_curr_present_fence_fd >= 0 ? ::dup(m_curr_present_fence_fd): -1, job->disp_data->refresh);
        }

        if (Platform::getInstance().m_config.plat_switch & HWC_PLAT_DBG_PRESENT_FENCE)
        {
            if (!m_fence_debugger)
            {
                m_fence_debugger = new FenceDebugger(std::string("pf_dbg_") + std::to_string(display->getId()),
                                                     FenceDebugger::CHECK_DIFF_BIG);
                if (m_fence_debugger == NULL)
                {
                    HWC_LOGW("cannot new FenceDebugger");
                }
                else
                {
                    m_fence_debugger->initialize();
                }
            }

            if (m_fence_debugger)
            {
                m_fence_debugger->dupAndStoreFence(prepare_param.fence_fd, prepare_param.fence_index);
            }
        }

        if (Platform::getInstance().m_config.is_support_mdp_pmqos_debug)
        {
            MDPFrameInfoDebugger::getInstance().setJobPresentFenceFd(job->sequence,
                m_curr_present_fence_fd >= 0 ? ::dup(m_curr_present_fence_fd): -1);
        }

        display->setRetireFenceFd(prepare_param.fence_fd, display->isConnected());

        DbgLogger* logger = &Debugger::getInstance().m_logger->set_info[static_cast<size_t>(job->disp_ori_id)];
        logger->printf("/PF(fd=%d, idx=%d, curr_pf_fd=%d,%d)",
            hw_outbuf->phy_present_fence_fd, hw_outbuf->phy_present_fence_idx, prepare_param.fence_fd, m_curr_present_fence_fd);
        HWC_ATRACE_FORMAT_NAME("TurnInto(%d)", prepare_param.fence_index);
    }
    else
    {
        // set as nodirty since could not find any dirty layers
        job->post_state = HWC_POST_INPUT_NOTDIRTY;

        DbgLogger* logger = &Debugger::getInstance().m_logger->set_info[static_cast<size_t>(job->disp_ori_id)];
        logger->printf(" / skip composition: no dirty layers");
        // clear all layers' acquire fences
        display->clearAllFences();
    }
}

void HWCDispatcher::PhyPostHandler::setMirror(
    DispatcherJob* src_job, DispatcherJob* dst_job)
{
    HWBuffer* phy_outbuf = &src_job->hw_outbuf;
    HWBuffer* dst_mirbuf = &dst_job->hw_mirbuf;
    HWBuffer* dst_outbuf = &dst_job->hw_outbuf;

    dst_mirbuf->mir_in_acq_fence_fd = phy_outbuf->mir_out_acq_fence_fd;
    dst_mirbuf->handle              = phy_outbuf->handle;
    memcpy(&dst_mirbuf->priv_handle, &phy_outbuf->priv_handle, sizeof(PrivateHandle));
    dst_mirbuf->dataspace = phy_outbuf->dataspace;

    if ((dst_job->disp_ori_id == HWC_DISPLAY_EXTERNAL) || (dst_job->disp_ori_id == HWC_DISPLAY_EXTERNAL_1))
    {
        unsigned int dst_format = phy_outbuf->priv_handle.format;
        switch (Platform::getInstance().m_config.format_mir_mhl)
        {
            case MIR_FORMAT_RGB888:
                dst_format = HAL_PIXEL_FORMAT_RGB_888;
                break;
            case MIR_FORMAT_YUYV:
                dst_format = HAL_PIXEL_FORMAT_YUYV;
                break;
            case MIR_FORMAT_YV12:
                dst_format = HAL_PIXEL_FORMAT_YV12;
                break;
            case MIR_FORMAT_UNDEFINE:
                // use same format as source, so do nothing
                break;
            default:
                HWC_LOGW("Not support mir format(%d), use same format as source(%x)",
                    Platform::getInstance().m_config.format_mir_mhl,
                    phy_outbuf->priv_handle.format);
         }

        dst_outbuf->priv_handle.format = dst_format;
        dst_outbuf->priv_handle.usage = phy_outbuf->priv_handle.usage;
    }

    // in decouple mode, need to wait for
    // both display and MDP finish reading this buffer
    {
        std::string name = android::base::StringPrintf("merged_fence(%d/%d)\n",
                                                       phy_outbuf->mir_out_if_fence_fd,
                                                       dst_mirbuf->mir_in_rel_fence_fd);

        // There are two components need MDP fence in the mirror path.
        // Memory session has duplicated it in set function of bliter, then return it to SF.
        // External session does not duplicated it, so we duplicated it in here.
        int tmp_fd = ((dst_job->disp_ori_id == HWC_DISPLAY_EXTERNAL) || (dst_job->disp_ori_id == HWC_DISPLAY_EXTERNAL_1)) ?
                ::dup(dst_mirbuf->mir_in_rel_fence_fd) :
                dst_mirbuf->mir_in_rel_fence_fd;

        int merged_fd = SyncFence::merge(
            phy_outbuf->mir_out_if_fence_fd,
            tmp_fd,
            name.c_str());

        ::protectedClose(phy_outbuf->mir_out_if_fence_fd);
        ::protectedClose(tmp_fd);

        // TODO: merge fences from different virtual displays to phy_outbuf->mir_out_mer_fence_fd
        phy_outbuf->mir_out_if_fence_fd = merged_fd;
    }

    PLOGD("set mirror (rel_fd=%d/handle=%p/ion=%d/dataspace=%d -> acq_fd=%d/handle=%p/ion=%d/dataspace=%d)",
        dst_mirbuf->mir_in_rel_fence_fd, dst_mirbuf->handle, dst_mirbuf->priv_handle.ion_fd,
        dst_mirbuf->dataspace, dst_outbuf->mir_in_rel_fence_fd, dst_outbuf->handle,
        dst_outbuf->priv_handle.ion_fd, dst_outbuf->dataspace);
}

void HWCDispatcher::PhyPostHandler::process(DispatcherJob* job)
{
    setOverlayInput(job);

    // set mirror output buffer if job is a mirror source
    if (job->need_output_buffer)
    {
        HWBuffer* hw_outbuf = &job->hw_outbuf;
        PrivateHandle* priv_handle = &hw_outbuf->priv_handle;

        OverlayPortParam param;

        bool is_secure = isSecure(priv_handle);
        if (is_secure)
        {
            param.va           = (void*)(uintptr_t)hw_outbuf->mir_out_sec_handle;
            param.mva          = (void*)(uintptr_t)hw_outbuf->mir_out_sec_handle;
        }
        else
        {
            param.va           = NULL;
            param.mva          = NULL;
        }

        param.pitch            = priv_handle->y_stride;
        param.format           = priv_handle->format;
        param.src_crop         = Rect(priv_handle->width, priv_handle->height);
        param.dst_crop         = param.src_crop;
        param.fence_index      = hw_outbuf->mir_out_acq_fence_idx;
        param.if_fence_index   = hw_outbuf->mir_out_if_fence_idx;
        param.secure           = is_secure;
        param.sequence         = job->sequence;
        param.ion_fd           = priv_handle->ion_fd;
        param.mir_rel_fence_fd = hw_outbuf->mir_out_if_fence_fd;
        param.fence            = hw_outbuf->mir_out_rel_fence_fd;
        param.dataspace        = hw_outbuf->dataspace;
        param.fb_id            = 0;
        param.alloc_id         = priv_handle->alloc_id;
        param.src_buf_width    = priv_handle->width;
        param.src_buf_height   = priv_handle->height;

        hw_outbuf->mir_out_rel_fence_fd = -1;

        m_ovl_engine->setOutput(&param, job->need_output_buffer);

        if (DisplayManager::m_profile_level & PROFILE_TRIG)
        {
            HWC_ATRACE_ASYNC_BEGIN("OVL0-MDP", job->sequence);
        }
    }
    else if (!job->need_output_buffer)
    {
        // disable overlay output
        m_ovl_engine->disableOutput();
    }

    if (job->aibld_enable)
    {
        AiBluLightDefender::getInstance().onProcess(job, m_ovl_engine);
    }
    if (job->display_dump_enable)
    {
        std::shared_ptr<DisplayDump> disp_dump =
            HWCMediator::getInstance().getHWCDisplay(job->disp_ori_id)->getDisplayDump();
        if (disp_dump != nullptr)
        {
            disp_dump->onProcess(job, m_ovl_engine);
        }
    }

    // trigger overlay engine
    m_ovl_engine->trigger(job->hw_outbuf.phy_present_fence_idx,
                          job->prev_present_fence_fd,
                          job->pq_fence_fd);

    job->prev_present_fence_fd = -1;
    job->pq_fence_fd = -1;
}

// ---------------------------------------------------------------------------

void HWCDispatcher::VirPostHandler::setError(DispatcherJob* job)
{
    for (unsigned int i = 0; i < job->num_layers; i++)
    {
        m_ovl_engine->disableInput(i);
    }
}

void HWCDispatcher::VirPostHandler::set(
    const sp<HWCDisplay>& display, DispatcherJob* job)
{
    // For WFD extension mode without OVL is available, let GPU to queue buffer to encoder directly.
    if ((!HwcFeatureList::getInstance().getFeature().copyvds) &&
        ((job->num_ui_layers + job->num_mm_layers + job->num_glai_layers) == 0) && !job->is_black_job)
    {
        PLOGD("No need to handle outbuf with GLES mode");
        job->post_state = HWC_POST_OUTBUF_DISABLE;
        display->clearAllFences();
        setError(job);
        return;
    }

    if (display->getOutbuf() == nullptr || display->getOutbuf()->getHandle() == nullptr)
    {
        PLOGE("Fail to get outbuf");
        job->post_state = HWC_POST_INVALID;
        display->clearAllFences();
        setError(job);
        return;
    }

    HWBuffer* hw_outbuf = &job->hw_outbuf;
    buffer_handle_t outbuf_hnd = display->getOutbuf()->getHandle();
    // Because hidle may release the buffer handle during dispatcher doing the job,
    // we need to control the lifecycle by HWC.
    if (!isNoDispatchThread())
    {
        dupBufferHandle(display->getOutbuf()->getHandle(), &outbuf_hnd);
    }
    PrivateHandle* priv_handle = &hw_outbuf->priv_handle;
    priv_handle->ion_fd = HWC_NO_ION_FD;

    status_t err = getPrivateHandle(outbuf_hnd, priv_handle, nullptr, true);
    err |= getAllocId(outbuf_hnd, priv_handle);
    if (err != NO_ERROR)
    {
        PLOGE("Failed to get private handle !! (outbuf=%p) !!", outbuf_hnd);
        job->post_state = HWC_POST_INVALID;
        display->clearAllFences();
        setError(job);
        if (!isNoDispatchThread())
        {
            freeDuppedBufferHandle(outbuf_hnd);
        }
        return;
    }

    job->post_state = HWC_POST_OUTBUF_ENABLE;

    hw_outbuf->dataspace = mapColorMode2DataSpace(display->getColorMode());
    if (HWC_MIRROR_SOURCE_INVALID != job->disp_mir_id)
    {
        // mirror mode
        //
        // set mirror output buffer
        hw_outbuf->mir_in_rel_fence_fd = dupCloseFd(display->getOutbuf()->getReleaseFenceFd());
        display->getOutbuf()->setReleaseFenceFd(-1, display->isConnected());
        hw_outbuf->handle              = outbuf_hnd;
    }
    else if (job->is_black_job)
    {
        // Virtual Black mode
        //
        // set output buffer
        hw_outbuf->mir_in_rel_fence_fd = dupCloseFd(display->getOutbuf()->getReleaseFenceFd());
        display->getOutbuf()->setReleaseFenceFd(-1, display->isConnected());
        hw_outbuf->handle              = outbuf_hnd;

        // Extension path doesn't need this mirror_out_roi info, clear it can avoid to effect
        // bliter_async's clearBackground function no work.
        gralloc_extra_ion_hwc_info_t* hwc_ext_info = &priv_handle->hwc_ext_info;
        if (hwc_ext_info != nullptr)
        {
            if (hwc_ext_info->mirror_out_roi.x != 0 || hwc_ext_info->mirror_out_roi.y != 0 ||
                    hwc_ext_info->mirror_out_roi.w != 0 || hwc_ext_info->mirror_out_roi.h != 0)
            {
                hwc_ext_info->mirror_out_roi.x = 0;
                hwc_ext_info->mirror_out_roi.y = 0;
                hwc_ext_info->mirror_out_roi.w = 0;
                hwc_ext_info->mirror_out_roi.h = 0;
                gralloc_extra_perform(hw_outbuf->handle, GRALLOC_EXTRA_SET_HWC_INFO, hwc_ext_info);
            }
        }
    }
    else
    {
        // extension mode
        //
        // get retire fence from display driver
        OverlayPrepareParam prepare_param;
        {
            prepare_param.ion_fd        = priv_handle->ion_fd;
            prepare_param.is_need_flush = 0;

            prepare_param.blending = HWC2_BLEND_MODE_NONE;
            prepare_param.drm_id_crtc = job->drm_id_cur_crtc;

            if (HWCMediator::getInstance().getLowLatencyWFD() == true)
            {
                prepare_param.fence_index = INIT_FENCE_IDX;
                prepare_param.fence_fd = -1;
            }
            else
            {
                err = m_ovl_engine->prepareOutput(prepare_param);
                if (NO_ERROR != err)
                {
                    prepare_param.fence_index = 0;
                    prepare_param.fence_fd = -1;
                }

                if (prepare_param.fence_fd <= 0)
                {
                    PLOGE("Failed to get retireFence !!");
                }
            }
        }

        hw_outbuf->out_acquire_fence_fd = dupCloseFd(display->getOutbuf()->getReleaseFenceFd());
        display->getOutbuf()->setReleaseFenceFd(-1, display->isConnected());
        hw_outbuf->out_retire_fence_fd  = prepare_param.fence_fd;
        hw_outbuf->out_retire_fence_idx = prepare_param.fence_index;
        hw_outbuf->handle               = outbuf_hnd;

        // Extension path doesn't need this mirror_out_roi info, clear it can avoid to effect
        // bliter_async's clearBackground function no work.
        gralloc_extra_ion_hwc_info_t* hwc_ext_info = &priv_handle->hwc_ext_info;
        if (hwc_ext_info != nullptr)
        {
            if (hwc_ext_info->mirror_out_roi.x != 0 || hwc_ext_info->mirror_out_roi.y != 0 ||
                    hwc_ext_info->mirror_out_roi.w != 0 || hwc_ext_info->mirror_out_roi.h != 0)
            {
                hwc_ext_info->mirror_out_roi.x = 0;
                hwc_ext_info->mirror_out_roi.y = 0;
                hwc_ext_info->mirror_out_roi.w = 0;
                hwc_ext_info->mirror_out_roi.h = 0;
                gralloc_extra_perform(hw_outbuf->handle, GRALLOC_EXTRA_SET_HWC_INFO, hwc_ext_info);
            }
        }

        display->setRetireFenceFd(prepare_param.fence_fd, display->isConnected());

        DbgLogger* logger = &Debugger::getInstance().m_logger->set_info[static_cast<size_t>(job->disp_ori_id)];

        logger->printf("/Outbuf(ret_fd=%d(%u), acq_fd=%d, handle=%p, ion=%d)",
            hw_outbuf->out_retire_fence_fd, hw_outbuf->out_retire_fence_idx,
            hw_outbuf->out_acquire_fence_fd, hw_outbuf->handle, priv_handle->ion_fd);

        if (!job->fbt_exist)
        {
            auto fbt_layer = display->getClientTarget();
            fbt_layer->setReleaseFenceFd(-1, display->isConnected());
        }
    }

    // set video usage and timestamp into output buffer handle
    gralloc_extra_ion_sf_info_t* ext_info = &hw_outbuf->priv_handle.ext_info;
    ext_info->timestamp = job->timestamp;
    if (DisplayManager::m_profile_level & PROFILE_TRIG)
    {
        // set token to buffer handle for profiling latency purpose
        ext_info->sequence = static_cast<uint32_t>(job->sequence % UINT32_MAX);
    }
    gralloc_extra_perform(
        hw_outbuf->handle, GRALLOC_EXTRA_SET_IOCTL_ION_SF_INFO, ext_info);
}

void HWCDispatcher::VirPostHandler::setMirror(
    DispatcherJob* /*src_job*/, DispatcherJob* /*dst_job*/)
{
}

void HWCDispatcher::VirPostHandler::process(DispatcherJob* job)
{
    if (HWC_MIRROR_SOURCE_INVALID == job->disp_mir_id && !job->is_black_job)
    {
        // extension mode

        setOverlayInput(job);

        // set output buffer for virtual display
        {
            HWBuffer* hw_outbuf = &job->hw_outbuf;
            PrivateHandle* priv_handle = &hw_outbuf->priv_handle;

            OverlayPortParam param;

            bool is_secure = isSecure(priv_handle);
            if (is_secure)
            {
                param.va      = (void*)(uintptr_t)priv_handle->sec_handle;
                param.mva     = (void*)(uintptr_t)priv_handle->sec_handle;
            }
            else
            {
                param.va      = NULL;
                param.mva     = NULL;
            }
            param.pitch       = priv_handle->y_stride;
            param.format      = priv_handle->format;
            param.src_crop    = Rect(priv_handle->width, priv_handle->height);
            param.dst_crop    = param.src_crop;
            param.fence_index = hw_outbuf->out_retire_fence_idx;
            param.secure      = is_secure;
            param.sequence    = job->sequence;
            param.ion_fd      = priv_handle->ion_fd;
            param.fence       = hw_outbuf->out_acquire_fence_fd;
            param.dataspace   = hw_outbuf->dataspace;
            hw_outbuf->out_acquire_fence_fd = -1;
            param.fb_id   = 0;
            param.src_buf_width  = priv_handle->width;
            param.src_buf_height = priv_handle->height;
            param.alloc_id      = priv_handle->alloc_id;

            m_ovl_engine->setOutput(&param);
        }

        // trigger overlay engine
        m_ovl_engine->trigger(DISP_NO_PRESENT_FENCE, -1, -1);
    }
    else
    {
        // mirror mode
        if (DisplayManager::m_profile_level & PROFILE_TRIG)
        {
            HWC_ATRACE_ASYNC_END("MDP-SMS", job->sequence);
        }
        DataExpress::getInstance().deletePackage(m_disp_id, job->sequence);
    }
}

