#ifndef HWC_MEDIATOR_H
#define HWC_MEDIATOR_H
#include <vector>
#include <atomic>
#include <sstream>
#include <mutex>

#include <utils/RefBase.h>

#include <android/hardware/graphics/composer/2.3/IComposerClient.h>
#include <aidl/android/hardware/graphics/common/DisplayDecorationSupport.h>
#include <aidl/android/hardware/graphics/common/Transform.h>

#include <composer_ext_intf/device_interface.h>

#include "hwc2_defs.h"
#include "color.h"
#include "hwc2_api.h"
#include "display.h"
#include "utils/tools.h"
#include "utils/fpscounter.h"
#include "hwcbuffer.h"
#include "hwclayer.h"
#include "hwcdisplay.h"
#include "hrt_common.h"

using PerFrameMetadataKey =
    android::hardware::graphics::composer::V2_3::IComposerClient::PerFrameMetadataKey;

using namespace aidl::android::hardware::graphics;

class HWCDisplay;
class IOverlayDevice;

class DisplayListener : public DisplayManager::EventListener
{
public:
    DisplayListener(
        const HWC2_PFN_HOTPLUG callback_hotplug,
        const hwc2_callback_data_t callback_hotplug_data,
        const HWC2_PFN_VSYNC callback_vsync,
        const hwc2_callback_data_t callback_vsync_data,
        const HWC2_PFN_REFRESH callback_refresh,
        const hwc2_callback_data_t callback_refresh_data,
        const HWC2_PFN_VSYNC_2_4 callback_vsync_2_4,
        const hwc2_callback_data_t callback_vsync_2_4_data,
        const HWC2_PFN_VSYNC_PERIOD_TIMING_CHANGED callback_vsync_timing_changed,
        const hwc2_callback_data_t callback_vsync_timing_changed_data,
        const HWC2_PFN_SEAMLESS_POSSIBLE callback_seamless_possible,
        const hwc2_callback_data_t callback_seamless_possbile_data);

        HWC2_PFN_HOTPLUG m_callback_hotplug;
        hwc2_callback_data_t m_callback_hotplug_data;

    virtual void setCallbackVsync(
        HWC2_PFN_VSYNC callback_vsync,
        hwc2_callback_data_t callback_vsync_data);

    virtual void setCallbackRefresh(
        HWC2_PFN_REFRESH callback_refresh,
        hwc2_callback_data_t callback_refresh_data);

    virtual void setCallbackVsync_2_4(
        HWC2_PFN_VSYNC_2_4 callback_vsync_2_4,
        hwc2_callback_data_t callback_vsync_2_4_data);

    virtual void setCallbackVsyncPeriodTimingChanged(
        HWC2_PFN_VSYNC_PERIOD_TIMING_CHANGED callback_vsync_timing_changed,
        hwc2_callback_data_t callback_vsync_timing_changed_data);

    virtual void setCallbackSeamlessPossible(
        HWC2_PFN_SEAMLESS_POSSIBLE callback_seamless_possible,
        hwc2_callback_data_t callback_seamless_possbile_data);

private:
    std::mutex m_vsync_lock;
    HWC2_PFN_VSYNC       m_callback_vsync;
    hwc2_callback_data_t m_callback_vsync_data;
    HWC2_PFN_VSYNC_2_4   m_callback_vsync_2_4;
    hwc2_callback_data_t m_callback_vsync_2_4_data;

    std::mutex m_refresh_lock;
    HWC2_PFN_REFRESH m_callback_refresh;
    hwc2_callback_data_t m_callback_refresh_data;

    std::mutex m_vsync_timing_changed_lock;
    HWC2_PFN_VSYNC_PERIOD_TIMING_CHANGED m_callback_vsync_timing_changed;
    hwc2_callback_data_t m_callback_vsync_timing_changed_data;

    std::mutex m_seamless_possible_lock;
    HWC2_PFN_SEAMLESS_POSSIBLE m_callback_seamless_possible;
    hwc2_callback_data_t m_callback_seamless_possible_data;

    virtual void onVSync(uint64_t dpy, nsecs_t timestamp, bool enabled);

    virtual void onPlugIn(uint64_t dpy, bool is_internal, uint32_t width, uint32_t height);

    virtual void onPlugOut(uint64_t dpy);

    virtual void onHotPlugExt(uint64_t dpy, int connected);

    virtual void onRefresh(uint64_t dpy);

    virtual void onRefresh(uint64_t dpy, unsigned int type);

    virtual void onVSyncPeriodTimingChange(uint64_t dpy, int64_t applied_time,
            uint8_t refresh_required, int64_t refresh_time);

    virtual void onSeamlessPossible(uint64_t dpy);
};

class HWCMediator : public HWC2Api, public ComposerExt::ClientContext
{
public:
    static HWCMediator& getInstance();
    ~HWCMediator();

    sp<HWCDisplay> getHWCDisplay(const hwc2_display_t& disp_id)
    {
        return disp_id < m_displays.size() ?
               m_displays[static_cast<std::vector<sp<HWCDisplay>>::size_type>(disp_id)] : nullptr;
    }

    void initHWCDisplay();

    sp<IOverlayDevice> getOvlDevice(const uint64_t& dpy)
    {
        return dpy < m_disp_devs.size() ?
               m_disp_devs[static_cast<std::vector<sp<IOverlayDevice>>::size_type>(dpy)] : nullptr;
    }

/*-------------------------------------------------------------------------*/
/* WFD Low Latency */
    bool getLowLatencyWFD() const { return m_low_latency_wfd; }
    void setLowLatencyWFD(const bool &val) { m_low_latency_wfd = val; }
/*-------------------------------------------------------------------------*/
/* query/set Buffer compression type */
    uint64_t getCompressionType() { return m_compression_type; }
    uint64_t getCompressionModifier() { return m_compression_modifier; };
    void setCompressionModifier(uint64_t comp_modifier) { m_compression_modifier = comp_modifier; };
    int updateCompressionType(buffer_handle_t handle);
/*-------------------------------------------------------------------------*/
/* composer ext interface*/
    int setScenarioHint(ComposerExt::ScenarioHint flag, ComposerExt::ScenarioHint mask);
/*-------------------------------------------------------------------------*/
    int32_t getPesentIdxAction() const { return m_present_idx_action; }
/*-------------------------------------------------------------------------*/
/* separate validate */
    bool isValidateSeparate() const { return m_is_validate_separate; }
/*-------------------------------------------------------------------------*/

private:
    HWCMediator();

    void setJobVideoTimeStamp();

    void updatePlatformConfig(bool is_init);
/*-------------------------------------------------------------------------*/
/* Skip Validate */
    bool checkSkipValidate(const bool& is_validate_call);

    void buildVisibleAndInvisibleLayerForAllDisplay();

    void prepareSfLog();
    void prepareForValidation();

    void validate();
    void calculateMdpDstRoi();

    void countdowmSkipValiRelatedNumber();

    void setValiPresentStateOfAllDisplay(const HWC_VALI_PRESENT_STATE& val, const int32_t& line);

    SKIP_VALI_STATE getNeedValidate() const { return m_need_validate; }
    void setNeedValidate(SKIP_VALI_STATE val) { m_need_validate = val; }

    void updateActiveConfigWithConstraintsForAllDisplay();
/*-------------------------------------------------------------------------*/

    std::vector<sp<HWCDisplay> > m_displays;

    // TODO: m_need_validate should only be used when single validate,
    //       it can be removed after single validate is phase out.
    SKIP_VALI_STATE m_need_validate;

    std::vector<sp<IOverlayDevice> > m_disp_devs;

    std::vector<int32_t> m_capabilities;

    bool m_is_mtk_aosp;

/*-------------------------------------------------------------------------*/
/* WFD Low Latency */
    bool m_low_latency_wfd;
/*-------------------------------------------------------------------------*/
/* Save fourcc compression type and modifier*/
    uint64_t m_compression_type;
    uint64_t m_compression_modifier;

   void notifyHwbinderTid();
/*-------------------------------------------------------------------------*/
/* separate validate */
   bool m_is_validate_separate;
/*-------------------------------------------------------------------------*/

public:
    bool supportApiFunction(MTK_HWC_API api);

    void open(/*hwc_private_device_t* device*/);

    void close(/*hwc_private_device_t* device*/);

    void getCapabilities(
        uint32_t* out_count,
        int32_t* /*hwc2_capability_t*/ out_capabilities);

    bool hasCapabilities(int32_t capabilities);

    void createExternalDisplay(uint64_t dpy);
    void destroyExternalDisplay(uint64_t dpy);

    int32_t setHwcVersion(
        uint32_t major,
        uint32_t minor,
        uint32_t patch);

    void getHwcVersion(
        uint32_t* major,
        uint32_t* minor,
        uint32_t* patch);

    /* Device functions */
    int32_t /*hwc2_error_t*/ deviceCreateVirtualDisplay(
        hwc2_device_t* device,
        uint32_t width,
        uint32_t height,
        int32_t* /*android_pixel_format_t*/ format,
        hwc2_display_t* outDisplay);

    int32_t /*hwc2_error_t*/ deviceDestroyVirtualDisplay(
        hwc2_device_t* device,
        hwc2_display_t display);

    void deviceDump(hwc2_device_t* device, uint32_t* outSize, char* outBuffer);

    uint32_t deviceGetMaxVirtualDisplayCount(hwc2_device_t* device);

    int32_t /*hwc2_error_t*/ deviceRegisterCallback(
        hwc2_device_t* device,
        int32_t /*hwc2_callback_descriptor_t*/ descriptor,
        hwc2_callback_data_t callbackData,
        hwc2_function_pointer_t pointer);

    int32_t /*hwc2_error_t*/ deviceGetDataspaceSaturationMatrix(
         hwc2_device_t* device,
         int32_t /*android_dataspace_t*/ dataspace,
         float* out_matrix);

    /* Display functions */
    int32_t /*hwc2_error_t*/ displayAcceptChanges(
        hwc2_device_t* device,
        hwc2_display_t display);

    int32_t /*hwc2_error_t*/ displayCreateLayer(
        hwc2_device_t* device,
        hwc2_display_t disply,
        hwc2_layer_t* outLayer);

    int32_t /*hwc2_error_t*/ displayDestroyLayer(
        hwc2_device_t* device,
        hwc2_display_t display,
        hwc2_layer_t layer);

    int32_t /*hwc2_error_t*/ displayGetActiveConfig(
        hwc2_device_t* device,
        hwc2_display_t display,
        hwc2_config_t* out_config);

    int32_t /*hwc2_error_t*/ displayGetBrightnessSupport(
        hwc2_device_t* device,
        hwc2_display_t display,
        bool* outSupport);

    int32_t /*hwc2_error_t*/ displayGetChangedCompositionTypes(
        hwc2_device_t* device,
        hwc2_display_t display,
        uint32_t* out_num_elem,
        hwc2_layer_t* out_layers,
        int32_t* /*hwc2_composition_t*/ out_types);

    int32_t /*hwc2_error_t*/ displayGetClientTargetSupport(
        hwc2_device_t* device,
        hwc2_display_t display,
        uint32_t width,
        uint32_t height,
        int32_t /*android_pixel_format_t*/ format,
        int32_t /*android_dataspace_t*/ dataspace);

    int32_t /*hwc2_error_t*/ displayGetColorMode(
        hwc2_device_t* device,
        hwc2_display_t display,
        uint32_t* out_num_modes,
        int32_t* out_modes);

    int32_t /*hwc2_error_t*/ displayGetAttribute(
        hwc2_device_t* device,
        hwc2_display_t display,
        hwc2_config_t config,
        int32_t /*hwc2_attribute_t*/ attribute,
        int32_t* out_value);

    int32_t /*hwc2_error_t*/ displayGetConfigs(
        hwc2_device_t* device,
        hwc2_display_t display,
        uint32_t* out_num_configs,
        hwc2_config_t* out_configs);

    int32_t /*hwc2_error_t*/ displayGetCapabilities(
        hwc2_device_t* device,
        hwc2_display_t display,
        uint32_t* outNumCapabilities,
        uint32_t* outCapabilities);

    int32_t /*hwc2_error_t*/ displayGetName(
        hwc2_device_t* device,
        hwc2_display_t display,
        uint32_t* out_lens,
        char* out_name);

    int32_t /*hwc2_error_t*/ displayGetRequests(
        hwc2_device_t* device,
        hwc2_display_t display,
        int32_t* /*hwc2_display_request_t*/ out_display_requests,
        uint32_t* out_num_elem,
        hwc2_layer_t* out_layer,
        int32_t* /*hwc2_layer_request_t*/ out_layer_requests);

    int32_t /*hwc2_error_t*/ displayGetType(
        hwc2_device_t* device,
        hwc2_display_t display,
        int32_t* /*hwc2_display_type_t*/ out_type);

    int32_t /*hwc2_error_t*/ displayGetDozeSupport(
        hwc2_device_t* device,
        hwc2_display_t display,
        int32_t* out_support);

    int32_t /*hwc2_error_t*/ displayGetHdrCapability(
        hwc2_device_t* device,
        hwc2_display_t display,
        uint32_t* out_num_types,
        int32_t* /*android_hdr_t*/ out_types,
        float* out_max_luminance,
        float* out_max_avg_luminance,
        float* out_min_luminance);

    int32_t /*hwc2_error_t*/ displayGetPerFrameMetadataKeys(
        hwc2_device_t* device,
        hwc2_display_t display,
        uint32_t* outNumKeys,
        int32_t* /*hwc2_per_frame_metadata_key_t*/ outKeys);

    int32_t /*hwc2_error_t*/ displayGetReleaseFence(
        hwc2_device_t* device,
        hwc2_display_t display,
        uint32_t* out_num_elem,
        hwc2_layer_t* out_layer,
        int32_t* out_fence);

    int32_t /*hwc2_error_t*/ displayPresent(
        hwc2_device_t* device,
        hwc2_display_t display,
        int32_t* out_retire_fence);

    int32_t /*hwc2_error_t*/ displaySetActiveConfig(
        hwc2_device_t* device,
        hwc2_display_t display,
        hwc2_config_t config_id);

    int32_t /*hwc2_error_t*/ displaySetBrightness(
        hwc2_device_t* device,
        hwc2_display_t display,
        float brightness);

    int32_t /*hwc2_error_t*/ displaySetClientTarget(
        hwc2_device_t* device,
        hwc2_display_t display,
        buffer_handle_t handle,
        int32_t acquire_fence,
        int32_t dataspace,
        hwc_region_t damage);

    int32_t /*hwc2_error_t*/ displaySetColorMode(
        hwc2_device_t* device,
        hwc2_display_t display, int32_t mode);

    int32_t /*hwc2_error_t*/ displaySetColorTransform(
        hwc2_device_t* device,
        hwc2_display_t display,
        const float* matrix,
        int32_t /*android_color_transform_t*/ hint);

    int32_t /*hwc2_error_t*/ displaySetOutputBuffer(
        hwc2_device_t* device,
        hwc2_display_t display,
        buffer_handle_t buffer,
        int32_t releaseFence);

    int32_t /*hwc2_error_t*/ displaySetPowerMode(
        hwc2_device_t* device,
        hwc2_display_t display,
        int32_t /*hwc2_power_mode_t*/ mode);

    int32_t /*hwc2_error_t*/ displaySetVsyncEnabled(
        hwc2_device_t* device,
        hwc2_display_t display,
        int32_t /*hwc2_vsync_t*/ enabled);

    int32_t /*hwc2_error_t*/ displayValidateDisplay(
        hwc2_device_t* device,
        hwc2_display_t display,
        uint32_t* outNumTypes,
        uint32_t* outNumRequests);

    int32_t displayGetDisplayedContentSamplingAttributes(
        hwc2_device_t* device,
        hwc2_display_t display,
        int32_t* /* android_pixel_format_t */ format,
        int32_t* /* android_dataspace_t */ dataspace,
        uint8_t* /* mask of android_component_t */ supported_components);

    int32_t displaySetDisplayedContentSamplingEnabled(
        hwc2_device_t* device,
        hwc2_display_t display,
        int32_t /*hwc2_displayed_content_sampling_t*/ enabled,
        uint8_t /* mask of android_component_t */ component_mask,
        uint64_t max_frames);

    int32_t displayGetDisplayedContentSample(
        hwc2_device_t* device,
        hwc2_display_t display,
        uint64_t max_frames,
        uint64_t timestamp,
        uint64_t* frame_count,
        int32_t samples_size[4],
        uint64_t* samples[4]);

    int32_t /*hwc2_error_t*/ displayGetRenderIntents(
        hwc2_device_t* device,
        hwc2_display_t display,
        int32_t mode,
        uint32_t* out_num_intents,
        int32_t* /*android_render_intent_v1_1_t*/ out_intents);

    int32_t /*hwc2_error_t*/ displaySetColorModeWithRenderIntent(
        hwc2_device_t* device,
        hwc2_display_t display,
        int32_t /*android_color_mode_t*/ mode,
        int32_t /*android_render_intent_v1_1_t */ intent);

    int32_t /*hwc2_error_t*/ displayGetDisplayVsyncPeriod(
        hwc2_device_t* device,
        hwc2_display_t display,
        hwc2_vsync_period_t* out_vsync_period);

    int32_t /*hwc2_error_t*/ displaySetActiveConfigWithConstraints(
        hwc2_device_t* device,
        hwc2_display_t display,
        hwc2_config_t config,
        hwc_vsync_period_change_constraints_t* vsync_period_change_constraints,
        hwc_vsync_period_change_timeline_t* out_timeline);

    int32_t /*hwc2_error_t*/ displayGetSupportedContentTypes(
        hwc2_device_t* device,
        hwc2_display_t display,
        uint32_t* out_num_supported_content_types,
        uint32_t* out_supported_content_types);

    int32_t /*hwc2_error_t*/ displayGetDisplayConnectionType(
        hwc2_device_t* device,
        hwc2_display_t display,
        uint32_t* /*hwc2_display_connection_type_t*/ out_type);

    int32_t /*hwc2_error_t*/ displaySetAutoLowLatencyMode(
        hwc2_device_t* device,
        hwc2_display_t display,
        bool on);

    int32_t /*hwc2_error_t*/ displaySetContentType(
        hwc2_device_t* device,
        hwc2_display_t display,
        int32_t /* hwc2_content_type_t */ content_type);

    int32_t /*hwc2_error_t*/ displayGetDisplayIdentificationData(
        hwc2_device_t* /*device*/,
        hwc2_display_t display,
        uint8_t* out_port,
        uint32_t* out_data_size,
        uint8_t* out_data);

    int32_t displaySetBootDisplayConfig(
        hwc2_device_t* device,
        hwc2_display_t display,
        hwc2_config_t config);

    int32_t displayClearBootDisplayConfig(
        hwc2_device_t* device,
        hwc2_display_t display);

    int32_t displayGetPreferredBootDisplayConfig(
        hwc2_device_t* device,
        hwc2_display_t display,
        hwc2_config_t* config);

    int32_t displayGetDisplayDecorationSupport(
        hwc2_device_t* device,
        hwc2_display_t display,
        std::optional<common::DisplayDecorationSupport>* support);

    int32_t displaySetIdleTimerEnabled(
        hwc2_device_t* device,
        hwc2_display_t display,
        int32_t timeout);

    int32_t displayGetDisplayPhysicalOrientation(
        hwc2_device_t* device,
        hwc2_display_t display,
        common::Transform* orientation);

    int32_t displaySetExpectedPresentTime(
        hwc2_device_t* device,
        hwc2_display_t display,
        int64_t timestamp_nanos);

    /* Layer functions */
    int32_t /*hwc2_error_t*/ layerSetCursorPosition(
        hwc2_device_t* device,
        hwc2_display_t display,
        hwc2_layer_t layer,
        int32_t x,
        int32_t y);

    int32_t /*hwc2_error_t*/ layerSetBuffer(
        hwc2_device_t* device,
        hwc2_display_t display,
        hwc2_layer_t layer,
        buffer_handle_t buffer,
        int32_t acquireFence);

    int32_t /*hwc2_error_t*/ layerSetSurfaceDamage(
        hwc2_device_t* device,
        hwc2_display_t display,
        hwc2_layer_t layer,
        hwc_region_t damage);

    /* Layer state functions */
    int32_t /*hwc2_error_t*/ layerStateSetBlendMode(
        hwc2_device_t* device,
        hwc2_display_t display,
        hwc2_layer_t layer,
        int32_t /*hwc2_blend_mode_t*/ mode);

    int32_t /*hwc2_error_t*/ layerStateSetColor(
        hwc2_device_t* device,
        hwc2_display_t display,
        hwc2_layer_t layer,
        hwc_color_t color);

    int32_t /*hwc2_error_t*/ layerStateSetCompositionType(
        hwc2_device_t* device,
        hwc2_display_t display,
        hwc2_layer_t layer,
        int32_t /*hwc2_composition_t*/ type);

    int32_t /*hwc2_error_t*/ layerStateSetDataSpace(
        hwc2_device_t* device,
        hwc2_display_t display,
        hwc2_layer_t layer,
        int32_t /*android_dataspace_t*/ dataspace);

    int32_t /*hwc2_error_t*/ layerStateSetDisplayFrame(
        hwc2_device_t* device,
        hwc2_display_t display,
        hwc2_layer_t layer,
        hwc_rect_t frame);

    int32_t /*hwc2_error_t*/ layerStateSetPlaneAlpha(
        hwc2_device_t* device,
        hwc2_display_t display,
        hwc2_layer_t layer,
        float alpha);

    int32_t /*hwc2_error_t*/ layerStateSetSidebandStream(
        hwc2_device_t* device,
        hwc2_display_t display,
        hwc2_layer_t layer,
        const native_handle_t* stream);

    int32_t /*hwc2_error_t*/ layerStateSetSourceCrop(
        hwc2_device_t* device,
        hwc2_display_t display,
        hwc2_layer_t layer,
        hwc_frect_t crop);

    int32_t /*hwc2_error_t*/ layerStateSetTransform(
        hwc2_device_t* device,
        hwc2_display_t display,
        hwc2_layer_t layer,
        int32_t /*hwc_transform_t*/ transform);

    int32_t /*hwc2_error_t*/ layerStateSetVisibleRegion(
        hwc2_device_t* device,
        hwc2_display_t display,
        hwc2_layer_t layer,
        hwc_region_t visible);

    int32_t /*hwc2_error_t*/ layerStateSetZOrder(
        hwc2_device_t* device,
        hwc2_display_t display,
        hwc2_layer_t layer,
        uint32_t z);

    int32_t /*hwc2_error_t*/ layerStateSetPerFrameMetadata(
        hwc2_device_t* device,
        hwc2_display_t display,
        hwc2_layer_t layer,
        uint32_t numElements,
        const int32_t* /*hw2_per_frame_metadata_key_t*/ keys,
        const float* metadata);

    int32_t /*hwc2_error_t*/ layerStateSetPerFrameMetadataBlobs(
        hwc2_device_t* device,
        hwc2_display_t display,
        hwc2_layer_t layer,
        uint32_t numElements,
        const int32_t* keys,
        const uint32_t* sizes,
        const uint8_t* metadata);

    int32_t /*hwc2_error_t*/ layerStateSetLayerColorTransform(
        hwc2_device_t* device,
        hwc2_display_t display,
        hwc2_layer_t layer,
        const float* matrix);

    int32_t layerSetLayerBrightness(
        hwc2_device_t* device,
        hwc2_display_t display,
        hwc2_layer_t layer,
        float brightness);

    int32_t layerSetLayerBlockingRegion(
        hwc2_device_t* device,
        hwc2_display_t display,
        hwc2_layer_t layer,
        hwc_region_t region);

public:
    /* interface for ComposerExt::ClientContext */
    virtual int registerClientContext(std::string client_name,
                                      std::shared_ptr<ComposerExt::ConfigCallback> callback,
                                      ComposerExt::ConfigInterface** intf);
    virtual void unRegisterClientContext(ComposerExt::ConfigInterface* intf);
private:
    class ComposerExtIntf: public ComposerExt::ConfigInterface
    {
    public:
        explicit ComposerExtIntf(std::weak_ptr<ComposerExt::ConfigCallback> callback,
                                 HWCMediator* hwc_mediator);
    private:
        virtual int isMSyncSupported(uint64_t disp_id, bool* supported);
        virtual int msyncSetEnable(uint64_t disp_id, ComposerExt::MSyncEnableType enable);
        virtual int msyncSetParamTable(ComposerExt::MSyncParamTableStruct& param_table);
        virtual int msyncGetDefaultParamTable(uint64_t disp_id,
                                              ComposerExt::MSyncParamTableStruct& param_table);


        virtual int setNextDisplayUsage(ComposerExt::DisplayUsage usage);
        virtual int enableHWCLogWithProp();

        virtual int getCtId(uint64_t disp_id, uint64_t* ct_id);

        virtual int isFeatureSupported(ComposerExt::HwcFeature feature, int* supported);

        virtual int setScenarioHint(ComposerExt::ScenarioHint flag, ComposerExt::ScenarioHint mask);

        virtual int getDispDumpEnable(ComposerExt::DisplayType dpy,
                                      bool enable,
                                      ComposerExt::Rect src,
                                      uint32_t dst_w, uint32_t dst_h,
                                      unsigned int hal_format);
        virtual int getDispDump(ComposerExt::DisplayType dpy,
                                ComposerExt::Rect src,
                                uint32_t dst_w, uint32_t dst_h,
                                ComposerExt::DispDumpPoint dump_point,
                                ComposerExt::DispDumpRepaintMode repaint_mode,
                                int64_t min_period_before_prev_get_ns,
                                const native_handle_t* buffer,
                                int release_fence);

    private:
        std::weak_ptr<ComposerExt::ConfigCallback> m_callback;
        HWCMediator *m_hwc_mediator = nullptr;
    };

public:
    void setNextDisplayUsage(ComposerExt::DisplayUsage usage)
    {
        m_next_display_usage = usage;
    }

    ComposerExt::DisplayUsage getNextDisplayUsage() const
    {
        return m_next_display_usage;
    }

private:
    std::atomic<ComposerExt::DisplayUsage> m_next_display_usage;

private:
    bool m_is_init_disp_manager;

    HWC2_PFN_HOTPLUG m_callback_hotplug;
    hwc2_callback_data_t m_callback_hotplug_data;

    HWC2_PFN_VSYNC   m_callback_vsync;
    hwc2_callback_data_t m_callback_vsync_data;

    HWC2_PFN_REFRESH   m_callback_refresh;
    hwc2_callback_data_t m_callback_refresh_data;

    HWC2_PFN_VSYNC_2_4   m_callback_vsync_2_4;
    hwc2_callback_data_t m_callback_vsync_2_4_data;

    HWC2_PFN_VSYNC_PERIOD_TIMING_CHANGED m_callback_vsync_timing_changed;
    hwc2_callback_data_t m_callback_vsync_timing_changed_data;

    HWC2_PFN_SEAMLESS_POSSIBLE m_callback_seamless_possible;
    hwc2_callback_data_t m_callback_seamless_possible_data;

    // for hdr capabilities and keys enum
    void setupHDRFeature();
    std::vector<int32_t> m_hdr_capabilities; // platform supported hdr cap
    std::vector<int32_t> m_hdr_metadata_keys; // platform supported hdr metadata

    int m_prev_hwbinder_tid;
    std::vector<int> m_hwbinder_tid;
    std::mutex m_ext_lock;
    struct ComposerCleintInfo
    {
        std::string client_name;
        std::weak_ptr<ComposerExt::ConfigCallback> callback;
    };
    std::map<uintptr_t, ComposerCleintInfo> m_ext_callback_list;
    std::vector<uintptr_t> m_need_tid_list;

    // control the present index layer
    int32_t m_present_idx_action;
    uint64_t m_present_idx_display_id;
    uint32_t m_present_idx_width;
    uint32_t m_present_idx_height;
    uint32_t m_present_idx_x;
    uint32_t m_present_idx_y;

    // hwc version
    uint32_t m_version_major;
    uint32_t m_version_minor;
    uint32_t m_version_patch;

    // for gpu Cache skip validate
    bool m_gpuc_skip_validate;
};

#endif // HWC_MEDIATOR_H
