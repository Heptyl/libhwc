#define DEBUG_LOG_TAG "BLT_ASYNC"

#include "graphics_mtk_defs.h"
#include "gralloc_mtk_defs.h"
#include "gm/gm_pixel_format.h"

#include "utils/debug.h"
#include "utils/tools.h"

#include "utils/transform.h"

#include "bliter_async.h"
#include "display.h"
#include "platform_wrap.h"
#include "overlay.h"
#include "dispatcher.h"
#include "worker.h"
#include "queue.h"
#include "hwc2.h"
#include "bliter_ultra.h"

#include <processgroup/sched_policy.h>
#include <system/thread_defs.h>
#include <sys/resource.h>
#include <sync/sync.h>

#include <utils/String8.h>

#define NOT_PRIVATE_FORMAT -1

#define BLOGD(i, x, ...) HWC_LOGD("(%" PRIu64 ":%d) " x, m_disp_id, i, ##__VA_ARGS__)
#define BLOGI(i, x, ...) HWC_LOGI("(%" PRIu64 ":%d) " x, m_disp_id, i, ##__VA_ARGS__)
#define BLOGW(i, x, ...) HWC_LOGW("(%" PRIu64 ":%d) " x, m_disp_id, i, ##__VA_ARGS__)
#define BLOGE(i, x, ...) HWC_LOGE("(%" PRIu64 ":%d) " x, m_disp_id, i, ##__VA_ARGS__)

#define WDT_BL_NODE(fn, ...)                                                                    \
({                                                                                              \
    if (Platform::getInstance().m_config.wdt_trace)                                             \
    {                                                                                           \
        ATRACE_NAME(#fn);                                                                       \
        m_bliter_node->fn(__VA_ARGS__);                                                         \
    }                                                                                           \
    else                                                                                        \
    {                                                                                           \
        m_bliter_node->fn(__VA_ARGS__);                                                         \
    }                                                                                           \
})

/* copy from PQ header to verify HDR10 metadata */
enum TRANSFER_CHARACTOR_E {
    TRANSFER_CHARACTOR_ITURBT709           = 1,
    TRANSFER_CHARACTOR_UNSPECIFIED         = 2,
    TRANSFER_CHARACTOR_GAMMA2_2CURVE       = 4,
    TRANSFER_CHARACTOR_GAMMA2_8CURVE       = 5,
    TRANSFER_CHARACTOR_SMPTE170M           = 6,
    TRANSFER_CHARACTOR_SMPTE240M           = 7,
    TRANSFER_CHARACTOR_LINEAR              = 8,
    TRANSFER_CHARACTOR_LOG                 = 9,
    TRANSFER_CHARACTOR_SQRT                = 10,
    TRANSFER_CHARACTOR_IEC61966_2_4        = 11,
    TRANSFER_CHARACTOR_ITURBT1361          = 12,
    TRANSFER_CHARACTOR_IEC61966_2_1        = 13,
    TRANSFER_CHARACTOR_ITURBT2020_10       = 14,
    TRANSFER_CHARACTOR_ITURBT2020_12       = 15,
    TRANSFER_CHARACTOR_SMPTEST2084         = 16,
    TRANSFER_CHARACTOR_SMPTEST428_1        = 17,
    TRANSFER_CHARACTOR_ARIBSTD_B67         = 18,
    TRANSFER_CHARACTOR_RESVERD,
};


using namespace mediatek::graphics::common;
// ---------------------------------------------------------------------------

AsyncBliterHandler::AsyncBliterHandler(uint64_t dpy, const sp<OverlayEngine>& ovl_engine)
    : LayerHandler(dpy, ovl_engine)
    , m_mirror_queue(nullptr)
{
    unsigned int num = m_ovl_engine->getMaxInputNum() + 1;
    m_dp_configs = (BufferConfig*)calloc(1, sizeof(BufferConfig) * num);
    LOG_ALWAYS_FATAL_IF(m_dp_configs == nullptr, "async dp_config calloc(%zu) fail",
        sizeof(BufferConfig) * num);

    m_bliter_node = new BliterNode(m_disp_id);

    if (isNoDispatchThread())
    {
        m_worker = std::make_shared<Worker>();
    }
}

AsyncBliterHandler::~AsyncBliterHandler()
{
    free(m_dp_configs);

    if (NULL != m_bliter_node)
    {
        delete m_bliter_node;
        m_bliter_node = NULL;
    }

    cleanPrevLayerInfo();
}

inline bool decideOutputAlphaEnable(HWLayer* hw_layer)
{
    // TODO: MDP keeps output buffer's alpha channel only if no scaling and no PQ
    if (hw_layer == nullptr)
        return true;
    return hw_layer->hdr_scenario != HWC_LAYER_HDR_GAME &&
           hw_layer->layer.blending != HWC2_BLEND_MODE_NONE;
}

void AsyncBliterHandler::set(const sp<HWCDisplay>& display, DispatcherJob* job)
{
    auto&& layers = display->getCommittedLayers();
    cleanPrevLayerInfo(&layers);

    if (HWC_MIRROR_SOURCE_INVALID != job->disp_mir_id)
    {
        setMirror(display, job);
        job->num_processed_mm_layers = 1;
        return;
    }
    else
    {
        // m_mirror_queue not needed any more
        if (CC_UNLIKELY(m_mirror_queue != nullptr))
        {
            m_mirror_queue = nullptr;
        }
    }

    if (job->is_black_job)
    {
        setBlack(display, job);
        setBlackToWorker(job);
        return;
    }

    uint32_t total_num = job->num_layers;
    int num_processed_layer = 0;

    for (uint32_t i = 0; i < total_num; i++)
    {
        HWLayer* hw_layer = &job->hw_layers[i];

        // skip non mm layers
        if (HWC_LAYER_TYPE_MM != hw_layer->type) continue;

        hw_layer->is_do_blit = false;

        // this layer is not enable
        if (!hw_layer->enable) continue;

        sp<HWCLayer> hwc_layer = layers[hw_layer->index];
        PrivateHandle& priv_handle = hw_layer->priv_handle;

        if (display->getId() == HWC_DISPLAY_EXTERNAL &&
            HWCMediator::getInstance().getOvlDevice(uint64_t(HWC_DISPLAY_EXTERNAL))->getDolbyInfo())
        {
            hwc_layer->setAlignDisplayFrame();
            hwc_layer->editMdpDstRoi() = hwc_layer->getDisplayFrame();
            hw_layer->mdp_dst_roi.left = hwc_layer->getMdpDstRoi().left;
            hw_layer->mdp_dst_roi.top = hwc_layer->getMdpDstRoi().top;
            hw_layer->mdp_dst_roi.right = hwc_layer->getMdpDstRoi().right;
            hw_layer->mdp_dst_roi.bottom = hwc_layer->getMdpDstRoi().bottom;
        }

        DbgLogger logger(DbgLogger::TYPE_HWC_LOG, 'D',
                         "[BLT_ASYNC]SET(%" PRIu64 ",%d, %d)", m_disp_id, i,
                         Platform::getInstance().m_config.dbg_mdp_always_blit);

        // prepare buffer queue for this (job, layer)
        sp<DisplayBufferQueue> queue = getDisplayBufferQueue(hw_layer);

        if (doBlit(hw_layer))
        {
            // workaround
            int release_fence_fd = -1;
            // If is MML,
            // 1. create bliter ultra's jobpara
            // 2. don't get release fence here
            // else if is MDP
            // 1. create bliter ultra's jobpara
            // 2. get jobID and release fence from blitstream
            bool is_mml = isMMLLayer(hw_layer);
            WDT_BL_NODE(createJob, hw_layer->mdp_job_id, release_fence_fd,
                (is_mml) ? BliterNode::HWC_2D_BLITER_PROCESSER_MML :
                    BliterNode::HWC_2D_BLITER_PROCESSER_MDP);

            copyHWCLayerIntoLightHwcLayer1(hwc_layer, &hw_layer->layer);
            hw_layer->layer.releaseFenceFd = -1;

            if (DisplayManager::m_profile_level & PROFILE_BLT)
            {
                char atrace_tag[256];
                if (snprintf(atrace_tag, sizeof(atrace_tag), "mm set:%p", hwc_layer->getHandle()) > 0)
                {
                    HWC_ATRACE_NAME(atrace_tag);
                }
            }

            // no dispatch thread mode should dup input handle for MDP layer
            // since it will run on another thread
            if (isNoDispatchThread() && !is_mml)
            {
                buffer_handle_t out_hnd = NULL;
                dupBufferHandle(hw_layer->priv_handle.handle, &out_hnd);
                getPrivateHandleBuff(out_hnd, &hw_layer->priv_handle);
            }
            else
            {
                if (priv_handle.ion_fd > 0)
                {
                    // workaround:
                    //IONDevice::getInstance().ionImport(&*(const_cast<int32_t*>(&priv_handle.ion_fd)));
                    IONDevice::getInstance().ionImport(&priv_handle.ion_fd);
                    hw_layer->is_do_blit = true;
                }
            }


            LightHwcLayer1* layer = &hw_layer->layer;
            BufferConfig* config = &m_dp_configs[i];
            DisplayBufferQueue::DisplayBuffer disp_buffer;

            const uint32_t assigned_output_format = hw_layer->mdp_output_format;
            const bool assigned_compression = hw_layer->mdp_output_compressed;
            const bool is_game = hw_layer->need_pq;
            const int32_t hdr_scenario = hw_layer->hdr_scenario;
            uint32_t pq_enhance = 0;

            setPQEnhance(hw_layer->priv_handle, &pq_enhance, hdr_scenario, is_game);

            // config buffer queue
            configDisplayBufferQueue(queue, &hw_layer->priv_handle,
                                     job->disp_data, assigned_output_format, assigned_compression);

            status_t err = queue->dequeueBuffer(&disp_buffer, true, isSecure(&hw_layer->priv_handle));

            if (err == NO_ERROR)
            {
                err = setDpConfig(&hw_layer->priv_handle, &disp_buffer,
                                  hw_layer->dataspace, hw_layer->mdp_output_dataspace,
                                  config, i);
                if (err != NO_ERROR)
                {
                    BLOGE(i, "%s(): setDpConfig() goes wrong err=%d", __func__, err);
                }
            }
            else
            {
                BLOGE(i, "%s(): dequeueBuffer() goes wrong err=%d", __func__, err);
            }

            if (err == NO_ERROR)
            {
                Rect src_roi = getFixedRect(layer->sourceCropf);
                Rect dst_roi = hw_layer->mdp_dst_roi;
                uint32_t xform = layer->transform;

                // rectify with preXform before all configuration
                rectifyRectWithPrexform(&src_roi, &hw_layer->priv_handle);
                rectifyXformWithPrexform(&xform, hw_layer->priv_handle.prexform);

                BliterNode::Parameter param = {src_roi, dst_roi, config, xform, pq_enhance, disp_buffer.secure};
                Rect mdp_cal_dst_crop;

                WDT_BL_NODE(setSrc, hw_layer->mdp_job_id, config, hw_layer->priv_handle, &layer->acquireFenceFd,
                    hw_layer->hdr_static_metadata_keys, hw_layer->hdr_static_metadata_values,
                    hw_layer->hdr_dynamic_metadata, hw_layer->hdr_scenario,
                    is_game, job->pq_mode_id);
                WDT_BL_NODE(setDst, hw_layer->mdp_job_id, &param, disp_buffer.out_ion_fd,
                            disp_buffer.out_sec_handle,
                            &disp_buffer.release_fence);
                WDT_BL_NODE(calculateAllROI, hw_layer->mdp_job_id, &mdp_cal_dst_crop);

                if (is_mml)
                {
                    bool is_pixel_alpha_used = false;
                    m_bliter_node->setLayerID(0, hw_layer->hwc2_layer_id);
                    m_bliter_node->setMMLMode(hw_layer->layer_caps);
                    is_pixel_alpha_used = hwc_layer->isPixelAlphaUsed();
                    m_bliter_node->setIsPixelAlphaUsed(is_pixel_alpha_used);
                    WDT_BL_NODE(invalidate, hw_layer->mdp_job_id, job->sequence,
                        job->active_config, job->present_after_ts, job->decouple_target_ts,
                        BliterNode::HWC_2D_BLITER_PROCESSER_MML,
                        &release_fence_fd);
                }

                hw_layer->layer.releaseFenceFd = release_fence_fd;
                hwc_layer->setReleaseFenceFd(dup(release_fence_fd), display->isConnected());
                savePrevLayerInfo(hwc_layer);

                logger.printf("/acq=%d/handle=%p/job=%d/caps=0x%x/pq=%d,%d,%d,%d,%d",
                        hwc_layer->getAcquireFenceFd(), hwc_layer->getHandle(),
                        hw_layer->mdp_job_id, hwc_layer->getLayerCaps(),
                        priv_handle.pq_enable, priv_handle.pq_pos, priv_handle.pq_orientation,
                        priv_handle.pq_table_idx, (priv_handle.ai_pq_info.param != 0));

                if (Platform::getInstance().m_config.dbg_switch & HWC_DBG_SWITCH_DEBUG_MM_BUFFER_INFO)
                {
                    HWC_LOGI("MMPathPassingInfoCheck: hwc_layer_id %" PRIu64" src alloc_id %" PRIu64" dispatcher job %d",
                             hwc_layer->getId(), priv_handle.alloc_id, hw_layer->mdp_job_id);
                }

                if (isSecure(&priv_handle))
                {
                    logger.printf("/sec");
                }

                passFenceFd(&disp_buffer.acquire_fence, &layer->releaseFenceFd);

                if (Platform::getInstance().m_config.is_support_mdp_pmqos_debug)
                {
                    MDPFrameInfoDebugger::getInstance().setJobAcquireFenceFd(job->sequence, ::dup(disp_buffer.acquire_fence));
                }

                disp_buffer.data_info.src_crop   = mdp_cal_dst_crop;
                disp_buffer.data_info.dst_crop.left   = layer->displayFrame.left;
                disp_buffer.data_info.dst_crop.top    = layer->displayFrame.top;
                disp_buffer.data_info.dst_crop.right  = layer->displayFrame.right;
                disp_buffer.data_info.dst_crop.bottom = layer->displayFrame.bottom;

                disp_buffer.data_info.is_sharpen = false;
                disp_buffer.alpha_enable         = decideOutputAlphaEnable(hw_layer);
                disp_buffer.alpha                = layer->planeAlpha;
                disp_buffer.blending             = layer->blending;
                disp_buffer.src_handle           = hw_layer->priv_handle.handle;
                disp_buffer.dataspace            = config->dst_dataspace;
                disp_buffer.ext_sel_layer        = hw_layer->ext_sel_layer;
                disp_buffer.hwc_layer_id         = hw_layer->hwc2_layer_id;
                disp_buffer.sequence = job->sequence;

                //
                // set EXT HDR metadata into data_info
                //
                bool is_hdr = false, is_hdr10plus = false, is_hlg = false, is_dv = false;
                disp_buffer.data_info.ethdr_meta.hdrType = MTK_ETHDR_HDR_TYPE_SDR;
                disp_buffer.data_info.ethdr_meta.colorPrimaries = priv_handle.hdrInfo.u4ColorPrimaries;
                disp_buffer.data_info.ethdr_meta.transferCharacteristics = priv_handle.hdrInfo.u4TransformCharacter;
                disp_buffer.data_info.ethdr_meta.matrixCoefficients = priv_handle.hdrInfo.u4MatrixCoeffs;
                HWC_LOGD("Set as SDR first: colorPrimaries: %d", priv_handle.hdrInfo.u4ColorPrimaries);

                if (HWCMediator::getInstance().getOvlDevice(m_disp_id)->getEthdrSupport(m_disp_id) && Platform::getInstance().m_config.ethdr_meta_support)
                { /* set EXT HDR metadata for VDOSYS1 */
                    HWC_LOGD("HDR info: u4transformcharacter: %d", priv_handle.hdrInfo.u4TransformCharacter);

                    if (CHECK_HDR10_META_VALID(priv_handle.hdrInfo.u4TransformCharacter))  //valid struct for HDR
                    {
                        HWC_LOGD("HDR info: country code: %d", priv_handle.hdrPlusInfo.country_code);
                        if (checkHDR10PlusMetadata(priv_handle.hdrPlusInfo))
                            is_hdr10plus = true;
                        else if (priv_handle.hdrInfo.u4TransformCharacter == TRANSFER_CHARACTOR_ARIBSTD_B67)
                            is_hlg = true;
                        else
                            is_hdr = true;
                    }

                    //for hdr10 plus
                    if(is_hdr10plus) {
                        memcpy(&disp_buffer.data_info.ethdr_meta.hdr10PlusDynamicMetadata, &priv_handle.hdrPlusInfo, sizeof(priv_handle.hdrPlusInfo));
                        disp_buffer.data_info.ethdr_meta.hdrType = MTK_ETHDR_HDR_TYPE_HDR10PLUS;
                        HWC_LOGD("is HDR10_PLUS");
                    }

                    //for hdr10
                    if(is_hdr) {
                        memcpy(&disp_buffer.data_info.ethdr_meta.hdr10StaticMetadata, &priv_handle.hdrInfo, sizeof(priv_handle.hdrInfo));
                        disp_buffer.data_info.ethdr_meta.hdrType = MTK_ETHDR_HDR_TYPE_HDR10;
                        HWC_LOGD("is HDR10");
                    }

                    //for hlg
                    if(is_hlg) {
                        memcpy(&disp_buffer.data_info.ethdr_meta.hdr10StaticMetadata, &priv_handle.hdrInfo, sizeof(priv_handle.hdrInfo));
                        disp_buffer.data_info.ethdr_meta.hdrType = MTK_ETHDR_HDR_TYPE_HLG;
                        HWC_LOGD("is HLG");
                    }

                    HWC_LOGD("DV info: DV offset: %d", priv_handle.meta_layout.block[GRALLOC_META_DMA_BLOCK_DV].offset);

                    if (priv_handle.meta_layout.block[GRALLOC_META_DMA_BLOCK_DV].offset != -1) {
                        is_dv = true;
                        HWC_LOGD("is DV");
                        HWC_LOGD("secure: %d", priv_handle.is_secure);
                       if (priv_handle.is_secure) {
                            HWC_LOGD("SEC_HANDLE: %d", priv_handle.sec_handle);
                        } else {
                            HWC_LOGD("dv fd: %d", priv_handle.meta_fd);
                        }
                        HWC_LOGD("dv start(offset): %d", priv_handle.meta_layout.start);
                        HWC_LOGD("dv size(total): %d", priv_handle.meta_layout.size);
                        HWC_LOGD("dv offset: %d", priv_handle.meta_layout.block[GRALLOC_META_DMA_BLOCK_DV].offset);
                        HWC_LOGD("dv size(real): %d", priv_handle.meta_layout.block[GRALLOC_META_DMA_BLOCK_DV].length);
                    }

                    //for DV
                    if(is_dv) {
                        disp_buffer.data_info.ethdr_meta.hdrType = MTK_ETHDR_HDR_TYPE_DV;
                        disp_buffer.data_info.ethdr_meta.isSecure = priv_handle.is_secure;
                        if (priv_handle.is_secure) {
                            disp_buffer.data_info.ethdr_meta.DVMetadataBufferFd = priv_handle.sec_handle;
                        } else {
                            disp_buffer.data_info.ethdr_meta.DVMetadataBufferFd = (unsigned int)priv_handle.meta_fd;
                        }
                        disp_buffer.data_info.ethdr_meta.DVMetadataBufferLength =
                            (unsigned int)priv_handle.meta_layout.block[GRALLOC_META_DMA_BLOCK_DV].length;
                        disp_buffer.data_info.ethdr_meta.DVMetadataBufferOffset = (unsigned int)priv_handle.meta_layout.start;

                    }
                }

                queue->queueBuffer(&disp_buffer);

                if (isNoDispatchThread())
                {
                    OverlayPortParam* const* ovl_params = m_ovl_engine->getInputParams();
                    prepareOverlayPortParam(i, job, queue, ovl_params[i], true, hw_layer->layer_caps);

                    if (is_mml)
                    {
                        if (!(HWC_MML_DISP_DIRECT_DECOUPLE_LAYER & hw_layer->layer_caps))
                        {
                            if (hw_layer->priv_handle.ion_fd > 0)
                            {
                                IONDevice::getInstance().ionClose(hw_layer->priv_handle.ion_fd);
                            }
                        }

                        LightHwcLayer1* layer = &hw_layer->layer;
                        closeFenceFd(&layer->acquireFenceFd);
                        closeFenceFd(&layer->releaseFenceFd);
                    }
                    else
                    {
                        setInvalidateToWorker(job, i);
                    }
                }
                else
                {
                    prepareOverlayPortParam(i, job, queue, &hw_layer->ovl_port_param, true, hw_layer->layer_caps);
                }

                if (HWC_MML_DISP_DIRECT_DECOUPLE_LAYER & hw_layer->layer_caps)
                {
                    // MML IR mode does not have fence so we use DBQ release fence for HWCLayer release fence.
                    hwc_layer->setReleaseFenceFd(dup(queue->getReleaseFence()), display->isConnected());
                }
                logger.printf("/rel=%d", hwc_layer->getReleaseFenceFd());
            }
            else
            {
                WDT_BL_NODE(cancelJob, hw_layer->mdp_job_id);

                BLOGE(i, "something wrong, cancel mdp job ...");

                if (isNoDispatchThread() && !is_mml)
                {
                    freeDuppedBufferHandle(hw_layer->priv_handle.handle);
                }
            }
            num_processed_layer++;
        }
        else
        {
            hw_layer->mdp_job_id = 0;

            hwc_layer->setReleaseFenceFd(dup(getPrevLayerInfoFence(hwc_layer)), display->isConnected());

            copyHWCLayerIntoLightHwcLayer1(hwc_layer, &hw_layer->layer, true);

            logger.printf("/async=bypass/acq=%d/handle=%p/skip_invalidate=%d/caps=0x%x",
                          hwc_layer->getAcquireFenceFd(), hwc_layer->getHandle(), hw_layer->mdp_skip_invalidate,
                          hw_layer->layer_caps);

            if (isNoDispatchThread())
            {
                OverlayPortParam* const* ovl_params = m_ovl_engine->getInputParams();
                prepareOverlayPortParam(i, job, queue, ovl_params[i], false);

                // update ext_sel_layer for this frame
                ovl_params[i]->ext_sel_layer = hw_layer->ext_sel_layer;
                // update dst_crop
                ovl_params[i]->dst_crop.left   = hw_layer->layer.displayFrame.left;
                ovl_params[i]->dst_crop.top    = hw_layer->layer.displayFrame.top;
                ovl_params[i]->dst_crop.right  = hw_layer->layer.displayFrame.right;
                ovl_params[i]->dst_crop.bottom = hw_layer->layer.displayFrame.bottom;
            }
            else
            {
                prepareOverlayPortParam(i, job, queue, &hw_layer->ovl_port_param, false);

                // update ext_sel_layer for this frame
                hw_layer->ovl_port_param.ext_sel_layer = hw_layer->ext_sel_layer;
                // update dst_crop
                hw_layer->ovl_port_param.dst_crop.left   = hw_layer->layer.displayFrame.left;
                hw_layer->ovl_port_param.dst_crop.top    = hw_layer->layer.displayFrame.top;
                hw_layer->ovl_port_param.dst_crop.right  = hw_layer->layer.displayFrame.right;
                hw_layer->ovl_port_param.dst_crop.bottom = hw_layer->layer.displayFrame.bottom;
            }

            if (DisplayManager::m_profile_level & PROFILE_BLT)
            {
                char atrace_tag[256];
                if (snprintf(atrace_tag, sizeof(atrace_tag),
                             "mm bypass:%p, skip_invalidate %d",
                             hwc_layer->getHandle(), hw_layer->mdp_skip_invalidate) > 0)
                {
                    HWC_ATRACE_NAME(atrace_tag);
                }
            }
        }

        //get is rx layer
        HWC_LOGV("[HDMIRX] Bliter_async %s, get magic number = 0x%x", __func__, priv_handle.ext_info.magic);
        if(priv_handle.ext_info.magic == 0xF0F0)
            hw_layer->ovl_port_param.hdmi_rx = true;
        else
            hw_layer->ovl_port_param.hdmi_rx = false;

    }
    job->num_processed_mm_layers = num_processed_layer;
}

int AsyncBliterHandler::prepareOverlayPortParam(unsigned int ovl_id,
                                                DispatcherJob* job,
                                                sp<DisplayBufferQueue> queue,
                                                OverlayPortParam* ovl_port_param,
                                                bool new_buf,
                                                int32_t hw_layer_caps)
{
    if (!queue)
    {
        BLOGE(ovl_id, "%s(), queue == nullptr", __FUNCTION__);
        return -EINVAL;
    }

    DisplayBufferQueue::DisplayBuffer* buffer = queue->getLastAcquiredBufEditable();

    if (new_buf)
    {
        // release previous buffer
        if (buffer->index != DisplayBufferQueue::INVALID_BUFFER_SLOT)
        {
            const int32_t rel_fence_fd = queue->getReleaseFence();
            queue->setReleaseFence(-1);
            if (queue->releaseBuffer(buffer->index, rel_fence_fd) != NO_ERROR)
            {
                ::protectedClose(rel_fence_fd);
            }
        }

        // acquire next buffer
        queue->acquireBuffer(buffer, true);
    }
    else
    {
        // close previous release fence
        const int32_t rel_fence_fd = queue->getReleaseFence();
        if (rel_fence_fd != -1)
        {
            ::protectedClose(rel_fence_fd);
        }
    }

    // get release fence for input queue
    OverlayPrepareParam prepare_param;
    prepare_param.id            = ovl_id;
    prepare_param.ion_fd        = buffer->out_ion_fd;
    prepare_param.is_need_flush = 0;
    prepare_param.blending      = buffer->blending;
    prepare_param.drm_id_crtc   = job->drm_id_cur_crtc;
    prepare_param.ext_video_frame = 0;
    HWC_LOGD("prepareOverlayPortParam: data_format: %u", buffer->data_format);

    /* do format check to prevent UI rotate send metadata to DRM */
    if (Platform::getInstance().m_config.ethdr_meta_support &&
        buffer->data_format != HAL_PIXEL_FORMAT_RGBA_8888 &&
        buffer->data_format != HAL_PIXEL_FORMAT_RGBX_8888 &&
        buffer->data_format != HAL_PIXEL_FORMAT_RGB_888 &&
        buffer->data_format != HAL_PIXEL_FORMAT_RGB_565 &&
        buffer->data_format != HAL_PIXEL_FORMAT_BGRA_8888 &&
        buffer->data_format != HAL_PIXEL_FORMAT_RGBA_FP16 &&
        buffer->data_format != HAL_PIXEL_FORMAT_RGBA_1010102) {
        //setup hdr ext metadata info
        prepare_param.ethdr_meta.planeId = buffer->data_info.ethdr_meta.planeId;
        prepare_param.ethdr_meta.hdrType = buffer->data_info.ethdr_meta.hdrType;
        memcpy(&prepare_param.ethdr_meta.hdr10StaticMetadata, &buffer->data_info.ethdr_meta.hdr10StaticMetadata,
           sizeof(buffer->data_info.ethdr_meta.hdr10StaticMetadata));
        memcpy(&prepare_param.ethdr_meta.hdr10PlusDynamicMetadata, &buffer->data_info.ethdr_meta.hdr10PlusDynamicMetadata,
           sizeof(buffer->data_info.ethdr_meta.hdr10PlusDynamicMetadata));
        prepare_param.ethdr_meta.DVMetadataBufferFd = buffer->data_info.ethdr_meta.DVMetadataBufferFd;
        prepare_param.ethdr_meta.DVMetadataBufferOffset = buffer->data_info.ethdr_meta.DVMetadataBufferOffset;
        prepare_param.ethdr_meta.DVMetadataBufferLength= buffer->data_info.ethdr_meta.DVMetadataBufferLength;
        prepare_param.ethdr_meta.isSecure = buffer->data_info.ethdr_meta.isSecure;
        prepare_param.ethdr_meta.colorRange = buffer->data_info.ethdr_meta.colorRange;
        prepare_param.ext_video_frame = 1;
    }

    status_t err = m_ovl_engine->prepareInput(prepare_param);
    if (err != NO_ERROR)
    {
        prepare_param.fence_index = 0;
        prepare_param.fence_fd = -1;
    }

    if (prepare_param.fence_fd <= 0)
    {
        BLOGE(ovl_id, "Failed to get releaseFence for input queue");
    }

    queue->setReleaseFence(prepare_param.fence_fd);

    // prepare OverlayPortParam
    OverlayPortParam* param = ovl_port_param;

    param->state          = OVL_IN_PARAM_ENABLE;
    if (buffer->secure)
    {
        param->va         = (void*)(uintptr_t)buffer->out_sec_handle;
        param->mva        = (void*)(uintptr_t)buffer->out_sec_handle;
    }
    else
    {
        param->va         = NULL;
        param->mva        = NULL;
    }
    param->pitch          = buffer->data_pitch;
    param->src_buf_width  = buffer->data_width;
    param->src_buf_height = buffer->data_height;
    param->v_pitch        = buffer->data_v_pitch;
    param->format         = buffer->data_format;
    param->dataspace      = buffer->dataspace;
    param->src_crop       = buffer->data_info.src_crop;
    param->dst_crop       = buffer->data_info.dst_crop;
    param->is_sharpen     = buffer->data_info.is_sharpen;
    param->fence_index    = prepare_param.fence_index;
    param->identity       = HWLAYER_ID_DBQ;
    param->connected_type = OVL_INPUT_QUEUE;
    param->protect        = buffer->protect;
    param->secure         = buffer->secure;
    param->alpha_enable   = buffer->alpha_enable;
    param->alpha          = buffer->alpha;
    param->blending       = buffer->blending;
    param->sequence       = buffer->sequence;
    param->dim            = false;
    param->ion_fd         = buffer->out_ion_fd;
    param->ext_sel_layer  = buffer->ext_sel_layer;
    param->fence          = buffer->acquire_fence;
    buffer->acquire_fence  = -1;
    param->fb_id = 0;
    param->alloc_id = buffer->alloc_id;
    param->hwc_layer_id = buffer->hwc_layer_id;
    param->size = buffer->buffer_size;

    // partial update - process mm layer dirty rect roughly
    param->ovl_dirty_rect_cnt = 1;
    param->ovl_dirty_rect[0].left   = param->src_crop.left;
    param->ovl_dirty_rect[0].top    = param->src_crop.top;
    param->ovl_dirty_rect[0].right  = param->src_crop.right;
    param->ovl_dirty_rect[0].bottom = param->src_crop.bottom;
    param->compress        = buffer->data_is_compress;
    param->is_mml = false;

    // if current frame gose to MML inline rotate
    if (HWC_MML_DISP_DIRECT_DECOUPLE_LAYER & hw_layer_caps)
    {
        param->is_mml = true;
        if (NULL == param->mml_cfg)
        {
            // allocate new memory for first use param.
            param->allocMMLCfg();
        }

        if (m_bliter_node->getMMLSubmit() != NULL)
        {
            copyMMLCfg(m_bliter_node->getMMLSubmit(), param->mml_cfg);
        }
    }

    BLOGD(ovl_id, "%s() id:%d ion_fd:%d rel_fence_idx:%d rel_fence_fd:%d p:%d v_p:%d caps:0x%x", __FUNCTION__,
          prepare_param.id, prepare_param.ion_fd, prepare_param.fence_index, prepare_param.fence_fd,
          param->pitch, param->v_pitch, hw_layer_caps);

    if (Platform::getInstance().m_config.dbg_switch & HWC_DBG_SWITCH_DEBUG_MM_BUFFER_INFO)
    {
        HWC_LOGI("MMPathPassingInfoCheck  hwc_layer_id %" PRIu64" dst alloc_id %" PRIu64" ",
                 param->hwc_layer_id, param->alloc_id);
    }

    return 0;
}

int AsyncBliterHandler::setOverlayPortParam(unsigned int ovl_id, const OverlayPortParam& ovl_port_param)
{
    OverlayPortParam* const* ovl_params = m_ovl_engine->getInputParams();
    copyOverlayPortParam(ovl_port_param, ovl_params[ovl_id]);
    return 0;
}

void AsyncBliterHandler::setMirror(
    const sp<HWCDisplay>& display,
    DispatcherJob* job)
{
    // clear all layer's retire fence
    if (display != nullptr)
    {
        if (HWC_DISPLAY_VIRTUAL == job->disp_ori_id)
        {
            for (auto& layer : display->getCommittedLayers())
            {
                layer->setReleaseFenceFd(-1, display->isConnected());
            }
        }
        else
        {
            display->clearAllFences();
        }
    }
    else
    {
        BLOGE(0, "setMirror, display is NULL");
        return;
    }

    // get release fence from mdp
    HWBuffer* hw_mirbuf = &job->hw_mirbuf;
    WDT_BL_NODE(createJob, job->fill_black.id, job->fill_black.fence);
    WDT_BL_NODE(createJob, job->mdp_job_output_buffer, hw_mirbuf->mir_in_rel_fence_fd);
    BLOGD(0, "create mirror fence by async: fill:%d|%d output:%d|%d",
        job->fill_black.id, job->fill_black.fence,
        job->mdp_job_output_buffer, hw_mirbuf->mir_in_rel_fence_fd);

    if (HWC_DISPLAY_VIRTUAL != job->disp_ori_id)
    {
        setPhyMirror(job);
    }
    // [NOTE]
    // there are two users who uses the retireFenceFd
    // 1) SurfaceFlinger 2) HWComposer
    // hence, the fence file descriptor MUST be DUPLICATED before
    // passing to SurfaceFlinger;
    // otherwise, HWComposer may wait for the WRONG fence file descriptor that
    // has been closed by SurfaceFlinger.
    //
    // we would let bliter output to virtual display's outbuf directly.
    // So retire fence is as same as bliter's release fence
    if (HWC_DISPLAY_VIRTUAL == job->disp_ori_id)
    {
        display->setRetireFenceFd(::dup(hw_mirbuf->mir_in_rel_fence_fd), display->isConnected());
    }
}

void AsyncBliterHandler::setBlack(
    const sp<HWCDisplay>& display,
    DispatcherJob* job)
{
    // clear all layer's retire fence
    if (display != nullptr)
    {
        if (HWC_DISPLAY_VIRTUAL == job->disp_ori_id)
        {
            for (auto& layer : display->getCommittedLayers())
            {
                layer->setReleaseFenceFd(-1, display->isConnected());
            }
        }
        else
        {
            display->clearAllFences();
        }
    }
    else
    {
        BLOGE(0, "setBlack, display is NULL");
        return;
    }

    // get release fence from mdp
    WDT_BL_NODE(createJob, job->fill_black.id, job->fill_black.fence);
    BLOGD(static_cast<unsigned int>(job->disp_ori_id), "create black fence by async: fill:%d|%d",
        job->fill_black.id, job->fill_black.fence);

    // [NOTE]
    // there are two users who uses the retireFenceFd
    // 1) SurfaceFlinger 2) HWComposer
    // hence, the fence file descriptor MUST be DUPLICATED before
    // passing to SurfaceFlinger;
    // otherwise, HWComposer may wait for the WRONG fence file descriptor that
    // has been closed by SurfaceFlinger.
    //
    // we would let bliter output to virtual display's outbuf directly.
    // So retire fence is as same as bliter's release fence

    HWBuffer& dst_buf = job->hw_outbuf;
    PrivateHandle priv_handle;

    int err = getPrivateHandle(dst_buf.handle, &priv_handle);
    if (0 != err)
    {
        display->setRetireFenceFd(-1, display->isConnected());
        BLOGE(static_cast<unsigned int>(job->disp_ori_id), "SetBlack Get priv_handle fail");
        return;
    }

    // judge attached secure buf if exists
    if (HWC_DISPLAY_VIRTUAL == job->disp_ori_id)
    {
        display->setRetireFenceFd(::dup(job->fill_black.fence), display->isConnected());
    }
}

bool AsyncBliterHandler::bypassBlit(HWLayer* hw_layer, uint32_t ovl_in)
{
    LightHwcLayer1* layer = &hw_layer->layer;
    int pool_id = hw_layer->priv_handle.ext_info.pool_id;

    if (doBlit(hw_layer))
    {
        BLOGD(ovl_in, "BLT/async=curr/pool=%d/rel=%d/acq=%d/handle=%p",
            pool_id, layer->releaseFenceFd, layer->acquireFenceFd, hw_layer->priv_handle.handle);

        return false;
    }

    HWC_ATRACE_NAME("bypass");
    BLOGD(ovl_in, "BLT/async=nop/pool=%d/handle=%p/fence=%d/skip_invalidate=%d",
        pool_id, hw_layer->priv_handle.handle, layer->releaseFenceFd, hw_layer->mdp_skip_invalidate);

    return true;
}

bool AsyncBliterHandler::doBlit(HWLayer* hw_layer)
{
    if (Platform::getInstance().m_config.dbg_mdp_always_blit)
    {
        return true;
    }

    if (HWC_MML_DISP_DIRECT_DECOUPLE_LAYER & hw_layer->layer_caps)
    {
        return true;
    }

    return hw_layer->dirty && !hw_layer->mdp_skip_invalidate;
}

sp<DisplayBufferQueue> AsyncBliterHandler::getDisplayBufferQueue(HWLayer *hw_layer)
{
    sp<DisplayBufferQueue> queue = nullptr;

    if (hw_layer)
    {
        queue = hw_layer->queue;
        if (queue == nullptr)
        {
            queue = new DisplayBufferQueue(DisplayBufferQueue::QUEUE_TYPE_BLT,
                                           hw_layer->hwc_layer->getId());
            hw_layer->queue = queue;
            hw_layer->hwc_layer->setBufferQueue(queue);
        }
    }
    else
    {
        queue = new DisplayBufferQueue(DisplayBufferQueue::QUEUE_TYPE_BLT);
    }

    if (queue == nullptr)
    {
        BLOGE(-1, "get buffer queue failed");
        return nullptr;
    }

    queue->setSynchronousMode(false);
    return queue;
}

void AsyncBliterHandler::configDisplayBufferQueue(sp<DisplayBufferQueue> queue, PrivateHandle* priv_handle,
      const DisplayData* disp_data, const uint32_t& assigned_format, const bool& assigned_compression) const
{
    unsigned int bpp = getBitsPerPixel(assigned_format);

    uint32_t buffer_w = static_cast<uint32_t>(ALIGN_CEIL_SIGN(disp_data->width, 2));
    uint32_t buffer_h = static_cast<uint32_t>(ALIGN_CEIL_SIGN(disp_data->height, 2));

    // set buffer format to buffer queue
    // TODO: should refer to layer->displayFrame
    DisplayBufferQueue::BufferParam buffer_param;
    buffer_param.disp_id = static_cast<int>(m_disp_id);
    buffer_param.pool_id = priv_handle->ext_info.pool_id;
    buffer_param.width   = buffer_w;
    buffer_param.height  = buffer_h;
    buffer_param.format  = assigned_format;
    // TODO: should calculate the size from the information of gralloc?
    buffer_param.size    = (buffer_w * buffer_h * bpp / 8);
    buffer_param.protect = usageHasProtected(priv_handle->usage);
    buffer_param.compression = assigned_compression;
    queue->setBufferParam(buffer_param);
}

status_t AsyncBliterHandler::setDpConfig(
    PrivateHandle* priv_handle, const DisplayBufferQueue::DisplayBuffer* disp_buf,
    int32_t src_dataspace, int32_t dst_dataspace,
    BufferConfig* config, uint32_t ovl_in)
{
    status_t result = NO_ERROR;

    // check if private color format is changed
    bool private_format_change = false;
    uint32_t curr_private_format = getPrivateFormat(*priv_handle);
    if (HAL_PIXEL_FORMAT_YUV_PRIVATE == priv_handle->format ||
        HAL_PIXEL_FORMAT_YCbCr_420_888 == priv_handle->format ||
        HAL_PIXEL_FORMAT_YUV_PRIVATE_10BIT == priv_handle->format)
    {
        if (HAL_PIXEL_FORMAT_YUV_PRIVATE == config->gralloc_format ||
            HAL_PIXEL_FORMAT_YCbCr_420_888 == config->gralloc_format ||
            HAL_PIXEL_FORMAT_YUV_PRIVATE_10BIT == config->gralloc_format)
        {
            private_format_change = (static_cast<uint32_t>(config->gralloc_private_format) != curr_private_format);
        }
    }

    bool ufo_align_change = false;
    int curr_ufo_align = 0;
    const bool src_compression = isCompressData(priv_handle);
    if ((HAL_PIXEL_FORMAT_UFO == priv_handle->format || HAL_PIXEL_FORMAT_UFO_AUO == priv_handle->format) ||
        GRALLOC_EXTRA_BIT_CM_UFO == curr_private_format)
    {
        curr_ufo_align = ((static_cast<unsigned int>(priv_handle->ext_info.status) & GRALLOC_EXTRA_MASK_UFO_ALIGN) >> 2);
        ufo_align_change = (config->gralloc_ufo_align_type != curr_ufo_align);
    }

    if (disp_buf != nullptr)
    {
        //we cannot just check reset dst dp config by reallocte DBQ or not,
        //becuse DBQ is on hwlayer, but BufferConfig is on AsyncBliterHandler,
        //and we have servel BufferConfigs depend on the number of OVL input
        DP_COLOR_ENUM dpformat = mapDpFormat(disp_buf->data_format);
        unsigned int dst_pitch = disp_buf->handle_stride * (getBitsPerPixel(disp_buf->data_format) / 8); // The unit is byte not pixel
        if(config->dst_dpformat == dpformat &&
           config->dst_width == disp_buf->data_width &&
           config->dst_height == disp_buf->data_height &&
           config->dst_pitch == dst_pitch &&
           config->dst_v_pitch == disp_buf->data_v_pitch &&
           config->dst_compression == disp_buf->data_is_compress &&
           config->dst_dataspace == dst_dataspace)
        {
            // data format is not changed
            if (!config->is_valid)
            {
                BLOGW(ovl_in, "Dst Format is not changed, but config in invalid !");
                result = -EINVAL;
            }
        }
        else
        {
            BLOGD(ovl_in, "Dst Format Change (w=%u h=%u s:%u vs:%u dpf=0x%x c=%d ds=0x%x) -> "
                          "(w=%u h=%u s:%u vs:%d dpf=0x%x c=%d ds=0x%x)",
                disp_buf->data_width, disp_buf->data_height, dst_pitch, disp_buf->data_v_pitch,
                dpformat, disp_buf->data_is_compress,  dst_dataspace,
                config->dst_width, config->dst_height, config->dst_pitch, config->dst_v_pitch,
                config->dst_dpformat, config->dst_compression, config->dst_dataspace);

            config->dst_dpformat = dpformat;

            switch (config->dst_dpformat)
            {
                case DP_COLOR_RGBA8888:
                //case DP_COLOR_RGBX8888: //same as DP_COLOR_RGBA8888
                case DP_COLOR_RGB888:
                case DP_COLOR_RGB565:
                case DP_COLOR_RGBA1010102:
                case DP_COLOR_YUYV:
                    config->dst_width = disp_buf->data_width;
                    config->dst_height = disp_buf->data_height;
                    config->dst_pitch = dst_pitch;
                    config->dst_pitch_uv = 0;
                    config->dst_v_pitch = disp_buf->data_v_pitch;
                    config->dst_plane = 1;
                    config->dst_size[0] = config->dst_pitch * config->dst_height;
                    config->dst_compression = disp_buf->data_is_compress;
                    config->dst_dataspace = dst_dataspace;
                    break;
                case DP_COLOR_NV12_10L:
                    config->dst_width = disp_buf->data_width;
                    config->dst_height = disp_buf->data_height;
                    config->dst_pitch = config->dst_width * 2;
                    config->dst_pitch_uv = config->dst_pitch;
                    config->dst_v_pitch = 0;
                    config->dst_plane = 2;
                    config->dst_size[0] = config->dst_pitch * ALIGN_CEIL(config->dst_height, 2);
                    config->dst_size[1]  = config->dst_pitch_uv * ALIGN_CEIL(config->dst_height, 2) / 2;
                    config->dst_compression = disp_buf->data_is_compress;
                    config->dst_dataspace = dst_dataspace;
                    break;
                default:
                    BLOGE(ovl_in, "dst_dpformat is invalid (0x%x)", config->dst_dpformat);
                    memset(config, 0, sizeof(BufferConfig));
                    return -EINVAL;
            }
        }
    }

    if (config->gralloc_prexform == priv_handle->prexform &&
        config->gralloc_width  == priv_handle->width  &&
        config->gralloc_height == priv_handle->height &&
        config->gralloc_stride  == priv_handle->y_stride &&
        config->gralloc_vertical_stride == priv_handle->vstride &&
        config->gralloc_cbcr_align == priv_handle->cbcr_align &&
        config->gralloc_format == priv_handle->format &&
        config->src_compression == src_compression &&
        config->src_dataspace == src_dataspace &&
        private_format_change  == false &&
        ufo_align_change == false)
    {
        // data format is not changed
        if (!config->is_valid)
        {
            BLOGW(ovl_in, "Src Format is not changed, but config in invalid !");
            return -EINVAL;
        }

        return result;
    }

    BLOGD(ovl_in, "Src Format Change (w=%u h=%u s:%u vs:%u f=0x%x c=%d ds=0x%x) -> "
                  "(w=%u h=%u s:%u vs:%u f=0x%x pf=0x%x ua=%d c=%d ds=0x%x)",
        config->gralloc_width, config->gralloc_height, config->gralloc_stride,
        config->gralloc_vertical_stride, config->gralloc_format, config->src_compression,
        config->src_dataspace,
        priv_handle->width, priv_handle->height, priv_handle->y_stride,
        priv_handle->vstride, priv_handle->format,
        curr_private_format, curr_ufo_align, src_compression,
        src_dataspace);

    // remember current buffer data format for next comparison
    config->gralloc_prexform = priv_handle->prexform;
    config->gralloc_width  = priv_handle->width;
    config->gralloc_height = priv_handle->height;
    config->gralloc_stride = priv_handle->y_stride;
    config->gralloc_cbcr_align = priv_handle->cbcr_align;
    config->gralloc_vertical_stride = priv_handle->vstride;
    config->gralloc_format = priv_handle->format;
    config->gralloc_private_format = static_cast<int32_t>(curr_private_format);
    config->gralloc_ufo_align_type = curr_ufo_align;
    config->src_compression = src_compression;

    //
    // set DpFramework configuration
    //
    unsigned int src_size_luma = 0;
    unsigned int src_size_chroma = 0;

    unsigned int width  = priv_handle->width;
    unsigned int height = priv_handle->height;
    unsigned int y_stride = priv_handle->y_stride;
    unsigned int vertical_stride = priv_handle->vstride;

    // reset uv pitch since RGB does not need it
    config->src_pitch_uv = 0;

    unsigned int input_format = grallocColor2HalColor(priv_handle->format, static_cast<int32_t>(curr_private_format));
    if (input_format == 0)
    {
        BLOGE(ovl_in, "Private Format is invalid (0x%x)", curr_private_format);
        memset(config, 0, sizeof(BufferConfig));
        return -EINVAL;
    }

    // remember real height since height should be aligned with 32 for NV12_BLK
    config->src_width = y_stride;
    config->src_height = height;
    config->deinterlace = false;

    // get color range configuration
    config->src_dataspace = src_dataspace;

    uint64_t compression_type = HWCMediator::getInstance().getCompressionType();
    uint32_t y_header = 0;
    uint32_t y_data = 0;
    uint32_t c_header = 0;
    uint32_t c_data = 0;

    switch (input_format)
    {
        case HAL_PIXEL_FORMAT_RGBA_8888:
            config->src_pitch = y_stride * 4;
            config->src_plane = 1;
            config->src_size[0] = config->src_pitch * height;
            config->src_dpformat = DP_COLOR_RGBA8888;
            break;

        case HAL_PIXEL_FORMAT_RGBX_8888:
            config->src_pitch = y_stride * 4;
            config->src_plane = 1;
            config->src_size[0] = config->src_pitch * height;
            config->src_dpformat = DP_COLOR_RGBA8888;
            break;

        case HAL_PIXEL_FORMAT_BGRA_8888:
            config->src_pitch = y_stride * 4;
            config->src_plane = 1;
            config->src_size[0] = config->src_pitch * height;
            config->src_dpformat = DP_COLOR_BGRA8888;
            break;

        case HAL_PIXEL_FORMAT_BGRX_8888:
        case HAL_PIXEL_FORMAT_IMG1_BGRX_8888:
            config->src_pitch = y_stride * 4;
            config->src_plane = 1;
            config->src_size[0] = config->src_pitch * height;
            config->src_dpformat = DP_COLOR_BGRA8888;
            break;

        case HAL_PIXEL_FORMAT_RGBA_1010102:
            config->src_pitch = y_stride * 4;
            config->src_plane = 1;
            config->src_size[0] = config->src_pitch * height;
            config->src_dpformat = DP_COLOR_RGBA1010102;
            break;

        case HAL_PIXEL_FORMAT_RGB_888:
            config->src_pitch = y_stride * 3;
            config->src_plane = 1;
            config->src_size[0] = config->src_pitch * height;
            config->src_dpformat = DP_COLOR_RGB888;
            break;

        case HAL_PIXEL_FORMAT_BGR_888:
            config->src_pitch = y_stride * 3;
            config->src_plane = 1;
            config->src_size[0] = config->src_pitch * height;
            config->src_dpformat = DP_COLOR_BGR888;
            break;

        case HAL_PIXEL_FORMAT_RGB_565:
            config->src_pitch = y_stride * 2;
            config->src_plane = 1;
            config->src_size[0] = config->src_pitch * height;
            config->src_dpformat = DP_COLOR_RGB565;
            break;

        case HAL_PIXEL_FORMAT_NV12_BLK_FCM:
            config->src_pitch    = y_stride * 32;
            config->src_pitch_uv = ALIGN_CEIL((y_stride / 2), priv_handle->cbcr_align ? priv_handle->cbcr_align : 16) * 2 * 16;
            config->src_plane = 2;
            config->src_height = vertical_stride;
            src_size_luma = y_stride * vertical_stride;
            config->src_size[0] = src_size_luma;
            config->src_size[1] = src_size_luma / 4 * 2;
            config->src_dpformat = DP_COLOR_420_BLKI;
            break;

        case HAL_PIXEL_FORMAT_NV12_BLK:
            config->src_pitch    = y_stride * 32;
            config->src_pitch_uv = ALIGN_CEIL((y_stride / 2), priv_handle->cbcr_align ? priv_handle->cbcr_align : 16) * 2 * 16;
            config->src_plane = 2;
            config->src_height = vertical_stride;
            src_size_luma = y_stride * vertical_stride;
            config->src_size[0] = src_size_luma;
            config->src_size[1] = src_size_luma / 4 * 2;
            config->src_dpformat = DP_COLOR_420_BLKP;
            break;

        case HAL_PIXEL_FORMAT_I420:
            config->src_pitch    = y_stride;
            config->src_pitch_uv = ALIGN_CEIL((y_stride / 2), priv_handle->cbcr_align ? priv_handle->cbcr_align : 1);
            config->src_plane = 3;
            src_size_luma = y_stride * vertical_stride;
            src_size_chroma = config->src_pitch_uv * vertical_stride / 2;
            config->src_size[0] = src_size_luma;
            config->src_size[1] = src_size_chroma;
            config->src_size[2] = src_size_chroma;
            config->src_dpformat = DP_COLOR_I420;
            break;

        case HAL_PIXEL_FORMAT_I420_DI:
            config->src_pitch    = y_stride * 2;
            config->src_pitch_uv = ALIGN_CEIL(y_stride, priv_handle->cbcr_align ? priv_handle->cbcr_align : 1);
            config->src_plane = 3;
            src_size_luma = y_stride * height;
            src_size_chroma = src_size_luma / 4;
            config->src_size[0] = src_size_luma;
            config->src_size[1] = src_size_chroma;
            config->src_size[2] = src_size_chroma;
            config->src_dpformat = DP_COLOR_I420;
            config->deinterlace = true;
            break;

        case HAL_PIXEL_FORMAT_YV12:
            config->src_pitch    = y_stride;
            config->src_pitch_uv = ALIGN_CEIL((y_stride / 2), priv_handle->cbcr_align ? priv_handle->cbcr_align : 16);
            config->src_plane = 3;
            src_size_luma = y_stride * ALIGN_CEIL(height, priv_handle->v_align ? priv_handle->v_align : 2);
            src_size_chroma = config->src_pitch_uv * ALIGN_CEIL(height, priv_handle->v_align ? priv_handle->v_align : 2) / 2;
            config->src_size[0] = src_size_luma;
            config->src_size[1] = src_size_chroma;
            config->src_size[2] = src_size_chroma;
            config->src_dpformat = DP_COLOR_YV12;
            break;

        case HAL_PIXEL_FORMAT_YV12_DI:
            config->src_pitch    = y_stride;
            config->src_pitch_uv = ALIGN_CEIL(y_stride, priv_handle->cbcr_align ? priv_handle->cbcr_align : 16);
            config->src_plane = 3;
            src_size_luma = y_stride * height;
            src_size_chroma = src_size_luma / 4;
            config->src_size[0] = src_size_luma;
            config->src_size[1] = src_size_chroma;
            config->src_size[2] = src_size_chroma;
            config->src_dpformat = DP_COLOR_YV12;
            config->deinterlace = true;
            break;

        case HAL_PIXEL_FORMAT_YUYV:
        case HAL_PIXEL_FORMAT_YCbCr_422_I:
            config->src_pitch    = y_stride * 2;
            config->src_pitch_uv = 0;
            config->src_plane = 1;
            config->src_size[0] = y_stride * height * 2;
            config->src_dpformat = DP_COLOR_YUYV;
            break;

        case HAL_PIXEL_FORMAT_UFO:
        case HAL_PIXEL_FORMAT_UFO_AUO:
            width = y_stride;
            height = vertical_stride;
            config->src_height   = height;
            config->src_pitch    = width * 32;
            config->src_pitch_uv = width * 16;
            config->src_plane = 2;
            // calculate PIC_SIZE_Y, need align 512
            src_size_luma = ALIGN_CEIL(width * height, 512);
            config->src_size[0] = src_size_luma;
            config->src_size[1] = src_size_luma;
            config->src_dpformat = (input_format == HAL_PIXEL_FORMAT_UFO) ? DP_COLOR_420_BLKP_UFO : DP_COLOR_420_BLKP_UFO_AUO;
            break;

        case HAL_PIXEL_FORMAT_NV12:
            if (src_compression == false)
            {
                config->src_pitch    = y_stride;
                config->src_pitch_uv = ALIGN_CEIL((y_stride), priv_handle->cbcr_align ? priv_handle->cbcr_align : 1);
                config->src_plane = 2;
                src_size_luma = y_stride * vertical_stride;
                src_size_chroma = config->src_pitch_uv * vertical_stride / 2;
                config->src_size[0] = src_size_luma;
                config->src_size[1] = src_size_chroma;
            }
            else
            {
                switch (compression_type)
                {
                    case HWC_COMPRESSION_TYPE_AFBC:
                        config->src_pitch    = width * 3 / 2;
                        config->src_pitch_uv = config->src_pitch;
                        config->src_plane = 1;
                        config->src_width = ALIGN_CEIL(y_stride, 32);
                        config->src_height = ALIGN_CEIL(height, 32);
                        config->src_size[0] =
                                (ALIGN_CEIL(width, 32) * ALIGN_CEIL(height, 32) * 3 / 2) +
                                ALIGN_CEIL(16 * (ALIGN_CEIL(width, 32) / 16) *
                                           (ALIGN_CEIL(height, 32) / 16), 256);
                        break;

                    default:
                        HWC_LOGE("%s: try to config NV12 with unknown compression type[%" PRIu64 "]",
                                __func__, compression_type);
                        break;
                }
            }
            config->src_dpformat = DP_COLOR_NV12;
            break;

        case HAL_PIXEL_FORMAT_YCRCB_420_SP:
            config->src_pitch    = y_stride;
            config->src_pitch_uv = ALIGN_CEIL((y_stride), priv_handle->cbcr_align ? priv_handle->cbcr_align : 1);
            config->src_plane = 2;
            src_size_luma = y_stride * vertical_stride;
            src_size_chroma = config->src_pitch_uv * vertical_stride / 2;
            config->src_size[0] = src_size_luma;
            config->src_size[1] = src_size_chroma;
            config->src_dpformat = DP_COLOR_NV21;
            break;

        case HAL_PIXEL_FORMAT_NV12_BLK_10BIT_H:
        case HAL_PIXEL_FORMAT_NV12_BLK_10BIT_H_JUMP:
            config->src_pitch    = y_stride * 40;
            config->src_pitch_uv = ALIGN_CEIL((y_stride / 2), priv_handle->cbcr_align ? priv_handle->cbcr_align : 20) * 2 * 20;
            config->src_plane = 2;
            config->src_height = vertical_stride;
            src_size_luma = ALIGN_CEIL(y_stride * vertical_stride * 5 / 4, 40);
            // Because the start address of chroma has to be a multiple of 512, we align luma size
            // with 512 to adjust chroma address.
            config->src_size[0] = ALIGN_CEIL(src_size_luma, 512);
            config->src_size[1] = src_size_luma / 4 * 2;
            config->src_dpformat = (input_format == HAL_PIXEL_FORMAT_NV12_BLK_10BIT_H) ? DP_COLOR_420_BLKP_10_H : DP_COLOR_420_BLKP_10_H_JUMP;
            break;

        case HAL_PIXEL_FORMAT_NV12_BLK_10BIT_V:
        case HAL_PIXEL_FORMAT_NV12_BLK_10BIT_V_JUMP:
            config->src_pitch    = y_stride * 40;
            config->src_pitch_uv = ALIGN_CEIL((y_stride / 2), priv_handle->cbcr_align ? priv_handle->cbcr_align : 20) * 2 * 20;
            config->src_plane = 2;
            config->src_height = vertical_stride;
            src_size_luma = ALIGN_CEIL(y_stride * vertical_stride * 5 / 4, 40);
            // Because the start address of chroma has to be a multiple of 512, we align luma size
            // with 512 to adjust chroma address.
            config->src_size[0] = ALIGN_CEIL(src_size_luma, 512);
            config->src_size[1] = src_size_luma / 4 * 2;
            config->src_dpformat = (input_format == HAL_PIXEL_FORMAT_NV12_BLK_10BIT_V) ? DP_COLOR_420_BLKP_10_V : DP_COLOR_420_BLKP_10_V_JUMP;
            break;

        case HAL_PIXEL_FORMAT_UFO_10BIT_H:
        case HAL_PIXEL_FORMAT_UFO_10BIT_H_JUMP:
            width = y_stride;
            height = vertical_stride;
            config->src_height   = height;
            config->src_pitch    = width * 40;
            config->src_pitch_uv = width * 20;
            config->src_plane = 2;
            src_size_luma = ALIGN_CEIL(width * height * 5 / 4, 4096);
            config->src_size[0] = src_size_luma;
            config->src_size[1] = src_size_luma;
            config->src_dpformat = (input_format == HAL_PIXEL_FORMAT_UFO_10BIT_H) ? DP_COLOR_420_BLKP_UFO_10_H : DP_COLOR_420_BLKP_UFO_10_H_JUMP;
            break;

        case HAL_PIXEL_FORMAT_UFO_10BIT_V:
        case HAL_PIXEL_FORMAT_UFO_10BIT_V_JUMP:
            width = y_stride;
            height = vertical_stride;
            config->src_height   = height;
            config->src_pitch    = width * 40;
            config->src_pitch_uv = width * 20;
            config->src_plane = 2;
            src_size_luma = ALIGN_CEIL(width * height * 5 / 4, 4096);
            config->src_size[0] = src_size_luma;
            config->src_size[1] = src_size_luma;
            config->src_dpformat = (input_format == HAL_PIXEL_FORMAT_UFO_10BIT_V) ? DP_COLOR_420_BLKP_UFO_10_V : DP_COLOR_420_BLKP_UFO_10_V_JUMP;
            break;

        case HAL_PIXEL_FORMAT_YCBCR_P010:
            if (src_compression == false)
            {
                config->src_pitch    = y_stride * 2;
                config->src_pitch_uv = ALIGN_CEIL((y_stride * 2), priv_handle->cbcr_align ? priv_handle->cbcr_align : 1);
                config->src_plane    = 2;
                src_size_luma = config->src_pitch * vertical_stride;
                src_size_chroma = config->src_pitch_uv * vertical_stride / 2;
                config->src_size[0] = src_size_luma;
                config->src_size[1] = src_size_chroma;
            }
            else
            {
                switch (compression_type)
                {
                    case HWC_COMPRESSION_TYPE_AFBC:
                        config->src_pitch    = width * 2;
                        config->src_pitch_uv = width * 2;
                        config->src_plane    = 1;
                        config->src_width = ALIGN_CEIL(y_stride, 32);
                        config->src_height = ALIGN_CEIL(height, 32);
                        config->src_size[0] = (ALIGN_CEIL(width, 32) * ALIGN_CEIL(height, 32) * 2) +
                                ALIGN_CEIL(16 * (ALIGN_CEIL(width, 32) / 16) *
                                           (ALIGN_CEIL(height, 32) / 16), 256);
                        break;

                    default:
                        HWC_LOGE("%s: try to config P010 with unknown compression type[%" PRIu64"]",
                                __func__, compression_type);
                        break;
                }
            }
            config->src_dpformat = DP_COLOR_NV12_10L;
            break;

        case GM_PIXEL_FORMAT_NV12_HYFBC:
            config->src_pitch = y_stride * 12;
            config->src_pitch_uv = 0;
            config->src_plane = 1;
            config->src_width = ALIGN_CEIL(width, 64);
            config->src_height = ALIGN_CEIL(height, 64);

            y_header = (config->src_width * config->src_height + 63) >> 6;
            y_data = config->src_width * config->src_height;
            c_header = (config->src_width * config->src_height / 2 + 63) >> 6;
            c_data = y_data >> 1;

            config->src_size[0] = ALIGN_CEIL(y_header, 4096) + ALIGN_CEIL(y_data, 4096);
            config->src_size[0] += ALIGN_CEIL(c_header, 4096) + c_data;

            config->src_dpformat = DP_COLOR_NV12_HYFBC;
            break;

        case GM_PIXEL_FORMAT_P010_HYFBC:
            config->src_pitch = y_stride * 18;
            config->src_pitch_uv = 0;
            config->src_plane = 1;
            config->src_width = ALIGN_CEIL(width, 64);
            config->src_height = ALIGN_CEIL(height, 64);

            y_header = (config->src_width * config->src_height + 63) >> 6;
            y_data = (config->src_width * config->src_height * 6) >> 2;
            c_header = (config->src_width * config->src_height / 2 + 63) >> 6;
            c_data = y_data >> 1;

            config->src_size[0] = ALIGN_CEIL(y_header, 4096) + ALIGN_CEIL(y_data, 4096);
            config->src_size[0] += ALIGN_CEIL(c_header, 4096) + c_data;
            config->src_dpformat = DP_COLOR_P010_HYFBC;
            break;

        default:
            BLOGE(ovl_in, "Color format for DP is invalid (0x%x)", input_format);
            //config->is_valid = false;
            memset(config, 0, sizeof(BufferConfig));
            return -EINVAL;
    }

    config->is_valid = true;
    return result;
}

status_t AsyncBliterHandler::setDstDpConfig(PrivateHandle& dst_priv_handle, BufferConfig* config)
{
    config->dst_width    = dst_priv_handle.width;
    config->dst_height   = dst_priv_handle.height;
    config->dst_v_pitch  = 0;

    switch (dst_priv_handle.format)
    {
        case HAL_PIXEL_FORMAT_RGBA_8888:
            config->dst_dpformat = DP_COLOR_RGBA8888;
            config->dst_pitch    = dst_priv_handle.y_stride * 4;
            config->dst_pitch_uv = 0;
            config->dst_plane    = 1;
            config->dst_size[0]  = config->dst_pitch * config->dst_height;
            config->dst_dataspace = HAL_DATASPACE_V0_BT601_625;
            break;

        case HAL_PIXEL_FORMAT_RGBX_8888:
            config->dst_dpformat = DP_COLOR_RGBX8888;
            config->dst_pitch    = dst_priv_handle.y_stride * 4;
            config->dst_pitch_uv = 0;
            config->dst_plane    = 1;
            config->dst_size[0]  = config->dst_pitch * config->dst_height;
            config->dst_dataspace = HAL_DATASPACE_V0_BT601_625;
            break;

        case HAL_PIXEL_FORMAT_YV12:
            config->dst_dpformat = DP_COLOR_YV12;
            config->dst_pitch    = dst_priv_handle.y_stride;
            config->dst_pitch_uv = ALIGN_CEIL((dst_priv_handle.y_stride / 2), 16);
            config->dst_plane    = 3;
            config->dst_size[0]  = config->dst_pitch * config->dst_height;
            config->dst_size[1]  = config->dst_pitch_uv * (config->dst_height / 2);
            config->dst_size[2]  = config->dst_size[1];

            // WORKAROUND: VENC only accpet BT601 limit range
            config->dst_dataspace = HAL_DATASPACE_V0_BT601_625;
            break;

        case HAL_PIXEL_FORMAT_RGB_888:
            config->dst_dpformat = DP_COLOR_RGB888;
            config->dst_pitch    = dst_priv_handle.y_stride * 3;
            config->dst_pitch_uv = 0;
            config->dst_plane    = 1;
            config->dst_size[0]  = config->dst_pitch * config->dst_height;
            config->dst_dataspace = HAL_DATASPACE_V0_BT601_625;
            break;

        case HAL_PIXEL_FORMAT_BGR_888:
            config->dst_dpformat = DP_COLOR_BGR888;
            config->dst_pitch    = dst_priv_handle.y_stride * 3;
            config->dst_pitch_uv = 0;
            config->dst_plane    = 1;
            config->dst_size[0]  = config->dst_pitch * config->dst_height;
            config->dst_dataspace = HAL_DATASPACE_V0_BT601_625;
            break;

        case HAL_PIXEL_FORMAT_RGB_565:
            config->dst_dpformat = DP_COLOR_RGB565;
            config->dst_pitch    = dst_priv_handle.y_stride * 2;
            config->dst_pitch_uv = 0;
            config->dst_plane    = 1;
            config->dst_size[0]  = config->dst_pitch * config->dst_height;
            config->dst_dataspace = HAL_DATASPACE_V0_BT601_625;
            break;

        case HAL_PIXEL_FORMAT_YUYV:
        case HAL_PIXEL_FORMAT_YCbCr_422_I:
            config->dst_dpformat = DP_COLOR_YUYV;
            config->dst_pitch    = dst_priv_handle.y_stride * 2;
            config->dst_pitch_uv = 0;
            config->dst_plane    = 1;
            config->dst_size[0]  = config->dst_pitch * config->dst_height;
            config->dst_dataspace = HAL_DATASPACE_V0_BT601_625;
            break;

        case HAL_PIXEL_FORMAT_YCRCB_420_SP:
            config->dst_dpformat = DP_COLOR_NV21;
            config->dst_pitch    = dst_priv_handle.y_stride;
            config->dst_pitch_uv = ALIGN_CEIL((dst_priv_handle.y_stride),
                                   dst_priv_handle.cbcr_align ? dst_priv_handle.cbcr_align : 1);
            config->dst_plane    = 2;
            config->dst_size[0]  = dst_priv_handle.y_stride * ALIGN_CEIL(dst_priv_handle.height, 2);
            config->dst_size[1]  = config->dst_pitch_uv * ALIGN_CEIL(dst_priv_handle.height, 2) / 2;
            config->dst_dataspace = HAL_DATASPACE_V0_BT601_625;
            break;

        case HAL_PIXEL_FORMAT_YCBCR_P010:
            config->dst_dpformat = DP_COLOR_NV12_10L;
            config->dst_pitch    = dst_priv_handle.y_stride * 2;
            config->dst_pitch_uv = config->dst_pitch;
            config->dst_plane    = 2;
            config->dst_size[0]  = config->dst_pitch * ALIGN_CEIL(dst_priv_handle.height, 2);
            config->dst_size[1]  = config->dst_pitch_uv * ALIGN_CEIL(dst_priv_handle.height, 2) / 2;
            // TODO: 10bit csc may different?
            config->dst_dataspace = HAL_DATASPACE_V0_BT601_625;
            break;

        case HAL_PIXEL_FORMAT_RGBA_1010102:
            config->dst_dpformat = DP_COLOR_RGBA1010102;
            config->dst_pitch    = dst_priv_handle.y_stride * 4;
            config->dst_pitch_uv = 0;
            config->dst_plane    = 1;
            config->dst_size[0]  = config->dst_pitch * config->dst_height;
            // TODO: 10bit csc may different?
            config->dst_dataspace = HAL_DATASPACE_V0_BT601_625;
            break;



        default:
            config->dst_size[0]  = config->dst_pitch * config->dst_height;
            HWC_LOGW("setDstDpConfig format(0x%x) unexpected", dst_priv_handle.format);
            return -EINVAL;
    }

    return NO_ERROR;
}

void AsyncBliterHandler::process(DispatcherJob* job)
{
    if (HWC_MIRROR_SOURCE_INVALID != job->disp_mir_id)
    {
        if (HWC_DISPLAY_VIRTUAL == job->disp_ori_id)
        {
            processVirMirror(job);
        }
        else
        {
            processPhyMirror(job);
        }
        return;
    }

    if (job->is_black_job)
    {
        if (HWC_DISPLAY_VIRTUAL == job->disp_ori_id)
        {
            processVirBlack(job);
        }
        return;
    }

    uint32_t total_num = job->num_layers;

    for (uint32_t i = 0; i < total_num; i++)
    {
        HWLayer* hw_layer = &job->hw_layers[i];

        // skip non mm layers
        if (HWC_LAYER_TYPE_MM != hw_layer->type) continue;

        // this layer is not enable
        if (!hw_layer->enable) continue;

        setOverlayPortParam(i, hw_layer->ovl_port_param);

        if (bypassBlit(hw_layer, i))
        {
            continue;
        }

        if (!isMMLLayer(hw_layer))
        {
            WDT_BL_NODE(invalidate, hw_layer->mdp_job_id, job->sequence, job->active_config,
                        job->present_after_ts, job->decouple_target_ts);
        }

        LightHwcLayer1* layer = &hw_layer->layer;
        closeFenceFd(&layer->acquireFenceFd);
        closeFenceFd(&layer->releaseFenceFd);
    }
}

void AsyncBliterHandler::nullop()
{
}

void AsyncBliterHandler::nullop(const uint32_t& job_id)
{
    WDT_BL_NODE(cancelJob, job_id);
}

void AsyncBliterHandler::cancelLayers(DispatcherJob* job)
{
    // cancel mm layers for dropping job
    if (HWC_MIRROR_SOURCE_INVALID != job->disp_mir_id)
    {
        cancelMirror(job);
        return;
    }

    uint32_t total_num = job->num_layers;

    for (uint32_t i = 0; i < total_num; i++)
    {
        HWLayer* hw_layer = &job->hw_layers[i];

        // skip non mm layers
        if (HWC_LAYER_TYPE_MM != hw_layer->type) continue;

        // this layer is not enable
        if (!hw_layer->enable) continue;

        if (bypassBlit(hw_layer, i)) continue;

        LightHwcLayer1* layer = &hw_layer->layer;

        BLOGD(i, "CANCEL/rel=%d/acq=%d/handle=%p",
            layer->releaseFenceFd, layer->acquireFenceFd, hw_layer->priv_handle.handle);

        if (DisplayManager::m_profile_level & PROFILE_BLT)
        {
            char atrace_tag[256];
            if (snprintf(atrace_tag, sizeof(atrace_tag), "mm cancel:%p", hw_layer->priv_handle.handle) > 0)
            {
                HWC_ATRACE_NAME(atrace_tag);
            }
        }

        if (layer->acquireFenceFd != -1) ::protectedClose(layer->acquireFenceFd);
        layer->acquireFenceFd = -1;

        if (hw_layer->mdp_job_id != 0) {
            nullop(hw_layer->mdp_job_id);
        }
        protectedClose(layer->releaseFenceFd);
        layer->releaseFenceFd = -1;

        if (hw_layer->priv_handle.ion_fd > 0)
        {
            IONDevice::getInstance().ionClose(hw_layer->priv_handle.ion_fd);
        }
    }
}

void AsyncBliterHandler::cancelMirror(DispatcherJob* job)
{
    // cancel mirror path output buffer for dropping job
    HWBuffer* hw_mirbuf = &job->hw_mirbuf;
    if (-1 != hw_mirbuf->mir_in_rel_fence_fd)
    {
        protectedClose(hw_mirbuf->mir_in_rel_fence_fd);
        hw_mirbuf->mir_in_rel_fence_fd = -1;
    }

    cancelFillBlackJob(job->fill_black);
    nullop(job->mdp_job_output_buffer);
    clearMdpJob(job->fill_black);

    if (hw_mirbuf->mir_in_acq_fence_fd != -1) ::protectedClose(hw_mirbuf->mir_in_acq_fence_fd);
    hw_mirbuf->mir_in_acq_fence_fd = -1;
}

static uint32_t  transform_table[4][4] =
{
    { 0x00, 0x07, 0x03, 0x04 },
    { 0x04, 0x00, 0x07, 0x03 },
    { 0x03, 0x04, 0x00, 0x07 },
    { 0x07, 0x03, 0x04, 0x00 }
};


void AsyncBliterHandler::calculateMirRoiXform(
                uint32_t* xform, Rect* src_roi, Rect* dst_roi, DispatcherJob* job)
{
    // ROT   0 = 000
    // ROT  90 = 100
    // ROT 180 = 011
    // ROT 270 = 111
    // count num of set bit as index for transform_table
    unsigned int ori_rot = job->disp_ori_rot;
    ori_rot = (ori_rot & 0x1) + ((ori_rot>>1) & 0x1) + ((ori_rot>>2) & 0x1);
    unsigned int mir_rot = job->disp_mir_rot;
    mir_rot = (mir_rot & 0x1) + ((mir_rot>>1) & 0x1) + ((mir_rot>>2) & 0x1);

    const DisplayData* ori_disp_data = job->disp_data;
    const DisplayData* mir_disp_data = job->mir_disp_data;

    // correct ori_disp transform with its hwrotation
    if (0 != ori_disp_data->hwrotation)
    {
        ori_rot = (ori_rot + ori_disp_data->hwrotation) % 4;
    }

    // correct ori_disp trasform with display driver's cap: is_output_rotated
    if (1 == HWCMediator::getInstance().getOvlDevice(static_cast<uint64_t>(job->disp_mir_id))->getDisplayOutputRotated())
    {
        ori_rot = (ori_rot + 2) % 4;
    }

    // correct mir_disp transform with its hwrotation
    if (0 != mir_disp_data->hwrotation)
    {
        mir_rot = (mir_rot + mir_disp_data->hwrotation) % 4;
    }

    *xform = transform_table[ori_rot][mir_rot];

    unsigned int rect_sel = ori_rot > mir_rot ? (ori_rot - mir_rot) : (mir_rot - ori_rot);
    if (rect_sel & 0x1)
    {
        *src_roi = mir_disp_data->mir_landscape;
        *dst_roi = ori_disp_data->mir_landscape;
    }
    else
    {
        *src_roi = mir_disp_data->mir_portrait;
        *dst_roi = ori_disp_data->mir_portrait;
    }
}

void AsyncBliterHandler::setPhyMirror(DispatcherJob* job)
{
    unsigned int ovl_id = 0;

    HWBuffer& src_buf = job->hw_mirbuf;
    HWBuffer& dst_buf = job->hw_outbuf;

    PrivateHandle& src_priv_handle = src_buf.priv_handle;
    PrivateHandle& dst_priv_handle = dst_buf.priv_handle;

    BufferConfig* config = &m_dp_configs[m_ovl_engine->getMaxInputNum()];
    DisplayBufferQueue::DisplayBuffer disp_buffer;

    // some platforms rdma not support roi_update
    bool full_screen_update = (0 == Platform::getInstance().m_config.rdma_roi_update) &&
                              (1 == job->num_layers);

    // get buffer queue
    if (m_mirror_queue == nullptr)
    {
        m_mirror_queue = getDisplayBufferQueue();
        if (m_mirror_queue == nullptr)
        {
            BLOGE(ovl_id, "%s(), m_mirror_queue == nullptr", __FUNCTION__);
            return;
        }
    }
    sp<DisplayBufferQueue> queue = m_mirror_queue;

    // config buffer queue
    configDisplayBufferQueue(queue, &dst_priv_handle, job->disp_data, convertFormat4Bliter(dst_priv_handle.format));

    status_t err = queue->dequeueBuffer(&disp_buffer, true);

    if (err == NO_ERROR)
    {
        status_t err = setDpConfig(&src_priv_handle, &disp_buffer,
                                   src_buf.dataspace, dst_buf.dataspace,
                                   config, ovl_id);
        if (err != NO_ERROR)
        {
            BLOGE(ovl_id, "%s(): setDpConfig() goes wrong err=%d", __func__, err);
        }
    }
    else
    {
        BLOGE(ovl_id, "%s(): dequeueBuffer() goes wrong err=%d", __func__, err);
    }

    if (NO_ERROR == err)
    {
        Rect src_roi;
        Rect dst_roi;
        uint32_t xform;

        calculateMirRoiXform(&xform, &src_roi, &dst_roi, job);

        if (full_screen_update)
        {
            clearBackground(disp_buffer.out_handle,
                            &dst_roi,
                            &disp_buffer.release_fence,
                            job->fill_black);
        }

        BliterNode::Parameter param = {src_roi, dst_roi, config, xform, false, disp_buffer.secure};
        WDT_BL_NODE(setSrc, job->mdp_job_output_buffer, config, src_priv_handle, &src_buf.mir_in_acq_fence_fd);
        WDT_BL_NODE(setDst, job->mdp_job_output_buffer, &param, disp_buffer.out_ion_fd,
                disp_buffer.out_sec_handle,
                &disp_buffer.release_fence);
        WDT_BL_NODE(calculateAllROI, job->mdp_job_output_buffer);

        passFenceFd(&disp_buffer.acquire_fence, &src_buf.mir_in_rel_fence_fd);
        if (full_screen_update)
        {
            int dst_crop_x = 0;
            int dst_crop_y = 0;
            int dst_crop_w = job->disp_data->width;
            int dst_crop_h = job->disp_data->height;
            Rect base_crop(Rect(0, 0, dst_crop_w, dst_crop_h));

            disp_buffer.data_info.src_crop   = base_crop;
            disp_buffer.data_info.dst_crop   = base_crop.offsetTo(dst_crop_x, dst_crop_y);
        }
        else
        {
            dst_roi.left = ALIGN_CEIL_SIGN(dst_roi.left, 2);
            dst_roi.right = ALIGN_FLOOR(dst_roi.right, 2);
            disp_buffer.data_info.src_crop   = dst_roi;
            disp_buffer.data_info.dst_crop   = dst_roi;
        }
        disp_buffer.data_info.is_sharpen = false;
        disp_buffer.alpha_enable         = 1;
        disp_buffer.alpha                = 0xFF;
        disp_buffer.sequence             = job->sequence;
        disp_buffer.src_handle           = src_buf.handle;

        queue->queueBuffer(&disp_buffer);
        prepareOverlayPortParam(ovl_id, job, queue, &job->mdp_mirror_ovl_port_param, true);
    }
    else
    {
        BLOGE(ovl_id, "Failed to dequeue mirror buffer...");
        WDT_BL_NODE(cancelJob, job->mdp_job_output_buffer);
    }

}

void AsyncBliterHandler::processPhyMirror(DispatcherJob* job)
{
    unsigned int ovl_id = 0;

    setOverlayPortParam(ovl_id, job->mdp_mirror_ovl_port_param);

    WDT_BL_NODE(invalidate, job->mdp_job_output_buffer);

    HWBuffer& src_buf = job->hw_mirbuf;
    closeFenceFd(&src_buf.mir_in_acq_fence_fd);
    closeFenceFd(&src_buf.mir_in_rel_fence_fd);
    clearMdpJob(job->fill_black);
}

void AsyncBliterHandler::processVirMirror(DispatcherJob* job)
{
    HWBuffer& src_buf = job->hw_mirbuf;
    HWBuffer& dst_buf = job->hw_outbuf;

    PrivateHandle& src_priv_handle = src_buf.priv_handle;
    PrivateHandle& dst_priv_handle = dst_buf.priv_handle;


    BufferConfig* config = &m_dp_configs[m_ovl_engine->getMaxInputNum()];

    status_t err = setDpConfig(&src_priv_handle, nullptr,
                               src_buf.dataspace, HAL_DATASPACE_UNKNOWN,
                               config, 0);

    if (NO_ERROR == err)
    {
        setDstDpConfig(dst_priv_handle, config);

        Rect src_roi;
        Rect dst_roi;
        uint32_t xform;

        calculateMirRoiXform(&xform, &src_roi, &dst_roi, job);

        if (dst_roi.left % 2)
        {
            --dst_roi.left;
            --dst_roi.right;
            if (dst_roi.left < 0)
            {
                ++dst_roi.left;
            }
        }

        if (WIDTH(dst_roi) == 0 || (WIDTH(dst_roi) % 2))
        {
            const DisplayData* mir_disp_data = job->disp_data;

            if (dst_roi.right + 1 <= static_cast<int32_t>(mir_disp_data->width))
                ++dst_roi.right;
            else
                --dst_roi.right;
        }

        clearBackground(dst_buf.handle,
                        &dst_roi,
                        &dst_buf.mir_in_rel_fence_fd,
                        job->fill_black);

        BliterNode::Parameter param = {src_roi, dst_roi, config, xform, false, isSecure(&dst_priv_handle)};
        WDT_BL_NODE(setSrc, job->mdp_job_output_buffer, config, src_priv_handle, &src_buf.mir_in_acq_fence_fd);
        WDT_BL_NODE(setDst, job->mdp_job_output_buffer, &param, dst_priv_handle.ion_fd,
                dst_priv_handle.sec_handle,
                &dst_buf.mir_in_rel_fence_fd);
        WDT_BL_NODE(calculateAllROI, job->mdp_job_output_buffer);
        WDT_BL_NODE(invalidate, job->mdp_job_output_buffer);
    }
    else
    {
        BLOGE(0, "Failed to get mirror buffer info !!");
        WDT_BL_NODE(cancelJob, job->mdp_job_output_buffer);
    }

    closeFenceFd(&src_buf.mir_in_acq_fence_fd);
    closeFenceFd(&dst_buf.mir_in_rel_fence_fd);
    clearMdpJob(job->fill_black);
}

void AsyncBliterHandler::processVirBlack(DispatcherJob* job)
{
    HWBuffer& dst_buf = job->hw_outbuf;

    clearBackground(dst_buf.handle,
                    nullptr,
                    &dst_buf.mir_in_rel_fence_fd,
                    job->fill_black);
    closeFenceFd(&dst_buf.mir_in_rel_fence_fd);
    clearMdpJob(job->fill_black);
}

int AsyncBliterHandler::dump(char* /*buff*/, int /*buff_len*/, int /*dump_level*/)
{
    return 0;
}

void AsyncBliterHandler::processFillBlack(PrivateHandle* dst_priv_handle, int* fence, MdpJob &job, bool use_white)
{
    AutoMutex l(use_white ? WhiteBuffer::getInstance().m_lock : BlackBuffer::getInstance().m_lock);

    // get BlackBuffer handle
    buffer_handle_t src_handle = use_white ?
                                 WhiteBuffer::getInstance().getHandle() :
                                 BlackBuffer::getInstance().getHandle();
    if (src_handle == 0)
    {
        HWC_LOGE("%s(), use_white %d, get handle fail", __FUNCTION__, use_white);
        return;
    }

    // check is_sec
    bool is_sec = false;
    if ((Platform::getInstance().m_config.plat_switch & HWC_PLAT_SWITCH_BLIT_CLR_BG_NEED_SECURE) != 0)
    {
        is_sec = isSecure(dst_priv_handle);
        if (is_sec)
        {
            if (use_white)
            {
                WhiteBuffer::getInstance().setSecure();
            }
            else
            {
                BlackBuffer::getInstance().setSecure();
            }
        }
    }

    BufferConfig  config;
    memset(&config, 0, sizeof(BufferConfig));

    PrivateHandle src_priv_handle;
    status_t err = getPrivateHandle(src_handle, &src_priv_handle);

    if (NO_ERROR == err)
    {
        err = setDpConfig(&src_priv_handle, nullptr,
                          HAL_DATASPACE_V0_BT601_625, HAL_DATASPACE_UNKNOWN,
                          &config, 0);
    }

    if (NO_ERROR == err)
    {
        setDstDpConfig(*dst_priv_handle, &config);

        Rect src_roi(src_priv_handle.width, src_priv_handle.height);
        if (src_priv_handle.width / dst_priv_handle->width >= 20)
        {
            src_roi.left = 0;
            src_roi.right = static_cast<int32_t>(dst_priv_handle->width * 19);
        }
        Rect dst_roi(dst_priv_handle->width, dst_priv_handle->height);

        BliterNode::Parameter param = {src_roi, dst_roi, &config, 0, false, isSecure(dst_priv_handle)};
        WDT_BL_NODE(setSrc, job.id, &config, src_priv_handle);
        WDT_BL_NODE(setDst, job.id, &param, dst_priv_handle->ion_fd, dst_priv_handle->sec_handle, fence);
        WDT_BL_NODE(calculateAllROI, job.id);
        WDT_BL_NODE(invalidate, job.id);
        job.is_used = true;

        if (NO_ERROR == err)
        {
            if (fence != NULL)
            {
                *fence = job.fence;
                job.fence = -1;
            }
        }
    }
    else
    {
        HWC_LOGE("%s, setDpConfig Fail", __FUNCTION__);
    }

    if ((Platform::getInstance().m_config.plat_switch & HWC_PLAT_SWITCH_BLIT_CLR_BG_NEED_SECURE) != 0)
    {
        if (is_sec)
        {
            if (use_white)
            {
                WhiteBuffer::getInstance().setNormal();
            }
            else
            {
                BlackBuffer::getInstance().setNormal();
            }
        }
    }
}

void AsyncBliterHandler::clearBackground(buffer_handle_t handle,
                                         const Rect* current_dst_roi,
                                         int* fence,
                                         MdpJob& job)
{
    PrivateHandle priv_handle;

    int err = getPrivateHandle(handle, &priv_handle, nullptr, true);
    if (0 != err)
    {
        HWC_LOGE("Failed to get handle(%p)", handle);
        cancelFillBlackJob(job);
        return;
    }

    if (current_dst_roi == nullptr)
    {
        processFillBlack(&priv_handle, fence, job);
        cancelFillBlackJob(job);
        HWC_LOGD("clearBufferBlack with NULL ROI");
        return;
    }

    gralloc_extra_ion_hwc_info_t* hwc_ext_info = &priv_handle.hwc_ext_info;
    _crop_t prev_crop = hwc_ext_info->mirror_out_roi;
    _crop_t current_roi;
    current_roi.x = current_dst_roi->left;
    current_roi.y = current_dst_roi->top;
    current_roi.w = current_dst_roi->getWidth();
    current_roi.h = current_dst_roi->getHeight();

    // INIT    = 0xxxb
    // ROT   0 = 1000b
    // ROT  90 = 1100b
    // ROT 180 = 1011b
    // ROT 270 = 1111b
    // USED    = 1xxxb
    if ((prev_crop.w <= 0 || prev_crop.h <=0 ) ||
        (current_roi.w <= 0 || current_roi.h <=0 ) ||
        (prev_crop.x != current_roi.x) ||
        (prev_crop.y != current_roi.y) ||
        (prev_crop.w != current_roi.w) ||
        (prev_crop.h != current_roi.h))
    {
        processFillBlack(&priv_handle, fence, job);

        HWC_LOGD("clearBufferBlack (%d,%d,%d,%d) (%d,%d,%d,%d)", prev_crop.x, prev_crop.y, prev_crop.w, prev_crop.h,
            current_roi.x, current_roi.y, current_roi.w, current_roi.h);

        hwc_ext_info->mirror_out_roi = current_roi;

        gralloc_extra_perform(
            handle, GRALLOC_EXTRA_SET_HWC_INFO, hwc_ext_info);
    }
    else if (Platform::getInstance().m_config.fill_black_debug)
    {
        processFillBlack(&priv_handle, fence, job, true);

        HWC_LOGD("clearBufferWhite (%d,%d,%d,%d) (%d,%d,%d,%d)", prev_crop.x, prev_crop.y, prev_crop.w, prev_crop.h,
            current_roi.x, current_roi.y, current_roi.w, current_roi.h);

        hwc_ext_info->mirror_out_roi = current_roi;
        gralloc_extra_perform(
            handle, GRALLOC_EXTRA_SET_HWC_INFO, hwc_ext_info);
    }

    cancelFillBlackJob(job);
}

void AsyncBliterHandler::clearMdpJob(MdpJob& job)
{
   if (job.fence != -1)
    {
        protectedClose(job.fence);
        job.fence = -1;
    }
}

void AsyncBliterHandler::cancelFillBlackJob(MdpJob& job)
{
    if (!job.is_used)
        WDT_BL_NODE(cancelJob, job.id);
    else
        job.is_used = false;
}

void AsyncBliterHandler::cleanPrevLayerInfo(const std::vector<sp<HWCLayer> >* hwc_layers)
{
    m_prev_layer_info.remove_if(
        [=](PrevLayerInfo& l)
        {
            if (hwc_layers)
            {
                for (const sp<HWCLayer>& hwc_layer : *hwc_layers)
                {
                    if (l.id == hwc_layer->getId())
                    {
                        return false;
                    }
                }
            }
            protectedClose(l.job_done_fd);
            return true;
        });
}

void AsyncBliterHandler::savePrevLayerInfo(const sp<HWCLayer>& hwc_layer)
{
    bool exist = false;

    for (PrevLayerInfo& prev : m_prev_layer_info)
    {
        if (prev.id == hwc_layer->getId())
        {
            exist = true;
            if (prev.job_done_fd >= 0)
            {
                protectedClose(prev.job_done_fd);
            }
            prev.job_done_fd = dup(hwc_layer->getReleaseFenceFd());
            break;
        }
    }

    if (!exist)
    {
        m_prev_layer_info.push_back({ .id = hwc_layer->getId(),
                                      .job_done_fd = dup(hwc_layer->getReleaseFenceFd()) });
    }
}

int AsyncBliterHandler::getPrevLayerInfoFence(const sp<HWCLayer>& hwc_layer)
{
    for (PrevLayerInfo& prev : m_prev_layer_info)
    {
        if (prev.id == hwc_layer->getId())
        {
            return prev.job_done_fd;
        }
    }

    return -1;
}

bool AsyncBliterHandler::isMMLLayer(const HWLayer* layer) const
{
    return ((HWC_MML_DISP_DECOUPLE_LAYER & layer->layer_caps) ||
           (HWC_MML_DISP_DIRECT_DECOUPLE_LAYER & layer->layer_caps)) ? true : false;
}

void AsyncBliterHandler::setBlackToWorker(const DispatcherJob* job)
{
    if (!isNoDispatchThread())
    {
        return;
    }

    if (CC_UNLIKELY(!m_worker))
    {
        HWC_LOGE("m_worker is null");
        return;
    }

    const HWBuffer& dst_buf = job->hw_outbuf;
    buffer_handle_t dup_handle = nullptr;
    dupBufferHandle(dst_buf.handle, &dup_handle);

    m_worker->post([handle = dup_handle,
                    mir_in_rel_fence_fd = dst_buf.mir_in_rel_fence_fd,
                    mdp_job = job->fill_black,
                    this]()
    {
        int fence_fd = mir_in_rel_fence_fd;
        MdpJob job = mdp_job;

        clearBackground(handle,
                        nullptr,
                        &fence_fd,
                        job);
        closeFenceFd(&fence_fd);
        clearMdpJob(job);

        freeDuppedBufferHandle(handle);
    });
}

void AsyncBliterHandler::setInvalidateToWorker(const DispatcherJob* job, uint32_t ovl_id)
{
    if (!isNoDispatchThread())
    {
        return;
    }

    if (CC_UNLIKELY(!m_worker))
    {
        HWC_LOGE("m_worker is null");
        return;
    }

    const HWLayer* hw_layer = &job->hw_layers[ovl_id];
    const LightHwcLayer1* layer = &hw_layer->layer;

    m_worker->post([mdp_job_id = hw_layer->mdp_job_id,
                    sequence = job->sequence,
                    active_config = job->active_config,
                    present_after_ts = job->present_after_ts,
                    decouple_target_ts = job->decouple_target_ts,
                    acq_fd_to_close = layer->acquireFenceFd,
                    rel_fd_to_close = layer->releaseFenceFd,
                    src_handle = hw_layer->priv_handle.handle,
                    queue = hw_layer->queue, // keep DBQ alive, display buffer stay alive before invalidate, and avoid dup out buf fd
                    this]() mutable
    {
        WDT_BL_NODE(invalidate, mdp_job_id, sequence, active_config,
                    present_after_ts, decouple_target_ts);

        closeFenceFd(&acq_fd_to_close);
        closeFenceFd(&rel_fd_to_close);

        freeDuppedBufferHandle(src_handle);

        queue = nullptr;
    });
}

AsyncBliterHandler::Worker::Worker()
{
    m_thread = std::thread(&Worker::run, this);

    if (pthread_setname_np(m_thread.native_handle(), "BlitWorker"))
    {
        ALOGI("pthread_setname_np BlitWorker fail");
    }

    // Use SCHED_FIFO to minimize jitter
    constexpr int THREAD_PRIORITY = 1;
    struct sched_param param = {0};
    param.sched_priority = THREAD_PRIORITY;
    if (pthread_setschedparam(m_thread.native_handle(), SCHED_FIFO, &param) != 0)
    {
        HWC_LOGW("Couldn't set SCHED_FIFO for BlitWorker");
    }

    pid_t tid = pthread_gettid_np(m_thread.native_handle());
    if (setpriority(PRIO_PROCESS, static_cast<id_t>(tid), HAL_PRIORITY_URGENT_DISPLAY) == -1)
    {
        HWC_LOGW("set BlitWorker HAL_PRIORITY_URGENT_DISPLAY fail");
    }
}

AsyncBliterHandler::Worker::~Worker()
{
    m_exit = true;
    m_cv.notify_all();
    if (m_thread.joinable())
    {
        m_thread.join();
    }
}

void AsyncBliterHandler::Worker::post(std::function<void()> runnable)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_run_queue.emplace(std::move(runnable));
    m_cv.notify_one();
}

void AsyncBliterHandler::Worker::run()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    while (!m_exit)
    {
        while (!m_run_queue.empty())
        {
            std::function<void()> runnable = m_run_queue.front();
            m_run_queue.pop();
            runnable();
        }
        m_cv.wait(lock);
    }
}
