#ifndef HWC_OVERLAY_H_
#define HWC_OVERLAY_H_

#include <utils/Vector.h>
#include <utils/RefBase.h>

#include <hwc_common/pool.h>
#include <linux/mediatek_drm.h>

#include "data_express.h"
#include "dev_interface.h"
#include "hwc_ui/Rect.h"
#include "hwc2_defs.h"
#include "vsync_listener.h"
#include "worker.h"
#include "mtk-mml.h"

#define HWLAYER_ID_NONE -1
#define HWLAYER_ID_DBQ  -20

#define POWER_MODE_CHANGED_DO_VALIDATE_NUM 1
#define HDMIRX_BLACK_SCREEN_FRAME_COUNT (30)

using namespace android;
using hwc::Rect;

class DisplayBufferQueue;
class DispatcherJob;
struct HWBuffer;
struct dump_buff;
class SyncFence;
struct ColorTransform;

// ---------------------------------------------------------------------------

struct OverlayPrepareParam
{
    OverlayPrepareParam()
        : id(UINT_MAX)
        , ion_fd(-1)
        , is_need_flush(0)
        , fence_index(0)
        , fence_fd(-1)
        , if_fence_index(0)
        , if_fence_fd(-1)
        , blending(0)
        , drm_id_crtc(UINT32_MAX)
    { }

    unsigned int id;
    int ion_fd;
    unsigned int is_need_flush;

    unsigned int fence_index;
    int fence_fd;

    // in decoupled mode,
    // interface fence of frame N would be released when
    // RDMA starts to read frame (N+1)
    unsigned int if_fence_index;
    int if_fence_fd;

    // Use to judge format which is premultiplied format or not
    int blending;

    uint32_t drm_id_crtc;

    // Use to judge ext video frame or not
    int ext_video_frame;

    // hdr ext metadata info
    struct mtk_ethdr_metadata ethdr_meta;
};

enum INPUT_PARAM_STATE
{
    OVL_IN_PARAM_DISABLE = 0,
    OVL_IN_PARAM_ENABLE  = 1,
};

struct OverlayPortParam
{
    OverlayPortParam()
        : state(OVL_IN_PARAM_DISABLE)
        , va(NULL)
        , mva(NULL)
        , pitch(0)
        , v_pitch(0)
        , format(0)
        , is_sharpen(0)
        , fence_index(0)
        , if_fence_index(0)
        , identity(HWLAYER_ID_NONE)
        , connected_type(0)
        , protect(false)
        , secure(false)
        , alpha_enable(0)
        , alpha(0xFF)
        , blending(0)
        , dim(false)
        , sequence(0)
        , ion_fd(-1)
        , mir_rel_fence_fd(-1)
        , ovl_dirty_rect_cnt(0)
        , ext_sel_layer(-1)
        , fence(-1)
        , layer_color(0)
        , dataspace(HAL_DATASPACE_UNKNOWN)
        , compress(false)
        , src_buf_width(0)
        , src_buf_height(0)
        , fb_id(0)
        , alloc_id(UINT64_MAX)
        , hwc_layer_id(UINT64_MAX)
        , queue_idx(-1)
        , size(0)
        , is_mml(false)
        , mml_cfg(NULL)
        , dump_point(-1)
        , debug_type(HWC_DEBUG_LAYER_TYPE_NONE)
        , hdmi_rx(false)
    {
        memset(&(ovl_dirty_rect[0]), 0, sizeof(hwc_rect_t) * MAX_DIRTY_RECT_CNT);
    }

    ~OverlayPortParam()
    {
        removeMMLCfg();
    }

    void resetMMLCfg()
    {
        mml_job * job_p =  NULL;
        mml_pq_param * pq_param_p[MML_MAX_OUTPUTS] = {NULL,NULL};

        if (NULL == mml_cfg)
        {
            HWC_LOGE("MMLCfg is not allocate, please allocate it first !");
            return;
        }

        // keep these allocated address to avoid memeset set to 0
        job_p = mml_cfg->job;
        for (int i =0; i < MML_MAX_OUTPUTS; i++)
        {
            pq_param_p[i] = mml_cfg->pq_param[i];
        }

        memset(mml_cfg, 0, sizeof(mml_submit));

        // reset src buffer fd
        for (int i =0; i < MML_MAX_PLANES; i++ )
        {
            mml_cfg->buffer.src.fd[i] = -1;
        }

        // reset dst buffer fd
        for (int i =0; i < MML_MAX_OUTPUTS; i++ )
        {
            for (int j =0; j < MML_MAX_PLANES; j++ )
            {
                mml_cfg->buffer.dest[i].fd[j] = -1;
            }
        }

        //assign address back
        mml_cfg->job = job_p;
        for (int i =0; i < MML_MAX_OUTPUTS; i++)
        {
            mml_cfg->pq_param[i] = pq_param_p[i];
        }

        // memeset set job, pq_param content
        if (NULL != job_p)
        {
            memset(mml_cfg->job, 0, sizeof(mml_job));
            mml_cfg->job->fence = -1;
        }

        for (int i =0; i < MML_MAX_OUTPUTS; i++)
        {
            if (NULL != mml_cfg->pq_param[i])
            {
                memset(mml_cfg->pq_param[i], 0, sizeof(mml_pq_param));
            }
        }
        return;
    }

    void allocMMLCfg()
    {
        if (NULL != mml_cfg)
        {
            HWC_LOGE("MMLCfg is not NULL, please free it first !");
            return;
        }

        mml_cfg = new mml_submit;
        if (NULL == mml_cfg)
        {
            HWC_LOGE("MMLCfg allocate fail");
            return;
        }
        memset(mml_cfg, 0, sizeof(mml_submit));

        mml_cfg->job = new mml_job;
        if (NULL == mml_cfg->job)
        {
            HWC_LOGE("MMLCfg job allocate fail");
            delete mml_cfg;
            mml_cfg = NULL;
            return;
        }
        memset(mml_cfg->job, 0, sizeof(mml_job));

        for (int i = 0; i < MML_MAX_OUTPUTS; ++i)
        {
            mml_cfg->pq_param[i] = new mml_pq_param;
            if (NULL == mml_cfg->pq_param[i])
            {
                HWC_LOGE("MMLCfg pq_param allocate fail");
                delete mml_cfg->job;
                delete mml_cfg;
                mml_cfg = NULL;
                return;
            }
            memset(mml_cfg->pq_param[i], 0, sizeof(mml_pq_param));
        }
        resetMMLCfg();
    };

    void removeMMLCfg()
    {
        if (NULL != mml_cfg)
        {
            if (NULL != mml_cfg->job)
                delete mml_cfg->job;

            for (int i = 0; i < MML_MAX_OUTPUTS; ++i)
            {
                if (NULL != mml_cfg->pq_param[i])
                    delete mml_cfg->pq_param[i];
            }

            delete mml_cfg;
            mml_cfg = NULL;
        }
    };

    int state;
    void* va;
    void* mva;
    unsigned int pitch;
    unsigned int v_pitch;
    unsigned int format;
    Rect src_crop;
    Rect dst_crop;
    unsigned int is_sharpen;
    unsigned int fence_index;
    unsigned int if_fence_index;
    int identity;
    int connected_type;
    bool protect;
    bool secure;
    unsigned int alpha_enable;
    unsigned char alpha;
    int blending;
    bool dim;
    uint64_t sequence;
#ifdef MTK_HWC_PROFILING
    int fbt_input_layers;
    int fbt_input_bytes;
#endif

    int ion_fd;

    int mir_rel_fence_fd;

    // dirty rect info
    size_t ovl_dirty_rect_cnt;
    hwc_rect_t ovl_dirty_rect[MAX_DIRTY_RECT_CNT];

    int ext_sel_layer;
    int fence;

    // solid color
    unsigned int layer_color;

    int dataspace;

    bool compress;

    // TODO: do we need this info?
    unsigned int src_buf_width;
    unsigned int src_buf_height;
    // DRM use fb_id to set buffer
    uint32_t fb_id;
    uint64_t alloc_id;
    uint64_t hwc_layer_id;

    // Only used for Output DBQ Buffer
    int queue_idx;

    // total bytes allocated by gralloc for dump buffer
    int size;

    bool is_mml;
    struct mml_submit* mml_cfg;

    // Dump point for display dump & aiblulight
    int dump_point;

    // the debug type of thie layer
    uint32_t debug_type;

    // buf from HDMI RX HAL
    bool hdmi_rx;
};

class FrameOverlayInfo
{
public:
    FrameOverlayInfo();
    ~FrameOverlayInfo();
    void initData();

    bool ovl_valid;
    unsigned int num_layers;
    Vector<OverlayPortParam*> input;
    uint32_t drm_id_crtc;

    bool enable_output;
    OverlayPortParam output;
    sp<ColorTransform> color_transform;
private:
    void resetOverlayPortParam(OverlayPortParam* param);
};

class FrameInfo : public LightPoolBase<FrameInfo>
{
public:
    void initData();

    int present_fence_idx;
    int ovlp_layer_num;
    int prev_present_fence;
    bool av_grouping;
    FrameOverlayInfo overlay_info;
    uint32_t hrt_weight;
    uint32_t hrt_idx;
    hwc2_config_t active_config;
#ifdef MTK_IN_DISPLAY_FINGERPRINT
    bool is_HBM;
#endif
#ifdef MTK_HDR_SET_DISPLAY_COLOR
    int is_HDR;
#endif
    uint64_t frame_seq;
    DataPackage* package;
    DataPackage* late_package;
    int pq_fence_fd;
    nsecs_t present_after_ts;
    nsecs_t decouple_target_ts;
    float ovl_mc;
    float ovl_mc_atomic_ratio;
    nsecs_t ovl_wo_atomic_work_time;
    unsigned int cpu_set;
    uint32_t drm_id_crtc;
    int mm_ion_fd;
};

enum OVL_INPUT_TYPE
{
    OVL_INPUT_NONE    = 0,
    OVL_INPUT_UNKNOWN = 1,
    OVL_INPUT_DIRECT  = 2,
    OVL_INPUT_QUEUE   = 3,
};

// OverlayEngine is used for UILayerComposer and MMLayerComposer
// to config overlay input layers
class OverlayEngine : public HWCThread, public HWCVSyncListener
{
public:
    // prepareInput() is used to preconfig a buffer with specific input port
    status_t prepareInput(OverlayPrepareParam& param);

    // disableInput() is used to disable specific input port
    status_t disableInput(unsigned int id);

    // disableOutput() is used to disable output port
    //   in regard to performance, the overlay output buffers would not be released.
    //   please use releaseOutputQueue() to release overlay output buffers
    status_t disableOutput();

    // prepareOutput() is used to preconfig a buffer with output port
    status_t prepareOutput(OverlayPrepareParam& param);

    // setOutput() is used to set output port
    status_t setOutput(OverlayPortParam* param, bool mirrored = false);

    // is used to allocate overlay output buffers
    status_t createOutputQueue(unsigned int format, bool secure);

    // is used to release overlay output buffers
    status_t releaseOutputQueue();

    // preparePresentFence() is used to get present fence
    // in order to know when screen is updated
    status_t preparePresentFence(OverlayPrepareParam& param);

    // configMirrorOutput() is used to configure output buffer of mirror source
    //   if virtial display is used as mirror source,
    //   nothing is done and retun with NO_ERROR
    status_t configMirrorOutput(DispatcherJob* job);

    // setOverlaySessionMode() sets the overlay session mode
    status_t setOverlaySessionMode(HWC_DISP_MODE mode);

    // setOverlaySessionMode() gets the overlay session mode
    HWC_DISP_MODE getOverlaySessionMode();

    // trigger() is used to nofity engine to start doing composition
    void trigger(const unsigned int& present_fence_idx,
                 const int& prev_present_fence, const int& pq_fence_fd);

    // getInputParams() is used for client to get input params for configuration
    OverlayPortParam* const* getInputParams();

    void setPowerMode(uint32_t drm_id_crtc, int mode);

    // getMaxInputNum() is used for getting max amount of overlay inputs
    unsigned int getMaxInputNum() { return m_max_inputs; }

    // getAvailableInputNum() is used for
    // getting current available overlay inputs
    unsigned int getAvailableInputNum();

    // waitUntilAvailable() is used for waiting until OVL resource is available
    //   during overlay session mode transition,
    //   it is possible for HWC getting transition result from display driver;
    //   this function is used to wait untill
    //   the overlay session mode transition is finisned
    bool waitUntilAvailable();

    // dump() is used to dump each input data to OverlayEngine
    void dump(String8* dump_str);

    // wakeup av grouping job
    void wakeup();

    bool isEnable()
    {
        AutoMutex l(m_lock);
        return (m_engine_state == OVL_ENGINE_ENABLED);
    }

    inline void setHandlingJob(DispatcherJob* job) { m_handling_job = job; }

    OverlayEngine(const uint64_t& dpy, uint32_t drm_id_crtc, uint32_t drm_id_connector,
                  uint32_t width = 0, uint32_t height = 0);
    ~OverlayEngine();

    void onVSync(void);

    // For skip validate
    void decPowerModeChanged()
    {
        AutoMutex l(m_lock);
        if (m_power_mode_changed > 0)
            --m_power_mode_changed;
    }

    int getPowerModeChanged() const
    {
        AutoMutex l(m_lock);
        return m_power_mode_changed;
    }

    size_t getQueueSize();

private:
    enum PORT_STATE
    {
        OVL_PORT_DISABLE = 0,
        OVL_PORT_ENABLE  = 1,
    };

    struct OverlayInput
    {
        OverlayInput();

        // param is used for configure input parameters
        OverlayPortParam param;
    };

    struct OverlayOutput
    {
        OverlayOutput();

        // connected_state points the output port usage state
        int connected_state;

        // param is used for configure output parameters
        OverlayPortParam param;

        // queue is used to acquire and release buffer from client
        sp<DisplayBufferQueue> queue;
    };

    // is used to preallocate overlay output buffers
    status_t createOutputQueueLocked(unsigned int format, bool secure);

    // disableInputLocked is used to clear status of m_inputs
    void disableInputLocked(unsigned int id);

    // disableOutputLocked is used to clear status of m_output
    void disableOutputLocked();

    // threadLoop() is used to process related job, e.g. wait fence and trigger overlay
    virtual bool threadLoop();

    // loopHandler() is used to trigger overlay
    status_t loopHandler(sp<FrameInfo>& info);

    // waitAllFence() is used to wait layer fence, present fence and output buffer fence
    void waitAllFence(sp<FrameInfo>& info);

    // waitOverlayFence is used to wait layer fence and output buffer fence
    void waitOverlayFence(sp<FrameInfo>& frame_info);

    // closeOverlayFenceFd is used to close input and output fence
    void closeOverlayFenceFd(FrameOverlayInfo* info);

    // closeAllFenceFd is used to close input, output and present fence
    void closeAllFenceFd(const sp<FrameInfo>& info);

    // closeAllIonFd is used to close input ion fd
    void closeAllIonFd(const sp<FrameInfo>& info);

    void closeMMLCfgFd(const sp<FrameInfo>& info);

    // onFirstRef() is used to init OverlayEngine thread
    virtual void onFirstRef();

    // packageFrameInfo() is used to backup the overlay info for queued frame
    void packageFrameInfo(sp<FrameInfo>& info, const unsigned int& present_fence_idx,
                          const int& prev_present_fence,
                          const int& pq_fence_fd);

    // setInputsAndOutput() is used to update configs of input layers and output buffer to driver
    void setInputsAndOutput(FrameOverlayInfo* info);

    // doMmBufferDump() is used to dump ovl input buffer to mm buffer dump
    void doMmBufferDump(sp<FrameInfo>& info);

    // waitPresentAfterTs() is to make sure present after given time stamp
    void waitPresentAfterTs(sp<FrameInfo>& info);

    // checkPresentAfterTs() print trace when present delayed
    void checkPresentAfterTs(sp<FrameInfo>& info, nsecs_t period);

    void calculatePerf(sp<FrameInfo>& info, nsecs_t period, pid_t tid, bool is_atomic);

    // update the cpu set with FrameInfo's configuration
    void updateCpuSet(pid_t tid, unsigned int cpu_set);

    // find the present index layer, then replace its buffer
    void preparePresentIndexBuffer(sp<FrameInfo>& info);

    // release the present index layer
    void releasePresentIndexBuffer(sp<FrameInfo>& info);

    // m_condition is used to wait (block) for a certain condition to become true
    mutable Condition m_cond;

    // m_disp_id is display id
    uint64_t m_disp_id;

    uint32_t m_drm_id_cur_crtc;   // will be updated when change crtc for this overlay

    enum
    {
        OVL_ENGINE_DISABLED = 0,
        OVL_ENGINE_ENABLED  = 1,
        OVL_ENGINE_PAUSED   = 2,
    };
    // m_state used to verify if OverlayEngine could be use
    int m_engine_state;

    // m_max_inputs is overlay engine max amount of input layer
    // it is platform dependent
    unsigned int m_max_inputs;

    // m_inputs is input information array
    // it needs to be initialized with runtime information
    Vector<OverlayInput*> m_inputs;

    // m_input_params is an array which
    // points to all input configurations
    // which would be set to display driver
    Vector<OverlayPortParam*> m_input_params;

    OverlayOutput m_output;

    DispatcherJob* m_handling_job;

    // m_sync_fence is used to wait fence
    sp<SyncFence> m_sync_fence;

    // m_frame_queue is an array which store the parameters of queued frame
    Vector< sp<FrameInfo> > m_frame_queue;

    // m_pool keep all pointer of FrameInfo and maintain their life cycle
    ObjectPool<FrameInfo>* m_pool;

    // m_lock_av_grouping is used to protect the parameter of av grouping
    mutable Mutex m_lock_av_grouping;

    // m_cond_threadloop is used to control trigger timeing when need av grouping
    Condition m_cond_threadloop;

    // m_need_wakeup is used to tag overlay engine whether it need wakeup or not
    bool m_need_wakeup;

    // keep the last frameinfo for layer dump
    sp<FrameInfo> m_last_frame_info;

    // m_lock_dump is used to protect the parameter of av grouping
    mutable Mutex m_lock_dump;

    // For skip validate
    int m_prev_power_mode;
    int m_power_mode_changed;

    std::string m_trace_delay_name;
    int m_trace_delay_counter;
    std::string m_trace_decoulpe_delay_name;
    std::string m_trace_decoulpe_delay_ns_name;

    uint32_t m_prev_uclamp_min = UINT32_MAX;
    std::string m_perf_remain_time_str;
    std::string m_perf_target_cpu_mhz_str;
    std::string m_perf_uclamp_str;
    std::string m_perf_extension_time_str;

    // store the last cpu set from FrameInfo
    unsigned int m_cpu_set = HWC_CPUSET_NONE;

    // use for HDMIRX black screen after unlock
    uint32_t m_hdmirx_black_screen_countdown;
};

#endif // HWC_OVERLAY_H_
