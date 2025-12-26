#define DEBUG_LOG_TAG "PLAT"

#include <hardware/hwcomposer.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "utils/debug.h"
#include "utils/tools.h"

#include "platform.h"
#include "DpBlitStream.h"

#include "hwc2.h"
extern unsigned int mapDpOrientation(const uint32_t transform);

// ---------------------------------------------------------------------------

Platform_MT8195::Platform_MT8195()
{
    m_config.platform = PLATFORM_MT8195;

    m_config.compose_level = COMPOSE_ENABLE_ALL;

    m_config.mirror_state = MIRROR_DISABLED;

    m_config.rdma_roi_update = 1;

    m_config.use_async_bliter_ultra = true;

    m_config.enable_smart_layer = true;

    m_config.enable_rgba_rotate = true;
    m_config.enable_rgbx_scaling = true;

    m_config.support_color_transform = true;

    m_config.mdp_scale_percentage = 0.1f;

    m_config.extend_mdp_capacity = true;

    m_config.av_grouping = false;

    m_config.disp_support_decompress = true;
    m_config.mdp_support_decompress = true;

    m_config.is_support_mdp_pmqos = true;

    m_config.is_ovl_support_RGBA1010102 = true;
    m_config.is_mdp_support_RGBA1010102 = true;

    m_config.is_client_clear_support = true;

    m_config.ethdr_meta_support = true;

    m_config.only_wfd_by_hwc = true;
    // ToDo: WFD should set 1
    m_config.vir_disp_sup_num = 0;

    m_config.use_dataspace_for_yuv = true;
}

size_t Platform_MT8195::getLimitedExternalDisplaySize()
{
    // 4k resolution
    return 3840 * 2160;
}

bool Platform_MT8195::isUILayerValid(const sp<HWCLayer>& layer, int32_t* line)
{
    return PlatformCommon::isUILayerValid(layer, line);
}


bool Platform_MT8195::isMMLayerValid(const sp<HWCLayer>& layer, const int32_t pq_mode_id,
        int32_t* line)
{
    return PlatformCommon::isMMLayerValid(layer, pq_mode_id, line);
}
