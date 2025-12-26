#ifndef HWC_DISPATCHER_H_
#define HWC_DISPATCHER_H_

#include <bitset>
#include <vector>
#include <utils/threads.h>
#include <utils/SortedVector.h>

#include "hwc_priv.h"

#include "utils/tools.h"

#include "display.h"
#include "worker.h"
#include "composer.h"
#include "sync.h"
#include "dev_interface.h"
#include "color.h"
#include "overlay.h"
#include "queue.h"
#include "vsync_listener.h"
#include <hwc_common/pool.h>

using namespace android;

struct dump_buff;
class DispatchThread;
class HWCDisplay;
class HWCLayer;
class DisplayBufferQueue;
class OverlayEngine;
// ---------------------------------------------------------------------------

// HWLayer::type values
enum {
    HWC_LAYER_TYPE_INVALID      = 0,
    HWC_LAYER_TYPE_FBT          = 1,
    HWC_LAYER_TYPE_UI           = 2,
    HWC_LAYER_TYPE_MM           = 3,
    HWC_LAYER_TYPE_DIM          = 4,
    HWC_LAYER_TYPE_CURSOR       = 5,
    HWC_LAYER_TYPE_GLAI         = 6,
    HWC_LAYER_TYPE_NONE         = 8,
    HWC_LAYER_TYPE_WORMHOLE     = 9,
    HWC_LAYER_TYPE_IGNORE       = 14,
};

enum {
    HW_LAYER_DIRTY_NONE = 0,
    HW_LAYER_DIRTY_BUFFER = 1 << 0,
    HW_LAYER_DIRTY_DISPATCHER = 1 << 1,
    HW_LAYER_DIRTY_HWC_LAYER_STATE = 1 << 2,
    HW_LAYER_DIRTY_HWC_LAYER_METADATA = 1 << 3,
};

enum
{
    HWC_LAYER_HDR_NONE = 0,
    HWC_LAYER_HDR_COMMON = 1,
    HWC_LAYER_HDR_VIDEO = 2,
    HWC_LAYER_HDR_GAME = 3,
    HWC_LAYER_HDR_CAMERA_PREVIEW = 4,
};

enum {
    HWC_DISPACHERJOB_VALID = 0,
    HWC_DISPACHERJOB_INVALID_DISPLAY = 1,
    HWC_DISPACHERJOB_INVALID_WORKERS = 2,
    HWC_DISPACHERJOB_INVALID_OVLENGINE = 3,
    HWC_DISPACHERJOB_INVALID_DROPJOB = 4,
    HWC_DISPACHERJOB_INVALID_JOB = 5,
};

inline const char* getMMLayerString(const int32_t& hwlayer_caps)
{
    if (hwlayer_caps & HWC_MML_DISP_DIRECT_LINK_LAYER)
        return "MML_DL";
    if (hwlayer_caps & HWC_MML_DISP_DIRECT_DECOUPLE_LAYER)
        return "MML_IR";
    if (hwlayer_caps & HWC_MML_DISP_DECOUPLE_LAYER)
        return "MML_DC";
    if (hwlayer_caps & HWC_MML_DISP_MDP_LAYER)
        return "MML_MM";
    return "MM";
}

inline const char* getHWLayerString(const int32_t& hwlayer_type, const int32_t& hwlayer_caps = 0)
{
    switch (hwlayer_type)
    {
        case HWC_LAYER_TYPE_INVALID:
            return "INV";

        case HWC_LAYER_TYPE_FBT:
            return "FBT";

        case HWC_LAYER_TYPE_UI:
            return "UI";

        case HWC_LAYER_TYPE_MM:
            return getMMLayerString(hwlayer_caps);

        case HWC_LAYER_TYPE_DIM:
            return "DIM";

        case HWC_LAYER_TYPE_CURSOR:
            return "CUR";

        case HWC_LAYER_TYPE_GLAI:
            return "GLAI";

        case HWC_LAYER_TYPE_NONE:
            return "NON";

        case HWC_LAYER_TYPE_WORMHOLE:
            return "HOLE";

        case HWC_LAYER_TYPE_IGNORE:
            return "IGG";

        default:
            HWC_LOGE("unknown getHwlayerType:%d", hwlayer_type);
            return "UNKNOWN";
    }
}

inline int32_t getHWLayerType(const char* hwlayer_string)
{
    if      (strcmp(hwlayer_string, "INV"  ) == 0) return HWC_LAYER_TYPE_INVALID;
    else if (strcmp(hwlayer_string, "FBT"  ) == 0) return HWC_LAYER_TYPE_FBT;
    else if (strcmp(hwlayer_string, "UI"   ) == 0) return HWC_LAYER_TYPE_UI;
    else if (strcmp(hwlayer_string, "MM"   ) == 0) return HWC_LAYER_TYPE_MM;
    else if (strcmp(hwlayer_string, "DIM"  ) == 0) return HWC_LAYER_TYPE_DIM;
    else if (strcmp(hwlayer_string, "CUR"  ) == 0) return HWC_LAYER_TYPE_CURSOR;
    else if (strcmp(hwlayer_string, "GLAI" ) == 0) return HWC_LAYER_TYPE_GLAI;
    else if (strcmp(hwlayer_string, "NON"  ) == 0) return HWC_LAYER_TYPE_NONE;
    else if (strcmp(hwlayer_string, "HOLE" ) == 0) return HWC_LAYER_TYPE_WORMHOLE;
    else if (strcmp(hwlayer_string, "IGG"  ) == 0) return HWC_LAYER_TYPE_IGNORE;
    else HWC_LOGE("unknown getHWLayerString:%s", hwlayer_string);
    return HWC_LAYER_TYPE_NONE;
}

// DispatcherJob::post_state values
enum HWC_POST_STATE
{
    HWC_POST_INVALID        = 0x0000,
    HWC_POST_OUTBUF_DISABLE = 0x0010,
    HWC_POST_OUTBUF_ENABLE  = 0x0011,
    HWC_POST_INPUT_NOTDIRTY = 0x0100,
    HWC_POST_INPUT_DIRTY    = 0x0101,
    HWC_POST_MIRROR         = 0x1001,

    HWC_POST_CONTINUE_MASK  = 0x0001,
};

enum {
    HWC_MIRROR_SOURCE_INVALID = -1,
};

enum {
    HWC_SEQUENCE_INVALID = 0,
};

// To store the information abort hrt
// These information will pass to display driver
struct HrtLayerConfig
{
    HrtLayerConfig()
        : ovl_id(0)
        , ext_sel_layer(-1)
    { }

    uint32_t ovl_id;

    int ext_sel_layer;
};

// to store information about cache layer is hit or not.
// These infomation are usefull in cache algorithm.
struct LayerListInfo
{
    LayerListInfo()
        : hwc_gles_head(-1)
        , hwc_gles_tail(-1)
        , gles_head(-1)
        , gles_tail(-1)
        , max_overlap_layer_num(-1)
        , hrt_config_list(nullptr)
        , hrt_weight(0)
        , hrt_idx(0)
    { }

    // GLES composition range judged by HWC
    int hwc_gles_head;
    int hwc_gles_tail;

    // gles layer start
    int gles_head;

    // gles layer end
    int gles_tail;

    // overlapping layer num for HRT
    int max_overlap_layer_num;

    // store the query info of layer from driver
    // size of HrtLayerConfig* is equal to size of layer list
    HrtLayerConfig* hrt_config_list;

    // for intraframe DVFS
    uint32_t hrt_weight;

    // for intraframe DVFS
    uint32_t hrt_idx;
};

// HWLayer is used to store information of layers selected
// to be processed by hardware composer
struct HWLayer
{
    HWLayer();
    void resetData();

    // used to determine if this layer is enabled
    bool enable;

    // hwc_layer index in hwc_display_contents_1
    unsigned int index;

    // identify if layer should be handled by UI or MM path
    int type;

    // identify if layer has dirty pixels
    bool dirty;
    // all dirty reason should be marked
    int dirty_reason;

    union
    {
        // information of UI layer
        struct
        {
#ifdef MTK_HWC_PROFILING
            // amount of layers handled by GLES
            int fbt_input_layers;

            // bytes of layers handled by GLES
            int fbt_input_bytes;
#endif

            // used by fbt of fake display
            // TODO: add implementation of this debug feature
            int fake_ovl_id;
        };

        // information of MM layer
        struct
        {
            // used to judge the job id for async mdp
            uint32_t mdp_job_id;

            // used to specify the destination ROI written by MDP
            Rect mdp_dst_roi = {};

            bool mdp_skip_invalidate;
        };
    };

    Rect glai_dst_roi;
    int glai_agent_id;

    // index of release fence from display driver
    unsigned int fence_index;

    // smart layer id, this number is queryed from driver
    // need to pass this id when hwc set layeys
    int ext_sel_layer;

    // light version of hwc_layer_1
    LightHwcLayer1 layer = {};

    hwc_rect_t surface_damage_rect[MAX_DIRTY_RECT_CNT] = {};

    // private handle information
    PrivateHandle priv_handle;

    // layer id from HWCLayer
    uint64_t hwc2_layer_id;

    int32_t layer_caps;

    // solid color
    uint32_t layer_color;

    int32_t dataspace;

    // fb_id is for DRM buffer
    uint32_t fb_id;

    // game pq
    bool need_pq;

    // ai pq
    bool is_ai_pq;

    sp<HWCLayer> hwc_layer;

    // hold a strong pointer on queue for this job life cycle
    sp<DisplayBufferQueue> queue;

    OverlayPortParam ovl_port_param;

    // MDP output buffers
    bool mdp_output_compressed;
    uint32_t mdp_output_format;
    int32_t mdp_output_dataspace;

    // HDR metadata
    std::vector<int32_t> hdr_static_metadata_keys;
    std::vector<float> hdr_static_metadata_values;
    std::vector<uint8_t> hdr_dynamic_metadata;

    // HDR scenario
    int32_t hdr_scenario;

    // the debug type of this layer
    uint32_t debug_type;

    // used to determine if this layer is blit.
    bool is_do_blit;
};

// HWBuffer is used to store buffer information of
// 1. virtual display
// 2. mirror source
struct HWBuffer
{
    HWBuffer()
    {
        resetData();
    }

    void resetData()
    {
        handle = NULL;
        // fd used for phyical display
        phy_present_fence_fd = -1;

        // fd used by virtual display
        out_acquire_fence_fd = -1;
        out_retire_fence_fd = -1;

        // fd used by mirror producer (overlay engine)
        mir_out_rel_fence_fd = -1;
        mir_out_acq_fence_fd = -1;
        mir_out_if_fence_fd = -1;

        // fd used by mirror consumer (DpFramework)
        mir_in_acq_fence_fd = -1;
        mir_in_rel_fence_fd = -1;

        phy_present_fence_idx = 0;
        out_retire_fence_idx = 0;
        mir_out_sec_handle = 0;
        mir_out_acq_fence_idx = 0;
        mir_out_if_fence_idx = 0;

        dataspace = HAL_DATASPACE_UNKNOWN;
        queue_idx = -1;

        wdma_dump_roi.clear();
        wdma_dump_point = 0;
    }

    // struct used for phyical display
    // phy_present_fence_fd is used for present fence to notify hw vsync
    int phy_present_fence_fd;

    // phy_present_fence_idx is present fence index from display driver
    unsigned int phy_present_fence_idx;

    // struct used by virtual display
    // out_acq_fence_fd is used for hwc to know if outbuf could be read
    int out_acquire_fence_fd;

    // out_retire_fence_fd is used to notify producer
    // if output buffer is ready to be written
    int out_retire_fence_fd;

    // out_retire_fence_idx is retire fence index from display driver
    unsigned int out_retire_fence_idx;


    // struct used by mirror producer (overlay engine)
    // mir_out_sec_handle is secure handle for mirror output if needed
    SECHAND mir_out_sec_handle;

    // mir_out_rel_fence_fd is fence to notify
    // all producer display in previous round are finished
    // and mirror buffer is ready to be written
    int mir_out_rel_fence_fd;

    // mir_out_acq_fence_fd is fence to notify
    // when mirror buffer's contents are available
    int mir_out_acq_fence_fd;

    // mir_out_acq_fence_idx is fence index from display driver
    unsigned int mir_out_acq_fence_idx;

    // mir_out_if_fence_fd is fence to notify
    // all producer display in previous round are finished
    // and mirror buffer is ready to be written
    int mir_out_if_fence_fd;

    // mir_out_if_fence_idx is fence index from display driver (decouple mode)
    unsigned int mir_out_if_fence_idx;

    // struct used by mirror consumer (DpFramework)
    // mir_in_acq_fence_fd is used to notify
    // when mirror buffer's contents are available
    int mir_in_acq_fence_fd;

    // mir_in_rel_fence_fd is used to notify
    // mirror source that buffer is ready to be written
    int mir_in_rel_fence_fd;

    // outbuf native handle
    buffer_handle_t handle;

    // private handle information
    PrivateHandle priv_handle;

    int32_t dataspace;

    // Only used for Output DBQ Buffer
    int queue_idx;

    // WDMA dump ROI
    Rect wdma_dump_roi;

    // WDMA dump point
    int wdma_dump_point;
};

struct MdpJob
{
    MdpJob()
        : id(0)
        , fence(-1)
        , is_used(false)
    { }
    uint32_t id;
    int fence;
    bool is_used;
};

// DispatcherJob is a display job unit and is used by DispatchThread
class DispatcherJob : public LightPoolBase<DispatcherJob>
{
public:
    DispatcherJob(unsigned int max_ovl_inputs);
    ~DispatcherJob();

    void resetData();

    void initData() {}

    unsigned int m_max_ovl_inputs;

    // check if job should be processed
    bool enable;

    // check if job is for internal display (phsical and not external)
    bool is_internal;

    // check if job should use secure composition
    bool secure;

    // check if job acts as a mirror source
    // if yes, it needs to provide a mirror output buffer to one another
    bool mirrored;

    // check if display use decouple mirror mode
    // if yes, it need to provide a decouple output buffer to display driver
    bool need_output_buffer;

    // display id
    uint64_t disp_ori_id;

    uint32_t drm_id_cur_crtc;

    // display id of mirror source
    int disp_mir_id;

    // orientation of display
    unsigned int disp_ori_rot;

    // orientation of mirror source
    unsigned int disp_mir_rot;

    // amount of the current available overlay inputs
    unsigned int num_layers;

    // check if ovl engine has availale inputs
    bool ovl_valid;

    // check if fbt is used
    bool fbt_exist;

    // check if the current job is triggered
    bool triggered;

    // amount of layers for UI composer
    int num_ui_layers;

    // amount of layers for MM composer
    int num_mm_layers;

    // amount of layers for GLAI composer
    int num_glai_layers;

    // used to determine if need to trigger UI/MM composers
    int post_state;

    // used as a sequence number for profiling latency purpose
    uint64_t sequence;

    // used for video frame
    unsigned int timestamp;

    // input layers for compositing
    HWLayer* hw_layers;

    // mirror source buffer for bitblit
    HWBuffer hw_mirbuf;

    // the output buffer of
    // 1. virtual display
    // 2. mirror source
    HWBuffer hw_outbuf;

    // store present fence fd
    int prev_present_fence_fd;

    LayerListInfo layer_info;

    // set true to invalidate full screen
    bool is_full_invalidate;

    // store the MDP job id for output buffer
    uint32_t mdp_job_output_buffer;

    OverlayPortParam mdp_mirror_ovl_port_param;

    // store the MDP job id and fence for clear background
    MdpJob fill_black;

    // determine the job whether it need to group mdp and overlay
    bool need_av_grouping;

    // amount of layers for MM composer which do not bypass
    int num_processed_mm_layers;

    // determine the job is need to fill black to output buffer or not
    bool is_black_job;

    sp<ColorTransform> color_transform;

    // active config
    hwc2_config_t active_config;

    // the display data active config
    const DisplayData* disp_data;

    // the mirror source display data active config
    const DisplayData* mir_disp_data;
#ifdef MTK_IN_DISPLAY_FINGERPRINT
    // for LED HBM (High Backlight Mode) control
    bool is_HBM;
#endif
#ifdef MTK_HDR_SET_DISPLAY_COLOR
    // set display color dynamically for HDR feature
    int is_HDR;
#endif

    // if the pq mode is the same with pervious job, we can skip to notify DISP PQ
    bool dirty_pq_mode_id;

    // hint MDP PQ and DISP PQ use which mode id in PQ XML
    int32_t pq_mode_id;

    // when DISP PQ need synchronization, it use this dence to notify us
    int pq_fence_fd;

    // sf want present at this ts
    nsecs_t sf_target_ts;

    // wait till this timestamp before present to kernel
    nsecs_t present_after_ts;

    // decouple target ts
    nsecs_t decouple_target_ts;

    float ovl_mc;
    float ovl_mc_atomic_ratio;
    nsecs_t ovl_wo_atomic_work_time;

    const HwcMCycleInfo* mc_info;

    bool aibld_enable;
    bool display_dump_enable;

    // use which cpu to process this job
    unsigned int cpu_set;

    // save current WDMA status for this job
    uint32_t wdma_status;
};

// HWCDispatcher is used to dispatch layers to DispatchThreads
class HWCDispatcher
{
public:
    static HWCDispatcher& getInstance();

    // onPlugIn() is used to notify if a new display is added
    void onPlugIn(uint64_t dpy, uint32_t drm_id_crtc, uint32_t drm_id_connector,
                  bool is_internal, uint32_t width, uint32_t height);

    // onPlugOut() is used to notify if a new display is removed
    void onPlugOut(uint64_t dpy, uint32_t drm_id_crtc);

    // setPowerMode() is used to wait display thread idle when display changes power mode
    void setPowerMode(uint64_t dpy, uint32_t drm_id_crtc, int mode);

    // onVSync() is used to receive vsync signal
    void onVSync(uint64_t dpy);

    // decideDirtyAndFlush() is used to verify if layer type is changed then set as dirty
    bool decideDirtyAndFlush(uint64_t dpy,
                             unsigned int idx,
                             sp<HWCLayer> hwc_layer,
                             HWLayer& hw_layer);

    // setSessionMode() is used to set display session mode
    void setSessionMode(uint64_t dpy, bool mirrored);

    // configMirrorJob() is used to config job as mirror source
    void configMirrorJob(DispatcherJob* job);
    void configMirrorOutput(DispatcherJob* job, const int& display_color_mode);

    // getJob() is used for HWCMediator to get a new job for filling in
    int getJob(uint64_t dpy);

    // getExistJob() is used for HWCMediator to get an exist job for re-filling
    DispatcherJob* getExistJob(uint64_t dpy);

    // setJob() is used to update jobs in hwc::set()
    void setJob(const sp<HWCDisplay>& display);

    // trigger() is used to queue job and trigger dispatchers to work
    void trigger(const hwc2_display_t& dpy);

    // registerVSyncListener() is used to register a VSyncListener to HWCDispatcher
    void registerVSyncListener(uint64_t dpy, const sp<HWCVSyncListener>& listener);

    // removeVSyncListener() is used to remove a VSyncListener to HWCDispatcher
    void removeVSyncListener(uint64_t dpy, const sp<HWCVSyncListener>& listener);

    // dump() is used for debugging purpose
    void dump(String8* dump_str);

    // ignoreJob() is used to notify that the display is removed
    void ignoreJob(uint64_t dpy, bool ignore);

//------------------------------------------------------------------------------------------
// For skip validate
    void incSessionModeChanged()
    {
        AutoMutex l(m_session_mode_changed_mutex);
        ++m_session_mode_changed;
    }
    void decSessionModeChanged()
    {
        AutoMutex l(m_session_mode_changed_mutex);
        if (m_session_mode_changed > 0)
            --m_session_mode_changed;
    }
    int getSessionModeChanged()
    {
        AutoMutex l(m_session_mode_changed_mutex);
        return m_session_mode_changed;
    }

    int getOvlEnginePowerModeChanged(const uint64_t& dpy) const;
    void decOvlEnginePowerModeChanged(const uint64_t& dpy) const;
//-------------------------------------------------------------------------------------------
public:
    void addBufToBufRecorder(buffer_handle_t val)
    {
        AutoMutex l(m_dup_recorder_mutex);
        m_dup_recorder.push_back(val);

        if (m_dup_recorder.size() > 20)
        {
            HWC_LOGE("Buf recorder size too big %zu", m_dup_recorder.size());
        }
    }

    void removeBufFromBufRecorder(buffer_handle_t val)
    {
        AutoMutex l(m_dup_recorder_mutex);
        size_t i = 0;
        for (; i < m_dup_recorder.size(); i++)
        {
            if (m_dup_recorder[i] == val)
                break;
        }

        if (i == m_dup_recorder.size())
        {
            HWC_LOGE("can't find handle buf:%p", val);
        }
        else
        {
            m_dup_recorder.erase(m_dup_recorder.begin() + i);
        }
    }

    void dupInputBufferHandle(DispatcherJob* job);
    void freeDuppedInputBufferHandle(DispatcherJob* job);

    void prepareMirror(const std::vector<sp<HWCDisplay> >& displays);
private:
    mutable Mutex m_dup_recorder_mutex;
    Vector<buffer_handle_t> m_dup_recorder;
private:
    friend class DispatchThread;

    HWCDispatcher();
    ~HWCDispatcher();

    void disableMirror(const sp<HWCDisplay>& display, DispatcherJob* job);
    // releaseResourceLocked() is used to release resources in display's WorkerCluster
    void releaseResourceLocked(uint64_t dpy, uint32_t drm_id_crtc);

    // clearUsedJob() is used to close dupped buffer handle and reset job
    void clearUsedJob(const hwc2_display_t& dpy, DispatcherJob* job);

    // handleJob() handle dispatch job with LayerComposer::trigger() and PostHandler::process()
    void handleJob(const hwc2_display_t& dpy, sp<DispatcherJob>& job);

    // access must be protected by m_vsync_lock
    mutable Mutex m_vsync_lock;

    // m_vsync_callbacks is a queue of VSyncListener registered by DispatchThread
    Vector<SortedVector< sp<HWCVSyncListener> > > m_vsync_callbacks;

    // m_alloc_disp_ids is a bit set of displays
    // each bit index with a 1 corresponds to an valid display session
    std::bitset<DisplayManager::MAX_DISPLAYS> m_alloc_disp_ids;

    // m_curr_jobs holds DispatcherJob of all displays
    // and is used between prepare() and set().
    sp<DispatcherJob> m_curr_jobs[DisplayManager::MAX_DISPLAYS];

    class PostHandler : public LightRefBase<PostHandler>
    {
    public:
        PostHandler(HWCDispatcher* dispatcher,
            uint64_t dpy, const sp<OverlayEngine>& ovl_engine);

        virtual ~PostHandler();

        // set() is used to get retired fence from display driver
        virtual void set(const sp<HWCDisplay>& display, DispatcherJob* job) = 0;

        // setMirror() is used to fill dst_job->hw_mirbuf
        // from src_job->hw_outbuf
        virtual void setMirror(DispatcherJob* src_job, DispatcherJob* dst_job) = 0;

        // process() is used to wait outbuf is ready to use
        // and sets output buffer to display driver
        virtual void process(DispatcherJob* job) = 0;

    protected:
        // set overlay input
        void setOverlayInput(DispatcherJob* job);

        // m_dispatcher is used for callback usage
        HWCDispatcher* m_dispatcher;

        // m_disp_id is used to identify which display
        uint64_t m_disp_id;

        // m_ovl_engine is used for config overlay engine
        sp<OverlayEngine> m_ovl_engine;

        // store the presentfence fd
        int m_curr_present_fence_fd;
    };

    class PhyPostHandler : public PostHandler
    {
    public:
        PhyPostHandler(HWCDispatcher* dispatcher,
            uint64_t dpy, const sp<OverlayEngine>& ovl_engine)
            : PostHandler(dispatcher, dpy, ovl_engine)
        { }

        virtual void set(const sp<HWCDisplay>& display, DispatcherJob* job);

        virtual void setMirror(DispatcherJob* src_job, DispatcherJob* dst_job);

        virtual void process(DispatcherJob* job);

    private:
        sp<FenceDebugger> m_fence_debugger;
    };

    class VirPostHandler : public PostHandler
    {
    public:
        VirPostHandler(HWCDispatcher* dispatcher,
            uint64_t dpy, const sp<OverlayEngine>& ovl_engine)
            : PostHandler(dispatcher, dpy, ovl_engine)
        { }

        virtual void set(const sp<HWCDisplay>& display, DispatcherJob* job);

        virtual void setMirror(DispatcherJob* src_job, DispatcherJob* dst_job);

        virtual void process(DispatcherJob* job);

    private:
        void setError(DispatcherJob* job);
    };

    // WorkerCluster is used for processing composition of single display.
    // One WokerCluster would creates
    // 1. one thread to handle a job list and
    // 2. two threads to handle UI or MM layers.
    // Different display would use different WorkCluster to handle composition.
    struct WorkerCluster
    {
        WorkerCluster()
            : enable(false)
            , is_internal(false)
            , force_wait(false)
            , ignore_job(false)
            , drm_id_cur_crtc(UINT32_MAX)
            , ovl_engine(NULL)
            , dp_thread(NULL)
            , idle_thread(NULL)
            , composer(NULL)
            , post_handler(NULL)
            , m_job_pool(NULL)
        {
        }

        ~WorkerCluster();

        // access must be protected by lock (DispatchThread <-> Hotplug thread)
        mutable Mutex plug_lock_loop;
        // access must be protected by lock (SurfaceFlinger <-> VSyncThread)
        mutable Mutex plug_lock_main;
        // access must be protected by lock (Hotplug thread <-> VSyncThread)
        mutable Mutex plug_lock_vsync;

        // plug out must be blocked if job still in used, should be unlock when job return to pool
        Condition plug_cond_job_inuse;

        bool enable;
        bool is_internal;
        bool force_wait;
        bool ignore_job;
        uint32_t drm_id_cur_crtc;   // will be updated when change crtc for this worker

        sp<OverlayEngine> ovl_engine;
        sp<DispatchThread> dp_thread;
        sp<IdleThread> idle_thread;
        sp<LayerComposer> composer;
        sp<PostHandler> post_handler;

        ObjectPool<DispatcherJob>* m_job_pool;

        struct PrevHwcLayer
        {
            uint64_t hwc2_layer_id;
            int pool_id;
            int32_t layer_caps;
            int32_t hdr_scenario;
            uint32_t pq_enable;
            uint32_t pq_pos;
            uint32_t pq_orientation;
            uint32_t pq_table_idx;
            bool need_pq;
            bool is_ai_pq;

            PrevHwcLayer(uint64_t id)
                : hwc2_layer_id(id)
                , pool_id(-1)
                , layer_caps(0)
                , hdr_scenario(HWC_LAYER_HDR_NONE)
                , pq_enable(0)
                , pq_pos(0)
                , pq_orientation(0)
                , pq_table_idx(0)
                , need_pq(false)
                , is_ai_pq(false)
            {
            }

            void update(const HWLayer& hw_layers);
        };
        std::list<PrevHwcLayer> prev_hwc_layers;
    };

    // m_workers is the WorkerCluster array used by different display composition.
    WorkerCluster m_workers[DisplayManager::MAX_DISPLAYS];

    // m_sequence is used as a sequence number for profiling latency purpose
    // initialized to 1, (0 is reserved to be an error code)
    uint64_t m_sequence;

    // TODO: m_session_mode_changed should only be used when single validate,
    //       it can be removed after single validate is phase out.
    // For skip validate
    int m_session_mode_changed;
    mutable Mutex m_session_mode_changed_mutex;
public:
    void fillPrevHwLayers(const sp<HWCDisplay>& display, DispatcherJob* job);

    nsecs_t m_prev_createjob_time[DisplayManager::MAX_DISPLAYS];
};

// DispatchThread handles DispatcherJobs
// from UILayerComposer and MMLayerComposer
class DispatchThread : public HWCThread,
                       public HWCVSyncListener
{
public:
    DispatchThread(uint64_t dpy);

    // trigger() is used to add a dispatch job into a job queue,
    // then triggers DispatchThread
    void trigger(sp<DispatcherJob> job);

    size_t getQueueSize();

private:
    virtual void onFirstRef();
    virtual bool threadLoop();

    // waitNextVSyncLocked() requests and waits for the next vsync signal
    void waitNextVSyncLocked(uint64_t dpy);

    /* ------------------------------------------------------------------------
     * VSyncListener interface
     */
    void onVSync();

    // implementation of drop job
    bool dropJob();

    void calculatePerf(DispatcherJob* job);

    // update the cpu set with job's configuration
    void updateCpuSet(pid_t tid, DispatcherJob* job);

    // m_disp_id is used to identify which display
    // DispatchThread needs to handle
    uint64_t m_disp_id;

    // m_job_queue is a job queue
    // which new job would be queued in set()
    typedef Vector<sp<DispatcherJob>> Fifo;
    Fifo m_job_queue;

    // access must be protected by m_vsync_lock
    mutable Mutex m_vsync_lock;
    Condition m_vsync_cond;

    pid_t m_tid;

    std::string m_perf_mc_dispatcher_str;
    std::string m_perf_mc_ovl_str;
    std::string m_perf_scenario_str;
    std::string m_perf_remain_time_str;
    std::string m_perf_target_cpu_mhz_str;
    std::string m_perf_uclamp_str;
    std::string m_perf_extension_time_str;

    uint32_t m_perf_prev_uclamp_min = UINT32_MAX;

    // store the last cpu set from DispatcherJob
    unsigned int m_cpu_set = HWC_CPUSET_NONE;
};

#endif // HWC_DISPATCHER_H_
