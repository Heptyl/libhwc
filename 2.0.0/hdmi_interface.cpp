#define DEBUG_LOG_TAG "HDMIDevice"

#define HDMIRX_DEV_PATH "/dev/hdmirx"
#define HDMIRX_IOWR(num, dtype) _IOWR('H', num, dtype)
#define MTK_HDMIRX_VID_INFO HDMIRX_IOWR(1, HDMIRxDevice::HDMIRX_VID_PARA)
#define MTK_HDMIRX_DEV_INFO HDMIRX_IOWR(4, HDMIRxDevice::HDMIRX_DEV_INFO)

#include <sys/ioctl.h>
#include "hdmi_interface.h"
#include "utils/tools.h"
#include "hwc2.h"
#include "dev_interface.h"
#include "display.h"

MtkHdmiCallback::MtkHdmiCallback()
{
}

MtkHdmiCallback::~MtkHdmiCallback()
{
}

Return<void> MtkHdmiCallback::onHdmiSettingsChange(int32_t mode, int32_t value)
{
    uint32_t val  = 0;
    uint32_t disp = 0;
    uint64_t dpy  = 0;

    HWC_LOGI("%s: mode:%d, value: %d", __func__, mode, value);
    if (value < 0)
    {
        HWC_LOGE("%s: receive err setting value %d", __func__, value);
        return Void();
    }
    else
    {
        val = (uint32_t)value;
    }

    disp = (val & 0xF000) >> 12;
    dpy = disp;

    if (dpy != HWC_DISPLAY_EXTERNAL && mode == ENABLE_HDR_DV_MODE)
        return Void();

    val  = (val & 0x0FFF) >> 0;
    HDMIDevice::getInstance().setHdmiSetting(dpy, (uint32_t)mode, val);

    return Void();
}

// ---------------------------------------------------------------------------
ModeHandleThread::ModeHandleThread()
{
    for(int i = 0; i < MAX_PHY_DISP; i++)
        m_changed_hdr[i] = HDR_DV_STATUS_INVALID;
    memset(m_changed_fmt, 0, sizeof(m_changed_fmt));
    memset(m_is_hdr_change, 0, sizeof(m_is_hdr_change));
    memset(m_is_fmt_change, 0, sizeof(m_is_fmt_change));
    m_thread_name = std::string("ModeHandle");
    m_queue_name  = std::string("ModeHandleQueue");
}

void ModeHandleThread::onFirstRef()
{
    run(m_thread_name.c_str(), PRIORITY_URGENT_DISPLAY);
}

void ModeHandleThread::sendSetActiveConfig(uint64_t dpy, uint32_t config)
{
    AutoMutex l(m_lock);

    HDMIModeConfig modeConfig;
    modeConfig.msg_type    = SF_SET_ACTIVE_CONFIG;
    modeConfig.mode_dpy    = dpy;
    modeConfig.mode_config = config;

    m_config_queue.push(modeConfig);

    m_state       = HWC_THREAD_TRIGGER;
    sem_post(&m_event);
}

void ModeHandleThread::sendSetHdmiSetting(uint64_t dpy, uint32_t mode, uint32_t val)
{
    AutoMutex l(m_lock);

    if (dpy >= MAX_PHY_DISP)
    {
        HWC_LOGW("%s error dpy:%" PRIu64 " set, default set external display", __func__, dpy);
        dpy = HWC_DISPLAY_EXTERNAL;
    }

    HDMIModeConfig modeConfig;

    modeConfig.msg_type      = HDMI_SERVICE_SETTING;
    modeConfig.mode_dpy      = dpy;
    modeConfig.set_mode      = mode;
    modeConfig.set_val       = val;

    m_config_queue.push(modeConfig);

    m_state       = HWC_THREAD_TRIGGER;
    sem_post(&m_event);
}

bool ModeHandleThread::threadLoop()
{
    sem_wait(&m_event);

    while (1)
    {
        HDMIModeConfig config;
        {
            AutoMutex l(m_lock);

            if (m_config_queue.empty())
            {
                HWC_LOGI("job is empty");
                break;
            }
            config = m_config_queue.front();
            m_config_queue.pop();
        }

        if (config.msg_type == SF_SET_ACTIVE_CONFIG)
            handleSetActiveConfig(&config);

        if (config.msg_type == HDMI_SERVICE_SETTING)
            handleHdmiSetting(&config);
    }


    {
        AutoMutex l(m_lock);
        if (m_config_queue.empty())
        {
            m_state = HWC_THREAD_IDLE;
            m_condition.signal();
        }
    }

    return true;
}

void ModeHandleThread::handleSetActiveConfig(HDMIModeConfig* modeConfig)
{
    uint64_t dpy           = modeConfig->mode_dpy;
    uint32_t config        = modeConfig->mode_config;
    int32_t supported_mode = -1;
    uint32_t maxConfig     = 0;
    uint32_t modeinfo[4];
    uint32_t drm_id_cur_crtc = 0;
    uint32_t drm_id_connector = 0;

    sp<IOverlayDevice> ovlDevice = HWCMediator::getInstance().getOvlDevice(dpy);
    drm_id_connector = HWCMediator::getInstance().getHWCDisplay(dpy)->getDrmIdConnector();
    maxConfig = ovlDevice->getNumConfigs(dpy, drm_id_connector);

    if (config <= maxConfig)
    {
        sp<IOverlayDevice> ovlDevice = HWCMediator::getInstance().getOvlDevice(dpy);
        if (config < maxConfig)
        {
            modeinfo[0] = (uint32_t)ovlDevice->getWidth(dpy, drm_id_connector, (hwc2_config_t)config);
            modeinfo[1] = (uint32_t)ovlDevice->getHeight(dpy, drm_id_connector, (hwc2_config_t)config);
            modeinfo[2] = (uint32_t)ovlDevice->getRefresh(dpy, drm_id_connector, (hwc2_config_t)config);
        }
        else
        {
            modeinfo[0] = 1920;
            modeinfo[1] = 1080;
            modeinfo[2] = 60;
        }

        HWC_LOGI("%s %dx%d@%d", __func__, modeinfo[0], modeinfo[1], modeinfo[2]);
        drm_id_cur_crtc = HWCMediator::getInstance().getHWCDisplay(dpy)->getDrmIdCurCrtc();
        supported_mode = HWCMediator::getInstance().getOvlDevice(dpy)->getSpportedConnectorMode(dpy, drm_id_cur_crtc, modeinfo);
        if (supported_mode >= 0)
        {
            HWC_LOGI("%s: drm mode:%d, value: %dx%d", __func__, supported_mode, modeinfo[0], modeinfo[1]);

            DisplayManager::getInstance().setHdmiChangeState(dpy, true);

            DisplayManager::getInstance().hotplugExt(dpy, false, true);
            usleep(32000);

            HWC_LOGI("%s: connect hdmi", __func__);
            DisplayManager::getInstance().hotplugExt(dpy, true, true);

            DisplayManager::getInstance().setHdmiChangeState(dpy, false);
            HWC_LOGI("%s: resolution change end", __func__);
        }
        else
        {
            HWC_LOGW("%s: not support or same mode: %d, supported_mode %d", __func__, config, supported_mode);
        }
    }
    else
    {
        HWC_LOGE("%s err config %d", __FUNCTION__, config);
    }
}

void ModeHandleThread::handleHdmiSetting(HDMIModeConfig* modeConfig)
{
    int32_t supported_mode = -1;
    uint32_t hdmi_enable   = 0;
    uint32_t color         = 0;
    uint32_t depth         = 0;
    int32_t new_fmt        = -1;
    uint32_t modeinfo[4];

    uint64_t dpy  = modeConfig->mode_dpy;
    uint32_t val  = modeConfig->set_val;
    uint32_t mode = modeConfig->set_mode;
    uint32_t drm_id_cur_crtc = 0;

    switch(mode)
    {
        case VIDEO_RESOLUTION_MODE:
            mapHdmiModeToConnectorMode(val, modeinfo);
            drm_id_cur_crtc = HWCMediator::getInstance().getHWCDisplay(dpy)->getDrmIdCurCrtc();
            supported_mode = HWCMediator::getInstance().getOvlDevice(dpy)->getSpportedConnectorMode(dpy, drm_id_cur_crtc, modeinfo);

            if (supported_mode == -2 &&
                !DisplayManager::getInstance().getDisplayConnected(dpy))
            {
                supported_mode = 0;
            }

            if (supported_mode >= 0 || m_is_hdr_change[dpy] || m_is_fmt_change[dpy])
            {
                HWC_LOGI("%s: drm mode:%d, value: %dx%d", __func__, supported_mode, modeinfo[0], modeinfo[1]);

                DisplayManager::getInstance().setHdmiChangeState(dpy, true);

                if (DisplayManager::getInstance().getDisplayConnected(dpy))
                {
                    HWC_LOGI("%s: disconnect hdmi", __func__);

                    // avoid nofirfy surfaceflinger hotplug event twice in short time.
                    DisplayManager::getInstance().hotplugExt(dpy, false, false, false);
                }

                if (m_is_hdr_change[dpy])
                {
                    drm_id_cur_crtc = HWCMediator::getInstance().getHWCDisplay(dpy)->getDrmIdCurCrtc();
                    HWCMediator::getInstance().getOvlDevice(dpy)->setHDMIMode(dpy, drm_id_cur_crtc, ENABLE_HDR_DV_MODE, m_changed_hdr[dpy]);
                    HWC_LOGI("%s: hdr value change to %d", __func__, m_changed_hdr[dpy]);
                    m_is_hdr_change[dpy] = false;
                }

                if (m_is_fmt_change[dpy])
                {
                    drm_id_cur_crtc = HWCMediator::getInstance().getHWCDisplay(dpy)->getDrmIdCurCrtc();
                    HWCMediator::getInstance().getOvlDevice(dpy)->setHDMIMode(dpy, drm_id_cur_crtc, SET_COLOR_FORMAT_MODE, m_changed_fmt[dpy]);
                    HWC_LOGI("%s: color fmt value change to %d", __func__, m_changed_fmt[dpy]);
                    m_is_fmt_change[dpy] = false;
                }

                HWC_LOGI("%s: connect hdmi", __func__);
                DisplayManager::getInstance().hotplugExt(dpy, true);

                DisplayManager::getInstance().setHdmiChangeState(dpy, false);
                HWC_LOGI("%s: resolution change end", __func__);
            }
            else
            {
                HWC_LOGD("%s: not support resolution mode or same mode: %d supported_mode %d", __func__, val, supported_mode);
            }
            break;
        case ENABLE_HDMI_MODE:
            drm_id_cur_crtc = HWCMediator::getInstance().getHWCDisplay(dpy)->getDrmIdCurCrtc();
            HWCMediator::getInstance().getOvlDevice(dpy)->getHDMIMode(dpy, drm_id_cur_crtc, mode, &hdmi_enable);
            if (val == 0 && hdmi_enable)
            {
                DisplayManager::getInstance().setHdmiChangeState(dpy, true);
                DisplayManager::getInstance().hotplugExt(dpy, false);
                drm_id_cur_crtc = HWCMediator::getInstance().getHWCDisplay(dpy)->getDrmIdCurCrtc();
                HWCMediator::getInstance().getOvlDevice(dpy)->setHDMIMode(dpy, drm_id_cur_crtc, ENABLE_HDMI_MODE, 0);
                DisplayManager::getInstance().setHdmiChangeState(dpy, false);
                HWC_LOGI("%s: turn off hdmi", __func__);
            }
            else if (val == 1 && !hdmi_enable)
            {
                DisplayManager::getInstance().setHdmiChangeState(dpy, true);
                drm_id_cur_crtc = HWCMediator::getInstance().getHWCDisplay(dpy)->getDrmIdCurCrtc();
                HWCMediator::getInstance().getOvlDevice(dpy)->setHDMIMode(dpy, drm_id_cur_crtc, ENABLE_HDMI_MODE, 1);
                usleep(32000);
                DisplayManager::getInstance().hotplugExt(dpy, true);
                DisplayManager::getInstance().setHdmiChangeState(dpy, false);
                HWC_LOGI("%s: turn on hdmi", __func__);
            }
            else
            {
                HWC_LOGW("%s: not need ENABLE_HDMI_MODE state: %d, cur state:%d", __func__, val, hdmi_enable);
            }
            break;
        case ENABLE_HDR_DV_MODE:
            if (HWCMediator::getInstance().getOvlDevice(dpy)->getEthdrSupport(dpy))
            {
                if (val >= MAX_HDR_MODE)
                {
                    HWC_LOGE("receive unsupport hdmi hdr mode %d", val);
                }
                else if (m_changed_hdr[dpy] != val)
                {
                    m_changed_hdr[dpy] = val;
                    m_is_hdr_change[dpy] = true;
                }
            }
            else
            {
                m_is_hdr_change[dpy] = false;
            }
            break;
        case SET_COLOR_FORMAT_MODE:
            color = (val & 0xF0) >> 4;
            depth = (val & 0x0F) >> 0;
            new_fmt = mapHdmiColorspaceAndDepth(color, depth);

            if (new_fmt >= 0)
            {
                if((uint32_t)new_fmt != m_changed_fmt[dpy])
                {
                    m_changed_fmt[dpy] = (uint32_t)new_fmt;
                    m_is_fmt_change[dpy] = true;
                }
            }
            else
            {
                HWC_LOGE("receive error color fmt");
            }

            break;
        default:
            HWC_LOGW("%s: not support mode: %d", __func__, mode);
            break;
    }
}
// ---------------------------------------------------------------------------
HDMIDevice& HDMIDevice::getInstance()
{
    static HDMIDevice gInstance;
    return gInstance;
}

HDMIDevice::HDMIDevice()
    : m_isHdmiServiceAvailable(false)
{
    m_mode_handler = new ModeHandleThread();
}

HDMIDevice::~HDMIDevice()
{
    if (m_mode_handler != nullptr)
        m_mode_handler = nullptr;
}

void HDMIDevice::getHdmiService()
{
    if (!m_isHdmiServiceAvailable)
    {
        sp<IMtkHdmiService> service = IMtkHdmiService::tryGetService();

        if (service == nullptr)
            return;

        HWC_LOGI("end get hdmi service");
        m_hdmiServer = service;

        mCallback = new MtkHdmiCallback();
        m_hdmiServer->setHdmiSettingsCallback(mCallback);
        m_isHdmiServiceAvailable = true;
    }
}

void HDMIDevice::checkHdmiEdid(uint64_t dpy, uint32_t type)
{
    getHdmiService();

    uint32_t edid[10];
    uint32_t drm_id_cur_crtc = HWCMediator::getInstance().getHWCDisplay(dpy)->getDrmIdCurCrtc();
    HWCMediator::getInstance().getOvlDevice(dpy)->getConnectorEDID(dpy, drm_id_cur_crtc, edid);

    EDID_More_t res;
    res.edid[0] = edid[0];
    res.edid[1] = edid[1];
    res.edid[2] = edid[2];
    res.edid[3] = edid[3];
    res.edid[4] = edid[4];
    res.edid[5] = edid[5];
    res.edid[6] = edid[6];
    res.edid[7] = edid[7];
    res.edid[8] = edid[8];
    res.edid[9] = edid[9];
    res.edid[10] = (uint32_t)dpy;
    res.edid[11] = type;
    HWC_LOGI("getEDID %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d",
            res.edid[0], res.edid[1], res.edid[2], res.edid[3], res.edid[4],
            res.edid[5], res.edid[6], res.edid[7], res.edid[8], res.edid[9],
            res.edid[10], res.edid[11]);
    m_hdmiServer->setEDIDInfoMore(res);
}

void HDMIDevice::setActiveConfig(uint64_t dpy, uint32_t config)
{
    if (m_mode_handler)
        m_mode_handler->sendSetActiveConfig(dpy, config);
}

void HDMIDevice::setHdmiSetting(uint64_t dpy, uint32_t mode, uint32_t val)
{
    if (m_mode_handler)
        m_mode_handler->sendSetHdmiSetting(dpy, mode, val);
}

HDMIRxDevice& HDMIRxDevice::getInstance()
{
    static HDMIRxDevice gInstance;
    return gInstance;
}

HDMIRxDevice::HDMIRxDevice()
    : m_fd(-1)
    , m_hdmirx_lock_status(0)
{

    m_fd = open(HDMIRX_DEV_PATH, O_RDONLY);
    if (m_fd < 0)
    {
        HWC_LOGE("%s, failed to open hdmi_rx device[%s]: %d", __FUNCTION__, HDMIRX_DEV_PATH, m_fd);
    }

    memset(&m_hdmirx_device_info, 0, sizeof(m_hdmirx_device_info));
    memset(&m_hdmirx_video_info, 0, sizeof(m_hdmirx_video_info));
}

HDMIRxDevice::~HDMIRxDevice()
{
    if (m_fd >= 0)
    {
        close(m_fd);
        m_fd = -1;
    }
}

void HDMIRxDevice::setHDMIRXLockStatus(uint32_t state)
{
    AutoMutex l(m_lock_rx_signal);
    m_hdmirx_lock_status = state;

    if(state == HDMI_RX_TIMING_UNLOCK)
        memset(&m_hdmirx_video_info, 0, sizeof(m_hdmirx_video_info));
}

bool HDMIRxDevice::getHDMIRXLockStatus(void)
{
    AutoMutex l(m_lock_rx_signal);
    if (m_hdmirx_lock_status == HDMI_RX_TIMING_LOCK)
        return true;
    else if (m_hdmirx_lock_status == HDMI_RX_TIMING_UNLOCK)
        return false;
    else // update lock status once for boot with hdmirx cable
    {
        if (m_fd < 0) {
            HWC_LOGE("%s, open hdmi_rx %s fail %d", __FUNCTION__, HDMIRX_DEV_PATH, m_fd);
            return false;
        }

        int ret = ioctl(m_fd, MTK_HDMIRX_DEV_INFO, &m_hdmirx_device_info);
        if (ret < 0)
        {
            HWC_LOGE("get hdmi_rx device info fail %s", strerror(errno));
            return false;
        }

        if (m_hdmirx_device_info.vid_locked)
        {
            m_hdmirx_lock_status = HDMI_RX_TIMING_LOCK;
            return true;
        }
        else
        {
            m_hdmirx_lock_status = HDMI_RX_TIMING_UNLOCK;
            return false;
        }
    }
}

int HDMIRxDevice::setHDMIRXVideoInfo(void)
{
    int ret = 0;

    if (m_fd < 0)
    {
        HWC_LOGE("%s, failed to open hdmi_rx %s fail %d", __FUNCTION__, HDMIRX_DEV_PATH, m_fd);
        return m_fd;
    }

    // update video info by uevent: unlock event first , avi change later
    ret = ioctl(m_fd, MTK_HDMIRX_VID_INFO, &m_hdmirx_video_info);
    if (ret < 0)
    {
        HWC_LOGE("get hdmi_rx video info fail %s", strerror(errno));
    }

    return ret;
}

uint32_t HDMIRxDevice::getHDMIRXVideoFrameRate(void)
{
    if(m_hdmirx_video_info.frame_rate != 0)
        return m_hdmirx_video_info.frame_rate;
    else // ioctl device / video info once
    {

        if (m_fd < 0) {
            HWC_LOGE("%s, open hdmi_rx %s fail %d", __FUNCTION__, HDMIRX_DEV_PATH, m_fd);
            return 0;
        }

        // update video info once
        int ret = ioctl(m_fd, MTK_HDMIRX_VID_INFO, &m_hdmirx_video_info);
        if (ret < 0)
        {
            HWC_LOGE("get hdmi_rx video info fail %s", strerror(errno));
            return 0;
        }

        return m_hdmirx_video_info.frame_rate;
    }

    return 0;
}
