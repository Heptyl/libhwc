#pragma once
#include <vector>
#include <map>
#include <string>
#include <hardware/hwcomposer2.h>

#include <aidl/android/hardware/graphics/composer3/BnComposer.h>
#include <aidl/android/hardware/graphics/composer3/BnComposerClient.h>

#include "hwclayer_simple.h"
#include "fake_vsync.h"
#include "ui/gralloc_extra.h"

using namespace aidl::android::hardware::graphics;

namespace simplehwc {

enum State {
    STATE_MODIFIED,
    STATE_VALIDATED_WITH_CHANGES,
    STATE_VALIDATED,
};

class DisplayInfo
{
public:
    DisplayInfo();

public:
    std::string name;
    uint32_t width;
    uint32_t height;
    int format;
    int vsync_period;
    int xdpi;
    int ydpi;
};

struct PrivateHnd
{
    PrivateHnd()
        : ion_fd(-1)
        //, fb_mva(0)
        , handle(NULL)
        , width(0)
        , height(0)
        , vstride(0)
        //, v_align(0)
        , y_stride(0)
        , y_align(0)
        , format(0)
        , size(0)
        //, usage(0)
        , alloc_id(UINT64_MAX)
        , fb_id(0)
        , fence_fd(-1)
        , fence_idx(0)
    {
        //memset(&ext_info, 0, sizeof(ext_info));
    }
    int ion_fd;
    // void* fb_mva;
    buffer_handle_t handle;

    unsigned int width;
    unsigned int height;
    unsigned int vstride;
    //unsigned int v_align;
    unsigned int y_stride;
    unsigned int y_align;
    unsigned int format; // init with buffer info, but may be modified in HWC
    //unsigned int format_original; // store format from buffer info
    int size; // total bytes allocated by gralloc
    //unsigned int usage;
    uint64_t alloc_id;
    uint32_t fb_id;//set by HWC
    int32_t fence_fd;//set by HWC
    uint32_t fence_idx;//set by HWC
};

class HWCDisplay_simple
{
public:
    HWCDisplay_simple(uint64_t display_id, int32_t type, int32_t connect_state);
    ~HWCDisplay_simple();

    uint64_t getId();

    bool isConnected();

    bool isValidLayer(hwc2_layer_t layer_id);

    int32_t createLayer(hwc2_layer_t* out_layer);
    int32_t destroyLayer(hwc2_layer_t layer_id);

    int32_t getActiveConfig(hwc2_config_t* out_config);

    int32_t acceptChanges();

    int32_t getChangedCompositionTypes(uint32_t* out_num_elem, hwc2_layer_t* out_layers,
            int32_t* out_types);

    int32_t getColorMode(uint32_t* out_num_modes, int32_t* out_modes);

    int32_t getConfig(uint32_t* out_num_configs, hwc2_config_t* out_configs);

    int32_t getRequest(int32_t* out_display_requests, uint32_t* out_num_elem);

    int32_t getType(int32_t* out_type);

    int32_t gerReleaseFence(uint32_t* out_num_elem, hwc2_layer_t* out_layer, int32_t* out_fence);

    int32_t present(int32_t* out_retire_fence);

    int32_t setActiveConfig(hwc2_config_t config_id);

    int32_t setClientTarget(buffer_handle_t handle, int32_t acquire_fence, int32_t dataspace,
            hwc_region_t damage);

    int32_t setColorMode(int32_t mode);

    int32_t setColorTransform(const float* matrix, int32_t hint);

    int32_t setOutputBuffer(buffer_handle_t buffer, int32_t release_fence);

    int32_t setPowerMode(int32_t mode);

    int32_t validate(uint32_t* out_num_types, uint32_t* out_num_requests);

    int32_t setCursorPosition(hwc2_layer_t layer, int32_t x, int32_t y);

    int32_t setLayerBuffer(hwc2_layer_t layer, buffer_handle_t buffer, int32_t acquire_fence);

    int32_t setLayerSurfaceDamage(hwc2_layer_t layer, hwc_region_t damage);

    int32_t setLayerBlendMode(hwc2_layer_t layer, int32_t mode);

    int32_t setLayerColor(hwc2_layer_t layer, hwc_color_t color);

    int32_t setLayerCompositionType(hwc2_layer_t layer, int32_t type);

    int32_t setLayerDataSpace(hwc2_layer_t layer, int32_t dataspace);

    int32_t setLayerDisplayFrame(hwc2_layer_t layer, hwc_rect_t frame);

    int32_t setLayerPlaneAlpha(hwc2_layer_t layer, float alpha);

    int32_t setLayerSidebandStream(hwc2_layer_t layer, const native_handle_t* stream);

    int32_t setLayerSourceCrop(hwc2_layer_t layer, hwc_frect_t crop);

    int32_t setLayerTransform(hwc2_layer_t layer, int32_t transform);

    int32_t setLayerVisibleRegion(hwc2_layer_t layer, hwc_region_t visible);

    int32_t setLayerZOrder(hwc2_layer_t layer, uint32_t z);

    int32_t setLayerPerFrameMetadata(hwc2_layer_t layer, uint32_t num_elements,
            const int32_t* keys, const float* metadata);

    int32_t setLayerPerFrameMetadataBlobs(hwc2_layer_t layer, uint32_t num_elements,
            const int32_t* keys, const uint32_t* sizes, const uint8_t* metadata);

    int32_t setLayerBrightness(hwc2_layer_t layer, float brightness);

    int32_t setLayerBlockingRegion(hwc2_layer_t layer, hwc_region_t region);

    int32_t getClientTargetSupport(uint32_t width, uint32_t height,
            int32_t format, int32_t dataspace);

    int32_t getAttribute(hwc2_config_t config, int32_t attribute, int32_t* out_value);

    int32_t getCapabilities(uint32_t* out_num_capabilities, uint32_t* out_capabilities);

    int32_t getName(uint32_t* out_lens, char* out_name);

    int32_t getPerFrameMetadataKey(uint32_t* out_num_keys, int32_t* out_keys);

    void setHotplugCallback(hwc2_function_pointer_t fptr, hwc2_callback_data_t data);

    void setVsyncCallback(hwc2_function_pointer_t fptr, hwc2_callback_data_t data);

    void setVsyncCallback_2_4(hwc2_function_pointer_t fptr, hwc2_callback_data_t data);

    int32_t setVsyncEnabled(int32_t enabled);

    int32_t getDisplayVsyncPeriod(hwc2_vsync_period_t* out_vsync_period);

    int32_t setActiveConfigWithConstraints(hwc2_config_t config,
            hwc_vsync_period_change_constraints_t* vsync_period_change_constraints,
            hwc_vsync_period_change_timeline_t* out_timeline);

    int32_t setExpectedPresentTime(int64_t timestamp_nanos);

    int32_t setBootDisplayConfig(hwc2_config_t config);

    int32_t clearBootDisplayConfig();

    int32_t getPreferredBootDisplayConfig(hwc2_config_t* config);

    int32_t getDisplayDecorationSupport(std::optional<common::DisplayDecorationSupport>* support);

    int32_t setIdleTimerEnabled(int32_t timeout);

    int32_t getDisplayPhysicalOrientation(common::Transform* orientation);

    int32_t setLayerColorTransform(hwc2_layer_t layer, const float* matrix);

private:
    State getState();
    void setState(State state);

    void clearDirtyLayer();

private:
    uint64_t m_id;

    std::map<uint64_t, std::shared_ptr<HWCLayer_simple>> m_layers;
    std::map<uint64_t, std::shared_ptr<HWCLayer_simple>> m_dirty_layers;

    hwc2_config_t m_active_config;

    State m_state;

    std::vector<int32_t> m_color_modes;

    int32_t m_type;

    buffer_handle_t m_buffer;
	PrivateHnd m_priv_hnd;
    int m_fence;
    int m_present_fence;

    DisplayInfo m_info;

    int32_t m_connect_state;

    HWC2_PFN_HOTPLUG m_callback_hotplug;
    hwc2_callback_data_t m_callback_data_hotplug;

    HWC2_PFN_VSYNC m_callback_vsync;
    hwc2_callback_data_t m_callback_data_vsync;

    HWC2_PFN_VSYNC_2_4 m_callback_vsync_2_4;
    hwc2_callback_data_t m_callback_data_vsync_2_4;

    FakeVsyncThread m_vsync;
};

}  // namespace simplehwc

