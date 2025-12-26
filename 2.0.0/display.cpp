#define DEBUG_LOG_TAG "DPY"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include <fcntl.h>
#include <stdint.h>

#include <numeric>
#include <cutils/properties.h>

#include <hwc_feature_list.h>

#include "utils/debug.h"
#include "utils/tools.h"

#include "dispatcher.h"
#include "display.h"
#include "overlay.h"
#include "event.h"
#include "sync.h"
#include "hwc2.h"

#include "platform_wrap.h"

#include <ged/ged_dvfs.h>
#include <ged/ged.h>

#ifdef USES_HDMISERVICE
#include "hdmi_interface.h"
#endif

#define BOUNDARY_CHECK_DPY(dpy)                                                                 \
    if (dpy >= DisplayManager::MAX_DISPLAYS) {                                       \
        LOG_FATAL("[%s] dpy(%d) out of range %s:%d", DEBUG_LOG_TAG, dpy, __func__, __LINE__);   \
    }
#define BASIC_CFG_IDX 0
// ---------------------------------------------------------------------------

sp<UEventThread> g_uevent_thread = NULL;
sp<RefreshRequestThread> g_refresh_thread = NULL;

#ifdef MTK_DRM_PIXEL_SHIFT
sp<PixelShiftThread> g_pixel_shift_thread = NULL;
#endif

#ifdef MTK_USER_BUILD
int DisplayManager::m_profile_level = PROFILE_NONE;
#else
int DisplayManager::m_profile_level = PROFILE_COMP | PROFILE_BLT | PROFILE_TRIG;
#endif

void EDID_1_3::updateCheckSum()
{
    uint8_t *dump;
    dump = reinterpret_cast<uint8_t*>(&m_edid_data);
    uint8_t sum = std::accumulate(&dump[0], &dump[126], static_cast<uint8_t>(0));
    sum = (~sum) + 1;
    HWC_LOGV("[DBG] Sum 0x%x", sum);
    m_edid_data.m_checksum = sum;
}

void EDID_1_3::genEdid(const char* dpy_name)
{
    HWC_LOGV("name %s", dpy_name);
    // get v_p_id
    // EISA id MTK
    uint16_t eisa = 0x8B36;
    uint32_t serial = 1;
    setProductInfo(eisa, eisa, serial, 0x2A, 0x1f);
    genDescriptor(m_edid_data.m_descriptor[0], DISPLAY_NAME, "MTKDEV", sizeof("MTKDEV"));
    // it total has four descriptors but only one descriptor reference by SF.
    /*descriptor[1].genDescriptor(DISPLAY_NAME, "MTKDEV", sizeof("MTKDEV"));
    descriptor[2].genDescriptor(DISPLAY_NAME, "MTKDEV", sizeof("MTKDEV"));
    descriptor[3].genDescriptor(DISPLAY_NAME, "MTKDEV", sizeof("MTKDEV"));*/
    updateCheckSum();
    m_size = sizeof(m_edid_data);
    m_is_valid =true;
    return;
}

DisplayManager& DisplayManager::getInstance()
{
    static DisplayManager gInstance;
    return gInstance;
}

void DisplayManager::genDisplayIdForDisplay(uint64_t dpy)
{
    sp<HWCDisplay> hwc_display = HWCMediator::getInstance().getHWCDisplay(dpy);

    m_edid[dpy].port = hwc_display->getId();
    m_edid[dpy].startEdid();
    m_edid[dpy].genEdid("MTK");

    if (Platform::getInstance().m_config.dbg_switch & HWC_DBG_SWITCH_DEBUG_DISP_ID_INFO)
    {
        uint8_t* dump = reinterpret_cast<uint8_t*>( &(m_edid[dpy].m_edid_data) );
        for (uint32_t i = 0; i < m_edid[dpy].m_size; i++)
        {
            HWC_LOGI("[DBG] %s val[%d] %x", __FUNCTION__, i, dump[i]);
        }
    }
}

DisplayManager::DisplayManager()
    : m_curr_disp_num(0)
    , m_fake_disp_num(0)
    , m_listener(NULL)
    , m_video_hdcp(UINT_MAX)
    , m_ged_log_handle(nullptr)
    , m_active_config(0)
{
    for (uint32_t i = 0; i < MAX_DISPLAYS; i++)
    {
        DisplayData *tmp = NULL;
        tmp = (DisplayData*) calloc(1, sizeof(DisplayData));
        LOG_ALWAYS_FATAL_IF(tmp == nullptr, "DisplayData[%d] calloc(%zu) fail"
            , i, MAX_DISPLAYS * sizeof(DisplayData));
        // create basic config
        m_data[i].push_back(tmp);
        m_data[i][BASIC_CFG_IDX]->trigger_by_vsync = false;
    }

    switch (HwcFeatureList::getInstance().getFeature().trigger_by_vsync)
    {
        case 1:
            m_data[HWC_DISPLAY_EXTERNAL][BASIC_CFG_IDX]->trigger_by_vsync = true;
            m_data[HWC_DISPLAY_EXTERNAL_1][BASIC_CFG_IDX]->trigger_by_vsync = true;
            break;
        case 2:
            m_data[HWC_DISPLAY_EXTERNAL][BASIC_CFG_IDX]->trigger_by_vsync = true;
            m_data[HWC_DISPLAY_EXTERNAL_1][BASIC_CFG_IDX]->trigger_by_vsync = true;
            m_data[HWC_DISPLAY_VIRTUAL][BASIC_CFG_IDX]->trigger_by_vsync = true;
            break;
    }

    g_uevent_thread = new UEventThread();
    if (g_uevent_thread == NULL)
    {
        HWC_LOGE("Failed to initialize UEvent thread!!");
        abort();
    }
    g_uevent_thread->initialize();

    if (HWCMediator::getInstance().getOvlDevice(HWC_DISPLAY_PRIMARY)->isDispSelfRefreshSupported())
    {
        g_refresh_thread = new RefreshRequestThread();
        if (g_refresh_thread == NULL) {
            HWC_LOGE("Failed to initialize RefreshRequestThread");
            abort();
        }
        g_refresh_thread->initialize();
        g_refresh_thread->setEnabled(true);
    }

#ifdef MTK_DRM_PIXEL_SHIFT
    g_pixel_shift_thread = new PixelShiftThread();
    if (g_pixel_shift_thread == NULL)
    {
        HWC_LOGE("Failed to initialize PixelShift thread!!");
        abort();
    }
    g_pixel_shift_thread->start();
#endif

    for (int i = 0; i < MAX_DISPLAYS; i++)
    {
        m_display_power_state[i] = false;
        m_usage[i] = ComposerExt::DisplayUsage::kUnknown;
        m_change_state[i] = 0;
        m_first_commit_done[i] = false;
        m_display_connected[i] = false;
        m_ext_active[i] = false;
        m_cur_connector_type[i] = 0;
    }

    m_primay_display_power = HWC2_POWER_MODE_ON;
}

DisplayManager::~DisplayManager()
{
    m_listener = NULL;
    for (uint32_t i = 0; i < MAX_DISPLAYS; i++)
    {
        for (auto iter: m_data[i])
        {
            if (iter != nullptr)
                free(iter);
        }
        m_data[i].clear();
    }

    if (m_ged_log_handle != nullptr)
        ged_log_disconnect(m_ged_log_handle);
}

int DisplayManager::getDeviceId(uint64_t dpy, uint8_t* out_port, uint8_t* data, uint32_t* out_data_size)
{
    uint32_t sz = 0;
    getDeviceIdSize(dpy, &sz);
    if (sz != *out_data_size)
    {
        return -1;
    }

    if (m_edid[dpy].m_is_valid == true)
    {
        *out_port = static_cast<uint8_t>(m_edid[dpy].port);
        memcpy(data, &m_edid[dpy].m_edid_data, m_edid[dpy].m_size);
    }
    else
    {
        HWC_LOGW("dpy: %" PRIu64 " Id not init", dpy);
        return -1;
    }
    return 0;
}

int DisplayManager::getDeviceIdSize(uint64_t dpy, uint32_t* sz)
{
    if (m_edid[dpy].m_is_valid == true && sz != nullptr)
    {
        *sz = m_edid[dpy].m_size;
    }
    else
    {
        HWC_LOGW("dpy: %" PRIu64 " Id not init, sz %p", dpy, sz);
        return -1;
    }
    return 0;
}

void DisplayManager::initInternal(uint64_t dpy, uint32_t drm_id_crtc)
{
    if (dpy == HWC_DISPLAY_PRIMARY)
    {
        m_curr_disp_num++;

        if (m_listener != NULL)
        {
            m_listener->onPlugIn(dpy, true);
        }

        createVsyncThread(dpy, drm_id_crtc);

        HWC_LOGI("Display Information:");
        HWC_LOGI("# fo current devices : %d", m_curr_disp_num.load(memory_order_relaxed));
        printDisplayInfo(dpy);
        m_display_power_state[dpy] = true;
        m_display_connected[dpy] = true;
        m_listener->onHotPlugExt(dpy, true);
    }
    else
    {
        hotplugExt(dpy, true);
    }
}

void DisplayManager::initPrimaryExternal(uint64_t dpy, uint32_t drm_id_crtc)
{
    HWC_LOGI("Display Information: dpy: %" PRIu64 " crtc: %d", dpy, drm_id_crtc);
    HWC_LOGI("# fo current devices : %d", m_curr_disp_num.load(memory_order_relaxed));

    //m_display_power_state[dpy] = true;
    //m_display_connected[dpy] = true;

    // init fake data
    DisplayData* disp_data = m_data[dpy][BASIC_CFG_IDX];
    disp_data->width     = 1920;
    disp_data->height    = 1080;
    disp_data->density   = 160;
    disp_data->xdpi      = 160000;
    disp_data->ydpi      = 160000;
    disp_data->has_vsync = 0;
    disp_data->connected = 1;
    disp_data->secure    = true;
    disp_data->subtype   = HWC_DISPLAY_HDMI_MHL;
    disp_data->refresh   = nsecs_t(1e9 / float(60) + 0.5f);
    disp_data->pixels    = static_cast<uint32_t>(disp_data->width * disp_data->height);

    int32_t width = 3840, height = 2160;
    HWCMediator::getInstance().getHWCDisplay(dpy)->getBootResolution(&width, &height);
    disp_data->width = width;
    disp_data->height = height;

    printDisplayInfo(dpy);

    m_listener->onHotPlugExt(dpy, true);
}


void DisplayManager::resentCallback()
{
    m_listener->onHotPlugExt(HWC_DISPLAY_PRIMARY, true);
}

void DisplayManager::createVsyncThread(uint64_t dpy, uint32_t drm_id_crtc)
{
    BOUNDARY_CHECK_DPY(dpy);
    AutoMutex _l(m_vsyncs[dpy].lock);
    m_vsyncs[dpy].thread = new VSyncThread(dpy, drm_id_crtc);
    if (m_vsyncs[dpy].thread == NULL)
    {
        HWC_LOGE("dpy=%" PRIu64 "/Failed to initialize VSYNC thread!!", dpy);
        abort();
    }
    m_vsyncs[dpy].thread->initialize(!m_data[dpy][BASIC_CFG_IDX]->has_vsync, m_data[dpy][BASIC_CFG_IDX]->refresh);
}

void DisplayManager::destroyVsyncThread(uint64_t dpy)
{
    BOUNDARY_CHECK_DPY(dpy);
    if (m_vsyncs[dpy].thread != NULL)
    {
        m_vsyncs[dpy].thread->requestExit();
        m_vsyncs[dpy].thread->setLoopAgain();
        m_vsyncs[dpy].thread->join();
    }

    {
        // We cannot lock the whole destoryVsyncThread(), or it will cause the deadlock between
        // UEventThread and DispatcherThread. When a secondary display plugged out, UEventThread
        // will wait completion of VSyncThread, and needs to acquire the VSync lock of onVSync().
        // DispatcherThread acquired the lock of onVsync() firstly and request a next VSync.
        // Unfortunately, DispatcherThread cannot get a VSync because the vsync lock is acquired
        // by UEventThread.
        AutoMutex _l(m_vsyncs[dpy].lock);
        m_vsyncs[dpy].thread = NULL;
    }
}

void DisplayManager::printDisplayInfo(uint64_t dpy)
{
    if (dpy >= MAX_DISPLAYS) return;

    DisplayData* disp_data = m_data[dpy][BASIC_CFG_IDX];

    HWC_LOGI("------------------------------------");
    HWC_LOGI("Device id   : %" PRIu64,   dpy);
    HWC_LOGI("Width       : %u",   disp_data->width);
    HWC_LOGI("Height      : %u",   disp_data->height);
    HWC_LOGI("xdpi        : %f",   disp_data->xdpi);
    HWC_LOGI("ydpi        : %f",   disp_data->ydpi);
    HWC_LOGI("vsync       : %d",   disp_data->has_vsync);
    HWC_LOGI("refresh     : %" PRId64, disp_data->refresh);
    HWC_LOGI("connected   : %d",   disp_data->connected);
    HWC_LOGI("hwrotation  : %d",   disp_data->hwrotation);
    HWC_LOGI("subtype     : %d",   disp_data->subtype);
    HWC_LOGI("secure      : %d",   disp_data->secure ? 1 : 0);
    HWC_LOGI("aspect      : %1.3f, %1.3f",
        disp_data->aspect_portrait, disp_data->aspect_landscape);
    HWC_LOGI("portrait    : [%4d,%4d,%4d,%4d]",
        disp_data->mir_portrait.left,  disp_data->mir_portrait.top,
        disp_data->mir_portrait.right, disp_data->mir_portrait.bottom);
    HWC_LOGI("landscape   : [%4d,%4d,%4d,%4d]",
        disp_data->mir_landscape.left,  disp_data->mir_landscape.top,
        disp_data->mir_landscape.right, disp_data->mir_landscape.bottom);
    HWC_LOGI("trigger_by_vsync: %d", disp_data->trigger_by_vsync);
    HWC_LOGI("density     : %d", disp_data->density);
}

void DisplayManager::dump(struct dump_buff* /*log*/)
{
    if (m_vsyncs[HWC_DISPLAY_PRIMARY].thread != NULL)
        m_vsyncs[HWC_DISPLAY_PRIMARY].thread->setProperty();

    if (g_uevent_thread != NULL)
        g_uevent_thread->setProperty();
}

void DisplayManager::setListener(const sp<EventListener>& listener)
{
    m_listener = listener;
}

void DisplayManager::requestVSync(uint64_t dpy, bool enabled)
{
    BOUNDARY_CHECK_DPY(dpy);
    AutoMutex _l(m_vsyncs[dpy].lock);
    if (m_vsyncs[dpy].thread != NULL)
        m_vsyncs[dpy].thread->setEnabled(enabled);
}

void DisplayManager::requestNextVSync(uint64_t dpy)
{
    BOUNDARY_CHECK_DPY(dpy);
    AutoMutex _l(m_vsyncs[dpy].lock);
    if (m_vsyncs[dpy].thread != NULL)
        m_vsyncs[dpy].thread->setLoopAgain();
}

void DisplayManager::vsync(uint64_t dpy, nsecs_t timestamp, bool enabled)
{
    if (m_listener != NULL)
    {
        // check if primary display needs to use external vsync source
        if (HWC_DISPLAY_PRIMARY != dpy)
        {
            Mutex::Autolock _l(m_power_lock);

            if (m_data[HWC_DISPLAY_PRIMARY][BASIC_CFG_IDX]->vsync_source == dpy)
                m_listener->onVSync(HWC_DISPLAY_PRIMARY, timestamp, enabled);
        }

        m_listener->onVSync(dpy, timestamp, enabled);
    }
}

void DisplayManager::hotplugExt(uint64_t dpy, bool connected, bool fake, bool notify)
{
    HWC_LOGI("Hotplug external disp(%" PRIu64 ") connect(%d) fake(%d)", dpy, connected, fake);

    DisplayData* disp_data = m_data[dpy][BASIC_CFG_IDX];

    disp_data->trigger_by_vsync =
        HwcFeatureList::getInstance().getFeature().trigger_by_vsync > 0 ? true : false;

    if (dpy == HWC_DISPLAY_PRIMARY)
    {
        if (connected && !m_display_connected[dpy])
        {
            sp<IOverlayDevice> ovlDevice = HWCMediator::getInstance().getOvlDevice(dpy);
            int32_t res = ovlDevice->updateDisplayResolution(dpy);
            if (res != NO_ERROR)
            {
                HWC_LOGE("give up report display(%" PRIu64 ") hotplug event: %d", dpy, res);
                //return;
            }

	    //xbh patch begin
            //if (m_listener == NULL) return;
	    if (m_listener == NULL)
            {
                while (true)
                {
		            HWC_LOGE("[HWC][%s, %d] display(%" PRIu64 ") get null listener, try to wait 5 ms.", __func__, __LINE__, dpy);
                    usleep(5000);
                    if (m_listener != NULL)
                        break;
                }
            }
	    //xbh patch end
            if (ovlDevice->updateConnectorMode(dpy, HWCMediator::getInstance().getHWCDisplay(dpy)->getDrmIdCurCrtc()) != NO_ERROR)
            {
                HWC_LOGI("---primary display hotplug in end at updateConnectorMode---");
                return;
            }

            if (m_listener != NULL) m_listener->onPlugIn(dpy, false);

            if (fake == true)
            {
                static int _s_shrink_size = 4;
                _s_shrink_size = (_s_shrink_size == 2) ? 4 : 2;
                memcpy(disp_data, m_data[HWC_DISPLAY_PRIMARY][BASIC_CFG_IDX], sizeof(DisplayData));
                disp_data->width   = m_data[HWC_DISPLAY_PRIMARY][BASIC_CFG_IDX]->width / _s_shrink_size;
                disp_data->height  = m_data[HWC_DISPLAY_PRIMARY][BASIC_CFG_IDX]->height / _s_shrink_size;
                disp_data->subtype = FAKE_DISPLAY;

                m_fake_disp_num++;
            }

            createVsyncThread(dpy, HWCMediator::getInstance().getHWCDisplay(dpy)->getDrmIdCurCrtc());

            hotplugPost(dpy, 1, DISP_PLUG_CONNECT, notify);
            HWCMediator::getInstance().getHWCDisplay(dpy)->updateDisplayConfigs(true);

            m_display_connected[dpy] = true;

            if (m_listener != NULL && notify) m_listener->onHotPlugExt(dpy, HWC2_CONNECTION_CONNECTED);

            if (disp_data->trigger_by_vsync)
            {
                requestVSync(dpy, true);
            }
            HWCDispatcher::getInstance().ignoreJob(dpy, false);

            HWC_LOGI("---primary display hotplug in end---");
            usleep(32000);
        }
        else if (!connected && m_display_connected[dpy])
        {
            HWCDispatcher::getInstance().ignoreJob(dpy, true);

            if (fake == true)
            {
                if (m_fake_disp_num < 1)
                {
                    HWC_LOGW("%s(), m_fake_disp_num %u < 1", __FUNCTION__, m_fake_disp_num);
                    m_fake_disp_num = 1;
                }
                m_fake_disp_num--;
            }

            if (disp_data->trigger_by_vsync)
            {
                requestVSync(dpy, false);
            }

            destroyVsyncThread(dpy);

            m_active_config = 0;
            HWCMediator::getInstance().getHWCDisplay(dpy)->updateDisplayConfigs(false);

            m_display_connected[dpy] = false;

            // Reset disp_data to default value when disconnect primary display.
            disp_data->width = 3840;
            disp_data->height = 2160;
            disp_data->refresh = 16666667;

            if (m_listener != NULL)
            {
                m_listener->onPlugOut(dpy);
                if (notify)
                {
                    m_listener->onHotPlugExt(dpy, HWC2_CONNECTION_CONNECTED);
                }
            }

            HWC_LOGI("---primary display hotplug out end---");
        }
    }
    else
    {
        if (connected && !m_display_connected[dpy])
        {
            sp<IOverlayDevice> ovlDevice = HWCMediator::getInstance().getOvlDevice(dpy);
            int32_t res = ovlDevice->updateDisplayResolution(dpy);
            if (res != NO_ERROR)
            {
                HWC_LOGE("give up report display(%" PRIu64 ") hotplug event: %d", dpy, res);
                //return;
            }

            if (m_listener == NULL) return;

            if (ovlDevice->updateConnectorMode(dpy, HWCMediator::getInstance().getHWCDisplay(dpy)->getDrmIdCurCrtc()) != NO_ERROR)
            {
                HWC_LOGI("---external display(%" PRIu64 ") hotplug in end at updateConnectorMode---", dpy);
                return;
            }

            HWCMediator::getInstance().createExternalDisplay(dpy);
            if (m_listener != NULL) m_listener->onPlugIn(dpy, false);

            if (fake == true)
            {
                static int _s_shrink_size = 4;
                _s_shrink_size = (_s_shrink_size == 2) ? 4 : 2;
                memcpy(disp_data, m_data[HWC_DISPLAY_PRIMARY][BASIC_CFG_IDX], sizeof(DisplayData));
                disp_data->width   = m_data[HWC_DISPLAY_PRIMARY][BASIC_CFG_IDX]->width / _s_shrink_size;
                disp_data->height  = m_data[HWC_DISPLAY_PRIMARY][BASIC_CFG_IDX]->height / _s_shrink_size;
                disp_data->subtype = FAKE_DISPLAY;

                m_fake_disp_num++;
            }

            createVsyncThread(dpy, HWCMediator::getInstance().getHWCDisplay(dpy)->getDrmIdCurCrtc());

            hotplugPost(dpy, 1, DISP_PLUG_CONNECT, notify);

            m_display_connected[dpy] = true;

            if (m_listener != NULL && notify) m_listener->onHotPlugExt(dpy, HWC2_CONNECTION_CONNECTED);

            if (disp_data->trigger_by_vsync)
            {
                requestVSync(dpy, true);
            }
            HWCDispatcher::getInstance().ignoreJob(dpy, false);
            if (!HWCMediator::getInstance().getHWCDisplay(dpy)->isInternal() && notify == true)
            {
                status_t ret = m_condition[dpy].waitRelative(m_uevent_lock[dpy], ms2ns(3000));
                if (ret != NO_ERROR)
                {
                    if (ret == TIMED_OUT)
                    {
                        HWC_LOGW("hotplug in is still not finished");
                    }
                    else
                    {
                        HWC_LOGE("To wait hotplug in failed(%d)", ret);
                    }
                }
            }
            HWC_LOGI("---external display(%" PRIu64 ") hotplug in end---", dpy);
        }
        else if (!connected && m_display_connected[dpy])
        {
            HWCDispatcher::getInstance().ignoreJob(dpy, true);

            if (fake == true)
            {
                if (m_fake_disp_num < 1)
                {
                    HWC_LOGW("%s(), m_fake_disp_num %u < 1", __FUNCTION__, m_fake_disp_num);
                    m_fake_disp_num = 1;
                }
                m_fake_disp_num--;
            }

            if (disp_data->trigger_by_vsync)
            {
                requestVSync(dpy, false);
            }

            destroyVsyncThread(dpy);

            m_display_connected[dpy] = false;

            if (m_listener != NULL)
            {
                m_listener->onPlugOut(dpy);
                hotplugPost(dpy, 0, DISP_PLUG_DISCONNECT, true);
                if (notify)
                {
                    m_listener->onHotPlugExt(dpy, HWC2_CONNECTION_DISCONNECTED);
                }
            }

            if (!HWCMediator::getInstance().getHWCDisplay(dpy)->isInternal() && notify) // mediatek patch begin/end
            {
                status_t ret = m_condition[dpy].waitRelative(m_uevent_lock[dpy], ms2ns(3000));
                if (ret != NO_ERROR)
                {
                    if (ret == TIMED_OUT)
                    {
                        HWC_LOGW("hotplug out is still not finished");
                    }
                    else
                    {
                        HWC_LOGE("To wait hotplug out failed(%d)", ret);
                    }
                }
            }
            else if (!HWCMediator::getInstance().getHWCDisplay(dpy)->isInternal()) // mediatek patch begin
            {
                notifyHotplugOutDone(dpy);
            }// mediatek patch end
            HWCMediator::getInstance().destroyExternalDisplay(dpy);
            HWC_LOGI("---external display(%" PRIu64 ") hotplug out end---", dpy);
        }
    }
}

void DisplayManager::hotplugExtOut()
{
    hotplugPost(HWC_DISPLAY_EXTERNAL, 0, DISP_PLUG_DISCONNECT, true);
}

void DisplayManager::notifyHotplugInDone(uint64_t dpy)
{
    if (!m_ext_active[dpy])
    {
        m_ext_active[dpy] = true;
        m_condition[dpy].signal();
        HWC_LOGI("(mtk), %s display(%" PRIu64 ")", __func__, dpy);
    }
}

void DisplayManager::notifyHotplugOutDone(uint64_t dpy)
{
    if (m_ext_active[dpy])
    {
        m_ext_active[dpy] = false;
        m_condition[dpy].signal();
        HWC_LOGI("(mtk), %s display(%" PRIu64 ")", __func__, dpy);
    }
}

void DisplayManager::hotplugVir(
    const uint64_t& dpy,
    const bool& connected,
    const uint32_t& width,
    const uint32_t& height,
    const unsigned int& format)
{
    if (dpy != HWC_DISPLAY_VIRTUAL)
    {
        HWC_LOGW("Failed to hotplug virtual disp(%" PRIu64 ") !", dpy);
        return;
    }

    DisplayData* disp_data = m_data[HWC_DISPLAY_VIRTUAL][BASIC_CFG_IDX];

    disp_data->trigger_by_vsync =
        HwcFeatureList::getInstance().getFeature().trigger_by_vsync > 1 ? true : false;

    HWC_LOGW("Hotplug virtual disp(%" PRIu64 ") connect(%d)", dpy, connected);

    if (connected)
    {
        setDisplayDataForVir(HWC_DISPLAY_VIRTUAL, width, height, format);

        hotplugPost(dpy, 1, DISP_PLUG_CONNECT);

        if (m_listener != NULL) m_listener->onPlugIn(dpy, false, width, height);

        // TODO: How HWC receives requests from WFD frameworks
    }
    else
    {
        if (m_listener != NULL) m_listener->onPlugOut(dpy);

        hotplugPost(dpy, 0, DISP_PLUG_DISCONNECT);

        // TODO: How HWC receives requests from WFD frameworks
    }
}

bool DisplayManager::checkIsWfd(uint64_t dpy)
{
    bool is_wfd = false;
    bool usage_is_wfd = (static_cast<uint32_t>(getUsage(dpy)) & static_cast<uint32_t>(ComposerExt::DisplayUsage::kIsWFD)) > 0;
    char value[PROPERTY_VALUE_MAX] = {0};
    property_get("debug.sf.enable_hwc_vds", value, "-1");
    int enable_hwc_vds = atoi(value);
    if (enable_hwc_vds == 0)
    {
        // debug.sf.enable_hwc_vds is set to 0, so SurfaceFlinger should not
        // request HWC to process virtual display. the exception is that
        // SurfaceFlinger has applied the patch to check virtaul display
        // whether it is WFD. therefore we assume SurfaceFlinger has checked
        // the virtual display when this property is 0.
        is_wfd = true;
    }
    else
    {
        is_wfd = usage_is_wfd;
    }
    HWC_LOGI("%s: is_wfd=%d enable_hwc_vds[%d] usage_is_wfd[%d]",
            __func__, is_wfd, enable_hwc_vds, usage_is_wfd);
    return is_wfd;
}

void DisplayManager::refreshForDisplay(uint64_t dpy, unsigned int type)
{
    if (m_listener != NULL)
    {
        m_listener->onRefresh(dpy, type);
    }
}

void DisplayManager::refreshForDriver(uint64_t dpy, unsigned int type)
{
    if (m_listener != NULL)
    {
        if (HWC_WAIT_FOR_REFRESH < type && type < HWC_REFRESH_TYPE_NUM)
        {
            m_listener->onRefresh(dpy, type);
        }
    }
}

void DisplayManager::setDisplayDataForVir(const uint64_t& dpy,
                                          const uint32_t& width, const uint32_t& height,
                                          const unsigned int& format)
{
    DisplayData* disp_data = m_data[dpy][BASIC_CFG_IDX];

    if (dpy == HWC_DISPLAY_VIRTUAL)
    {
        disp_data->width     = static_cast<int>(width);
        disp_data->height    = static_cast<int>(height);
        disp_data->format    = format;
        disp_data->xdpi      = m_data[HWC_DISPLAY_PRIMARY][BASIC_CFG_IDX]->xdpi;
        disp_data->ydpi      = m_data[HWC_DISPLAY_PRIMARY][BASIC_CFG_IDX]->ydpi;
        disp_data->has_vsync = false;
        disp_data->connected = true;
        disp_data->density   = m_data[HWC_DISPLAY_PRIMARY][BASIC_CFG_IDX]->density;

        const bool is_wfd = checkIsWfd(dpy);
        if (is_wfd)
        {
            disp_data->secure  = (static_cast<uint32_t>(getUsage(dpy)) & static_cast<uint32_t>(ComposerExt::DisplayUsage::kIsSecure)) > 0;
            disp_data->subtype = HWC_DISPLAY_WIRELESS;
            disp_data->hdcp_version = UINT_MAX;
            HWC_LOGI("(%" PRIu64 ") hdcp version is %d", dpy, disp_data->hdcp_version);
        }
        else
        {
            disp_data->secure  = false;
            disp_data->subtype = HWC_DISPLAY_MEMORY;
        }

        // [NOTE]
        // only if the display without any physical rotation,
        // same ratio can be applied to both portrait and landscape
        disp_data->aspect_portrait  = float(disp_data->width) / float(disp_data->height);
        disp_data->aspect_landscape = disp_data->aspect_portrait;

        // TODO
        //disp_data->vsync_source = HWC_DISPLAY_VIRTUAL;

        // currently no need for vir disp
        disp_data->hwrotation = 0;

        disp_data->pixels = static_cast<unsigned int>(disp_data->width * disp_data->height);

        disp_data->trigger_by_vsync =
            HwcFeatureList::getInstance().getFeature().trigger_by_vsync > 1 ? true : false;
    }
}

void DisplayManager::setExtraDisplayDataForPhy(uint64_t dpy, uint32_t drm_id_crtc, uint32_t drm_id_connector, bool is_internal)
{
    unsigned int physical_width = 0;
    unsigned int physical_height = 0;
    SessionInfo info;
    sp<IOverlayDevice> ovl_device = HWCMediator::getInstance().getOvlDevice(dpy);
    DisplayData* disp_data = m_data[dpy][BASIC_CFG_IDX];
    // (w, h) key same = same group
    std::vector<std::pair<int, int>> group_list;
    group_list.emplace_back(disp_data->width, disp_data->height);

    // fill primary display extra config
    uint32_t num_configs = ovl_device->getNumConfigs(dpy, drm_id_connector);

    ovl_device->getOverlaySessionInfo(dpy, drm_id_crtc, &info);

    if (is_internal == true)
    {
        // get planel Size when it is an internal display
        getPhysicalPanelSize(&physical_width, &physical_height, info);
    }

    if (num_configs > 1)
    {
        m_data[dpy].resize(num_configs);

        for (unsigned int i = 1; i < num_configs; ++i)
        {
            m_data[dpy][i] = (DisplayData *) calloc(1, sizeof(DisplayData));
            *m_data[dpy][i] = *m_data[dpy][BASIC_CFG_IDX];
            m_data[dpy][i]->width = ovl_device->getWidth(dpy, drm_id_connector, i);
            m_data[dpy][i]->height = ovl_device->getHeight(dpy, drm_id_connector, i);
            if (is_internal == true)
            {
                m_data[dpy][i]->xdpi = physical_width == 0 ? 0 :
                    m_data[dpy][i]->width * 25.4f * 1000000 / physical_width;
                m_data[dpy][i]->ydpi = physical_height == 0 ? 0 :
                    m_data[dpy][i]->height * 25.4f * 1000000 / physical_height;
            }
            else
            {
                m_data[dpy][i]->xdpi = info.physicalWidth == 0 ? disp_data->density :
                                       (info.displayWidth * 25.4f / info.physicalWidth);
                m_data[dpy][i]->ydpi = info.physicalHeight == 0 ? disp_data->density :
                                       (info.displayHeight * 25.4f / info.physicalHeight);
            }
            m_data[dpy][i]->aspect_portrait =
                float(m_data[dpy][i]->width) / float(m_data[dpy][i]->height);
            m_data[dpy][i]->aspect_landscape =
                float(m_data[dpy][i]->height) / float(m_data[dpy][i]->width);
            m_data[dpy][i]->mir_portrait =
                Rect(m_data[dpy][i]->width, m_data[dpy][i]->height);
            m_data[dpy][i]->mir_landscape =
                Rect(m_data[dpy][i]->width, m_data[dpy][i]->height);
            int32_t refresh_rate = ovl_device->getRefresh(dpy, drm_id_connector, i);
            m_data[dpy][i]->refresh = nsecs_t(1e9 / float(refresh_rate) + 0.5f);
            m_data[dpy][i]->pixels =
                static_cast<uint32_t>(m_data[dpy][i]->width * m_data[dpy][i]->height);

            // group
            auto compare = [w = m_data[dpy][i]->width, h = m_data[dpy][i]->height](std::pair<int, int> key)
                           {
                               return w == key.first && h == key.second;
                           };
            auto it = std::find_if(group_list.begin(), group_list.end(), compare);

            int group;
            if (it == group_list.end())
            {
                group_list.emplace_back(m_data[dpy][i]->width, m_data[dpy][i]->height);
                group = static_cast<int>(group_list.size() - 1);
            }
            else
            {
                group = static_cast<int>(it - group_list.begin());
            }
            m_data[dpy][i]->group = group;
        }
    }
}

void DisplayManager::setDisplayDataForPhy(uint64_t dpy, uint32_t drm_id_crtc, uint32_t drm_id_connector)
{
    DisplayData* disp_data = m_data[dpy][BASIC_CFG_IDX];
    bool is_internal = false;
    char value[PROPERTY_VALUE_MAX] = {0};

    // give default value of density
    float density = 160.0;

    if (dpy == HWC_DISPLAY_VIRTUAL)
    {
        HWC_LOGE("%s(), HWC2 should not call for virtual displays", __FUNCTION__);
        return;
    }

    SessionInfo info;
    sp<IOverlayDevice> ovl_device = HWCMediator::getInstance().getOvlDevice(dpy);
    unsigned int physical_width = 0;
    unsigned int physical_height = 0;
    std::vector<CreateDisplayInfo> create_disp_infos;
    getHwDevice()->getCreateDisplayInfos(create_disp_infos);

    // check if this display is an internal display
    for (auto iter: create_disp_infos)
    {
        if (iter.drm_id_connector == drm_id_connector)
        {
            is_internal = iter.is_internal;
        }
    }

    if (is_internal == true)
    {
        ovl_device->getOverlaySessionInfo(dpy, drm_id_crtc, &info);

        disp_data->width     = static_cast<int>(info.displayWidth);
        disp_data->height    = static_cast<int>(info.displayHeight);
        disp_data->format    = info.displayFormat;
        getPhysicalPanelSize(&physical_width, &physical_height, info);
        disp_data->density   = info.density == 0 ? static_cast<int>(density) : static_cast<int>(info.density);
        disp_data->xdpi      = physical_width == 0 ? disp_data->density * 1000:
                                (info.displayWidth * 25.4f * 1000000 / physical_width);
        disp_data->ydpi      = physical_height == 0 ? disp_data->density * 1000:
                                (info.displayHeight * 25.4f * 1000000 / physical_height);

        disp_data->has_vsync = info.isHwVsyncAvailable;
        disp_data->connected = info.isConnected;

        // TODO: ask from display driver
        disp_data->secure    = true;
        disp_data->hdcp_version = UINT_MAX;
        disp_data->subtype   = HWC_DISPLAY_LCM;

        disp_data->aspect_portrait  = float(info.displayWidth) / float(info.displayHeight);
        disp_data->aspect_landscape = float(info.displayHeight) / float(info.displayWidth);
        disp_data->mir_portrait     = Rect(info.displayWidth, info.displayHeight);
        disp_data->mir_landscape    = Rect(info.displayWidth, info.displayHeight);

        disp_data->vsync_source = dpy;

        int32_t refresh_rate = ovl_device->getRefresh(dpy, drm_id_connector, 0);
        if (0 >= refresh_rate) refresh_rate = 60;
        disp_data->refresh = nsecs_t(1e9 / float(refresh_rate) + 0.5f);

        // get physically installed rotation for primary display from property
        property_get("ro.vendor.sf.hwrotation", value, "0");
        disp_data->hwrotation = static_cast<uint32_t>(atoi(value) / 90);

        disp_data->pixels = static_cast<uint32_t>(disp_data->width * disp_data->height);

        disp_data->trigger_by_vsync = false;

        disp_data->group = 0;

    }
    else
    {
        ovl_device->getOverlaySessionInfo(dpy, drm_id_crtc, &info);
        if (!info.isConnected)
        {
            HWC_LOGE("Failed to add display, hdmi is not connected!");
            return;
        }

        disp_data->width     = static_cast<int>(info.displayWidth);
        disp_data->height    = static_cast<int>(info.displayHeight);
        disp_data->format    = info.displayFormat;
        disp_data->density   = info.density == 0 ? static_cast<int>(density) : static_cast<int>(info.density);
        disp_data->xdpi      = info.physicalWidth == 0 ? disp_data->density :
                                (info.displayWidth * 25.4f / info.physicalWidth);
        disp_data->ydpi      = info.physicalHeight == 0 ? disp_data->density :
                                (info.displayHeight * 25.4f / info.physicalHeight);
        disp_data->has_vsync = info.isHwVsyncAvailable;
        disp_data->connected = info.isConnected;
        // TODO: check if hdcp 1.x is used

        disp_data->secure    = true;

        disp_data->hdcp_version = info.isHDCPSupported;
        HWC_LOGI("(%" PRIu64 ") hdcp version is %d", dpy, disp_data->hdcp_version);
        // ToDo: get from connector type.
        disp_data->subtype = HWC_DISPLAY_HDMI_MHL;

        // [NOTE]
        // only if the display without any physical rotation,
        // same ratio can be applied to both portrait and landscape
        disp_data->aspect_portrait  = float(info.displayWidth) / float(info.displayHeight);
        disp_data->aspect_landscape = disp_data->aspect_portrait;

        disp_data->vsync_source = dpy;

        int32_t refresh_rate = ovl_device->getCurrentRefresh(dpy, drm_id_crtc);
        if (0 >= refresh_rate) refresh_rate = 60;
        disp_data->refresh = nsecs_t(1e9 / float(refresh_rate) + 0.5f);

        // get physically installed rotation for extension display from property
        property_get("ro.vendor.sf.hwrotation.ext", value, "0");
        disp_data->hwrotation = static_cast<uint32_t>(atoi(value) / 90);

        disp_data->pixels = static_cast<uint32_t>(disp_data->width * disp_data->height);

        disp_data->trigger_by_vsync =
            HwcFeatureList::getInstance().getFeature().trigger_by_vsync > 0 ? true : false;
    }
    setExtraDisplayDataForPhy(dpy, drm_id_crtc, drm_id_connector, is_internal);
}

void DisplayManager::setMirrorRegion(uint64_t dpy)
{
    DisplayData* main_disp_data = m_data[HWC_DISPLAY_PRIMARY][BASIC_CFG_IDX];
    DisplayData* disp_data = m_data[dpy][BASIC_CFG_IDX];

    // calculate portrait region
    if (main_disp_data->aspect_portrait > disp_data->aspect_portrait)
    {
        // calculate for letterbox
        int portrait_h = static_cast<int>(disp_data->width / main_disp_data->aspect_portrait);
        int portrait_y = static_cast<int>((disp_data->height - portrait_h) / 2);
        disp_data->mir_portrait.left = 0;
        disp_data->mir_portrait.top = portrait_y;
        disp_data->mir_portrait.right = disp_data->width;
        disp_data->mir_portrait.bottom = portrait_y + portrait_h;
    }
    else
    {
        // calculate for pillarbox
        int portrait_w = static_cast<int>(disp_data->height * main_disp_data->aspect_portrait);
        int portrait_x = static_cast<int>((disp_data->width - portrait_w) / 2);
        disp_data->mir_portrait.left = portrait_x;
        disp_data->mir_portrait.top = 0;
        disp_data->mir_portrait.right = portrait_x + portrait_w;
        disp_data->mir_portrait.bottom = disp_data->height;
    }

    // calculate landscape region
    if (main_disp_data->aspect_landscape > disp_data->aspect_landscape)
    {
        // calculate for letterbox
        int landscape_h = static_cast<int>(disp_data->width / main_disp_data->aspect_landscape);
        int landscape_y = static_cast<int>((disp_data->height - landscape_h) / 2);
        disp_data->mir_landscape.left = 0;
        disp_data->mir_landscape.top = landscape_y;
        disp_data->mir_landscape.right = disp_data->width;
        disp_data->mir_landscape.bottom = landscape_y + landscape_h;
    }
    else
    {
        // calculate for pillarbox
        int landscape_w = static_cast<int>(disp_data->height * main_disp_data->aspect_landscape);
        int landscape_x = static_cast<int>((disp_data->width - landscape_w) / 2);
        disp_data->mir_landscape.left = landscape_x;
        disp_data->mir_landscape.top = 0;
        disp_data->mir_landscape.right = landscape_x + landscape_w;
        disp_data->mir_landscape.bottom = disp_data->height;
    }
}

void DisplayManager::hotplugPost(uint64_t dpy, bool connected, int state, bool print_info)
{
    DisplayData* disp_data = m_data[dpy][BASIC_CFG_IDX];

    switch (state)
    {
        case DISP_PLUG_CONNECT:
            HWC_LOGI("Added Display Information:");
            setMirrorRegion(dpy);
            if (print_info)
            {
                printDisplayInfo(dpy);
            }
            m_curr_disp_num++;
            break;

        case DISP_PLUG_DISCONNECT:
            HWC_LOGI("Removed Display Information:");
            if (print_info)
            {
                printDisplayInfo(dpy);
            }
            for (size_t i = 1; i < m_data[dpy].size(); i++)
            {
                free(m_data[dpy][i]);
            }
            m_data[dpy].resize(1);
            m_data[dpy].shrink_to_fit();
            memset(disp_data, 0, sizeof(DisplayData));
            m_curr_disp_num--;
            // reset edid
            m_edid[dpy].m_is_valid = false;
            memset(&m_edid[dpy].m_edid_data, 0, sizeof(struct EDID_DATA));
            m_edid[dpy].m_size = 0;
            m_edid[dpy].port = 0;
            break;

        case DISP_PLUG_NONE:
            HWC_LOGW("Unexpected hotplug: disp(%" PRIu64 ":%d) connect(%d)",
                     dpy, disp_data->connected, connected);
            return;
    };
}

void DisplayManager::setPowerMode(uint64_t dpy, int mode)
{
    Mutex::Autolock _l(m_power_lock);

    if (HWC_DISPLAY_PRIMARY != dpy) return;

    if (m_data[HWC_DISPLAY_EXTERNAL][BASIC_CFG_IDX]->connected &&
        m_data[HWC_DISPLAY_EXTERNAL][BASIC_CFG_IDX]->subtype == HWC_DISPLAY_SMARTBOOK)
    {
        m_data[dpy][BASIC_CFG_IDX]->vsync_source = (HWC2_POWER_MODE_OFF == mode) ?
                                        HWC_DISPLAY_EXTERNAL : HWC_DISPLAY_PRIMARY;
    }

    if (Platform::getInstance().m_config.dynamic_switch_path && (HWC_DISPLAY_PRIMARY == dpy))
    {
        display_set_primary_power(mode);
    }
}

bool DisplayManager::isAllDisplayOff()
{
    AutoMutex _l(m_state_lock);
    int res = 0;
    for (int i = 0; i < MAX_DISPLAYS; i++)
    {
        if (m_display_power_state[i])
        {
            res |= 1 << i;
        }
    }

    HWC_LOGD("all panel state: %x", res);
    return res ? false : true;
}

void DisplayManager::setDisplayPowerState(uint64_t dpy, int state)
{
    AutoMutex _l(m_state_lock);

    if (state == HWC2_POWER_MODE_OFF)
    {
        m_display_power_state[dpy] = false;
    }
    else
    {
        m_display_power_state[dpy] = true;
    }
}

int DisplayManager::getDisplayPowerState(const uint64_t& dpy)
{
    AutoMutex _l(m_state_lock);
    return m_display_power_state[dpy];
}

void DisplayManager::getPhysicalPanelSize(unsigned int *width, unsigned int *height,
                                          SessionInfo &info)
{
    if (width != NULL)
    {
        if (info.physicalWidthUm != 0)
        {
            *width = info.physicalWidthUm;
        }
        else
        {
            *width = info.physicalWidth * 1000;
        }
    }

    if (height != NULL)
    {
        if (info.physicalHeightUm != 0)
        {
            *height = info.physicalHeightUm;
        }
        else
        {
            *height = info.physicalHeight * 1000;
        }
    }
}

uint32_t DisplayManager::getVideoHdcp() const
{
    RWLock::AutoRLock _l(m_video_hdcp_rwlock);
    return m_video_hdcp;
}

void DisplayManager::setVideoHdcp(const uint32_t& video_hdcp)
{
    RWLock::AutoWLock _l(m_video_hdcp_rwlock);
    m_video_hdcp = video_hdcp;
}

int32_t DisplayManager::getSupportedColorMode(uint64_t dpy)
{
    return HWCMediator::getInstance().getOvlDevice(dpy)->getSupportedColorMode();
}

const DisplayData* DisplayManager::getDisplayData(uint64_t dpy, hwc2_config_t config)
{
    if (m_data[dpy].size() > config)
    {
        return m_data[dpy][config];
    }
    HWC_LOGE("Fail to get DisplayData, dpy(%" PRIu64 ") config(%d/%zu)", dpy, config,
             m_data[dpy].size());
    return nullptr;
}

unsigned int DisplayManager::getNumberPluginDisplay()
{
    return m_curr_disp_num;
}

void DisplayManager::checkHDMIService(uint64_t dpy, uint32_t type)
{
    HDMIDevice::getInstance().checkHdmiEdid(dpy, type);
    if (!HDMIDevice::getInstance().getHdmiServiceAvaliable())
    {
        HWC_LOGI("start get hdmi service loop...");
    }
    while (!HDMIDevice::getInstance().getHdmiServiceAvaliable())
    {
        usleep(16000);
        HDMIDevice::getInstance().checkHdmiEdid(dpy, type);
    }
}

void DisplayManager::updateVsyncThreadPeriod(uint64_t dpy, nsecs_t period){
    AutoMutex _l(m_vsyncs[dpy].lock);
    if (m_vsyncs[dpy].thread != NULL)
    {
        m_vsyncs[dpy].thread->updatePeriod(period);
    }
}

void DisplayManager::updateVsyncPeriodTimingChange(uint64_t dpy, int64_t applied_time,
        uint8_t refresh_required, int64_t refresh_time)
{
    if (m_listener != NULL)
    {
        m_listener->onVSyncPeriodTimingChange(dpy, applied_time, refresh_required, refresh_time);
    }
}

bool DisplayManager::checkHDMIRxLocked(void)
{
    if (HDMIRxDevice::getInstance().getHDMIRXLockStatus() == true)
        return true;
    else
        return false;
}

uint32_t DisplayManager::getHDMIRxFrameRate(void)
{
    return HDMIRxDevice::getInstance().getHDMIRXVideoFrameRate();
}

void DisplayManager::setActiveConfig(uint64_t dpy, uint32_t config)
{
    m_active_config = config;
    HDMIDevice::getInstance().setActiveConfig(dpy, config);
}

hwc2_config_t DisplayManager::getActiveConfig(uint64_t dpy)
{
    return (dpy == HWC_DISPLAY_PRIMARY) ? m_active_config : 0;
}

void DisplayManager::setCurrentConnectorType(uint64_t dpy, uint32_t connector_type)
{
    BOUNDARY_CHECK_DPY(dpy);

    m_cur_connector_type[dpy] = connector_type;
}

uint32_t DisplayManager::getCurrentConnectorType(uint64_t dpy)
{
    BOUNDARY_CHECK_DPY(dpy);

    return m_cur_connector_type[dpy];
}

