#define DEBUG_LOG_TAG "HRT"

#include "hrt.h"

#include <vector>

#include "hwdev.h"
#include "overlay.h"
#include "sync.h"
#include "dispatcher.h"
#include "hwc2.h"
#include "dev_interface.h"

static int32_t mapLayeringCaps2HwcLayeringCaps(unsigned int caps)
{
    int32_t tmp = 0;

    if (caps & LAYERING_OVL_ONLY)
    {
        tmp |= HWC_LAYERING_OVL_ONLY;
    }

    if (caps & MDP_RSZ_LAYER)
    {
        tmp |= HWC_MDP_RSZ_LAYER;
    }

    if (caps & DISP_RSZ_LAYER)
    {
        tmp |= HWC_DISP_RSZ_LAYER;
    }

    if (caps & MDP_ROT_LAYER)
    {
        tmp |= HWC_MDP_ROT_LAYER;
    }

    if (caps & MDP_HDR_LAYER)
    {
        tmp |= HWC_MDP_HDR_LAYER;
    }

    if (caps & NO_FBDC)
    {
        tmp |= HWC_NO_FBDC;
    }

    if (caps & CLIENT_CLEAR_LAYER)
    {
        tmp |= HWC_CLIENT_CLEAR_LAYER;
    }

    if (caps & DISP_CLIENT_CLEAR_LAYER)
    {
        tmp |= HWC_DISP_CLIENT_CLEAR_LAYER;
    }

    return tmp;
}

static unsigned int mapHwcLayeringCaps2LayeringCaps(int32_t caps)
{
    unsigned int tmp = 0;

    if (caps & HWC_LAYERING_OVL_ONLY)
    {
        tmp |= LAYERING_OVL_ONLY;
    }

    if (caps & HWC_MDP_RSZ_LAYER)
    {
        tmp |= MDP_RSZ_LAYER;
    }

    if (caps & HWC_DISP_RSZ_LAYER)
    {
        tmp |= DISP_RSZ_LAYER;
    }

    if (caps & HWC_MDP_ROT_LAYER)
    {
        tmp |= MDP_ROT_LAYER;
    }

    if (caps & HWC_MDP_HDR_LAYER)
    {
        tmp |= MDP_HDR_LAYER;
    }

    if (caps & HWC_NO_FBDC)
    {
        tmp |= NO_FBDC;
    }

    if (caps & HWC_CLIENT_CLEAR_LAYER)
    {
        tmp |= CLIENT_CLEAR_LAYER;
    }

    if (caps & HWC_DISP_CLIENT_CLEAR_LAYER)
    {
        tmp |= DISP_CLIENT_CLEAR_LAYER;
    }

    return tmp;
}

bool Hrt::isEnabled() const
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

bool Hrt::isRPOEnabled() const
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

void Hrt::fillLayerConfigList()
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
        const unsigned int&& layers_num = static_cast<unsigned int>(layers.size());

        // reallocate layer_config_list if needed
        if (layers_num > m_layer_config_len[disp_id])
        {
            if (NULL != m_layer_config_list[disp_id])
                free(m_layer_config_list[disp_id]);

            m_layer_config_len[disp_id] = layers_num;
            m_layer_config_list[disp_id] = (layer_config*)calloc(m_layer_config_len[disp_id], sizeof(layer_config));
            if (NULL == m_layer_config_list[disp_id])
            {
                HWC_LOGE("(%" PRIu64 ") Failed to malloc layer_config_list (len=%u)", disp_id, layers_num);
                m_layer_config_len[disp_id] = 0;
                return;
            }
        }

        // init and get PrivateHandle
        layer_config* layer_config = m_layer_config_list[disp_id];

        for (auto& layer : layers)
        {
            layer_config->ovl_id        = UINT_MAX;
            layer_config->ext_sel_layer = -1;
            layer_config->src_fmt       =
                (layer->getHwlayerType() == HWC_LAYER_TYPE_DIM) ?
                    DISP_FORMAT_DIM : convertFormat4Hrt(layer->getPrivateHandle().format);

            layer_config->dataspace     = layer->getDataspace();
            layer_config->compress      = isCompressData(&(layer->getPrivateHandle()));

            if (layer->getHwlayerType() == HWC_LAYER_TYPE_MM)
            {
                layer_config->src_fmt = convertFormat4Hrt(layer->decideMdpOutputFormat(*display));
                layer_config->dataspace = layer->decideMdpOutDataspace();
                layer_config->compress = layer->decideMdpOutputCompressedBuffers(*display);
            }
            layer_config->src_offset_y  = static_cast<unsigned int>(getSrcTop(layer));
            layer_config->src_offset_x  = static_cast<unsigned int>(getSrcLeft(layer));
            layer_config->dst_offset_y  = static_cast<unsigned int>(getDstTop(layer));
            layer_config->dst_offset_x  = static_cast<unsigned int>(getDstLeft(layer));
            layer_config->dst_width     = static_cast<unsigned int>(getDstWidth(layer));
            layer_config->dst_height    = static_cast<unsigned int>(getDstHeight(layer));
            layer_config->layer_caps    = mapHwcLayeringCaps2LayeringCaps(layer->getLayerCaps());

            const PrivateHandle& priv_hnd = layer->getPrivateHandle();
            switch(layer->getHwlayerType())
            {
                case HWC_LAYER_TYPE_DIM:
                    layer_config->src_width = static_cast<unsigned int>(getDstWidth(layer));
                    layer_config->src_height = static_cast<unsigned int>(getDstHeight(layer));
                    break;

                case HWC_LAYER_TYPE_MM:
                    layer_config->src_width = static_cast<unsigned int>(WIDTH(layer->getMdpDstRoi()));
                    layer_config->src_height = static_cast<unsigned int>(HEIGHT(layer->getMdpDstRoi()));
                    break;

                default:
                    if (layer->getHwlayerType() == HWC_LAYER_TYPE_UI &&
                        (priv_hnd.prexform & HAL_TRANSFORM_ROT_90))
                    {
                        layer_config->src_width  = static_cast<unsigned int>(getSrcHeight(layer));
                        layer_config->src_height = static_cast<unsigned int>(getSrcWidth(layer));
                    }
                    else
                    {
                        layer_config->src_width  = static_cast<unsigned int>(getSrcWidth(layer));
                        layer_config->src_height = static_cast<unsigned int>(getSrcHeight(layer));
                    }
                    break;
            }

            ++layer_config;
        }
    }
}

void Hrt::fillDispLayer()
{
    memset(&m_disp_layer, 0, sizeof(disp_layer_info));
    m_disp_layer.hrt_num = -1;
    for (uint64_t disp_id : m_disp_id_list)
    {
        const size_t disp_input = (disp_id == HWC_DISPLAY_PRIMARY) ? 0 : 1;

        m_disp_layer.gles_head[disp_input] = -1;
        m_disp_layer.gles_tail[disp_input] = -1;
    }

    // prepare disp_layer_info for ioctl
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

        // driver only supports two displays at the same time
        // disp_input 0: primary display; disp_input 1: secondry display(MHL or vds)
        // fill display info
        const size_t disp_input = (disp_id == HWC_DISPLAY_PRIMARY) ? 0 : 1;

        if (!display->isConnected() ||
            display->getMirrorSrc() != -1 ||
            ovl_dev->getType() != OVL_DEVICE_TYPE_OVL)
        {
            continue;
        }

        const unsigned int layers_num = static_cast<unsigned int>(display->getVisibleLayersSortedByZ().size());

        m_disp_layer.input_config[disp_input] = m_layer_config_list[disp_id];

        switch (disp_id) {
            case HWC_DISPLAY_PRIMARY:
                m_disp_layer.disp_mode[disp_input] =
                    mapHwcDispMode2Disp(ovl_dev->getOverlaySessionMode(disp_id, UINT32_MAX));
                m_disp_layer.active_config_id[disp_input] = static_cast<int>(display->getActiveConfig());
                break;

            case HWC_DISPLAY_EXTERNAL:
                m_disp_layer.disp_mode[disp_input] = DISP_SESSION_DIRECT_LINK_MODE;
                break;

            case HWC_DISPLAY_VIRTUAL:
                m_disp_layer.disp_mode[disp_input] = DISP_SESSION_DECOUPLE_MODE;
                break;

            default:
                HWC_LOGE("%s: Unknown disp_id(%" PRIu64 ")", __func__, disp_id);
        }

        m_disp_layer.layer_num[disp_input] = static_cast<int>(
            (m_layer_config_len[disp_id] < layers_num) ? m_layer_config_len[disp_id] : layers_num);

        display->getGlesRange(
            &m_disp_layer.gles_head[disp_input],
            &m_disp_layer.gles_tail[disp_input]);
        HWC_LOGV("%s disp:%" PRIu64 " m_disp_layer.gles_head[disp_input]:%d, m_disp_layer.gles_tail[disp_input]:%d",
            __func__, disp_id, m_disp_layer.gles_head[disp_input],m_disp_layer.gles_tail[disp_input] );
    }
}

void Hrt::fillLayerInfoOfDispatcherJob()
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

        DispatcherJob* job = HWCDispatcher::getInstance().getExistJob(disp_id);
        int32_t mir_src = display->getMirrorSrc();

        if (!display->isConnected() || NULL == job || mir_src != -1)
        {
            HWC_LOGV("%s(), job:%p display->getMirrorSrc():%d", __FUNCTION__, job, mir_src);
            continue;
        }

        // only support two display at the same time
        // index 0: primary display; index 1: secondry display(MHL or vds)
        // fill display info
        const size_t disp_input = (disp_id == HWC_DISPLAY_PRIMARY) ? 0 : 1;

        // fill layer info
        if (m_layer_config_len[disp_id] > m_hrt_config_len[disp_input])
        {
            unsigned int layers_num = m_layer_config_len[disp_id];
            if (NULL != m_hrt_config_list[disp_input])
                delete m_hrt_config_list[disp_input];

            m_hrt_config_len[disp_input] = layers_num;
            m_hrt_config_list[disp_input] = new HrtLayerConfig[layers_num];
            if (NULL == m_hrt_config_list[disp_input])
            {
                HWC_LOGE("(%" PRIu64 ") Failed to malloc hrt_config_list (len=%u)", disp_id, layers_num);
                m_hrt_config_len[disp_input] = 0;
                job->layer_info.hrt_config_list = nullptr;
                return;
            }
        }
        for (unsigned int i = 0; i < m_layer_config_len[disp_id]; i++)
        {
            if (ovl_dev->getType() == OVL_DEVICE_TYPE_OVL &&
                m_disp_layer.input_config[disp_input] != NULL)
            {
                m_hrt_config_list[disp_input][i].ovl_id = m_disp_layer.input_config[disp_input][i].ovl_id;
                m_hrt_config_list[disp_input][i].ext_sel_layer = m_disp_layer.input_config[disp_input][i].ext_sel_layer;
            }
            else
            {
                m_hrt_config_list[disp_input][i].ovl_id = 0;
                m_hrt_config_list[disp_input][i].ext_sel_layer = -1;
            }
        }
        job->layer_info.hrt_config_list = m_hrt_config_list[disp_input];

        if (m_disp_layer.input_config[disp_input] == NULL)
        {
            const std::vector<sp<HWCLayer> >& layers = display->getVisibleLayersSortedByZ();
            job->layer_info.max_overlap_layer_num = -1;
            job->layer_info.hrt_weight = 0;
            job->layer_info.hrt_idx    = 0;

            const int&& layers_num = static_cast<int>(layers.size());
            job->layer_info.gles_head = layers_num ? 0 : -1;
            job->layer_info.gles_tail = layers_num - 1;
            HWC_LOGV("%s(), disp:%" PRIu64 " gles_head:%d gles_tail:%d with no hrt config", __FUNCTION__,
                disp_id, job->layer_info.gles_head, job->layer_info.gles_tail);
            continue;
        }
        else
        {
            job->layer_info.max_overlap_layer_num = m_disp_layer.hrt_num;
            job->layer_info.hrt_weight = m_disp_layer.hrt_weight;
            job->layer_info.hrt_idx    = m_disp_layer.hrt_idx;
            job->layer_info.gles_head  = m_disp_layer.gles_head[disp_input];
            job->layer_info.gles_tail  = m_disp_layer.gles_tail[disp_input];
            HWC_LOGV("%s(), disp:%" PRIu64 " gles_head:%d gles_tail:%d", __FUNCTION__,
                disp_id, job->layer_info.gles_head, job->layer_info.gles_tail);
        }

        for (size_t i = 0; i < display->getVisibleLayersSortedByZ().size(); ++i)
        {
            if (static_cast<int32_t>(i) >= m_disp_layer.layer_num[disp_input])
                break;

            auto& layer = display->getVisibleLayersSortedByZ()[i];
            layer->setLayerCaps(mapLayeringCaps2HwcLayeringCaps(m_disp_layer.input_config[disp_input][i].layer_caps));
            if (layer->getLayerCaps() & HWC_DISP_CLIENT_CLEAR_LAYER)
            {
                layer->setHWCRequests(layer->getHWCRequests() | HWC2_LAYER_REQUEST_CLEAR_CLIENT_TARGET);
            }
        }
        // for (int32_t i = 0 ; i < m_disp_layer.layer_num[disp_input]; ++i)
        //    logger.printf("i:%d ovl_id:%d caps:%d, ", i, m_disp_layer.input_config[disp_input][i].ovl_id, m_disp_layer.input_config[disp_input][i].layer_caps);
    }
}

void Hrt::printQueryValidLayerResult()
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

        const size_t disp_input = (disp_id == HWC_DISPLAY_PRIMARY) ? 0 : 1;

        m_hrt_result[disp_id].str("");
        m_hrt_result[disp_id] << "[HRT]";
        m_hrt_result[disp_id] << " [(" << disp_id << ") active_config_id:" << m_disp_layer.active_config_id[disp_input] << "]";
        for (int32_t j = 0; j < m_disp_layer.layer_num[disp_input]; ++j)
        {
            const auto& cfg = m_disp_layer.input_config[disp_input][j];
            m_hrt_result[disp_id] << " [(" << disp_id << "," << j <<
                ") s_wh:" << cfg.src_width << "," << cfg.src_height <<
                " d_xywh:" << cfg.dst_offset_x << "," << cfg.dst_offset_y << "," << cfg.dst_width << ","<< cfg.dst_height <<
                " caps:" << cfg.layer_caps << "]";
        }
        printLog(m_hrt_result[disp_id].str());
    }
}

bool Hrt::queryValidLayer()
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
