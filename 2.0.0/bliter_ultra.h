#ifndef HWC_BLITER_ULTRA_H_
#define HWC_BLITER_ULTRA_H_

#include "queue.h"
#include "bliter_async.h"
#include "utils/tools.h"
#include "mml_asyncblitstream.h"

#include <memory>
#include <mutex>
#include <unordered_map>

using namespace android;

struct BufferConfig
{
    // identify if this config is valid
    bool is_valid;

    // check if need to do interlacing
    bool deinterlace;

    // buffer information in native handle
    unsigned int gralloc_width;
    unsigned int gralloc_height;
    unsigned int gralloc_format;
    unsigned int gralloc_stride;
    unsigned int gralloc_vertical_stride;
    unsigned int gralloc_cbcr_align;
    int gralloc_private_format;
    int gralloc_ufo_align_type;
    uint32_t gralloc_prexform;

    // src buffer setting for DpFramework
    DP_COLOR_ENUM src_dpformat;
    unsigned int  src_pitch;
    unsigned int  src_pitch_uv;
    unsigned int  src_width;
    unsigned int  src_height;
    unsigned int  src_plane;
    unsigned int  src_size[3];
    int32_t       src_dataspace;
    bool          src_compression;

    // dst buffer setting for DpFramework
    DP_COLOR_ENUM dst_dpformat;
    unsigned int  dst_width;
    unsigned int  dst_height;
    unsigned int  dst_pitch;
    unsigned int  dst_pitch_uv;
    unsigned int  dst_v_pitch;
    unsigned int  dst_plane;
    unsigned int  dst_size[3];
    unsigned int  dst_ovl_id;
    int32_t       dst_dataspace;
    bool          dst_compression;
};

struct HWLayer;

class BliterNode
{
public:
    struct BufferInfo
    {
        BufferInfo()
            : ion_fd(-1)
            , sec_handle(0)
            , handle(nullptr)
            , fence_fd(-1)
            , dpformat(DP_COLOR_YUYV)
            , pitch(0)
            , pitch_uv(0)
            , v_pitch(0)
            , plane(1)
            , pq_enable(0)
            , pq_pos(0)
            , pq_orientation(0)
            , pq_table_idx(0)
            , ai_pq_param(0)
            , compression(false)
        { memset(size, 0, sizeof(size)); }
        int ion_fd;
        SECHAND sec_handle;
        buffer_handle_t handle;
        Rect rect;
        int fence_fd;

        // dst buffer setting for DpFramework
        DP_COLOR_ENUM dpformat;
        unsigned int  pitch;
        unsigned int  pitch_uv;
        uint32_t      v_pitch;
        unsigned int  plane;
        unsigned int  size[3];
        uint32_t pq_enable;
        uint32_t pq_pos;
        uint32_t pq_orientation;
        uint32_t pq_table_idx;
        bool ai_pq_param;
        bool compression;

        void dump();
    };

    struct SrcInvalidateParam
    {
        SrcInvalidateParam()
            : deinterlace(false)
            , is_secure(false)
            , is_flush(false)
            , is_game(false)
            , pool_id(0)
            , time_stamp(0)
            , dataspace(HAL_DATASPACE_UNKNOWN)
            , hdr_scenario(HWC_LAYER_HDR_NONE)
            , pq_mode_id(DEFAULT_PQ_MODE_ID)
        {}

        BufferInfo bufInfo;

        bool deinterlace;
        bool is_secure;
        bool is_flush;
        bool is_game;

        int32_t pool_id;
        uint32_t time_stamp;
        int32_t dataspace;
        std::vector<int32_t> hdr_static_metadata_keys;
        std::vector<float> hdr_static_metadata_values;
        std::vector<uint8_t> hdr_dynamic_metadata;
        int32_t hdr_scenario;

        int32_t pq_mode_id;

        void dump();
    };

    struct DstInvalidateParam
    {
        DstInvalidateParam()
            : xform(0)
            , is_enhance(0)
            , dataspace(HAL_DATASPACE_UNKNOWN)
            , is_secure(0)
        {}
        BufferInfo bufInfo;
        uint32_t xform;
        Rect src_crop;
        Rect dst_crop;
        uint32_t is_enhance;
        int32_t dataspace;
        bool is_secure;

        void dump();
    };

    struct Parameter
    {
        Rect            src_roi;
        Rect            dst_roi;
        BufferConfig*   config;
        uint32_t        xform;
        uint32_t        pq_enhance;
        bool            secure;
    };

    typedef enum {
        HWC_2D_BLITER_PROCESSER_NONE = 0,
        HWC_2D_BLITER_PROCESSER_MDP = 1,
        HWC_2D_BLITER_PROCESSER_MML = 2,
    } HWC_2D_BLITER_PROCESSER;

    struct JobParam
    {
        JobParam(uint32_t in_job_id, DP_STATUS_ENUM in_create_state)
            : job_id(in_job_id)
            , create_state(in_create_state)
            , hwc_to_mdp_rel_fd(-1)
        {
            memset(&mdp_finish_time, 0, sizeof(struct timeval));

            processer = HWC_2D_BLITER_PROCESSER_NONE;
        }

        uint32_t job_id;
        DP_STATUS_ENUM create_state;
        SrcInvalidateParam src_param;
        DstInvalidateParam dst_param;
        DpRect src_roi;
        DpRect dst_roi;
        DpRect output_roi;

        int32_t hwc_to_mdp_rel_fd;
        timespec mdp_finish_time;

        HWC_2D_BLITER_PROCESSER processer;

        void dump(std::string prefix);
    };

    BliterNode(uint64_t dpy);

    ~BliterNode();

    void createJob(uint32_t& job_id, int32_t &fence,
                       const BliterNode::HWC_2D_BLITER_PROCESSER &blit_processer
                           = HWC_2D_BLITER_PROCESSER_MDP);

    void setSrc(const uint32_t& job_id,
                BufferConfig* config,
                PrivateHandle& src_priv_handle,
                int* src_fence_fd = NULL,
                const std::vector<int32_t>& hdr_static_metadata_keys = std::vector<int32_t>(),
                const std::vector<float>& hdr_static_metadata_values = std::vector<float>(),
                const std::vector<uint8_t>& hdr_dynamic_metadata = std::vector<uint8_t>(),
                const int32_t& hdr_scenario = HWC_LAYER_HDR_NONE,
                const bool& is_game = false,
                const int32_t& pq_mode_id = DEFAULT_PQ_MODE_ID);

    void setDst(const uint32_t& job_id,
                Parameter* param,
                int ion_fd,
                SECHAND sec_handle,
                int* dst_fence_fd = NULL);

    void calculateAllROI(const uint32_t& job_id, Rect* cal_dst_roi = nullptr);

    void cancelJob(const uint32_t& job_id);

    status_t invalidate(const uint32_t& job_id,
                        const uint64_t& dispatch_job_id = 0,
                        const hwc2_config_t& active_config = 0,
                        const nsecs_t present_after_ts = -1,
                        const nsecs_t decouple_target_ts = -1,
                        const HWC_2D_BLITER_PROCESSER& blit_processer
                            = HWC_2D_BLITER_PROCESSER_MDP,
                        int32_t* rel_fence = NULL);

    status_t getHWCExpectMDPFinishedTime(timeval* mdp_finish_time,
                                         timespec* mdp_finish_time_ts,
                                         const uint64_t& dispatch_job_id,
                                         const nsecs_t& refresh,
                                         const nsecs_t present_after_ts,
                                         const nsecs_t decouple_target_ts,
                                         const bool& is_game);
    void setLayerID(const uint32_t& portIndex, const uint64_t &layer_id);
    void setMMLMode(const int32_t& mode);
    void setIsPixelAlphaUsed(bool is_pixel_alpha_used);
    mml_submit* getMMLSubmit();
private:
    static status_t calculateROI(DpRect* src_roi, DpRect* dst_roi, DpRect* output_size,
        const SrcInvalidateParam& src_param, const DstInvalidateParam& dst_param,
        const BufferInfo& src_buf, const BufferInfo& dst_buf);
    static status_t calculateContentROI(Rect* cnt_roi, const DpRect& dst_roi,
        const DpRect& output_roi, int32_t padding);

    DP_PROFILE_ENUM mapDataspace2DpColorRange(const int32_t ds, const bool& is_input);

    status_t errorCheck(const std::shared_ptr<JobParam>& job_param);
    void cancelJobInternal(const uint32_t& job_id, const std::shared_ptr<JobParam>& job_param);
#ifdef HWC_PQ_DEMO
    DpAsyncBlitStream2 m_blit_stream;
#else
    DpAsyncBlitStream m_blit_stream;
#endif
    DbgLogger* m_config_logger;

    DbgLogger* m_geometry_logger;

    DbgLogger* m_buffer_logger;

    uint64_t m_dpy;

    bool m_bypass_mdp_for_debug;

    std::mutex mMutex;
    std::unordered_map<uint32_t, std::shared_ptr<JobParam>> m_job_params GUARDED_BY(mMutex);

private:
    // Because MML doesn't create JobID and fence at createJob() anymore,
    // use m_job_para_id to replace jobID which is return from blitstream createJob().
    uint32_t m_job_para_id;
    MMLASyncBlitStream m_mml_blit_stream;
};
#endif
