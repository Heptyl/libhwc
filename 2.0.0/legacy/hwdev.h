#ifndef HWC_HWDEV_H_
#define HWC_HWDEV_H_

#include <linux/disp_session.h>

#include "dev_interface.h"

// ---------------------------------------------------------------------------

DISP_FORMAT mapDispInFormat(unsigned int format, int mode = HWC2_BLEND_MODE_NONE);
DISP_FORMAT convertFormat4Hrt(unsigned int format);
DISP_MODE mapHwcDispMode2Disp(HWC_DISP_MODE mode);

class DispDevice : public IOverlayDevice
{
public:
    static DispDevice& getInstance();
    ~DispDevice();

    int32_t getType() { return OVL_DEVICE_TYPE_OVL; }

    // initOverlay() initializes overlay related hw setting
    void initOverlay();

    // getHwVersion() is used to get the HW version for check platform family
    static unsigned int getHwVersion();

    // isDispRszSupported() is used to query if display rsz is supported
    bool isDispRszSupported();

    // isDispRszSupported() is used to query if display rsz is supported
    bool isDispRpoSupported();

    // isDisp3X4DisplayColorTransformSupported() returns whether DISP_PQ supports
    // 3X4 color matrix or not
    bool isDisp3X4DisplayColorTransformSupported();

    // isDispAodForceDisable return display forces disable aod on hwcomposer
    bool isDispAodForceDisable();

    // isPartialUpdateSupported() is used to query if PartialUpdate is supported
    bool isPartialUpdateSupported();

    // isFenceWaitSupported() is used to query if FenceWait is supported
    bool isFenceWaitSupported();

    // isConstantAlphaForRGBASupported is used to query if hardware support constant alpha for RGBA
    bool isConstantAlphaForRGBASupported();

    // isDispSelfRefreshSupported is used to query if hardware support ioctl of self-refresh
    bool isDispSelfRefreshSupported();

    // isDisplayHrtSupport() is used to query HRT suuported or not
    bool isDisplayHrtSupport();

    // isDisplaySupportedWidthAndHeight() is used to query Width and Height is supported or not
    bool isDisplaySupportedWidthAndHeight(unsigned int width, unsigned int height);

    // getSupportedColorMode is used to check what colormode device support
    int32_t getSupportedColorMode();

    // getMaxOverlayInputNum() gets overlay supported input amount
    unsigned int getMaxOverlayInputNum();

    // getMaxOverlayHeight() gets overlay supported height amount
    uint32_t getMaxOverlayHeight();

    // getMaxOverlayWidth() gets overlay supported width amount
    uint32_t getMaxOverlayWidth();

    // getDisplayOutputRotated() get the decouple buffer is rotated or not
    int32_t getDisplayOutputRotated();

    // getRszMaxWidthInput() get the max width of rsz input
    uint32_t getRszMaxWidthInput();

    // getRszMaxHeightInput() get the max height of rsz input
    uint32_t getRszMaxHeightInput();

    // enableDisplayFeature() is used to force hwc to enable feature
    void enableDisplayFeature(uint32_t flag);

    // disableDisplayFeature() is used to force hwc to disable feature
    void disableDisplayFeature(uint32_t flag);

    // createOverlaySession() creates overlay composition session
    status_t createOverlaySession(
        uint64_t dpy, uint32_t drm_id_crtc, uint32_t width, uint32_t height,
        HWC_DISP_MODE mode = HWC_DISP_SESSION_DIRECT_LINK_MODE);

    // destroyOverlaySession() destroys overlay composition session
    void destroyOverlaySession(uint64_t dpy, uint32_t drm_id_crtc);

    // truggerOverlaySession() used to trigger overlay engine to do composition
    status_t triggerOverlaySession(uint64_t dpy, uint32_t drm_id_crtc, int present_fence_idx,
                                   int ovlp_layer_num, int prev_present_fence_fd, hwc2_config_t config,
                                   const uint32_t& hrt_weight, const uint32_t& hrt_idx,
                                   unsigned int num, OverlayPortParam* const* params,
                                   sp<ColorTransform> color_transform,
                                   TriggerOverlayParam trigger_param
                                   );

    // disableOverlaySession() usd to disable overlay session to do composition
    void disableOverlaySession(uint64_t dpy, uint32_t drm_id_crtc, OverlayPortParam* const* params, unsigned int num);

    // setOverlaySessionMode() sets the overlay session mode
    status_t setOverlaySessionMode(uint64_t dpy, HWC_DISP_MODE mode);

    // getOverlaySessionMode() gets the overlay session mode
    HWC_DISP_MODE getOverlaySessionMode(uint64_t dpy, uint32_t drm_id_crtc);

    // getOverlaySessionInfo() gets specific display device information
    status_t getOverlaySessionInfo(uint64_t dpy, uint32_t drm_id_crtc, SessionInfo* info);

    // getAvailableOverlayInput gets available amount of overlay input
    // for different session
    unsigned int getAvailableOverlayInput(uint64_t dpy, uint32_t drm_id_crtc);

    // prepareOverlayInput() gets timeline index and fence fd of overlay input layer
    void prepareOverlayInput(uint64_t dpy, OverlayPrepareParam* param);

    // updateOverlayInputs() updates multiple overlay input layers
    void updateOverlayInputs(uint64_t dpy, uint32_t drm_id_crtc,
                             OverlayPortParam* const* params, unsigned int num, sp<ColorTransform> color_transform);

    // prepareOverlayOutput() gets timeline index and fence fd for overlay output buffer
    void prepareOverlayOutput(uint64_t dpy, OverlayPrepareParam* param);

    // disableOverlayOutput() disables overlay output buffer
    void disableOverlayOutput(uint64_t dpy, uint32_t drm_id_crtc);

    // enableOverlayOutput() enables overlay output buffer
    void enableOverlayOutput(uint64_t dpy, uint32_t drm_id_crtc, OverlayPortParam* param);

    // prepareOverlayPresentFence() gets present timeline index and fence
    void prepareOverlayPresentFence(uint64_t dpy, OverlayPrepareParam* param);

    // waitVSync() is used to wait vsync signal for specific display device
    status_t waitVSync(uint64_t dpy, uint32_t drm_id_crtc, nsecs_t *ts);

    // setPowerMode() is used to switch power setting for display
    void setPowerMode(uint64_t dpy, uint32_t drm_id_crtc, int mode);

    // to query valid layers which can handled by OVL
    bool queryValidLayer(void* ptr);

    // waitAllJobDone() use to wait driver for processing all job
    status_t waitAllJobDone(const uint64_t dpy);

    // waitRefreshRequest() is used to wait for refresh request from driver
    status_t waitRefreshRequest(unsigned int* type);

    // getWidth() gets the width from the config
    int32_t getWidth(uint64_t dpy, uint32_t drm_id_connector, hwc2_config_t config);

    // getHeight() gets the height from the config
    int32_t getHeight(uint64_t dpy, uint32_t drm_id_connector, hwc2_config_t config);

    // getRefresh() gets the fps from the config
    int32_t getRefresh(uint64_t dpy, uint32_t drm_id_connector, hwc2_config_t config);

    // getNumConfigs gets the number of configs
    uint32_t getNumConfigs(uint64_t dpy, uint32_t drm_id_connector);

    // dump dev info
    void dump(const uint64_t& dpy, String8* dump_str);

    // updateDisplayResolution use to update display resolution
    int32_t updateDisplayResolution(uint64_t dpy);

    // getCurrentRefresh() gets the current mode fps
    // this is only used for external display
    int32_t getCurrentRefresh(uint64_t dpy, uint32_t drm_id_crtc);

    // MML decouple IOCtrl, after calling this ioctrl MML start to work
    void submitMML(uint64_t dpy, struct mml_submit& params);

    // Display Driver debug log IOCtrl
    void enableDisplayDriverLog(uint32_t param);

    // updateConnectorMode use for update connector's mode for display
    int32_t updateConnectorMode(uint64_t dpy, uint32_t drm_id_crtc);

    // getSpportedConnectorMode use for get connector's supported mode for changing mode
    int32_t getSpportedConnectorMode(uint64_t dpy, uint32_t drm_id_crtc, uint32_t *modeinfo);
    // getConnectorEDID use for get connector's edid
    int32_t getConnectorEDID(uint64_t dpy, uint32_t drm_id_crtc, uint32_t *edid);

    int32_t setHDMIMode(uint64_t dpy, uint32_t drm_id_crtc, uint32_t mode, uint32_t value);
    int32_t getHDMIMode(uint64_t dpy, uint32_t drm_id_crtc, uint32_t mode, uint32_t *value);
    // getEthdrSupport to check hdmi ethdr feature
    bool getEthdrSupport(uint64_t dpy);
    // getKernelLogoSize() gets the width and height from kernel logo
    int32_t getKernelLogoSize(int32_t *width, int32_t *height);
private:
    DispDevice();

    // for lagacy driver API
    status_t legacySetInputBuffer(uint64_t dpy);
    status_t legacySetOutputBuffer(uint64_t dpy);
    status_t legacyTriggerSession(uint64_t dpy, int present_fence_idx);

    // for new driver API from MT6755
    status_t frameConfig(uint64_t dpy, int present_fence_idx, int ovlp_layer_num,
                         int prev_present_fence_fd, hwc2_config_t config,
#ifdef MTK_IN_DISPLAY_FINGERPRINT
                         const uint32_t& hrt_weight, const uint32_t& hrt_idx,
                         const bool& is_HBM = false);
#else
                         const uint32_t& hrt_weight, const uint32_t& hrt_idx);
#endif

    // query hw capabilities through ioctl and store in m_caps_info
    status_t queryCapsInfo();

    // get the correct device id for extension display when enable dual display
    unsigned int getDeviceId(uint64_t dpy);

    //get hw multi configs info
    status_t getOverlayMultiConfigs(uint64_t dpy);

    // isMultiConfigsSupport() is used to query if multi configs is supported
    bool isMultiConfigsSupport();

    enum
    {
        DISP_INVALID_SESSION = static_cast<unsigned int>(-1),
    };

    int m_dev_fd;

    unsigned int m_ovl_input_num;

    disp_frame_cfg_t m_frame_cfg[DisplayManager::MAX_DISPLAYS];

    disp_session_input_config m_input_config[DisplayManager::MAX_DISPLAYS];

    disp_session_output_config m_output_config[DisplayManager::MAX_DISPLAYS];

    disp_caps_info m_caps_info;

    layer_config* m_layer_config_list[DisplayManager::MAX_DISPLAYS];

    layer_dirty_roi** m_hwdev_dirty_rect[DisplayManager::MAX_DISPLAYS];

    struct multi_configs m_multi_cfgs[DisplayManager::MAX_DISPLAYS];
};

#endif // HWC_HWDEV_H_
