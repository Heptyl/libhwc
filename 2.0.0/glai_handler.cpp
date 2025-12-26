#define DEBUG_LOG_TAG "GLAI_HANDLER"

#include "glai_handler.h"

#include "graphics_mtk_defs.h"
#include "gralloc_mtk_defs.h"
#include <gm/gm_pixel_format.h>

#include "utils/debug.h"
#include "utils/tools.h"

#include "dispatcher.h"
#include "display.h"
#include "glai_controller.h"
#include "hwcdisplay.h"
#include "platform_wrap.h"
#include "overlay.h"
#include "worker.h"
#include "queue.h"
#include "hwc2.h"

#include <sync/sync.h>

#include <utils/String8.h>

using namespace mediatek::graphics::common;

//#define TEST_CPU_COPY
#ifdef TEST_CPU_COPY
bool force_copy = false;
#endif

#define GLOGD(i, x, ...) HWC_LOGD("(%" PRIu64 ":%d) " x, m_disp_id, i, ##__VA_ARGS__)
#define GLOGI(i, x, ...) HWC_LOGI("(%" PRIu64 ":%d) " x, m_disp_id, i, ##__VA_ARGS__)
#define GLOGW(i, x, ...) HWC_LOGW("(%" PRIu64 ":%d) " x, m_disp_id, i, ##__VA_ARGS__)
#define GLOGE(i, x, ...) HWC_LOGE("(%" PRIu64 ":%d) " x, m_disp_id, i, ##__VA_ARGS__)

// ---------------------------------------------------------------------------

GlaiHandler::GlaiHandler(uint64_t dpy, const sp<OverlayEngine>& ovl_engine)
    : LayerHandler(dpy, ovl_engine)
{
}

GlaiHandler::~GlaiHandler()
{
    cleanPrevLayerInfo();
}

void GlaiHandler::set(const sp<HWCDisplay>& display, DispatcherJob* job)
{
    HWC_ATRACE_CALL();

    auto&& layers = display->getCommittedLayers();
    cleanPrevLayerInfo(&layers);

    for (uint32_t i = 0; i < job->num_layers; i++)
    {
        HWLayer* hw_layer = &job->hw_layers[i];

        // skip non glai layers
        if (HWC_LAYER_TYPE_GLAI != hw_layer->type) continue;

        // this layer is not enable
        if (!hw_layer->enable) continue;

        sp<HWCLayer> hwc_layer = layers[hw_layer->index];
        PrivateHandle& priv_handle = hw_layer->priv_handle;

        DbgLogger logger(DbgLogger::TYPE_HWC_LOG, 'D',
                         "[GLAI_HANDLER]SET(%" PRIu64 ",%d, %d)", m_disp_id, i,
                         Platform::getInstance().m_config.dbg_mdp_always_blit);

        // prepare buffer queue for this (job, layer)
        sp<DisplayBufferQueue> queue = getDisplayBufferQueue(hw_layer);

        if (doGlai(hw_layer))
        {
            // TODO: since inference in main thread, ond't need dup acquire fence?
            copyHWCLayerIntoLightHwcLayer1(hwc_layer, &hw_layer->layer);

#if 0   // ion fd didn't change thread in glai
            // TODO: this should handle by AI lib
            if (priv_handle.ion_fd > 0)
            {
                // workaround:
                //IONDevice::getInstance().ionImport(&*(const_cast<int32_t*>(&priv_handle.ion_fd)));
                IONDevice::getInstance().ionImport(&priv_handle.ion_fd);
            }
#endif
            LightHwcLayer1* layer = &hw_layer->layer;
            DisplayBufferQueue::DisplayBuffer disp_buffer;

            // config buffer queue
            configDisplayBufferQueue(queue, hw_layer);

            status_t err = queue->dequeueBuffer(&disp_buffer, true, isSecure(&hw_layer->priv_handle));

            if (err != NO_ERROR)
            {
                GLOGE(i, "%s(): dequeueBuffer() goes wrong err=%d", __func__, err);
            }
            else
            {
                // np agent excute
                size_t in_size = priv_handle.height *
                                 priv_handle.y_stride *
                                 (getBitsPerPixel(priv_handle.format) / 8);
                size_t out_size = disp_buffer.data_height *
                                  disp_buffer.handle_stride *
                                  (getBitsPerPixel(disp_buffer.data_format) / 8);
                int inference_done_fence = -1;

                GlaiController::InferenceParam param{ .agent_id = hw_layer->glai_agent_id,
                                                      .in_fd = priv_handle.ion_fd,
                                                      .in_size = in_size,
                                                      .in_acquire_fence = layer->acquireFenceFd,
                                                      .out_fd = disp_buffer.out_ion_fd,
                                                      .out_size = out_size,
                                                      .out_release_fence = disp_buffer.release_fence,
                                                      .inference_done_fence = &inference_done_fence,
                                                      .buffer_handle = hw_layer->priv_handle.handle,
                                                    };
                GlaiController::getInstance().inference(param);

                // get release fence from glai
                hwc_layer->setReleaseFenceFd(inference_done_fence, display->isConnected());
                savePrevLayerInfo(hwc_layer);
                hw_layer->layer.releaseFenceFd = dup(hwc_layer->getReleaseFenceFd());

                logger.printf("/rel=%d/acq=%d/handle=%p",
                    hwc_layer->getReleaseFenceFd(), hwc_layer->getAcquireFenceFd(),
                    hwc_layer->getHandle());

                passFenceFd(&disp_buffer.acquire_fence, &layer->releaseFenceFd);

                // TODO: handle src crop from AOSP
                disp_buffer.data_info.src_crop        = hw_layer->glai_dst_roi;
                disp_buffer.data_info.dst_crop.left   = layer->displayFrame.left;
                disp_buffer.data_info.dst_crop.top    = layer->displayFrame.top;
                disp_buffer.data_info.dst_crop.right  = layer->displayFrame.right;
                disp_buffer.data_info.dst_crop.bottom = layer->displayFrame.bottom;

                disp_buffer.data_info.is_sharpen = false;
                disp_buffer.alpha_enable         = (layer->blending != HWC2_BLEND_MODE_NONE);
                disp_buffer.alpha                = layer->planeAlpha;
                disp_buffer.blending             = layer->blending;
                disp_buffer.src_handle           = hw_layer->priv_handle.handle;
                disp_buffer.dataspace            = hw_layer->dataspace;
                disp_buffer.ext_sel_layer        = hw_layer->ext_sel_layer;
                disp_buffer.hwc_layer_id         = hw_layer->hwc2_layer_id;
                disp_buffer.sequence = job->sequence;

                queue->queueBuffer(&disp_buffer);

                if (isNoDispatchThread())
                {
                    OverlayPortParam* const* ovl_params = m_ovl_engine->getInputParams();
                    prepareOverlayPortParam(i, job, queue, ovl_params[i], true);

                    LightHwcLayer1* layer = &hw_layer->layer;
                    closeFenceFd(&layer->acquireFenceFd);
                    closeFenceFd(&layer->releaseFenceFd);
                }
                else
                {
                    prepareOverlayPortParam(i, job, queue, &hw_layer->ovl_port_param, true);
                }
            }
        }
        else
        {
            hwc_layer->setReleaseFenceFd(dup(getPrevLayerInfoFence(hwc_layer)), display->isConnected());

            copyHWCLayerIntoLightHwcLayer1(hwc_layer, &hw_layer->layer, true);

            logger.printf("/async=bypass/acq=%d/handle=%p/skip_invalidate=%d",
                          hwc_layer->getAcquireFenceFd(), hwc_layer->getHandle());

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

                HWC_ATRACE_NAME("glai bypass");
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
        }
    }
}

int GlaiHandler::prepareOverlayPortParam(unsigned int ovl_id,
                                         DispatcherJob* job,
                                         sp<DisplayBufferQueue> queue,
                                         OverlayPortParam* ovl_port_param,
                                         bool new_buf)
{
    if (!queue)
    {
        GLOGE(ovl_id, "%s(), queue == nullptr", __FUNCTION__);
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

    status_t err = m_ovl_engine->prepareInput(prepare_param);
    if (err != NO_ERROR)
    {
        prepare_param.fence_index = 0;
        prepare_param.fence_fd = -1;
    }

    if (prepare_param.fence_fd <= 0)
    {
        GLOGE(ovl_id, "Failed to get releaseFence for input queue");
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
    // seems no use in drmdev
/*    param->ovl_dirty_rect_cnt = 1;
    param->ovl_dirty_rect[0].left   = param->src_crop.left;
    param->ovl_dirty_rect[0].top    = param->src_crop.top;
    param->ovl_dirty_rect[0].right  = param->src_crop.right;
    param->ovl_dirty_rect[0].bottom = param->src_crop.bottom;*/
#ifdef TEST_CPU_COPY
    param->compress        = force_copy;
#else
    param->compress        = buffer->data_is_compress;
#endif

    GLOGD(ovl_id, "%s() id:%d ion_fd:%d rel_fence_idx:%d rel_fence_fd:%d p:%d v_p:%d", __FUNCTION__,
          prepare_param.id, prepare_param.ion_fd, prepare_param.fence_index, prepare_param.fence_fd,
          param->pitch, param->v_pitch);

    return 0;
}

int GlaiHandler::setOverlayPortParam(unsigned int ovl_id, const OverlayPortParam& ovl_port_param)
{
    OverlayPortParam* const* ovl_params = m_ovl_engine->getInputParams();
    memcpy(ovl_params[ovl_id], &ovl_port_param, sizeof(OverlayPortParam));
    return 0;
}

bool GlaiHandler::doGlai(HWLayer* hw_layer)
{
    if (Platform::getInstance().m_config.dbg_mdp_always_blit)
    {
        return true;
    }

    return hw_layer->dirty && !hw_layer->mdp_skip_invalidate;
}

sp<DisplayBufferQueue> GlaiHandler::getDisplayBufferQueue(HWLayer *hw_layer)
{
    sp<DisplayBufferQueue> queue = nullptr;

    if (hw_layer)
    {
        queue = hw_layer->queue;
        if (queue == nullptr)
        {
            queue = new DisplayBufferQueue(DisplayBufferQueue::QUEUE_TYPE_GLAI,
                                           hw_layer->hwc_layer->getId());
            hw_layer->queue = queue;
            hw_layer->hwc_layer->setBufferQueue(queue);
        }
    }
    else
    {
        queue = new DisplayBufferQueue(DisplayBufferQueue::QUEUE_TYPE_GLAI);
    }

    if (queue == nullptr)
    {
        GLOGE(-1, "get buffer queue failed");
        return nullptr;
    }

    queue->setSynchronousMode(false);
    return queue;
}

void GlaiHandler::configDisplayBufferQueue(sp<DisplayBufferQueue> queue,
                                           const HWLayer* hw_layer) const
{
    const PrivateHandle* priv_handle = &hw_layer->priv_handle;

    const GlaiController::Model* model = GlaiController::getInstance().getModel(hw_layer->glai_agent_id);

    // set buffer format to buffer queue
    DisplayBufferQueue::BufferParam buffer_param;
    buffer_param.disp_id = static_cast<int>(m_disp_id);
    buffer_param.pool_id = priv_handle->ext_info.pool_id;

    if (model)
    {
        buffer_param.width = model->out_w;
        buffer_param.height = model->out_h;
        buffer_param.format = model->out_format;
        buffer_param.compression = model->out_compress;
    }
    else
    {
        // error handle, just use w/h/f as HRT stated
        buffer_param.width = static_cast<unsigned int>(WIDTH(hw_layer->glai_dst_roi));
        buffer_param.height = static_cast<unsigned int>(HEIGHT(hw_layer->glai_dst_roi));
        buffer_param.format = priv_handle->format;
        buffer_param.compression = false;
    }

    // TODO: should calculate the size from the information of gralloc?
    buffer_param.size = buffer_param.width *
                        buffer_param.height *
                        (getBitsPerPixel(buffer_param.format) / 8);
    buffer_param.protect = usageHasProtected(priv_handle->usage);

#ifdef TEST_CPU_COPY
    buffer_param.sw_usage = true;
    force_copy = isCompressData(priv_handle);
#endif

    queue->setBufferParam(buffer_param);
}

void GlaiHandler::cleanPrevLayerInfo(const std::vector<sp<HWCLayer> >* hwc_layers)
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

void GlaiHandler::savePrevLayerInfo(const sp<HWCLayer>& hwc_layer)
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

int GlaiHandler::getPrevLayerInfoFence(const sp<HWCLayer>& hwc_layer)
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

void GlaiHandler::process(DispatcherJob* job)
{
    for (uint32_t i = 0; i < job->num_layers; i++)
    {
        HWLayer* hw_layer = &job->hw_layers[i];

        // skip non glai layers
        if (HWC_LAYER_TYPE_GLAI != hw_layer->type) continue;

        // this layer is not enable
        if (!hw_layer->enable) continue;

        setOverlayPortParam(i, hw_layer->ovl_port_param);

        if (!doGlai(hw_layer))
        {
            HWC_ATRACE_NAME("glai bypass");
            continue;
        }

#if 0
        if (hw_layer->priv_handle.ion_fd > 0)
        {
            IONDevice::getInstance().ionClose(hw_layer->priv_handle.ion_fd);
        }
#endif
        LightHwcLayer1* layer = &hw_layer->layer;
        closeFenceFd(&layer->acquireFenceFd);
        closeFenceFd(&layer->releaseFenceFd);
    }
}

void GlaiHandler::cancelLayers(DispatcherJob* job)
{
    for (uint32_t i = 0; i < job->num_layers; i++)
    {
        HWLayer* hw_layer = &job->hw_layers[i];

        // skip non mm layers
        if (HWC_LAYER_TYPE_GLAI != hw_layer->type) continue;

        // this layer is not enable
        if (!hw_layer->enable) continue;

        if (!doGlai(hw_layer)) continue;

        LightHwcLayer1* layer = &hw_layer->layer;

        GLOGD(i, "CANCEL/rel=%d/acq=%d/handle=%p",
            layer->releaseFenceFd, layer->acquireFenceFd, hw_layer->priv_handle.handle);

        if (layer->acquireFenceFd != -1) ::protectedClose(layer->acquireFenceFd);
        layer->acquireFenceFd = -1;

        protectedClose(layer->releaseFenceFd);
        layer->releaseFenceFd = -1;

        if (hw_layer->priv_handle.ion_fd > 0)
        {
            IONDevice::getInstance().ionClose(hw_layer->priv_handle.ion_fd);
        }
    }
}

int GlaiHandler::dump(char* /*buff*/, int /*buff_len*/, int /*dump_level*/)
{
    return 0;
}

