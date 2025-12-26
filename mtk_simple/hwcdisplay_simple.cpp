#define DEBUG_LOG_TAG "hwcdisplay_simple"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include "hwcdisplay_simple.h"

#include <cutils/log.h>
#include <inttypes.h>
#include <sync/sync.h>

#include "dev_interface_simple.h"
//#include "hwc_ui/GraphicBufferMapper.h"

#include "debug_simple.h"
#include <cutils/properties.h>

namespace simplehwc {

#define UNUSED(param) do { (void)(param); } while(0)

#define CHECK_LAYER(layer) \
    do { \
        if (!isValidLayer(layer)) { \
            HWC_LOGE("(%" PRIu64 "): this layer(%" PRIu64 ") is invalid", m_id, layer); \
            return HWC2_ERROR_BAD_LAYER; \
        } \
    } while(0)

#define ALIGN(x,a) (((x)+(a)-1)&~((a)-1))

//-----------------------------------------------------------------------------

DisplayInfo::DisplayInfo()
    : name("")
    , width(0)
    , height(0)
    , format(0)
    , vsync_period(0)
    , xdpi(0)
    , ydpi(0)
{
}

//-----------------------------------------------------------------------------

HWCDisplay_simple::HWCDisplay_simple(uint64_t display_id, int32_t type, int32_t connect_state)
    : m_id(display_id)
    , m_active_config(0)
    , m_state(STATE_MODIFIED)
    , m_type(type)
    , m_buffer(nullptr)
    , m_fence(-1)
    , m_present_fence(-1)
    , m_connect_state(connect_state)
    , m_callback_hotplug(nullptr)
    , m_callback_data_hotplug(nullptr)
    , m_callback_vsync(nullptr)
    , m_callback_data_vsync(nullptr)
    , m_callback_vsync_2_4(nullptr)
    , m_callback_data_vsync_2_4(nullptr)
    , m_vsync(display_id)
{
    HWC_LOGD("(%" PRIu64 ")%s()", display_id, __func__);

    m_color_modes.push_back(HAL_COLOR_MODE_NATIVE);
    m_vsync.start(m_info.vsync_period);//
    HWC_LOGD("(%" PRIu64 ")%s: getHwDevice_simple()+", display_id, __func__);
    auto hw_device = getHwDevice_simple();

    HWC_LOGD("(%" PRIu64 ")%s: hw_device->getDisplyResolution()+", display_id, __func__);
    hw_device->getDisplyResolution(display_id, &m_info.width, &m_info.height);
    uint32_t phy_width = 0;
    uint32_t phy_height = 0;
    hw_device->getDisplyPhySize(display_id, &phy_width, &phy_height);
    m_info.xdpi = static_cast<int>((m_info.width * 1000) / (0.0393700787f * phy_width));
    m_info.ydpi = static_cast<int>((m_info.height * 1000) / (0.0393700787f * phy_height));
    uint32_t refresh = 0;
    hw_device->getDisplayRefresh(display_id, &refresh);
    m_info.vsync_period = static_cast<int>(1000000000 / refresh);
    //m_vsync.start(m_info.vsync_period);//
    HWC_LOGI("(%" PRIu64 ")%s: vir_size=%ux%u  xdpi=%d  ydpi=%d  refresh=%d",
            display_id, __func__, m_info.width, m_info.height, m_info.xdpi, m_info.ydpi,
            m_info.vsync_period);
    hw_device->initDisplay(display_id);
    if (!isForceMemcpy() )
        hw_device->createOverlaySession(display_id, HWC_DISP_SESSION_DIRECT_LINK_MODE);//for prepare fence
}

HWCDisplay_simple::~HWCDisplay_simple()
{
}

uint64_t HWCDisplay_simple::getId()
{
    return m_id;
}

bool HWCDisplay_simple::isConnected()
{
    return m_connect_state == HWC2_CONNECTION_CONNECTED ? true : false;
}

int32_t HWCDisplay_simple::createLayer(hwc2_layer_t* out_layer)
{
    auto layer = std::make_shared<HWCLayer_simple>();
    if (layer == nullptr)
    {
        ALOGI("%s: failed to allocate layer", __func__);
        return HWC2_ERROR_NO_RESOURCES;
    }

    m_layers[layer->getId()] = layer;
    *out_layer = layer->getId();
    setState(STATE_MODIFIED);
    HWC_LOGI("(%" PRIu64 "): out_layer:%" PRIu64, m_id, *out_layer);
    return HWC2_ERROR_NONE;
}

bool HWCDisplay_simple::isValidLayer(hwc2_layer_t layer_id)
{
    auto layer = m_layers.find(layer_id);
    if (layer == m_layers.end())
    {
        return false;
    }
    return true;
}

int32_t HWCDisplay_simple::destroyLayer(hwc2_layer_t layer_id)
{
    auto layer = m_dirty_layers.find(layer_id);
    if (layer != m_dirty_layers.end())
    {
        m_dirty_layers.erase(layer);
    }

    int32_t res = HWC2_ERROR_NONE;
    layer = m_layers.find(layer_id);
    if (layer == m_layers.end())
    {
        res = HWC2_ERROR_BAD_LAYER;
        HWC_LOGW("(%" PRIu64 "): failed to destroy layer:%" PRIu64, m_id, layer_id);
    }
    else
    {
        m_layers.erase(layer);
        setState(STATE_MODIFIED);
        HWC_LOGI("(%" PRIu64 "): out_layer:%" PRIu64, m_id, layer_id);
    }
    return res;
}

int32_t HWCDisplay_simple::getActiveConfig(hwc2_config_t* out_config)
{
    *out_config = m_active_config;
    return HWC2_ERROR_NONE;
}

State HWCDisplay_simple::getState()
{
    return m_state;
}

void HWCDisplay_simple::setState(State state)
{
    m_state = state;
}

void HWCDisplay_simple::clearDirtyLayer()
{
    m_dirty_layers.clear();
}

int32_t HWCDisplay_simple::acceptChanges()
{
    clearDirtyLayer();
    setState(STATE_VALIDATED);

    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::getChangedCompositionTypes(uint32_t* out_num_elem, hwc2_layer_t* out_layers,
             int32_t* out_types)
{
    if (getState() == STATE_MODIFIED)
    {
        HWC_LOGW("(%" PRIu64 "): state is wrong[%d]", m_id, getState());
        return HWC2_ERROR_NOT_VALIDATED;
    }

    if (out_layers && out_types)
    {
        *out_num_elem = std::min(*out_num_elem, static_cast<uint32_t>(m_dirty_layers.size()));
        auto iter = m_dirty_layers.cbegin();
        for (uint32_t i = 0; i < *out_num_elem; i++)
        {
            out_layers[i] = iter->first;
            out_types[i] = HWC2_COMPOSITION_CLIENT;
            iter++;
        }
    }
    else
    {
        *out_num_elem = static_cast<uint32_t>(m_dirty_layers.size());
    }

    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::getColorMode(uint32_t* out_num_modes, int32_t* out_modes)
{
    if (out_modes)
    {
        *out_num_modes = std::min(*out_num_modes, static_cast<uint32_t>(m_color_modes.size()));
        for (uint32_t i = 0; i < *out_num_modes; i++)
        {
            out_modes[i] = m_color_modes[i];
        }
    }
    else
    {
        *out_num_modes = static_cast<uint32_t>(m_color_modes.size());
    }
    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::getConfig(uint32_t* out_num_configs, hwc2_config_t* out_configs)
{
    if (out_configs)
    {
        if (*out_num_configs > 0)
        {
            out_configs[0] = m_active_config;
            *out_num_configs = 1;
        }
    }
    else
    {
        *out_num_configs = 1;
    }
    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::getRequest(int32_t* out_display_requests, uint32_t* out_num_elem)
{
    if (out_display_requests)
    {
        *out_display_requests = 0;
    }
    if (out_num_elem)
    {
        *out_num_elem = 0;
    }
    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::getType(int32_t* out_type)
{
    *out_type = m_type;
    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::gerReleaseFence(uint32_t* out_num_elem, hwc2_layer_t* out_layer,
        int32_t* out_fence)
{
    UNUSED(out_layer);
    UNUSED(out_fence);
    if (out_num_elem)
    {
        *out_num_elem = 0;
    }
    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::present(int32_t* out_retire_fence)
{
    HWC_LOGD("HWCDisplay_simple %s", __func__);
    if (getState() != STATE_VALIDATED)
    {
        HWC_LOGW("(%" PRIu64 "): state is wrong[%d]", m_id, getState());
        return HWC2_ERROR_NOT_VALIDATED;
    }

    if (m_fence >= 0)
    {
        int res = sync_wait(m_fence, 1000);
        if (res < 0 && errno == ETIME)
        {
            HWC_LOGW("(%" PRIu64"): client target fence does not signal in 1000ms", m_id);
        }
        close(m_fence);
        m_fence = -1;
    }

    HWC_LOGV("call getHwDevice_simple()->postBuffer()+");
    getHwDevice_simple()->postBuffer(m_id, m_buffer, m_priv_hnd);

    if (out_retire_fence)
    {
        *out_retire_fence = m_present_fence;
        HWC_LOGV("*out_retire_fence: %d", m_present_fence);
        m_present_fence = -1;
    }
    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::setActiveConfig(hwc2_config_t config_id)
{
    if (config_id != m_active_config)
    {
        return HWC2_ERROR_BAD_CONFIG;
    }
    return HWC2_ERROR_NONE;
}

inline int getPrivateHandleInfo(
    buffer_handle_t handle, PrivateHnd* priv_handle)
{
    priv_handle->handle = handle;
    if (isForceMemcpy())
    {
        unsigned int y_stride;
        int err = gralloc_extra_query(handle, GRALLOC_EXTRA_GET_STRIDE, &y_stride);
        if (err != GRALLOC_EXTRA_OK)
        {
            static bool need_print_once = true;
            if (UNLIKELY(need_print_once))
            {
                // If use SW3D which do not have gralloc_extra
                HWC_LOGW("Failed to get stride from gralloc_extra, use 32 as default. May display corrupted image if not using SW3D");
                need_print_once = false;
            }
        }
        else
        {
            priv_handle->y_stride = y_stride;
        }
    }
    else
    {
        int err = gralloc_extra_query(handle, GRALLOC_EXTRA_GET_ION_FD, &priv_handle->ion_fd);
        err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_WIDTH, &priv_handle->width);
        err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_HEIGHT, &priv_handle->height);
        err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_STRIDE, &priv_handle->y_stride);
        //err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_VERTICAL_STRIDE, &priv_handle->vstride);
        err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_FORMAT, &priv_handle->format);
        err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_ALLOC_SIZE, &priv_handle->size);
        err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_ID, &priv_handle->alloc_id);
        if (err != GRALLOC_EXTRA_OK)
        {
            HWC_LOGE("%s gralloc_extra_query err(%x), (handle=%p) !!", __func__, err, handle);
            return -EINVAL;
        }
    }

    return 0;
}

int32_t HWCDisplay_simple::setClientTarget(buffer_handle_t handle, int32_t acquire_fence,
        int32_t dataspace, hwc_region_t damage)
{
    int ret = 0;
    UNUSED(dataspace);
    UNUSED(damage);

    m_buffer = handle;
    if (m_buffer != nullptr)
    {
        // Default y_stride align to 32, because SW3D always use 32 alignment stride,
        // but SW3D do not have gralloc_extra to tell display the y_stride,
        // so we need to do hard-code at here, when gralloc_extra_query y_stride failed,
        // means it is runing SW3D, use 32 alignment stride.  
        m_priv_hnd.y_stride = ALIGN(m_info.width, 32u);
        ret = getPrivateHandleInfo(m_buffer, &m_priv_hnd);
        HWC_LOGD("alloc_id:%" PRIu64 ", getIonfd:%d, format:%d, w:%d, h:%d, pith:%d, size:%d",
            m_priv_hnd.alloc_id, m_priv_hnd.ion_fd, m_priv_hnd.format, m_priv_hnd.width, m_priv_hnd.height,
            m_priv_hnd.y_stride, m_priv_hnd.size);
    }
    if (m_fence >= 0)
    {
        int res = close(m_fence);
        if (res != 0)
        {
            HWC_LOGW("(%" PRIu64 ")%s: failed to close fence=%d", m_id, __func__, m_fence);
        }
        m_fence = -1;
    }
    m_fence = acquire_fence;
    if (m_present_fence >= 0)
    {
        int res = close(m_present_fence);
        if (res != 0)
        {
            HWC_LOGW("(%" PRIu64 ")%s: failed to close pf=%d", m_id, __func__, m_present_fence);
        }
    }

    //if Display support present_fence ready
    if (!isForceMemcpy())
    {
        int32_t present_fence_fd = -1;
        uint32_t present_fence_index = 0;
        getHwDevice_simple()->prepareOverlayPresentFence(m_id, &present_fence_fd, &present_fence_index);//dxs
        if (present_fence_fd != -1)
        {
            m_present_fence = present_fence_fd;
            m_priv_hnd.fence_fd = present_fence_fd;
            m_priv_hnd.fence_idx = present_fence_index;
        }
        else
        {
            m_present_fence = dup(acquire_fence);//dxs
            m_priv_hnd.fence_idx = 0;
        }
    }
    else
    {
        //m_present_fence = dup(acquire_fence);
        m_present_fence = -1;
    }

    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::setColorMode(int32_t mode)
{
    for (auto item : m_color_modes)
    {
        if (item == mode)
        {
            return HWC2_ERROR_NONE;
        }
    }
    return HWC2_ERROR_BAD_PARAMETER;
}

int32_t HWCDisplay_simple::setColorTransform(const float* matrix, int32_t hint)
{
    UNUSED(matrix);
    UNUSED(hint);
    setState(STATE_MODIFIED);
    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::setOutputBuffer(buffer_handle_t buffer, int32_t release_fence)
{
    UNUSED(buffer);
    UNUSED(release_fence);
    return HWC2_ERROR_UNSUPPORTED;
}

int32_t HWCDisplay_simple::setPowerMode(int32_t mode)
{
    getHwDevice_simple()->setPowerMode(m_id, mode);
    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::validate(uint32_t* out_num_types, uint32_t* out_num_requests)
{
    *out_num_requests = 0;
    *out_num_types = static_cast<uint32_t>(m_dirty_layers.size());
    if (*out_num_types > 0)
    {
        setState(STATE_VALIDATED_WITH_CHANGES);
        return HWC2_ERROR_HAS_CHANGES;
    }
    setState(STATE_VALIDATED);
    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::setCursorPosition(hwc2_layer_t layer, int32_t x, int32_t y)
{
    UNUSED(layer);
    UNUSED(x);
    UNUSED(y);
    return HWC2_ERROR_BAD_LAYER;
}

int32_t HWCDisplay_simple::setLayerBuffer(hwc2_layer_t layer, buffer_handle_t buffer, int32_t acquire_fence)
{
    UNUSED(buffer);
    if (acquire_fence >= 0)
    {
        close(acquire_fence);
    }
    CHECK_LAYER(layer);
    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::setLayerSurfaceDamage(hwc2_layer_t layer, hwc_region_t damage)
{
    UNUSED(damage);
    CHECK_LAYER(layer);
    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::setLayerBlendMode(hwc2_layer_t layer, int32_t mode)
{
    UNUSED(mode);
    CHECK_LAYER(layer);
    setState(STATE_MODIFIED);
    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::setLayerColor(hwc2_layer_t layer, hwc_color_t color)
{
    UNUSED(color);
    CHECK_LAYER(layer);
    setState(STATE_MODIFIED);
    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::setLayerCompositionType(hwc2_layer_t layer, int32_t type)
{
    CHECK_LAYER(layer);
    if (type == HWC2_COMPOSITION_CLIENT)
    {
        auto iter = m_dirty_layers.find(layer);
        if (iter != m_dirty_layers.end())
        {
            m_dirty_layers.erase(iter);
        }
    }
    else
    {
        auto item = m_layers[layer];
        m_dirty_layers[item->getId()] = item;
    }
    setState(STATE_MODIFIED);
    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::setLayerDataSpace(hwc2_layer_t layer, int32_t dataspace)
{
    UNUSED(dataspace);
    CHECK_LAYER(layer);
    setState(STATE_MODIFIED);
    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::setLayerDisplayFrame(hwc2_layer_t layer, hwc_rect_t frame)
{
    UNUSED(frame);
    CHECK_LAYER(layer);
    setState(STATE_MODIFIED);
    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::setLayerPlaneAlpha(hwc2_layer_t layer, float alpha)
{
    UNUSED(alpha);
    CHECK_LAYER(layer);
    setState(STATE_MODIFIED);
    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::setLayerSidebandStream(hwc2_layer_t layer, const native_handle_t* stream)
{
    UNUSED(stream);
    CHECK_LAYER(layer);
    setState(STATE_MODIFIED);
    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::setLayerSourceCrop(hwc2_layer_t layer, hwc_frect_t crop)
{
    UNUSED(crop);
    CHECK_LAYER(layer);
    setState(STATE_MODIFIED);
    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::setLayerTransform(hwc2_layer_t layer, int32_t transform)
{
    UNUSED(transform);
    CHECK_LAYER(layer);
    setState(STATE_MODIFIED);
    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::setLayerVisibleRegion(hwc2_layer_t layer, hwc_region_t visible)
{
    UNUSED(visible);
    CHECK_LAYER(layer);
    setState(STATE_MODIFIED);
    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::setLayerZOrder(hwc2_layer_t layer, uint32_t z)
{
    UNUSED(z);
    CHECK_LAYER(layer);
    setState(STATE_MODIFIED);
    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::setLayerPerFrameMetadata(hwc2_layer_t layer, uint32_t num_elements,
        const int32_t* keys, const float* metadata)
{
    UNUSED(num_elements);
    UNUSED(keys);
    UNUSED(metadata);
    CHECK_LAYER(layer);
    return HWC2_ERROR_UNSUPPORTED;
}

int32_t HWCDisplay_simple::setLayerPerFrameMetadataBlobs(hwc2_layer_t layer, uint32_t num_elements,
        const int32_t* keys, const uint32_t* sizes, const uint8_t* metadata)
{
    UNUSED(num_elements);
    UNUSED(keys);
    UNUSED(sizes);
    UNUSED(metadata);
    CHECK_LAYER(layer);
    return HWC2_ERROR_UNSUPPORTED;
}

int32_t HWCDisplay_simple::setLayerBrightness(hwc2_layer_t layer, float brightness)
{
    UNUSED(brightness);
    CHECK_LAYER(layer);
    setState(STATE_MODIFIED);
    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::setLayerBlockingRegion(hwc2_layer_t layer, hwc_region_t region)
{
    UNUSED(region);
    CHECK_LAYER(layer);
    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::getClientTargetSupport(uint32_t width, uint32_t height,
             int32_t format, int32_t dataspace)
{
    UNUSED(dataspace);
    if (width != m_info.width)
    {
        return HWC2_ERROR_UNSUPPORTED;
    }
    if (height != m_info.height)
    {
        return HWC2_ERROR_UNSUPPORTED;
    }
    if (format != m_info.format)
    {
        return HWC2_ERROR_UNSUPPORTED;
    }
    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::getAttribute(hwc2_config_t config, int32_t attribute, int32_t* out_value)
{
    if (config != m_active_config)
    {
        return HWC2_ERROR_BAD_CONFIG;
    }

    switch (attribute) {
        case HWC2_ATTRIBUTE_WIDTH:
            *out_value = static_cast<int32_t>(m_info.width);
            break;
        case HWC2_ATTRIBUTE_HEIGHT:
            *out_value = static_cast<int32_t>(m_info.height);
            break;
        case HWC2_ATTRIBUTE_VSYNC_PERIOD:
            *out_value = static_cast<int32_t>(m_info.vsync_period);
            break;
        case HWC2_ATTRIBUTE_DPI_X:
            *out_value = static_cast<int32_t>(m_info.xdpi);
            break;
        case HWC2_ATTRIBUTE_DPI_Y:
            *out_value = static_cast<int32_t>(m_info.ydpi);
            break;
        default:
            return HWC2_ERROR_BAD_PARAMETER;
    }

    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::getCapabilities(uint32_t* out_num_capabilities, uint32_t* out_capabilities)
{
    UNUSED(out_capabilities);
    if (out_num_capabilities)
    {
        *out_num_capabilities = 0;
    }
    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::getName(uint32_t* out_lens, char* out_name)
{
    if (out_name)
    {
        *out_lens = static_cast<uint32_t>(m_info.name.copy(out_name, *out_lens));
    }
    else
    {
        *out_lens = static_cast<uint32_t>(m_info.name.size());
    }
    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::getPerFrameMetadataKey(uint32_t* out_num_keys, int32_t* out_keys)
{
    UNUSED(out_keys);
    if (out_num_keys)
    {
        *out_num_keys = 0;
    }
    return HWC2_ERROR_NONE;
}

void HWCDisplay_simple::setHotplugCallback(hwc2_function_pointer_t fptr, hwc2_callback_data_t data)
{
    m_callback_hotplug = reinterpret_cast<HWC2_PFN_HOTPLUG>(fptr);
    m_callback_data_hotplug = data;
}

void HWCDisplay_simple::setVsyncCallback(hwc2_function_pointer_t fptr, hwc2_callback_data_t data)
{
    m_callback_vsync = reinterpret_cast<HWC2_PFN_VSYNC>(fptr);
    m_callback_data_vsync = data;
    m_vsync.setCallback(m_callback_vsync, m_callback_data_vsync);
}

void HWCDisplay_simple::setVsyncCallback_2_4(hwc2_function_pointer_t fptr,
        hwc2_callback_data_t data)
{
    m_callback_vsync_2_4 = reinterpret_cast<HWC2_PFN_VSYNC_2_4>(fptr);
    m_callback_data_vsync_2_4 = data;
    m_vsync.setCallback_2_4(m_callback_vsync_2_4, m_callback_data_vsync_2_4);
}

int32_t HWCDisplay_simple::setVsyncEnabled(int32_t enabled)
{
    m_vsync.enableVsync(enabled);
    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::getDisplayVsyncPeriod(hwc2_vsync_period_t* out_vsync_period)
{
    if (out_vsync_period == nullptr)
    {
        HWC_LOGW("%s: out_vsync_period is null", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    *out_vsync_period = static_cast<hwc2_vsync_period_t>(m_info.vsync_period);
    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::setActiveConfigWithConstraints(hwc2_config_t config,
        hwc_vsync_period_change_constraints_t* vsync_period_change_constraints,
        hwc_vsync_period_change_timeline_t* out_timeline)
{
    if (vsync_period_change_constraints == nullptr)
    {
        HWC_LOGW("%s: vsync_period_change_constraints is null", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }
    if (out_timeline == nullptr)
    {
        HWC_LOGW("%s: out_timeline is null", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    if (config != m_active_config)
    {
        HWC_LOGE("%s: set invalid display config:%d", __func__, config);
        return HWC2_ERROR_BAD_CONFIG;
    }

    nsecs_t now = systemTime();
    out_timeline->newVsyncAppliedTimeNanos = std::max(now,
            vsync_period_change_constraints->desiredTimeNanos);
    out_timeline->refreshRequired = true;
    out_timeline->refreshTimeNanos = out_timeline->newVsyncAppliedTimeNanos;
    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::setExpectedPresentTime(int64_t timestamp_nanos)
{
    UNUSED(timestamp_nanos);
    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::setBootDisplayConfig(hwc2_config_t config)
{
    if (config != m_active_config)
    {
        return HWC2_ERROR_BAD_CONFIG;
    }
    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::clearBootDisplayConfig()
{
    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::getPreferredBootDisplayConfig(hwc2_config_t* config)
{
    *config = m_active_config;
    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::getDisplayDecorationSupport(
        std::optional<common::DisplayDecorationSupport>* support)
{
    UNUSED(support);
    // if platform does not support this feature, the value of support should be null.
    // therefore we do not need assign any value to support.
    return HWC2_ERROR_NONE;
}

int32_t HWCDisplay_simple::setIdleTimerEnabled(int32_t timeout)
{
    UNUSED(timeout);
    return HWC2_ERROR_UNSUPPORTED;
}

int32_t HWCDisplay_simple::getDisplayPhysicalOrientation(common::Transform* orientation)
{
    UNUSED(orientation);
    return HWC2_ERROR_UNSUPPORTED;
}

int32_t HWCDisplay_simple::setLayerColorTransform(hwc2_layer_t layer, const float* matrix)
{
    UNUSED(matrix);
    CHECK_LAYER(layer);
    setState(STATE_MODIFIED);
    return HWC2_ERROR_NONE;
}

}  // namespace simplehwc

