#ifndef HWC_PLATFORM_COMMON_H_
#define HWC_PLATFORM_COMMON_H_

#include "hwc2.h"

#include <chrono>
#include <cutils/properties.h>

using namespace android;

// ---------------------------------------------------------------------------

//copy from enum mtk_mmsys_id <kernel-headers/linux/mediatek_drm.h>
enum PLATFORM_INFO {
    PLATFORM_NOT_DEFINE = 0,

    PLATFORM_MT8127 = 0x8127,
    PLATFORM_MT8135 = 0x8135,
    PLATFORM_MT8163 = 0x8163,
    PLATFORM_MT8167 = 0x8167,
    PLATFORM_MT8173 = 0x8173,
    PLATFORM_MT8168 = 0x8168,
    PLATFORM_MT8188 = 0x8188,
    PLATFORM_MT8195 = 0x8195,

    PLATFORM_MT6735 = 0x6735,
    PLATFORM_MT6739 = 0x6739,
    PLATFORM_MT6755 = 0x6755,
    PLATFORM_MT6757 = 0x6757,
    PLATFORM_MT6763 = 0x6763,
    PLATFORM_MT6771 = 0x6771,
    PLATFORM_MT6775 = 0x6775,
    PLATFORM_MT6765 = 0x6765,
    PLATFORM_MT6761 = 0x6761,
    PLATFORM_MT6797 = 0x6797,
    PLATFORM_MT6799 = 0x6799,
    PLATFORM_MT6758 = 0x6758,
    PLATFORM_MT3967 = 0x3967,
    PLATFORM_MT6580 = 0x6580,
    PLATFORM_MT6779 = 0x6779,
    PLATFORM_MT6885 = 0x6885,
    PLATFORM_MT6768 = 0x6768,
    PLATFORM_MT6785 = 0x6785,
    PLATFORM_MT6873 = 0x6873,
    PLATFORM_MT6893 = 0x6893,
    PLATFORM_MT6853 = 0x6853,
    PLATFORM_MT6983 = 0x6983,
    PLATFORM_MT6879 = 0x6879,
    PLATFORM_MT6895 = 0x6895,
    PLATFORM_MT6855 = 0x6855,
    PLATFORM_MT6985 = 0x6985,
    PLATFORM_MT6886 = 0x6886,
};

enum HWC_MIRROR_STATE {
    MIRROR_UNDEFINED = 0, // reserved to avoid using this state by accident

    MIRROR_ENABLED   = (1 << 0),
    MIRROR_PAUSED    = (1 << 1),
    MIRROR_DISABLED  = (1 << 2),
};

enum HWC_MIR_FORMAT {
    MIR_FORMAT_UNDEFINE = 0,
    MIR_FORMAT_RGB888   = 1,
    MIR_FORMAT_YUYV     = 2,
    MIR_FORMAT_YV12     = 3,
};

enum MTK_LCM_DEGREE {
    MTK_LCM_DEGREE_0   = 0,
    MTK_LCM_DEGREE_90  = 90,
    MTK_LCM_DEGREE_180 = 180,
    MTK_LCM_DEGREE_270 = 270,
};

enum HWC_AV_GROUPING_TYPE {
    MTK_AV_GROUPING_NONE = 0x00,
    MTK_AV_GROUPING_SECURE_VP = 0x01,
    MTK_AV_GROUPING_NORMAL_VP = 0x02,
    MTK_AV_GROUPING_CAMERA = 0x04,
    MTK_AV_GROUPING_ALL_VP = MTK_AV_GROUPING_SECURE_VP | MTK_AV_GROUPING_NORMAL_VP,
};

enum HWC_PLAT_SWITCH {
    HWC_PLAT_SWITCH_MULTIPLE_FB_CACHE = 1 << 0,
    HWC_PLAT_SWITCH_BLIT_CLR_BG_NEED_SECURE = 1 << 1, // TODO: remove @ Android T, if all platform not needed
    HWC_PLAT_SWITCH_DEBUG_HISTOGRAM = 1 << 2,
    HWC_PLAT_SWITCH_NO_BLACK_JOB_FOR_OVL_DEV = 1 << 3,
    HWC_PLAT_SWITCH_NOTIFY_HWBINDER_TID = 1 << 4,
    HWC_PLAT_SWITCH_USE_PERF = 1 << 5,
    HWC_PLAT_DBG_PRESENT_FENCE = 1 << 6,
    HWC_PLAT_SWITCH_VIRTUAL_DISPLAY_MDP_ASAP = 1 << 7,
    HWC_PLAT_SWITCH_ALWAYS_ON_MIDDLE_CORE = 1 << 8,
    HWC_PLAT_SWITCH_VP_ON_MIDDLE_CORE = 1 << 9,
    HWC_PLAT_SWITCH_EXTEND_SF_TARGET_TS_FOR_VIDEO = 1 << 10,
    HWC_PLAT_SWITCH_EXTEND_SF_TARGET_TS_FOR_CAMERA = 1 << 11,
    HWC_PLAT_SWITCH_OVERWRITE_SWITCH_CONFIG = 1 << 12,
    HWC_PLAT_SWITCH_NO_DISPATCH_THREAD = 1 << 13,
    // 1. please reserve bit usage here: https://wiki.mediatek.inc/x/QZfXOg
    // 2. vendor should not add in this enum group
};

enum HWC_DBG_SWITCH {
    HWC_DBG_SWITCH_DEBUG_SEMAPHORE = 1 << 0,
    HWC_DBG_SWITCH_DEBUG_LAYER_METADATA = 1 << 1,
    HWC_DBG_SWITCH_DEBUG_MM_BUFFER_INFO = 1 << 2,
    HWC_DBG_SWITCH_DEBUG_DISP_ID_INFO = 1 << 3,
    HWC_DBG_SWITCH_DEBUG_LAYER_DATASPACE = 1 << 4,
    // 1. please reserve bit usage here: https://wiki.mediatek.inc/x/RpfXOg
    // 2. vendor should not add in this enum group
};

// use to generate the property for each HWC_PLAT_SWITCH item
// e.g. HWC_PLAT_SWITCH_MULTIPLE_FB_CACHE : vendor.mtk.hwc.HWC_PLAT_SWITCH_MULTIPLE_FB_CACHE
#define GENERATE_PLAT_SWITCH_PROP(plat_switch) {plat_switch, "vendor.mtk.hwc."#plat_switch}

// An abstract class of Platform. Each function of Platform must have a condidate in
// PlatformCommon to avoid compilation error except pure virtual functions.
class PlatformCommon
{
public:
    PlatformCommon() { };
    virtual ~PlatformCommon() { };

    // initOverlay() is used to init overlay related setting
    void initOverlay();

    // if ui layer could be handled by hwcomposer
    virtual bool isUILayerValid(const sp<HWCLayer>& layer, int32_t* line);

    // isMMLayerValid() is used to verify
    // if mm layer could be handled by hwcomposer
    virtual bool isMMLayerValid(const sp<HWCLayer>& layer, const int32_t pq_mode_id, int32_t* line);

    // if ui layer could be handled by hwcomposer
    virtual bool isGlaiLayerValid(const sp<HWCLayer>& layer, int32_t* line);

    // getUltraVideoSize() is used to return the limitation of video resolution
    // when this device connect with the maximum resolution of external display
    size_t getLimitedVideoSize();

    // getUltraDisplaySize() is used to return the limitation of external display
    // when this device play maximum resolution of video
    size_t getLimitedExternalDisplaySize();

    struct PlatformConfig
    {
        PlatformConfig();

        // platform define related hw family, includes ovl and mdp engine
        int platform;

        // compose_level defines default compose level
        int compose_level;

        // mirror_state defines mirror enhancement state
        int mirror_state;

        // mir_scale_ratio defines the maxinum scale ratio of mirror source
        float mir_scale_ratio;

        // format_mir_mhl defines which color format
        // should be used as mirror result for MHL
        int format_mir_mhl;

        // can UI process prexform buffer
        int prexformUI;

        // can rdma support roi update
        int rdma_roi_update;

        // force full invalidate for partial update debug through setprop
        bool force_full_invalidate;

        // use async bliter ultra
        bool use_async_bliter_ultra;

        // force hwc to wait fence for display
        bool wait_fence_for_display;

        // Smart layer switch
        bool enable_smart_layer;

        // enable rgba rotate
        bool enable_rgba_rotate;

        // enable rgbx scaling
        bool enable_rgbx_scaling;

        // enable av grouping
        bool av_grouping;
        uint32_t grouping_type;

        // dump input buffers of hwc2
        char dump_buf_type;
        int32_t dump_buf;
        char dump_buf_cont_type;
        int32_t dump_buf_cont;
        bool dump_buf_log_enable;

        // debug flag for filleBalck function, this flag will fill white content
        // into outputbuffer if it no need to fill black
        bool fill_black_debug;

        bool always_setup_priv_hnd;

        bool wdt_trace;

        // If ture, only WiFi-display is composed by HWC, and
        // other virtual displays such as CTS and screenrecord are composed by GPU
        bool only_wfd_by_hwc;

        // If ture, only WiFi-display is converted by OVL, and
        // other virtual displays such as screenrecord are converted by MDP
        bool only_wfd_by_dispdev;

        // If there is only one ovl hw, virtual displays can composed by GPU, and
        // the output format is converted to YV12 by BlitDevice
        bool blitdev_for_virtual;

        // to indicate how HWC process the virtual display when only one ovl hw.
        bool is_support_ext_path_for_virtual;

        // If true, the flow will change from validate -> present to
        // 1. If skip, present
        // 2. If no skip, present -> validate -> present
        bool is_skip_validate;

        bool support_color_transform;

        double mdp_scale_percentage;

        bool extend_mdp_capacity;

        int32_t rpo_ui_max_src_width;

        bool disp_support_decompress;

        bool mdp_support_decompress;
        bool mdp_support_compress;

        bool remove_invisible_layers;

        // Both disp and bliter use dataspace to map color_range instead of using
        // gralloc_extra
        bool use_dataspace_for_yuv;

        bool fill_hwdec_hdr;
        bool is_support_mdp_pmqos;
        bool is_support_mdp_pmqos_debug;

        // force layer into PQ flow by order index
        int32_t force_pq_index;

        bool is_mdp_support_RGBA1010102;

        // TODO: query ovl device
        bool is_ovl_support_RGBA1010102;

        // ovl support 2-subsampled format with odd size of roi
        bool support_2subsample_with_odd_size_roi;

        // enable mm buffer dump
        bool enable_mm_buffer_dump;

        // dump specific ovl buffer to mm buffer dump
        // 0x05(0000 0101b) means that ovl_0 and ovl_2
        // if the value is 0, it means dump all overlay
        uint32_t dump_ovl_bits;

        // set 1 to always blit, don't care dirty and skip_invalidate
        bool dbg_mdp_always_blit;
        // slow motion in HWC, SF side will be removed @ Android S
        // delay amount of time for every display after present
        std::chrono::microseconds dbg_present_delay_time;

        // The layer during gles range with secure and opaque attributes
        // can be processed by the device, and its area will be cleared in the client buffer.
        bool is_client_clear_support;

        // skip hrt in validate when transaction not change
        bool is_skip_hrt;

        // The layer with hint_id or hint_name will use hint_hwlayer_type as
        // its composition type as much as possible (only enable when hint_id > 0 or hint_name not empty)
        uint64_t hint_id;
        std::string hint_name;
        // We match layer name via string::compare with pos=hint_name_shift, len=hint_name.size()
        size_t hint_name_shift;
        // If hint_hwlayer_type is HWC_LAYER_TYPE_IGNORE,
        // the layer with hint_id will not participate ovelay process.
        int32_t hint_hwlayer_type;

        // cache the private hnd of CT for performance
        bool cache_CT_private_hnd;

        // for dynamic ovl switch
        bool dynamic_switch_path;

        // add some toleranc time to refresh of display which is to detemine
        // whether to drop job when trigger by vsync is enable for display
        nsecs_t tolerance_time_to_refresh;

        // for judge legacy chip which cannot support odd width/height
        bool is_ovl_support_odd_size;

        // specify MDP output format for debug
        uint32_t force_mdp_output_format;

        // If true, HWC will check whether display feature flag contains 3x4 display
        // color transform or not.
        bool check_skip_client_color_transform;

        // store multiple swtich option
        unsigned int plat_switch;

        // store multiple debug swtich option
        unsigned int dbg_switch;

        // HWC to control mml on/off
        bool mml_switch;

        // for external HDR metadata feature support
        bool ethdr_meta_support;

        // HWC support virtual display number
        uint32_t vir_disp_sup_num;

        // the default bin number of color histogram
        uint32_t histogram_bin_number;

        std::list<UClampCpuTable> uclamp_cpu_table; // in ascending order

        std::list<HwcMCycleInfo> hwc_mcycle_table;

        uint32_t perf_prefer_below_cpu_mhz;
        nsecs_t perf_reserve_time_for_wait_fence;

        // this is for BWMonitor feature option
        bool is_bw_monitor_support;

        // this is for SmartComposition feature option
        bool is_smart_composition_support;

        int inactive_set_expired_cnt;
        nsecs_t inactive_set_expired_duration;
        bool bwm_skip_hrt_calc;

        // for hyfbc primary display force option
        int32_t primary_force_pat;

        // for hyfbc ext display force option
        int32_t ext_force_pat;

        struct CpuSetIndex
        {
            uint32_t little;
            uint32_t middle;
            uint32_t big;
        };
        CpuSetIndex cpu_set_index;
    };
    PlatformConfig m_config;

    // overwrite the HWC_PLAT_SWITCH with property
    void updateConfigFromProperty();

private:
    static bool queryHWSupport(bool is_mml_supported,
                                uint32_t srcWidth,
                                uint32_t srcHeight,
                                uint32_t dstWidth,
                                uint32_t dstHeight,
                                int32_t Orientation = 0,
                                DpColorFormat srcFormat = DP_COLOR_UNKNOWN,
                                DpColorFormat dstFormat = DP_COLOR_UNKNOWN,
                                DpPqParam *PqParam = 0,
                                DpRect *srcCrop = 0,
                                uint32_t compress = 0,
                                HWCLayer* layer = NULL);

    struct PaltSwitchProp {
        uint32_t switch_item;
        char property[PROPERTY_VALUE_MAX];
    };

    // the properties of mapping table for HWC_PLAT_SWITCH
    PaltSwitchProp m_plat_switch_list[4] = {
        GENERATE_PLAT_SWITCH_PROP(HWC_PLAT_SWITCH_ALWAYS_ON_MIDDLE_CORE),
        GENERATE_PLAT_SWITCH_PROP(HWC_PLAT_SWITCH_VP_ON_MIDDLE_CORE),
        GENERATE_PLAT_SWITCH_PROP(HWC_PLAT_SWITCH_EXTEND_SF_TARGET_TS_FOR_VIDEO),
        GENERATE_PLAT_SWITCH_PROP(HWC_PLAT_SWITCH_EXTEND_SF_TARGET_TS_FOR_CAMERA),
    };
};

#endif // HWC_PLATFORM_H_
