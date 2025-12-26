#ifndef __HWC_HDMI_DEV_H__
#define __HWC_HDMI_DEV_H__

#include <queue>

#include "worker.h"
#include <vendor/mediatek/hardware/hdmi/1.2/IMtkHdmiCallback.h>
#include <vendor/mediatek/hardware/hdmi/1.3/IMtkHdmiService.h>

using android::hardware::hidl_array;
using vendor::mediatek::hardware::hdmi::V1_2::IMtkHdmiCallback;
using vendor::mediatek::hardware::hdmi::V1_3::IMtkHdmiService;
//using vendor::mediatek::hardware::hdmi::V1_0::Result;
using vendor::mediatek::hardware::hdmi::V1_0::EDID_t;
using vendor::mediatek::hardware::hdmi::V1_3::EDID_More_t;
using ::android::hardware::Return;using ::android::hardware::Void;

using namespace android;

/*
 * HDMI service resolution
 * from vendor/mediatek/proprietary/external/hdmi/MtkHdmiService.cpp
 */
#define HDMI_VIDEO_720x480i_60Hz 0
#define HDMI_VIDEO_720x576i_50Hz 1
#define RESOLUTION_720X480P_60HZ 2
#define RESOLUTION_720X576P_50HZ 3
#define RESOLUTION_1280X720P_60HZ 4
#define RESOLUTION_1280X720P_50HZ 5
#define RESOLUTION_1920X1080I_60HZ 6
#define RESOLUTION_1920X1080I_50HZ 7
#define RESOLUTION_1920X1080P_30HZ 8
#define RESOLUTION_1920X1080P_25HZ 9
#define RESOLUTION_1920X1080P_24HZ 10
#define RESOLUTION_1920X1080P_23HZ 11
#define RESOLUTION_1920X1080P_29HZ 12
#define RESOLUTION_1920X1080P_60HZ 13
#define RESOLUTION_1920X1080P_50HZ 14
#define RESOLUTION_1280X720P3D_60HZ 15
#define RESOLUTION_1280X720P3D_50HZ 16
#define RESOLUTION_1920X1080I3D_60HZ 17
#define RESOLUTION_1920X1080I3D_50HZ 18
#define RESOLUTION_1920X1080P3D_24HZ 19
#define RESOLUTION_1920X1080P3D_23HZ 20
#define RESOLUTION_3840X2160P23_976HZ 21
#define RESOLUTION_3840X2160P_24HZ 22
#define RESOLUTION_3840X2160P_25HZ 23
#define RESOLUTION_3840X2160P29_97HZ 24
#define RESOLUTION_3840X2160P_30HZ 25
#define RESOLUTION_4096X2161P_24HZ 26
#define RESOLUTION_3840X2160P_60HZ 27
#define RESOLUTION_3840X2160P_50HZ 28
#define RESOLUTION_4096X2161P_60HZ 29
#define RESOLUTION_4096X2161P_50HZ 30

/*
 * HDMI mode setting
 * from vendor/mediatek/proprietary/external/hdmi/MtkHdmiService.cpp
 */
#define VIDEO_RESOLUTION_MODE 0
#define ENABLE_HDMI_MODE      1
#define ENABLE_HDR_DV_MODE 2
#define SET_COLOR_FORMAT_MODE 3

#define MAX_HDR_MODE 3
#define MAX_PHY_DISP 4
#define HDR_DV_STATUS_INVALID 0xFF

#define SINK_480P (1 << 0)
#define SINK_720P60 (1 << 1)
#define SINK_1080I60 (1 << 2)
#define SINK_1080P60 (1 << 3)
#define SINK_480P_1440 (1 << 4)
#define SINK_480P_2880 (1 << 5)
#define SINK_480I (1 << 6)
#define SINK_480I_1440 (1 << 7)
#define SINK_480I_2880 (1 << 8)
#define SINK_1080P30 (1 << 9)
#define SINK_576P (1 << 10)
#define SINK_720P50 (1 << 11)
#define SINK_1080I50 (1 << 12)
#define SINK_1080P50 (1 << 13)
#define SINK_576P_1440 (1 << 14)
#define SINK_576P_2880 (1 << 15)
#define SINK_576I (1 << 16)
#define SINK_576I_1440 (1 << 17)
#define SINK_576I_2880 (1 << 18)
#define SINK_1080P25 (1 << 19)
#define SINK_1080P24 (1 << 20)
#define SINK_1080P23976 (1 << 21)
#define SINK_1080P2997 (1 << 22)

/*the 2160 mean 3840x2160 */
#define SINK_2160P_23_976HZ (1 << 0)
#define SINK_2160P_24HZ (1 << 1)
#define SINK_2160P_25HZ (1 << 2)
#define SINK_2160P_29_97HZ (1 << 3)
#define SINK_2160P_30HZ (1 << 4)
/*the 2161 mean 4096x2160 */
#define SINK_2161P_24HZ (1 << 5)
#define SINK_2161P_25HZ (1 << 6)
#define SINK_2161P_30HZ (1 << 7)
#define SINK_2160P_50HZ (1 << 8)
#define SINK_2160P_60HZ (1 << 9)
#define SINK_2161P_50HZ (1 << 10)
#define SINK_2161P_60HZ (1 << 11)

enum HDMI_COLOR_BIT
{
    RGB444_8bit = 0,
    RGB444_10bit,
    RGB444_12bit,
    RGB444_16bit,
    YCBCR444_8bit,
    YCBCR444_10bit,
    YCBCR444_12bit,
    YCBCR444_16bit,
    YCBCR422_8bit_NO_SUPPORT,
    YCBCR422_10bit_NO_SUPPORT,
    YCBCR422_12bit,
    YCBCR422_16bit_NO_SUPPORT,
    YCBCR420_8bit,
    YCBCR420_10bit,
    YCBCR420_12bit,
    YCBCR420_16bit,
    HDMI_COLOR_BIT_MAX,
};

enum HDMI_COLOR_FORMAT_ENUM
{
    HDMI_COLOR_FORMAT_RGB = 0,
    HDMI_COLOR_FORMAT_YCBCR444 = 2,
    HDMI_COLOR_FORMAT_YCBCR422,
    HDMI_COLOR_FORMAT_YCBCR420,
    HDMI_COLOR_FORMAT_MAX,
};

enum HDMI_COLOR_DEPTH_ENUM
{
    HDMI_COLOR_DEPTH_NO_DEEP = 1,
    HDMI_COLOR_DEPTH_10_BIT,
    HDMI_COLOR_DEPTH_12_BIT,
    HDMI_COLOR_DEPTH_16_BIT,
    HDMI_COLOR_DEPTH_MAX,
};

enum
{
    MODE_HANDLE_INVALID,
    SF_SET_ACTIVE_CONFIG,
    HDMI_SERVICE_SETTING,
};

enum HDMIRX_NOTIFY_T {
    HDMI_RX_PWR_5V_CHANGE = 0,
    HDMI_RX_TIMING_LOCK,
    HDMI_RX_TIMING_UNLOCK,
    HDMI_RX_AUD_LOCK,
    HDMI_RX_AUD_UNLOCK,
    HDMI_RX_ACP_PKT_CHANGE,
    HDMI_RX_AVI_INFO_CHANGE,
    HDMI_RX_AUD_INFO_CHANGE,
    HDMI_RX_HDR_INFO_CHANGE,
    HDMI_RX_EDID_CHANGE,
    HDMI_RX_HDCP_VERSION,
    HDMI_RX_PLUG_IN, /// 11
    HDMI_RX_PLUG_OUT, /// 12
};

inline void mapHdmiModeToConnectorMode(uint32_t hdmimode, uint32_t *modeinfo)
{
    switch(hdmimode)
    {
    case RESOLUTION_720X480P_60HZ:
        modeinfo[0] = 720;
        modeinfo[1] = 480;
        modeinfo[2] = 60;
        break;
    case RESOLUTION_720X576P_50HZ:
        modeinfo[0] = 720;
        modeinfo[1] = 576;
        modeinfo[2] = 50;
        break;
    case RESOLUTION_1280X720P_60HZ:
    case RESOLUTION_1280X720P3D_60HZ:
        modeinfo[0] = 1280;
        modeinfo[1] = 720;
        modeinfo[2] = 60;
        break;
    case RESOLUTION_1280X720P_50HZ:
    case RESOLUTION_1280X720P3D_50HZ:
        modeinfo[0] = 1280;
        modeinfo[1] = 720;
        modeinfo[2] = 50;
        break;
    case RESOLUTION_1920X1080P_60HZ:
        modeinfo[0] = 1920;
        modeinfo[1] = 1080;
        modeinfo[2] = 60;
        break;
    case RESOLUTION_1920X1080P_50HZ:
        modeinfo[0] = 1920;
        modeinfo[1] = 1080;
        modeinfo[2] = 50;
        break;
    case RESOLUTION_1920X1080P_30HZ:
        modeinfo[0] = 1920;
        modeinfo[1] = 1080;
        modeinfo[2] = 30;
        break;
    case RESOLUTION_1920X1080P_25HZ:
        modeinfo[0] = 1920;
        modeinfo[1] = 1080;
        modeinfo[2] = 25;
        break;
    case RESOLUTION_1920X1080P_24HZ:
    case RESOLUTION_1920X1080P3D_24HZ:
        modeinfo[0] = 1920;
        modeinfo[1] = 1080;
        modeinfo[2] = 24;
        break;
    case RESOLUTION_1920X1080P_23HZ:
    case RESOLUTION_1920X1080P3D_23HZ:
        modeinfo[0] = 1920;
        modeinfo[1] = 1080;
        modeinfo[2] = 23;
        break;
    case RESOLUTION_1920X1080P_29HZ:
        modeinfo[0] = 1920;
        modeinfo[1] = 1080;
        modeinfo[2] = 29;
        break;
    case RESOLUTION_3840X2160P_24HZ:
        modeinfo[0] = 3840;
        modeinfo[1] = 2160;
        modeinfo[2] = 24;
        break;
    case RESOLUTION_3840X2160P_25HZ:
        modeinfo[0] = 3840;
        modeinfo[1] = 2160;
        modeinfo[2] = 25;
        break;
    case RESOLUTION_3840X2160P_30HZ:
        modeinfo[0] = 3840;
        modeinfo[1] = 2160;
        modeinfo[2] = 30;
        break;
    case RESOLUTION_3840X2160P_60HZ:
        modeinfo[0] = 3840;
        modeinfo[1] = 2160;
        modeinfo[2] = 60;
        break;
    case RESOLUTION_3840X2160P_50HZ:
        modeinfo[0] = 3840;
        modeinfo[1] = 2160;
        modeinfo[2] = 50;
        break;
    default:
        modeinfo[0] = 0;
        modeinfo[1] = 0;
        modeinfo[2] = 0;
        break;
    }
}

inline int32_t mapHdmiColorspaceAndDepth(uint32_t color, uint32_t depth)
{
    int32_t color_bit = -1;
    int32_t fmt = -1;
    switch(color)
    {
        case HDMI_COLOR_FORMAT_RGB:
            color_bit = (int32_t)(0 + depth - 1);
            break;
        case HDMI_COLOR_FORMAT_YCBCR444:
            color_bit = (int32_t)(4 + depth - 1);
            break;
        case HDMI_COLOR_FORMAT_YCBCR422:
            color_bit = (int32_t)(8 + depth - 1);
            break;
        case HDMI_COLOR_FORMAT_YCBCR420:
            color_bit = (int32_t)(12 + depth - 1);
            break;
        default:
            break;
    }

    if (color_bit >= 0)
        fmt = 1 << color_bit;

    return fmt;
}

// ---------------------------------------------------------------------------
class MtkHdmiCallback : public IMtkHdmiCallback {
public:
    MtkHdmiCallback();
    ~MtkHdmiCallback();
    Return<void> onHdmiSettingsChange(int32_t mode, int32_t value);
};

struct HDMIModeConfig
{
public:
    uint32_t msg_type;
    uint32_t set_mode;
    uint32_t set_val;
    uint64_t mode_dpy;
    uint32_t mode_config;
    HDMIModeConfig() : msg_type(MODE_HANDLE_INVALID), set_mode(0), set_val(0), mode_dpy(0), mode_config(0){}
};

class ModeHandleThread : public HWCThread
{
public:
    ModeHandleThread();
    void sendSetActiveConfig(uint64_t dpy, uint32_t config);
    void sendSetHdmiSetting(uint64_t dpy, uint32_t mode, uint32_t val);
    void handleSetActiveConfig(HDMIModeConfig* modeConfig);
    void handleHdmiSetting(HDMIModeConfig* modeConfig);

private:
    virtual void onFirstRef();
    virtual bool threadLoop();
    uint32_t m_changed_hdr[MAX_PHY_DISP];
    uint32_t m_changed_fmt[MAX_PHY_DISP];
    uint32_t m_is_hdr_change[MAX_PHY_DISP];
    uint32_t m_is_fmt_change[MAX_PHY_DISP];

    // m_config_queue is an array which store the parameters of HDMI mode config
    std::queue<HDMIModeConfig>m_config_queue;
};

class HDMIDevice
{
public:
    static HDMIDevice& getInstance();
    HDMIDevice();
    ~HDMIDevice();

    // init hdmi service and callback
    void getHdmiService();
    void checkHdmiEdid(uint64_t dpy, uint32_t type);
    bool getHdmiServiceAvaliable() { return m_isHdmiServiceAvailable; }
    void setActiveConfig(uint64_t dpy, uint32_t config);
    void setHdmiSetting(uint64_t dpy, uint32_t mode, uint32_t val);

private:
    // hdmi service
    sp<IMtkHdmiService> m_hdmiServer;
    // hdmi service resolution change callback
    sp<MtkHdmiCallback> mCallback;
    // hdmi service avaliable or not
    bool m_isHdmiServiceAvailable;

    sp<ModeHandleThread> m_mode_handler;
};

class HDMIRxDevice
{
public:
    static HDMIRxDevice& getInstance();
    HDMIRxDevice();
    ~HDMIRxDevice();

    void setHDMIRXLockStatus(uint32_t state);
    bool getHDMIRXLockStatus(void);
    int setHDMIRXVideoInfo(void);
    uint32_t getHDMIRXVideoFrameRate(void);

struct HDMIRX_DEV_INFO {
    uint8_t hdmirx5v;
    bool hpd;
    uint32_t power_on;
    uint8_t state;
    uint8_t vid_locked;
    uint8_t aud_locked;
    uint8_t hdcp_version;
    uint8_t hdcp14_decrypted;
    uint8_t hdcp22_decrypted;
};

enum HDMIRX_CS {
    HDMI_CS_RGB = 0,
    HDMI_CS_YUV444,
    HDMI_CS_YUV422,
    HDMI_CS_YUV420
};

enum HdmiRxDP {
    HDMIRX_BIT_DEPTH_8_BIT = 0,
    HDMIRX_BIT_DEPTH_10_BIT,
    HDMIRX_BIT_DEPTH_12_BIT,
    HDMIRX_BIT_DEPTH_16_BIT
};

enum HdmiRxClrSpc {
    HDMI_RX_CLRSPC_UNKNOWN,
    HDMI_RX_CLRSPC_YC444_601,
    HDMI_RX_CLRSPC_YC422_601,
    HDMI_RX_CLRSPC_YC420_601,

    HDMI_RX_CLRSPC_YC444_709,
    HDMI_RX_CLRSPC_YC422_709,
    HDMI_RX_CLRSPC_YC420_709,

    HDMI_RX_CLRSPC_XVYC444_601,
    HDMI_RX_CLRSPC_XVYC422_601,
    HDMI_RX_CLRSPC_XVYC420_601,

    HDMI_RX_CLRSPC_XVYC444_709,
    HDMI_RX_CLRSPC_XVYC422_709,
    HDMI_RX_CLRSPC_XVYC420_709,

    HDMI_RX_CLRSPC_sYCC444_601,
    HDMI_RX_CLRSPC_sYCC422_601,
    HDMI_RX_CLRSPC_sYCC420_601,

    HDMI_RX_CLRSPC_Adobe_YCC444_601,
    HDMI_RX_CLRSPC_Adobe_YCC422_601,
    HDMI_RX_CLRSPC_Adobe_YCC420_601,

    HDMI_RX_CLRSPC_RGB,
    HDMI_RX_CLRSPC_Adobe_RGB,

    HDMI_RX_CLRSPC_BT_2020_RGB_non_const_luminous,
    HDMI_RX_CLRSPC_BT_2020_RGB_const_luminous,

    HDMI_RX_CLRSPC_BT_2020_YCC444_non_const_luminous,
    HDMI_RX_CLRSPC_BT_2020_YCC422_non_const_luminous,
    HDMI_RX_CLRSPC_BT_2020_YCC420_non_const_luminous,

    HDMI_RX_CLRSPC_BT_2020_YCC444_const_luminous,
    HDMI_RX_CLRSPC_BT_2020_YCC422_const_luminous,
    HDMI_RX_CLRSPC_BT_2020_YCC420_const_luminous
};

enum HdmiRxRange {
    HDMI_RX_RGB_FULL,
    HDMI_RX_RGB_LIMT,
    HDMI_RX_YCC_FULL,
    HDMI_RX_YCC_LIMT
};

struct HDMIRX_VID_PARA {
    enum HDMIRX_CS cs;
    enum HdmiRxDP dp;
    enum HdmiRxClrSpc HdmiClrSpc;
    enum HdmiRxRange HdmiRange;
    uint32_t htotal;
    uint32_t vtotal;
    uint32_t hactive;
    uint32_t vactive;
    uint32_t is_pscan;
    bool hdmi_mode;
    uint32_t frame_rate;
    uint32_t pixclk;
    uint32_t tmdsclk;
    bool is_40x;
    uint32_t id;
};

private:
    int m_fd;
    int getDeviceInfo(HDMIRX_DEV_INFO *dinfo);
    mutable Mutex m_lock_rx_signal;
    uint32_t m_hdmirx_lock_status;
    HDMIRX_DEV_INFO m_hdmirx_device_info;
    HDMIRX_VID_PARA m_hdmirx_video_info;
};
#endif
