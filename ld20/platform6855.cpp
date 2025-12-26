#define DEBUG_LOG_TAG "PLAT"

#include <hardware/hwcomposer.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "utils/debug.h"
#include "utils/tools.h"

#include "platform6855.h"
#include "DpBlitStream.h"

#include "hwc2.h"
#include "dev_interface.h"

extern unsigned int mapDpOrientation(const uint32_t transform);

// ---------------------------------------------------------------------------
Platform_MT6855::Platform_MT6855()
{
    m_config.platform = PLATFORM_MT6855;

    m_config.compose_level = COMPOSE_ENABLE_ALL;

    m_config.mirror_state = MIRROR_DISABLED;

    m_config.rdma_roi_update = 1;

    m_config.use_async_bliter_ultra = true;

    m_config.enable_smart_layer = true;

    m_config.enable_rgba_rotate = true;
    m_config.enable_rgbx_scaling = true;

    m_config.support_color_transform = true;

    m_config.mdp_scale_percentage = 0.1f;

    m_config.extend_mdp_capacity = false;

    m_config.av_grouping = false;

    m_config.disp_support_decompress = true;
    m_config.mdp_support_decompress = true;
    m_config.mdp_support_compress = true;

    m_config.is_support_mdp_pmqos = true;

    m_config.is_ovl_support_RGBA1010102 = true;
    m_config.is_mdp_support_RGBA1010102 = true;

    m_config.is_client_clear_support = true;

    m_config.plat_switch |= HWC_PLAT_SWITCH_MULTIPLE_FB_CACHE;
    m_config.plat_switch |= HWC_PLAT_SWITCH_NOTIFY_HWBINDER_TID;

    if (!getHwDevice()->isHwcFeatureSupported(HWC_FEATURE_OVL_VIRUTAL_DISPLAY))
    {
        m_config.blitdev_for_virtual = true;
        m_config.is_support_ext_path_for_virtual =
              m_config.blitdev_for_virtual ? true : false;
    }
}

size_t Platform_MT6855::getLimitedExternalDisplaySize()
{
    // 4k resolution
    return 3840 * 2160;
}

bool Platform_MT6855::isUILayerValid(const sp<HWCLayer>& layer, int32_t* line)
{
    return PlatformCommon::isUILayerValid(layer, line);
}


bool Platform_MT6855::isMMLayerValid(const sp<HWCLayer>& layer, const int32_t pq_mode_id,
        int32_t* line)
{
    return PlatformCommon::isMMLayerValid(layer, pq_mode_id, line);
}
