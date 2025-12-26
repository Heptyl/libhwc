#pragma once
#include <vector>
#include <atomic>
#include <sstream>
#include <mutex>
#include <map>
#include <unordered_set>

#include <utils/RefBase.h>

#include "hwc2_api.h"

#include "hwcdisplay_simple.h"

#include <aidl/android/hardware/graphics/composer3/BnComposer.h>
#include <aidl/android/hardware/graphics/composer3/BnComposerClient.h>

using namespace aidl::android::hardware::graphics;

namespace simplehwc {

class HWCMediator_simple : public HWC2Api
{
public:
    static HWCMediator_simple& getInstance();
    ~HWCMediator_simple();

private:
    HWCMediator_simple();

public:
    bool supportApiFunction(MTK_HWC_API api);
    void open(/*hwc_private_device_t* device*/);

    void close(/*hwc_private_device_t* device*/);

    void getCapabilities(
        uint32_t* out_count,
        int32_t* /*hwc2_capability_t*/ out_capabilities);

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

    int32_t setHwcVersion(uint32_t major, uint32_t minor, uint32_t patch);

    uint32_t deviceGetMaxVirtualDisplayCount(hwc2_device_t* device);

    int32_t /*hwc2_error_t*/ deviceRegisterCallback(
        hwc2_device_t* device,
        int32_t /*hwc2_callback_descriptor_t*/ descriptor,
        hwc2_callback_data_t callbackData,
        hwc2_function_pointer_t pointer);

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

    int32_t /*hwc2_error_t*/ deviceGetDataspaceSaturationMatrix(
         hwc2_device_t* device,
         int32_t /*android_dataspace_t*/ dataspace,
         float* out_matrix);

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

    int32_t /* hwc2_error_t*/ displayGetRenderIntents(
        hwc2_device_t* device,
        hwc2_display_t display,
        int32_t mode,
        uint32_t* out_num_intents,
        int32_t* /* android_render_intent_v1_1_t*/ out_intents);

    int32_t /* hwc2_error_t*/ displaySetColorModeWithRenderIntent(
        hwc2_device_t* device,
        hwc2_display_t display,
        int32_t /* android_color_mode_t*/ mode,
        int32_t /* android_render_intent_v1_1_t */ intent);

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
        hwc2_device_t* device,
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

    int32_t displaySetExpectedPresentTime(
        hwc2_device_t* device,
        hwc2_display_t display,
        int64_t timestamp_nanos);

private:
    bool isValidDisplay(hwc2_display_t disply);

private:
    static std::atomic<uint64_t> s_layer_id;

    std::map<uint64_t, std::shared_ptr<HWCDisplay_simple>> m_displays;

    std::unordered_set<int32_t> m_capabilities;
};

}  // namespace simplehwc

