#ifndef HWC_OVL_DEV_H
#define HWC_OVL_DEV_H

#include <vector>

#include <utils/String8.h>
#include <hardware/hwcomposer2.h>

#include "hwc2_defs.h"
#include "color.h"
#include "data_express.h"
#include "display.h"

#define DISP_NO_PRESENT_FENCE UINT_MAX

using namespace android;

struct OverlayPrepareParam;
struct OverlayPortParam;

// ---------------------------------------------------------------------------
#define CHECK_DPY_VALID(dpy) (dpy < DisplayManager::MAX_DISPLAYS)

#define CHECK_DPY_RET_STATUS(dpy) \
    do { \
        if (!CHECK_DPY_VALID(dpy)) { \
            LOG_FATAL("%s(), invalid dpy %" PRIu64, __FUNCTION__, dpy); \
            return BAD_VALUE; \
        } \
    } while(0)

#define CHECK_DPY_RET_VOID(dpy) \
    do { \
        if (!CHECK_DPY_VALID(dpy)) { \
            LOG_FATAL("%s(), invalid dpy %" PRIu64, __FUNCTION__, dpy); \
            return; \
        } \
    } while(0)

// MAX_DIRTY_RECT_CNT hwc supports
enum
{
    MAX_DIRTY_RECT_CNT = 10,
};

enum
{
    OVL_DEVICE_TYPE_INVALID,
    OVL_DEVICE_TYPE_OVL,
    OVL_DEVICE_TYPE_BLITDEV
};

enum
{
    HWC_WDMA_DUMP_POINT_OVL_OUT,
    HWC_WDMA_DUMP_POINT_PQ_OUT,
};

typedef enum {
    HWC_DISP_INVALID_SESSION_MODE = 0,
    HWC_DISP_SESSION_DIRECT_LINK_MODE = 1,
    HWC_DISP_SESSION_DECOUPLE_MODE = 2,
    HWC_DISP_SESSION_DIRECT_LINK_MIRROR_MODE = 3,
    HWC_DISP_SESSION_DECOUPLE_MIRROR_MODE = 4,
    HWC_DISP_SESSION_RDMA_MODE,
    HWC_DISP_SESSION_DUAL_DIRECT_LINK_MODE,
    HWC_DISP_SESSION_DUAL_DECOUPLE_MODE,
    HWC_DISP_SESSION_DUAL_RDMA_MODE,
    HWC_DISP_SESSION_TRIPLE_DIRECT_LINK_MODE,
    HWC_DISP_SESSION_MODE_NUM,
} HWC_DISP_MODE;

typedef enum {
    HWC_DISP_IF_TYPE_DBI = 0,
    HWC_DISP_IF_TYPE_DPI,
    HWC_DISP_IF_TYPE_DSI0,
    HWC_DISP_IF_TYPE_DSI1,
    HWC_DISP_IF_TYPE_DSIDUAL,
    HWC_DISP_IF_HDMI = 7,
    HWC_DISP_IF_HDMI_SMARTBOOK,
    HWC_DISP_IF_MHL,
    HWC_DISP_IF_EPD,
    HWC_DISP_IF_SLIMPORT,
} HWC_DISP_IF_TYPE;

typedef enum {
    HWC_DISP_IF_MODE_VIDEO = 0,
    HWC_DISP_IF_MODE_COMMAND,
} HWC_DISP_IF_MODE;

typedef enum {
    HWC_WAIT_FOR_REFRESH,
    HWC_REFRESH_FOR_ANTI_LATENCY2,
    HWC_REFRESH_FOR_SWITCH_DECOUPLE,
    HWC_REFRESH_FOR_SWITCH_DECOUPLE_MIRROR,
    HWC_REFRESH_FOR_IDLE,
    HWC_REFRESH_FOR_IDLE_THREAD,
    HWC_REFRESH_FOR_LOW_LATENCY_REPAINT,
    HWC_REFRESH_FOR_AI_BLULIGHT_DEFENDER,
    HWC_REFRESH_FOR_AI_BLULIGHT_DEFENDER_AGAIN,
    HWC_REFRESH_FOR_DISPLAY_DUMP,
    HWC_REFRESH_FOR_DEBUG,
    HWC_REFRESH_FOR_OTHER_HOTPLUG,
    HWC_REFRESH_TYPE_NUM,
} HWC_SELF_REFRESH_TYPE;

// About HWC_MML_OVL_LAYER to HWC_MML_DISP_NOT_SUPPORT
// 1. label MM layer as HWC_MML_OVL_LAYER to display driver at completeLayerCaps()
// 2. display driver will mark MML mode by caps
// 3. HWC accoding different caps do the different things
// 3.1 UI layer (HWC_MML_DISP_DIRECT_LINK_LAYER and HWC_MML_DISP_DIRECT_DECOUPLE_LAYER)
//      label MM layer to UI lauer
//      bring the info of layer need to handle by MML to overlay engine,
//      and set the relate para by atomic_commit
// 3.2 GPU layer (HWC_MML_DISP_NOT_SUPPORT)
//      in gled_head and gled_tail
// 3.3 MML decouple (HWC_MML_DISP_DECOUPLE_LAYER)
//      the flow is simular as MDP flow

enum HWC_LAYERING_CAPS {
    HWC_LAYERING_OVL_ONLY = 0x00000001,
    HWC_MDP_RSZ_LAYER = 0x00000002,
    HWC_DISP_RSZ_LAYER = 0x00000004,
    HWC_MDP_ROT_LAYER = 0x00000008,
    HWC_MDP_HDR_LAYER = 0x00000010,
    HWC_NO_FBDC       = 0x00000020,
    HWC_CLIENT_CLEAR_LAYER = 0x00000040,
    HWC_DISP_CLIENT_CLEAR_LAYER = 0x00000080,
    // Label this to display driver as it need to check MML mode in hrt()
    HWC_MML_OVL_LAYER = 0x00000100,
    // direct link mode, set as UI layer
    HWC_MML_DISP_DIRECT_LINK_LAYER = 0x00000200,
    // direct decouple (inline rotate) mode, set as UI layer
    HWC_MML_DISP_DIRECT_DECOUPLE_LAYER = 0x00000400,
    // decouple mode, set as MM layer, for HRT not sufficient
    HWC_MML_DISP_DECOUPLE_LAYER = 0x00000800,
    // Mask for DISP mode
    HWC_MML_DISP_MODE_MASK = 0x00000E00,
    // decouple mode, set as MM layer, for switch direct link/direct decouple to decouple mode
    HWC_MML_DISP_MDP_LAYER = 0x00001000,
    // somehow display driver can't support (MML kernel not support or HRT)
    HWC_MML_DISP_NOT_SUPPORT = 0x00002000,
    //hwc mark it, indicates the layer is unchanged for BWMonitor
    HWC_UNCHANGED_LAYER = 0x00004000,
    //hwc mark it, indicates the layer is inactive for gpu cache
    HWC_INACTIVE_LAYER = 0x00008000,
    //hwc mark it, indicates the fbt is unchanged for gpu cache
    HWC_UNCHANGED_FBT_LAYER = 0x00010000,
    //disp mark it, indicates get unchanged ratio for BWMonitor
    HWC_DISP_UNCHANGED_RATIO_VALID = 0x00020000,
    //disp mark it, indicates get unchanged FBT ratio for gpu cache
    HWC_DISP_FBT_RATIO_VALID = 0x00040000,
    //disp mark it, indicates pushed the inactive layer to GPU for gpu cache
    HWC_DISP_CLIENT_LAYER = 0x00080000,
};

enum HWC_DISP_CAPS {
    //disp mark it, indicates get unchanged FBT ratio for gpu cache
    HWC_GLES_FBT_GET_RATIO = 0x00000001,
    //hwc mark it, indicates the fbt is unchanged for gpu cache
    HWC_GLES_FBT_UNCHANGED = 0x00000002,
};

enum HWC_FEATURE {
     HWC_FEATURE_TIME_SHARING = 0x00000001,
     HWC_FEATURE_HRT = 0x00000002,
     HWC_FEATURE_PARTIAL = 0x00000004,
     HWC_FEATURE_FENCE_WAIT = 0x00000008,
     HWC_FEATURE_RSZ = 0x00000010,
     HWC_FEATURE_NO_PARGB = 0x00000020,
     HWC_FEATURE_DISP_SELF_REFRESH = 0x00000040,
     HWC_FEATURE_RPO = 0x00000080,
     HWC_FEATURE_FBDC = 0x00000100,
     HWC_FEATURE_FORCE_DISABLE_AOD = 0x00000200,
     HWC_FEATURE_MSYNC2_0 = 0x00000400,
     HWC_FEATURE_COLOR_HISTOGRAM = 0x00000800,
     HWC_FEATURE_OVL_VIRUTAL_DISPLAY = 0x00001000,
     HWC_FEATURE_BW_MONITOR_SUPPORT = 0x00002000,
     HWC_FEATURE_SMART_COMPOSITION_SUPPORT = 0x00004000,
};

enum HWC_DISP_SESSION_TYPE{
    HWC_DISP_SESSION_PRIMARY = 1,
    HWC_DISP_SESSION_EXTERNAL = 2,
    HWC_DISP_SESSION_MEMORY = 3,
    HWC_DISP_SESSION_EXTERNAL_1 = 4,
};

enum HWC_DEBUG_LOG{
    HWC_DEBUG_LOG_MOBILE_ON = 1,
    HWC_DEBUG_LOG_DETAIL_ON = 2,
    HWC_DEBUG_LOG_FENCE_ON = 3,
    HWC_DEBUG_LOG_IRQ_ON = 4
};

class SessionInfo
{
public:
    SessionInfo();
    ~SessionInfo() = default;

    unsigned int maxLayerNum;
    unsigned int isHwVsyncAvailable;
    HWC_DISP_IF_TYPE displayType;
    unsigned int displayWidth;
    unsigned int displayHeight;
    unsigned int displayFormat;
    HWC_DISP_IF_MODE displayMode;
    unsigned int vsyncFPS;
    unsigned int physicalWidth;
    unsigned int physicalHeight;
    unsigned int physicalWidthUm;
    unsigned int physicalHeightUm;
    unsigned int density;
    unsigned int isConnected;
    unsigned int isHDCPSupported;
};

struct CreateDisplayInfo
{
    bool is_internal;
    hwc2_display_type_t disp_type;
    uint32_t drm_id_crtc_default;
    uint32_t drm_id_connector;
    uint32_t drm_id_crtc_pipe;
};

//-----------------------------------------------------------------------------

enum
{
    HISTOGRAM_STATE_NO_SUPPORT = 0,
    HISTOGRAM_STATE_SUPPORT,
    HISTOGRAM_STATE_NO_RESOURCE,
    HISTOGRAM_STATE_NUM,
};

#define NUM_FORMAT_COMPONENTS 4

//-----------------------------------------------------------------------------

class IOverlayDevice: public RefBase
{
public:
    struct TriggerOverlayParam {
#ifdef MTK_IN_DISPLAY_FINGERPRINT
        bool is_HBM = false;
#endif
#ifdef MTK_HDR_SET_DISPLAY_COLOR
        int is_HDR = 0;
#endif
        uint64_t ovl_seq = 0;
        DataPackage* package = nullptr;
        DataPackage* late_package = nullptr;
    };

    virtual ~IOverlayDevice() {}

    virtual int32_t getType() = 0;

    // initOverlay() initializes overlay related hw setting
    virtual void initOverlay() = 0;

    // getHwVersion() is used to get the HW version for check platform family
    static unsigned int getHwVersion();

    // isDispRszSupported() is used to query if display rsz is supported
    virtual bool isDispRszSupported() = 0;

    // isDispRszSupported() is used to query if display rsz is supported
    virtual bool isDispRpoSupported() = 0;

    // isDisp3X4DisplayColorTransformSupported() returns whether DISP_PQ supports
    // 3X4 color matrix or not
    virtual bool isDisp3X4DisplayColorTransformSupported() = 0;

    // isDispAodForceDisable return display forces disable aod on hwcomposer
    virtual bool isDispAodForceDisable() = 0;

    // isPartialUpdateSupported() is used to query if PartialUpdate is supported
    virtual bool isPartialUpdateSupported() = 0;

    // isFenceWaitSupported() is used to query if FenceWait is supported
    virtual bool isFenceWaitSupported() = 0;

    // isConstantAlphaForRGBASupported() is used to query if PRGBA is supported
    virtual bool isConstantAlphaForRGBASupported() = 0;

    // isDispSelfRefreshSupported is used to query if hardware support ioctl of self-refresh
    virtual bool isDispSelfRefreshSupported() = 0;

    // isDisplayHrtSupport() is used to query HRT suuported or not
    virtual bool isDisplayHrtSupport() = 0;

    // isDisplaySupportedWidthAndHeight() is used to query Width and Height is supported or not
    virtual bool isDisplaySupportedWidthAndHeight(unsigned int width, unsigned int height) = 0;

    // isSupportMml() is used to query crtc support MML or not
    virtual bool isSupportMml(uint32_t /*drm_id_crtc*/) { return false; }

    // isSupportDispPq() is used to query crtc support display pq or not
    virtual bool isSupportDispPq(uint32_t /*drm_id_crtc*/) { return false; }
    // isSupportWb() is used to query crtc support WB or not
    virtual bool isSupportWb(uint32_t /*drm_id_crtc*/) { return false; }

    // isSupportWbParams() is used to query crtc support WB or not with parameters
    virtual bool isSupportWbParams(uint32_t /*drm_id_crtc*/, uint32_t /*drm_id_connector*/, int /*dump_point*/,
                                   unsigned int /*src_w*/, unsigned int /*src_h*/,
                                   unsigned int /*dst_w*/, unsigned int /*dst_h*/,
                                   hwc2_config_t /*config*/) { return false; }

    // isSupportSeparateHrt() gets overlay supported separate HRT
    virtual bool isSupportSeparateHrt() { return false; }

    // isSupportDynamicSwitch() is used to query dynamic switch support or not
    virtual bool isSupportDynamicSwitch() { return false; }

    // getMaxOverlayInputNum() gets overlay supported input amount
    virtual unsigned int getMaxOverlayInputNum() = 0;

    // getMaxOverlayHeight() gets overlay supported height amount
    virtual uint32_t getMaxOverlayHeight() = 0;

    // getMaxOverlayWidth() gets overlay supported width amount
    virtual uint32_t getMaxOverlayWidth() = 0;

    // getDisplayOutputRotated() get the decouple buffer is rotated or not
    virtual int32_t getDisplayOutputRotated() = 0;

    // getRszMaxWidthInput() get the max width of rsz input
    virtual uint32_t getRszMaxWidthInput() = 0;

    // getRszMaxHeightInput() get the max height of rsz input
    virtual uint32_t getRszMaxHeightInput() = 0;

    // enableDisplayFeature() is used to force hwc to enable feature
    virtual void enableDisplayFeature(uint32_t flag) = 0;

    // disableDisplayFeature() is used to force hwc to disable feature
    virtual void disableDisplayFeature(uint32_t flag) = 0;

    // createOverlaySession() creates overlay composition session
    virtual status_t createOverlaySession(
        uint64_t dpy, uint32_t drm_id_crtc, uint32_t width, uint32_t height,
        HWC_DISP_MODE mode = HWC_DISP_SESSION_DIRECT_LINK_MODE) = 0;

    // destroyOverlaySession() destroys overlay composition session
    virtual void destroyOverlaySession(uint64_t dpy, uint32_t drm_id_crtc) = 0;

    // truggerOverlaySession() used to trigger overlay engine to do composition
    virtual status_t triggerOverlaySession(uint64_t dpy, uint32_t drm_id_crtc, int present_fence_idx,
                                   int ovlp_layer_num, int prev_present_fence_fd, hwc2_config_t config,
                                   const uint32_t& hrt_weight, const uint32_t& hrt_idx,
                                   unsigned int num, OverlayPortParam* const* params,
                                   sp<ColorTransform> color_transform,
                                   TriggerOverlayParam trigger_param
                                   ) = 0;


    // disableOverlaySession() usd to disable overlay session to do composition
    virtual void disableOverlaySession(uint64_t dpy, uint32_t drm_id_crtc,
                                       OverlayPortParam* const* params, unsigned int num) = 0;

    // setOverlaySessionMode() sets the overlay session mode
    virtual status_t setOverlaySessionMode(uint64_t dpy, HWC_DISP_MODE mode) = 0;

    // getOverlaySessionMode() gets the overlay session mode
    virtual HWC_DISP_MODE getOverlaySessionMode(uint64_t dpy, uint32_t drm_id_crtc) = 0;

    // getOverlaySessionInfo() gets specific display device information
    virtual status_t getOverlaySessionInfo(uint64_t dpy, uint32_t drm_id_crtc, SessionInfo* info) = 0;

    // getAvailableOverlayInput gets available amount of overlay input
    // for different session
    virtual unsigned int getAvailableOverlayInput(uint64_t dpy, uint32_t drm_id_crtc) = 0;

    // prepareOverlayInput() gets timeline index and fence fd of overlay input layer
    virtual void prepareOverlayInput(uint64_t dpy, OverlayPrepareParam* param) = 0;

    // updateOverlayInputs() updates multiple overlay input layers
    virtual void updateOverlayInputs(uint64_t dpy, uint32_t drm_id_crtc,
                                     OverlayPortParam* const* params,
                                     unsigned int num,
                                     sp<ColorTransform> color_transform) = 0;

    // prepareOverlayOutput() gets timeline index and fence fd for overlay output buffer
    virtual void prepareOverlayOutput(uint64_t dpy, OverlayPrepareParam* param) = 0;

    // disableOverlayOutput() disables overlay output buffer
    virtual void disableOverlayOutput(uint64_t dpy, uint32_t drm_id_crtc) = 0;

    // enableOverlayOutput() enables overlay output buffer
    virtual void enableOverlayOutput(uint64_t dpy, uint32_t drm_id_crtc, OverlayPortParam* param) = 0;

    // prepareOverlayPresentFence() gets present timeline index and fence
    virtual void prepareOverlayPresentFence(uint64_t dpy, OverlayPrepareParam* param) = 0;

    // waitVSync() is used to wait vsync signal for specific display device
    virtual status_t waitVSync(uint64_t dpy, uint32_t drm_id_crtc, nsecs_t *ts) = 0;

    // setPowerMode() is used to switch power setting for display
    virtual void setPowerMode(uint64_t dpy, uint32_t drm_id_crtc, int mode) = 0;

    // to query valid layers which can handled by OVL
    virtual bool queryValidLayer(void* ptr) = 0;

    // waitAllJobDone() use to wait driver for processing all job
    virtual status_t waitAllJobDone(const uint64_t dpy) = 0;

    // getSupportedColorMode is used to check what colormode device support
    virtual int32_t getSupportedColorMode() = 0;

    // waitRefreshRequest() is used to wait for refresh request from driver
    virtual status_t waitRefreshRequest(unsigned int* type) = 0;

    // getWidth() gets the width from the config
    virtual int32_t getWidth(uint64_t dpy, uint32_t drm_id_connector, hwc2_config_t config) = 0;

    // getHeight() gets the height from the config
    virtual int32_t getHeight(uint64_t dpy, uint32_t drm_id_connector, hwc2_config_t config) = 0;

    // getRefresh() gets the fps from the config
    virtual int32_t getRefresh(uint64_t dpy, uint32_t drm_id_connector, hwc2_config_t config) = 0;

    // getNumConfigs gets the number of configs
    virtual uint32_t getNumConfigs(uint64_t dpy, uint32_t drm_id_connector) = 0;

    // dump dev info
    virtual void dump(const uint64_t& dpy, String8* dump_str) = 0;

    // updateDisplayResolution use to update display resolution
    virtual int32_t updateDisplayResolution(uint64_t dpy) = 0;

    // getCurrentRefresh() gets the current mode fps
    // this is only used for external display
    virtual int32_t getCurrentRefresh(uint64_t dpy, uint32_t drm_id_crtc) = 0;

    // query if HWC_FEATURE support or not
    virtual bool isHwcFeatureSupported(uint32_t /*hwc_feature_flag*/) { return false; };

    // MML decouple IOCtrl, after calling this ioctrl MML start to work
    virtual void submitMML(uint64_t dpy, struct mml_submit& params) = 0;

    // getHistogramAttribute() is used to get capability of color histogram
    virtual int32_t getHistogramAttribute(int32_t* /*color_format*/, int32_t* /*data_space*/,
            uint8_t* /*mask*/, uint32_t* /*max_bin*/) { return INVALID_OPERATION; }

    // enableHistogram() is used to start/stop histogram
    virtual int32_t enableHistogram(const bool /*enable*/, const int32_t /*format*/,
            const uint8_t /*format_mask*/, const int32_t /*dataspace*/,
            const uint32_t /*bin_count*/) { return INVALID_OPERATION; }

    // collectHistogram() is used to get the histogram of last frame
    virtual int32_t collectHistogram(uint32_t* /*fence_index*/,
            uint32_t* /*histogram_ptr*/[NUM_FORMAT_COMPONENTS]) { return INVALID_OPERATION; }

    virtual int32_t getCompressionDefine(const char* /*name*/, uint64_t* /*type*/,
            uint64_t* /*modifier*/) { return -1; };

    // Display Driver debug log IOCtrl
    virtual void enableDisplayDriverLog(uint32_t param) = 0;

    virtual const MSync2Data::ParamTable* getMSyncDefaultParamTable() { return nullptr; }

    virtual void updateDisplayConnection(uint64_t /*dpy*/, uint32_t /*connector_type*/) {}

    virtual void getCreateDisplayInfos(std::vector<CreateDisplayInfo>& /*create_display_infos*/) {}

    // updateConnectorMode use for update connector's mode for display
    virtual int32_t updateConnectorMode(uint64_t dpy, uint32_t drm_id_crtc) = 0;

    // getSpportedConnectorMode use for get connector's supported mode for changing mode
    virtual int32_t getSpportedConnectorMode(uint64_t dpy, uint32_t drm_id_crtc, uint32_t *modeinfo) = 0;
    // getConnectorEDID use for get connector's edid
    virtual int32_t getConnectorEDID(uint64_t dpy, uint32_t drm_id_crtc, uint32_t *edid) = 0;
    virtual int32_t setHDMIMode(uint64_t dpy, uint32_t drm_id_crtc, uint32_t mode, uint32_t value) = 0;
    virtual int32_t getHDMIMode(uint64_t dpy, uint32_t drm_id_crtc, uint32_t mode, uint32_t *value) = 0;
    // getEthdrSupport to check hdmi ethdr feature
    virtual bool getEthdrSupport(uint64_t dpy);
    // get Dolby info
    // for rounding standard
    // this is only used for external display
    virtual int32_t getDolbyInfo() = 0;
    // getKernelLogoSize() gets the width and height from kernel logo
    virtual int32_t getKernelLogoSize(int32_t *width, int32_t *height) = 0;

#ifdef MTK_DRM_PIXEL_SHIFT
    virtual int32_t setPixelShift(struct mtk_pixel_shift_user_config *cfg);
#endif
};

IOverlayDevice* getHwDevice();

#endif
