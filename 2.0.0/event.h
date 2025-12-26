#ifndef HWC_EVENT_H_
#define HWC_EVENT_H_

#include <utils/threads.h>
#include <list>
#include <queue>
#include <thread>
#include "worker.h"

using namespace android;

// ---------------------------------------------------------------------------

class VSyncThread : public HWCThread
{
public:
    VSyncThread(uint64_t dpy, uint32_t drm_id_crtc);
    virtual ~VSyncThread();

    // initialize() is used to check if hw vsync can be used
    void initialize(bool force_sw_vsync, nsecs_t refresh);

    // setEnabled() is used to notify if needing to listen vsync signal
    void setEnabled(bool enabled);

    // setLoopAgain() is used to force vsync thread loop again
    void setLoopAgain();

    // setProperty() is used for debug purpose
    void setProperty();

    // updatePeriod() is used to update the vsync period of fake vsync
    void updatePeriod(nsecs_t period);

private:
    virtual void onFirstRef() { }
    virtual bool threadLoop();

    mutable Mutex m_lock;
    Condition m_condition;

    // m_disp_id is used to identify which display
    uint64_t m_disp_id;
    uint32_t m_drm_id_cur_crtc; // will be updated when change crtc

    bool m_enabled;
    nsecs_t m_refresh;

    bool m_loop;

    bool m_fake_vsync;
    mutable nsecs_t m_prev_fake_vsync;

    long m_max_period_io;
    long m_max_period_req;
};

class UEventThread : public HWCThread
{
public:
    UEventThread();
    virtual ~UEventThread();

    // initialize() is used to check if hw vsync can be used
    void initialize();

    // setProperty() is used for debug purpose
    void setProperty();

private:
    virtual void onFirstRef() { }
    virtual bool threadLoop();

    void handleUevents(const char *buff, ssize_t len);

    int m_socket;
    bool *m_is_hotplug;

    enum DEBUG_HOTPLUG
    {
        FAKE_HDMI_NONE   = 0,
        FAKE_HDMI_PLUG   = 1,
        FAKE_HDMI_UNPLUG = 2,
    };

    int m_fake_hdmi;
    bool m_fake_hotplug;
    bool m_is_register_service;
};

class RefreshRequestThread : public HWCThread
{
public:
    RefreshRequestThread();
    virtual ~RefreshRequestThread();

    // initialize() is used to run refresh request thread
    void initialize();

    // setEnabled() is used to notify if needing to listen refresh signal
    void setEnabled(bool enable);

private:
    virtual void onFirstRef() {}
    virtual bool threadLoop();

    mutable Mutex m_lock;
    Condition m_condition;
    bool m_enabled;
};

class IdleThread : public HWCThread
{
public:
    IdleThread(uint64_t dpy);
    virtual ~IdleThread();

    // initialize() is used to run idlethread
    void initialize(nsecs_t refresh);

    // setEnabled() is used to notify if needing to detect idle
    void setEnabled(bool enable);

private:
    virtual void onFirstRef() {}
    virtual bool threadLoop();

    uint64_t m_dpy;

    mutable Mutex m_lock;
    Condition m_condition;

    bool m_enabled;
    nsecs_t m_wait_time;
};

class HWVSyncEstimator
{
public:
    static HWVSyncEstimator& getInstance();
    ~HWVSyncEstimator();
    void resetAvgVSyncPeriod(nsecs_t period);
    void pushPresentFence(const int& fd, const nsecs_t cur_period);
    void update();
    nsecs_t getNextHWVsync(nsecs_t cur);
private:
    HWVSyncEstimator();
    void updateLocked();

    mutable Mutex m_mutex;
    std::list<int> m_present_fence_queue;
    nsecs_t m_avg_period;
    nsecs_t m_cur_config_period;
    int m_sample_count;
    nsecs_t m_last_signaled_prenset_fence_time;

    size_t const m_history_size = 5;
};

class FenceDebugger : public HWCThread
{
public:
    FenceDebugger(std::string name, int mode, bool wait_log = false);
    virtual ~FenceDebugger();

    void initialize();

    void dupAndStoreFence(const int fd, const unsigned int fence_idx);

    enum
    {
        CHECK_DIFF_BIG = 1 << 0,
        WAIT_PERIODICALLY = 1 << 1,
    };

private:
    struct FenceInfo
    {
        int fd;
        unsigned int fence_idx;
    };

    virtual void onFirstRef() {}
    virtual bool threadLoop();

    mutable Mutex m_lock;
    std::list<FenceInfo> m_fence_queue;
    Condition m_condition;

    std::string m_name;

    uint64_t m_prev_signal_time = UINT64_MAX;

    int m_same_count = 0;

    int m_mode;

    bool m_wait_log = false;
};

class SwitchConfigMonitor
{
public:
    //SwitchConfigMonitor();
    static SwitchConfigMonitor& getInstance();
    virtual ~SwitchConfigMonitor();

    // start all thread
    void start();

    // stop all thread
    void stop();

    // request monitorActiveThread to check the assigned index whether HWComposer package the
    // configuration on time
    void setCheckPoint(uint64_t dpy, bool need_check, int64_t index, nsecs_t deadline);

    // request monitorAppliedThread to check the assigned index whether its present fence is
    // signal or not
    void monitorAppliedConfig(uint64_t dpy, int64_t index, nsecs_t applied_time,
            nsecs_t target_time, hwc2_config_t config, nsecs_t period,
            unsigned int present_fence_index, int present_fence_fd);

private:
    SwitchConfigMonitor();

    // start monitorActiveThread
    void startActiveThread();
    // stop monitorActiveThread
    void stopActiveThread();

    // start monitorAppliedThread
    void startAppliedThread();
    // stop monitorAppliedThread
    void stopAppliedThread();

    // use to monitor the configuration whether HWComposer use it or not
    void monitorActiveThread();

    // use to monitor the configuration whether display driver apply it
    void monitorAppliedThread();

private:
    struct ActiveConfigInfo
    {
        bool need_check = false;
        int64_t index = -1;
        nsecs_t deadline = -1;
    };
    std::mutex m_lock_active;
    std::condition_variable m_condition_active;
    std::thread m_active_thread;
    bool m_stop_active;
    std::map<uint64_t, ActiveConfigInfo> m_active_map;

    struct AppliedConfigInfo
    {
        int64_t index = -1;
        hwc2_config_t config = 0;
        nsecs_t applied_time = -1;
        nsecs_t target_time = -1;
        nsecs_t period = -1;
        unsigned int present_fence_index = 0;
        int present_fence_fd = -1;
    };
    typedef std::queue<AppliedConfigInfo> AppliedConfigQueue;
    std::mutex m_lock_applied;
    std::condition_variable m_condition_applied;
    std::thread m_applied_thread;
    bool m_stop_applied;
    std::map<uint64_t, AppliedConfigQueue> m_applied_map;
};

#ifdef MTK_DRM_PIXEL_SHIFT

#define PIXEL_SHIFT_THREAD_DELAY 3000 // ms

class PixelShiftThread : public HWCThread
{
public:
    PixelShiftThread();
    virtual ~PixelShiftThread();

    // start the loop
    void start();

private:

    // android property name for pixel shift control
    const char *propName = "persist.vendor.mtk.drm.pixel_shift";

    // display ids
    std::vector<uint64_t> displays;

    // name of the display, ex. primary, external ...etc.
    const char **dispName;

    // pixel shift user configurations
    struct mtk_pixel_shift_user_config *cfg;

    // read all pixel shift related android properties
    bool readProperties(uint64_t disp);

    // read and parse the property, reassign it if out of boundary
    template<typename T>
    bool readProperty(std::string prop, int min, int max, T &container);

    virtual bool threadLoop();
};
#endif // MTK_DRM_PIXEL_SHIFT

#endif // HWC_EVENT_H_
