#define DEBUG_LOG_TAG "PLAT"

#include <hardware/hwcomposer.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "utils/debug.h"
#include "utils/tools.h"

#include "platform6985.h"
#include "DpBlitStream.h"

#include "hwc2.h"

// ---------------------------------------------------------------------------
Platform_MT6985::Platform_MT6985()
{
    m_config.platform = PLATFORM_MT6985;

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
    m_config.mdp_support_compress = true;

    m_config.is_support_mdp_pmqos = true;

    m_config.is_ovl_support_RGBA1010102 = true;
    m_config.is_mdp_support_RGBA1010102 = true;

    m_config.is_client_clear_support = true;

    m_config.plat_switch |= HWC_PLAT_SWITCH_MULTIPLE_FB_CACHE;
    m_config.plat_switch |= HWC_PLAT_SWITCH_NO_BLACK_JOB_FOR_OVL_DEV;
    m_config.plat_switch |= HWC_PLAT_SWITCH_NOTIFY_HWBINDER_TID;
    m_config.plat_switch |= HWC_PLAT_SWITCH_VIRTUAL_DISPLAY_MDP_ASAP;

    m_config.plat_switch |= HWC_PLAT_SWITCH_USE_PERF;

    m_config.plat_switch |= HWC_PLAT_SWITCH_OVERWRITE_SWITCH_CONFIG;

    m_config.plat_switch |= HWC_PLAT_SWITCH_NO_DISPATCH_THREAD;

    m_config.use_dataspace_for_yuv = true;
    m_config.dbg_switch |= HWC_DBG_SWITCH_DEBUG_LAYER_DATASPACE;

    m_config.uclamp_cpu_table.assign({{ .uclamp = 30, .cpu_mhz = 200, },
                                      { .uclamp = 40, .cpu_mhz = 300, },
                                      { .uclamp = 50, .cpu_mhz = 400, },
                                      { .uclamp = 60, .cpu_mhz = 500, },
                                      { .uclamp = 70, .cpu_mhz = 550, },
                                      { .uclamp = 80, .cpu_mhz = 650, },
                                      { .uclamp = 90, .cpu_mhz = 800, },
                                      { .uclamp = 100, .cpu_mhz = 850, },
                                      { .uclamp = 110, .cpu_mhz = 950, },
                                      { .uclamp = 120, .cpu_mhz = 1100, },
                                      { .uclamp = 130, .cpu_mhz = 1250, },
                                      { .uclamp = 140, .cpu_mhz = 1450, },
                                      { .uclamp = 150, .cpu_mhz = 1600, },
                                      { .uclamp = 160, .cpu_mhz = 1800, },});

    m_config.hwc_mcycle_table.assign({{ .id = HWC_MC_NONE, .main_mc = 1.098501f, .dispatcher_mc = 0.281605f, .ovl_mc = 1.12789f,
                                        .ovl_mc_atomic_ratio = .8f, .dispatcher_target_work_time = 2500000, .ovl_wo_atomic_work_time = -1},
                                      { .id = HWC_MC_1U, .main_mc = 1.098501f, .dispatcher_mc = 0.281605f, .ovl_mc = 1.271f,
                                        .ovl_mc_atomic_ratio = .8f, .dispatcher_target_work_time = 2500000, .ovl_wo_atomic_work_time = -1},
                                      { .id = HWC_MC_2U, .main_mc = 1.195f, .dispatcher_mc = 0.2954f, .ovl_mc = 1.217f,
                                        .ovl_mc_atomic_ratio = .8f, .dispatcher_target_work_time = 2500000, .ovl_wo_atomic_work_time = -1},
                                      { .id = HWC_MC_3U, .main_mc = 1.349024893f, .dispatcher_mc = 0.3296f, .ovl_mc = 1.488f,
                                        .ovl_mc_atomic_ratio = .8f, .dispatcher_target_work_time = 2500000, .ovl_wo_atomic_work_time = -1},
                                      { .id = HWC_MC_4U, .main_mc = 1.417f, .dispatcher_mc = 0.2986f, .ovl_mc = 1.545f,
                                        .ovl_mc_atomic_ratio = .8f, .dispatcher_target_work_time = 2500000, .ovl_wo_atomic_work_time = -1},
                                      { .id = HWC_MC_5U, .main_mc = 1.522f, .dispatcher_mc = 0.3224f, .ovl_mc = 1.6888f,
                                        .ovl_mc_atomic_ratio = .8f, .dispatcher_target_work_time = 2500000, .ovl_wo_atomic_work_time = -1},
                                      { .id = HWC_MC_6U, .main_mc = 1.6742f, .dispatcher_mc = 0.319412f, .ovl_mc = 1.82216f,
                                        .ovl_mc_atomic_ratio = .8f, .dispatcher_target_work_time = 2500000, .ovl_wo_atomic_work_time = -1},
                                      { .id = HWC_MC_0U_1M, .main_mc = 3.7474f, .dispatcher_mc = 0.47835f, .ovl_mc = 2.277f,
                                        .ovl_mc_atomic_ratio = .83f, .dispatcher_target_work_time = 2500000, .ovl_wo_atomic_work_time = -1},
                                      { .id = HWC_MC_1U_1M, .main_mc = 3.7474f, .dispatcher_mc = 0.47835f, .ovl_mc = 2.277f,
                                        .ovl_mc_atomic_ratio = .83f, .dispatcher_target_work_time = 2500000, .ovl_wo_atomic_work_time = -1},
                                      { .id = HWC_MC_2U_1M, .main_mc = 3.954f, .dispatcher_mc = 0.47835f, .ovl_mc = 2.4605f,
                                        .ovl_mc_atomic_ratio = .83f, .dispatcher_target_work_time = 2500000, .ovl_wo_atomic_work_time = -1},
                                      { .id = HWC_MC_3U_1M, .main_mc = 4.16244f, .dispatcher_mc = 0.47835f, .ovl_mc = 2.64434f,
                                        .ovl_mc_atomic_ratio = .83f, .dispatcher_target_work_time = 2500000, .ovl_wo_atomic_work_time = -1},
                                      { .id = HWC_MC_4U_1M, .main_mc = 4.27922f, .dispatcher_mc = 0.47835f, .ovl_mc = 2.78f,
                                        .ovl_mc_atomic_ratio = .83f, .dispatcher_target_work_time = 2500000, .ovl_wo_atomic_work_time = -1},
                                      { .id = HWC_MC_5U_1M, .main_mc = 4.396f, .dispatcher_mc = 0.47835f, .ovl_mc = 2.8966f,
                                        .ovl_mc_atomic_ratio = .83f, .dispatcher_target_work_time = 2500000, .ovl_wo_atomic_work_time = -1}});

    m_config.perf_prefer_below_cpu_mhz = 0;
    m_config.perf_reserve_time_for_wait_fence = us2ns(100);

    if (!isUserLoad())
    {
        m_config.dbg_switch |= HWC_DBG_SWITCH_DEBUG_SEMAPHORE;
    }

    m_config.cpu_set_index.little = 0x000f;
    m_config.cpu_set_index.middle= 0x0070;
    m_config.cpu_set_index.big = 0x0080;

    if (getHwDevice()->isHwcFeatureSupported(HWC_FEATURE_BW_MONITOR_SUPPORT))
    {
        m_config.is_bw_monitor_support = true;
    }

    if (getHwDevice()->isHwcFeatureSupported(HWC_FEATURE_SMART_COMPOSITION_SUPPORT))
    {
        m_config.is_smart_composition_support = true;
    }
}

size_t Platform_MT6985::getLimitedExternalDisplaySize()
{
    // 4k resolution
    return 3840 * 2160;
}

bool Platform_MT6985::isUILayerValid(const sp<HWCLayer>& layer, int32_t* line)
{
    return PlatformCommon::isUILayerValid(layer, line);
}


bool Platform_MT6985::isMMLayerValid(const sp<HWCLayer>& layer, const int32_t pq_mode_id,
        int32_t* line)
{
    return PlatformCommon::isMMLayerValid(layer, pq_mode_id, line);
}
