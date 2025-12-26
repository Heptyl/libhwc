#define DEBUG_LOG_TAG "COMP"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include "gralloc_mtk_defs.h"

#include "utils/debug.h"
#include "utils/tools.h"

#include "composer.h"
#include "display.h"
#include "overlay.h"
#include "dispatcher.h"
#include "worker.h"
#include "platform_wrap.h"
#include "hwc2.h"

#define CLOGV(i, x, ...) HWC_LOGV("(%" PRIu64 ":%d) " x, m_disp_id, i, ##__VA_ARGS__)
#define CLOGD(i, x, ...) HWC_LOGD("(%" PRIu64 ":%d) " x, m_disp_id, i, ##__VA_ARGS__)
#define CLOGI(i, x, ...) HWC_LOGI("(%" PRIu64 ":%d) " x, m_disp_id, i, ##__VA_ARGS__)
#define CLOGW(i, x, ...) HWC_LOGW("(%" PRIu64 ":%d) " x, m_disp_id, i, ##__VA_ARGS__)
#define CLOGE(i, x, ...) HWC_LOGE("(%" PRIu64 ":%d) " x, m_disp_id, i, ##__VA_ARGS__)

// ---------------------------------------------------------------------------

LayerHandler::LayerHandler(uint64_t dpy, const sp<OverlayEngine>& ovl_engine)
    : m_disp_id(dpy)
    , m_ovl_engine(ovl_engine)
{
}

LayerHandler::~LayerHandler()
{
    m_ovl_engine = NULL;
}

// ---------------------------------------------------------------------------

ComposerHandler::ComposerHandler(uint64_t dpy, const sp<OverlayEngine>& ovl_engine)
    : LayerHandler(dpy, ovl_engine)
{ }

void ComposerHandler::set(
    const sp<HWCDisplay>& display,
    DispatcherJob* job)
{
    uint32_t total_num = job->num_layers;
    auto&& layers = display->getCommittedLayers();
    HWC_LOGV("+ ComposerHandler::set() commit_layers size:%zu", layers.size());

    OverlayPortParam* const* ovl_params = m_ovl_engine->getInputParams();

    //DbgLogger logger(DbgLogger::TYPE_HWC_LOG, 'D', "(%d) ComposerHandler::set()", display->getId());
    for (uint32_t i = 0; i < total_num; i++)
    {
        HWLayer* hw_layer = &job->hw_layers[i];

        HWC_LOGV("hw_layers[i:%u] enable:%d", i, hw_layer->enable);
        // this layer is not enable
        if (!hw_layer->enable)
        {
            continue;
        }

        // skip mm/glai layers
        if (HWC_LAYER_TYPE_MM == hw_layer->type || HWC_LAYER_TYPE_GLAI == hw_layer->type) continue;

        HWC_LOGV("ComposerHandler::set() hw_layer->index:%u", hw_layer->index);
        sp<HWCLayer> layer = layers[hw_layer->index];
        HWC_LOGV("ComposerHandler::set() id:%" PRIu64 " is_ct:%d", layer->getId(), layer->isClientTarget());
        PrivateHandle* priv_handle = &hw_layer->priv_handle;

        if (HWC_LAYER_TYPE_DIM == hw_layer->type)
        {
            memset(&hw_layer->layer, 0, sizeof(LightHwcLayer1));
            copyHWCLayerIntoLightHwcLayer1(layer, &hw_layer->layer);

            CLOGV(i, "SET/dim");
        }
        else
        {
            if (priv_handle == nullptr)
            {
                hw_layer->enable = false;
                continue;
            }

            unsigned int type = getGeTypeFromPrivateHandle(priv_handle);
            unsigned int is_need_flush = (type != GRALLOC_EXTRA_BIT_TYPE_GPU);

            // get release fence from display driver
            {
                OverlayPrepareParam prepare_param;
                prepare_param.id            = i;
                prepare_param.ion_fd        = priv_handle->ion_fd;
                prepare_param.is_need_flush = is_need_flush;
                prepare_param.blending = layer->getBlend();
                prepare_param.drm_id_crtc = job->drm_id_cur_crtc;

                status_t err = m_ovl_engine->prepareInput(prepare_param);
                if (NO_ERROR != err)
                {
                    prepare_param.fence_index = 0;
                    prepare_param.fence_fd = -1;
                }
                hw_layer->fence_index = prepare_param.fence_index;

                if (prepare_param.fence_fd <= 0)
                {
                    CLOGE(i, "Failed to get releaseFence !!");
                }

                if (layer->isClientTarget())
                {
                    protectedClose(prepare_param.fence_fd);
                    prepare_param.fence_fd = -1;
                }
                else
                {
                    layer->setReleaseFenceFd(prepare_param.fence_fd, display->isConnected());
                }
                // logger.printf(" [i:%u id:%" PRIu64 " ion_fd:%d rel_fence:%d is_ct:%d]",
                //        i, layer->getId(), priv_handle->ion_fd,layer->getReleaseFenceFd(), layer->isClientTarget());
            }
            memset(&hw_layer->layer, 0, sizeof(LightHwcLayer1));
            copyHWCLayerIntoLightHwcLayer1(layer, &hw_layer->layer);

            // partial update - fill dirty rects info
            hw_layer->layer.surfaceDamage = { 0, hw_layer->surface_damage_rect};
            const size_t& num_rect = layer->getDamage().numRects;
            if (!job->is_full_invalidate)
            {
                hwc_rect_t* job_dirty_rect = hw_layer->surface_damage_rect;
                if (num_rect == 0 || num_rect > MAX_DIRTY_RECT_CNT)
                {
                    job_dirty_rect[0].left   = 0;
                    job_dirty_rect[0].top    = 0;
                    job_dirty_rect[0].right  = static_cast<int>(priv_handle->width);
                    job_dirty_rect[0].bottom = static_cast<int>(priv_handle->height);
                    hw_layer->layer.surfaceDamage.numRects = 1;
                }
                else
                {
                    memcpy(job_dirty_rect, layer->getDamage().rects, sizeof(hwc_rect_t) * num_rect);
                    hw_layer->layer.surfaceDamage.numRects = num_rect;
                }
            }

            CLOGV(i, "SET/rel=%d(%d)/acq=%d/handle=%p/ion=%d/flush=%d",
                layer->getReleaseFenceFd(), hw_layer->fence_index, layer->getAcquireFenceFd(),
                layer->getHandle(), priv_handle->ion_fd, is_need_flush);

        }

        if (isNoDispatchThread())
        {
            setOverlayPortParam(job, ovl_params[i], hw_layer);
        }

    }
    HWC_LOGV("- ComposerHandler::set");
}

void ComposerHandler::process(DispatcherJob* job)
{
#ifndef MTK_USER_BUILD
    HWC_ATRACE_CALL();
#endif

    uint32_t total_num = job->num_layers;
    uint32_t i = 0;

    OverlayPortParam* const* ovl_params = m_ovl_engine->getInputParams();

    // fill overlay engine setting
    for (i = 0; i < total_num; i++)
    {
        HWLayer* hw_layer = &job->hw_layers[i];

        // this layer is not enable
        if (!hw_layer->enable)
        {
            ovl_params[i]->state = OVL_IN_PARAM_DISABLE;
            ovl_params[i]->sequence = HWC_SEQUENCE_INVALID;
            continue;
        }

        // skip mm/glai layers
        if (HWC_LAYER_TYPE_MM == hw_layer->type || HWC_LAYER_TYPE_GLAI == hw_layer->type) continue;

        setOverlayPortParam(job, ovl_params[i], hw_layer);
    }
}

void ComposerHandler::cancelLayers(DispatcherJob* job)
{
    // cancel ui layers for dropping job
    for (unsigned int i = 0; i < job->num_layers; ++i)
    {
        HWLayer* hw_layer = &job->hw_layers[i];
        if (!hw_layer->enable) continue;

        // skip mm/glai layers
        if (HWC_LAYER_TYPE_MM == hw_layer->type || HWC_LAYER_TYPE_GLAI == hw_layer->type) continue;

        if (hw_layer->layer.acquireFenceFd != -1) ::protectedClose(hw_layer->layer.acquireFenceFd);
        hw_layer->layer.acquireFenceFd = -1;
    }
}

void ComposerHandler::setOverlayPortParam(DispatcherJob* job,
                                          OverlayPortParam* param,
                                          HWLayer* hw_layer)
{
    LightHwcLayer1* layer = &hw_layer->layer;
    PrivateHandle* priv_handle = &hw_layer->priv_handle;

    int l, t, r, b;
    if (HWC_LAYER_TYPE_DIM != hw_layer->type)
    {
        // [NOTE]
        // Since OVL does not support float crop, adjust coordinate to interger
        // as what SurfaceFlinger did with hwc before version 1.2
        hwc_frect_t* src_cropf = &layer->sourceCropf;
        l = (int)(ceilf(src_cropf->left));
        t = (int)(ceilf(src_cropf->top));
        r = (int)(floorf(src_cropf->right));
        b = (int)(floorf(src_cropf->bottom));
    }
    else
    {
        l = layer->displayFrame.left;
        t = layer->displayFrame.top;
        r = layer->displayFrame.right;
        b = layer->displayFrame.bottom;
    }
    Rect src_crop(l, t, r, b);
    rectifyRectWithPrexform(&src_crop, priv_handle);

    Rect dst_crop(*(Rect *)&(layer->displayFrame));

    param->state        = OVL_IN_PARAM_ENABLE;
    param->mva          = (void*)priv_handle->fb_mva;
    param->pitch        = priv_handle->y_stride;
    param->v_pitch      = priv_handle->vstride;
    param->format       = priv_handle->format;
    param->src_crop     = src_crop;
    param->dst_crop     = dst_crop;
    param->is_sharpen   = false;
    param->fence_index  = hw_layer->fence_index;
    // use hw layer type as identity
    param->identity     = hw_layer->type;
    param->protect      = usageHasProtected(priv_handle->usage);
    param->secure       = usageHasSecure(priv_handle->usage);
    if (param->secure)
    {
        param->mva = reinterpret_cast<void*>(static_cast<uintptr_t>(priv_handle->sec_handle));
        param->va = reinterpret_cast<void*>(static_cast<uintptr_t>(priv_handle->sec_handle));
    }
    param->alpha_enable = (layer->blending != HWC2_BLEND_MODE_NONE);
    param->alpha        = layer->planeAlpha;
    param->blending     = layer->blending;
    param->dim          = (hw_layer->type == HWC_LAYER_TYPE_DIM);
    param->sequence     = job->sequence;
    param->ext_sel_layer = hw_layer->ext_sel_layer;
    param->dataspace    = hw_layer->dataspace;
#ifdef MTK_HWC_PROFILING
    if (HWC_LAYER_TYPE_FBT == hw_layer->type)
    {
        param->fbt_input_layers = hw_layer->fbt_input_layers;
        param->fbt_input_bytes  = hw_layer->fbt_input_bytes;
    }
#endif
    param->ion_fd      = priv_handle->ion_fd;
    param->fence       = layer->acquireFenceFd;
    param->compress    = isCompressData(priv_handle);

    // partial update - fill dirty rects info
    param->ovl_dirty_rect_cnt = layer->surfaceDamage.numRects;
    if (param->ovl_dirty_rect_cnt > 0 && param->ovl_dirty_rect_cnt <= MAX_DIRTY_RECT_CNT)
    {
        const size_t size = sizeof(hwc_rect_t) * param->ovl_dirty_rect_cnt;
        memcpy(param->ovl_dirty_rect, layer->surfaceDamage.rects, size);
    }
    else if (param->ovl_dirty_rect_cnt != 0)
    {
        HWC_LOGW("the rect number of surfaceDamage is invalid (%zu), set cnt = 0", param->ovl_dirty_rect_cnt);
        param->ovl_dirty_rect_cnt = 0;
    }

    // set solid color
    param->layer_color = hw_layer->layer_color;

    // set buffer info for DRM
    // TODO: do we need width and height for trigger?
    param->src_buf_width = priv_handle->width;
    param->src_buf_height = priv_handle->height;
    param->alloc_id = priv_handle->alloc_id;
    param->hwc_layer_id = hw_layer->hwc2_layer_id;
    param->size = priv_handle->size;
    param->is_mml = false;

    // set the debug type of this layer
    param->debug_type = hw_layer->debug_type;

    if (priv_handle->ext_info.magic == 0xF0F0)
        param->hdmi_rx = true;
    else
        param->hdmi_rx = false;
}
