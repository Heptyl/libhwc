#define DEBUG_LOG_TAG "hwc2_simple"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include <cstring>
#include <sstream>
#include <cutils/properties.h>
#include <cutils/log.h>
#include <sync/sync.h>
#include <sw_sync.h>
#include <inttypes.h>

#include <hwc_feature_list.h>

#include "hwc2_simple.h"
#include "debug_simple.h"

using namespace aidl::android::hardware::graphics;

namespace simplehwc {

#define UNUSED(param) do { (void)(param); } while(0)

#define CHECK_DISP(display) \
    do { \
        if (!isValidDisplay(display)) { \
            ALOGE("%s: this display(%" PRIu64 ") is invalid", __FUNCTION__, display); \
            return HWC2_ERROR_BAD_DISPLAY; \
        } \
    } while(0)

extern "C" HWC2Api* getHWCMediatorSimple()
{
    return &HWCMediator_simple::getInstance();
}

HWCMediator_simple& HWCMediator_simple::getInstance()
{
    HWC_LOGI("HWCMediator_simple - getInstance()");
    static HWCMediator_simple gInstance;
    return gInstance;
}

HWCMediator_simple::HWCMediator_simple()
{
#if 0
    char value[PROPERTY_VALUE_MAX] = {0};
    property_get("vendor.debug.force.memcpy", value, "-1");
    ALOGD("getprop [vendor.debug.force.memcpy] %s", value);
    if (-1 != atoi(value))
    {
        setForceMemcpy(atoi(value));
        ALOGD("setForceMemcpy: %d", atoi(value));
    }
#endif
    char value[PROPERTY_VALUE_MAX] = {0};
    property_get("ro.build.type", value, "eng");
    bool is_eng_build = strcmp(value, "eng") == 0;
    if (is_eng_build)
    {
        HWC_LOGI("eng load set log level to debug");
        setLogLevel(LOG_LEVEL_DEBUG);
    }
    HWC_LOGD("make_shared<HWCDisplay_simple>");
    auto primary = std::make_shared<HWCDisplay_simple>(HWC_DISPLAY_PRIMARY, HWC2_DISPLAY_TYPE_PHYSICAL,
            HWC2_CONNECTION_CONNECTED);
    m_displays[HWC_DISPLAY_PRIMARY] = primary;
}

HWCMediator_simple::~HWCMediator_simple()
{
}

bool HWCMediator_simple::isValidDisplay(hwc2_display_t id)
{
    auto display = m_displays.find(id);
    if (display == m_displays.end())
    {
        return false;
    }
    return true;
}

//-----------------------------------------------------------------------------

void HWCMediator_simple::open(/*hwc_private_device_t* device*/)
{
}

void HWCMediator_simple::close(/*hwc_private_device_t* device*/)
{
}

void HWCMediator_simple::getCapabilities(
        uint32_t* out_count,
        int32_t* /*hwc2_capability_t*/ out_capabilities)
{
    if (out_capabilities == nullptr) {
        *out_count = static_cast<uint32_t>(m_capabilities.size());
        return;
    }

    auto iter = m_capabilities.cbegin();
    for (size_t i = 0; i < *out_count; ++i)
    {
        if (iter == m_capabilities.cend())
        {
            return;
        }
        out_capabilities[i] = static_cast<int32_t>(*iter);
        ++iter;
    }
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::deviceCreateVirtualDisplay(
    hwc2_device_t* device,
    uint32_t width,
    uint32_t height,
    int32_t* /*android_pixel_format_t*/ format,
    hwc2_display_t* out_display)
{
    UNUSED(device);
    UNUSED(width);
    UNUSED(height);
    UNUSED(format);
    UNUSED(out_display);
    return HWC2_ERROR_NO_RESOURCES;
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::deviceDestroyVirtualDisplay(
    hwc2_device_t* device,
    hwc2_display_t display)
{
    UNUSED(device);
    UNUSED(display);
    return HWC2_ERROR_BAD_DISPLAY;
}

void HWCMediator_simple::deviceDump(hwc2_device_t* device, uint32_t* outSize, char* outBuffer)
{
    UNUSED(device);
    UNUSED(outSize);
    UNUSED(outBuffer);
    {
        char value[PROPERTY_VALUE_MAX] = {0};

        property_get("vendor.debug.hwc.log", value, "-1");
        ALOGD("getprop [vendor.debug.hwc.log] %s", value);
        if (-1 != atoi(value))
        {
            setLogLevel(atoi(value));
            ALOGD("setLogLevel: %d", atoi(value));
        }

        property_get("vendor.debug.hwc.dump_buf_cont", value, "-1");
        ALOGD("getprop [vendor.debug.hwc.dump_buf_cont] %s", value);
        if (-1 != atoi(value))
        {
            setDumpBuf(atoi(value));
            ALOGD("setDumpBuf: %d", atoi(value));
        }

    }
}

int32_t HWCMediator_simple::setHwcVersion(uint32_t major, uint32_t minor, uint32_t patch)
{
    UNUSED(major);
    UNUSED(minor);
    UNUSED(patch);
    return HWC2_ERROR_NONE;
}

uint32_t HWCMediator_simple::deviceGetMaxVirtualDisplayCount(hwc2_device_t* device)
{
    UNUSED(device);
    return 0;
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::deviceRegisterCallback(
    hwc2_device_t* device,
    int32_t /*hwc2_callback_descriptor_t*/ descriptor,
    hwc2_callback_data_t callback_data,
    hwc2_function_pointer_t pointer)
{
    UNUSED(device);
    switch (descriptor)
    {
        case HWC2_CALLBACK_HOTPLUG:
            if (pointer)
            {
                for (auto iter : m_displays)
                {
                    iter.second->setHotplugCallback(pointer, callback_data);
                    if (iter.second->isConnected())
                    {
                        reinterpret_cast<HWC2_PFN_HOTPLUG>(pointer)(callback_data,
                                iter.second->getId(), HWC2_CONNECTION_CONNECTED);
                    }
                }
            }
            break;

        case HWC2_CALLBACK_REFRESH:
            break;

        case HWC2_CALLBACK_VSYNC:
            if (pointer)
            {
                for (auto iter : m_displays)
                {
                    iter.second->setVsyncCallback(pointer, callback_data);
                }
            }
            break;

        case HWC2_CALLBACK_VSYNC_2_4:
            if (pointer)
            {
                for (auto iter : m_displays)
                {
                    iter.second->setVsyncCallback_2_4(pointer, callback_data);
                }
            }
            break;

        case HWC2_CALLBACK_VSYNC_PERIOD_TIMING_CHANGED:
            break;

        case HWC2_CALLBACK_SEAMLESS_POSSIBLE:
            break;

        default:
            return HWC2_ERROR_BAD_PARAMETER;
    }

    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::displayAcceptChanges(
    hwc2_device_t* device,
    hwc2_display_t display)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->acceptChanges();
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::displayCreateLayer(
    hwc2_device_t* device,
    hwc2_display_t display,
    hwc2_layer_t* out_layer)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->createLayer(out_layer);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::displayDestroyLayer(
    hwc2_device_t* device,
    hwc2_display_t display,
    hwc2_layer_t layer)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->destroyLayer(layer);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::displayGetActiveConfig(
    hwc2_device_t* device,
    hwc2_display_t display,
    hwc2_config_t* out_config)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->getActiveConfig(out_config);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::displayGetBrightnessSupport(
    hwc2_device_t* device,
    hwc2_display_t display,
    bool* out_support)
{
    UNUSED(device);
    CHECK_DISP(display);
    if (out_support != nullptr)
    {
        *out_support = false;
    }
    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::displayGetChangedCompositionTypes(
    hwc2_device_t* device,
    hwc2_display_t display,
    uint32_t* out_num_elem,
    hwc2_layer_t* out_layers,
    int32_t* /*hwc2_composition_t*/ out_types)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->getChangedCompositionTypes(out_num_elem, out_layers, out_types);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::displayGetClientTargetSupport(
    hwc2_device_t* device,
    hwc2_display_t display,
    uint32_t width,
    uint32_t height,
    int32_t /*android_pixel_format_t*/ format,
    int32_t /*android_dataspace_t*/ dataspace)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->getClientTargetSupport(width, height, format, dataspace);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::displayGetColorMode(
    hwc2_device_t* device,
    hwc2_display_t display,
    uint32_t* out_num_modes,
    int32_t* out_modes)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->getColorMode(out_num_modes, out_modes);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::displayGetAttribute(
    hwc2_device_t* device,
    hwc2_display_t display,
    hwc2_config_t config,
    int32_t /*hwc2_attribute_t*/ attribute,
    int32_t* out_value)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->getAttribute(config, attribute, out_value);;
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::displayGetConfigs(
    hwc2_device_t* device,
    hwc2_display_t display,
    uint32_t* out_num_configs,
    hwc2_config_t* out_configs)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->getConfig(out_num_configs, out_configs);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::displayGetCapabilities(
    hwc2_device_t* device,
    hwc2_display_t display,
    uint32_t* out_num_capabilities,
    uint32_t* out_capabilities)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->getCapabilities(out_num_capabilities, out_capabilities);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::displayGetName(
    hwc2_device_t* device,
    hwc2_display_t display,
    uint32_t* out_lens,
    char* out_name)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->getName(out_lens, out_name);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::displayGetRequests(
    hwc2_device_t* device,
    hwc2_display_t display,
    int32_t* /*hwc2_display_request_t*/ out_display_requests,
    uint32_t* out_num_elem,
    hwc2_layer_t* out_layer,
    int32_t* /*hwc2_layer_request_t*/ out_layer_requests)
{
    UNUSED(device);
    UNUSED(out_layer);
    UNUSED(out_layer_requests);
    CHECK_DISP(display);
    return m_displays[display]->getRequest(out_display_requests, out_num_elem);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::displayGetType(
    hwc2_device_t* device,
    hwc2_display_t display,
    int32_t* /*hwc2_display_type_t*/ out_type)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->getType(out_type);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::displayGetDozeSupport(
    hwc2_device_t* device,
    hwc2_display_t display,
    int32_t* out_support)
{
    UNUSED(device);
    CHECK_DISP(display);
    if (out_support)
    {
        *out_support = false;
    }
    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::displayGetHdrCapability(
    hwc2_device_t* device,
    hwc2_display_t display,
    uint32_t* out_num_types,
    int32_t* /*android_hdr_t*/ out_types,
    float* out_max_luminance,
    float* out_max_avg_luminance,
    float* out_min_luminance)
{
    UNUSED(device);
    UNUSED(out_types);
    UNUSED(out_max_luminance);
    UNUSED(out_max_avg_luminance);
    UNUSED(out_min_luminance);
    CHECK_DISP(display);
    if (out_num_types)
    {
        *out_num_types = 0;
    }
    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::displayGetPerFrameMetadataKeys(
    hwc2_device_t* device,
    hwc2_display_t display,
    uint32_t* out_num_keys,
    int32_t* /*hwc2_per_frame_metadata_key_t*/ out_keys)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->getPerFrameMetadataKey(out_num_keys, out_keys);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::displayGetDisplayIdentificationData(
    hwc2_device_t* device,
    hwc2_display_t display,
    uint8_t* out_port,
    uint32_t* out_data_size,
    uint8_t* out_data)
{
    UNUSED(device);
    UNUSED(out_port);
    UNUSED(out_data_size);
    UNUSED(out_data);
    CHECK_DISP(display);
    return HWC2_ERROR_UNSUPPORTED;
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::displayGetReleaseFence(
    hwc2_device_t* device,
    hwc2_display_t display,
    uint32_t* out_num_elem,
    hwc2_layer_t* out_layer,
    int32_t* out_fence)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->gerReleaseFence(out_num_elem, out_layer, out_fence);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::displayPresent(
    hwc2_device_t* device,
    hwc2_display_t display,
    int32_t* out_retire_fence)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->present(out_retire_fence);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::displaySetActiveConfig(
    hwc2_device_t* device,
    hwc2_display_t display,
    hwc2_config_t config_id)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->setActiveConfig(config_id);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::displaySetBrightness(
    hwc2_device_t* device,
    hwc2_display_t display,
    float brightness)
{
    UNUSED(device);
    UNUSED(brightness);
    CHECK_DISP(display);
    return HWC2_ERROR_UNSUPPORTED;
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::displaySetClientTarget(
    hwc2_device_t* device,
    hwc2_display_t display,
    buffer_handle_t handle,
    int32_t acquire_fence,
    int32_t dataspace,
    hwc_region_t damage)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->setClientTarget(handle, acquire_fence, dataspace, damage);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::displaySetColorMode(
    hwc2_device_t* device,
    hwc2_display_t display, int32_t mode)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->setColorMode(mode);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::displaySetColorTransform(
    hwc2_device_t* device,
    hwc2_display_t display,
    const float* matrix,
    int32_t /*android_color_transform_t*/ hint)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->setColorTransform(matrix, hint);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::displaySetOutputBuffer(
    hwc2_device_t* device,
    hwc2_display_t display,
    buffer_handle_t buffer,
    int32_t release_fence)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->setOutputBuffer(buffer, release_fence);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::displaySetPowerMode(
    hwc2_device_t* device,
    hwc2_display_t display,
    int32_t /*hwc2_power_mode_t*/ mode)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->setPowerMode(mode);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::displaySetVsyncEnabled(
    hwc2_device_t* device,
    hwc2_display_t display,
    int32_t /*hwc2_vsync_t*/ enabled)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->setVsyncEnabled(enabled);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::displayValidateDisplay(
    hwc2_device_t* device,
    hwc2_display_t display,
    uint32_t* out_num_types,
    uint32_t* out_num_requests)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->validate(out_num_types, out_num_requests);
}

/* Layer functions */
int32_t /*hwc2_error_t*/ HWCMediator_simple::layerSetCursorPosition(
    hwc2_device_t* device,
    hwc2_display_t display,
    hwc2_layer_t layer,
    int32_t x,
    int32_t y)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->setCursorPosition(layer, x, y);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::layerSetBuffer(
    hwc2_device_t* device,
    hwc2_display_t display,
    hwc2_layer_t layer,
    buffer_handle_t buffer,
    int32_t acquire_fence)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->setLayerBuffer(layer, buffer, acquire_fence);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::layerSetSurfaceDamage(
    hwc2_device_t* device,
    hwc2_display_t display,
    hwc2_layer_t layer,
    hwc_region_t damage)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->setLayerSurfaceDamage(layer, damage);
}

/* Layer state functions */
int32_t /*hwc2_error_t*/ HWCMediator_simple::layerStateSetBlendMode(
    hwc2_device_t* device,
    hwc2_display_t display,
    hwc2_layer_t layer,
    int32_t /*hwc2_blend_mode_t*/ mode)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->setLayerBlendMode(layer, mode);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::layerStateSetColor(
    hwc2_device_t* device,
    hwc2_display_t display,
    hwc2_layer_t layer,
    hwc_color_t color)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->setLayerColor(layer, color);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::layerStateSetCompositionType(
    hwc2_device_t* device,
    hwc2_display_t display,
    hwc2_layer_t layer,
    int32_t /*hwc2_composition_t*/ type)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->setLayerCompositionType(layer, type);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::layerStateSetDataSpace(
    hwc2_device_t* device,
    hwc2_display_t display,
    hwc2_layer_t layer,
    int32_t /*android_dataspace_t*/ dataspace)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->setLayerDataSpace(layer, dataspace);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::layerStateSetDisplayFrame(
    hwc2_device_t* device,
    hwc2_display_t display,
    hwc2_layer_t layer,
    hwc_rect_t frame)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->setLayerDisplayFrame(layer, frame);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::layerStateSetPlaneAlpha(
    hwc2_device_t* device,
    hwc2_display_t display,
    hwc2_layer_t layer,
    float alpha)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->setLayerPlaneAlpha(layer, alpha);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::layerStateSetSidebandStream(
    hwc2_device_t* device,
    hwc2_display_t display,
    hwc2_layer_t layer,
    const native_handle_t* stream)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->setLayerSidebandStream(layer, stream);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::layerStateSetSourceCrop(
    hwc2_device_t* device,
    hwc2_display_t display,
    hwc2_layer_t layer,
    hwc_frect_t crop)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->setLayerSourceCrop(layer, crop);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::layerStateSetTransform(
    hwc2_device_t* device,
    hwc2_display_t display,
    hwc2_layer_t layer,
    int32_t /*hwc_transform_t*/ transform)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->setLayerTransform(layer, transform);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::layerStateSetVisibleRegion(
    hwc2_device_t* device,
    hwc2_display_t display,
    hwc2_layer_t layer,
    hwc_region_t visible)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->setLayerVisibleRegion(layer, visible);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::layerStateSetZOrder(
    hwc2_device_t* device,
    hwc2_display_t display,
    hwc2_layer_t layer,
    uint32_t z)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->setLayerZOrder(layer, z);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::layerStateSetPerFrameMetadata(
    hwc2_device_t* device,
    hwc2_display_t display,
    hwc2_layer_t layer,
    uint32_t num_elements,
    const int32_t* /*hw2_per_frame_metadata_key_t*/ keys,
    const float* metadata)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->setLayerPerFrameMetadata(layer, num_elements, keys, metadata);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::layerStateSetPerFrameMetadataBlobs(
    hwc2_device_t* device,
    hwc2_display_t display,
    hwc2_layer_t layer,
    uint32_t num_elements,
    const int32_t* keys,
    const uint32_t* sizes,
    const uint8_t* metadata)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->setLayerPerFrameMetadataBlobs(layer, num_elements, keys,
            sizes, metadata);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::displayGetDisplayedContentSamplingAttributes(
        hwc2_device_t* device,
        hwc2_display_t display,
        int32_t* /* android_pixel_format_t */ format,
        int32_t* /* android_dataspace_t */ dataspace,
        uint8_t* /* mask of android_component_t */ supported_components)
{
    UNUSED(device);
    UNUSED(format);
    UNUSED(dataspace);
    UNUSED(supported_components);
    CHECK_DISP(display);
    return HWC2_ERROR_UNSUPPORTED;
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::displaySetDisplayedContentSamplingEnabled(
        hwc2_device_t* device,
        hwc2_display_t display,
        int32_t /*hwc2_displayed_content_sampling_t*/ enabled,
        uint8_t /* mask of android_component_t */ component_mask,
        uint64_t max_frames)
{
    UNUSED(device);
    UNUSED(enabled);
    UNUSED(component_mask);
    UNUSED(max_frames);
    CHECK_DISP(display);
    return HWC2_ERROR_UNSUPPORTED;
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::displayGetDisplayedContentSample(
        hwc2_device_t* device,
        hwc2_display_t display,
        uint64_t max_frames,
        uint64_t timestamp,
        uint64_t* frame_count,
        int32_t samples_size[4],
        uint64_t* samples[4])
{
    UNUSED(device);
    UNUSED(max_frames);
    UNUSED(timestamp);
    UNUSED(frame_count);
    UNUSED(samples_size);
    UNUSED(samples);
    CHECK_DISP(display);

    return HWC2_ERROR_UNSUPPORTED;
}

bool HWCMediator_simple::supportApiFunction(MTK_HWC_API api)
{
    UNUSED(api);

    return false;
}

int32_t /* hwc2_error_t*/ HWCMediator_simple::displayGetRenderIntents(
        hwc2_device_t* device,
        hwc2_display_t display,
        int32_t mode,
        uint32_t* out_num_intents,
        int32_t* /* android_render_intent_v1_1_t*/ out_intents)
{
    UNUSED(device);
    UNUSED(mode);
    UNUSED(out_num_intents);
    UNUSED(out_intents);

    CHECK_DISP(display);
    return HWC2_ERROR_UNSUPPORTED;
}

int32_t /* hwc2_error_t*/ HWCMediator_simple::displaySetColorModeWithRenderIntent(
        hwc2_device_t* device,
        hwc2_display_t display,
        int32_t /* android_color_mode_t*/ mode,
        int32_t /* android_render_intent_v1_1_t */ intent)
{
    UNUSED(device);
    UNUSED(mode);
    UNUSED(intent);
    CHECK_DISP(display);

    return HWC2_ERROR_UNSUPPORTED;
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::displayGetDisplayVsyncPeriod(
    hwc2_device_t* device,
    hwc2_display_t display,
    hwc2_vsync_period_t* out_vsync_period)
{
    UNUSED(device);
    CHECK_DISP(display);

    return m_displays[display]->getDisplayVsyncPeriod(out_vsync_period);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::displaySetActiveConfigWithConstraints(
    hwc2_device_t* device,
    hwc2_display_t display,
    hwc2_config_t config,
    hwc_vsync_period_change_constraints_t* vsync_period_change_constraints,
    hwc_vsync_period_change_timeline_t* out_timeline)
{
    UNUSED(device);
    CHECK_DISP(display);

    return m_displays[display]->setActiveConfigWithConstraints(config,
            vsync_period_change_constraints, out_timeline);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::displayGetSupportedContentTypes(
    hwc2_device_t* device,
    hwc2_display_t display,
    uint32_t* out_num_supported_content_types,
    uint32_t* out_supported_content_types)
{
    UNUSED(device);
    UNUSED(out_supported_content_types);
    CHECK_DISP(display);

    // We currently do not implement this API yet, just for pass VTS
    *out_num_supported_content_types = 0;
    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::displayGetDisplayConnectionType(
    hwc2_device_t* device,
    hwc2_display_t display,
    uint32_t* /*hwc2_display_connection_type_t*/ out_type)
{
    UNUSED(device);
    CHECK_DISP(display);

    switch (m_displays[display]->getId())
    {
        case HWC_DISPLAY_PRIMARY:
            *out_type = HWC2_DISPLAY_CONNECTION_TYPE_INTERNAL;
            return HWC2_ERROR_NONE;

        case HWC_DISPLAY_EXTERNAL:
            *out_type = HWC2_DISPLAY_CONNECTION_TYPE_EXTERNAL;
            return HWC2_ERROR_NONE;

        default:
            return HWC2_ERROR_BAD_DISPLAY;
    }
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::displaySetAutoLowLatencyMode(
    hwc2_device_t* device,
    hwc2_display_t display,
    bool on)
{
    UNUSED(device);
    UNUSED(on);
    CHECK_DISP(display);

    // We currently do not implement this API yet, just for pass VTS
    return HWC2_ERROR_UNSUPPORTED;
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::displaySetContentType(
    hwc2_device_t* device,
    hwc2_display_t display,
    int32_t /* hwc2_content_type_t */ content_type)
{
    UNUSED(device);
    CHECK_DISP(display);

    // We currently do not implement this API yet, just for pass VTS
    switch (content_type)
    {
        case HWC2_CONTENT_TYPE_GRAPHICS:
        case HWC2_CONTENT_TYPE_PHOTO:
        case HWC2_CONTENT_TYPE_CINEMA:
        case HWC2_CONTENT_TYPE_GAME:
            return HWC2_ERROR_UNSUPPORTED;

        default:
            return HWC2_ERROR_BAD_PARAMETER;
    }
}

int32_t HWCMediator_simple::displaySetBootDisplayConfig(
    hwc2_device_t* device,
    hwc2_display_t display,
    hwc2_config_t config)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->setBootDisplayConfig(config);
}

int32_t HWCMediator_simple::displayClearBootDisplayConfig(
    hwc2_device_t* device,
    hwc2_display_t display)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->clearBootDisplayConfig();
}

int32_t HWCMediator_simple::displayGetPreferredBootDisplayConfig(
    hwc2_device_t* device,
    hwc2_display_t display,
    hwc2_config_t* config)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->getPreferredBootDisplayConfig(config);
}

int32_t HWCMediator_simple::displayGetDisplayDecorationSupport(
    hwc2_device_t* device,
    hwc2_display_t display,
    std::optional<common::DisplayDecorationSupport>* support)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->getDisplayDecorationSupport(support);
}

int32_t HWCMediator_simple::displaySetIdleTimerEnabled(
    hwc2_device_t* device,
    hwc2_display_t display,
    int32_t timeout)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->setIdleTimerEnabled(timeout);
}

int32_t HWCMediator_simple::displayGetDisplayPhysicalOrientation(
    hwc2_device_t* device,
    hwc2_display_t display,
    common::Transform* orientation)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->getDisplayPhysicalOrientation(orientation);
}

int32_t HWCMediator_simple::layerSetLayerBrightness(
    hwc2_device_t* device,
    hwc2_display_t display,
    hwc2_layer_t layer,
    float brightness)
{
    UNUSED(device);
    CHECK_DISP(display);

    return m_displays[display]->setLayerBrightness(layer, brightness);
}

int32_t HWCMediator_simple::layerSetLayerBlockingRegion(
    hwc2_device_t* device,
    hwc2_display_t display,
    hwc2_layer_t layer,
    hwc_region_t region)
{
    UNUSED(device);
    CHECK_DISP(display);

    return m_displays[display]->setLayerBlockingRegion(layer, region);
}

int32_t HWCMediator_simple::displaySetExpectedPresentTime(
    hwc2_device_t* device,
    hwc2_display_t display,
    int64_t timestamp_nanos)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->setExpectedPresentTime(timestamp_nanos);
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::deviceGetDataspaceSaturationMatrix(
     hwc2_device_t* device,
     int32_t /*android_dataspace_t*/ dataspace,
     float* out_matrix)
{
    UNUSED(device);
    out_matrix[0] = 1.0f;
    out_matrix[1] = 0.0f;
    out_matrix[2] = 0.0f;
    out_matrix[3] = 0.0f;

    out_matrix[4] = 0.0f;
    out_matrix[5] = 1.0f;
    out_matrix[6] = 0.0f;
    out_matrix[7] = 0.0f;

    out_matrix[8] = 0.0f;
    out_matrix[9] = 0.0f;
    out_matrix[10] = 1.0f;
    out_matrix[11] = 0.0f;

    out_matrix[12] = 0.0f;
    out_matrix[13] = 0.0f;
    out_matrix[14] = 0.0f;
    out_matrix[15] = 1.0f;

    if (dataspace != HAL_DATASPACE_SRGB_LINEAR)
    {
        return HWC2_ERROR_BAD_PARAMETER;
    }

    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator_simple::layerStateSetLayerColorTransform(
    hwc2_device_t* device,
    hwc2_display_t display,
    hwc2_layer_t layer,
    const float* matrix)
{
    UNUSED(device);
    CHECK_DISP(display);
    return m_displays[display]->setLayerColorTransform(layer, matrix);
}

}  // namespace simplehwc

