#define DEBUG_LOG_TAG "HRT"

#include "drmhrt.h"

#include <vector>
#include <algorithm>

#include "drm/drmmodeutils.h"
#include "overlay.h"
#include "sync.h"
#include "dispatcher.h"
#include "hwc2.h"
#include "dev_interface.h"
#include "drm/drmdev.h"
#include <drm_fourcc.h>
#include <hwc_feature_list.h>
#include "mtk-mml.h"

static int32_t mapMtkLayeringCaps2HwcLayeringCaps(unsigned int caps)
{
    int32_t tmp = 0;

    if (caps & MTK_LAYERING_OVL_ONLY)
    {
        tmp |= HWC_LAYERING_OVL_ONLY;
    }

    if (caps & MTK_MDP_RSZ_LAYER)
    {
        tmp |= HWC_MDP_RSZ_LAYER;
    }

    if (caps & MTK_DISP_RSZ_LAYER)
    {
        tmp |= HWC_DISP_RSZ_LAYER;
    }

    if (caps & MTK_MDP_ROT_LAYER)
    {
        tmp |= HWC_MDP_ROT_LAYER;
    }

    if (caps & MTK_MDP_HDR_LAYER)
    {
        tmp |= HWC_MDP_HDR_LAYER;
    }

    if (caps & MTK_NO_FBDC)
    {
        tmp |= HWC_NO_FBDC;
    }

    if (caps & MTK_CLIENT_CLEAR_LAYER)
    {
        tmp |= HWC_CLIENT_CLEAR_LAYER;
    }

    if (caps & MTK_DISP_CLIENT_CLEAR_LAYER)
    {
        tmp |= HWC_DISP_CLIENT_CLEAR_LAYER;
    }

    if (caps & MTK_MML_DISP_DIRECT_LINK_LAYER)
    {
        tmp |= HWC_MML_DISP_DIRECT_LINK_LAYER;
    }

    if (caps & MTK_MML_DISP_DIRECT_DECOUPLE_LAYER)
    {
        tmp |= HWC_MML_DISP_DIRECT_DECOUPLE_LAYER;
    }

    if (caps & MTK_MML_DISP_DECOUPLE_LAYER)
    {
        tmp |= HWC_MML_DISP_DECOUPLE_LAYER;
    }

    if (caps & MTK_MML_DISP_MDP_LAYER)
    {
        tmp |= HWC_MML_DISP_MDP_LAYER;
    }

    if (caps & MTK_MML_DISP_NOT_SUPPORT)
    {
        tmp |= HWC_MML_DISP_NOT_SUPPORT;
    }

    if (caps & MTK_HWC_UNCHANGED_LAYER)
    {
        tmp |= HWC_UNCHANGED_LAYER;
    }

    if (caps & MTK_HWC_INACTIVE_LAYER)
    {
        tmp |= HWC_INACTIVE_LAYER;
    }

    if (caps & MTK_HWC_UNCHANGED_FBT_LAYER)
    {
        tmp |= HWC_UNCHANGED_FBT_LAYER;
    }

    if (caps & MTK_DISP_UNCHANGED_RATIO_VALID)
    {
        tmp |= HWC_DISP_UNCHANGED_RATIO_VALID;
    }

    if (caps & MTK_DISP_FBT_RATIO_VALID)
    {
        tmp |= HWC_DISP_FBT_RATIO_VALID;
    }

    if (caps & MTK_DISP_CLIENT_LAYER)
    {
        tmp |= HWC_DISP_CLIENT_LAYER;
    }

    return tmp;
}

static unsigned int mapHwcLayeringCaps2MtkLayeringCaps(int32_t caps)
{
    unsigned int tmp = 0;

    if (caps & HWC_LAYERING_OVL_ONLY)
    {
        tmp |= MTK_LAYERING_OVL_ONLY;
    }

    if (caps & HWC_MDP_RSZ_LAYER)
    {
        tmp |= MTK_MDP_RSZ_LAYER;
    }

    if (caps & HWC_DISP_RSZ_LAYER)
    {
        tmp |= MTK_DISP_RSZ_LAYER;
    }

    if (caps & HWC_MDP_ROT_LAYER)
    {
        tmp |= MTK_MDP_ROT_LAYER;
    }

    if (caps & HWC_MDP_HDR_LAYER)
    {
        tmp |= MTK_MDP_HDR_LAYER;
    }

    if (caps & HWC_NO_FBDC)
    {
        tmp |= MTK_NO_FBDC;
    }

    if (caps & HWC_CLIENT_CLEAR_LAYER)
    {
        tmp |= MTK_CLIENT_CLEAR_LAYER;
    }

    if (caps & HWC_DISP_CLIENT_CLEAR_LAYER)
    {
        tmp |= MTK_DISP_CLIENT_CLEAR_LAYER;
    }

    if (caps & HWC_MML_OVL_LAYER)
    {
        tmp |= MTK_MML_OVL_LAYER;
    }

    if (caps & HWC_UNCHANGED_LAYER)
    {
        tmp |= MTK_HWC_UNCHANGED_LAYER;
    }

    if (caps & HWC_INACTIVE_LAYER)
    {
        tmp |= MTK_HWC_INACTIVE_LAYER;
    }

    if (caps & HWC_UNCHANGED_FBT_LAYER)
    {
        tmp |= MTK_HWC_UNCHANGED_FBT_LAYER;
    }

    if (caps & HWC_DISP_UNCHANGED_RATIO_VALID)
    {
        tmp |= MTK_DISP_UNCHANGED_RATIO_VALID;
    }

    if (caps & HWC_DISP_FBT_RATIO_VALID)
    {
        tmp |= MTK_DISP_FBT_RATIO_VALID;
    }

    if (caps & HWC_DISP_CLIENT_LAYER)
    {
        tmp |= MTK_DISP_CLIENT_LAYER;
    }

    return tmp;
}

static uint32_t mapMtkDispCaps2HwcDispCaps(uint32_t caps)
{
    uint32_t tmp = 0;

    if (caps & MTK_GLES_FBT_GET_RATIO)
    {
        tmp |= HWC_GLES_FBT_GET_RATIO;
    }

    if (caps & MTK_GLES_FBT_UNCHANGED)
    {
        tmp |= HWC_GLES_FBT_UNCHANGED;
    }

    return tmp;
}

bool DrmHrt::isEnabled() const
{
    if (CC_UNLIKELY(m_disp_id_list.empty()))
    {
        HWC_LOGE("%s(): m_disp_id_list is empty, this should not happen!", __FUNCTION__);
        HWC_ASSERT(0);
        return false;
    }

    IOverlayDevice* ovl_dev = m_disp_devs[m_disp_id_list[0]].get();
    if (CC_UNLIKELY(ovl_dev == nullptr))
    {
        HWC_LOGE("%s(): Failed to get ovl_dev %" PRIu64, __FUNCTION__, m_disp_id_list[0]);
        HWC_ASSERT(0);
        return false;
    }

    return ovl_dev->isDisplayHrtSupport();
}

bool DrmHrt::isRPOEnabled() const
{
    if (CC_UNLIKELY(m_disp_id_list.empty()))
    {
        HWC_LOGE("%s(): m_disp_id_list is empty, this should not happen!", __FUNCTION__);
        HWC_ASSERT(0);
        return false;
    }

    IOverlayDevice* ovl_dev = m_disp_devs[m_disp_id_list[0]].get();
    if (CC_UNLIKELY(ovl_dev == nullptr))
    {
        HWC_LOGE("%s(): Failed to get ovl_dev %" PRIu64, __FUNCTION__, m_disp_id_list[0]);
        HWC_ASSERT(0);
        return false;
    }

    return ovl_dev->isDispRpoSupported();
}

void DrmHrt::fillLayerConfigList()
{
    for (uint64_t disp_id : m_disp_id_list)
    {
        HWCDisplay* display = m_displays[disp_id].get();
        if (CC_UNLIKELY(display == nullptr))
        {
            HWC_LOGE("%s(): Failed to get display %" PRIu64, __FUNCTION__, disp_id);
            HWC_ASSERT(0);
            continue;
        }

        if (!display->isConnected())
            continue;

        const std::vector<sp<HWCLayer> >& layers = display->getVisibleLayersSortedByZ();
        unsigned int layers_num = static_cast<unsigned int>(layers.size());

        if (CC_UNLIKELY(disp_id >= DisplayManager::MAX_DISPLAYS)) {
            HWC_LOGE("%s(), disp_id %" PRIu64 " > MAX_DISPLAYS %d", __FUNCTION__, disp_id,
                     DisplayManager::MAX_DISPLAYS);
            continue;
        }
        // reallocate layer_config_list if needed
        if ((m_layer_config_len[disp_id] == 0) || (layers_num > m_layer_config_len[disp_id]))
        {
            if (NULL != m_layer_config_list[disp_id])
                free(m_layer_config_list[disp_id]);

            if (layers_num == 0)
                layers_num = 1;

            m_layer_config_len[disp_id] = layers_num;
            m_layer_config_list[disp_id] = (drm_mtk_layer_config*)calloc(m_layer_config_len[disp_id], sizeof(drm_mtk_layer_config));
            m_layer_mml_info[disp_id] = (mml_frame_info*)calloc(m_layer_config_len[disp_id], sizeof(mml_frame_info));
            if (NULL == m_layer_config_list[disp_id])
            {
                HWC_LOGE("(%" PRIu64 ") Failed to malloc layer_config_list (len=%u)", disp_id, layers_num);
                m_layer_config_len[disp_id] = 0;
                return;
            }
        }

        // init and get PrivateHandle
        drm_mtk_layer_config* layer_config = m_layer_config_list[disp_id];
        mml_frame_info* layer_mml_info = m_layer_mml_info[disp_id];

        for (auto& layer : layers)
        {
            layer_config->ovl_id        = static_cast<uint32_t>(-1);
            layer_config->ext_sel_layer = -1;
            layer_config->src_fmt       =
                (layer->getHwlayerType() == HWC_LAYER_TYPE_DIM) ?
                    mapDispInputColorFormat(HAL_PIXEL_FORMAT_DIM) :
                    mapDispInputColorFormat(layer->getPrivateHandle().format);
            layer_config->compress      = isCompressData(&(layer->getPrivateHandle()));
            layer_config->dataspace = layer->getDataspace();

            if (layer->getHwlayerType() == HWC_LAYER_TYPE_MM)
            {
                layer_config->src_fmt = mapDispInputColorFormat(layer->decideMdpOutputFormat(*display));
                layer_config->dataspace = layer->decideMdpOutDataspace();
                layer_config->compress = layer->decideMdpOutputCompressedBuffers(*display);
            }

            layer_config->src_offset_y  = static_cast<uint32_t>(getSrcTop(layer));
            layer_config->src_offset_x  = static_cast<uint32_t>(getSrcLeft(layer));
            layer_config->dst_offset_y  = static_cast<uint32_t>(getDstTop(layer));
            layer_config->dst_offset_x  = static_cast<uint32_t>(getDstLeft(layer));
            layer_config->dst_width     = static_cast<uint32_t>(getDstWidth(layer));
            layer_config->dst_height    = static_cast<uint32_t>(getDstHeight(layer));
            layer_config->layer_caps    = mapHwcLayeringCaps2MtkLayeringCaps(layer->getLayerCaps());
            layer_config->secure        = isSecure(&(layer->getPrivateHandle()));
            layer_config->buffer_alloc_id = layer->getPrivateHandle().alloc_id;//add for BW Monitor
            HWC_LOGV("(%" PRIu64 ") %s() id:%" PRIu64" d_xywh:%u,%u,%u,%u, sec:%u, alloc_id:%" PRIu64 ", layer_caps:0x%x",
                disp_id, __func__, layer->getId(), layer_config->dst_offset_x, layer_config->dst_offset_y,
                layer_config->dst_width, layer_config->dst_height, layer_config->secure, layer_config->buffer_alloc_id, layer_config->layer_caps);
            const PrivateHandle& priv_hnd = layer->getPrivateHandle();
            switch(layer->getHwlayerType())
            {
                case HWC_LAYER_TYPE_DIM:
                    layer_config->src_width = static_cast<uint32_t>(getDstWidth(layer));
                    layer_config->src_height = static_cast<uint32_t>(getDstHeight(layer));
                    break;

                case HWC_LAYER_TYPE_MM:
                    layer_config->src_width = static_cast<uint32_t>(WIDTH(layer->getMdpDstRoi()));
                    layer_config->src_height = static_cast<uint32_t>(HEIGHT(layer->getMdpDstRoi()));
                    memcpy(layer_mml_info, layer->getMMLCfg(), sizeof(mml_frame_info));
                    break;

                case HWC_LAYER_TYPE_GLAI:
                    layer_config->src_width  = static_cast<uint32_t>(WIDTH(layer->getGlaiDstRoi()));
                    layer_config->src_height = static_cast<uint32_t>(HEIGHT(layer->getGlaiDstRoi()));
                    layer_config->src_fmt = mapDispInputColorFormat(layer->getGlaiOutFormat());
                    break;

                default:
                    if (layer->getHwlayerType() == HWC_LAYER_TYPE_UI &&
                        (priv_hnd.prexform & HAL_TRANSFORM_ROT_90))
                    {
                        layer_config->src_width  = static_cast<uint32_t>(getSrcHeight(layer));
                        layer_config->src_height = static_cast<uint32_t>(getSrcWidth(layer));
                    }
                    else
                    {
                        layer_config->src_width  = static_cast<uint32_t>(getSrcWidth(layer));
                        layer_config->src_height = static_cast<uint32_t>(getSrcHeight(layer));
                    }
                    break;
            }

            ++layer_config;
            ++layer_mml_info;
        }
    }
}

void DrmHrt::fillDispLayer()
{
    memset(&m_disp_layer, 0, sizeof(drm_mtk_layering_info));
    m_disp_layer.hrt_num = -1;
    if (m_is_validate_separate)
    {
        m_disp_layer.disp_list = m_disp_list;
    }
    for (uint32_t i = 0; i < m_disp_array_size; ++i)
    {
        m_disp_layer.gles_head[i] = -1;
        m_disp_layer.gles_tail[i] = -1;
    }

    // prepare drm_mtk_layering_info for ioctl
    for (uint64_t disp_id : m_disp_id_list)
    {
        HWCDisplay* display = m_displays[disp_id].get();
        if (CC_UNLIKELY(display == nullptr))
        {
            HWC_LOGE("%s(): Failed to get display %" PRIu64, __FUNCTION__, disp_id);
            HWC_ASSERT(0);
            continue;
        }

        IOverlayDevice* ovl_dev = m_disp_devs[disp_id].get();
        if (CC_UNLIKELY(ovl_dev == nullptr))
        {
            HWC_LOGE("%s(): Failed to get ovl_dev %" PRIu64, __FUNCTION__, disp_id);
            HWC_ASSERT(0);
            continue;
        }

        uint32_t id_crtc = display->getDrmIdCurCrtc();
        if (id_crtc == UINT32_MAX)
        {
            continue;
        }

        if (ovl_dev->getType() != OVL_DEVICE_TYPE_OVL)
        {
            continue;
        }
        sp<DrmDevice> drm_dev = reinterpret_cast<DrmDevice*>(ovl_dev);
        uint32_t disp_idx = drm_dev->getHrtIndex(id_crtc);
        const uint32_t disp_input = m_is_validate_separate ? 0 : disp_idx;
        if (CC_UNLIKELY(disp_input >= m_disp_array_size)) {
            HWC_LOGE("(%" PRIu64 ") %s(), id_crtc %u, disp_input %u > m_disp_array_size %d",
                     disp_id,
                     __FUNCTION__,
                     id_crtc,
                     disp_input,
                     m_disp_array_size);
            HWC_ASSERT(0);
            continue;
        }
        if (m_is_validate_separate)
        {
            m_disp_layer.disp_idx = disp_idx;
            if (display->getOtherHotplugChanged())
            {
                HWC_LOGI("%s(), start to run HRT for display %" PRIu64 " after other hutplug. disp_list:%u, disp_idx:%u",
                         __FUNCTION__, disp_id, m_disp_layer.disp_list, m_disp_layer.disp_idx);
            }
        }

        if (!display->isValid() ||
            display->getMirrorSrc() != -1)
        {
            continue;
        }

        unsigned int layers_num = static_cast<unsigned int>(display->getVisibleLayersSortedByZ().size());

        m_disp_layer.input_config[disp_input] = m_layer_config_list[disp_id];
        m_disp_layer.mml_cfg[disp_input] = m_layer_mml_info[disp_id];

        switch (disp_id) {
            case HWC_DISPLAY_PRIMARY:
                m_disp_layer.disp_mode[disp_input] =
                        static_cast<int>(drm_dev->getDrmSessionMode(display->getDrmIdCurCrtc()));
                m_disp_layer.disp_mode_idx[disp_input] = static_cast<int>(display->getActiveConfig());

                if (m_disp_layer.disp_mode[disp_input] == HWC_DISP_INVALID_SESSION_MODE)
                {
                    m_disp_layer.input_config[disp_input] = NULL;
                    layers_num = 0;
                    display->setGlesRange(-1, -1);
                }

                break;
            case HWC_DISPLAY_EXTERNAL:
                m_disp_layer.disp_mode[disp_input] = HWC_DISP_SESSION_DIRECT_LINK_MODE;
                break;

            case HWC_DISPLAY_EXTERNAL_1:
                m_disp_layer.disp_mode[disp_input] = HWC_DISP_SESSION_DIRECT_LINK_MODE;
                break;

            case HWC_DISPLAY_VIRTUAL:
                m_disp_layer.disp_mode[disp_input] = HWC_DISP_SESSION_DECOUPLE_MODE;
                break;
            default:
                HWC_LOGE("%s: Unknown disp_id(%" PRIu64 ")", __func__, disp_id);
        }

        m_disp_layer.layer_num[disp_input] = static_cast<int>(
            (m_layer_config_len[disp_id] < layers_num) ? m_layer_config_len[disp_id] : layers_num);

        DispatcherJob* job = HWCDispatcher::getInstance().getExistJob(disp_id);// add for BW Monitor
        m_disp_layer.frame_idx[disp_input] = (job) ? static_cast<uint32_t>((job->sequence % UINT32_MAX)) : UINT32_MAX;
        if (display->isSupportSmartComposition() && display->hasValidCurrentSet() && display->isFbtUnchanged())
        {
            m_disp_layer.disp_caps[disp_input] |= MTK_GLES_FBT_UNCHANGED;
            HWC_LOGV("(%" PRIu64 ") %s() job:%" PRIu64 ", frame_idx:%d, disp_caps:0x%x" , disp_id, __func__,
                (job) ? job->sequence : UINT64_MAX, m_disp_layer.frame_idx[disp_input], m_disp_layer.disp_caps[disp_input]);
        }

        display->getGlesRange(
            &m_disp_layer.gles_head[disp_input],
            &m_disp_layer.gles_tail[disp_input]);

        m_hwc_gles_head[disp_id] = m_disp_layer.gles_head[disp_input];
        m_hwc_gles_tail[disp_id] = m_disp_layer.gles_tail[disp_input];
        HWC_LOGV("(%" PRIu64 ") %s(), disp_input:%u, m_disp_layer.gles_head:%d, m_disp_layer.gles_tail:%d",
            disp_id, __func__, disp_input, m_disp_layer.gles_head[disp_input],m_disp_layer.gles_tail[disp_input] );

        if (display->getWdmaStatus() != HWC_WDMA_STATUS_DISABLE)
        {
            uint32_t format;
            display->getWdmaConfig(
                format,
                m_disp_layer.wb_cfg[disp_input].src_width,
                m_disp_layer.wb_cfg[disp_input].src_height,
                m_disp_layer.wb_cfg[disp_input].dst_width,
                m_disp_layer.wb_cfg[disp_input].dst_height);
            m_disp_layer.wb_cfg[disp_input].fmt = mapDispInputColorFormat(format);

            HWC_LOGV("%s disp:%" PRIu64 ", disp_input:%u WDMA is enable, format:%d, src:%d, %d, dst:%d, %d", __FUNCTION__,
                disp_id, disp_input,
                m_disp_layer.wb_cfg[disp_input].fmt,
                m_disp_layer.wb_cfg[disp_input].src_width,
                m_disp_layer.wb_cfg[disp_input].src_height,
                m_disp_layer.wb_cfg[disp_input].dst_width,
                m_disp_layer.wb_cfg[disp_input].dst_height);
        }

    }
}

void DrmHrt::fillLayerInfoOfDispatcherJob()
{
    // DbgLogger logger(DbgLogger::TYPE_HWC_LOG, 'D',"fillLayerInfoOfDispatcherJob()");
    for (uint64_t disp_id : m_disp_id_list)
    {
        HWCDisplay* display = m_displays[disp_id].get();
        if (CC_UNLIKELY(display == nullptr))
        {
            HWC_LOGE("%s(): Failed to get display %" PRIu64, __FUNCTION__, disp_id);
            HWC_ASSERT(0);
            continue;
        }

        IOverlayDevice* ovl_dev = m_disp_devs[disp_id].get();
        if (CC_UNLIKELY(ovl_dev == nullptr))
        {
            HWC_LOGE("%s(): Failed to get ovl_dev %" PRIu64, __FUNCTION__, disp_id);
            HWC_ASSERT(0);
            continue;
        }

        uint32_t id_crtc = display->getDrmIdCurCrtc();
        if (id_crtc == UINT32_MAX)
        {
            continue;
        }

        DispatcherJob* job = HWCDispatcher::getInstance().getExistJob(disp_id);

        if (!display->isConnected() || NULL == job || display->getMirrorSrc() != -1)
        {
            HWC_LOGV("(%" PRIu64 ") %s(), job:%p display->getMirrorSrc():%d", disp_id, __FUNCTION__, job, display->getMirrorSrc());
            continue;
        }

        // only support two display at the same time
        // index 0: primary display; index 1: secondry display(MHL or vds)
        // fill display info
        if (ovl_dev->getType() != OVL_DEVICE_TYPE_OVL)
        {
            continue;
        }
        sp<DrmDevice> drm_dev = reinterpret_cast<DrmDevice*>(ovl_dev);
        const uint32_t disp_input = m_is_validate_separate ? 0 : drm_dev->getHrtIndex(id_crtc);
        uint32_t numDisp = std::min(static_cast<uint32_t>(DisplayManager::MAX_DISPLAYS), static_cast<uint32_t>(LYE_CRTC));
        if (CC_UNLIKELY(disp_input >= numDisp)) {
            HWC_LOGE("(%" PRIu64 ") %s(), disp_input %u > MAX_DISPLAYS %d", disp_id, __FUNCTION__, disp_input, DisplayManager::MAX_DISPLAYS);
            continue;
        }
        if (CC_UNLIKELY(disp_input >= m_disp_array_size)) {
            HWC_LOGE("(%" PRIu64 ") %s(), id_crtc %u, disp_input %u > m_disp_array_size %d",
                     disp_id,
                     __FUNCTION__,
                     id_crtc,
                     disp_input,
                     m_disp_array_size);
            HWC_ASSERT(0);
            continue;
        }

        // fill layer info
        if (m_layer_config_len[disp_id] > m_hrt_config_len[disp_id])
        {
            unsigned int layers_num = m_layer_config_len[disp_id];
            if (NULL != m_hrt_config_list[disp_id])
                delete m_hrt_config_list[disp_id];

            m_hrt_config_len[disp_id] = layers_num;
            m_hrt_config_list[disp_id] = new HrtLayerConfig[layers_num];
            if (NULL == m_hrt_config_list[disp_id])
            {
                HWC_LOGE("(%" PRIu64 ") Failed to malloc hrt_config_list (len=%u)", disp_id, layers_num);
                m_hrt_config_len[disp_id] = 0;
                return;
            }
        }
        for (unsigned int i = 0; i < m_layer_config_len[disp_id]; i++)
        {
            if (m_disp_layer.input_config[disp_input] != NULL)
            {
                m_hrt_config_list[disp_id][i].ovl_id = m_disp_layer.input_config[disp_input][i].ovl_id;
                m_hrt_config_list[disp_id][i].ext_sel_layer = m_disp_layer.input_config[disp_input][i].ext_sel_layer;
            }
            else
            {
                m_hrt_config_list[disp_id][i].ovl_id = 0;
                m_hrt_config_list[disp_id][i].ext_sel_layer = -1;
            }
        }
        job->layer_info.hrt_config_list = m_hrt_config_list[disp_id];

        if (m_disp_layer.input_config[disp_input] == NULL)
        {
            const std::vector<sp<HWCLayer> >& layers = display->getVisibleLayersSortedByZ();
            job->layer_info.max_overlap_layer_num = -1;
            job->layer_info.hrt_weight = 0;
            job->layer_info.hrt_idx    = 0;
            const int&& layers_num = static_cast<int>(layers.size());
            job->layer_info.gles_head = layers_num ? 0 : -1;
            job->layer_info.gles_tail = layers_num - 1;
            HWC_LOGV("%s(), disp:%" PRIu64 " gles_head:%d gles_tail:%d hrt:%u,%u with no hrt config", __FUNCTION__,
                disp_id, job->layer_info.gles_head, job->layer_info.gles_tail,
                job->layer_info.hrt_weight, job->layer_info.hrt_idx);
            continue;
        }
        else
        {
            job->layer_info.max_overlap_layer_num = m_disp_layer.hrt_num;
            job->layer_info.hrt_weight = m_disp_layer.hrt_weight;
            job->layer_info.hrt_idx = m_disp_layer.hrt_idx;
            job->layer_info.gles_head = m_disp_layer.gles_head[disp_input];
            job->layer_info.gles_tail = m_disp_layer.gles_tail[disp_input];
            HWC_LOGV("%s(), disp:%" PRIu64 " job:%" PRIu64 " gles_head:%d gles_tail:%d hrt:%u,%u", __FUNCTION__,
                disp_id, (job) ? job->sequence : UINT64_MAX, job->layer_info.gles_head, job->layer_info.gles_tail,
                job->layer_info.hrt_weight, job->layer_info.hrt_idx);
        }

        for (size_t i = 0; i < display->getVisibleLayersSortedByZ().size(); ++i)
        {
            if (static_cast<int32_t>(i) >= m_disp_layer.layer_num[disp_input])
                break;

            auto& layer = display->getVisibleLayersSortedByZ()[i];
            uint32_t layer_caps = m_disp_layer.input_config[disp_input][i].layer_caps;
            layer->setLayerCaps(mapMtkLayeringCaps2HwcLayeringCaps(layer_caps));
            HWC_LOGV("(%" PRIu64 ") %s() id:%" PRIu64" layer_caps:0x%x",
                disp_id, __func__, layer->getId(), layer_caps);
            if (layer->getLayerCaps() & HWC_DISP_CLIENT_CLEAR_LAYER)
            {
                layer->setHWCRequests(layer->getHWCRequests() | HWC2_LAYER_REQUEST_CLEAR_CLIENT_TARGET);
            }

            if (display->isSupportBWMonitor() && layer->isValidUnchangedLayer())// for unchanged layer skip hrt confirm
            {
                bool is_ratio_valid = false;

                if ((layer->getPrevHwlayerType() == HWC_LAYER_TYPE_UI) && (layer->getLayerCaps() & HWC_DISP_UNCHANGED_RATIO_VALID))
                {
                    is_ratio_valid = true;
                }
                layer->setUnchangedRatioValid(is_ratio_valid);
                HWC_LOGV("unchagned layer id:%" PRIu64" layer_caps:0x%x is_ratio_valid:%d unchagned_cnt:%d",
                    layer->getId(), layer_caps, is_ratio_valid, layer->getUnchangedCnt());
            }

        }

        //postRecognition
        if (display->isSupportSmartComposition() && display->hasValidCurrentSet())// call postRecognition(), for inactive layer skip hrt confirm
        {
            uint32_t hwc_disp_caps = mapMtkDispCaps2HwcDispCaps(m_disp_layer.disp_caps[disp_input]);
            display->postRecognitionInactiveSet(m_disp_layer.gles_head[disp_input], m_disp_layer.gles_tail[disp_input], hwc_disp_caps);
        }

        // for (int32_t i = 0 ; i < m_disp_layer.layer_num[disp_input]; ++i)
        //    logger.printf("i:%d ovl_id:%d caps:%d, ", i, m_disp_layer.input_config[disp_input][i].ovl_id, m_disp_layer.input_config[disp_input][i].layer_caps);
    }
}

void DrmHrt::printQueryValidLayerResult()
{
    for (uint64_t disp_id : m_disp_id_list)
    {
        HWCDisplay* display = m_displays[disp_id].get();
        if (CC_UNLIKELY(display == nullptr))
        {
            HWC_LOGE("%s(): Failed to get display %" PRIu64, __FUNCTION__, disp_id);
            HWC_ASSERT(0);
            continue;
        }

        IOverlayDevice* ovl_dev = m_disp_devs[disp_id].get();
        if (CC_UNLIKELY(ovl_dev == nullptr))
        {
            HWC_LOGE("%s(): Failed to get ovl_dev %" PRIu64, __FUNCTION__, disp_id);
            HWC_ASSERT(0);
            continue;
        }

        uint32_t id_crtc = display->getDrmIdCurCrtc();
        if (id_crtc == UINT32_MAX)
        {
            continue;
        }

        if (ovl_dev->getType() != OVL_DEVICE_TYPE_OVL)
        {
            continue;
        }
        sp<DrmDevice> drm_dev = reinterpret_cast<DrmDevice*>(ovl_dev);
        const uint32_t disp_input = m_is_validate_separate ? 0 : drm_dev->getHrtIndex(id_crtc);

        if (m_disp_layer.layer_num[disp_input] == 0)
        {
            continue;
        }

        m_hrt_result[disp_id].str("");
        m_hrt_result[disp_id] << "[HRT DRM]";
        m_hrt_result[disp_id] << " max_overlap:" << m_disp_layer.hrt_num << " hrt:" << m_disp_layer.hrt_weight << " hrt_idx:" << m_disp_layer.hrt_idx;
        m_hrt_result[disp_id] << " [(" << disp_id << ") mode:" << m_disp_layer.disp_mode_idx[disp_input];
        m_hrt_result[disp_id] << " disp_list:" << m_disp_layer.disp_list << " disp_idx:" << m_disp_layer.disp_idx;
        m_hrt_result[disp_id] << " layer_num:" << m_disp_layer.layer_num[disp_input] << " gles["<< m_hwc_gles_head[disp_id] <<"," << m_hwc_gles_tail[disp_id] <<
                ":" << m_disp_layer.gles_head[disp_input] << "," << m_disp_layer.gles_tail[disp_input]<< "] frame_idx:" << m_disp_layer.frame_idx[disp_input] << "]";
        for (int32_t j = 0; j < m_disp_layer.layer_num[disp_input]; ++j)
        {
            const auto& cfg = m_disp_layer.input_config[disp_input][j];
            m_hrt_result[disp_id] << "  [(" << j <<
                ") s_wh:" << cfg.src_width << "," << cfg.src_height <<
                " d_xywh:" << cfg.dst_offset_x << "," << cfg.dst_offset_y << "," << cfg.dst_width << ","<< cfg.dst_height <<
                " caps:" << std::hex << cfg.layer_caps << "]" << std::dec;
        }
        printLog(m_hrt_result[disp_id].str());
    }
}

bool DrmHrt::queryValidLayer()
{
    if (CC_UNLIKELY(m_disp_id_list.empty()))
    {
        HWC_LOGE("%s(): m_disp_id_list is empty, this should not happen!", __FUNCTION__);
        HWC_ASSERT(0);
        return false;
    }

    IOverlayDevice* ovl_dev = m_disp_devs[m_disp_id_list[0]].get();
    if (CC_UNLIKELY(ovl_dev == nullptr))
    {
        HWC_LOGE("%s(): Failed to get ovl_dev %" PRIu64, __FUNCTION__, m_disp_id_list[0]);
        HWC_ASSERT(0);
        return false;
    }

    return ovl_dev->queryValidLayer(&m_disp_layer);
}

void DrmHrt::onHotplug(uint64_t disp_id, bool connected)
{
    if (m_is_validate_separate)
    {
        if (disp_id < DisplayManager::MAX_DISPLAYS)
        {
            HWCDisplay* display = m_displays[disp_id].get();
            if (CC_UNLIKELY(display == nullptr))
            {
                HWC_LOGE("%s(): Failed to get display %" PRIu64, __FUNCTION__, disp_id);
                HWC_ASSERT(0);
                return;
            }

            IOverlayDevice* ovl_dev = m_disp_devs[disp_id].get();
            if (CC_UNLIKELY(ovl_dev == nullptr))
            {
                HWC_LOGE("%s(): Failed to get ovl_dev %" PRIu64, __FUNCTION__, disp_id);
                HWC_ASSERT(0);
                return;
            }

            uint32_t id_crtc = display->getDrmIdCurCrtc();
            if (id_crtc != UINT32_MAX &&
                ovl_dev->getType() == OVL_DEVICE_TYPE_OVL)
            {
                sp<DrmDevice> drm_dev = reinterpret_cast<DrmDevice*>(ovl_dev);
                const uint32_t disp_input = drm_dev->getHrtIndex(id_crtc);
                if (CC_UNLIKELY(disp_input >= m_disp_array_size))
                {
                    HWC_LOGE("(%s(), display %" PRIu64 ": id_crtc %u, disp_input %u > m_disp_array_size %d",
                             __FUNCTION__, disp_id, id_crtc, disp_input, m_disp_array_size);
                    HWC_ASSERT(0);
                }
                else
                {
                    if (connected)
                    {
                        if (!display->isConnected())
                        {
                            HWC_LOGE("%s(): display %" PRIu64 " is not connected", __FUNCTION__, disp_id);
                        }
                        else
                        {
                            m_disp_list |= (1 << disp_input);
                        }
                    }
                    else
                    {
                        m_disp_list &= ~(1 << disp_input);
                    }
                }
            }
        }
        else
        {
            HWC_LOGE("%s(): display %" PRIu64 " is invalid!", __FUNCTION__, disp_id);
        }
    }
}
