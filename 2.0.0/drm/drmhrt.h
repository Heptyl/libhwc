#ifndef HWC_DRM_HRT_H
#define HWC_DRM_HRT_H

#include <stdint.h>
#include <linux/mediatek_drm.h>

#include "hrt_common.h"
#include "display.h"
#include "mtk-mml.h"

struct HrtLayerConfig;

class DrmHrt : public HrtCommon
{
public:
    DrmHrt()
        : m_disp_list(0)
    {
        memset(m_layer_config_list, 0, sizeof(m_layer_config_list));
        memset(&m_disp_layer, 0, sizeof(m_disp_layer));
        memset(m_layer_mml_info, 0, sizeof(m_layer_mml_info));
        memset(&m_hwc_gles_head, -1, sizeof(m_hwc_gles_head));
        memset(&m_hwc_gles_tail, -1, sizeof(m_hwc_gles_tail));
        m_disp_array_size = sizeof(m_disp_layer.disp_mode) / sizeof(m_disp_layer.disp_mode[0]);
    }
    ~DrmHrt()
    {
        for (int i = 0; i < DisplayManager::MAX_DISPLAYS; ++i)
        {
            if (m_layer_config_list[i])
                free(m_layer_config_list[i]);
        }
    }
    bool isEnabled() const;

    bool isRPOEnabled() const;

    void printQueryValidLayerResult();

    void fillLayerConfigList();

    void fillDispLayer();

    void fillLayerInfoOfDispatcherJob();

    bool queryValidLayer();

    void onHotplug(uint64_t disp_id, bool connected);

private:
    uint32_t m_disp_list;

    mml_frame_info* m_layer_mml_info[DisplayManager::MAX_DISPLAYS];
    drm_mtk_layer_config* m_layer_config_list[DisplayManager::MAX_DISPLAYS];
    drm_mtk_layering_info m_disp_layer;
    int m_hwc_gles_head[DisplayManager::MAX_DISPLAYS];
    int m_hwc_gles_tail[DisplayManager::MAX_DISPLAYS];
    uint32_t m_disp_array_size;
};

#endif
