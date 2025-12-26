#ifndef HWC_DISPLAY_H_
#define HWC_DISPLAY_H_

#include <hardware/hwcomposer2.h>
#include <composer_ext_intf/device_interface.h>

#include <atomic>

#include <utils/Vector.h>
#include "hwc_priv.h"

#include "event.h"
#include "ged/ged.h"
#include "hwc_ui/Rect.h"


#define FAKE_DISPLAY -30

using namespace android;
using hwc::Rect;

struct dump_buff;
class SessionInfo;

#define DESCRIPTOR_HEAD_SZ 5
#define DESCRIPTOR_DATA_SZ 13
#define DESCRIPTOR_DATA_TYPE_IDX 3
#define EDID_1_3_SZ 128

enum TYPE_OF_DESCRIPTOR
{
    DISPLAY_NAME = 0xfc,
    ASCII_TEXT = 0xfe,
    SERIAL_NUMBER = 0xff,
};

// ---------------------------------------------------------------------------
struct Descriptor
{
    uint8_t m_header[DESCRIPTOR_HEAD_SZ] = {0};
    uint8_t m_data[DESCRIPTOR_DATA_SZ] = {0};
};

/*
*   This struct follow edid spec.
*   Please do not modify any member unless spec change
*/
struct EDID_DATA
{
    /*------------------------------------------------*/
    // header total 8 byte
    uint8_t m_header[8] = {0x00, 0xFF, 0xFF, 0xFF,
                           0xFF, 0xFF, 0xFF, 0x00};
    /*------------------------------------------------*/
    // vendor and product id, total 10 byte
    uint16_t m_product_eisa_id = 0; //aka manufacturerId in SF
    uint16_t m_product_id = 0;
    uint32_t m_product_serial = 0;
    uint8_t  m_product_week = 0;
    uint8_t  m_product_year = 0;
    /*------------------------------------------------*/
    // Descriptor, total 36 byte
    // version info, total 2 byte
    // This part SF dose not parse
    uint8_t m_ver_rev[2] = {0x01, 0x03};
    uint8_t m_disp_param_feat[5] = {0};
    uint8_t m_color_chara[10] = {0};
    uint8_t m_estab_time[3] = {0};
    uint8_t m_stand_time_id[16] = {0};
    /*------------------------------------------------*/
    // This part compose by four parts,
    // every part has 18 byte and total 72 byte
    Descriptor m_descriptor[4];
    /*------------------------------------------------*/
    // we don't set extend edid for now, total one byte
    uint8_t m_ext_flag = 0;
    /*------------------------------------------------*/
    // make sure the final byte of
    // the sum of above value + checksum is 0;
    // 0x10010 + 0x10(checksum)
    uint8_t m_checksum = 0;
};

class EDID_1_3
{
//ver 1.3
public:
    EDID_DATA m_edid_data;

    EDID_1_3()
        : m_is_valid(false)
        , m_size(0)
    {
    };

    void startEdid()
    {
        memset(m_edid_data.m_header ,0xff, sizeof(m_edid_data.m_header));
        m_edid_data.m_header[0] = 0x00;
        m_edid_data.m_header[7] = 0x00;
        m_edid_data.m_ver_rev[0] = 0x01;
        m_edid_data.m_ver_rev[1] = 0x03;
    };

    void genEdid(const char* name);

    void setProductInfo(uint16_t eisa, uint16_t p_code, uint32_t serial, uint8_t week, uint8_t year)
    {
        m_edid_data.m_product_eisa_id = eisa;
        m_edid_data.m_product_id = p_code;
        m_edid_data.m_product_serial = serial;
        m_edid_data.m_product_week = week;
        m_edid_data.m_product_year = year;
    };

    void genDescriptor(Descriptor& desc, enum TYPE_OF_DESCRIPTOR type, const char* input, uint8_t size)
    {
        switch (type)
        {
            // we only compose SF refence val;
            case DISPLAY_NAME: /*display name*/
            case ASCII_TEXT: /*ascii Text*/
            case SERIAL_NUMBER: /*serial number*/
                memset(desc.m_header, 0, sizeof(uint8_t) * DESCRIPTOR_HEAD_SZ);
                desc.m_header[DESCRIPTOR_DATA_TYPE_IDX] = DISPLAY_NAME;
                composeEdidText(desc, input, size);
                break;
        }
    };

    void composeEdidText(Descriptor& desc, const char* in, uint8_t size)
    {
        // use 0x20 fill for empty m_data by spec definition
        memset(desc.m_data, 0x20, sizeof(uint8_t) * DESCRIPTOR_DATA_SZ);

        if (size == 0)
        {
            return;
        }

        if (size < sizeof(desc.m_data))
        {
            // write value in m_data
            memcpy(desc.m_data, in, size -1);
            // use 0x0A for end char by spec definition
            desc.m_data[size-1] = 0x0A;
        }
        else
        {
            // write value in m_data
            memcpy(desc.m_data, in, DESCRIPTOR_DATA_SZ-1);
            // use 0x0A for end char
            desc.m_data[DESCRIPTOR_DATA_SZ-1] = 0x0A;
        }
    }

    void updateCheckSum();
    bool m_is_valid = false;
    uint32_t m_size = 0;
    uint64_t port = 0;
};

struct DisplayData
{
    int width;
    int height;
    uint32_t format;
    float xdpi;
    float ydpi;
    int density;
    nsecs_t refresh;
    bool has_vsync;
    bool connected;
    bool secure;
    int32_t group;

    // hwrotation is physical display rotation
    uint32_t hwrotation;

    // pixels is the area of the display
    uint32_t pixels;

    int subtype;

    float aspect_portrait;
    float aspect_landscape;
    Rect mir_portrait;
    Rect mir_landscape;

    uint64_t vsync_source;

    // DispatchThread starts jobs when HWC receives VSync
    bool trigger_by_vsync;

    // supporting hdcp version = minimum(dongle, phone sw)
    uint32_t hdcp_version;
};

class DisplayManager
{
public:
    static DisplayManager& getInstance();

    ~DisplayManager();

    enum
    {
        HWC_DISPLAY_DYNAMIC_DONT_USE = HWC_DISPLAY_EXTERNAL_1 + 1,
        MAX_DISPLAYS,
    };

    enum DISP_QUERY_TYPE
    {
        DISP_CURRENT_NUM  = 0x00000001,
    };

    // dump() for debug prupose
    void dump(struct dump_buff* log);

    void initInternal(uint64_t dpy, uint32_t drm_id_crtc);
    void initPrimaryExternal(uint64_t dpy, uint32_t drm_id_crtc);

    void resentCallback();

    struct EventListener : public virtual RefBase
    {
        // is called to notify vsync signal
        virtual void onVSync(uint64_t dpy, nsecs_t timestamp, bool enabled) = 0;

        // onPlugIn() is called to notify a display is plugged
        virtual void onPlugIn(uint64_t dpy, bool is_internal, uint32_t width = 0, uint32_t height = 0) = 0;

        // onPlugOut() is called to notify a display is unplugged
        virtual void onPlugOut(uint64_t dpy) = 0;

        // onHotPlug() is called to notify external display hotplug event
        virtual void onHotPlugExt(uint64_t dpy, int connected) = 0;

        // onRefresh() is called to notify a display to refresh
        virtual void onRefresh(uint64_t dpy) = 0;

        virtual void onRefresh(uint64_t dpy, unsigned int type) = 0;

        // onVSyncPeriodTimingChange() is called to nofiy the client that the previously reported
        // timing for vsync period change has been updated.
        virtual void onVSyncPeriodTimingChange(uint64_t dpy, int64_t applied_time,
                uint8_t refresh_required, int64_t refresh_time) = 0;

        // onSeamlessPossible() is called to notify seamless may be possible now
        virtual void onSeamlessPossible(uint64_t dpy) = 0;
    };

    // setListener() is used for client to register listener to get event
    void setListener(const sp<EventListener>& listener);

    inline sp<EventListener> getListener() const {return m_listener;}
    // requestVSync() is used for client to request vsync signal
    void requestVSync(uint64_t dpy, bool enabled);

    // requestNextVSync() is used by HWCDispatcher to request next vsync
    void requestNextVSync(uint64_t dpy);

    // vsync() is callback by vsync thread
    void vsync(uint64_t dpy, nsecs_t timestamp, bool enabled);

    // hotplugExt() is called to insert or remove extrenal display
    void hotplugExt(uint64_t dpy, bool connected, bool fake = false, bool notify = true);

    void hotplugExtOut();

    // hotplugVir() is called to insert or remove virtual display
    void hotplugVir(
        const uint64_t& dpy, const bool& connected, const uint32_t& width,
        const uint32_t& height, const unsigned int& format);

    // is called to refresh display
    void refreshForDisplay(uint64_t dpy, unsigned int type);

    // for display self refresh
    void refreshForDriver(uint64_t dpy, unsigned int type);

    // setDisplayData() is called to init display data in DisplayManager
    void setDisplayDataForPhy(uint64_t dpy, uint32_t drm_id_crtc, uint32_t drm_id_connector);
    void setExtraDisplayDataForPhy(uint64_t dpy, uint32_t drm_id_crtc, uint32_t drm_id_connector, bool is_internal);
    void setDisplayDataForVir(const uint64_t& dpy,
                              const uint32_t& width, const uint32_t& height,
                              const unsigned int& format);

    // setPowerMode() notifies which display's power mode
    void setPowerMode(uint64_t dpy, int mode);

    // getFakeDispNum() gets amount of fake external displays
    unsigned int getFakeDispNum() { return m_fake_disp_num; }

    // isAllDisplayOff() checks all displays whether they are in the suspend mode
    bool isAllDisplayOff();

    // setDisplayPowerState() sets the power state of specific display
    void setDisplayPowerState(uint64_t dpy, int state);
    int getDisplayPowerState(const uint64_t& dpy);

    void setHdmiChangeState(uint64_t dpy, bool state)
    {
        m_change_state[dpy] = state;
    }
    bool getHdmiChangeState(uint64_t dpy)
    {
        return m_change_state[dpy];
    }
    void checkHDMIService(uint64_t dpy, uint32_t type);
    bool checkHDMIRxLocked(void);
    uint32_t getHDMIRxFrameRate(void);

    enum
    {
        WAKELOCK_TIMER_PAUSE,
        WAKELOCK_TIMER_START,
        WAKELOCK_TIMER_PLAYOFF,
    };

    // setWakelockTimerState() used to control the watch dog of wakelock
    void setWakelockTimerState(int state);

    // m_data is the detailed information for each display device
    const DisplayData* getDisplayData(uint64_t dpy, hwc2_config_t config = 0);

    // m_profile_level is used for profiling purpose
    static int m_profile_level;

    // accesor of m_video_hdcp
    uint32_t getVideoHdcp() const;
    void setVideoHdcp(const uint32_t&);

    bool checkIsWfd(uint64_t dpy);
    void notifyHotplugInDone(uint64_t dpy);
    void notifyHotplugOutDone(uint64_t dpy);

    int32_t getSupportedColorMode(uint64_t dpy);

    // return the number of plug-in display
    unsigned int getNumberPluginDisplay();

    bool getDisplayConnected(uint64_t dpy) { return m_display_connected[dpy]; }

    // update the period of VSyncThread
    void updateVsyncThreadPeriod(uint64_t dpy, nsecs_t period);

    // update the reported timeing of vsync
    void updateVsyncPeriodTimingChange(uint64_t dpy, int64_t applied_time,
            uint8_t refresh_required, int64_t refresh_time);

    int getDeviceId(uint64_t dpy, uint8_t* out_port, uint8_t* data, uint32_t* out_data_size);

    int getDeviceIdSize(uint64_t dpy, uint32_t* sz);

    void genDisplayIdForDisplay(uint64_t dpy);

    void setActiveConfig(uint64_t dpy, uint32_t config);
    hwc2_config_t getActiveConfig(uint64_t dpy);

    void setCurrentConnectorType(uint64_t dpy, uint32_t connector_type);

    uint32_t getCurrentConnectorType(uint64_t dpy);

private:
    DisplayManager();

    enum
    {
        DISP_PLUG_NONE       = 0,
        DISP_PLUG_CONNECT    = 1,
        DISP_PLUG_DISCONNECT = 2,
    };

    // setMirrorRect() is used to calculate valid region for mirror mode
    void setMirrorRegion(uint64_t dpy);

    // hotplugPost() is used to do post jobs after insert/remove display
    void hotplugPost(uint64_t dpy, bool connected, int state, bool print_info = true);

    // createVsyncThread() is used to create vsync thread
    void createVsyncThread(uint64_t dpy, uint32_t drm_id_crtc);

    // destroyVsyncThread() is used to destroy vsync thread
    void destroyVsyncThread(uint64_t dpy);

    // printDisplayInfo() used to print out display data
    void printDisplayInfo(uint64_t dpy);

    // getPhysicalPanelSize() used to get the correct physical size of panel
    void getPhysicalPanelSize(unsigned int *width, unsigned int *height, SessionInfo &info);

    std::vector<DisplayData *> m_data[DisplayManager::MAX_DISPLAYS];

    // for getDisplayIdentificationData
    class EDID_1_3 m_edid[DisplayManager::MAX_DISPLAYS];

    // m_curr_disp_num is current amount of displays
    std::atomic_uint m_curr_disp_num;

    // amount of fake external displays
    unsigned int m_fake_disp_num;

    // m_listener is used for listen vsync event
    sp<EventListener> m_listener;

    mutable Mutex m_power_lock;

    // a wrapper class of VSyncThread
    struct HwcVSyncSource
    {
        HwcVSyncSource()
            :thread(NULL)
        {}

        // a thread to receive VSync from display driver
        sp<VSyncThread> thread;

        // Because device plug-out and vsync requests happened on different threads
        // add a lock to resolve the race condition
        mutable Mutex lock;
    };

    // a list of HwcVSyncSource
    // Each item is responsible for vsync receiving of each display device
    HwcVSyncSource m_vsyncs[DisplayManager::MAX_DISPLAYS];

    // for display state lock
    mutable Mutex m_state_lock;

    // m_display_power_state is used to stored power state of all display
    bool m_display_power_state[MAX_DISPLAYS];

    // hdcp version required by video provider
    mutable RWLock m_video_hdcp_rwlock;
    uint32_t m_video_hdcp;

    mutable Mutex m_uevent_lock[MAX_DISPLAYS];
    Condition m_condition[MAX_DISPLAYS];
    bool m_ext_active[MAX_DISPLAYS];

    GED_LOG_HANDLE m_ged_log_handle;

    bool m_change_state[MAX_DISPLAYS];
    hwc2_config_t m_active_config;

    std::atomic_bool m_display_connected[MAX_DISPLAYS];

    uint32_t m_cur_connector_type[MAX_DISPLAYS];

public:
    // dynamic switch :vds 1th job must wait primay 1th job atomic commit done
    void display_set_commit_done_state(uint64_t dpy,bool state = false) { m_first_commit_done[dpy].store(state); }
    bool display_get_commit_done_state(uint64_t dpy) { return m_first_commit_done[dpy]; }
    void display_set_primary_power(int state) { m_primay_display_power.store(state); }
    bool primary_is_power_off() { return (m_primay_display_power == HWC2_POWER_MODE_OFF); }

private:
    std::atomic_bool m_first_commit_done[DisplayManager::MAX_DISPLAYS];
    std::atomic_int m_primay_display_power;

public:
    void setUsage(uint64_t dpy, ComposerExt::DisplayUsage usage) { m_usage[dpy] = usage; }
    ComposerExt::DisplayUsage getUsage(uint64_t dpy) const { return m_usage[dpy]; }

private:
    ComposerExt::DisplayUsage m_usage[DisplayManager::MAX_DISPLAYS];
};

#endif // HWC_DISPLAY_H_
