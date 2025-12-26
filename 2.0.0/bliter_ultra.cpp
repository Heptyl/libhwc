#define DEBUG_LOG_TAG "NOD"

#include <cutils/properties.h>
#include <iomanip>
#include <sstream>
#include <sync.h>
#include <math.h>

#include <hwc_feature_list.h>

#include "utils/debug.h"
#include "utils/tools.h"
#include "bliter_ultra.h"
#include "overlay.h"
#include "dispatcher.h"
#include "utils/transform.h"
#include "display.h"
#include "platform_wrap.h"
#include "sync.h"
#include "hwc2.h"

#include <android/hardware/graphics/common/1.2/types.h>
using android::hardware::graphics::common::V1_2::BufferUsage;

using std::to_string;
using std::endl;

#ifdef USE_SWWATCHDOG
#include "utils/swwatchdog.h"
#define WDT_BL_STREAM(fn, BlitProcesser, ...)                                                   \
({                                                                                              \
    DP_STATUS_ENUM ret;                                                                         \
    if (Platform::getInstance().m_config.wdt_trace)                                             \
    {                                                                                           \
        ATRACE_NAME(#fn);                                                                       \
        SWWatchDog::AutoWDT _wdt("[NOD] DpAsyncBlitStream2." #fn "():" STRINGIZE(__LINE__), 500);   \
        if (HWC_2D_BLITER_PROCESSER_MML == BlitProcesser)                                       \
            ret = m_mml_blit_stream.fn(__VA_ARGS__);                                            \
        else                                                                                    \
            ret = m_blit_stream.fn(__VA_ARGS__);                                                \
    }                                                                                           \
    else                                                                                        \
    {                                                                                           \
        SWWatchDog::AutoWDT _wdt("[NOD] DpAsyncBlitStream2." #fn "():" STRINGIZE(__LINE__), 500);   \
        if (HWC_2D_BLITER_PROCESSER_MML == BlitProcesser)                                       \
            ret = m_mml_blit_stream.fn(__VA_ARGS__);                                            \
        else                                                                                    \
            ret = m_blit_stream.fn(__VA_ARGS__);                                                \
    }                                                                                           \
    if (ret != DP_STATUS_RETURN_SUCCESS) job_param->dump(std::string(#fn"_") + to_string(ret)); \
    ret;                                                                                        \
})

#define WDT_BL_STREAM_1VAR(fn, BlitProcesser, var1, ...)                                        \
({                                                                                              \
    DP_STATUS_ENUM ret;                                                                         \
    if (Platform::getInstance().m_config.wdt_trace)                                             \
    {                                                                                           \
        ATRACE_NAME(#fn);                                                                       \
        SWWatchDog::AutoWDT _wdt("[NOD] DpAsyncBlitStream2." #fn "():" STRINGIZE(__LINE__), 500);   \
        if (HWC_2D_BLITER_PROCESSER_MML == BlitProcesser)                                       \
            ret = m_mml_blit_stream.fn(var1, __VA_ARGS__);                                      \
        else                                                                                    \
            ret = m_blit_stream.fn(var1);                                                       \
    }                                                                                           \
    else                                                                                        \
    {                                                                                           \
        SWWatchDog::AutoWDT _wdt("[NOD] DpAsyncBlitStream2." #fn "():" STRINGIZE(__LINE__), 500);   \
        if (HWC_2D_BLITER_PROCESSER_MML == BlitProcesser)                                       \
            ret = m_mml_blit_stream.fn(var1, __VA_ARGS__);                                      \
        else                                                                                    \
            ret = m_blit_stream.fn(var1);                                                       \
    }                                                                                           \
    if (ret != DP_STATUS_RETURN_SUCCESS) job_param->dump(std::string(#fn"_") + to_string(ret)); \
    ret;                                                                                        \
})
#else // USE_SWWATCHDOG
#define WDT_BL_STREAM(fn, BlitProcesser, ...)                                                   \
({                                                                                              \
    DP_STATUS_ENUM ret;                                                                         \
    if (Platform::getInstance().m_config.wdt_trace)                                             \
    {                                                                                           \
        ATRACE_NAME(#fn);                                                                       \
        if (HWC_2D_BLITER_PROCESSER_MML == BlitProcesser)                                       \
            ret = m_mml_blit_stream.fn(__VA_ARGS__);                                            \
        else                                                                                    \
            ret = m_blit_stream.fn(__VA_ARGS__);                                                \
    }                                                                                           \
    else                                                                                        \
    {                                                                                           \
        if (HWC_2D_BLITER_PROCESSER_MML == MMLType)                                             \
            ret = m_mml_blit_stream.fn(__VA_ARGS__);                                            \
        else                                                                                    \
            ret = m_blit_stream.fn(__VA_ARGS__);                                                \
    }                                                                                           \
    if (ret != DP_STATUS_RETURN_SUCCESS) job_param->dump(std::string(#fn"_") + to_string(ret)); \
    ret;                                                                                        \
})

#define WDT_BL_STREAM_1VAR(fn, BlitProcesser, var1, ...)                                        \
({                                                                                              \
    DP_STATUS_ENUM ret;                                                                         \
    if (Platform::getInstance().m_config.wdt_trace)                                             \
    {                                                                                           \
        ATRACE_NAME(#fn);                                                                       \
        if (HWC_2D_BLITER_PROCESSER_MML == BlitProcesser)                                       \
            ret = m_mml_blit_stream.fn(var1, __VA_ARGS__);                                      \
        else                                                                                    \
            ret = m_blit_stream.fn(var1);                                                       \
    }                                                                                           \
    else                                                                                        \
    {                                                                                           \
        if (HWC_2D_BLITER_PROCESSER_MML == MMLType)                                             \
            ret = m_mml_blit_stream.fn(var1, __VA_ARGS__);                                      \
        else                                                                                    \
            ret = m_blit_stream.fn(var1);                                                       \
    }                                                                                           \
    if (ret != DP_STATUS_RETURN_SUCCESS) job_param->dump(std::string(#fn"_") + to_string(ret)); \
    ret;                                                                                        \
})
#endif // USE_SWWATCHDOG

#define NLOGD(x, ...) HWC_LOGD("(%" PRIu64 ") " x, m_dpy, ##__VA_ARGS__)
#define NLOGI(x, ...) HWC_LOGI("(%" PRIu64 ") " x, m_dpy, ##__VA_ARGS__)
#define NLOGW(x, ...) HWC_LOGW("(%" PRIu64 ") " x, m_dpy, ##__VA_ARGS__)
#define NLOGE(x, ...) HWC_LOGE("(%" PRIu64 ") " x, m_dpy, ##__VA_ARGS__)

unsigned int mapDpOrientation(const uint32_t transform)
{
    unsigned int orientation = DpAsyncBlitStream::ROT_0;

    // special case
    switch (transform)
    {
        // logically equivalent to (ROT_270 + FLIP_V)
        case (Transform::ROT_90 | Transform::FLIP_H):
            return (DpAsyncBlitStream::ROT_90 | DpAsyncBlitStream::FLIP_V);

        // logically equivalent to (ROT_270 + FLIP_H)
        case (Transform::ROT_90 | Transform::FLIP_V):
            return (DpAsyncBlitStream::ROT_90 | DpAsyncBlitStream::FLIP_H);
    }

    // general case
    if (Transform::FLIP_H & transform)
        orientation |= DpAsyncBlitStream::FLIP_H;

    if (Transform::FLIP_V & transform)
        orientation |= DpAsyncBlitStream::FLIP_V;

    if (Transform::ROT_90 & transform)
        orientation |= DpAsyncBlitStream::ROT_90;

    return orientation;
}

BliterNode::BliterNode(uint64_t dpy)
    : m_dpy(dpy)
    , m_job_para_id(0)
    , m_mml_blit_stream(dpy)
{
    const uint32_t dbg_flag = DbgLogger::TYPE_HWC_LOG;

    m_buffer_logger     = new DbgLogger(dbg_flag, 'D', nullptr);
    m_config_logger     = new DbgLogger(dbg_flag | DbgLogger::TYPE_PERIOD, 'D', nullptr);
    m_geometry_logger   = new DbgLogger(dbg_flag | DbgLogger::TYPE_PERIOD, 'D', nullptr);

    char value[PROPERTY_VALUE_MAX] = {0};

    // Read property for bypass MDP
    property_get("vendor.debug.hwc.bypassMDP", value, "0");
    m_bypass_mdp_for_debug = (0 != atoi(value));

    if (m_dpy == HWC_DISPLAY_PRIMARY)
    {
        m_blit_stream.setUser(DP_BLIT_GENERAL_USER);
    }
    else
    {
#ifdef MTK_HDMI_RXVC_MULTIDISP_SUPPORT
        // MDP resource confict : HDMIRX and HWC external display
        m_blit_stream.setUser(DP_BLIT_GENERAL_USER);
#else
        m_blit_stream.setUser(DP_BLIT_ADDITIONAL_DISPLAY);
#endif
    }
}

BliterNode::~BliterNode()
{
    delete m_buffer_logger;
    delete m_config_logger;
    delete m_geometry_logger;
}

void BliterNode::cancelJob(const uint32_t& job_id)
{
    std::shared_ptr<JobParam> job_param;
    {
        std::lock_guard<std::mutex> lk(mMutex);
        job_param = m_job_params[job_id];
        m_job_params.erase(job_id);
    }
    if (!job_param)
    {
        LOG_FATAL("%s(), job_param == nullptr", __FUNCTION__);
        return;
    }

    cancelJobInternal(job_param->job_id, job_param);
}

void BliterNode::cancelJobInternal(const uint32_t& job_id, const std::shared_ptr<JobParam>& job_param)
{
    closeFenceFd(&job_param->src_param.bufInfo.fence_fd);
    closeFenceFd(&job_param->dst_param.bufInfo.fence_fd);
    if (job_param->create_state == DP_STATUS_RETURN_SUCCESS)
    {
        WDT_BL_STREAM(cancelJob, job_param->processer, job_id);
    }
}

status_t BliterNode::errorCheck(const std::shared_ptr<JobParam>& job_param)
{
    SrcInvalidateParam& src_param = job_param->src_param;
    BufferInfo& src_buf = src_param.bufInfo;

    if (job_param->create_state != DP_STATUS_RETURN_SUCCESS)
    {
        NLOGE("%s / create_state(%d)", __FUNCTION__, job_param->create_state);
        return -EINVAL;
    }

    if (m_bypass_mdp_for_debug)
    {
        NLOGE("%s / Bypass MDP", __FUNCTION__);
        return -EINVAL;
    }

    if (src_buf.ion_fd < 0)
    {
        NLOGE("%s / no src ion fd", __FUNCTION__);
        return -EINVAL;
    }

    DstInvalidateParam& dst_param = job_param->dst_param;
    BufferInfo& dst_buf = dst_param.bufInfo;

    if (dst_buf.ion_fd < 0)
    {
        NLOGE("%s / no dst ion fd", __FUNCTION__);
        return -EINVAL;
    }

    Rect& src_crop = dst_param.src_crop;
    Rect& dst_crop = dst_param.dst_crop;
    if ((src_crop.getWidth() <= 1) || (src_crop.getHeight() <= 1) ||
        (dst_crop.getWidth() <= 0) || (dst_crop.getHeight() <= 0))
    {
        NLOGE("%s / unexpectedWH / src(%d,%d) dst(%d,%d)", __FUNCTION__,
                src_crop.getWidth(), src_crop.getHeight(),
                dst_crop.getWidth(), dst_crop.getHeight());
        return -EINVAL;
    }

    return NO_ERROR;
}

DP_PROFILE_ENUM BliterNode::mapDataspace2DpColorRange(const int32_t ds, const bool& is_input)
{
    switch (ds & HAL_DATASPACE_STANDARD_MASK)
    {
        case HAL_DATASPACE_STANDARD_BT601_625:
        case HAL_DATASPACE_STANDARD_BT601_625_UNADJUSTED:
        case HAL_DATASPACE_STANDARD_BT601_525:
        case HAL_DATASPACE_STANDARD_BT601_525_UNADJUSTED:
            return ((ds & HAL_DATASPACE_RANGE_MASK) == HAL_DATASPACE_RANGE_FULL) ?
                DP_PROFILE_FULL_BT601 : DP_PROFILE_BT601;

        case HAL_DATASPACE_STANDARD_BT709:
        case HAL_DATASPACE_STANDARD_DCI_P3:
            if (is_input) {
                return ((ds & HAL_DATASPACE_RANGE_MASK) == HAL_DATASPACE_RANGE_FULL) ?
                    DP_PROFILE_FULL_BT709 : DP_PROFILE_BT709;
            }
            return DP_PROFILE_BT709;

        case HAL_DATASPACE_STANDARD_BT2020:
            if (is_input) {
                return ((ds & HAL_DATASPACE_RANGE_MASK) == HAL_DATASPACE_RANGE_FULL) ?
                    DP_PROFILE_FULL_BT2020 : DP_PROFILE_BT2020;
            }
            return DP_PROFILE_BT709;
    }

    HWC_LOGW("Not support color range(%#x) is_input:%d, use default BT601", ds, is_input);
    return DP_PROFILE_BT601;
}

void BliterNode::calculateAllROI(const uint32_t& job_id, Rect* cal_dst_roi)
{
    std::shared_ptr<JobParam> job_param;
    {
        std::lock_guard<std::mutex> lk(mMutex);
        job_param = m_job_params[job_id];
    }
    if (!job_param)
    {
        LOG_FATAL("%s(), job_param == nullptr", __FUNCTION__);
        return;
    }

    DbgLogger& geo_logger = *m_geometry_logger;
    geo_logger.printf("[NOD] (%" PRIu64 ")geo", m_dpy);

    SrcInvalidateParam& src_param = job_param->src_param;
    DstInvalidateParam& dst_param = job_param->dst_param;
    BufferInfo& src_buf = src_param.bufInfo;
    BufferInfo& dst_buf = dst_param.bufInfo;

    DpRect& src_roi = job_param->src_roi;
    DpRect& dst_roi = job_param->dst_roi;
    DpRect& output_roi = job_param->output_roi;

    calculateROI(&src_roi, &dst_roi, &output_roi, src_param, dst_param, src_buf, dst_buf);

    geo_logger.printf(" D%d/(%d,%d)/xform=%d/(%d,%d,%dx%d)->(%d,%d,%dx%d)/out(%d,%d,%dx%d)/pitch(%d,%d,%d)", 0,
                              dst_buf.rect.getWidth(), dst_buf.rect.getHeight(), dst_param.xform,
                              src_roi.x, src_roi.y, src_roi.w, src_roi.h,
                              dst_roi.x, dst_roi.y, dst_roi.w, dst_roi.h,
                              output_roi.x, output_roi.y, output_roi.w, output_roi.h,
                              dst_buf.pitch, dst_buf.v_pitch, dst_buf.pitch_uv);

    if (cal_dst_roi)
    {
        int32_t padding = m_blit_stream.queryPaddingSide(mapDpOrientation(dst_param.xform));
        calculateContentROI(cal_dst_roi, dst_roi, output_roi, padding);
        geo_logger.printf("/cnt(%d,%d,%d,%d)", cal_dst_roi->left, cal_dst_roi->top,
                                            cal_dst_roi->right, cal_dst_roi->bottom);
    }

    geo_logger.printf(" S(%d,%d)", src_buf.rect.getWidth(), src_buf.rect.getHeight());
    geo_logger.tryFlush();
}

status_t BliterNode::calculateROI(DpRect* src_roi, DpRect* dst_roi, DpRect* output_roi,
                                  const SrcInvalidateParam& src_param, const DstInvalidateParam& dst_param,
                                  const BufferInfo& src_buf, const BufferInfo& dst_buf)
{
    // The crop area of source buffer and destination buffer which format is YUV422
    // or YUV 420 can support odd position and odd width, so we do not need do
    // alignment for it. However, The position and size of write need to do aligment.
    bool dst_x_align_2 = false;
    bool dst_y_align_2 = false;

    // determine destination buffer x alignment
    if (DP_COLOR_GET_H_SUBSAMPLE(dst_buf.dpformat))
    {
        dst_x_align_2 = true;
        dst_y_align_2 = (dst_param.xform & HAL_TRANSFORM_ROT_90) != 0 ? true : dst_y_align_2;
    }
    // determine destination buffer y alignment
    if (DP_COLOR_GET_V_SUBSAMPLE(dst_buf.dpformat))
    {
        dst_y_align_2 = true;
        dst_x_align_2 = (dst_param.xform & HAL_TRANSFORM_ROT_90) != 0 ? true : dst_x_align_2;
    }

    src_roi->x = dst_param.src_crop.left;
    src_roi->y = dst_param.src_crop.top;
    src_roi->w = dst_param.src_crop.getWidth();
    src_roi->h = dst_param.src_crop.getHeight();

    // The writed position of destination buffer is also controlled by crop area.
    // Therefore, we have to do alignment in here.
    dst_roi->x = dst_x_align_2 ?
                 ALIGN_FLOOR(dst_param.dst_crop.left, 2) :
                 dst_param.dst_crop.left;
    dst_roi->y = dst_y_align_2 ?
                 ALIGN_FLOOR(dst_param.dst_crop.top, 2) :
                 dst_param.dst_crop.top;
    dst_roi->w = dst_param.dst_crop.getWidth();
    dst_roi->h = dst_param.dst_crop.getHeight();

    output_roi->x = dst_roi->x;
    output_roi->y = dst_roi->y;
    output_roi->w = dst_x_align_2 ? ALIGN_CEIL_SIGN(dst_roi->w, 2) : dst_roi->w;
    output_roi->h = dst_y_align_2 ? ALIGN_CEIL_SIGN(dst_roi->h, 2) : dst_roi->h;

    if (src_param.deinterlace) src_roi->h /= 2;

    // if src region is out of boundary, should adjust it
    if ((src_roi->x + src_roi->w) > src_buf.rect.getWidth())
    {
        HWC_LOGW("out of boundary src W %d+%d>%d", src_roi->x, src_roi->w, src_buf.rect.getWidth());
        src_roi->w -= 2;
    }

    if ((src_roi->y + src_roi->h) > src_buf.rect.getHeight())
    {
        HWC_LOGW("out of boundary src H %d+%d>%d", src_roi->y, src_roi->h, src_buf.rect.getHeight());
        src_roi->h -= 2;
    }

    // check for OVL limitation
    // if dst region is out of boundary, should adjust it
    if ((dst_roi->x + dst_roi->w) > dst_buf.rect.getWidth() ||
        (output_roi->x + output_roi->w) > dst_buf.rect.getWidth())
    {
        HWC_LOGW("out of boundary dst W dst_roi(%d+%d) output_roi(%d+%d) buffer_width(%d)",
                dst_roi->x, dst_roi->w, output_roi->x, output_roi->w,
                dst_buf.rect.getWidth());
        dst_roi->w -= 2;
        output_roi->w -= 2;
    }

    if ((dst_roi->y + dst_roi->h) > dst_buf.rect.getHeight() ||
        (output_roi->y + output_roi->h) > dst_buf.rect.getHeight())
    {
        HWC_LOGW("out of boundary dst H dst_roi(%d+%d) output_roi(%d+%d) buffer_height(%d)",
                dst_roi->y, dst_roi->h, output_roi->y, output_roi->h,
                dst_buf.rect.getHeight());
        dst_roi->h -= 2;
        output_roi->h -= 2;
    }
    return NO_ERROR;
}

status_t BliterNode::calculateContentROI(Rect* cnt_roi, const DpRect& dst_roi,
         const DpRect& output_roi, int32_t padding)
{
    cnt_roi->left = dst_roi.x;
    cnt_roi->top = dst_roi.y;
    cnt_roi->right = dst_roi.w + dst_roi.x;
    cnt_roi->bottom = dst_roi.h + dst_roi.y;

    bool shift_x = false;
    bool shift_y = false;
    if ((dst_roi.w != output_roi.w) && (padding & DpAsyncBlitStream::PADDING_LEFT))
    {
        shift_x = true;
    }

    if ((dst_roi.h != output_roi.h) && (padding & DpAsyncBlitStream::PADDING_TOP))
    {
        shift_y = true;
    }

    if (shift_x || shift_y)
    {
        cnt_roi->offsetBy((shift_x ? 1 : 0), (shift_y ? 1 : 0));
    }

    return NO_ERROR;
}

status_t BliterNode::invalidate(const uint32_t &job_id,
                                const uint64_t& dispatch_job_id,
                                const hwc2_config_t& active_config,
                                const nsecs_t present_after_ts,
                                const nsecs_t decouple_target_ts,
                                const HWC_2D_BLITER_PROCESSER& blit_processer,
                                int32_t* rel_fence)
{
    HWC_ATRACE_CALL();

    DbgLogger& buf_logger = *m_buffer_logger;
    DbgLogger& cfg_logger = *m_config_logger;

    buf_logger.printf("[NOD] (%" PRIu64 ", %d)", m_dpy, job_id);
    cfg_logger.printf("[NOD] (%" PRIu64 ")cfg", m_dpy);

    std::shared_ptr<JobParam> job_param;
    {
        std::lock_guard<std::mutex> lk(mMutex);
        job_param = m_job_params[job_id];
        m_job_params.erase(job_id);
    }
    if (!job_param)
    {
        LOG_FATAL("%s(), job_param == nullptr", __FUNCTION__);
        return -EINVAL;
    }

    if (NO_ERROR != errorCheck(job_param))
    {
        cancelJobInternal(job_param->job_id, job_param);
        return -EINVAL;
    }

    SrcInvalidateParam& src_param = job_param->src_param;
    DstInvalidateParam& dst_param = job_param->dst_param;
    BufferInfo& src_buf = src_param.bufInfo;
    BufferInfo& dst_buf = dst_param.bufInfo;

    DpSecure dp_secure = src_param.is_secure ? DP_SECURE : DP_SECURE_NONE;
    DP_PROFILE_ENUM in_range = DP_PROFILE_BT601;
    if (src_param.dataspace == HAL_DATASPACE_UNKNOWN &&
        dst_param.dataspace != HAL_DATASPACE_UNKNOWN)
    {
        HWC_LOGV("in_range is UNKNOWN, use out_range(%x) as default, prevent additional conversion",
                 dst_param.dataspace);
        in_range = mapDataspace2DpColorRange(dst_param.dataspace, false);
    }
    else
    {
        in_range = mapDataspace2DpColorRange(src_param.dataspace, true);
    }
#ifdef HWC_PQ_DEMO
    WDT_BL_STREAM(setConfigBegin, blit_processer, job_param->job_id,
                                 static_cast<int32_t>(src_buf.pq_pos),
                                 static_cast<int32_t>(src_buf.pq_orientation));
#else
    WDT_BL_STREAM(setConfigBegin, blit_processer, job_param->job_id);
#endif

    buf_logger.printf(" S/fence=%d", src_buf.fence_fd);

    int src_ion_fd = src_buf.ion_fd;
    WDT_BL_STREAM(setSrcBuffer, blit_processer, src_ion_fd, src_buf.size,
                                src_buf.plane, src_buf.fence_fd);

    buf_logger.printf("/buf ion_fd=%d hand=%p plane:%d", src_ion_fd, src_buf.handle, src_buf.plane);
    src_buf.fence_fd = -1;

    // [NOTE] setSrcConfig provides y and uv pitch configuration
    // if uv pitch is 0, DP would calculate it according to y pitch
    WDT_BL_STREAM(setSrcConfig, blit_processer, src_buf.rect.getWidth(), src_buf.rect.getHeight(),
                                static_cast<int32_t>(src_buf.pitch), static_cast<int32_t>(src_buf.pitch_uv),
                                src_buf.dpformat, in_range,
                                eInterlace_None, dp_secure, src_param.is_flush, src_buf.compression);

    cfg_logger.printf(" flush=%d/in_range=%d/ S/sec=%d/fmt=%d/stream=%p/pq=%d,%d,%d,%d,%d/c=%d",
                        src_param.is_flush, in_range, src_param.is_secure, src_buf.dpformat, &m_blit_stream,
                        src_buf.pq_enable, src_buf.pq_pos, src_buf.pq_orientation, src_buf.pq_table_idx, src_buf.ai_pq_param, src_buf.compression);


    int32_t hwc_to_mdp_rel_fd = SyncFence::merge(
        dst_buf.fence_fd, dst_buf.fence_fd, "HWC_to_MDP_dst_rel");
    ::protectedClose(dst_buf.fence_fd);
    dst_buf.fence_fd = -1;
    buf_logger.printf(" D%d/fence=%d", 0, hwc_to_mdp_rel_fd);
    job_param->hwc_to_mdp_rel_fd = hwc_to_mdp_rel_fd;

    DpSecure dst_dp_secure = dst_param.is_secure ? DP_SECURE : DP_SECURE_NONE;

    // TODO: remove this debug log after comfirm
    if (!isUserLoad())
    {
        std::ostringstream ss;
        getFdInfo(dst_buf.ion_fd, &ss);
        if (ss.str().find("dma") == std::string::npos)
        {
            HWC_LOGE("Dst buffer fd is not dma, but: %s", ss.str().c_str());
        }
    }

    WDT_BL_STREAM(setDstBuffer, blit_processer, 0, dst_buf.ion_fd, dst_buf.size,
                                dst_buf.plane, hwc_to_mdp_rel_fd);
    buf_logger.printf("/buf=%d hand:%p plane:%d", dst_buf.ion_fd, dst_buf.handle, dst_buf.plane);

    hwc_to_mdp_rel_fd = -1;

    DP_PROFILE_ENUM out_range = DP_PROFILE_BT601;
    if (dst_param.dataspace == HAL_DATASPACE_UNKNOWN &&
        src_param.dataspace != HAL_DATASPACE_UNKNOWN)
    {
        HWC_LOGV("out_range is UNKNOWN, use in_range(%x) as default, prevent additional conversion",
                 src_param.dataspace);
        out_range = mapDataspace2DpColorRange(src_param.dataspace, false);
    }
    else
    {
        out_range = mapDataspace2DpColorRange(dst_param.dataspace, false);
    }

    cfg_logger.printf(" D%d/sec=%d/fmt=%d/out_range=%d/c=%d", 0, dst_dp_secure == DP_SECURE ? 1 : 0,
                              dst_buf.dpformat, out_range, dst_buf.compression);

    DpRect& src_roi = job_param->src_roi;
    DpRect& dst_roi = job_param->dst_roi;
    DpRect& output_roi = job_param->output_roi;

    WDT_BL_STREAM(setSrcCrop, blit_processer, 0, src_roi);

    // [NOTE] setDstConfig provides y and uv pitch configuration
    // if uv pitch is 0, DP would calculate it according to y pitch
    // ROI designates the dimension and the position of the bitblited image
    WDT_BL_STREAM(setDstConfig, blit_processer, 0, output_roi.w, output_roi.h,
                                static_cast<int32_t>(dst_buf.pitch), static_cast<int32_t>(dst_buf.pitch_uv),
                                dst_buf.dpformat, out_range, eInterlace_None, &dst_roi, dst_dp_secure, false,
                                dst_buf.compression, static_cast<int32_t>(dst_buf.v_pitch));

    WDT_BL_STREAM(setOrientation, blit_processer, 0, mapDpOrientation(dst_param.xform));

    DpPqParam dppq_param;

    setPQParam(m_dpy, &dppq_param, dst_param.is_enhance, src_param.pool_id,
        dst_param.dataspace, src_param.dataspace,
        src_param.hdr_static_metadata_keys, src_param.hdr_static_metadata_values,
        src_param.hdr_dynamic_metadata, src_param.hdr_scenario,
        src_param.time_stamp, src_param.bufInfo.handle, src_buf.pq_table_idx,
        src_param.is_game,
        src_param.pq_mode_id);

    /* for 8188/8195 platform, set hdr metadata handle to null in EXT_DISP */
    /* let EXT_DISP apply HDR only to prevent duplicate HDR effect */
    if (Platform::getInstance().m_config.ethdr_meta_support && HWCMediator::getInstance().getOvlDevice(m_dpy)->getEthdrSupport(m_dpy))
    {
        memset(&dppq_param.u.video.HDRMetadata, 0, sizeof(PQHDRMetaDataInfo));
    }

    cfg_logger.printf("/en=%d/scen=%d/gamut:src=%d dst=%d param:%d/video_scen=%d/hdr_meta_sz=%u,"
            "%u/pq_mode_id=%d",
        dppq_param.enable, dppq_param.scenario, dppq_param.srcGamut,
        dppq_param.dstGamut, dppq_param.u.video.paramTable, dppq_param.u.video.videoScenario,
        dppq_param.u.video.HDRMetadata.AOSPHDRStaticMetadata.numElements,
        dppq_param.u.video.HDRMetadata.AOSPHDRDynamicMetadata.size, dppq_param.u.video.xmlModeId);
    WDT_BL_STREAM(setPQParameter, blit_processer, 0, dppq_param);

    WDT_BL_STREAM(setConfigEnd, blit_processer);

    buf_logger.tryFlush();
    cfg_logger.tryFlush();

    DP_STATUS_ENUM status = DP_STATUS_RETURN_SUCCESS;

    if (Platform::getInstance().m_config.is_support_mdp_pmqos)
    {
        timeval mdp_finish_time;
        mdp_finish_time.tv_sec = 0;
        mdp_finish_time.tv_usec = 0;
        const nsecs_t refresh = DisplayManager::getInstance().getDisplayData(0, active_config)->refresh;
        timespec mdp_finish_time_ts;
        mdp_finish_time_ts.tv_sec = 0;
        mdp_finish_time_ts.tv_nsec = 0;

        if (NO_ERROR == getHWCExpectMDPFinishedTime(&mdp_finish_time,
                                                    &mdp_finish_time_ts,
                                                    dispatch_job_id,
                                                    refresh,
                                                    present_after_ts,
                                                    decouple_target_ts,
                                                    src_param.is_game || src_param.hdr_scenario == HWC_LAYER_HDR_GAME))
        {
            status = WDT_BL_STREAM_1VAR(invalidate, blit_processer, &mdp_finish_time,
                    &mdp_finish_time_ts);
            job_param->mdp_finish_time = mdp_finish_time_ts;
        }
        else
        {
            status = WDT_BL_STREAM(invalidate, blit_processer);
            job_param->mdp_finish_time.tv_sec = -1;
            job_param->mdp_finish_time.tv_nsec = -1;
        }
    }
    else
    {
        status = WDT_BL_STREAM(invalidate, blit_processer);
    }

    if (HWC_2D_BLITER_PROCESSER_MML == blit_processer && rel_fence)
        *rel_fence = m_mml_blit_stream.getReleaseFence();

    if (DP_STATUS_RETURN_SUCCESS != status)
    {
        NLOGE("errorCheck /blit fail/err=%d/job_id=%d/stream=%p", status,
            job_param->job_id, &m_blit_stream);
        abort();
        return -EINVAL;
    }
    else if (Debugger::m_skip_log != 1)
    {
        job_param->dump(std::string(__FUNCTION__));
    }

    if (Platform::getInstance().m_config.dbg_switch & HWC_DBG_SWITCH_DEBUG_MM_BUFFER_INFO)
    {
        HWC_LOGI("MMPathPassingInfoCheck dispatcher job %d mml job id %d ",
                  job_id, job_param->job_id);
    }
    return NO_ERROR;
}

void BliterNode::createJob(uint32_t &job_id, int32_t &fence,
    const HWC_2D_BLITER_PROCESSER &blit_processer)
{
    job_id = ++m_job_para_id;

    if (HWC_2D_BLITER_PROCESSER_MDP == blit_processer)
    {
        uint32_t mdp_job_id = 0;
        DP_STATUS_ENUM create_state = m_blit_stream.createJob(mdp_job_id, fence);

        std::lock_guard<std::mutex> lk(mMutex);
        m_job_params[job_id] = std::make_shared<JobParam>(mdp_job_id, create_state);
        m_job_params[job_id]->processer = blit_processer;
        if (m_job_params[job_id]->create_state != DP_STATUS_RETURN_SUCCESS)
        {
            NLOGE("%s blit stream createJob failed /job_id=%d/state=%d", __FUNCTION__, job_id, m_job_params[job_id]->create_state);
        }
    }
    else if (HWC_2D_BLITER_PROCESSER_MML == blit_processer)
    {
        std::shared_ptr<JobParam> job_param = std::make_shared<JobParam>(job_id, DP_STATUS_UNKNOWN_ERROR);

        job_param->create_state = m_mml_blit_stream.createJob(job_param->job_id, fence);

        std::lock_guard<std::mutex> lk(mMutex);
        m_job_params[job_id] = job_param;
        m_job_params[job_id]->processer = blit_processer;
        if (m_job_params[job_id]->create_state != DP_STATUS_RETURN_SUCCESS)
        {
            NLOGE("%s blit stream createJob failed /job_id=%d/state=%d", __FUNCTION__, job_id, m_job_params[job_id]->create_state);
        }
    }

}

void BliterNode::setSrc(const uint32_t& job_id,
                        BufferConfig* config,
                        PrivateHandle& src_priv_handle,
                        int* src_fence_fd,
                        const std::vector<int32_t>& hdr_static_metadata_keys,
                        const std::vector<float>& hdr_static_metadata_values,
                        const std::vector<uint8_t>& hdr_dynamic_metadata,
                        const int32_t& hdr_scenario,
                        const bool& is_game,
                        const int32_t& pq_mode_id)
{
    std::shared_ptr<JobParam> job_param;
    {
        std::lock_guard<std::mutex> lk(mMutex);
        job_param = m_job_params[job_id];
    }
    if (!job_param)
    {
        LOG_FATAL("%s(), job_param == nullptr", __FUNCTION__);
        return;
    }

    SrcInvalidateParam& src_param = job_param->src_param;
    BliterNode::BufferInfo& src_buf = src_param.bufInfo;

    src_buf.ion_fd       = src_priv_handle.ion_fd;
    src_buf.sec_handle   = src_priv_handle.sec_handle;
    src_buf.handle       = src_priv_handle.handle;

    if (src_priv_handle.pq_enable)
    {
        src_buf.pq_enable    = src_priv_handle.pq_enable;
        src_buf.pq_pos       = src_priv_handle.pq_pos;
        src_buf.pq_orientation = src_priv_handle.pq_orientation;
        src_buf.pq_table_idx   = src_priv_handle.pq_table_idx;
    }
    else
    {
        src_buf.pq_enable    = 0;
        src_buf.pq_pos       = 0;
        src_buf.pq_orientation = 0;
        src_buf.pq_table_idx   = src_priv_handle.pq_table_idx;
    }
    src_buf.ai_pq_param = (src_priv_handle.ai_pq_info.param != 0);

    if (NULL != src_fence_fd)
        passFenceFd(&src_buf.fence_fd, src_fence_fd);
    else
        src_buf.fence_fd = -1;

    src_buf.dpformat    = config->src_dpformat;
    src_buf.pitch       = config->src_pitch;
    src_buf.pitch_uv    = config->src_pitch_uv;
    src_buf.plane       = config->src_plane;
    src_buf.compression = config->src_compression;
    src_buf.rect        = Rect(config->src_width, config->src_height);
    memcpy(src_buf.size, config->src_size, sizeof(src_buf.size));

    src_param.deinterlace   = config->deinterlace;
    src_param.is_secure     = isSecure(&src_priv_handle);

    src_param.pool_id       = src_priv_handle.ext_info.pool_id;
    src_param.time_stamp    = src_priv_handle.ext_info.timestamp;
    src_param.dataspace     = config->src_dataspace;
    src_param.hdr_static_metadata_keys = hdr_static_metadata_keys;
    src_param.hdr_static_metadata_values = hdr_static_metadata_values;
    src_param.hdr_dynamic_metadata = hdr_dynamic_metadata;
    src_param.hdr_scenario = hdr_scenario;

    src_param.is_flush = false;
    unsigned int producer_type = getGeTypeFromPrivateHandle(&src_priv_handle);
    if ((src_priv_handle.usage & BufferUsage::CPU_WRITE_MASK) &&
        producer_type == GRALLOC_EXTRA_BIT_TYPE_VIDEO &&
        (static_cast<unsigned int>(src_priv_handle.ext_info.status) & GRALLOC_EXTRA_MASK_FLUSH) == GRALLOC_EXTRA_BIT_FLUSH)
    {
        src_param.is_flush = true;
    }
    // after kernel-5.10 with dma buffer, gralloc does not flush cache when user call
    // GraphicBuffer's unlock and unlockAndPost. Therefore, consumer need to flush it
    // by itself.
    else if (isSupportDmaBuf() && producer_type == GRALLOC_EXTRA_BIT_TYPE_CPU &&
            src_priv_handle.usage & BufferUsage::CPU_WRITE_MASK)
    {
        src_param.is_flush = true;
    }

    src_param.is_game = is_game;
    src_param.pq_mode_id = pq_mode_id;
}

void BliterNode::setDst(const uint32_t& job_id,
                        Parameter* param,
                        int ion_fd,
                        SECHAND sec_handle,
                        int* dst_fence_fd)
{
    std::shared_ptr<JobParam> job_param;
    {
        std::lock_guard<std::mutex> lk(mMutex);
        job_param = m_job_params[job_id];
    }
    if (!job_param)
    {
        LOG_FATAL("%s(), job_param == nullptr", __FUNCTION__);
        return;
    }

    DstInvalidateParam& dst_param = job_param->dst_param;

    BufferConfig* config = param->config;
    BliterNode::BufferInfo& dst_buf = dst_param.bufInfo;

    dst_buf.ion_fd      = ion_fd;
    dst_buf.sec_handle  = sec_handle;
    dst_buf.dpformat    = config->dst_dpformat;
    dst_buf.pitch       = config->dst_pitch;
    dst_buf.pitch_uv    = config->dst_pitch_uv;
    dst_buf.v_pitch     = config->dst_v_pitch;
    dst_buf.plane       = config->dst_plane;
    dst_buf.compression = config->dst_compression;
    dst_buf.rect        = Rect(config->dst_width, config->dst_height);
    dst_buf.size[0]     = config->dst_size[0];
    dst_buf.size[1]     = config->dst_size[1];
    dst_buf.size[2]     = config->dst_size[2];

    if (NULL != dst_fence_fd)
        passFenceFd(&dst_buf.fence_fd, dst_fence_fd);
    else
        dst_buf.fence_fd = -1;

    dst_param.xform     = param->xform;
    dst_param.src_crop  = param->src_roi;
    dst_param.dst_crop  = param->dst_roi;
    // enable PQ when feature support, and buffer source type is video

    dst_param.is_enhance = param->pq_enhance;
    dst_param.dataspace     = config->dst_dataspace;
    dst_param.is_secure = param->secure;
}

status_t BliterNode::getHWCExpectMDPFinishedTime(
    timeval* mdp_finish_time,
    timespec* mdp_finish_time_ts,
    const uint64_t& dispatch_job_id,
    const nsecs_t& refresh,
    const nsecs_t present_after_ts,
    const nsecs_t decouple_target_ts,
    const bool& is_game)
{
    const nsecs_t cur_time = systemTime();
    const nsecs_t reserve_exec_time = PMQOS_DISPLAY_DRIVER_EXECUTE_TIME;
    nsecs_t next_vsync_time;
    nsecs_t exec_time;

    if ((Platform::getInstance().m_config.plat_switch & HWC_PLAT_SWITCH_VIRTUAL_DISPLAY_MDP_ASAP) &&
        m_dpy == HWC_DISPLAY_VIRTUAL)
    {
        // TODO: reference sf target ts for virtual display
        next_vsync_time = 0;
        exec_time = 0;
    }
    else if (decouple_target_ts > 0)
    {
        next_vsync_time = present_after_ts + refresh;   // useless
        exec_time = decouple_target_ts - cur_time;
    }
    else
    {
        next_vsync_time = HWVSyncEstimator::getInstance().getNextHWVsync(present_after_ts > 0 ?
                                                                         present_after_ts + PMQOS_VSYNC_TOLERANCE_NS :
                                                                         cur_time + refresh);
        exec_time = next_vsync_time - cur_time - reserve_exec_time;
    }

    if (mdp_finish_time == nullptr || mdp_finish_time_ts == nullptr)
    {
        HWC_LOGW("mdp finish time is null");
        return -EFAULT;
    }

    if (next_vsync_time < 0)
    {
        HWC_LOGW("next_vsync_time < 0");
        return -EFAULT;
    }

    if (exec_time < 0)
    {
        if (Platform::getInstance().m_config.is_support_mdp_pmqos_debug)
        {
            HWC_LOGI("cur_time %" PRId64 ", next_vsync_time %" PRId64 ", present_after_ts %" PRId64,
                     cur_time, next_vsync_time, present_after_ts);
        }
        return -EFAULT;
    }

    if (gettimeofday(mdp_finish_time, NULL) < 0)
    {
        HWC_LOGE("gettimeofday() failure with err:%d", errno);
        return -EFAULT;
    }

    if (Platform::getInstance().m_config.is_support_mdp_pmqos_debug)
    {
        HWC_LOGI("result.tv_sec:%ld result.tv_usec:%ld refresh:%" PRIu64 " is_game:%d",
            mdp_finish_time->tv_sec, mdp_finish_time->tv_usec, refresh, is_game);
    }

    mdp_finish_time_ts->tv_sec = mdp_finish_time->tv_sec;
    mdp_finish_time_ts->tv_nsec = mdp_finish_time->tv_usec * 1000;
    mdp_finish_time_ts->tv_sec += static_cast<long>(exec_time / 1e9);
    mdp_finish_time_ts->tv_nsec += static_cast<long>(std::fmod(exec_time, 1e9));
    if (mdp_finish_time_ts->tv_nsec >= 1e9)
    {
        mdp_finish_time_ts->tv_sec += 1;
        mdp_finish_time_ts->tv_nsec = static_cast<long>(std::fmod(mdp_finish_time_ts->tv_nsec, 1e9));
    }
    mdp_finish_time->tv_sec = mdp_finish_time_ts->tv_sec;
    mdp_finish_time->tv_usec = mdp_finish_time_ts->tv_nsec / 1000;

    if (ATRACE_ENABLED())
    {
        char tag[128];
        memset(tag, '\0', 128);
        if (snprintf(tag, sizeof(tag), "expect MDP exec time:%" PRId64 ".%06" PRId64 "(ms) refresh:%" PRIu64 " is_game:%d",
            exec_time / 1000000, exec_time % 1000000, refresh, is_game) > 0)
        {
            ATRACE_NAME(tag);
        }
    }

    if (Platform::getInstance().m_config.is_support_mdp_pmqos_debug)
    {
        HWC_LOGI("result.tv_sec:%ld result.tv_usec:%ld exec_time:%" PRIu64,
            mdp_finish_time->tv_sec, mdp_finish_time->tv_usec,
            exec_time);

        MDPFrameInfoDebugger::getInstance().setJobHWCConfigMDPTime(dispatch_job_id, cur_time);
        MDPFrameInfoDebugger::getInstance().setJobHWCExpectMDPFinsihTime(dispatch_job_id, next_vsync_time);
    }

    return NO_ERROR;
}

void BliterNode::setLayerID(const uint32_t& portIndex, const uint64_t &layer_id)
{
    m_mml_blit_stream.setLayerID(portIndex, layer_id);
}

void BliterNode::setMMLMode(const int32_t& mode)
{
    m_mml_blit_stream.setMMLMode(mode);
}

void BliterNode::setIsPixelAlphaUsed(bool is_pixel_alpha_used)
{
    m_mml_blit_stream.setIsPixelAlphaUsed(is_pixel_alpha_used);
    return;
}

mml_submit* BliterNode::getMMLSubmit()
{
    return m_mml_blit_stream.getMMLSubmit();
}

void BliterNode::BufferInfo::dump()
{
    std::ostringstream ss;
    ss << "BufferInfo:" << endl;
    ss << "ion_fd: " << ion_fd << ", sec_handle: " << sec_handle << ", handle: " << handle << endl;
    getFdInfo(ion_fd, &ss);
    getBufferName(handle, &ss);
    PrintTo(rect, &ss);
    HWC_LOGI("%s", ss.str().c_str());
    ss.str("");

    ss << "fence_fd: " << fence_fd << endl;
    ss << "dpformat: 0x" << std::hex << dpformat << std::dec << endl;
    ss << "pitch: " << pitch << ", pitch_uv: " << pitch_uv << ", plane: " << plane << endl;
    ss << "size[0~2]: " << size[0] << " " << size[1] << " " << size[2];
    ss << "compression: " << compression;
    HWC_LOGI("%s", ss.str().c_str());
    ss.str("");

    ss << "pq_enable: " << pq_enable;
    ss << ", pq_pos: " << pq_pos;
    ss << ", pq_orientation: " << pq_orientation;
    ss << ", pq_table_idx: " << pq_table_idx;
    ss << ", ai_pq_param: " << ai_pq_param;
    HWC_LOGI("%s", ss.str().c_str());
}

void BliterNode::SrcInvalidateParam::dump()
{
    HWC_LOGI("SrcInvalidateParam:");
    bufInfo.dump();

    std::ostringstream ss;
    ss << "deinterlace: " << deinterlace << endl;
    ss << "is_(s,f,u,g,c): " << is_secure << ",";
    ss << is_flush << ",";
    ss << is_game << ",";
    ss << "pool_id: " << pool_id << endl;
    ss << "time_stamp: " << time_stamp << endl;
    ss << "dataspace: " << dataspace;
    HWC_LOGI("%s", ss.str().c_str());
}

void BliterNode::DstInvalidateParam::dump()
{
    HWC_LOGI("DstInvalidateParam:");
    bufInfo.dump();

    std::ostringstream ss;
    ss << "xform: " << xform << endl;
    PrintTo(src_crop, &ss);
    PrintTo(dst_crop, &ss);
    ss << endl << "is_enhance: " << is_enhance << endl;
    ss << "dataspace: " << dataspace << endl;
    ss << "is_secure: " << is_secure;
    HWC_LOGI("%s", ss.str().c_str());
}

void PrintTo(const DpRect& rect, ::std::ostream* os) {
    *os << "DpRect(" << rect.x << ", " << rect.y << ", " << rect.w << ", " << rect.h << ")";
}

void BliterNode::JobParam::dump(std::string prefix)
{
    HWC_LOGI("%s, job_id: %u", prefix.c_str(), job_id);
    src_param.dump();
    dst_param.dump();

    std::ostringstream ss;
    ss << "src_roi: ";
    PrintTo(src_roi, &ss);
    ss << ", dst_roi: ";
    PrintTo(dst_roi, &ss);
    ss << ", output_roi: ";
    PrintTo(output_roi, &ss);
    ss << endl;

    ss << "hwc_to_mdp_rel_fd: " << hwc_to_mdp_rel_fd << endl;
    ss << "mdp_finish_time: " << mdp_finish_time.tv_sec << "." << std::setfill('0') << std::setw(9)
            << mdp_finish_time.tv_nsec;
    HWC_LOGI("%s", ss.str().c_str());
}
