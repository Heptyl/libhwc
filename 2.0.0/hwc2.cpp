#define DEBUG_LOG_TAG "HWC"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS
#define LOG_TAG "hwcomposer"
#include <cstring>
#include <sstream>
#include <cutils/properties.h>
#include <sync.h>

#include "gralloc_mtk_defs.h"

#include <hwc_feature_list.h>

#include "utils/debug.h"
#include "utils/tools.h"
#include "utils/perfhelper.h"

#include "hwc2.h"
#include "platform_wrap.h"
#include "display.h"
#include "overlay.h"
#include "data_express.h"
#include "dispatcher.h"
#include "worker.h"
#include "composer.h"
#include "bliter_ultra.h"
#include "asyncblitdev.h"
#include "sync.h"
#include "pq_interface.h"
#include "led_device.h"
#include "index_buffer_generator.h"

#include "ai_blulight_defender.h"
#include "display_dump.h"
#include "glai_controller.h"
#include "grallocdev.h"

#include "utils/transform.h"
#include "ui/gralloc_extra.h"
#include <utils/SortedVector.h>
#include <utils/String8.h>
#include "hwc2_simple_Api.h"
#include <dlfcn.h>

using namespace aidl::android::hardware::graphics;

int32_t checkMirrorPath(const vector<sp<HWCDisplay> >& displays);

bool isDispConnected(const uint64_t& display)
{
    return DisplayManager::getInstance().getDisplayData(display)->connected;
}

// -----------------------------------------------------------------------------

DisplayListener::DisplayListener(
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
    const hwc2_callback_data_t callback_seamless_possbile_data)
    : m_callback_hotplug(callback_hotplug)
    , m_callback_hotplug_data(callback_hotplug_data)
    , m_callback_vsync(callback_vsync)
    , m_callback_vsync_data(callback_vsync_data)
    , m_callback_vsync_2_4(callback_vsync_2_4)
    , m_callback_vsync_2_4_data(callback_vsync_2_4_data)
    , m_callback_refresh(callback_refresh)
    , m_callback_refresh_data(callback_refresh_data)
    , m_callback_vsync_timing_changed(callback_vsync_timing_changed)
    , m_callback_vsync_timing_changed_data(callback_vsync_timing_changed_data)
    , m_callback_seamless_possible(callback_seamless_possible)
    , m_callback_seamless_possible_data(callback_seamless_possbile_data)
{
}

void DisplayListener::onVSync(uint64_t dpy, nsecs_t timestamp, bool enabled)
{
    std::lock_guard<std::mutex> lock(m_vsync_lock);
    if (dpy != HWC_DISPLAY_EXTERNAL && dpy != HWC_DISPLAY_EXTERNAL_1 && dpy != HWC_DISPLAY_VIRTUAL && enabled)
    {
        if (m_callback_vsync)
        {
            m_callback_vsync(m_callback_vsync_data, dpy, timestamp);
        }
        else if (m_callback_vsync_2_4)
        {
            sp<HWCDisplay> hwc_display = HWCMediator::getInstance().getHWCDisplay(dpy);
            hwc2_vsync_period_t period = static_cast<hwc2_vsync_period_t>(
                    hwc_display->getNextVsyncPeriod(timestamp));
            m_callback_vsync_2_4(m_callback_vsync_2_4_data, dpy, timestamp, period);
        }
    }

    HWCDispatcher::getInstance().onVSync(dpy);
}

void DisplayListener::onPlugIn(uint64_t dpy, bool is_internal, uint32_t width, uint32_t height)
{
    sp<HWCDisplay> hwc_display = HWCMediator::getInstance().getHWCDisplay(dpy);

    HWCDispatcher::getInstance().onPlugIn(dpy, hwc_display->getDrmIdCurCrtc(), hwc_display->getDrmIdConnector(),
                                          is_internal, width, height);
    hwc_display->initializeAppliedPeriod();
    HrtHelper::getInstance().onHotplug(dpy, true);
}

void DisplayListener::onPlugOut(uint64_t dpy)
{
    sp<HWCDisplay> hwc_display = HWCMediator::getInstance().getHWCDisplay(dpy);

    HWCDispatcher::getInstance().onPlugOut(dpy, hwc_display->getDrmIdCurCrtc());
    HrtHelper::getInstance().onHotplug(dpy, false);
}

void DisplayListener::onHotPlugExt(uint64_t dpy, int connected)
{
    if (m_callback_hotplug && dpy != HWC_DISPLAY_VIRTUAL)
    {
        DisplayManager::getInstance().genDisplayIdForDisplay(dpy);
        m_callback_hotplug(m_callback_hotplug_data, dpy, connected);
    }
}

void DisplayListener::onRefresh(uint64_t dpy)
{
    std::lock_guard<std::mutex> lock(m_refresh_lock);
    if (m_callback_refresh)
    {
        m_callback_refresh(m_callback_refresh_data, dpy);
    }
}

void DisplayListener::onRefresh(uint64_t dpy, unsigned int type)
{
    std::lock_guard<std::mutex> lock(m_refresh_lock);
    if (m_callback_refresh)
    {
        HWC_LOGI("(%" PRIu64 ")fire a callback of refresh to SF[%u]", dpy, type);
        m_callback_refresh(m_callback_refresh_data, dpy);
    }
    for (uint64_t i = 0; i < DisplayManager::MAX_DISPLAYS; i++)
    {
        sp<HWCDisplay> hwc_display = HWCMediator::getInstance().getHWCDisplay(i);
        if (hwc_display != nullptr && hwc_display->isConnected())
        {
            hwc_display->onRefresh(type);
        }
    }
}

void DisplayListener::onVSyncPeriodTimingChange(uint64_t dpy, int64_t applied_time,
             uint8_t refresh_required, int64_t refresh_time)
{
    std::lock_guard<std::mutex> lock(m_vsync_timing_changed_lock);
    if (m_callback_vsync_timing_changed)
    {
        hwc_vsync_period_change_timeline_t change_timeline;
        change_timeline.newVsyncAppliedTimeNanos = applied_time;
        change_timeline.refreshRequired = refresh_required;
        change_timeline.refreshTimeNanos = refresh_time;
        m_callback_vsync_timing_changed(m_callback_vsync_timing_changed_data, dpy,
                &change_timeline);
    }
}

void DisplayListener::onSeamlessPossible(uint64_t dpy)
{
    std::lock_guard<std::mutex> lock(m_seamless_possible_lock);
    if (m_callback_seamless_possible)
    {
        m_callback_seamless_possible(m_callback_seamless_possible_data, dpy);
    }
}

void DisplayListener::setCallbackVsync(
    HWC2_PFN_VSYNC callback_vsync,
    hwc2_callback_data_t callback_vsync_data)
{
    std::lock_guard<std::mutex> lock(m_vsync_lock);
    m_callback_vsync= callback_vsync;
    m_callback_vsync_data = callback_vsync_data;
}

void DisplayListener::setCallbackRefresh(
    HWC2_PFN_REFRESH callback_refresh,
    hwc2_callback_data_t callback_refresh_data)
{
    std::lock_guard<std::mutex> lock(m_refresh_lock);
    m_callback_refresh = callback_refresh;
    m_callback_refresh_data = callback_refresh_data;
}

void DisplayListener::setCallbackVsync_2_4(
    HWC2_PFN_VSYNC_2_4 callback_vsync_2_4,
    hwc2_callback_data_t callback_vsync_2_4_data)
{
    std::lock_guard<std::mutex> lock(m_vsync_lock);
    m_callback_vsync_2_4 = callback_vsync_2_4;
    m_callback_vsync_2_4_data = callback_vsync_2_4_data;
}

void DisplayListener::setCallbackVsyncPeriodTimingChanged(
    HWC2_PFN_VSYNC_PERIOD_TIMING_CHANGED callback_vsync_timing_changed,
    hwc2_callback_data_t callback_vsync_timing_changed_data)
{
    std::lock_guard<std::mutex> lock(m_vsync_timing_changed_lock);
    m_callback_vsync_timing_changed = callback_vsync_timing_changed;
    m_callback_vsync_timing_changed_data = callback_vsync_timing_changed_data;
}

void DisplayListener::setCallbackSeamlessPossible(
    HWC2_PFN_SEAMLESS_POSSIBLE callback_seamless_possible,
    hwc2_callback_data_t callback_seamless_possbile_data)
{
    std::lock_guard<std::mutex> lock(m_seamless_possible_lock);
    m_callback_seamless_possible = callback_seamless_possible;
    m_callback_seamless_possible_data = callback_seamless_possbile_data;
}

// -----------------------------------------------------------------------------

HWC2Api* getHWCMediator_simple() {
    const char *error = nullptr;
    typedef HWC2Api* (*getHWCSimplePrototype)();
    HWC2Api* mSimpleHwcInstance = nullptr;
    void* mSimpleHwcHandle = nullptr;
    ALOGI("getHWCMediator_simple()-call dlopen()+");
    mSimpleHwcHandle = dlopen("libsimplehwc.so", RTLD_LAZY);
    error = dlerror();
    if (mSimpleHwcHandle)
    {
        ALOGI("getHWCMediator_simple()-call dlsym()+");
        getHWCSimplePrototype creatPtr = reinterpret_cast<getHWCSimplePrototype>(dlsym(mSimpleHwcHandle, "getHWCMediatorSimple"));
        error = dlerror();
        if (creatPtr)
        {
            mSimpleHwcInstance = creatPtr();
            if (mSimpleHwcInstance)
            {
                return mSimpleHwcInstance;
            }
        }
        else
        {
            if (error != NULL)
            {
                ALOGE("%s(), dlsym %s failed. (%s)\n", __FUNCTION__, "getHWCMediatorSimple()", error);
            }
        }
    }
    else
    {
        if (error != NULL)
        {
            ALOGE("%s(), dlopen %s failed. (%s)\n", __FUNCTION__, "libsimplehwc.so", error);
        }
    }
    return nullptr;
}

/*
void deinitHWCMediator_simple() {
    if (mHwcSimpleInstance) {
        delete mHwcSimpleInstance;
    }
    if (mSimpleHwcHandle) {
        dlclose(mSimpleHwcHandle);
    }
}*/

HWC2Api* getHWCMediator()
{
    if (HwcFeatureList::getInstance().getFeature().is_enable_simplehwc)
    {
        char value[PROPERTY_VALUE_MAX] = {0};
        bool force_full_hwc = false;
        property_get("vendor.debug.hwc.force_fullhwc", value, "-1");
        ALOGI("getprop [vendor.debug.hwc.force_fullhwc] %s", value);
        if (-1 != atoi(value))
        {
            force_full_hwc = atoi(value);
        }

        if(!force_full_hwc)
        {
            HWC2Api* mHwcSimpleInstance = getHWCMediator_simple();
            if (mHwcSimpleInstance != nullptr)
            {
                ALOGI("getHWCMediator use simple-hwc");
                return mHwcSimpleInstance;
            }
        }
    }

    ALOGD("getHWCMediator default use full-hwc");
    return &HWCMediator::getInstance();
}

HWC2Api* g_hwc2_api = getHWCMediator();

#define CHECK_DISP(display) \
    do { \
        if (display >= DisplayManager::MAX_DISPLAYS) { \
            HWC_LOGE("%s: this display(%" PRIu64 ") is invalid", __FUNCTION__, display); \
            return HWC2_ERROR_BAD_DISPLAY; \
        } \
        if (display >= m_displays.size()) { \
            HWC_LOGE("%s: this display(%" PRIu64 ") is invalid, display size is %zu", \
                     __FUNCTION__, display, m_displays.size()); \
            return HWC2_ERROR_BAD_DISPLAY; \
        } \
    } while(0)

#define CHECK_DISP_CONNECT(display) \
    do { \
        CHECK_DISP(display); \
        if (!isDispConnected(display)) { \
            HWC_LOGE("%s: the display(%" PRIu64 ") is not connected", __FUNCTION__, display); \
            return HWC2_ERROR_BAD_DISPLAY; \
        } \
    } while(0)

#define CHECK_DISP_LAYER(display, layer, hwc_layer) \
    do { \
        if (hwc_layer == nullptr) { \
            HWC_LOGE("%s: the display(%" PRIu64 ") does not contain layer(%" PRIu64 ")", __FUNCTION__, display, layer); \
            return HWC2_ERROR_BAD_LAYER; \
        } \
    } while(0)

// Wrap platform config switch in macro for expandability
#define PLAT_SWITCH_SET_CLEAR_BIT(name) \
    do { \
        property_get("vendor.debug.hwc."#name, value, "-1"); \
        if (-1 != atoi(value)) \
        { \
            Platform::getInstance().m_config.name = static_cast<unsigned int>(strtoul(value, nullptr, 0)); \
            if (property_set("vendor.debug.hwc."#name, "-1") < 0) \
            { \
                HWC_LOGI("failed to set vendor.debug.hwc."#name); \
            } \
        } \
        property_get("vendor.debug.hwc."#name"_set_bit", value, "-1"); \
        if (atoi(value) > -1 && atoi(value) < 32) \
        { \
            Platform::getInstance().m_config.name |= (1 << atoi(value)); \
            if (property_set("vendor.debug.hwc."#name"_set_bit", "-1") < 0) \
            { \
                HWC_LOGI("failed to set vendor.debug.hwc."#name"_set_bit"); \
            } \
        } \
        property_get("vendor.debug.hwc."#name"_clear_bit", value, "-1"); \
        if (atoi(value) > -1 && atoi(value) < 32) \
        { \
            Platform::getInstance().m_config.name &= ~(1 << atoi(value)); \
            if (property_set("vendor.debug.hwc."#name"_clear_bit", "-1") < 0) \
            { \
                HWC_LOGI("failed to set vendor.debug.hwc."#name"_clear_bit"); \
            } \
        } \
    } while(0)



HWCMediator& HWCMediator::getInstance()
{
    static HWCMediator gInstance;
    return gInstance;
}

HWCMediator::HWCMediator()
    : m_need_validate(HWC_SKIP_VALIDATE_NOT_SKIP)
    , m_is_mtk_aosp(false)
    , m_low_latency_wfd(false)
    , m_compression_type(HWC_COMPRESSION_TYPE_NONE)
    , m_compression_modifier(0)
    , m_is_validate_separate(false)
    , m_next_display_usage(ComposerExt::DisplayUsage::kUnknown)
    , m_is_init_disp_manager(false)
    , m_callback_hotplug(nullptr)
    , m_callback_hotplug_data(nullptr)
    , m_callback_vsync(nullptr)
    , m_callback_vsync_data(nullptr)
    , m_callback_refresh(nullptr)
    , m_callback_refresh_data(nullptr)
    , m_prev_hwbinder_tid(0)
    , m_present_idx_action(-1)
    , m_present_idx_display_id(HWC_DISPLAY_PRIMARY)
    , m_present_idx_width(0)
    , m_present_idx_height(0)
    , m_present_idx_x(0)
    , m_present_idx_y(0)
    , m_version_major(2)
    , m_version_minor(0)
    , m_version_patch(0)
    , m_gpuc_skip_validate(false)
{
    sp<IOverlayDevice> primary_disp_dev = getHwDevice();
    sp<IOverlayDevice> virtual_disp_dev = nullptr;
    if (Platform::getInstance().m_config.blitdev_for_virtual)
    {
        virtual_disp_dev = new AsyncBlitDevice();
    }
    else
    {
        virtual_disp_dev = primary_disp_dev;
    }
    m_disp_devs.resize(4, primary_disp_dev);
    m_disp_devs[HWC_DISPLAY_VIRTUAL] = virtual_disp_dev;

    if (primary_disp_dev->isDispAodForceDisable())
    {
        HwcFeatureList::getInstance().getEditableFeature().aod = 0;
        HWC_LOGI("force to disable aod feature by caps");
    }

    Debugger::getInstance();
    Debugger::getInstance().m_logger = new Debugger::LOGGER(DisplayManager::MAX_DISPLAYS);

    if (primary_disp_dev->isSupportSeparateHrt())
    {
        if (Platform::getInstance().m_config.mirror_state & MIRROR_DISABLED)
        {
            // set vendor.debug.sf.validate_separate if platform support validate separately
            if (property_set("vendor.debug.sf.validate_separate", "1") < 0)
            {
                HWC_LOGE("Ignore isSupportSeparateHrt, because failed to set vendor.debug.sf.validate_separate");
                HWC_ASSERT(0);
            }
            else
            {
                m_is_validate_separate = true;
            }
        }
        else
        {
            HWC_LOGW("Ignore isSupportSeparateHrt, because the platform need to support mirror feature");
        }
    }
    initHWCDisplay();
    /*
    // check if virtual display could be composed by hwc
    status_t err = DispDevice::getInstance().createOverlaySession(HWC_DISPLAY_VIRTUAL);
    m_is_support_ext_path_for_virtual = (err == NO_ERROR);
    DispDevice::getInstance().destroyOverlaySession(HWC_DISPLAY_VIRTUAL);
    */

    m_capabilities.clear();
    // When primary display is external, it should not support skip validate feature.
    bool is_primary_internal = true;
    for (auto& hwc_display : m_displays)
    {
        if (hwc_display->getId() == HWC_DISPLAY_PRIMARY)
        {
            if (!hwc_display->isInternal())
            {
                is_primary_internal = false;
            }
            break;
        }
    }
    char value[PROPERTY_VALUE_MAX] = {0};
    property_get("vendor.debug.hwc.is_skip_validate", value, "-1");
    if ((-1 != atoi(value)) && is_primary_internal)
    {
        Platform::getInstance().m_config.is_skip_validate = atoi(value);
    }

    if ((Platform::getInstance().m_config.is_skip_validate == 1) && is_primary_internal)
    {
        m_capabilities.push_back(HWC2_CAPABILITY_SKIP_VALIDATE);
    }

    if (HwcFeatureList::getInstance().getFeature().is_support_pq &&
        Platform::getInstance().m_config.support_color_transform &&
        Platform::getInstance().m_config.check_skip_client_color_transform &&
        getOvlDevice(HWC_DISPLAY_PRIMARY)->isDisp3X4DisplayColorTransformSupported())
    {
        m_capabilities.push_back(HWC2_CAPABILITY_SKIP_CLIENT_COLOR_TRANSFORM);
    }

    property_get("ro.surface_flinger.force_hwc_copy_for_virtual_displays", value, "false");
    if (!strncmp(value, "true", strlen("true")))
    {
        HwcFeatureList::getInstance().getEditableFeature().copyvds = 1;
    }

    if (HwcFeatureList::getInstance().getFeature().hdr_display || Platform::getInstance().m_config.ethdr_meta_support)
    {
        setupHDRFeature();
    }

    // set hwc pid to property for watchdog
    char buf[16];
    if (snprintf(buf, sizeof(buf), "%d", getpid()) > 0) {
        if (property_set("vendor.debug.sf.hwc_pid", buf) < 0) {
            HWC_LOGI("failed to set HWC pid to debug.sf.hwc_pid");
        }
    } else {
        HWC_LOGE("failed to build value for setting vendor.debug.sf.hwc_pid");
    }

    // init perfhelper to avoid blocking common flow
    PerfHelper::getInstance();

    int error = ComposerExt::DeviceInterface::registerDevice(this);
    if (error)
    {
        HWC_LOGW("Could not register IComposerExt as service (%d).", error);
    }
    else
    {
        HWC_LOGI("IComposerExt service registration completed.");
    }

    updatePlatformConfig(true);

    // avoid MM layer taking much time to reallocate at the first time
    GrallocDevice::getInstance();
}

HWCMediator::~HWCMediator()
{
}

void HWCMediator::initHWCDisplay()
{
    std::vector<CreateDisplayInfo> create_disp_infos;
    getHwDevice()->getCreateDisplayInfos(create_disp_infos);

    HWC_LOGI("%s(), create_disp_infos size %zu", __FUNCTION__, create_disp_infos.size());

    for (const CreateDisplayInfo& info : create_disp_infos)
    {
        HWC_LOGI("%s(), is_internal %d, disp_type %d, crtc id %u, connector id %u, pipe %u",
                 __FUNCTION__,
                 info.is_internal, info.disp_type,
                 info.drm_id_crtc_default, info.drm_id_connector,
                 info.drm_id_crtc_pipe);
    }

    if (create_disp_infos.size() > 0)
    {
        const CreateDisplayInfo* primary_disp_info = nullptr;
        const CreateDisplayInfo* external_disp_info = nullptr;
        const CreateDisplayInfo* virtual_disp_info = nullptr;
        const CreateDisplayInfo* external_1_disp_info = nullptr;
        const CreateDisplayInfo* temp_disp_info = nullptr;

        std::vector<const CreateDisplayInfo*> dynamic_disp_infos;

        // get info of first 3 display, and dynamic allocate display
        for (const CreateDisplayInfo& info : create_disp_infos)
        {
            if (primary_disp_info == nullptr &&
                info.drm_id_crtc_pipe == HWC_DISPLAY_PRIMARY &&
                info.disp_type == HWC2_DISPLAY_TYPE_PHYSICAL)
            {
                primary_disp_info = &info;
                continue;
            }

            if (external_disp_info == nullptr &&
                info.drm_id_crtc_pipe == HWC_DISPLAY_EXTERNAL &&
                info.disp_type == HWC2_DISPLAY_TYPE_PHYSICAL)
            {
                external_disp_info = &info;
                continue;
            }

            if (external_1_disp_info == nullptr &&
                info.drm_id_crtc_pipe == HWC_DISPLAY_EXTERNAL_1 &&
                info.disp_type == HWC2_DISPLAY_TYPE_PHYSICAL)
            {
                external_1_disp_info = &info;
                continue;
            }

            if (virtual_disp_info == nullptr &&
                info.is_internal == false &&
                info.disp_type == HWC2_DISPLAY_TYPE_VIRTUAL)
            {
                virtual_disp_info = &info;
                continue;
            }

            dynamic_disp_infos.push_back(&info);
        }

        if (primary_disp_info)
        {
            if (HwcFeatureList::getInstance().getFeature().sub_as_main)
            {
                if (external_disp_info != nullptr)
                {
                    temp_disp_info = primary_disp_info;
                    primary_disp_info = external_disp_info;
                    external_disp_info = temp_disp_info;
                }
                else
                {
                    HWC_LOGE("%s(), external_disp_info is NULL", __FUNCTION__);
                    abort();
                }
            }

            m_displays.push_back(new HWCDisplay(HWC_DISPLAY_PRIMARY, *primary_disp_info,
                                 m_disp_devs[HWC_DISPLAY_PRIMARY]));
        }
        else
        {
            if ((external_disp_info != nullptr) && (create_disp_infos.size() >= 1))
            {
                // sub_as_main scenario
                primary_disp_info = external_disp_info;
                external_disp_info = nullptr;

                m_displays.push_back(new HWCDisplay(HWC_DISPLAY_PRIMARY, *primary_disp_info,
                                 m_disp_devs[HWC_DISPLAY_PRIMARY]));
            }
            else
            {
                HWC_LOGE("%s(), no primary display found", __FUNCTION__);
                abort();
            }
        }

        if (external_disp_info)
        {
            m_displays.push_back(new HWCDisplay(HWC_DISPLAY_EXTERNAL, *external_disp_info,
                                 m_disp_devs[HWC_DISPLAY_EXTERNAL]));
        }
        else
        {
            HWC_LOGW("%s(), no external display found", __FUNCTION__);
            m_displays.push_back(new HWCDisplay(HWC_DISPLAY_EXTERNAL,
                                 {
                                     .is_internal = false,
                                     .disp_type = HWC2_DISPLAY_TYPE_PHYSICAL,
                                     .drm_id_crtc_default = UINT32_MAX,
                                     .drm_id_connector = UINT32_MAX,
                                     .drm_id_crtc_pipe = UINT32_MAX,
                                 },
                                 m_disp_devs[HWC_DISPLAY_EXTERNAL]));
        }

        if (virtual_disp_info)
        {
            m_displays.push_back(new HWCDisplay(HWC_DISPLAY_VIRTUAL, *virtual_disp_info,
                                 m_disp_devs[HWC_DISPLAY_VIRTUAL]));
        }
        else
        {
            HWC_LOGW("%s(), no virtual display found", __FUNCTION__);
            m_displays.push_back(new HWCDisplay(HWC_DISPLAY_VIRTUAL,
                                 {
                                     .is_internal = false,
                                     .disp_type = HWC2_DISPLAY_TYPE_VIRTUAL,
                                     .drm_id_crtc_default = UINT32_MAX,
                                     .drm_id_connector = UINT32_MAX,
                                     .drm_id_crtc_pipe = UINT32_MAX,
                                 },
                                 m_disp_devs[HWC_DISPLAY_VIRTUAL]));
        }

        if (external_1_disp_info)
        {
            m_displays.push_back(new HWCDisplay(HWC_DISPLAY_EXTERNAL_1, *external_1_disp_info,
                                 m_disp_devs[HWC_DISPLAY_EXTERNAL_1]));
        }
        else
        {
            HWC_LOGW("%s(), no external_1 display found", __FUNCTION__);
            m_displays.push_back(new HWCDisplay(HWC_DISPLAY_EXTERNAL_1,
                                 {
                                     .is_internal = false,
                                     .disp_type = HWC2_DISPLAY_TYPE_PHYSICAL,
                                     .drm_id_crtc_default = UINT32_MAX,
                                     .drm_id_connector = UINT32_MAX,
                                     .drm_id_crtc_pipe = UINT32_MAX,
                                 },
                                 m_disp_devs[HWC_DISPLAY_EXTERNAL_1]));
        }

        // dynamic display
        int dynamic_cur_id = HWC_DISPLAY_EXTERNAL_1 + 1;
        for (const CreateDisplayInfo* info : dynamic_disp_infos)
        {
            if (dynamic_cur_id >= DisplayManager::MAX_DISPLAYS)
            {
                break;
            }

            // decide ovl for this display
            sp<IOverlayDevice> ovl = info->disp_type == HWC2_DISPLAY_TYPE_PHYSICAL ?
                                     m_disp_devs[HWC_DISPLAY_PRIMARY] :
                                     m_disp_devs[HWC_DISPLAY_VIRTUAL];

            m_displays.push_back(new HWCDisplay(static_cast<uint64_t>(dynamic_cur_id), *info, ovl));
            m_disp_devs.push_back(ovl);

            dynamic_cur_id++;
        }
    }
    else
    {
        CreateDisplayInfo create_disp_info;
        create_disp_info.drm_id_crtc_default = UINT32_MAX;
        create_disp_info.drm_id_connector = UINT32_MAX;
        create_disp_info.drm_id_crtc_pipe = UINT32_MAX;

        create_disp_info.is_internal = true;
        create_disp_info.disp_type = HWC2_DISPLAY_TYPE_PHYSICAL;
        m_displays.push_back(new HWCDisplay(HWC_DISPLAY_PRIMARY, create_disp_info,
                                            m_disp_devs[HWC_DISPLAY_PRIMARY]));

        create_disp_info.is_internal = false;
        create_disp_info.disp_type = HWC2_DISPLAY_TYPE_PHYSICAL;
        m_displays.push_back(new HWCDisplay(HWC_DISPLAY_EXTERNAL, create_disp_info,
                                            m_disp_devs[HWC_DISPLAY_EXTERNAL]));

        create_disp_info.is_internal = false;
        create_disp_info.disp_type = HWC2_DISPLAY_TYPE_VIRTUAL;
        m_displays.push_back(new HWCDisplay(HWC_DISPLAY_VIRTUAL, create_disp_info,
                                            m_disp_devs[HWC_DISPLAY_VIRTUAL]));

    }
    HrtHelper::getInstance().init(m_displays, m_disp_devs, m_is_validate_separate);
}

bool HWCMediator::supportApiFunction(MTK_HWC_API api)
{
    switch (api)
    {
        case MTK_HWC_API_RENDER_INTENT:
            // if we do not support this API in composer3, it caused the VTS fail. therefore
            // we always support this API in composer3.
            if (m_version_major >= 3)
            {
                return true;
            }
            else
            {
                return getPqDevice()->supportPqXml();
            }
    }
    return false;
}

void HWCMediator::open(/*hwc_private_device_t* device*/)
{
}

void HWCMediator::close(/*hwc_private_device_t* device*/)
{
}

void HWCMediator::getCapabilities(
        uint32_t* out_count,
        int32_t* /*hwc2_capability_t*/ out_capabilities)
{
    if (out_capabilities == NULL)
    {
        *out_count = static_cast<uint32_t>(m_capabilities.size());
        return;
    }

    for(uint32_t i = 0; i < *out_count; ++i)
    {
        out_capabilities[i] = m_capabilities[i];
    }
}

bool HWCMediator::hasCapabilities(int32_t capability)
{
    for (size_t i = 0; i < m_capabilities.size(); ++i)
    {
        if (m_capabilities[i] == capability)
        {
            return true;
        }
    }

    return false;
}

void HWCMediator::createExternalDisplay(uint64_t dpy)
{
    if (getHWCDisplay(dpy)->isConnected())
    {
        HWC_LOGE("external display is already connected %s", __func__);
        abort();
    }
    else
    {
        if (getHwDevice()->isSupportDynamicSwitch() && dpy == HWC_DISPLAY_EXTERNAL)
        {
            std::vector<CreateDisplayInfo> create_disp_infos;
            getHwDevice()->getCreateDisplayInfos(create_disp_infos);

            HWC_LOGI("%s(), create_disp_infos size %zu", __FUNCTION__, create_disp_infos.size());

            for (const CreateDisplayInfo& info : create_disp_infos)
            {
                if (dpy == info.drm_id_crtc_pipe)
                {
                    HWC_LOGI("%s(), is_internal %d, disp_type %d, crtc id %u, connector id %u, pipe %u",
                             __FUNCTION__,
                             info.is_internal, info.disp_type,
                             info.drm_id_crtc_default, info.drm_id_connector,
                             info.drm_id_crtc_pipe);

                    sp<HWCDisplay> hwc_display = HWCMediator::getInstance().getHWCDisplay(dpy);
                    hwc_display->updateDisplayInfo(info);
                }
            }
        }
        getHWCDisplay(dpy)->init();
    }
}

void HWCMediator::destroyExternalDisplay(uint64_t dpy)
{
    if (getHWCDisplay(dpy)->isConnected())
    {
        HWC_LOGE("external display is not disconnected %s", __func__);
        abort();
    }
    else
    {
        getHWCDisplay(dpy)->clear();
    }
}

int32_t HWCMediator::setHwcVersion(
    uint32_t major,
    uint32_t minor,
    uint32_t patch)
{
    m_version_major = major;
    m_version_minor = minor;
    m_version_patch = patch;
    return HWC2_ERROR_NONE;
}

void HWCMediator::getHwcVersion(
    uint32_t* major,
    uint32_t* minor,
    uint32_t* patch)
{
    if (major != nullptr)
    {
        *major = m_version_major;
    }
    if (minor != nullptr)
    {
        *minor = m_version_minor;
    }
    if (patch != nullptr)
    {
        *patch = m_version_patch;
    }
}

/* Device functions */
int32_t /*hwc2_error_t*/ HWCMediator::deviceCreateVirtualDisplay(
    hwc2_device_t* /*device*/,
    uint32_t width,
    uint32_t height,
    int32_t* /*android_pixel_format_t*/ format,
    hwc2_display_t* outDisplay)
{

    if ((NULL == format) || (NULL == outDisplay))
    {
        return HWC2_ERROR_BAD_PARAMETER;
    }

    *outDisplay = HWC_DISPLAY_VIRTUAL;

    if (*outDisplay >= m_displays.size())
    {
        HWC_LOGE("%s: this display(%" PRIu64 ") is invalid, display size is %zu",
                 __func__, *outDisplay, m_displays.size());
        return HWC2_ERROR_BAD_DISPLAY;
    }

    DisplayManager::getInstance().setUsage(*outDisplay, getNextDisplayUsage());
    setNextDisplayUsage(ComposerExt::DisplayUsage::kUnknown); // Clear the flag

    sp<HWCDisplay> hwc_display = getHWCDisplay(*outDisplay);

    if (hwc_display->getDrmIdCurCrtc() == UINT32_MAX)
    {
        HWC_LOGE("%s: this display(%" PRIu64 ") is invalid, hwc_display->getDrmIdCurCrtc() %u",
                 __func__, *outDisplay, hwc_display->getDrmIdCurCrtc());
        return HWC2_ERROR_BAD_DISPLAY;
    }

    if (hwc_display->isConnected())
    {
        return HWC2_ERROR_NO_RESOURCES;
    }

    const bool is_wfd = DisplayManager::getInstance().checkIsWfd(*outDisplay);

    if (Platform::getInstance().m_config.only_wfd_by_hwc && !is_wfd)
    {
        return HWC2_ERROR_NO_RESOURCES;
    }

    if (width > HWCMediator::getInstance().getOvlDevice(HWC_DISPLAY_PRIMARY)->getMaxOverlayWidth() ||
        height > HWCMediator::getInstance().getOvlDevice(HWC_DISPLAY_PRIMARY)->getMaxOverlayHeight())
    {
        HWC_LOGI("(%" PRIu64 ") %s hwc not support width:%u x %u limit: %u x %u", *outDisplay, __func__, width, height,
            HWCMediator::getInstance().getOvlDevice(HWC_DISPLAY_PRIMARY)->getMaxOverlayWidth(),
            HWCMediator::getInstance().getOvlDevice(HWC_DISPLAY_PRIMARY)->getMaxOverlayHeight());
        return HWC2_ERROR_NO_RESOURCES;
    }

    if (Platform::getInstance().m_config.only_wfd_by_dispdev &&
        HwcFeatureList::getInstance().getFeature().copyvds)
    {
        if (is_wfd)
        {
            if (getOvlDevice(*outDisplay)->getType() != OVL_DEVICE_TYPE_OVL)
            {
                m_disp_devs[HWC_DISPLAY_VIRTUAL] = getHwDevice();
                HrtHelper::getInstance().init(m_displays, m_disp_devs, m_is_validate_separate);
                HWC_LOGI("virtual display change to use DispDevice");
            }
        }
        else
        {
            if (getOvlDevice(*outDisplay)->getType() != OVL_DEVICE_TYPE_BLITDEV)
            {
                m_disp_devs[HWC_DISPLAY_VIRTUAL] = new AsyncBlitDevice();
                HrtHelper::getInstance().init(m_displays, m_disp_devs, m_is_validate_separate);
                HWC_LOGI("virtual display change to use AsyncBlitDevice");
            }
        }
    }

    HWC_LOGI("(%" PRIu64 ") %s format:%d", *outDisplay, __func__, *format);
    hwc_display->init();
    if (format != NULL && *format == HAL_PIXEL_FORMAT_RGBA_8888 && !is_wfd)
    {
        hwc_display->setGpuComposition(true);
    }
#ifndef MTK_HWC_USE_DRM_DEVICE
    // always gpu compose, when not mtk aosp for non-drm platform
    if (!m_is_mtk_aosp)
    {
        hwc_display->setGpuComposition(true);
    }
#endif
    DisplayManager::getInstance().hotplugVir(
        HWC_DISPLAY_VIRTUAL, true, width, height, static_cast<unsigned int>(*format));

    char value[PROPERTY_VALUE_MAX] = {0};
    property_get("vendor.mtk.lowlatencywfd.enable", value,"0");
    setLowLatencyWFD(atoi(value) ? true : false);

    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator::deviceDestroyVirtualDisplay(
    hwc2_device_t* /*device*/,
    hwc2_display_t display)
{
    CHECK_DISP_CONNECT(display);

    HWC_LOGI("(%" PRIu64 ") %s", display, __func__);
    const uint32_t width = 0, height = 0;
    const unsigned int format = 0;
    DisplayManager::getInstance().hotplugVir(
        HWC_DISPLAY_VIRTUAL, false, width, height, format);
    getHWCDisplay(display)->clear();
    setLowLatencyWFD(false);
    return HWC2_ERROR_NONE;
}

inline int32_t getHintHWLayerType(int32_t hwlayerType)
{
    switch (hwlayerType)
    {
        case HWC_LAYER_TYPE_INVALID:
        case HWC_LAYER_TYPE_UI:
        case HWC_LAYER_TYPE_MM:
        case HWC_LAYER_TYPE_DIM:
        case HWC_LAYER_TYPE_IGNORE:
        case HWC_LAYER_TYPE_GLAI:
            return hwlayerType;
            break;
        default:
            HWC_LOGW("hint_hwlayer_type %s is not supported", getHWLayerString(hwlayerType));
            return HWC_LAYER_TYPE_NONE;
            break;
    }
}

void HWCMediator::deviceDump(hwc2_device_t* /*device*/, uint32_t* outSize, char* outBuffer)
{
    static String8 m_dump_str;
    String8 dump_str;

    // if there are two callers which call dump() at the same time, the SharedBuffer of
    // m_dump_str may be corrupted by race condition. Using a lock guarantees that only
    // one caller can access it.
    static std::mutex s_dump_lock;
    std::lock_guard<std::mutex> lock(s_dump_lock);
    if (outBuffer)
    {
        size_t sizeSrc = m_dump_str.size();
        size_t sizeFinal = (*outSize >= sizeSrc) ? sizeSrc : *outSize;
        memcpy(outBuffer, const_cast<char*>(m_dump_str.string()), sizeFinal);
        outBuffer[*outSize - 1] = '\0';
    }
    else
    {
        for (auto& display : m_displays)
        {
            if (!display->isConnected())
                continue;
            display->dump(&dump_str);
            HrtHelper::getInstance().dump(&dump_str, display->getId());
        }
        updatePlatformConfig(false);

        HWCDispatcher::getInstance().dump(&dump_str);
        dump_str.appendFormat("\n");
        Debugger::getInstance().dump(&dump_str);
        if (HwcFeatureList::getInstance().getFeature().is_support_brightness)
        {
            dump_str.appendFormat("\n");
            LedDevice::getInstance().dump(&dump_str);
        }
        dump_str.appendFormat("\n[Driver Support]\n");
#ifndef MTK_USER_BUILD
        dump_str.appendFormat("  res_switch:%d\n", HWCMediator::getInstance().getOvlDevice(HWC_DISPLAY_PRIMARY)->isDispRszSupported());
        dump_str.appendFormat("  rpo:%d max_w,h:%d,%d ui_max_src_width:%d\n",
            HWCMediator::getInstance().getOvlDevice(HWC_DISPLAY_PRIMARY)->isDispRpoSupported(),
            HWCMediator::getInstance().getOvlDevice(HWC_DISPLAY_PRIMARY)->getRszMaxWidthInput(),
            HWCMediator::getInstance().getOvlDevice(HWC_DISPLAY_PRIMARY)->getRszMaxHeightInput(),
            Platform::getInstance().m_config.rpo_ui_max_src_width);
        dump_str.appendFormat("  partial_update:%d\n", HWCMediator::getInstance().getOvlDevice(HWC_DISPLAY_PRIMARY)->isPartialUpdateSupported());
        dump_str.appendFormat("  waits_fences:%d\n", HWCMediator::getInstance().getOvlDevice(HWC_DISPLAY_PRIMARY)->isFenceWaitSupported());
        dump_str.appendFormat("  ConstantAlphaForRGBA:%d\n", HWCMediator::getInstance().getOvlDevice(HWC_DISPLAY_PRIMARY)->isConstantAlphaForRGBASupported());
        dump_str.appendFormat("  ext_path_for_virtual:%d\n", Platform::getInstance().m_config.is_support_ext_path_for_virtual);

        dump_str.appendFormat("  self_refresh:%d\n", HWCMediator::getInstance().getOvlDevice(HWC_DISPLAY_PRIMARY)->isDispSelfRefreshSupported());
        dump_str.appendFormat("  lcm_color_mode:%d\n", DisplayManager::getInstance().getSupportedColorMode(HWC_DISPLAY_PRIMARY));
        dump_str.appendFormat("  3x4_disp_color_transform:%d\n", HWCMediator::getInstance().getOvlDevice(HWC_DISPLAY_PRIMARY)->isDisp3X4DisplayColorTransformSupported());
#else
        dump_str.appendFormat("  %d,%d-%d-%d-%d,%d,%d,%d,%d,%d\n",
            HWCMediator::getInstance().getOvlDevice(HWC_DISPLAY_PRIMARY)->isDispRszSupported(),
            HWCMediator::getInstance().getOvlDevice(HWC_DISPLAY_PRIMARY)->isDispRpoSupported(),
            HWCMediator::getInstance().getOvlDevice(HWC_DISPLAY_PRIMARY)->getRszMaxWidthInput(),
            HWCMediator::getInstance().getOvlDevice(HWC_DISPLAY_PRIMARY)->getRszMaxHeightInput(),
            Platform::getInstance().m_config.rpo_ui_max_src_width,
            HWCMediator::getInstance().getOvlDevice(HWC_DISPLAY_PRIMARY)->isPartialUpdateSupported(),
            HWCMediator::getInstance().getOvlDevice(HWC_DISPLAY_PRIMARY)->isFenceWaitSupported(),
            HWCMediator::getInstance().getOvlDevice(HWC_DISPLAY_PRIMARY)->isConstantAlphaForRGBASupported(),
            Platform::getInstance().m_config.is_support_ext_path_for_virtual,
            HWCMediator::getInstance().getOvlDevice(HWC_DISPLAY_PRIMARY)->isDisp3X4DisplayColorTransformSupported());

        dump_str.appendFormat("  %d,%d\n",
            HWCMediator::getInstance().getOvlDevice(HWC_DISPLAY_PRIMARY)->isDispSelfRefreshSupported(),
            DisplayManager::getInstance().getSupportedColorMode(HWC_DISPLAY_PRIMARY));

#endif
        dump_str.appendFormat("\n[HWC Property]\n");
#ifndef MTK_USER_BUILD
        dump_str.appendFormat("  force_full_invalidate(vendor.debug.hwc.forceFullInvalidate):%d\n", Platform::getInstance().m_config.force_full_invalidate);
        dump_str.appendFormat("  wait_fence_for_display(vendor.debug.hwc.waitFenceForDisplay):%d\n", Platform::getInstance().m_config.wait_fence_for_display);
        dump_str.appendFormat("  rgba_rotate(vendor.debug.hwc.rgba_rotate):%d\n", Platform::getInstance().m_config.enable_rgba_rotate);
        dump_str.appendFormat("  rgba_rotate(vendor.debug.hwc.rgbx_scaling):%d\n", Platform::getInstance().m_config.enable_rgbx_scaling);
        dump_str.appendFormat("  compose_level(vendor.debug.hwc.compose_level):%d, ", Platform::getInstance().m_config.compose_level);
        dump_str.appendFormat("  mirror_state(vendor.debug.hwc.mirror_state):%d\n", Platform::getInstance().m_config.mirror_state);

        dump_str.appendFormat("  enableUBL(vendor.debug.hwc.enableUBL):%d\n", Platform::getInstance().m_config.use_async_bliter_ultra);
        dump_str.appendFormat("  prexformUI(vendor.debug.hwc.prexformUI):%d\n", Platform::getInstance().m_config.prexformUI);
        dump_str.appendFormat("  log_level(persist.vendor.debug.hwc.log):%c, ", Debugger::getInstance().getLogThreshold());
        dump_str.appendFormat("  skip_period_log(vendor.debug.hwc.skip_log):%d\n", Debugger::m_skip_log);
        dump_str.appendFormat("  mhl_output(vendor.debug.hwc.mhl_output):%d\n", Platform::getInstance().m_config.format_mir_mhl);

        dump_str.appendFormat("  profile_level(vendor.debug.hwc.profile_level):%d\n", DisplayManager::m_profile_level);
        dump_str.appendFormat("  mir_scale_ratio(vendor.debug.hwc.mir_scale_ratio):%f\n", Platform::getInstance().m_config.mir_scale_ratio);
        dump_str.appendFormat("  ext layer:%d(vendor.debug.hwc.ext_layer)\n", Platform::getInstance().m_config.enable_smart_layer);

        dump_str.appendFormat("  trigger_by_vsync(vendor.debug.hwc.trigger_by_vsync):%d\n", HwcFeatureList::getInstance().getFeature().trigger_by_vsync);
        dump_str.appendFormat("  AV_grouping(vendor.debug.hwc.av_grouping):%d\n", Platform::getInstance().m_config.av_grouping);
        dump_str.appendFormat("  AV_grouping type(vendor.debug.hwc.grouping_type):0x%x\n", Platform::getInstance().m_config.grouping_type);
        dump_str.appendFormat("  DumpBuf(vendor.debug.hwc.dump_buf):%c-%d, DumpBufCont(debug.hwc.dump_buf_cont):%c-%d log:%d\n",
            Platform::getInstance().m_config.dump_buf_type, Platform::getInstance().m_config.dump_buf,
            Platform::getInstance().m_config.dump_buf_cont_type, Platform::getInstance().m_config.dump_buf_cont,
            Platform::getInstance().m_config.dump_buf_log_enable);

        dump_str.appendFormat("  fill_black_debug(vendor.debug.hwc.fill_black_debug):%d\n", Platform::getInstance().m_config.fill_black_debug);
        dump_str.appendFormat("  Always_Setup_Private_Handle(vendor.debug.hwc.always_setup_priv_hnd):%d\n", Platform::getInstance().m_config.always_setup_priv_hnd);
        dump_str.appendFormat("  wdt_trace(vendor.debug.hwc.wdt_trace):%d\n", Platform::getInstance().m_config.wdt_trace);
        dump_str.appendFormat("  only_wfd_by_hwc(vendor.debug.hwc.only_wfd_by_hwc):%d\n", Platform::getInstance().m_config.only_wfd_by_hwc);
        dump_str.appendFormat("  blitdev_for_virtual(vendor.debug.hwc.blitdev_for_virtual):%d\n", Platform::getInstance().m_config.blitdev_for_virtual);

        dump_str.appendFormat("  is_skip_validate(vendor.debug.hwc.is_skip_validate):%d\n", Platform::getInstance().m_config.is_skip_validate);
        dump_str.appendFormat("  support_color_transform(vendor.debug.hwc.color_transform):%d\n", Platform::getInstance().m_config.support_color_transform);
        dump_str.appendFormat("  mdp_scaling_percentage(vendor.debug.hwc.mdp_scale_percentage):%.2f\n", Platform::getInstance().m_config.mdp_scale_percentage);
        dump_str.appendFormat("  ExtendMDP(vendor.debug.hwc.extend_mdp_cap):%d\n", Platform::getInstance().m_config.extend_mdp_capacity);
        dump_str.appendFormat("  disp_support_decompress(vendor.debug.hwc.disp_support_decompress):%d\n", Platform::getInstance().m_config.disp_support_decompress);

        dump_str.appendFormat("  mdp_support_decompress(vendor.debug.hwc.mdp_support_decompress):%d\n", Platform::getInstance().m_config.mdp_support_decompress);
        dump_str.appendFormat("  mdp_support_compress(vendor.debug.hwc.mdp_support_compress):%d\n", Platform::getInstance().m_config.mdp_support_compress);
        dump_str.appendFormat("  mdp_support_decompress(vendor.debug.hwc.remove_invisible_layers):%d\n", Platform::getInstance().m_config.remove_invisible_layers);
        dump_str.appendFormat("  force_mdp_output_format(vendor.debug.hwc.force_mdp_output_format):%d\n", Platform::getInstance().m_config.force_mdp_output_format);
        dump_str.appendFormat("  use_datapace_for_yuv(vendor.debug.hwc.use_dataspace_for_yuv):%d\n", Platform::getInstance().m_config.use_dataspace_for_yuv);
        dump_str.appendFormat("  hdr_support (vendor.debug.hwc.hdr):%d\n", HwcFeatureList::getInstance().getFeature().hdr_display);

        dump_str.appendFormat("  hdr_support (vendor.debug.hwc.fill_hwdec_hdr):%d\n", Platform::getInstance().m_config.fill_hwdec_hdr);

        dump_str.appendFormat("  is_support_mdp_pmqos(vendor.debug.hwc.is_support_mdp_pmqos):%d\n", Platform::getInstance().m_config.is_support_mdp_pmqos);
        dump_str.appendFormat("  is_support_mdp_pmqos_debug(vendor.debug.hwc.is_support_mdp_pmqos_debug):%d\n", Platform::getInstance().m_config.is_support_mdp_pmqos_debug);
        dump_str.appendFormat("  force_pq_index(vendor.debug.hwc.force_pq_index):%d\n", Platform::getInstance().m_config.force_pq_index);
        dump_str.appendFormat("  is_support_game_pq(vendor.debug.hwc.is_support_game_pq):%d\n", HwcFeatureList::getInstance().getFeature().game_pq);

        dump_str.appendFormat("  dbg_mdp_always_blit(vendor.debug.hwc.dbg_mdp_always_blit):%d\n", Platform::getInstance().m_config.dbg_mdp_always_blit);
        dump_str.appendFormat("  dbg_present_delay_time(vendor.debug.hwc.dbg_present_delay_time):%" PRId64 "\n",
                              static_cast<int64_t>(Platform::getInstance().m_config.dbg_present_delay_time.count()));
        dump_str.appendFormat("  is_client_clear_support(vendor.debug.hwc.is_client_clear_support):%d\n", Platform::getInstance().m_config.is_client_clear_support);
        dump_str.appendFormat("  game_hdr(vendor.debug.hwc.game_hdr):%d\n", HwcFeatureList::getInstance().getFeature().game_hdr);
        dump_str.appendFormat("  is_skip_hrt(vendor.debug.hwc.is_skip_hrt):%d\n", Platform::getInstance().m_config.is_skip_hrt);
        dump_str.appendFormat("  cache_CT_private_hnd(vendor.debug.hwc.cache_CT_private_hnd):%d\n", Platform::getInstance().m_config.cache_CT_private_hnd);
        dump_str.appendFormat("  tolerance_time_to_refresh(vendor.debug.hwc.tolerance_time_to_refresh):%" PRId64"\n", Platform::getInstance().m_config.tolerance_time_to_refresh);
        dump_str.appendFormat("  check_skip_client_color_transform(vendor.debug.hwc.check_skip_client_color_transform):%d\n", Platform::getInstance().m_config.check_skip_client_color_transform);
        dump_str.appendFormat("  plat_switch(vendor.debug.hwc.plat_switch):0x%x\n", Platform::getInstance().m_config.plat_switch);
        dump_str.appendFormat("  dbg_switch(vendor.debug.hwc.dbg_switch):0x%x\n", Platform::getInstance().m_config.dbg_switch);
        dump_str.appendFormat("  mml_switch(vendor.debug.hwc.mml_switch):%d\n", Platform::getInstance().m_config.mml_switch);
        dump_str.appendFormat("  support_mml(vendor.debug.hwc.is_support_mml):%d\n", HwcFeatureList::getInstance().getFeature().is_support_mml);
        dump_str.appendFormat("  AI_PQ:%d\n", HwcFeatureList::getInstance().getFeature().ai_pq);
        dump_str.appendFormat("  has_glai(ro.vendor.game_aisr_enable):%d\n", HwcFeatureList::getInstance().getFeature().has_glai);
        dump_str.appendFormat("  show_present_index(vendor.debug.hwc.show_present_index):e%d d%" PRIu64 " %ux%u (%u,%u)\n",
                m_present_idx_action, m_present_idx_display_id, m_present_idx_width,
                m_present_idx_height, m_present_idx_x, m_present_idx_y);
        dump_str.appendFormat("  is_bw_monitor_support(vendor.debug.hwc.is_bw_monitor_support):%d\n", Platform::getInstance().m_config.is_bw_monitor_support);
        dump_str.appendFormat("  is_smart_composition_support(vendor.debug.hwc.is_smart_composition_support):%d\n", Platform::getInstance().m_config.is_smart_composition_support);
        dump_str.appendFormat("  inactive_set_expired_cnt(vendor.debug.hwc.inactive_set_expired_cnt):%d\n", Platform::getInstance().m_config.inactive_set_expired_cnt);
        dump_str.appendFormat("  inactive_set_expired_duration(vendor.debug.hwc.inactive_set_expired_duration):%" PRId64 "\n", Platform::getInstance().m_config.inactive_set_expired_duration);
        dump_str.appendFormat("  is_validate_separate(vendor.debug.sf.validate_separate):%d\n", m_is_validate_separate);
        dump_str.appendFormat("\n");
#else // MTK_USER_BUILD
        dump_str.appendFormat("  %d,%d,%d,%d,%d,%d",
                Platform::getInstance().m_config.force_full_invalidate,
                Platform::getInstance().m_config.wait_fence_for_display,
                Platform::getInstance().m_config.enable_rgba_rotate,
                Platform::getInstance().m_config.enable_rgbx_scaling,
                Platform::getInstance().m_config.compose_level,
                Platform::getInstance().m_config.mirror_state);

        dump_str.appendFormat(" ,%d,%d,%c,%d,%d",
                Platform::getInstance().m_config.use_async_bliter_ultra,
                Platform::getInstance().m_config.prexformUI,
                Debugger::getInstance().getLogThreshold(),
                Debugger::m_skip_log,
                Platform::getInstance().m_config.format_mir_mhl);

        dump_str.appendFormat(" ,%d,%f,%d",
                DisplayManager::m_profile_level,
                Platform::getInstance().m_config.mir_scale_ratio,
                Platform::getInstance().m_config.enable_smart_layer);

        dump_str.appendFormat(" ,%d,%d,0x%x,%c-%d,%c-%d,%d",
                HwcFeatureList::getInstance().getFeature().trigger_by_vsync,
                Platform::getInstance().m_config.av_grouping,
                Platform::getInstance().m_config.grouping_type,
                Platform::getInstance().m_config.dump_buf_type, Platform::getInstance().m_config.dump_buf,
                Platform::getInstance().m_config.dump_buf_cont_type, Platform::getInstance().m_config.dump_buf_cont,
                Platform::getInstance().m_config.dump_buf_log_enable);

        dump_str.appendFormat(" ,%d,%d,%d,%d,%d,%d,%d",
                Platform::getInstance().m_config.fill_black_debug,
                Platform::getInstance().m_config.always_setup_priv_hnd,
                Platform::getInstance().m_config.wdt_trace,
                Platform::getInstance().m_config.only_wfd_by_hwc,
                Platform::getInstance().m_config.blitdev_for_virtual,
                Platform::getInstance().m_config.is_skip_validate,
                Platform::getInstance().m_config.support_color_transform);

        dump_str.appendFormat(" ,%.2f,%d,%d,%d,%d,%d",
                Platform::getInstance().m_config.mdp_scale_percentage,
                Platform::getInstance().m_config.extend_mdp_capacity,
                Platform::getInstance().m_config.disp_support_decompress,
                Platform::getInstance().m_config.mdp_support_decompress,
                Platform::getInstance().m_config.mdp_support_compress,
                Platform::getInstance().m_config.force_mdp_output_format);

        dump_str.appendFormat(" ,%d %d %d,%d,%d,%d,%d",
                Platform::getInstance().m_config.use_dataspace_for_yuv,
                HwcFeatureList::getInstance().getFeature().hdr_display,
                Platform::getInstance().m_config.fill_hwdec_hdr,
                Platform::getInstance().m_config.is_support_mdp_pmqos,
                Platform::getInstance().m_config.is_support_mdp_pmqos_debug,
                Platform::getInstance().m_config.force_pq_index,
                HwcFeatureList::getInstance().getFeature().game_pq);

        dump_str.appendFormat(" ,%d,%" PRId64 ",%d,%d,%d,%d,%" PRId64,
                Platform::getInstance().m_config.dbg_mdp_always_blit,
                static_cast<int64_t>(Platform::getInstance().m_config.dbg_present_delay_time.count()),
                Platform::getInstance().m_config.is_client_clear_support,
                HwcFeatureList::getInstance().getFeature().game_hdr,
                Platform::getInstance().m_config.is_skip_hrt,
                Platform::getInstance().m_config.cache_CT_private_hnd,
                Platform::getInstance().m_config.tolerance_time_to_refresh);

        dump_str.appendFormat(" ,%d,0x%x,0x%x,%d,%d,%d,%d",
                Platform::getInstance().m_config.check_skip_client_color_transform,
                Platform::getInstance().m_config.plat_switch,
                Platform::getInstance().m_config.dbg_switch,
                Platform::getInstance().m_config.mml_switch,
                HwcFeatureList::getInstance().getFeature().is_support_mml,
                HwcFeatureList::getInstance().getFeature().ai_pq,
                HwcFeatureList::getInstance().getFeature().has_glai);

        dump_str.appendFormat(" ,%d,%" PRIu64 ",%ux%u,(%u,%u)",
                m_present_idx_action, m_present_idx_display_id, m_present_idx_width,
                m_present_idx_height, m_present_idx_x, m_present_idx_y);

        dump_str.appendFormat(" ,%d,%d,%d,%" PRId64 ",%d",
                Platform::getInstance().m_config.is_bw_monitor_support,
                Platform::getInstance().m_config.is_smart_composition_support,
                Platform::getInstance().m_config.inactive_set_expired_cnt,
                Platform::getInstance().m_config.inactive_set_expired_duration,
                Platform::getInstance().m_config.bwm_skip_hrt_calc);

        dump_str.appendFormat(" ,%d\n\n",
                m_is_validate_separate);
#endif // MTK_USER_BUILD

        dump_str.appendFormat("[PQ Support]\n");
#ifndef MTK_USER_BUILD
        dump_str.appendFormat("  useColorTransformIoctl(vendor.debug.hwc.useColorTransformIoctl):%d\n",
                getPqDevice()->isColorTransformIoctl());
        dump_str.appendFormat("  pq_video_whitelist_support:%d\n",
                HwcFeatureList::getInstance().getFeature().pq_video_whitelist_support);
        dump_str.appendFormat("  video transition:%d\n",
                HwcFeatureList::getInstance().getFeature().video_transition);
        dump_str.appendFormat("  mtk_pq_support:%d\n",
                HwcFeatureList::getInstance().getFeature().is_support_pq);
        dump_str.appendFormat("  hdr_video or hdr_display:%d\n",
                HwcFeatureList::getInstance().getFeature().hdr_display);
#else
        dump_str.appendFormat(" %d,%d,%d,%d,%d\n",
                getPqDevice()->isColorTransformIoctl(),
                HwcFeatureList::getInstance().getFeature().pq_video_whitelist_support,
                HwcFeatureList::getInstance().getFeature().video_transition,
                HwcFeatureList::getInstance().getFeature().is_support_pq,
                HwcFeatureList::getInstance().getFeature().hdr_display);
#endif
        dump_str.appendFormat("\n");

        dump_str.appendFormat("[MM Buffer]\n");
#ifndef MTK_USER_BUILD
        dump_str.appendFormat("  enable_mm_buffer_dump(vendor.debug.hwc.enable_mm_buffer_dump):%d\n",
                Platform::getInstance().m_config.enable_mm_buffer_dump);
        dump_str.appendFormat("  dump_ovl_bits(vendor.debug.hwc.dump_ovl_bits):%d\n",
                Platform::getInstance().m_config.dump_ovl_bits);
#else
        dump_str.appendFormat(" %d, %d\n",
                Platform::getInstance().m_config.enable_mm_buffer_dump,
                Platform::getInstance().m_config.dump_ovl_bits);
#endif
        dump_str.appendFormat("\n");

        // hint composition type for specific layer
        dump_str.appendFormat("[Layer Hint]\n");
#ifndef MTK_USER_BUILD
        dump_str.appendFormat("  hint_id(vendor.debug.hwc.hint_id):%" PRIu64 "\n",
                Platform::getInstance().m_config.hint_id);
        dump_str.appendFormat("  hint_name(vendor.debug.hwc.hint_name):%s\n",
                Platform::getInstance().m_config.hint_name.c_str());
        dump_str.appendFormat("  hint_name_shift(vendor.debug.hwc.hint_name_shift):%zu\n",
                Platform::getInstance().m_config.hint_name_shift);
        dump_str.appendFormat("  hint_hwlayer_type(vendor.debug.hwc.hint_hwlayer_type):%s\n",
                getHWLayerString(Platform::getInstance().m_config.hint_hwlayer_type));
#else
        dump_str.appendFormat(" %" PRIu64 ", %s, %zu, %s\n",
                Platform::getInstance().m_config.hint_id,
                Platform::getInstance().m_config.hint_name.c_str(),
                Platform::getInstance().m_config.hint_name_shift,
                getHWLayerString(Platform::getInstance().m_config.hint_hwlayer_type));
#endif

        DataExpress::getInstance().dump(&dump_str);

        if (HwcFeatureList::getInstance().getFeature().has_glai)
        {
            GlaiController::getInstance().dump(&dump_str);
        }
        dump_str.appendFormat("\n");

        dump_str.appendFormat("[ComposerExt]\n");
        dump_str.appendFormat("client_list[%zu]\n", m_ext_callback_list.size());
        for (auto iter = m_ext_callback_list.begin(); iter != m_ext_callback_list.end(); ++iter)
        {
            dump_str.appendFormat("\t%s\n", iter->second.client_name.c_str());
        }
        dump_str.appendFormat("\n");

        *outSize = static_cast<uint32_t>(dump_str.size() + 1);
        m_dump_str = dump_str;
    }
}

void HWCMediator::updatePlatformConfig(bool is_init)
{
    char value[PROPERTY_VALUE_MAX] = {0};

    // if the property need to update when HWC do initialization, add it in here
    unsigned int old_plat_switch = Platform::getInstance().m_config.plat_switch;
    PLAT_SWITCH_SET_CLEAR_BIT(plat_switch);
    PLAT_SWITCH_SET_CLEAR_BIT(dbg_switch);

    old_plat_switch = old_plat_switch ^ Platform::getInstance().m_config.plat_switch;
    // if the dirty bits are related with CPU, we need to update the cpu set
    if ((HWC_PLAT_SWITCH_ALWAYS_ON_MIDDLE_CORE | HWC_PLAT_SWITCH_VP_ON_MIDDLE_CORE) &
            old_plat_switch)
    {
        for (auto& display: m_displays)
        {
            display->setDirtyCpuSet();
        }
    }

    // if the property only update when someone call dump function, add it in below section
    if (!is_init)
    {
        // force full invalidate
        property_get("vendor.debug.hwc.forceFullInvalidate", value, "-1");
        if (-1 != atoi(value))
            Platform::getInstance().m_config.force_full_invalidate = atoi(value);

        property_get("vendor.debug.hwc.rgba_rotate", value, "-1");
        if (-1 != atoi(value))
            Platform::getInstance().m_config.enable_rgba_rotate = atoi(value);

        property_get("vendor.debug.hwc.rgbx_scaling", value, "-1");
        if (-1 != atoi(value))
            Platform::getInstance().m_config.enable_rgbx_scaling = atoi(value);

        // check compose level
        property_get("vendor.debug.hwc.compose_level", value, "-1");
        if (-1 != atoi(value))
            Platform::getInstance().m_config.compose_level = atoi(value);

        property_get("vendor.debug.hwc.enableUBL", value, "-1");
        if (-1 != atoi(value))
            Platform::getInstance().m_config.use_async_bliter_ultra = (0 != atoi(value));

        property_get("vendor.debug.hwc.prexformUI", value, "-1");
        if (-1 != atoi(value))
            Platform::getInstance().m_config.prexformUI = atoi(value);

        property_get("vendor.debug.hwc.skip_log", value, "-1");
        if (-1 != atoi(value))
            Debugger::m_skip_log = atoi(value);

        // check mirror state
        property_get("vendor.debug.hwc.mirror_state", value, "-1");
        if (-1 != atoi(value))
            Platform::getInstance().m_config.mirror_state = atoi(value);

        // dynamic change mir format for mhl_output
        property_get("vendor.debug.hwc.mhl_output", value, "-1");
        if (-1 != atoi(value))
            Platform::getInstance().m_config.format_mir_mhl = atoi(value);

        // check profile level
        property_get("vendor.debug.hwc.profile_level", value, "-1");
        if (-1 != atoi(value))
            DisplayManager::m_profile_level = atoi(value);

        // check the maximum scale ratio of mirror source
        property_get("vendor.debug.hwc.mir_scale_ratio", value, "0");
        if (!(strlen(value) == 1 && value[0] == '0'))
            Platform::getInstance().m_config.mir_scale_ratio = strtof(value, NULL);

        property_get("persist.vendor.debug.hwc.log", value, "0");
        if (!(strlen(value) == 1 && value[0] == '0'))
            Debugger::getInstance().setLogThreshold(value[0]);

        property_get("vendor.debug.hwc.ext_layer", value, "-1");
        if (-1 != atoi(value))
            Platform::getInstance().m_config.enable_smart_layer = atoi(value);

        // 0: All displays' jobs are dispatched when they are added into job queue
        // 1: Only external display's jobs are dispatched when external display's vsync is received
        // 2: external and wfd displays' jobs are dispatched when they receive VSync
        property_get("vendor.debug.hwc.trigger_by_vsync", value, "-1");
        if (-1 != atoi(value))
            HwcFeatureList::getInstance().getEditableFeature().trigger_by_vsync = atoi(value);

        property_get("vendor.debug.hwc.av_grouping", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.av_grouping = atoi(value);
        }

        property_get("vendor.debug.hwc.grouping_type", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.grouping_type = static_cast<unsigned int>(strtoul(value, nullptr, 0));
        }

        // force hwc to wait fence for display
        property_get("vendor.debug.hwc.waitFenceForDisplay", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.wait_fence_for_display = atoi(value);
        }

        property_get("vendor.debug.hwc.always_setup_priv_hnd", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.always_setup_priv_hnd = atoi(value);
        }

        property_get("vendor.debug.hwc.only_wfd_by_hwc", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.only_wfd_by_hwc = atoi(value);
        }

        property_get("vendor.debug.hwc.wdt_trace", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.wdt_trace = atoi(value);
        }

        property_get("vendor.debug.hwc.dump_buf", value, "-1");
        if ('-' != value[0])
        {
            if (value[0] == 'M' || value[0] == 'U' || value[0] == 'C')
            {
                Platform::getInstance().m_config.dump_buf_type = value[0];
                Platform::getInstance().m_config.dump_buf = atoi(value + 1);
            }
            else if(isdigit(value[0]))
            {
                Platform::getInstance().m_config.dump_buf_type = 'A';
                Platform::getInstance().m_config.dump_buf = atoi(value);
            }
        }
        else
        {
            Platform::getInstance().m_config.dump_buf_type = 'A';
            Platform::getInstance().m_config.dump_buf = 0;
        }

        property_get("vendor.debug.hwc.dump_buf_cont", value, "-1");
        if ('-' != value[0])
        {
            if (value[0] == 'M' || value[0] == 'U' || value[0] == 'C')
            {
                Platform::getInstance().m_config.dump_buf_cont_type = value[0];
                Platform::getInstance().m_config.dump_buf_cont = atoi(value + 1);
            }
            else if(isdigit(value[0]))
            {
                Platform::getInstance().m_config.dump_buf_cont_type = 'A';
                Platform::getInstance().m_config.dump_buf_cont = atoi(value);
            }
        }
        else
        {
            Platform::getInstance().m_config.dump_buf_cont_type = 'A';
            Platform::getInstance().m_config.dump_buf_cont = 0;
        }

        property_get("vendor.debug.hwc.dump_buf_log_enable", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.dump_buf_log_enable = atoi(value);
        }

        property_get("vendor.debug.hwc.fill_black_debug", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.fill_black_debug = atoi(value);
        }

        // When primary display is external, it should not support skip validate feature.
        bool is_primary_internal = true;
        for (auto& hwc_display : m_displays)
        {
            if (hwc_display->getId() == HWC_DISPLAY_PRIMARY)
            {
                if (!hwc_display->isInternal())
                {
                    is_primary_internal = false;
                }
                break;
            }
        }
        property_get("vendor.debug.hwc.is_skip_validate", value, "-1");
        if ((-1 != atoi(value)) && is_primary_internal)
        {
            Platform::getInstance().m_config.is_skip_validate = atoi(value);

            if (Platform::getInstance().m_config.is_skip_validate == 0)
            {
                std::vector<int32_t>::iterator capbility = std::find(m_capabilities.begin(), m_capabilities.end(), HWC2_CAPABILITY_SKIP_VALIDATE);
                if (capbility != m_capabilities.end())
                {
                    m_capabilities.erase(capbility);
                }
            }
            else
            {
                if (hasCapabilities(HWC2_CAPABILITY_SKIP_VALIDATE) == false)
                {
                    m_capabilities.push_back(HWC2_CAPABILITY_SKIP_VALIDATE);
                }
            }
        }

        property_get("vendor.debug.hwc.color_transform", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.support_color_transform = atoi(value);
        }

        property_get("vendor.debug.hwc.enable_rpo", value, "-1");
        if (1 == atoi(value))
        {
            HWCMediator::getInstance().getOvlDevice(HWC_DISPLAY_PRIMARY)->enableDisplayFeature(HWC_FEATURE_RPO);
        }
        else if (0 == atoi(value))
        {
            HWCMediator::getInstance().getOvlDevice(HWC_DISPLAY_PRIMARY)->disableDisplayFeature(HWC_FEATURE_RPO);
        }

        property_get("vendor.debug.hwc.rpo_ui_max_src_width", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.rpo_ui_max_src_width = atoi(value);
        }

        property_get("vendor.debug.hwc.mdp_scale_percentage", value, "-1");
        const double num_double = atof(value);
        if (fabs(num_double - (-1)) > 0.05f)
        {
            Platform::getInstance().m_config.mdp_scale_percentage = num_double;
        }

        property_get("vendor.debug.hwc.extend_mdp_cap", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.extend_mdp_capacity = atoi(value);
        }

        property_get("vendor.debug.hwc.disp_support_decompress", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.disp_support_decompress = atoi(value);
        }

        property_get("vendor.debug.hwc.mdp_support_decompress", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.mdp_support_decompress = atoi(value);
        }

        property_get("vendor.debug.hwc.mdp_support_compress", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.mdp_support_compress = atoi(value);
        }

        property_get("vendor.debug.hwc.remove_invisible_layers", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.remove_invisible_layers = atoi(value);
        }

        property_get("vendor.debug.hwc.use_dataspace_for_yuv", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.use_dataspace_for_yuv = atoi(value);
        }

        property_get("vendor.debug.hwc.hdr", value, "-1");
        if (-1 != atoi(value))
        {
            HwcFeatureList::getInstance().getEditableFeature().hdr_display = atoi(value);
        }

        property_get("vendor.debug.hwc.fill_hwdec_hdr", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.fill_hwdec_hdr = atoi(value);
        }

        property_get("vendor.debug.hwc.is_support_mdp_pmqos", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.is_support_mdp_pmqos = atoi(value);
        }

        property_get("vendor.debug.hwc.is_support_mdp_pmqos_debug", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.is_support_mdp_pmqos_debug = atoi(value);
        }

        property_get("vendor.debug.hwc.game_hdr", value, "-1");
        if (-1 != atoi(value))
        {
            HwcFeatureList::getInstance().getEditableFeature().game_hdr = atoi(value);
        }


        property_get("vendor.debug.hwc.force_pq_index", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.force_pq_index = atoi(value);
        }
        else
        {
            Platform::getInstance().m_config.force_pq_index = -1;
        }

        property_get("vendor.debug.hwc.is_support_game_pq", value, "-1");
        if (-1 != atoi(value))
        {
            HwcFeatureList::getInstance().getEditableFeature().game_pq = atoi(value);
        }

        property_get("vendor.debug.hwc.dbg_mdp_always_blit", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.dbg_mdp_always_blit = atoi(value);
        }

        property_get("vendor.debug.hwc.is_client_clear_support", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.is_client_clear_support = atoi(value);
        }

        property_get("vendor.debug.hwc.dbg_present_delay_time", value, "0");
        Platform::getInstance().m_config.dbg_present_delay_time = std::chrono::microseconds(atoi(value));

        property_get("vendor.debug.hwc.is_skip_hrt", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.is_skip_hrt = atoi(value);
        }

        property_get("vendor.debug.hwc.cache_CT_private_hnd", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.cache_CT_private_hnd = atoi(value);
        }

        property_get("vendor.debug.hwc.tolerance_time_to_refresh", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.tolerance_time_to_refresh = atoi(value);
        }

        property_get("vendor.debug.hwc.force_mdp_output_format", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.force_mdp_output_format =
                static_cast<uint32_t>(atoi(value));
        }

        property_get("vendor.debug.hwc.check_skip_client_color_transform", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.check_skip_client_color_transform =
                static_cast<uint32_t>(atoi(value));
        }

        property_get("vendor.debug.hwc.mml_switch", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.mml_switch =
                static_cast<bool>(atoi(value));
        }

        property_get("vendor.debug.hwc.is_support_mml", value, "-1");
        if (-1 != atoi(value))
        {
            HwcFeatureList::getInstance().getEditableFeature().is_support_mml =
                static_cast<bool>(atoi(value));
        }

        property_get("vendor.debug.hwc.glai_wo_fence", value, "-1");
        if (-1 != atoi(value))
        {
            GlaiController::getInstance().setInferenceWoFence(atoi(value));
        }

        property_get("vendor.debug.hwc.aibld_dump_enable", value, "-1");
        if (-1 != atoi(value))
        {
            AiBluLightDefender::getInstance().setDumpEnable(atoi(value) != 0);
        }

        property_get("vendor.debug.hwc.useColorTransformIoctl", value, "-1");
        if (atoi(value) != -1)
        {
            getPqDevice()->useColorTransformIoctl(atoi(value));
        }

        property_get("vendor.debug.hwc.enable_mm_buffer_dump", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.enable_mm_buffer_dump = atoi(value);
        }

        property_get("vendor.debug.hwc.dump_ovl_bits", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.dump_ovl_bits = static_cast<uint32_t>(atoi(value));
        }

        // hint composition type for specific layer
        property_get("vendor.debug.hwc.hint_id", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.hint_id = static_cast<uint64_t>(atoi(value));
        }

        property_get("vendor.debug.hwc.hint_name", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.hint_name = value;
        }

        property_get("vendor.debug.hwc.hint_name_shift", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.hint_name_shift = static_cast<size_t>(atoi(value));
        }

        property_get("vendor.debug.hwc.hint_hwlayer_type", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.hint_hwlayer_type = getHintHWLayerType(getHWLayerType(value));
        }

        property_get("vendor.debug.hwc.primary_force_pat", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.primary_force_pat = atoi(value);
        }

        property_get("vendor.debug.hwc.ext_force_pat", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.ext_force_pat = atoi(value);
        }

        // setproperty vendor.debug.hwc.show_present_index e=1::d=0::w=512::h=256::x=0::y=256
        // e: enable/disable this debug function. 0:disable, 1:enable
        // d: display id.
        // w: the width of layer.
        // h: the height of layer.
        // x: the coordinate of x axis.
        // y: the coordinate of y axis.
        int len = property_get("vendor.debug.hwc.show_present_index", value, "");
        if (len)
        {
            std::string delimiter_item = "::";
            std::string delimiter_parameter = "=";
            std::string command = value;
            while (command.length())
            {
                size_t pos_item = command.find(delimiter_item);
                std::string item = "";
                if (pos_item != std::string::npos)
                    item = command.substr(0, pos_item);

                size_t pos_parameter = item.find(delimiter_parameter);
                if (pos_parameter != std::string::npos)
                {
                    std::string parameter = item.substr(0, pos_parameter);
                    std::string val = item.substr(pos_parameter + delimiter_parameter.length());
                    if (parameter.compare("e") == 0)
                    {
                        m_present_idx_action = std::atoi(val.c_str());
                    }
                    else if (parameter.compare("d") == 0)
                    {
                        m_present_idx_display_id = static_cast<uint64_t>(
                                std::atoll(val.c_str()));
                    }
                    else if (parameter.compare("w") == 0)
                    {
                        m_present_idx_width = static_cast<uint32_t>(std::atoi(val.c_str()));
                    }
                    else if (parameter.compare("h") == 0)
                    {
                        m_present_idx_height = static_cast<uint32_t>(std::atoi(val.c_str()));
                    }
                    else if (parameter.compare("x") == 0)
                    {
                        m_present_idx_x = static_cast<uint32_t>(std::atoi(val.c_str()));
                    }
                    else if (parameter.compare("y") == 0)
                    {
                        m_present_idx_y = static_cast<uint32_t>(std::atoi(val.c_str()));
                    }
                }

                if (pos_item == std::string::npos)
                {
                    command.erase();
                }
                else
                {
                    command.erase(0, pos_item + delimiter_item.length());
                }
            }
            if (m_present_idx_action > 0)
            {
                if (m_present_idx_width == 0)
                {
                    m_present_idx_width = 512;
                }
                if (m_present_idx_height == 0)
                {
                    m_present_idx_height = 256;
                }
            }
            property_set("vendor.debug.hwc.show_present_index", "");
        }

        property_get("vendor.debug.hwc.is_bw_monitor_support", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.is_bw_monitor_support = atoi(value);
        }

        property_get("vendor.debug.hwc.is_smart_composition_support", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.is_smart_composition_support = atoi(value);
        }

        property_get("vendor.debug.hwc.inactive_set_expired_cnt", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.inactive_set_expired_cnt = atoi(value);
        }

        property_get("vendor.debug.hwc.inactive_set_expired_duration", value, "-1");
        if (-1 != atoi(value))
        {
            // setprop vendor.debug.hwc.inactive_set_expired_duration 2000
            Platform::getInstance().m_config.inactive_set_expired_duration = ms2ns(atoi(value));
        }

        property_get("vendor.debug.hwc.bwm_skip_hrt_calc", value, "-1");
        if (-1 != atoi(value))
        {
            Platform::getInstance().m_config.bwm_skip_hrt_calc = atoi(value);
        }

        // for internal debug only
        if (isInternalLoad())
        {
            // color mode render intent test d=0::c=7::r=0
            // dpy = 0, color mode = 7, render intent 0
            len = property_get("vendor.debug.hwc.color_mode_render_intent", value, "");
            if (len)
            {
                std::string delimiter_item = "::";
                std::string delimiter_parameter = "=";
                std::string command = value;
                uint64_t dpy = 0;
                int32_t color_mode = -1;
                int32_t render_intent = -1;
                while (command.length())
                {
                    size_t pos_item = command.find(delimiter_item);
                    std::string item = "";
                    if (pos_item != std::string::npos)
                        item = command.substr(0, pos_item);

                    size_t pos_parameter = item.find(delimiter_parameter);
                    if (pos_parameter != std::string::npos)
                    {
                        std::string parameter = item.substr(0, pos_parameter);
                        std::string val = item.substr(pos_parameter + delimiter_parameter.length());
                        if (parameter.compare("d") == 0)
                        {
                            dpy = static_cast<uint64_t>(std::atoi(val.c_str()));
                        }
                        else if (parameter.compare("c") == 0)
                        {
                            color_mode = std::atoi(val.c_str());
                        }
                        else if (parameter.compare("r") == 0)
                        {
                            render_intent = std::atoi(val.c_str());
                        }
                    }

                    if (pos_item == std::string::npos)
                    {
                        command.erase();
                    }
                    else
                    {
                        command.erase(0, pos_item + delimiter_item.length());
                    }
                }
                if (getHWCDisplay(dpy) && color_mode >= 0 && render_intent >= 0)
                {
                    HWC_LOGI("debug dpy %" PRIu64 " setColorModeWithRenderIntent(%d, %d)",
                             dpy, color_mode, render_intent);
                    getHWCDisplay(dpy)->setColorModeWithRenderIntent(color_mode, render_intent);
                    DisplayManager::getInstance().refreshForDisplay(HWC_DISPLAY_PRIMARY, HWC_REFRESH_FOR_DEBUG);
                }
            }
        }
    }
}

uint32_t HWCMediator::deviceGetMaxVirtualDisplayCount(hwc2_device_t* /*device*/)
{
    return Platform::getInstance().m_config.vir_disp_sup_num;
}

int32_t /*hwc2_error_t*/ HWCMediator::deviceRegisterCallback(
    hwc2_device_t* /*device*/,
    int32_t /*hwc2_callback_descriptor_t*/ descriptor,
    hwc2_callback_data_t callback_data,
    hwc2_function_pointer_t pointer)
{
    switch (descriptor)
    {
        case HWC2_CALLBACK_HOTPLUG:
            {
                m_callback_hotplug = reinterpret_cast<HWC2_PFN_HOTPLUG>(pointer);
                m_callback_hotplug_data = callback_data;
                sp<DisplayListener> listener = (DisplayListener *) DisplayManager::getInstance().getListener().get();
                HWC_LOGI("Register hotplug callback(ptr=%p)", m_callback_hotplug);
                if (listener != NULL)
                {
                    listener->m_callback_hotplug =  m_callback_hotplug;
                    listener->m_callback_hotplug_data = m_callback_hotplug_data;
                    DisplayManager::getInstance().resentCallback();
                }
            }
            break;

        case HWC2_CALLBACK_VSYNC:
            {
                m_callback_vsync = reinterpret_cast<HWC2_PFN_VSYNC>(pointer);
                m_callback_vsync_data = callback_data;
                sp<DisplayListener> listener = (DisplayListener *) DisplayManager::getInstance().getListener().get();
                HWC_LOGI("Register vsync callback(ptr=%p)", m_callback_vsync);
                if (listener != NULL)
                {
                    listener->setCallbackVsync(m_callback_vsync, m_callback_vsync_data);
                }

                // SurfaceFlinger has removed VSync callback, so we should disable VSync thread.
                // It can avoid that SurfaceFlinger get VSync when new SurfaceFlinger registers
                // a new callback function without enable VSync.
                if (m_callback_vsync == NULL)
                {
                    for (auto& hwc_display : m_displays)
                    {
                        hwc_display->setVsyncEnabled(false);
                    }
                }
            }
            break;
        case HWC2_CALLBACK_REFRESH:
            {
                m_callback_refresh = reinterpret_cast<HWC2_PFN_REFRESH>(pointer);
                m_callback_refresh_data = callback_data;
                sp<DisplayListener> listener = (DisplayListener *) DisplayManager::getInstance().getListener().get();
                HWC_LOGI("Register refresh callback(ptr=%p)", m_callback_refresh);
                if (listener != NULL)
                {
                    listener->setCallbackRefresh(m_callback_refresh, m_callback_refresh_data);
                }
            }
            break;

        case HWC2_CALLBACK_VSYNC_2_4:
            {
                m_callback_vsync_2_4 = reinterpret_cast<HWC2_PFN_VSYNC_2_4>(pointer);
                m_callback_vsync_2_4_data = callback_data;
                sp<DisplayListener> listener = reinterpret_cast<DisplayListener*>(DisplayManager::getInstance().getListener().get());
                HWC_LOGI("Register vsync_2_4 callback(ptr=%p)", m_callback_vsync_2_4);
                if (listener != NULL)
                {
                    listener->setCallbackVsync_2_4(m_callback_vsync_2_4, m_callback_vsync_2_4_data);
                }
            }
            break;

        case HWC2_CALLBACK_VSYNC_PERIOD_TIMING_CHANGED:
            {
                m_callback_vsync_timing_changed = reinterpret_cast<HWC2_PFN_VSYNC_PERIOD_TIMING_CHANGED>(pointer);
                m_callback_vsync_timing_changed_data = callback_data;
                sp<DisplayListener> listener = reinterpret_cast<DisplayListener*>(DisplayManager::getInstance().getListener().get());
                HWC_LOGI("Register vsync_timing_change callback(ptr=%p)", m_callback_vsync_timing_changed);
                if (listener != NULL)
                {
                    listener->setCallbackVsyncPeriodTimingChanged(m_callback_vsync_timing_changed,
                            m_callback_vsync_timing_changed_data);
                }
            }
            break;

        case HWC2_CALLBACK_SEAMLESS_POSSIBLE:
            {
                m_callback_seamless_possible = reinterpret_cast<HWC2_PFN_SEAMLESS_POSSIBLE>(pointer);
                m_callback_seamless_possible_data = callback_data;
                sp<DisplayListener> listener = reinterpret_cast<DisplayListener*>(DisplayManager::getInstance().getListener().get());
                HWC_LOGI("Register seamless_possible callback(ptr=%p)", m_callback_seamless_possible);
                if (listener != NULL)
                {
                    listener->setCallbackSeamlessPossible(m_callback_seamless_possible,
                            m_callback_seamless_possible_data);
                }
            }
            break;

        default:
            HWC_LOGE("%s: unknown descriptor(%d)", __func__, descriptor);
            return HWC2_ERROR_BAD_PARAMETER;
    }

    if (!m_is_init_disp_manager)
    {
        bool has_all_callback = false;
        // HWC version: 1.x~2.3
        if (m_callback_vsync && m_callback_hotplug && m_callback_refresh)
        {
            has_all_callback = true;
        }
        // HWC version: 2.4~
        else if (m_callback_vsync_2_4 && m_callback_hotplug && m_callback_refresh &&
                m_callback_vsync_timing_changed && m_callback_seamless_possible)
        {
            has_all_callback = true;
        }

        if (has_all_callback)
        {
            m_is_init_disp_manager = true;
            DisplayManager::getInstance().setListener(
                    new DisplayListener(
                    m_callback_hotplug,
                    m_callback_hotplug_data,
                    m_callback_vsync,
                    m_callback_vsync_data,
                    m_callback_refresh,
                    m_callback_refresh_data,
                    m_callback_vsync_2_4,
                    m_callback_vsync_2_4_data,
                    m_callback_vsync_timing_changed,
                    m_callback_vsync_timing_changed_data,
                    m_callback_seamless_possible,
                    m_callback_seamless_possible_data));
            // initialize DisplayManager
            for (auto& hwc_display : m_displays)
            {
                if (hwc_display->getId() == 2)
                {
                    continue;
                }

                if (hwc_display->isInternal())
                    DisplayManager::getInstance().initInternal(hwc_display->getId(), hwc_display->getDrmIdCurCrtc());
                else if (!hwc_display->isInternal() && hwc_display->getId() == HWC_DISPLAY_PRIMARY)
                    DisplayManager::getInstance().initPrimaryExternal(hwc_display->getId(), hwc_display->getDrmIdCurCrtc());
            }
        }
    }
    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator::deviceGetDataspaceSaturationMatrix(
     hwc2_device_t* /*device*/,
     int32_t /*android_dataspace_t*/ dataspace,
     float* out_matrix)
{
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

    if (m_version_major >= 3)
    {
        if (dataspace != HAL_DATASPACE_V0_SRGB_LINEAR)
        {
            return HWC2_ERROR_BAD_PARAMETER;
        }
    }
    else
    {
        if (dataspace != HAL_DATASPACE_SRGB_LINEAR)
        {
            return HWC2_ERROR_BAD_PARAMETER;
        }
    }

    return HWC2_ERROR_NONE;
}

/* Display functions */
int32_t /*hwc2_error_t*/ HWCMediator::displayAcceptChanges(
    hwc2_device_t* /*device*/,
    hwc2_display_t display)
{
    CHECK_DISP_CONNECT(display);

    getHWCDisplay(display)->acceptChanges();

    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator::displayCreateLayer(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    hwc2_layer_t* out_layer)
{
    CHECK_DISP_CONNECT(display);

    return getHWCDisplay(display)->createLayer(out_layer, false);
}

int32_t /*hwc2_error_t*/ HWCMediator::displayDestroyLayer(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    hwc2_layer_t layer)
{
    CHECK_DISP_CONNECT(display);

    sp<HWCDisplay> hwc_display = getHWCDisplay(display);
    sp<HWCLayer> hwc_layer = hwc_display->getLayer(layer);
    CHECK_DISP_LAYER(display, layer, hwc_layer);
    if(hwc_layer->isClientTarget())
    {
        HWC_LOGE("%s: the display(%" PRIu64 ") can't remove ClientTarget(%" PRIu64 ")", __func__, display, layer);
        return HWC2_ERROR_BAD_LAYER;
    }

    return hwc_display->destroyLayer(layer);
}

int32_t /*hwc2_error_t*/ HWCMediator::displayGetActiveConfig(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    hwc2_config_t* out_config)
{
    CHECK_DISP_CONNECT(display);

    *out_config = getHWCDisplay(display)->getActiveConfig();

    if (!getHWCDisplay(display)->isInternal())
        DisplayManager::getInstance().notifyHotplugInDone(getHWCDisplay(display)->getId());
    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator::displayGetBrightnessSupport(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    bool* out_support)
{
    CHECK_DISP(display);

    if (HwcFeatureList::getInstance().getFeature().is_support_brightness)
    {
        sp<HWCDisplay> hwc_display = getHWCDisplay(display);
        return hwc_display->getBrightnessSupport(out_support);
    }
    *out_support = false;
    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator::displayGetChangedCompositionTypes(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    uint32_t* out_num_elem,
    hwc2_layer_t* out_layers,
    int32_t* /*hwc2_composition_t*/ out_types)
{
    CHECK_DISP_CONNECT(display);

    sp<HWCDisplay> hwc_display = getHWCDisplay(display);
    if (!hwc_display->isValidated())
    {
        HWC_LOGE("%s: the display(%" PRIu64 ") does not validate yet", __func__, display);
        return HWC2_ERROR_NOT_VALIDATED;
    }

    hwc_display->getChangedCompositionTypes(out_num_elem, out_layers, out_types);

    // sent composition mode changes to SF
    bool has_changed = false;
    int new_mode = MTK_COMPOSITION_MODE_NORMAL;

    hwc_display->getCompositionMode(has_changed, new_mode);

    if (has_changed)
    {
        ComposerExt::HwcCompositionStruct composition{ .disp_id = display };
        composition.composition_mode = (new_mode == MTK_COMPOSITION_MODE_NORMAL) ?
                                       ComposerExt::HwcCompositionMode::kNormal :
                                       ComposerExt::HwcCompositionMode::kDecouple;

        std::lock_guard<std::mutex> lock(m_ext_lock);
        for (auto iter = m_ext_callback_list.begin(); iter != m_ext_callback_list.end(); ++iter)
        {
            std::shared_ptr<ComposerExt::ConfigCallback> callback = iter->second.callback.lock();
            if (callback)
            {
                callback->NotifyHwcComposition(composition);
            }
        }
    }

    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator::displayGetClientTargetSupport(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    uint32_t width,
    uint32_t height,
    int32_t /*android_pixel_format_t*/ format,
    int32_t /*android_dataspace_t*/ dataspace)
{
    CHECK_DISP(display);

    sp<HWCDisplay> hwc_display = getHWCDisplay(display);
    nsecs_t now = systemTime();
    hwc2_config_t config = hwc_display->getActiveConfig(now);
    if (hwc_display->getWidth(config) != static_cast<int32_t>(width) ||
        hwc_display->getHeight(config) != static_cast<int32_t>(height))
    {
        HWC_LOGW("%s: this display(%" PRIu64 ") does not support the CT format(%ux%u:%d:%d)",
                 __func__, display, width, height, format, dataspace);
        return HWC2_ERROR_UNSUPPORTED;
    }

    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator::displayGetColorMode(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    uint32_t* out_num_modes,
    int32_t* out_modes)
{
    CHECK_DISP(display);

    sp<HWCDisplay> hwc_display = getHWCDisplay(display);
    return hwc_display->getColorModeList(out_num_modes, out_modes);
}

int32_t /*hwc2_error_t*/ HWCMediator::displayGetAttribute(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    hwc2_config_t config,
    int32_t /*hwc2_attribute_t*/ attribute,
    int32_t* out_value)
{
    CHECK_DISP_CONNECT(display);

    int32_t err = getHWCDisplay(display)->getAttribute(config, attribute, out_value);
    if (err == HWC2_ERROR_NONE && attribute == HWC2_ATTRIBUTE_MTK_AOSP)
        m_is_mtk_aosp = true;

    return err;
}

int32_t /*hwc2_error_t*/ HWCMediator::displayGetConfigs(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    uint32_t* out_num_configs,
    hwc2_config_t* out_configs)
{
    CHECK_DISP_CONNECT(display);

    return getHWCDisplay(display)->getConfigs(out_num_configs, out_configs);
}

int32_t /*hwc2_error_t*/ HWCMediator::displayGetCapabilities(
        hwc2_device_t* device,
        hwc2_display_t display,
        uint32_t* outNumCapabilities,
        uint32_t* outCapabilities)
{
    CHECK_DISP_CONNECT(display);

    sp<HWCDisplay> hwc_display = getHWCDisplay(display);
    uint32_t numCapabilities = 0;
    if (hasCapabilities(HWC2_CAPABILITY_SKIP_CLIENT_COLOR_TRANSFORM))
    {
        if (outCapabilities != nullptr && hwc_display->isSupportDispPq())
        {
            outCapabilities[numCapabilities] = HWC2_DISPLAY_CAPABILITY_SKIP_CLIENT_COLOR_TRANSFORM;
        }
        numCapabilities++;
    }
    int32_t doze_support = 0;
    if (displayGetDozeSupport(device, display, &doze_support) == HWC2_ERROR_NONE)
    {
        if (doze_support != 0)
        {
            if (outCapabilities != nullptr)
            {
                outCapabilities[numCapabilities] = HWC2_DISPLAY_CAPABILITY_DOZE;
            }
            numCapabilities++;
        }
    }
    bool brightness_support = false;
    if (displayGetBrightnessSupport(device, display, &brightness_support) == HWC2_ERROR_NONE)
    {
        if (brightness_support == true)
        {
            if (outCapabilities != nullptr)
            {
                outCapabilities[numCapabilities] = HWC2_DISPLAY_CAPABILITY_BRIGHTNESS;
            }
            numCapabilities++;
        }
    }

    if (outNumCapabilities != nullptr)
    {
        *outNumCapabilities = numCapabilities;
    }
    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator::displayGetName(
    hwc2_device_t* /*device*/,
    hwc2_display_t /*display*/,
    uint32_t* /*out_lens*/,
    char* /*out_name*/)
{
    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator::displayGetRequests(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    int32_t* /*hwc2_display_request_t*/ /*out_display_requests*/,
    uint32_t* out_num_elem,
    hwc2_layer_t* out_layers,
    int32_t* /*hwc2_layer_request_t*/ out_layer_requests)
{
    CHECK_DISP_CONNECT(display);

    if (!getHWCDisplay(display)->isValidated())
    {
        HWC_LOGE("%s: the display(%" PRIu64 ") does not validate yet", __func__, display);
        return HWC2_ERROR_NOT_VALIDATED;
    }

    getHWCDisplay(display)->getChangedRequests(out_num_elem, out_layers, out_layer_requests);

    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator::displayGetType(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    int32_t* /*hwc2_display_type_t*/ out_type)
{
    CHECK_DISP_CONNECT(display);

    getHWCDisplay(display)->getType(out_type);

    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator::displayGetDozeSupport(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    int32_t* out_support)
{
    CHECK_DISP_CONNECT(display);

    if (getHWCDisplay(display)->isInternal())
    {
        *out_support = HwcFeatureList::getInstance().getFeature().aod;
    }
    else
    {
        *out_support = false;
    }

    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator::displayGetHdrCapability(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    uint32_t* out_num_types,
    int32_t* /*android_hdr_t*/ out_types,
    float* /*out_max_luminance*/,
    float* /*out_max_avg_luminance*/,
    float* /*out_min_luminance*/)
{
    CHECK_DISP_CONNECT(display);
    if (out_types == NULL)
    {
        *out_num_types = static_cast<uint32_t>(m_hdr_capabilities.size());
    }
    else
    {
        DbgLogger logger(DbgLogger::TYPE_HWC_LOG, 'I', "(%" PRIu64 ") %s() caps:", display, __func__);
        for(uint32_t i = 0; i < *out_num_types; ++i)
        {
            out_types[i] = m_hdr_capabilities[i];
            logger.printf("%d,", out_types[i]);
        }
    }

    return HWC2_ERROR_NONE;
}

void HWCMediator::setupHDRFeature()
{
    m_hdr_capabilities.push_back(HAL_HDR_HDR10);
    m_hdr_capabilities.push_back(HAL_HDR_HLG);
    m_hdr_capabilities.push_back(HAL_HDR_HDR10_PLUS);

    //smpte2086
    m_hdr_metadata_keys.push_back(HWC2_DISPLAY_RED_PRIMARY_X);
    m_hdr_metadata_keys.push_back(HWC2_DISPLAY_RED_PRIMARY_Y);
    m_hdr_metadata_keys.push_back(HWC2_DISPLAY_GREEN_PRIMARY_X);
    m_hdr_metadata_keys.push_back(HWC2_DISPLAY_GREEN_PRIMARY_Y);
    m_hdr_metadata_keys.push_back(HWC2_DISPLAY_BLUE_PRIMARY_X);
    m_hdr_metadata_keys.push_back(HWC2_DISPLAY_BLUE_PRIMARY_Y);
    m_hdr_metadata_keys.push_back(HWC2_WHITE_POINT_X);
    m_hdr_metadata_keys.push_back(HWC2_WHITE_POINT_Y);
    m_hdr_metadata_keys.push_back(HWC2_MAX_LUMINANCE);
    m_hdr_metadata_keys.push_back(HWC2_MIN_LUMINANCE);

    //cta861_3
    m_hdr_metadata_keys.push_back(HWC2_MAX_CONTENT_LIGHT_LEVEL);
    m_hdr_metadata_keys.push_back(HWC2_MAX_FRAME_AVERAGE_LIGHT_LEVEL);

    //ST2094-40 SEI
    m_hdr_metadata_keys.push_back(static_cast<int32_t>(PerFrameMetadataKey::HDR10_PLUS_SEI));
}

int32_t /*hwc2_error_t*/ HWCMediator::displayGetPerFrameMetadataKeys(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    uint32_t* outNumKeys,
    int32_t* /*hwc2_per_frame_metadata_key_t*/ outKeys)
{
    CHECK_DISP_CONNECT(display);

    if (outKeys == NULL)
    {
        *outNumKeys = static_cast<uint32_t>(m_hdr_metadata_keys.size());
    }
    else
    {
        DbgLogger logger(DbgLogger::TYPE_HWC_LOG, 'I', "(%" PRIu64 ") %s() keys:", display, __func__);
        for(uint32_t i = 0; i < *outNumKeys; ++i)
        {
            outKeys[i] = m_hdr_metadata_keys[i];
            logger.printf("%d,", outKeys[i]);
        }
    }
    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator::displayGetReleaseFence(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    uint32_t* out_num_elem,
    hwc2_layer_t* out_layer,
    int32_t* out_fence)
{
    CHECK_DISP_CONNECT(display);

    getHWCDisplay(display)->getReleaseFenceFds(out_num_elem, out_layer, out_fence);
    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator::displayPresent(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    int32_t* out_retire_fence)
{
    CHECK_DISP(display);

    sp<HWCDisplay> hwc_display = getHWCDisplay(display);

    HWC_LOGV("(%" PRIu64 ") %s", display, __func__);
    HWC_ABORT_MSG("(%" PRIu64 ") %s s:%s=>", display, __func__, getPresentValiStateString(hwc_display->getValiPresentState()));
    if (!isDispConnected(display))
    {
        HWC_LOGE("%s: the display(%" PRIu64 ") is not connected", __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    if (m_is_validate_separate)
    {
        if (hasCapabilities(HWC2_CAPABILITY_SKIP_VALIDATE))
        {
            switch (hwc_display->getValiPresentState())
            {
                case HWC_VALI_PRESENT_STATE_PRESENT_DONE:
                    if (hwc_display->isValid())
                    {
                        hwc_display->updateActiveConfigWithConstraints();
                        hwc_display->buildVisibleAndInvisibleLayer();

                        hwc_display->prepareSfLog();
                        hwc_display->prepareForValidation();
                        hwc_display->setJobVideoTimeStamp();
                        if (hwc_display->checkSkipValidate(false))
                        {
                            HrtHelper::getInstance().run(display, true);
                            hwc_display->setValiPresentState(HWC_VALI_PRESENT_STATE_VALIDATE_DONE, __LINE__);
                            if (hwc_display->isSupportSmartComposition() && hwc_display->hasValidCurrentSet())
                            {
                                hwc_display->setGpucSkipValidate(true);
                            }
                        }
                        else
                        {
                            hwc_display->setValiPresentState(HWC_VALI_PRESENT_STATE_FALLBACK_VALIDATE, __LINE__);
                            return HWC2_ERROR_NOT_VALIDATED;
                        }
                    }
                    else
                    {
                        hwc_display->buildVisibleAndInvisibleLayer();
                        hwc_display->prepareSfLog();
                        hwc_display->initSFCompTypesBeforeValid();
                        hwc_display->setValiPresentState(HWC_VALI_PRESENT_STATE_FALLBACK_VALIDATE, __LINE__);
                        // do not create job & do HRT, we are not goint to show this frame
                        return HWC2_ERROR_NOT_VALIDATED;
                    }
                    break;

                case HWC_VALI_PRESENT_STATE_FALLBACK_VALIDATE:
                    return HWC2_ERROR_NOT_VALIDATED;

                case HWC_VALI_PRESENT_STATE_VALIDATE_DONE:
                    break;

                default:
                    HWC_LOGE("%s: the display(%" PRIu64 ") is presented with invalid state %s", __func__, display,
                             getPresentValiStateString(hwc_display->getValiPresentState()));
                    HWC_ASSERT(0);
                    return HWC2_ERROR_NOT_VALIDATED ;
            }
        }
    }
    else
    {
        if (hasCapabilities(HWC2_CAPABILITY_SKIP_VALIDATE) &&
            (hwc_display->getValiPresentState() == HWC_VALI_PRESENT_STATE_PRESENT_DONE ||
             hwc_display->getValiPresentState() == HWC_VALI_PRESENT_STATE_VALIDATE ||
             hwc_display->getValiPresentState() == HWC_VALI_PRESENT_STATE_CHECK_SKIP_VALI))
        {
            if (hwc_display->getValiPresentState() == HWC_VALI_PRESENT_STATE_PRESENT_DONE &&
                hwc_display->getUnpresentCount() == 0)
            {
                updateActiveConfigWithConstraintsForAllDisplay();
                buildVisibleAndInvisibleLayerForAllDisplay();

                int32_t mirror_sink_dpy = -1;
                if (getHWCDisplay(HWC_DISPLAY_EXTERNAL)->isConnected() ||
                    getHWCDisplay(HWC_DISPLAY_EXTERNAL_1)->isConnected() ||
                    getHWCDisplay(HWC_DISPLAY_VIRTUAL)->isConnected())
                {
                    mirror_sink_dpy = checkMirrorPath(m_displays);
                }

                const bool use_decouple_mode = mirror_sink_dpy != -1;

                if (DisplayManager::getInstance().getDisplayConnected(HWC_DISPLAY_PRIMARY))
                    HWCDispatcher::getInstance().setSessionMode(HWC_DISPLAY_PRIMARY, use_decouple_mode);
                else if (DisplayManager::getInstance().getDisplayConnected(HWC_DISPLAY_EXTERNAL))
                    HWCDispatcher::getInstance().setSessionMode(HWC_DISPLAY_EXTERNAL, use_decouple_mode);

                prepareSfLog();
                prepareForValidation();
                setJobVideoTimeStamp();
                setNeedValidate(HWC_SKIP_VALIDATE_NOT_SKIP);
                setValiPresentStateOfAllDisplay(HWC_VALI_PRESENT_STATE_CHECK_SKIP_VALI, __LINE__);
                if (checkSkipValidate(false) == true)
                    setNeedValidate(HWC_SKIP_VALIDATE_SKIP);
                else
                    setNeedValidate(HWC_SKIP_VALIDATE_NOT_SKIP);
            }

            if (getNeedValidate() == HWC_SKIP_VALIDATE_NOT_SKIP)
            {
                return HWC2_ERROR_NOT_VALIDATED;
            }
            else
            {
                HrtHelper::getInstance().run(true);
                setValiPresentStateOfAllDisplay(HWC_VALI_PRESENT_STATE_VALIDATE_DONE, __LINE__);
                if (hwc_display->isSupportSmartComposition() && hwc_display->hasValidCurrentSet())
                {
                    m_gpuc_skip_validate = true;
                }
            }
        }
        hwc_display->decUnpresentCount();
    }

    if (hwc_display->getValiPresentState() == HWC_VALI_PRESENT_STATE_VALIDATE_DONE)
    {
        if (hwc_display->isConnected())
        {
            hwc_display->buildCommittedLayers();
            HWC_LOGV("(%" PRIu64 ") %s getCommittedLayers() size:%zu",
                     hwc_display->getId(), __func__, hwc_display->getCommittedLayers().size());
            hwc_display->beforePresent(DisplayManager::getInstance().getNumberPluginDisplay());
            hwc_display->present();
            HWCDispatcher::getInstance().trigger(display);
        }
    }

    if (hwc_display->getRetireFenceFd() == -1)
    {
        *out_retire_fence = -1;
    }
    else
    {
#ifdef USES_FENCE_RENAME
        *out_retire_fence = SyncFence::merge(hwc_display->getRetireFenceFd(), hwc_display->getRetireFenceFd(), "HWC_to_SF_present");
#else
        *out_retire_fence = ::dup(hwc_display->getRetireFenceFd());
#endif
        ::protectedClose(hwc_display->getRetireFenceFd());
        hwc_display->setRetireFenceFd(-1, isDispConnected(display));
    }

    HWC_LOGV("(%" PRIu64 ") %s out_retire_fence:%d", display, __func__, *out_retire_fence);

    hwc_display->afterPresent();
    hwc_display->setValiPresentState(HWC_VALI_PRESENT_STATE_PRESENT_DONE, __LINE__);
    notifyHwbinderTid();
    getPqDevice()->afterPresent();
    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator::displaySetActiveConfig(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    hwc2_config_t config_id)
{
    CHECK_DISP_CONNECT(display);

    HWC_LOGI("(%" PRIu64 ") %s config:%d", display, __func__, config_id);
    int32_t err = getHWCDisplay(display)->setActiveConfig(config_id);

    return err;
}

int32_t /*hwc2_error_t*/ HWCMediator::displaySetBrightness(
        hwc2_device_t* /*device*/,
        hwc2_display_t display,
        float brightness)
{
    CHECK_DISP_CONNECT(display);

    if (HwcFeatureList::getInstance().getFeature().is_support_brightness)
    {
        sp<HWCDisplay> hwc_display = getHWCDisplay(display);
        return hwc_display->setBrightness(brightness);;
    }
    else
    {
        return HWC2_ERROR_UNSUPPORTED;
    }
}

int32_t /*hwc2_error_t*/ HWCMediator::displaySetClientTarget(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    buffer_handle_t handle,
    int32_t acquire_fence,
    int32_t /*dataspace*/,
    hwc_region_t damage)
{
    CHECK_DISP_CONNECT(display);

    sp<HWCDisplay> hwc_display = getHWCDisplay(display);
    sp<HWCLayer> ct = hwc_display->getClientTarget();

    ct->setHandle(handle);
    ct->setAcquireFenceFd(acquire_fence);
    ct->setDataspace(mapColorMode2DataSpace(hwc_display->getColorMode()));
    ct->setDamage(damage);
    ct->setupPrivateHandle();

    if (display == HWC_DISPLAY_VIRTUAL && hwc_display->getMirrorSrc() == -1 &&
        !Platform::getInstance().m_config.is_support_ext_path_for_virtual &&
        !HwcFeatureList::getInstance().getFeature().copyvds)
    {
        const int32_t dup_acq_fence_fd = ::dup(acquire_fence);
        HWC_LOGV("(%" PRIu64 ") setClientTarget() handle:%p acquire_fence:%d(%d)", display, handle, acquire_fence, dup_acq_fence_fd);
        hwc_display->setRetireFenceFd(dup_acq_fence_fd, true);
    }
    else
    {
        HWC_LOGV("(%" PRIu64 ") setClientTarget() handle:%p acquire_fence:%d", display, handle, acquire_fence);
    }
    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator::displaySetColorMode(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    int32_t mode)
{
    CHECK_DISP_CONNECT(display);

    HWC_LOGI("(%" PRIu64 ") %s mode:%d", display, __func__, mode);
    sp<HWCDisplay> hwc_display = getHWCDisplay(display);
    return hwc_display->setColorMode(mode);
}

int32_t /*hwc2_error_t*/ HWCMediator::displaySetColorTransform(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    const float* matrix,
    int32_t /*android_color_transform_t*/ hint)
{
    CHECK_DISP_CONNECT(display);

    sp<HWCDisplay> hwc_display = getHWCDisplay(display);

    return getHWCDisplay(display)->setColorTransform(matrix, hint);

}

int32_t /*hwc2_error_t*/ HWCMediator::displaySetOutputBuffer(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    buffer_handle_t buffer,
    int32_t release_fence)
{
    if (display != HWC_DISPLAY_VIRTUAL)
    {
        HWC_LOGE("%s: invalid display(%" PRIu64 ")", __func__, display);
        return HWC2_ERROR_UNSUPPORTED;
    }

    CHECK_DISP_CONNECT(display);

    const int32_t dup_fd = ::dup(release_fence);
    HWC_LOGV("(%" PRIu64 ") %s outbuf fence:%d->%d", display, __func__, release_fence, dup_fd);
    getHWCDisplay(display)->setOutbuf(buffer, dup_fd);
    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator::displaySetPowerMode(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    int32_t /*hwc2_power_mode_t*/ mode)
{
    CHECK_DISP_CONNECT(display);

    HWC_LOGD("%s display:%" PRIu64 " mode:%d", __func__, display, mode);
    if (!HwcFeatureList::getInstance().getFeature().aod &&
        (mode == HWC2_POWER_MODE_DOZE || mode == HWC2_POWER_MODE_DOZE_SUSPEND))
    {
        return HWC2_ERROR_UNSUPPORTED;
    }

    switch (mode)
    {
        case HWC2_POWER_MODE_OFF:
        case HWC2_POWER_MODE_ON:
        case HWC2_POWER_MODE_DOZE:
        case HWC2_POWER_MODE_DOZE_SUSPEND:
            getHWCDisplay(display)->setPowerMode(mode);
            break;

        default:
            HWC_LOGE("%s: display(%" PRIu64 ") a unknown parameter(%d)!", __func__, display, mode);
            return HWC2_ERROR_BAD_PARAMETER;
    }

    // notify sf that CT need to apply color transform when no display pq
    sp<HWCDisplay> hwc_display = getHWCDisplay(display);
    if (hwc_display->isPrevSupportDispPq() != hwc_display->isSupportDispPq())
    {
        ComposerExt::HwcDispCapabilityStruct update_capability;
        update_capability.disp_id = display;
        update_capability.capability = ComposerExt::HwcDispCapability::kSkipClientColorTransform;
        update_capability.add_capability = hwc_display->isSupportDispPq();

        std::lock_guard<std::mutex> lock(m_ext_lock);
        for (auto iter = m_ext_callback_list.begin(); iter != m_ext_callback_list.end(); ++iter)
        {
            std::shared_ptr<ComposerExt::ConfigCallback> callback = iter->second.callback.lock();
            if (callback)
            {
                callback->NotifyHwcDisplayCapability(update_capability);
            }
        }
        HWC_LOGD("%s() capability:%d update:%d", __FUNCTION__, update_capability.capability, update_capability.add_capability);
        hwc_display->setPrevSupportDispPq(hwc_display->isSupportDispPq());
    }

    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator::displaySetVsyncEnabled(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    int32_t /*hwc2_vsync_t*/ enabled)
{
    if ((getHWCDisplay(display)->getId() != HWC_DISPLAY_EXTERNAL)
        && (getHWCDisplay(display)->getId() != HWC_DISPLAY_EXTERNAL_1))
        CHECK_DISP_CONNECT(display);

    switch (enabled)
    {
        case HWC2_VSYNC_ENABLE:
            if ((getHWCDisplay(display)->getId() == HWC_DISPLAY_EXTERNAL
                || getHWCDisplay(display)->getId() == HWC_DISPLAY_EXTERNAL_1)
                && getHWCDisplay(display)->isConnected() == false)
                return HWC2_ERROR_BAD_DISPLAY;
            getHWCDisplay(display)->setVsyncEnabled(true);
            break;

        case HWC2_VSYNC_DISABLE:
            if ((getHWCDisplay(display)->getId() == HWC_DISPLAY_EXTERNAL
                || getHWCDisplay(display)->getId() == HWC_DISPLAY_EXTERNAL_1)
                && getHWCDisplay(display)->isConnected() == false)
            {
                DisplayManager::getInstance().notifyHotplugOutDone(getHWCDisplay(display)->getId());
                return HWC2_ERROR_BAD_DISPLAY;
            }
            getHWCDisplay(display)->setVsyncEnabled(false);
            break;

        default:
            HWC_LOGE("%s: display( %" PRIu64 ") a unknown parameter(%d)!", __func__, display, enabled);
            return HWC2_ERROR_BAD_PARAMETER;
    }
    return HWC2_ERROR_NONE;
}

static bool isMirrorList(const vector<sp<HWCLayer> >& src_layers,
                         const vector<sp<HWCLayer> >& sink_layers,
                         const uint32_t& src_disp,
                         const uint32_t& sink_disp)
{
    bool ret = false;
    DbgLogger logger(DbgLogger::TYPE_HWC_LOG | DbgLogger::TYPE_DUMPSYS, 'D',"mirror?(%" PRIu32 "->%" PRIu32 "): ", src_disp, sink_disp);

    if (src_disp == sink_disp)
    {
        logger.printf("E-same_dpy");
        return ret;
    }

    logger.printf("I-size(%zu|%zu) ", sink_layers.size(), src_layers.size());

    if (sink_layers.size() == 0 || src_layers.size() == 0)
    {
        // it will clearBlackground and processMirror with black src buffer
        // this means that clear black twice by MDP in this case
        // therefore, switch this case to extension path to remove redundant clear black
        logger.printf("E-null_list");
        return ret;
    }

    vector<uint64_t> src_layers_alloc_id;
    vector<uint64_t> sink_layers_alloc_id;

    for (auto& layer : src_layers)
    {
        HWC_LOGV("isMirrorList 1 layer->getHandle():%p", layer->getHandle());
        if (layer->getSFCompositionType() != HWC2_COMPOSITION_SIDEBAND)
        {
            auto& hnd =layer->getPrivateHandle();
            src_layers_alloc_id.push_back(hnd.alloc_id);
        }
        // todo: check sidebandStream
    }

    HWC_LOGV("src_layers_alloc_id size:%zu", src_layers_alloc_id.size());

    for (auto& layer : sink_layers)
    {
        HWC_LOGV("isMirrorList 2 layer->getHandle():%p", layer->getHandle());
        if (layer->getSFCompositionType() != HWC2_COMPOSITION_SIDEBAND)
        {
            auto& hnd =layer->getPrivateHandle();
            sink_layers_alloc_id.push_back(hnd.alloc_id);
        }
        // todo: check sidebandStream
    }

    if (src_layers_alloc_id == sink_layers_alloc_id)
    {
        logger.printf("T2 ");
        ret = true;
    }

    logger.printf("E-%d", ret);
    return ret;
}

// checkMirrorPath() checks if mirror path exists
int32_t checkMirrorPath(const vector<sp<HWCDisplay> >& displays)
{
    DbgLogger logger(DbgLogger::TYPE_HWC_LOG | DbgLogger::TYPE_DUMPSYS, 'D', "chkMir(%zu: ", displays.size());

    // display id of mirror source
    const uint32_t mir_dpy = HWC_DISPLAY_PRIMARY;
    auto&& src_layers = displays[HWC_DISPLAY_PRIMARY]->getVisibleLayersSortedByZ();
    for (uint32_t i = 1; i <= HWC_DISPLAY_EXTERNAL_1; ++i)
    {
        const DisplayData* display_data = DisplayManager::getInstance().getDisplayData(i);
        auto& display = displays[i];
        const uint64_t disp_id = display->getId();
        auto&& layers = display->getVisibleLayersSortedByZ();
        if (DisplayManager::MAX_DISPLAYS <= disp_id)
            continue;

        if (!display->isConnected())
            continue;

        if (display->isGpuComposition())
            continue;

        if (HWC_DISPLAY_PRIMARY == disp_id)
        {
            display->setMirrorSrc(-1);
            logger.printf("(%" PRIu32 ":L%d) ", i, __LINE__);
            continue;
        }

        if (listForceGPUComp(layers))
        {
            display->setMirrorSrc(-1);
            logger.printf("(%" PRIu32 ":L%d) ", i, __LINE__);
            continue;
        }

        auto& mirrored_display = displays[mir_dpy];
        if (!(mirrored_display->isColorTransformOK()))
        {
            display->setMirrorSrc(-1);
            logger.printf("(%" PRIu32 ":L%d) ", i, __LINE__);
            continue;
        }

        if ((Platform::getInstance().m_config.mirror_state & MIRROR_DISABLED) ||
            (Platform::getInstance().m_config.mirror_state & MIRROR_PAUSED))
        {
            // disable mirror mode
            // either the mirror state is disabled or the mirror source is blanked
            display->setMirrorSrc(-1);
            logger.printf("(%" PRIu64 ":L%d) ", disp_id, __LINE__);
            continue;
        }

        const bool is_mirror_list = isMirrorList(src_layers, layers, mir_dpy, static_cast<uint32_t>(disp_id));

        // the layer list is different with primary display
        if (!is_mirror_list)
        {
            display->setMirrorSrc(-1);
            logger.printf("(%" PRIu64 ":L%d) ", disp_id, __LINE__);
            continue;
        }

        if (listSecure(layers))
        {
            // if any secure or protected layer exists in mirror source
            // CMDQ will check display wlv1 secure for ext path
            display->setMirrorSrc(-1);
            logger.printf("(%" PRIu32 ":L%d) ", i, __LINE__);
            continue;
        }

        if (!HwcFeatureList::getInstance().getFeature().copyvds &&
            layers.empty())
        {
            // disable mirror mode
            // since force copy vds is not used
            display->setMirrorSrc(-1);
            logger.printf("(%" PRIu32 ":L%d) ", i, __LINE__);
            continue;
        }

        // check enlargement ratio (i.e. scale ratio > 0)
        if (Platform::getInstance().m_config.mir_scale_ratio > 0)
        {
            const DisplayData* mir_display_data =
                DisplayManager::getInstance().getDisplayData(mir_dpy, displays[mir_dpy]->getActiveConfig());
            float scaled_ratio = display_data->pixels / static_cast<float>(mir_display_data->pixels);

            if (scaled_ratio > Platform::getInstance().m_config.mir_scale_ratio)
            {
                // disable mirror mode
                // since scale ratio exceeds the maximum one
                display->setMirrorSrc(-1);
                logger.printf("(%" PRIu32 ":L%d) ", i, __LINE__);
                continue;
            }
        }
        display->setMirrorSrc(static_cast<int32_t>(mir_dpy));
        logger.printf("mir");
        return static_cast<int32_t>(disp_id);
    }
    logger.printf("!mir");
    return -1;
}

int32_t /*hwc2_error_t*/ HWCMediator::displayValidateDisplay(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    uint32_t* out_num_types,
    uint32_t* out_num_requests)
{
    CHECK_DISP(display);

    if (m_present_idx_action >= 0)
    {
        sp<HWCDisplay> hwc_display = getHWCDisplay(m_present_idx_display_id);
        if (hwc_display != nullptr)
        {
            if (m_present_idx_action == 0)
            {
                HWC_LOGI("try to destroy present indexlayer");
                hwc_display->destroyPresentIdxLayer();
                IndexBufferGenerator::getInstance().stop();
            }
            else
            {
                HWC_LOGI("try to create present layer");
                hwc_display->createPresentIdxLayer(m_present_idx_width,
                        m_present_idx_height, m_present_idx_x, m_present_idx_y);
                IndexBufferGenerator::getInstance().start();
            }
        }
        m_present_idx_action = -1;
    }

    sp<HWCDisplay> hwc_display = getHWCDisplay(display);

    HWC_LOGD("(%" PRIu64 ") %s s:%s", display, __func__, getPresentValiStateString(hwc_display->getValiPresentState()));
    bool is_validate_only_one_display = false;
    HWC_ABORT_MSG("(%" PRIu64 ") %s s:%s", display, __func__, getPresentValiStateString(hwc_display->getValiPresentState()));

    if (!isDispConnected(display))
    {
        if (hwc_display->getId() == HWC_DISPLAY_PRIMARY)
            hwc_display->removePendingRemovedLayers();
        return HWC2_ERROR_BAD_DISPLAY;
    }

    if (HWCDispatcher::getInstance().getExistJob(hwc_display->getId()) == nullptr)
    {
        /* SF always need to do validate and present for primary display */
        if ((hwc_display->getId() != HWC_DISPLAY_PRIMARY) && (hwc_display->isInternal() == false))
        {
            HWC_LOGI("(%" PRIu64 ") %s: dispatcher won't setJob when external display is onPlugOut",
                     display, __func__);
            return HWC2_ERROR_BAD_DISPLAY;
        }
    }

    if (m_is_validate_separate)
    {
        switch (hwc_display->getValiPresentState())
        {
            case HWC_VALI_PRESENT_STATE_PRESENT_DONE:
                if (hwc_display->isValid())
                {
                    hwc_display->updateActiveConfigWithConstraints();
                    hwc_display->buildVisibleAndInvisibleLayer();

                    hwc_display->prepareSfLog();
                    hwc_display->prepareForValidation();
                    hwc_display->setJobVideoTimeStamp();
                    if (Platform::getInstance().m_config.is_skip_hrt &&
                        hwc_display->checkSkipValidate(true))
                    {
                        HWC_LOGV("%s() dispay %" PRIu64 " - skip HRT", __FUNCTION__, display);
                        HrtHelper::getInstance().run(display, true);
                    }
                    else
                    {
                        hwc_display->validate();
                        hwc_display->calculateMdpDstRoi();
                        HrtHelper::getInstance().run(display, false);
                        hwc_display->countdowmSkipValiRelatedNumber();
                    }
                }
                else
                {
                    hwc_display->buildVisibleAndInvisibleLayer();
                    hwc_display->prepareSfLog();
                    hwc_display->initSFCompTypesBeforeValid();
                    // do not create job & do HRT, we are not goint to show this frame
                    hwc_display->countdowmSkipValiRelatedNumber();
                }
                break;

            case HWC_VALI_PRESENT_STATE_FALLBACK_VALIDATE:
                if (hwc_display->isValid())
                {
                    hwc_display->validate();
                    hwc_display->calculateMdpDstRoi();
                    HrtHelper::getInstance().run(display, false);
                    hwc_display->countdowmSkipValiRelatedNumber();
                }
                else
                {
                    hwc_display->countdowmSkipValiRelatedNumber();
                }
                break;

            case HWC_VALI_PRESENT_STATE_VALIDATE_DONE:
                break;

            default:
                HWC_LOGE("%s: the display(%" PRIu64 ") is validated with invalid state %s", __func__, display,
                         getPresentValiStateString(hwc_display->getValiPresentState()));
                HWC_ASSERT(0);
                return HWC2_ERROR_BAD_DISPLAY;
        }
    }
    else
    {
        if (hwc_display->getValiPresentState() == HWC_VALI_PRESENT_STATE_PRESENT_DONE)
        {
            if (hwc_display->getUnpresentCount() == 0)
            {
                updateActiveConfigWithConstraintsForAllDisplay();
                buildVisibleAndInvisibleLayerForAllDisplay();
                int32_t mirror_sink_dpy = -1;
                if (getHWCDisplay(HWC_DISPLAY_EXTERNAL)->isConnected() ||
                    getHWCDisplay(HWC_DISPLAY_EXTERNAL_1)->isConnected() ||
                    getHWCDisplay(HWC_DISPLAY_VIRTUAL)->isConnected())
                {
                    mirror_sink_dpy = checkMirrorPath(m_displays);
                }

                const bool use_decouple_mode = mirror_sink_dpy != -1;

                if (DisplayManager::getInstance().getDisplayConnected(HWC_DISPLAY_PRIMARY))
                    HWCDispatcher::getInstance().setSessionMode(HWC_DISPLAY_PRIMARY, use_decouple_mode);
                else if (DisplayManager::getInstance().getDisplayConnected(HWC_DISPLAY_EXTERNAL))
                    HWCDispatcher::getInstance().setSessionMode(HWC_DISPLAY_EXTERNAL, use_decouple_mode);

                prepareSfLog();
                prepareForValidation();
                setJobVideoTimeStamp();

                if (Platform::getInstance().m_config.is_skip_hrt)
                {
                    if (checkSkipValidate(true))
                        setNeedValidate(HWC_SKIP_VALIDATE_SKIP);
                    else
                        setNeedValidate(HWC_SKIP_VALIDATE_NOT_SKIP);
                }
                else
                {
                    setNeedValidate(HWC_SKIP_VALIDATE_NOT_SKIP);
                }
            }
            else
            {
                if (hwc_display->getPrevUnpresentCount() >
                    hwc_display->getUnpresentCount())
                {
                    is_validate_only_one_display = true;
                    hwc_display->updateActiveConfigWithConstraints();
                    hwc_display->buildVisibleAndInvisibleLayer();
                    hwc_display->createJob();
                    hwc_display->initSFCompTypesBeforeValid();
                }
                else
                {
                    hwc_display->setValiPresentState(HWC_VALI_PRESENT_STATE_VALIDATE, __LINE__);
                }
            }
        }

        if (hwc_display->getValiPresentState() == HWC_VALI_PRESENT_STATE_PRESENT_DONE ||
            hwc_display->getValiPresentState() == HWC_VALI_PRESENT_STATE_CHECK_SKIP_VALI)
        {
            if (getNeedValidate() == HWC_SKIP_VALIDATE_NOT_SKIP)
            {
                if (is_validate_only_one_display)
                {
                    if (hwc_display->isValid())
                    {
                        hwc_display->validate();
                        hwc_display->calculateMdpDstRoi();
                    }
                    HrtHelper::getInstance().run(false);
                }
                else
                {
                    validate();
                    calculateMdpDstRoi();
                    HrtHelper::getInstance().run(false);
                    countdowmSkipValiRelatedNumber();
                }
            }
            else
            {
                HWC_LOGV("%s() - skip HRT", __func__);
                HrtHelper::getInstance().run(true);
                setValiPresentStateOfAllDisplay(HWC_VALI_PRESENT_STATE_VALIDATE, __LINE__);
            }
        }
    }

    const vector<int32_t>& prev_comp_types = hwc_display->getSFCompTypesBeforeValid();
    vector<sp<HWCLayer> > changed_comp_types;
    vector<sp<HWCLayer> > hwclayer_requests;
    {
        // DbgLogger logger(DbgLogger::TYPE_HWC_LOG, 'D', "(%d) validateDisplay(): ", display);

        auto&& layers = hwc_display->getVisibleLayersSortedByZ();

        for (size_t i = 0; i < layers.size(); ++i)
        {
            // if the prev frame is gpuc_skip_validate, the comp type may be diff(SF:DEV/ HWC:CLI),
            // then, in order to make sure they are the same comp type,
            // this 1st Validate should update the latest hwc comp type to sf
            if ((m_gpuc_skip_validate || hwc_display->getGpucSkipValidate()) && hwc_display->isSupportSmartComposition())
            {
                if ((layers[i]->getReturnedCompositionType() != prev_comp_types[i]  ||
                    layers[i]->getReturnedCompositionType() != layers[i]->getSFCompositionType()) &&
                    layers[i]->getDebugType() != HWC_DEBUG_LAYER_TYPE_PRESENT_IDX)
                {
                    changed_comp_types.push_back(layers[i]);
                }
            }
            else if ((layers[i]->getReturnedCompositionType() != prev_comp_types[i] || (layers[i]->getReturnedCompositionType() != layers[i]->getSFCompositionType())) &&
                layers[i]->getDebugType() != HWC_DEBUG_LAYER_TYPE_PRESENT_IDX)
            {
                changed_comp_types.push_back(layers[i]);
            }

            if (layers[i]->getHWCRequests() != 0)
            {
                hwclayer_requests.push_back(layers[i]);
            }

#ifndef MTK_USER_BUILD
            HWC_LOGD("(%" PRIu64 ") val %s req(%d)", display, layers[i]->toString8().string(), layers[i]->getHWCRequests());
            HWC_LOGD("    caps:%x ucnt:%d icnt:%d", layers[i]->getLayerCaps(), layers[i]->getUnchangedCnt(), layers[i]->getInactiveCnt());
#else
            if (Debugger::m_skip_log != 1)
            {
                HWC_LOGV("(%" PRIu64 ") val %s req(%d)", display, layers[i]->toString8().string(), layers[i]->getHWCRequests());
                HWC_LOGV("    caps:%x ucnt:%d icnt:%d", layers[i]->getLayerCaps(), layers[i]->getUnchangedCnt(), layers[i]->getInactiveCnt());
            }
#endif
        }
    }

    hwc_display->moveChangedCompTypes(&changed_comp_types);
    hwc_display->moveChangedHWCRequests(&hwclayer_requests);
    *out_num_types = static_cast<uint32_t>(changed_comp_types.size());
    *out_num_requests = static_cast<uint32_t>(hwclayer_requests.size());

    hwc_display->setValiPresentState(HWC_VALI_PRESENT_STATE_VALIDATE_DONE, __LINE__);
    m_gpuc_skip_validate = false;

    return HWC2_ERROR_NONE;
}

int32_t HWCMediator::displayGetDisplayedContentSamplingAttributes(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    int32_t* /* android_pixel_format_t */ format,
    int32_t* /* android_dataspace_t */ dataspace,
    uint8_t* /* mask of android_component_t */ supported_components)
{
    CHECK_DISP_CONNECT(display);

    sp<HWCDisplay> hwc_display = getHWCDisplay(display);
    return hwc_display->getContentSamplingAttribute(format, dataspace, supported_components);
}

int32_t HWCMediator::displaySetDisplayedContentSamplingEnabled(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    int32_t /*hwc2_displayed_content_sampling_t*/ enabled,
    uint8_t /* mask of android_component_t */ component_mask,
    uint64_t max_frames)
{
    CHECK_DISP_CONNECT(display);

    sp<HWCDisplay> hwc_display = getHWCDisplay(display);
    return hwc_display->setContentSamplingEnabled(enabled, component_mask, max_frames);
}

int32_t HWCMediator::displayGetDisplayedContentSample(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    uint64_t max_frames,
    uint64_t timestamp,
    uint64_t* frame_count,
    int32_t samples_size[4],
    uint64_t* samples[4])
{
    CHECK_DISP_CONNECT(display);

    sp<HWCDisplay> hwc_display = getHWCDisplay(display);
    return hwc_display->getContentSample(max_frames, timestamp, frame_count, samples_size, samples);
}

int32_t /*hwc2_error_t*/ HWCMediator::displayGetRenderIntents(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    int32_t mode,
    uint32_t* out_num_intents,
    int32_t* /*android_render_intent_v1_1_t*/ out_intents)
{
    CHECK_DISP_CONNECT(display);

    sp<HWCDisplay> hwc_display = getHWCDisplay(display);
    return hwc_display->getRenderIntents(mode, out_num_intents, out_intents);
}

int32_t /*hwc2_error_t*/ HWCMediator::displaySetColorModeWithRenderIntent(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    int32_t /*android_color_mode_t*/ mode,
    int32_t /*android_render_intent_v1_1_t */ intent)
{
    CHECK_DISP_CONNECT(display);

    HWC_LOGI("(%" PRIu64 ") %s mode:%d intent:%d", display, __func__, mode, intent);
    sp<HWCDisplay> hwc_display = getHWCDisplay(display);
    return hwc_display->setColorModeWithRenderIntent(mode, intent);
}

int32_t /*hwc2_error_t*/ HWCMediator::displayGetDisplayVsyncPeriod(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    hwc2_vsync_period_t* out_vsync_period)
{
    CHECK_DISP_CONNECT(display);

    sp<HWCDisplay> hwc_display = getHWCDisplay(display);
    return hwc_display->getDisplayVsyncPeriod(out_vsync_period);
}

int32_t /*hwc2_error_t*/ HWCMediator::displaySetActiveConfigWithConstraints(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    hwc2_config_t config,
    hwc_vsync_period_change_constraints_t* vsync_period_change_constraints,
    hwc_vsync_period_change_timeline_t* out_timeline)
{
    CHECK_DISP_CONNECT(display);

    sp<HWCDisplay> hwc_display = getHWCDisplay(display);
    return hwc_display->setActiveConfigWithConstraints(config, vsync_period_change_constraints,
            out_timeline);
}

int32_t /*hwc2_error_t*/ HWCMediator::displayGetSupportedContentTypes(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    uint32_t* out_num_supported_content_types,
    uint32_t* /*out_supported_content_types*/)
{
    CHECK_DISP_CONNECT(display);

    // We currently do not implement this API yet, just for pass VTS
    *out_num_supported_content_types = 0;
    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator::displayGetDisplayConnectionType(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    uint32_t* /*hwc2_display_connection_type_t*/ out_type)
{
    CHECK_DISP_CONNECT(display);

    sp<HWCDisplay> hwc_display = getHWCDisplay(display);

    if (hwc_display->isInternal())
    {
        *out_type = HWC2_DISPLAY_CONNECTION_TYPE_INTERNAL;
        return HWC2_ERROR_NONE;
    }

    int32_t disp_type;
    hwc_display->getType(&disp_type);

    if (disp_type != HWC2_DISPLAY_TYPE_VIRTUAL)
    {
        *out_type = HWC2_DISPLAY_CONNECTION_TYPE_EXTERNAL;
        return HWC2_ERROR_NONE;
    }

    return HWC2_ERROR_BAD_DISPLAY;
}

int32_t /*hwc2_error_t*/ HWCMediator::displaySetAutoLowLatencyMode(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    bool /*on*/)
{
    CHECK_DISP_CONNECT(display);

    // We currently do not implement this API yet, just for pass VTS
    return HWC2_ERROR_UNSUPPORTED;
}

int32_t /*hwc2_error_t*/ HWCMediator::displaySetContentType(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    int32_t /* hwc2_content_type_t */ content_type)
{
    CHECK_DISP_CONNECT(display);

    // We currently do not implement this API yet, just for pass VTS
    switch (content_type)
    {
        case HWC2_CONTENT_TYPE_NONE:
            return HWC2_ERROR_NONE;

        case HWC2_CONTENT_TYPE_GRAPHICS:
        case HWC2_CONTENT_TYPE_PHOTO:
        case HWC2_CONTENT_TYPE_CINEMA:
        case HWC2_CONTENT_TYPE_GAME:
            return HWC2_ERROR_UNSUPPORTED;

        default:
            return HWC2_ERROR_BAD_PARAMETER;
    }
}

int32_t /*hwc2_error_t*/ HWCMediator::displayGetDisplayIdentificationData(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    uint8_t* out_port,
    uint32_t* out_data_size,
    uint8_t* out_data)
{
    CHECK_DISP_CONNECT(display);
    if (out_data_size == nullptr || out_port == nullptr)
    {
        return HWC2_ERROR_BAD_DISPLAY;
    }

    if (out_data == nullptr)
    {
        if (DisplayManager::getInstance().getDeviceIdSize(display, out_data_size) < 0)
        {
            return HWC2_ERROR_BAD_DISPLAY;
        }
        HWC_LOGV("getDeviceIdSize %d", *out_data_size);
        return HWC2_ERROR_NONE;
    }

    if (DisplayManager::getInstance().getDeviceId(display, out_port, out_data, out_data_size) < 0)
    {
        return HWC2_ERROR_BAD_DISPLAY;
    }

    return HWC2_ERROR_NONE;
}

int32_t HWCMediator::displaySetBootDisplayConfig(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    hwc2_config_t /*config*/)
{
    CHECK_DISP_CONNECT(display);
    return HWC2_ERROR_UNSUPPORTED;
}

int32_t HWCMediator::displayClearBootDisplayConfig(
    hwc2_device_t* /*device*/,
    hwc2_display_t display)
{
    CHECK_DISP_CONNECT(display);
    return HWC2_ERROR_UNSUPPORTED;
}

int32_t HWCMediator::displayGetPreferredBootDisplayConfig(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    hwc2_config_t* /*config*/)
{
    CHECK_DISP_CONNECT(display);
    return HWC2_ERROR_UNSUPPORTED;
}

int32_t HWCMediator::displayGetDisplayDecorationSupport(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    std::optional<common::DisplayDecorationSupport>* /*support*/)
{
    CHECK_DISP_CONNECT(display);
    // we do not support R8 for decoration now, so return null to client
    return HWC2_ERROR_NONE;
}

int32_t HWCMediator::displaySetIdleTimerEnabled(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    int32_t /*timeout*/)
{
    CHECK_DISP_CONNECT(display);
    return HWC2_ERROR_UNSUPPORTED;
}

int32_t HWCMediator::displayGetDisplayPhysicalOrientation(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    common::Transform* /*orientation*/)
{
    CHECK_DISP_CONNECT(display);
    return HWC2_ERROR_UNSUPPORTED;
}

int32_t HWCMediator::displaySetExpectedPresentTime(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    int64_t timestamp_nanos)
{
    CHECK_DISP_CONNECT(display);
    sp<HWCDisplay> hwc_display = getHWCDisplay(display);
    hwc_display->setSfTargetTs(timestamp_nanos);
    return HWC2_ERROR_NONE;
}

/* Layer functions */
int32_t /*hwc2_error_t*/ HWCMediator::layerSetCursorPosition(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    hwc2_layer_t layer,
    int32_t x,
    int32_t y)
{
    CHECK_DISP_CONNECT(display);

    sp<HWCDisplay> hwc_display = getHWCDisplay(display);
    sp<HWCLayer> ct = hwc_display->getClientTarget();

    // when set with client target, it is for present_after_ts
    // new API add for this on Android T, from google comment
    if (ct->getId() == layer)
    {
        nsecs_t ts;
        ts = static_cast<nsecs_t>(static_cast<uint64_t>(x) << 32 | static_cast<uint32_t>(y));

        hwc_display->setSfTargetTs(ts);
    }

    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator::layerSetBuffer(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    hwc2_layer_t layer,
    buffer_handle_t buffer,
    int32_t acquire_fence)
{
    CHECK_DISP_CONNECT(display);

    sp<HWCDisplay> hwc_display = HWCMediator::getInstance().getHWCDisplay(display);
    sp<HWCLayer> hwc_layer = hwc_display->getLayer(layer);
    CHECK_DISP_LAYER(display, layer, hwc_layer);

    HWC_LOGV("(%" PRIu64 ") layerSetBuffer() layer id:%" PRIu64 " hnd:%p acquire_fence:%d", display, layer, buffer, acquire_fence);

    if (buffer)
    {
        hwc_display->editSetBufFromSfLog().printf("%" PRIu64 ",%x", layer, static_cast<uint32_t>(((const intptr_t)(buffer) & 0xffff0) >> 4));
    }
    else
    {
        hwc_display->editSetBufFromSfLog().printf("%" PRIu64 ",null", layer);
    }

    hwc_layer->setHandle(buffer);
    hwc_layer->setAcquireFenceFd(acquire_fence);

    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator::layerSetSurfaceDamage(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    hwc2_layer_t layer_id,
    hwc_region_t damage)
{
    CHECK_DISP_CONNECT(display);

    sp<HWCLayer> layer = getHWCDisplay(display)->getLayer(layer_id);
    CHECK_DISP_LAYER(display, layer_id, layer);

    layer->setDamage(damage);

    return HWC2_ERROR_NONE;
}

/* Layer state functions */
int32_t /*hwc2_error_t*/ HWCMediator::layerStateSetBlendMode(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    hwc2_layer_t layer_id,
    int32_t /*hwc2_blend_mode_t*/ mode)
{
    CHECK_DISP_CONNECT(display);

    sp<HWCLayer> layer = getHWCDisplay(display)->getLayer(layer_id);
    CHECK_DISP_LAYER(display, layer_id, layer);

    switch (mode)
    {
        case HWC2_BLEND_MODE_NONE:
        case HWC2_BLEND_MODE_PREMULTIPLIED:
        case HWC2_BLEND_MODE_COVERAGE:
            layer->setBlend(mode);
            break;
        default:
            HWC_LOGE("%s: unknown mode(%d)", __func__, mode);
            return HWC2_ERROR_BAD_PARAMETER;
    }
    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator::layerStateSetColor(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    hwc2_layer_t layer_id,
    hwc_color_t color)
{
    CHECK_DISP_CONNECT(display);

    sp<HWCLayer> layer = getHWCDisplay(display)->getLayer(layer_id);
    CHECK_DISP_LAYER(display, layer_id, layer);

    layer->setLayerColor(color);

    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator::layerStateSetCompositionType(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    hwc2_layer_t layer_id,
    int32_t /*hwc2_composition_t*/ type)
{
    CHECK_DISP_CONNECT(display);

    sp<HWCDisplay> hwc_display = HWCMediator::getInstance().getHWCDisplay(display);

    HWC_LOGV("(%" PRIu64 ")%s: layer id:%" PRIu64 " type:%s", display, __func__, layer_id,
            getCompString(type));
    hwc_display->editSetCompFromSfLog().printf("%" PRIu64 ",%s)", layer_id, getCompString(type));

    auto&& layer = hwc_display->getLayer(layer_id);
    CHECK_DISP_LAYER(display, layer_id, layer);

    int32_t previous_compoition_type = layer->getReturnedCompositionType();
    switch (type)
    {
        case HWC2_COMPOSITION_CLIENT:
        case HWC2_COMPOSITION_DEVICE:
        case HWC2_COMPOSITION_CURSOR:
            if (type == HWC2_COMPOSITION_DEVICE &&
                Platform::getInstance().m_config.is_smart_composition_support &&
                layer->getHwlayerType() == HWC_LAYER_TYPE_INVALID && // prev type is HRT GPU
                layer->getSFCompositionType() == HWC2_COMPOSITION_DEVICE)//prev sf type is DEVICE
            {
                // the layer maybe must be prev_exist
                layer->setStateChanged(HWC_LAYER_STATE_CHANGE_HWC_CLIENT_TYPE);
            }
            else if (previous_compoition_type != type)
            {
                layer->setStateChanged(HWC_LAYER_STATE_CHANGE_SF_COMP_TYPE);
            }

            break;

        case HWC2_COMPOSITION_SIDEBAND:
            abort();

        case HWC2_COMPOSITION_SOLID_COLOR:
            if (previous_compoition_type != type)
                layer->setStateChanged(HWC_LAYER_STATE_CHANGE_SF_COMP_TYPE);
            layer->toBeDim();
            break;

        default:
            HWC_LOGW("(%" PRIu64 ")%s: unknown composition type(layer id:%" PRIu64 ", type:%d)",
                    display, __func__, layer_id, type);
            layer->setStateChanged(HWC_LAYER_STATE_CHANGE_SF_COMP_TYPE);
            break;
    }
    layer->setSFCompositionType(type);
    layer->setSFCompTypeCallFromSF(true);

    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator::layerStateSetDataSpace(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    hwc2_layer_t layer,
    int32_t /*android_dataspace_t*/ dataspace)
{
    CHECK_DISP_CONNECT(display);

    sp<HWCLayer> hwc_layer = getHWCDisplay(display)->getLayer(layer);
    CHECK_DISP_LAYER(display, layer, hwc_layer);

    HWC_LOGV("(%" PRIu64 ") layerSetDataSpace() layer id:%" PRIu64 " dataspace:%d", display, layer, dataspace);

    int32_t layer_dataspace = dataspace;
    if ((dataspace & HAL_DATASPACE_STANDARD_MASK) == HAL_DATASPACE_STANDARD_UNSPECIFIED)
    {
        switch (dataspace & 0xffff)
        {
            case HAL_DATASPACE_JFIF:
                layer_dataspace = HAL_DATASPACE_V0_JFIF;
                break;
            case HAL_DATASPACE_BT601_625:
                layer_dataspace = HAL_DATASPACE_V0_BT601_625;
                break;
            case HAL_DATASPACE_BT601_525:
                layer_dataspace = HAL_DATASPACE_V0_BT601_525;
                break;
            case HAL_DATASPACE_SRGB_LINEAR:
                layer_dataspace = HAL_DATASPACE_V0_SRGB_LINEAR;
                break;
            case HAL_DATASPACE_SRGB:
                layer_dataspace = HAL_DATASPACE_V0_SRGB;
                break;
            case HAL_DATASPACE_BT709:
                layer_dataspace = HAL_DATASPACE_V0_BT709;
                break;
        }
        if (layer_dataspace != dataspace)
        {
            HWC_LOGV("(%" PRIu64 ") layerSetDataSpace() layer id:%" PRIu64 " replace dataspace %d to %d", display, layer, dataspace, layer_dataspace);
        }
    }

    hwc_layer->setDataspace(layer_dataspace);

    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator::layerStateSetDisplayFrame(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    hwc2_layer_t layer_id,
    hwc_rect_t frame)
{
    CHECK_DISP_CONNECT(display);

    sp<HWCDisplay> hwc_display = getHWCDisplay(display);
    sp<HWCLayer> layer = hwc_display->getLayer(layer_id);
    CHECK_DISP_LAYER(display, layer_id, layer);

    nsecs_t now = systemTime();
    const DisplayData* disp_info = DisplayManager::getInstance().getDisplayData(display,
                                                                                hwc_display->getActiveConfig(now));

    if (frame.right > disp_info->width)
    {
        HWC_LOGW("%s: (%" PRIu64 ") layer id:%" PRIu64 " displayframe width(%d) > display device width(%d)",
            __func__, display, layer_id, WIDTH(frame), disp_info->width);
    }

    if (frame.bottom > disp_info->height)
    {
        HWC_LOGW("%s: (%" PRIu64 ") layer id:%" PRIu64 " displayframe height(%d) > display device height(%d)",
            __func__, display, layer_id, HEIGHT(frame), disp_info->height);
    }

    HWC_LOGV("%s: (%" PRIu64 ") layer id:%" PRIu64 " frame[%d,%d,%d,%d] ",
        __func__, display, layer_id, frame.left, frame.top, frame.right, frame.bottom);

    layer->setDisplayFrame(frame);

    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator::layerStateSetPlaneAlpha(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    hwc2_layer_t layer_id,
    float alpha)
{
    CHECK_DISP_CONNECT(display);

    sp<HWCLayer> layer = getHWCDisplay(display)->getLayer(layer_id);
    CHECK_DISP_LAYER(display, layer_id, layer);

    layer->setPlaneAlpha(alpha);

    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator::layerStateSetSidebandStream(
    hwc2_device_t* /*device*/,
    hwc2_display_t /*display*/,
    hwc2_layer_t /*layer*/,
    const native_handle_t* /*stream*/)
{
    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator::layerStateSetSourceCrop(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    hwc2_layer_t layer_id,
    hwc_frect_t crop)
{
    CHECK_DISP_CONNECT(display);

    sp<HWCLayer> layer = getHWCDisplay(display)->getLayer(layer_id);
    if (layer == nullptr)
    {
        HWC_LOGE("%s: the display(%" PRIu64 ") does not contain layer(%" PRIu64 ")", __func__, display, layer_id);
        return HWC2_ERROR_BAD_LAYER;
    }

    layer->setSourceCrop(crop);
    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator::layerStateSetTransform(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    hwc2_layer_t layer_id,
    int32_t /*hwc_transform_t*/ transform)
{
    CHECK_DISP_CONNECT(display);

    sp<HWCLayer> layer = getHWCDisplay(display)->getLayer(layer_id);
    CHECK_DISP_LAYER(display, layer_id, layer);

    switch (transform)
    {
        case 0:
        case HWC_TRANSFORM_FLIP_H:
        case HWC_TRANSFORM_FLIP_V:
        case HWC_TRANSFORM_ROT_90:
        case HWC_TRANSFORM_ROT_180:
        case HWC_TRANSFORM_ROT_270:
        case HWC_TRANSFORM_FLIP_H_ROT_90:
        case HWC_TRANSFORM_FLIP_V_ROT_90:
        case Transform::ROT_INVALID:
            layer->setTransform(static_cast<unsigned int>(transform));
            break;

        default:
            HWC_LOGE("%s: unknown transform(%d)", __func__, transform);
            return HWC2_ERROR_BAD_PARAMETER;
    }
    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator::layerStateSetVisibleRegion(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    hwc2_layer_t layer_id,
    hwc_region_t visible)
{
    CHECK_DISP_CONNECT(display);

    sp<HWCLayer> layer = getHWCDisplay(display)->getLayer(layer_id);
    CHECK_DISP_LAYER(display, layer_id, layer);

    HWC_LOGV("(%" PRIu64 ") layerSetVisibleRegion() layer id:%" PRIu64, display, layer_id);
    layer->setVisibleRegion(visible);
    layer->setVisible(true);

    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator::layerStateSetZOrder(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    hwc2_layer_t layer_id,
    uint32_t z)
{
    CHECK_DISP_CONNECT(display);

    sp<HWCLayer> layer = getHWCDisplay(display)->getLayer(layer_id);
    CHECK_DISP_LAYER(display, layer_id, layer);

    layer->setZOrder(z);
    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator::layerStateSetPerFrameMetadata(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    hwc2_layer_t layer_id,
    uint32_t numElements,
    const int32_t* /*hw2_per_frame_metadata_key_t*/ keys,
    const float* metadata)
{
    CHECK_DISP_CONNECT(display);

    sp<HWCLayer> layer = getHWCDisplay(display)->getLayer(layer_id);
    CHECK_DISP_LAYER(display, layer_id, layer);

    if (keys == nullptr || metadata == nullptr)
    {
        HWC_LOGE("%s: keys:%p metadata:%p", __func__, keys, metadata);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    std::map<int32_t, float> per_frame_metadata;
    for (uint32_t i = 0; i < numElements; ++i)
    {
        per_frame_metadata[keys[i]] = metadata[i];
    }
    layer->setPerFrameMetadata(per_frame_metadata);
    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator::layerStateSetPerFrameMetadataBlobs(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    hwc2_layer_t layer_id,
    uint32_t numElements,
    const int32_t* keys,
    const uint32_t* sizes,
    const uint8_t* metadata)
{
    CHECK_DISP_CONNECT(display);

    sp<HWCLayer> layer = getHWCDisplay(display)->getLayer(layer_id);
    CHECK_DISP_LAYER(display, layer_id, layer);

    if (keys == nullptr || sizes == nullptr || metadata == nullptr)
    {
        HWC_LOGE("%s: keys:%p sizes:%p metadata:%p", __func__, keys, sizes, metadata);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    std::map<int32_t, std::vector<uint8_t> > per_frame_metadata_blobs;
    uint32_t shift = 0;
    for (uint32_t i = 0; i < numElements; ++i)
    {
        switch (static_cast<PerFrameMetadataKey>(keys[i]))
        {
            case PerFrameMetadataKey::HDR10_PLUS_SEI:
                per_frame_metadata_blobs[keys[i]] =
                    std::vector<uint8_t>(metadata + shift, metadata + (shift + sizes[i]));
                shift += sizes[i];
                break;
            default:
                HWC_LOGE("%s: [%u] keys:%i is not valid", __func__, i, keys[i]);
                return HWC2_ERROR_BAD_PARAMETER;
        }
    }
    layer->setPerFrameMetadataBlobs(per_frame_metadata_blobs);
    return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ HWCMediator::layerStateSetLayerColorTransform(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    hwc2_layer_t layer_id,
    const float* matrix)
{
    CHECK_DISP_CONNECT(display);

    sp<HWCLayer> layer = getHWCDisplay(display)->getLayer(layer_id);
    CHECK_DISP_LAYER(display, layer_id, layer);
    layer->setColorTransform(matrix);
    return HWC2_ERROR_NONE;
}

int32_t HWCMediator::layerSetLayerBrightness(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    hwc2_layer_t layer_id,
    float brightness)
{
    CHECK_DISP_CONNECT(display);

    sp<HWCLayer> layer = getHWCDisplay(display)->getLayer(layer_id);
    CHECK_DISP_LAYER(display, layer_id, layer);
    return layer->setBrightness(brightness);
}

int32_t HWCMediator::layerSetLayerBlockingRegion(
    hwc2_device_t* /*device*/,
    hwc2_display_t display,
    hwc2_layer_t layer_id,
    hwc_region_t region)
{
    CHECK_DISP_CONNECT(display);

    sp<HWCLayer> layer = getHWCDisplay(display)->getLayer(layer_id);
    CHECK_DISP_LAYER(display, layer_id, layer);
    layer->setBlockingRegion(region);
    return HWC2_ERROR_NONE;
}

void HWCMediator::countdowmSkipValiRelatedNumber()
{
    for (auto& hwc_display : m_displays)
    {
        if (!hwc_display->isConnected())
            continue;

        HWCDispatcher::getInstance().decSessionModeChanged();
        hwc_display->countdowmSkipValiRelatedNumber();
    }
}

bool HWCMediator::checkSkipValidate(const bool& is_validate_call)
{
    // If just change the power mode and it's session mode change, we should not skip validate
    if (HWCDispatcher::getInstance().getSessionModeChanged() > 0)
    {
        HWC_LOGD("no skip vali(L%d) validate(%d), "
                 "getSessionModeChanged: %d",
                 __LINE__, is_validate_call,
                 HWCDispatcher::getInstance().getSessionModeChanged());
        return false;
    }

    bool has_valid_display = false;
    for (auto& hwc_display : m_displays)
    {
        if (!hwc_display->isValid())
        {
            continue;
        }

        has_valid_display = true;

        if (!hwc_display->checkSkipValidate(is_validate_call))
        {
            return false;
        }
    }

    if (!has_valid_display)
    {
        HWC_LOGD("no skip vali(L%d) validate(%d), "
                 "do not have valid display",
                 __LINE__, is_validate_call);
        return false;
    }

    HWC_LOGD("do skip vali(L%d) validate(%d)",
             __LINE__, is_validate_call);
    return true;
}

void HWCMediator::buildVisibleAndInvisibleLayerForAllDisplay()
{
    // build visible layers
    for (auto& hwc_display : m_displays)
    {
        if (!hwc_display->isValid())
            continue;

        hwc_display->buildVisibleAndInvisibleLayer();
        hwc_display->addUnpresentCount();
    }
}

void HWCMediator::updateActiveConfigWithConstraintsForAllDisplay()
{
    for (auto& hwc_display : m_displays)
    {
        if (!hwc_display->isValid())
            continue;

        hwc_display->updateActiveConfigWithConstraints();
    }
}

void HWCMediator::prepareSfLog()
{
    for (auto& hwc_display : m_displays)
    {
        hwc_display->prepareSfLog();
    }
}

void HWCMediator::prepareForValidation()
{
    // validate all displays
    for (auto& hwc_display : m_displays)
    {
        if (!hwc_display->isValid())
            continue;

        hwc_display->prepareForValidation();
    }

    HWCDispatcher::getInstance().prepareMirror(m_displays);
}

void HWCMediator::setJobVideoTimeStamp()
{
    for (auto& hwc_display : m_displays)
    {
        if (!hwc_display->isValid())
            continue;

        hwc_display->setJobVideoTimeStamp();
    }

    for (auto& hwc_display : m_displays)
    {
        if (!hwc_display->isValid())
            continue;

        if (HWC_DISPLAY_VIRTUAL != hwc_display->getId())
            continue;

        DispatcherJob* job = HWCDispatcher::getInstance().getExistJob(hwc_display->getId());
        if (NULL != job && (HWC_MIRROR_SOURCE_INVALID != hwc_display->getMirrorSrc()))
        {
            DispatcherJob* mir_src_job = HWCDispatcher::getInstance().getExistJob(static_cast<uint64_t>(job->disp_mir_id));
            if (NULL != mir_src_job)
                job->timestamp = mir_src_job->timestamp;
        }
    }
}

void HWCMediator::setValiPresentStateOfAllDisplay(const HWC_VALI_PRESENT_STATE& val, const int32_t& line)
{
    for (size_t i = 0; i < m_displays.size(); ++i)
    {
        sp<HWCDisplay> hwc_display = getHWCDisplay(i);
        if (!hwc_display->isValid())
        {
            continue;
        }
        hwc_display->setValiPresentState(val, line);
    }
}

void HWCMediator::validate()
{
    // check if mirror mode exists
    {
        for (auto& hwc_display : m_displays)
        {
            if (!hwc_display->isValid())
                continue;

            hwc_display->validate();
        }
    }
}

void HWCMediator::calculateMdpDstRoi()
{
    for (auto& hwc_display : m_displays)
    {
        if (!hwc_display->isValid())
            continue;

        hwc_display->calculateMdpDstRoi();
    }
}

void HWCMediator::notifyHwbinderTid()
{
    if (Platform::getInstance().m_config.plat_switch & HWC_PLAT_SWITCH_NOTIFY_HWBINDER_TID)
    {
        ATRACE_NAME("notifyHwbinderTid");
        {
            std::lock_guard<std::mutex> lock(m_ext_lock);
            if (!m_need_tid_list.empty())
            {
                for (size_t i = 0; i < m_need_tid_list.size(); i++)
                {
                    auto item = m_ext_callback_list.find(m_need_tid_list[i]);
                    if (item != m_ext_callback_list.end())
                    {
                        std::shared_ptr<ComposerExt::ConfigCallback> callback = item->second.callback.lock();
                        if (callback)
                        {
                            for (size_t j = 0; j < m_hwbinder_tid.size(); j++)
                            {
                                callback->NotifyHwcHwBinderTidCallback(m_hwbinder_tid[j]);
                            }
                        }
                    }
                }
                m_need_tid_list.clear();
            }
        }
        int tid = gettid();
        if (tid != m_prev_hwbinder_tid)
        {
            size_t size = m_hwbinder_tid.size();
            bool new_tid = true;
            for (size_t i = 0; i < size; i++)
            {
                if (m_hwbinder_tid[i] == tid)
                {
                    new_tid = false;
                    break;
                }
            }
            m_prev_hwbinder_tid = tid;
            if (new_tid)
            {
                m_hwbinder_tid.push_back(tid);
                std::lock_guard<std::mutex> lock(m_ext_lock);
                if (m_ext_callback_list.size() != 0)
                {
                    for (auto iter = m_ext_callback_list.begin();
                            iter != m_ext_callback_list.end(); ++iter)
                    {
                        std::shared_ptr<ComposerExt::ConfigCallback> callback = iter->second.callback.lock();
                        if (callback)
                        {
                            callback->NotifyHwcHwBinderTidCallback(tid);
                        }
                    }
                }
            }
        }
    }
}

int HWCMediator::registerClientContext(std::string client_name,
                                       std::shared_ptr<ComposerExt::ConfigCallback> callback,
                                       ComposerExt::ConfigInterface** intf)
{
    if (!intf)
    {
        HWC_LOGE("Invalid intf");
        return -EINVAL;
    }

    std::weak_ptr<ComposerExt::ConfigCallback> wp_callback = callback;
    ComposerExtIntf* impl = new ComposerExtIntf(wp_callback, this);
    *intf = impl;
    {
        std::lock_guard<std::mutex> lock(m_ext_lock);
        m_ext_callback_list[reinterpret_cast<uintptr_t>(impl)] = {.client_name = client_name,
                                                                  .callback = callback};
        m_need_tid_list.push_back(reinterpret_cast<uintptr_t>(impl));
    }

    return 0;
}

void HWCMediator::unRegisterClientContext(ComposerExt::ConfigInterface* intf)
{
    {
        std::lock_guard<std::mutex> lock(m_ext_lock);
        auto item = m_ext_callback_list.find(reinterpret_cast<uintptr_t>(intf));
        if (item != m_ext_callback_list.end())
        {
            m_ext_callback_list.erase(item);
        }
    }
    delete static_cast<ComposerExtIntf*>(intf);
}

HWCMediator::ComposerExtIntf::ComposerExtIntf(std::weak_ptr<ComposerExt::ConfigCallback> callback,
                                              HWCMediator* hwc_mediator)
{
    m_callback = callback;
    m_hwc_mediator = hwc_mediator;
}

int HWCMediator::ComposerExtIntf::isMSyncSupported(uint64_t disp_id, bool* supported)
{
    if (!supported)
    {
        HWC_LOGE("supported == nullptr");
        return -EINVAL;
    }

    if (disp_id >= DisplayManager::MAX_DISPLAYS)
    {
        HWC_LOGE("%s(), invalid disp_id %" PRIu64, __FUNCTION__, disp_id);
        return -EINVAL;
    }

    *supported = m_hwc_mediator->getOvlDevice(disp_id)->isHwcFeatureSupported(HWC_FEATURE_MSYNC2_0);

    return 0;
}

int HWCMediator::ComposerExtIntf::msyncSetEnable(uint64_t disp_id, ComposerExt::MSyncEnableType enable)
{
    if (disp_id >= DisplayManager::MAX_DISPLAYS)
    {
        HWC_LOGE("%s(), invalid disp_id %" PRIu64, __FUNCTION__, disp_id);
        return -EINVAL;
    }

    m_hwc_mediator->getHWCDisplay(disp_id)->setMSync2Enable(enable == ComposerExt::MSyncEnableType::kOn ?
                                                            true : false);

    return 0;
}

int HWCMediator::ComposerExtIntf::msyncSetParamTable(ComposerExt::MSyncParamTableStruct& param_table)
{
    uint64_t disp_id = param_table.disp_id;
    if (disp_id >= DisplayManager::MAX_DISPLAYS)
    {
        HWC_LOGE("%s(), invalid disp_id %" PRIu64, __FUNCTION__, disp_id);
        return -EINVAL;
    }

    if (param_table.level_num != param_table.level_tables.size())
    {
        HWC_LOGW("level_num not match");
        return -EINVAL;
    }

    std::shared_ptr<MSync2Data::ParamTable> hwc_param_table;
    hwc_param_table = std::make_shared<MSync2Data::ParamTable>();

    hwc_param_table->max_fps = param_table.max_fps;
    hwc_param_table->min_fps = param_table.min_fps;
    hwc_param_table->level_num = param_table.level_num;

    hwc_param_table->level_tables.resize(param_table.level_num);

    for (size_t i = 0; i < param_table.level_num; i++)
    {
        hwc_param_table->level_tables[i].level_id = param_table.level_tables[i].level_id;
        hwc_param_table->level_tables[i].level_fps = param_table.level_tables[i].level_fps;
        hwc_param_table->level_tables[i].max_fps = param_table.level_tables[i].max_fps;
        hwc_param_table->level_tables[i].min_fps = param_table.level_tables[i].min_fps;
    }

    m_hwc_mediator->getHWCDisplay(disp_id)->setMSync2ParamTable(hwc_param_table);
    return 0;
}

int HWCMediator::ComposerExtIntf::msyncGetDefaultParamTable(uint64_t disp_id,
                                                            ComposerExt::MSyncParamTableStruct& param_table)
{
    if (disp_id >= DisplayManager::MAX_DISPLAYS)
    {
        HWC_LOGE("%s(), invalid disp_id %" PRIu64, __FUNCTION__, disp_id);
        return -EINVAL;
    }

    const MSync2Data::ParamTable* tb = m_hwc_mediator->getOvlDevice(disp_id)->getMSyncDefaultParamTable();
    if (!tb)
    {
        HWC_LOGW("getMSyncDefaultParamTable return null");
        return -ENODATA;
    }

    if (tb->level_num == 0)
    {
        HWC_LOGW("tb->level_num %u", tb->level_num);
        return -ENODATA;
    }

    param_table.disp_id = disp_id;
    param_table.max_fps = tb->max_fps;
    param_table.min_fps = tb->min_fps;
    param_table.level_num = tb->level_num;

    param_table.level_tables.resize(param_table.level_num);

    for (size_t i = 0; i < param_table.level_tables.size(); i++)
    {
        param_table.level_tables[i].level_id = tb->level_tables[i].level_id;
        param_table.level_tables[i].level_fps = tb->level_tables[i].level_fps;
        param_table.level_tables[i].max_fps = tb->level_tables[i].max_fps;
        param_table.level_tables[i].min_fps = tb->level_tables[i].min_fps;
    }

    return 0;
}

int HWCMediator::ComposerExtIntf::setNextDisplayUsage(ComposerExt::DisplayUsage usage)
{
    m_hwc_mediator->setNextDisplayUsage(usage);

    return 0;
}

int HWCMediator::ComposerExtIntf::getCtId(uint64_t disp_id, uint64_t* ct_id)
{
    if (!ct_id)
    {
        HWC_LOGE("ct_id == nullptr");
        return -EINVAL;
    }

    if (disp_id >= DisplayManager::MAX_DISPLAYS)
    {
        HWC_LOGE("invalid disp_id %" PRIu64, disp_id);
        return -EINVAL;
    }

    *ct_id = m_hwc_mediator->getHWCDisplay(disp_id)->getClientTarget()->getId();

    return 0;
}

int HWCMediator::ComposerExtIntf::isFeatureSupported(ComposerExt::HwcFeature feature, int* supported)
{
    if (!supported)
    {
        HWC_LOGE("supported == nullptr");
        return -EINVAL;
    }

    uint32_t major = 0;
    m_hwc_mediator->getHwcVersion(&major, nullptr, nullptr);
    switch (feature)
    {
        case ComposerExt::HwcFeature::kSfTargetTs:
            // composer3 have standard hwcomposer API to set this information, so we can notify
            // client that it does not need to set target time via ComposerExt.
            if (major >= 3)
            {
                *supported = 0;
            }
            else
            {
                // vendor s need this
                *supported = 1;
            }
            break;
        default:
            return -EINVAL;
            break;
    }
    return 0;
}

int HWCMediator::updateCompressionType(buffer_handle_t handle)
{
    static bool is_called = false;
    int ret = -1;
    buffer_handle_t imported_handle;
    aidl::android::hardware::graphics::common::ExtendableType cmpType;

    if (is_called == true)
    {
        return 0;
    }

    ret = hwc::GraphicBufferMapper::getInstance().importBuffer(handle, &imported_handle);
    if (ret)
    {
        HWC_LOGW("Can not import from handle %d", ret);
        return ret;
    }

    ret = hwc::GraphicBufferMapper::getInstance().getCompression(imported_handle, &cmpType);

    if (ret)
    {
        HWC_LOGW("Can not get compression from handle %d", ret);

        ret = hwc::GraphicBufferMapper::getInstance().freeBuffer(imported_handle);
        if (ret)
        {
            HWC_LOGW("%s(), freeBuffer fail, ret %d", __FUNCTION__, ret);
        }
        return ret;
    }

    getOvlDevice(HWC_DISPLAY_PRIMARY)->getCompressionDefine(cmpType.name.c_str(),
            &m_compression_type, &m_compression_modifier);
    if (m_compression_modifier == 0)
    {
        HWC_LOGE("Fourcc not define for %s", cmpType.name.c_str());
    }

    ret = hwc::GraphicBufferMapper::getInstance().freeBuffer(imported_handle);

    HWC_LOGV("ret %d Comps type cmpType (%s)", ret, cmpType.name.c_str());
    if (ret)
    {
        HWC_LOGE("%s(), freeBuffer fail, ret %d, %s", __FUNCTION__, ret, cmpType.name.c_str());
        return ret;
    }

    is_called = true;
    return 0;
}

int HWCMediator::setScenarioHint(ComposerExt::ScenarioHint flag, ComposerExt::ScenarioHint mask)
{
    for (auto& display: m_displays)
    {
        display->setScenarioHint(flag, mask);
    }
    return 0;
}

int HWCMediator::ComposerExtIntf::enableHWCLogWithProp()
{
    char value[PROPERTY_VALUE_MAX] = {0};
    property_get("persist.vendor.debug.hwc.log", value, "0");
    if (!(strlen(value) == 1 && value[0] == '0'))
        Debugger::getInstance().setLogThreshold(value[0]);

    property_get("vendor.debug.hwc.skip_log", value, "-1");
    if (-1 != atoi(value))
        Debugger::m_skip_log = atoi(value);

    property_get("vendor.debug.hwc.color_transform", value, "-1");
    if (-1 != atoi(value))
    {
        Platform::getInstance().m_config.support_color_transform = atoi(value);
    }

    property_get("vendor.debug.hwc.mobile_on", value, "-1");
    if (1 == atoi(value))
    {
        HWCMediator::getInstance().getOvlDevice(HWC_DISPLAY_PRIMARY)->enableDisplayDriverLog(HWC_DEBUG_LOG_MOBILE_ON);
    }

    property_get("vendor.debug.hwc.detail_on", value, "-1");
    if (1 == atoi(value))
    {
        HWCMediator::getInstance().getOvlDevice(HWC_DISPLAY_PRIMARY)->enableDisplayDriverLog(HWC_DEBUG_LOG_DETAIL_ON);
    }

    property_get("vendor.debug.hwc.fence_on", value, "-1");
    if (1 == atoi(value))
    {
        HWCMediator::getInstance().getOvlDevice(HWC_DISPLAY_PRIMARY)->enableDisplayDriverLog(HWC_DEBUG_LOG_FENCE_ON);
    }

    property_get("vendor.debug.hwc.irq_on", value, "-1");
    if (1 == atoi(value))
    {
        HWCMediator::getInstance().getOvlDevice(HWC_DISPLAY_PRIMARY)->enableDisplayDriverLog(HWC_DEBUG_LOG_IRQ_ON);
    }
    return 0;
}

int HWCMediator::ComposerExtIntf::setScenarioHint(ComposerExt::ScenarioHint flag,
        ComposerExt::ScenarioHint mask)
{
    m_hwc_mediator->setScenarioHint(flag, mask);
    return 0;
}

int HWCMediator::ComposerExtIntf::getDispDumpEnable(ComposerExt::DisplayType dpy,
                                                    bool enable,
                                                    ComposerExt::Rect src,
                                                    uint32_t dst_w, uint32_t dst_h,
                                                    unsigned int hal_format)
{
    HWC_LOGI("%s(), dpy %d, enable %d, (%d,%d,%d,%d), (%d, %d), %d",
             __FUNCTION__,
             dpy, enable,
             src.left, src.top, src.right, src.bottom,
             dst_w, dst_h,
             hal_format);

    hwc2_display_t disp_id;
    int err = mapComposerExtDisplayType(dpy, &disp_id);
    if (err)
    {
        HWC_LOGE("%s(), display type(%d) is not support", __FUNCTION__, dpy);
        return err;
    }

    sp<HWCDisplay> display = m_hwc_mediator->getHWCDisplay(disp_id);
    if (display == nullptr)
    {
        HWC_LOGE("%s(), fail to get HWCDisplay(%" PRIu64 ")", __FUNCTION__, disp_id);
        return -ENXIO;
    }

    std::shared_ptr<DisplayDump> disp_dump = display->getDisplayDump();
    if (disp_dump == nullptr)
    {
        HWC_LOGE("%s(), disp_dump is null with disp_id(%" PRIu64 ")", __FUNCTION__, disp_id);
        return -ENXIO;
    }

    Rect hwc_src = Rect(static_cast<int32_t>(src.left),
                        static_cast<int32_t>(src.top),
                        static_cast<int32_t>(src.right),
                        static_cast<int32_t>(src.bottom));

    if (enable)
    {
        if (hwc_src.getWidth() <= 0 || hwc_src.getHeight() <= 0 || dst_w <= 0 || dst_h <= 0)
        {
            HWC_LOGE("%s(), src or dst width or height less than 0", __FUNCTION__);
            return -EINVAL;
        }
        // haven't know which dump point client would like to use
        bool support = display->isSupportWbParams(HWC_WDMA_DUMP_POINT_OVL_OUT,
                                                  static_cast<unsigned int>(hwc_src.getWidth()),
                                                  static_cast<unsigned int>(hwc_src.getHeight()),
                                                  dst_w, dst_h) ||
                       display->isSupportWbParams(HWC_WDMA_DUMP_POINT_PQ_OUT,
                                                  static_cast<unsigned int>(hwc_src.getWidth()),
                                                  static_cast<unsigned int>(hwc_src.getHeight()),
                                                  dst_w, dst_h);
        if (!support)
        {
            HWC_LOGW("%s(), dpy(%" PRIu64 ") not support with params, src(%d, %d), dst(%d, %d)",
                    __FUNCTION__, disp_id, hwc_src.getWidth(), hwc_src.getHeight(), dst_w, dst_h);
            return -EINVAL;
        }
    }

    err = disp_dump->setEnable(enable, hwc_src, dst_w, dst_h, hal_format);
    if (!err)
    {
        display->setWdmaEnable(HWC_WDMA_STATUS_DISPLAY_DUMP, enable);
    }

    return err;
}

int HWCMediator::ComposerExtIntf::getDispDump(ComposerExt::DisplayType dpy,
                                              ComposerExt::Rect src,
                                              uint32_t dst_w, uint32_t dst_h,
                                              ComposerExt::DispDumpPoint dump_point,
                                              ComposerExt::DispDumpRepaintMode repaint_mode,
                                              int64_t min_period_before_prev_get_ns,
                                              const native_handle_t* buffer,
                                              int release_fence)
{
    HWC_LOGI("%s(), dpy %d, (%d,%d,%d,%d), (%d, %d), (%d, %d), (%" PRId64 "), (%d), (%p)",
             __FUNCTION__, dpy,
             src.left, src.top, src.right, src.bottom,
             dst_w, dst_h,
             dump_point, repaint_mode,
             min_period_before_prev_get_ns,
             release_fence,
             buffer);

    hwc2_display_t disp_id;
    int err = mapComposerExtDisplayType(dpy, &disp_id);
    if (err)
    {
        HWC_LOGE("%s(), display type(%d) is not support", __FUNCTION__, dpy);
        return err;
    }

    std::shared_ptr<DisplayDump> disp_dump = m_hwc_mediator->getHWCDisplay(disp_id)->getDisplayDump();
    if (disp_dump == nullptr)
    {
        HWC_LOGE("%s(), disp_dump is null with disp_id(%" PRIu64 ")", __FUNCTION__, disp_id);
        return -ENXIO;
    }

    int hwc_dump_point;
    switch (dump_point)
    {
        case ComposerExt::DispDumpPoint::kOvlOut:
            hwc_dump_point = HWC_WDMA_DUMP_POINT_OVL_OUT;
            break;
        case ComposerExt::DispDumpPoint::kPqOut:
        default:
            hwc_dump_point = HWC_WDMA_DUMP_POINT_PQ_OUT;
            break;
    }

    Rect hwc_src = Rect(static_cast<int32_t>(src.left),
                        static_cast<int32_t>(src.top),
                        static_cast<int32_t>(src.right),
                        static_cast<int32_t>(src.bottom));

    bool support = false;
    support = m_hwc_mediator->getHWCDisplay(disp_id)->isSupportWbParams(hwc_dump_point,
                                                                        static_cast<unsigned int>(hwc_src.getWidth()),
                                                                        static_cast<unsigned int>(hwc_src.getHeight()),
                                                                        dst_w, dst_h);
    if (!support)
    {
        HWC_LOGW("%s(), dpy(%" PRIu64 ") not support with params dump_point(%d), src(%d, %d), dst(%d, %d)",
                 __FUNCTION__, disp_id, hwc_dump_point, hwc_src.getWidth(), hwc_src.getHeight(), dst_w, dst_h);
        return -EINVAL;
    }

    DisplayDump::FuncReadyCallback ready_callback = [callback = m_callback](const uint64_t disp_id,
                                                                            const native_handle_t* buffer,
                                                                            const int acq_fence,
                                                                            const int64_t vsync_period_ns)
                          {
                              auto cb = callback.lock();
                              if (cb)
                              {
                                  ComposerExt::DisplayType dpy;
                                  switch (disp_id)
                                  {
                                      case HWC_DISPLAY_PRIMARY:
                                          dpy = ComposerExt::DisplayType::kPrimary;
                                          break;
                                      case HWC_DISPLAY_EXTERNAL:
                                          dpy = ComposerExt::DisplayType::kExternal;
                                          break;
                                      case HWC_DISPLAY_VIRTUAL:
                                          dpy = ComposerExt::DisplayType::kVirtual;
                                          break;
                                      default:
                                          dpy = ComposerExt::DisplayType::kInvalid;
                                          break;
                                  }
                                  cb->NotifyDispDumpReady(dpy, buffer, acq_fence, vsync_period_ns);
                              }
                          };

    int hwc_repaint;
    switch (repaint_mode)
    {
        case ComposerExt::DispDumpRepaintMode::kRepaintAlways:
            hwc_repaint = DisplayDump::REPAINT_ALWAYS;
            break;
        case ComposerExt::DispDumpRepaintMode::kRepaintAuto:
            hwc_repaint = DisplayDump::REPAINT_AUTO;
            break;
        case ComposerExt::DispDumpRepaintMode::kRepaintNever:
        default:
            hwc_repaint = DisplayDump::REPAINT_NEVER;
            break;
    }

    // tell it next job need to dump to buffer handle
    return disp_dump->setDumpBuf(hwc_src, dst_w, dst_h, hwc_dump_point, hwc_repaint,
                                 min_period_before_prev_get_ns, buffer,
                                 release_fence, ready_callback);
}

