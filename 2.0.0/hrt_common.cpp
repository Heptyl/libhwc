#define DEBUG_LOG_TAG "HRT"

#include "hrt_common.h"

#include <vector>

#include "overlay.h"
#include "dispatcher.h"
#include "hwc2.h"

#ifndef MTK_HWC_USE_DRM_DEVICE
#include "legacy/hrt.h"
#else
#include "drm/drmhrt.h"
#endif

#include "utils/debug.h"

bool HrtCommon::isEnabled() const
{
    return false;
}

bool HrtCommon::isRPOEnabled() const
{
    return false;
}

void HrtCommon::setCompType()
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

        DispatcherJob* job = HWCDispatcher::getInstance().getExistJob(disp_id);

        if (!display->isConnected() || NULL == job || display->getMirrorSrc() != -1)
            continue;

        // only support two display at the same time
        // index 0: primary display; index 1: secondry display(MHL or vds)
        // fill display info
        int32_t gles_head = -1, gles_tail = -1;
        display->getGlesRange(&gles_head, &gles_tail);
        if (gles_head != job->layer_info.gles_head || gles_tail != job->layer_info.gles_tail)
        {
            gles_head = job->layer_info.gles_head;
            gles_tail = job->layer_info.gles_tail;
            display->setGlesRange(gles_head, gles_tail);
        }
    }
}

void HrtCommon::dump(String8* str, const hwc2_display_t& disp_id) const
{
    if (disp_id < DisplayManager::MAX_DISPLAYS)
    {
        str->appendFormat("\n%s\n\n", m_hrt_result[disp_id].str().c_str());
    }
}

void HrtCommon::printLog(const std::string& str)
{
    size_t index = 0;
    size_t max_length = 224;
    std::string sub;

    while (index < str.length())
    {
        if (str.length() <= index + max_length)
        {
            sub = str.substr(index);
        }
        else
        {
            sub = str.substr(index, max_length);
        }
        index += max_length;
        HWC_LOGD("%s", sub.c_str());
    }
}

void HrtCommon::modifyMdpDstRoiIfRejectedByRpo()
{
    for (uint64_t disp_id : m_disp_id_list)
    {
        HWCDisplay* disp = m_displays[disp_id].get();
        if (CC_UNLIKELY(disp == nullptr))
        {
            HWC_LOGE("%s(): Failed to get display %" PRIu64, __FUNCTION__, disp_id);
            HWC_ASSERT(0);
            continue;
        }

        if (!disp->isValid())
            continue;

        for (auto& layer : disp->getVisibleLayersSortedByZ())
        {
            if (layer->getHwlayerType() == HWC_LAYER_TYPE_MM &&
                (WIDTH(layer->getMdpDstRoi()) != WIDTH(layer->getDisplayFrame()) ||
                HEIGHT(layer->getMdpDstRoi()) != HEIGHT(layer->getDisplayFrame())) &&
                (layer->getLayerCaps() & HWC_DISP_RSZ_LAYER) == 0)
            {
                layer->editMdpDstRoi().left = 0;
                layer->editMdpDstRoi().top = 0;
                layer->editMdpDstRoi().right = WIDTH(layer->getDisplayFrame());
                layer->editMdpDstRoi().bottom = HEIGHT(layer->getDisplayFrame());
            }
        }
    }
}

void HrtCommon::simpleLayeringRule()
{
    for (uint64_t disp_id : m_disp_id_list)
    {
        HWCDisplay* hwc_display = m_displays[disp_id].get();
        if (CC_UNLIKELY(hwc_display == nullptr))
        {
            HWC_LOGE("%s(): Failed to get display %" PRIu64, __FUNCTION__, disp_id);
            HWC_ASSERT(0);
            continue;
        }

        if (!hwc_display->isValid())
            continue;

        // AsyncBlitDevice do not need simpleLayeringRule
        IOverlayDevice* ovl_dev = m_disp_devs[disp_id].get();
        if (CC_UNLIKELY(ovl_dev == nullptr))
        {
            HWC_LOGE("%s(): Failed to get ovl_dev %" PRIu64, __FUNCTION__, disp_id);
            HWC_ASSERT(0);
            continue;
        }

        if (ovl_dev->getType() != OVL_DEVICE_TYPE_OVL)
        {
            continue;
        }

        if (hwc_display->getMirrorSrc() != -1)
            continue;

        DispatcherJob* job = HWCDispatcher::getInstance().getExistJob(disp_id);

        if (NULL == job)
            continue;

        int32_t gles_head = -1, gles_tail = -1;
        hwc_display->getGlesRange(&gles_head, &gles_tail);
        const bool only_hwc_comp = (gles_tail == -1) && (gles_head == -1);
        if (!only_hwc_comp &&
            ((gles_head | gles_tail) < 0 || (gles_head > gles_tail)))
        {
            HWC_LOGE("wrong GLES range (%d,%d)", gles_head, gles_tail);
            abort();
        }

        const int num_hwc_layers = static_cast<int>(hwc_display->getVisibleLayersSortedByZ().size());
        int max_layer = static_cast<int>(job->num_layers);
        const int gles_count = only_hwc_comp ? 0 : (gles_tail - gles_head + 1);
        const int hwc_count = num_hwc_layers - gles_count;
        const int committed_count = only_hwc_comp ? hwc_count : hwc_count + 1;

        if (committed_count > max_layer)
        {
            const std::vector<sp<HWCLayer> >& layers = hwc_display->getVisibleLayersSortedByZ();
            bool has_clear_client_layers = false;
            for (size_t i = 1; i < layers.size() - 1; ++i)
            {
                sp<HWCLayer> layer = layers[i];
                if (layer->getLayerCaps() & HWC_CLIENT_CLEAR_LAYER)
                {
                    has_clear_client_layers = true;
                    layer->setHWCRequests(layer->getHWCRequests() | HWC2_LAYER_REQUEST_CLEAR_CLIENT_TARGET);
                    max_layer--;
                }
                if (max_layer == 1)
                    break;
            }

            const int over_layer_count = committed_count - max_layer;
            if (has_clear_client_layers)
            {
                gles_tail = static_cast<int>(layers.size()) - 1;
                gles_head = 0;
            }
            else if (only_hwc_comp)
            {
                gles_tail = num_hwc_layers - 1;
                gles_head = gles_tail - over_layer_count;
            }
            else
            {
                const int hwc_num_after_gles_tail = num_hwc_layers - 1 - gles_tail;
                const int hwc_num_before_gles_head = gles_head;
                const int excess_layer = over_layer_count > hwc_num_after_gles_tail ?
                                         over_layer_count - hwc_num_after_gles_tail : 0;
                if (excess_layer > hwc_num_before_gles_head)
                {
                    HWC_LOGE("wrong GLES head range (%d,%d) (%d,%d)", gles_head, gles_tail, hwc_count, max_layer);
                    abort();
                }
                gles_tail = excess_layer == 0 ?
                            (gles_tail + over_layer_count) : (num_hwc_layers - 1);
                gles_head -= excess_layer;
            }
            hwc_display->setGlesRange(gles_head, gles_tail);
        }
    }
}

void HrtCommon::fillLayerInfoOfDispatcherJob()
{
    static unsigned int hrt_idx = 0;
    for (uint64_t disp_id : m_disp_id_list)
    {
        HWCDisplay* hwc_display = m_displays[disp_id].get();
        if (CC_UNLIKELY(hwc_display == nullptr))
        {
            HWC_LOGE("%s(): Failed to get display %" PRIu64, __FUNCTION__, disp_id);
            HWC_ASSERT(0);
            continue;
        }

        if (!hwc_display->isValid())
            continue;

        if (hwc_display->getMirrorSrc() != -1)
            continue;

        DispatcherJob* job = HWCDispatcher::getInstance().getExistJob(disp_id);

        if (!job)
        {
            continue;
        }

        // fill layer info
        const std::vector<sp<HWCLayer> >& layers = hwc_display->getVisibleLayersSortedByZ();
        const unsigned int layers_num = static_cast<unsigned int>(layers.size());
        if (layers_num > m_hrt_config_len[disp_id])
        {
            if (NULL != m_hrt_config_list[disp_id])
                delete m_hrt_config_list[disp_id];

            m_hrt_config_len[disp_id] = layers_num;
            m_hrt_config_list[disp_id] = new HrtLayerConfig[layers_num];
            if (NULL == m_hrt_config_list[disp_id])
            {
                HWC_LOGE("(%" PRIu64 ") Failed to malloc hrt_config_list (len=%d)", disp_id, layers_num);
                m_hrt_config_len[disp_id] = 0;
                return;
            }
        }

        int32_t gles_head = -1, gles_tail = -1;
        hwc_display->getGlesRange(&gles_head, &gles_tail);
        job->layer_info.gles_head = gles_head;
        job->layer_info.gles_tail = gles_tail;
        unsigned int ovl_index = 0;
        unsigned int clear_layer_num = hwc_display->getClientClearLayerNum();
        bool has_clear_client_layers = clear_layer_num > 0;
        int32_t layer_idx = 0;
        for (size_t i = 0; i < layers.size(); ++i, ++layer_idx)
        {
            sp<HWCLayer> layer = layers[i];
            if (has_clear_client_layers)
            {
                if (layer_idx >= gles_head || layer_idx <= gles_tail)
                {
                    if (layer->getHWCRequests() & HWC2_LAYER_REQUEST_CLEAR_CLIENT_TARGET)
                    {
                        m_hrt_config_list[disp_id][i].ovl_id = ovl_index;
                        HWC_LOGV("%s(), layer%zu id:%u dev clear",
                                 __FUNCTION__, i, m_hrt_config_list[disp_id][i].ovl_id);
                        ovl_index++;
                        clear_layer_num--;
                    }
                    else
                    {
                        m_hrt_config_list[disp_id][i].ovl_id = ovl_index + clear_layer_num;
                        HWC_LOGV("%s(), layer%zu id:%u cli", __FUNCTION__, i, m_hrt_config_list[disp_id][i].ovl_id);
                    }
                }
                else
                {
                    m_hrt_config_list[disp_id][i].ovl_id = ovl_index;
                    HWC_LOGV("%s(), layer%zu id:%u dev", __FUNCTION__, i, m_hrt_config_list[disp_id][i].ovl_id);
                    ovl_index++;
                }
            }
            else
            {
                HWC_LOGV("%s(), layer%zu id:%u ", __FUNCTION__, i, ovl_index);
                m_hrt_config_list[disp_id][i].ovl_id = ovl_index;
                if ( layer_idx < gles_head || layer_idx >= gles_tail)
                {
                    ovl_index++;
                }
            }
            m_hrt_config_list[disp_id][i].ext_sel_layer = -1;
        }
        job->layer_info.hrt_config_list = m_hrt_config_list[disp_id];
        job->layer_info.max_overlap_layer_num = -1;
        job->layer_info.hrt_weight = 0;
        job->layer_info.hrt_idx = hrt_idx++;

        HWC_LOGV("%s(), disp:%" PRIu64 " gles_head:%d gles_tail:%d hrt:%u,%u", __FUNCTION__,
                 disp_id, job->layer_info.gles_head, job->layer_info.gles_tail,
                 job->layer_info.hrt_weight, job->layer_info.hrt_idx);
    }
}

void HrtCommon::init(const std::vector<sp<HWCDisplay>>& displays, const std::vector<sp<IOverlayDevice>>& disp_devs)
{
    for (size_t i = 0; i < DisplayManager::MAX_DISPLAYS; ++i)
    {
        m_displays[i].clear();
        m_disp_devs[i].clear();
    }
    for (auto& display : displays)
    {
        if (display)
        {
            uint64_t disp_id = display->getId();
            if (disp_id < disp_devs.size() && disp_id < DisplayManager::MAX_DISPLAYS)
            {
                sp<IOverlayDevice> disp_dev = disp_devs[static_cast<size_t>(disp_id)];
                if (disp_dev)
                {
                    if (m_displays[disp_id] != nullptr)
                    {
                        HWC_LOGW("%s(): set display %" PRIu64 " duplicately!", __FUNCTION__, disp_id);
                    }
                    m_displays[disp_id] = display;
                    m_disp_devs[disp_id] = disp_dev;
                }
            }
            else
            {
                HWC_LOGE("%s(): display %" PRIu64 " is invalid!", __FUNCTION__, disp_id);
            }
        }
    }
}

void HrtCommon::init(const std::vector<sp<HWCDisplay>>& displays, const std::vector<sp<IOverlayDevice>>& disp_devs, int32_t ovl_type)
{
    m_is_validate_separate = false;
    init(displays, disp_devs);
    m_disp_id_list.clear();
    for (uint64_t disp_id = 0; disp_id < DisplayManager::MAX_DISPLAYS; ++disp_id)
    {
        HWCDisplay* display = m_displays[disp_id].get();
        if (display != nullptr)
        {
            IOverlayDevice* ovl_dev = m_disp_devs[disp_id].get();
            if (ovl_dev != nullptr)
            {
                if (ovl_dev->getType() == ovl_type)
                {
                    m_disp_id_list.push_back(disp_id);
                }
            }
        }
    }
}

void HrtCommon::init(const std::vector<sp<HWCDisplay>>& displays, const std::vector<sp<IOverlayDevice>>& disp_devs, uint64_t disp_id)
{
    m_is_validate_separate = true;
    init(displays, disp_devs);
    m_disp_id_list.clear();
    if (disp_id < DisplayManager::MAX_DISPLAYS)
    {
        HWCDisplay* display = m_displays[disp_id].get();
        if (display != nullptr)
        {
            IOverlayDevice* ovl_dev = m_disp_devs[disp_id].get();
            if (ovl_dev != nullptr)
            {
                m_disp_id_list.push_back(disp_id);
            }
        }
    }
    else
    {
        HWC_LOGE("%s(): display %" PRIu64 " is invalid", __FUNCTION__, disp_id);
        HWC_ASSERT(0);
    }
}

void HrtCommon::run(const bool& is_skip_validate)
{
    if (m_disp_id_list.empty())
    {
        return;
    }

    updateGlesRangeForDisplaysBeforHrt();
    if (0 == isEnabled())
    {
        HrtCommon::simpleLayeringRule();
        updateGlesRangeHwTypeForDisplays();
        HrtCommon::fillLayerInfoOfDispatcherJob();
        return;
    }

    if (is_skip_validate)
    {
        updateGlesRangeHwTypeForDisplays();
        fillLayerInfoOfDispatcherJob();
        return;
    }

    fillLayerConfigList();

    fillDispLayer();

    if (queryValidLayer())
    {
        fillLayerInfoOfDispatcherJob();
        setCompType();
        if (Debugger::m_skip_log != 1)
        {
            printQueryValidLayerResult();
        }
        if (isRPOEnabled())
        {
            modifyMdpDstRoiIfRejectedByRpo();
        }
        updateGlesRangeHwTypeForDisplays();
    }
    else
    {
        HWC_LOGE("%s: an error when hrt calculating!", __func__);

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

            DispatcherJob* job = HWCDispatcher::getInstance().getExistJob(disp_id);

            if (NULL == job || display->getMirrorSrc() != -1)
                continue;

            job->layer_info.max_overlap_layer_num = -1;
        }
    }
}

void HrtCommon::updateGlesRangeForDisplaysBeforHrt()
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

        display->updateGlesRange();
    }
}

void HrtCommon::updateGlesRangeHwTypeForDisplays()
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

        display->updateGlesRangeHwType();
    }
}

HrtHelper& HrtHelper::getInstance()
{
    static HrtHelper g_instance;
    return g_instance;
}

void HrtHelper::dump(String8* str, const hwc2_display_t& disp_id)
{
    if (m_is_validate_separate)
    {
        if (disp_id < m_hrt.size())
        {
            m_hrt[static_cast<size_t>(disp_id)]->dump(str, disp_id);
        }
        else
        {
            HWC_LOGE("%s(): display %" PRIu64 " is invalid", __FUNCTION__, disp_id);
        }
    }
    else
    {
        m_hrt[HRT_TYPE_OVL]->dump(str, disp_id);
        m_hrt[HRT_TYPE_BLITDEV]->dump(str, disp_id);
    }
}

void HrtHelper::init(const std::vector<sp<HWCDisplay>>& displays, const std::vector<sp<IOverlayDevice>>& disp_devs, bool is_validate_separate)
{
    m_is_validate_separate = is_validate_separate;
    if (is_validate_separate)
    {
        for (size_t i = m_hrt.size(); i < DisplayManager::MAX_DISPLAYS; ++i)
        {
#ifndef MTK_HWC_USE_DRM_DEVICE
            m_hrt.push_back(sp<HrtCommon>(new Hrt()));
#else
            m_hrt.push_back(sp<HrtCommon>(new DrmHrt()));
#endif
        }
        for (auto& display : displays)
        {
            if (display)
            {
                uint64_t disp_id = display->getId();
                if (disp_id < m_hrt.size())
                {
                    m_hrt[static_cast<size_t>(disp_id)]->init(displays, disp_devs, disp_id);
                }
                else
                {
                    HWC_LOGE("%s(): display %" PRIu64 " is invalid", __FUNCTION__, disp_id);
                }
            }
        }
    }
    else
    {
        for (size_t i = m_hrt.size(); i < HRT_TYPE_NUM; ++i)
        {
#ifndef MTK_HWC_USE_DRM_DEVICE
            m_hrt.push_back(sp<HrtCommon>(new Hrt()));
#else
            m_hrt.push_back(sp<HrtCommon>(new DrmHrt()));
#endif
        }
        m_hrt[HRT_TYPE_OVL]->init(displays, disp_devs, OVL_DEVICE_TYPE_OVL);
        m_hrt[HRT_TYPE_BLITDEV]->init(displays, disp_devs, OVL_DEVICE_TYPE_BLITDEV);
    }
}

void HrtHelper::run(const bool& is_skip_validate)
{
    if (CC_UNLIKELY(m_is_validate_separate))
    {
        HWC_LOGE("%s(), call single validate API when separate mode", __FUNCTION__);
        HWC_ASSERT(0);
        return;
    }
    m_hrt[HRT_TYPE_OVL]->run(is_skip_validate);
    m_hrt[HRT_TYPE_BLITDEV]->run(is_skip_validate);
}

void HrtHelper::run(uint64_t disp_id, const bool& is_skip_validate)
{
    if (CC_UNLIKELY(!m_is_validate_separate))
    {
        HWC_LOGE("%s(), call separate validate API when single mode", __FUNCTION__);
        HWC_ASSERT(0);
        return;
    }
    if (disp_id < m_hrt.size())
    {
        m_hrt[static_cast<size_t>(disp_id)]->run(is_skip_validate);
    }
    else
    {
        HWC_LOGE("%s(): display %" PRIu64 " is invalid", __FUNCTION__, disp_id);
    }
}

void notifyOtherHotplug(uint64_t disp_id)
{
    for (uint64_t i = 0; i < DisplayManager::MAX_DISPLAYS; i++)
    {
        if (i != disp_id)
        {
            sp<HWCDisplay> hwc_display = HWCMediator::getInstance().getHWCDisplay(i);
            if (hwc_display != nullptr && hwc_display->isConnected())
            {
                hwc_display->onOtherHotplug();
                DisplayManager::getInstance().refreshForDisplay(i, HWC_REFRESH_FOR_OTHER_HOTPLUG);
            }
        }
    }
}

void HrtHelper::onHotplug(uint64_t disp_id, bool connected)
{
    if (m_is_validate_separate)
    {
        for (auto& hrt : m_hrt)
        {
            hrt->onHotplug(disp_id, connected);
        }
        notifyOtherHotplug(disp_id);
    }
}

HrtHelper::HrtHelper()
    : m_is_validate_separate(false)
{}
