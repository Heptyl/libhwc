#define DEBUG_LOG_TAG "NOD"

#include "mml_asyncblitstream.h"
#include "utils/debug.h"
#include "utils/tools.h"
#include "hwc2.h"
#include <MMLUtil.h>

MMLASyncBlitStream::MMLASyncBlitStream(uint64_t dpy)
    : m_disp_id(dpy)
{
    memset(&m_config, 0, sizeof(mml_submit));
    for (int i = 0; i < MML_MAX_OUTPUTS; ++i)
    {
        m_config.pq_param[i] = new mml_pq_param;
    }
    m_config.job = new mml_job;
    m_config.job->jobid = 0;
    m_config.job->fence = -1;

    m_job_id = NULL;
}

MMLASyncBlitStream::~MMLASyncBlitStream()
{
    for (int i = 0; i < MML_MAX_OUTPUTS; ++i)
    {
        delete m_config.pq_param[i];
    }
    delete m_config.job;
}

DP_STATUS_ENUM MMLASyncBlitStream::setConfigBegin(uint32_t /*job_id*/,
    int32_t /*enhancePos*/, int32_t /*enhanceDir*/)
{
    if (NULL != m_config.pq_param[0])
    {
        // TODO:
        //m_config.pq_param[0]->enhance_pos = static_cast<uint32_t>(enhancePos);
        //m_config.pq_param[0]->enhance_dir = static_cast<uint32_t>(enhanceDir);
    }

    return DP_STATUS_RETURN_SUCCESS;
}

DP_STATUS_ENUM MMLASyncBlitStream::setSrcBuffer(void ** /*pVAList*/, uint32_t * /*pSizeList*/,
     uint32_t /*planeNumber*/, int32_t /*fenceFd*/)
{
    // TODO: secure case
    /*m_config.buffer.src.size[0] = pSizeList[0];
    m_config.buffer.src.size[1] = pSizeList[1];
    m_config.buffer.src.size[2] = pSizeList[2];
    m_config.buffer.src.fence = fenceFd;
    m_config.buffer.src.cnt = 1;*/
    return DP_STATUS_RETURN_SUCCESS;
}

DP_STATUS_ENUM MMLASyncBlitStream::setSrcBuffer(int32_t fileDesc, uint32_t *pSizeList,
    uint32_t planeNumber, int32_t fenceFd)
{
    m_config.buffer.src.fd[0] = fileDesc;
    m_config.buffer.src.fd[1] = -1;
    m_config.buffer.src.fd[2] = -1;
    m_config.buffer.src.size[0] = pSizeList[0];
    m_config.buffer.src.size[1] = pSizeList[1];
    m_config.buffer.src.size[2] = pSizeList[2];
    m_config.buffer.src.fence = fenceFd;
    m_config.buffer.src.cnt = static_cast<uint8_t>(planeNumber);
    m_config.info.src.plane_cnt = static_cast<uint8_t>(planeNumber);
    return DP_STATUS_RETURN_SUCCESS;
}

DP_STATUS_ENUM MMLASyncBlitStream::setSrcConfig(int32_t width, int32_t height, int32_t yPitch, int32_t uvPitch,
    DpColorFormat format, DP_PROFILE_ENUM profile, DpInterlaceFormat /*field*/, DpSecure secure,
    bool doFlush, uint32_t compress)
{
    // info
    m_config.info.src.width = static_cast<uint32_t>(width);
    m_config.info.src.height = static_cast<uint32_t>(height);
    m_config.info.src.y_stride = static_cast<uint32_t>(yPitch);
    m_config.info.src.uv_stride = static_cast<uint32_t>(uvPitch);
    // vert_stride is for 2D BLITER PROCESSER to caluculate AFBC buffer offset
    // when the buffer size > some limitation. Right now,
    // we don't need to fill this up.
    m_config.info.src.vert_stride = 0;
    m_config.info.src.format = static_cast<uint32_t>(format);
    m_config.info.src.profile = static_cast<uint16_t>(profile);
    for (size_t i = 0; i < MML_MAX_PLANES; ++i)
        m_config.info.src.plane_offset[i] = 0;
    m_config.info.src.secure = secure;

    if (compress == 1 && (format != DP_COLOR_NV12_HYFBC && format != DP_COLOR_P010_HYFBC))
    {
        m_config.info.src.modifier = HWCMediator::getInstance().getCompressionModifier();
    }
    else
    {
        m_config.info.src.modifier = 0;
    }

    // buf
    // flush: after cpu modify sync to dma
    m_config.buffer.src.flush = doFlush;
    // invalid: after hw write done, sync back to cpu, used on camera case before
    m_config.buffer.src.invalid = false;
    return DP_STATUS_RETURN_SUCCESS;
}

DP_STATUS_ENUM MMLASyncBlitStream::setSrcCrop(uint32_t portIndex, DpRect roi)
{
    DpRect2MMLCrop(&roi, m_config.info.dest[portIndex].crop);
    return DP_STATUS_RETURN_SUCCESS;
}

DP_STATUS_ENUM MMLASyncBlitStream::setDstBuffer(uint32_t portIndex, void ** /*pVABaseList*/,
    uint32_t *pSizeList, uint32_t planeNumber, int32_t  fenceFd)
{
    m_config.buffer.dest_cnt = 1;
    m_config.buffer.dest[portIndex].size[0] = pSizeList[0];
    m_config.buffer.dest[portIndex].size[1] = pSizeList[1];
    m_config.buffer.dest[portIndex].size[2] = pSizeList[2];
    m_config.buffer.dest[portIndex].fence = fenceFd;
    m_config.buffer.dest[portIndex].cnt = static_cast<uint8_t>(planeNumber);
    return DP_STATUS_RETURN_SUCCESS;
}

// for ION file descriptor
DP_STATUS_ENUM MMLASyncBlitStream::setDstBuffer(uint32_t portIndex, int32_t fileDesc,
    uint32_t *pSizeList, uint32_t planeNumber, int32_t fenceFd)
{
    m_config.buffer.dest_cnt = 1;
    m_config.buffer.dest[portIndex].fd[0] = fileDesc;
    m_config.buffer.dest[portIndex].fd[1] = -1;
    m_config.buffer.dest[portIndex].fd[2] = -1;
    m_config.buffer.dest[portIndex].size[0] = pSizeList[0];
    m_config.buffer.dest[portIndex].size[1] = pSizeList[1];
    m_config.buffer.dest[portIndex].size[2] = pSizeList[2];
    m_config.buffer.dest[portIndex].fence = fenceFd;
    m_config.buffer.dest[portIndex].cnt = static_cast<uint8_t>(planeNumber);
    m_config.info.dest[portIndex].data.plane_cnt = static_cast<uint8_t>(planeNumber);
    for (size_t i = 0; i < MML_MAX_PLANES; ++i)
        m_config.info.dest[portIndex].data.plane_offset[i] = 0;
    return DP_STATUS_RETURN_SUCCESS;
}

DP_STATUS_ENUM MMLASyncBlitStream::setDstConfig(uint32_t portIndex,
    int32_t width, int32_t height, int32_t yPitch, int32_t uvPitch,
    DpColorFormat format, DP_PROFILE_ENUM profile, DpInterlaceFormat /*field*/,
    DpRect *pROI, DpSecure secure, bool doFlush, uint32_t compress, int32_t vertPitch)
{
    m_config.info.dest_cnt = 1;
    m_config.info.dest[portIndex].data.width = static_cast<uint32_t>(width);
    m_config.info.dest[portIndex].data.height = static_cast<uint32_t>(height);
    m_config.info.dest[portIndex].data.y_stride = static_cast<uint32_t>(yPitch);
    m_config.info.dest[portIndex].data.uv_stride = static_cast<uint32_t>(uvPitch);
    m_config.info.dest[portIndex].data.vert_stride = static_cast<uint32_t>(vertPitch);
    m_config.info.dest[portIndex].data.format = static_cast<uint32_t>(format);
    m_config.info.dest[portIndex].data.profile = static_cast<uint16_t>(profile);
    m_config.info.dest[portIndex].data.secure = static_cast<bool>(secure);
    DpRect2MMLRect(pROI, m_config.info.dest[portIndex].compose);

    if (compress == 1)
    {
        m_config.info.dest[portIndex].data.modifier = HWCMediator::getInstance().getCompressionType();
    }
    else
    {
        m_config.info.dest[portIndex].data.modifier = 0;
    }
    // buf
    // flush: after cpu modify sync to dma
    m_config.buffer.dest[portIndex].flush = doFlush;
    // invalid: after hw write done, sync back to cpu, used on camera case before
    m_config.buffer.dest[portIndex].invalid = false;
    return DP_STATUS_RETURN_SUCCESS;
}

DP_STATUS_ENUM MMLASyncBlitStream::setConfigEnd()
{
    m_config.update = false;
    return DP_STATUS_RETURN_SUCCESS;
}

DP_STATUS_ENUM MMLASyncBlitStream::setOrientation(uint32_t portIndex, uint32_t transform)
{
    // Because mml define flip as 1 bit, can't assign var addr
    // use temp var to access
    uint16_t rotate = 0;
    bool flip = false;
    DpFW2MML_Orientation(transform, &rotate, &flip);

    m_config.info.dest[portIndex].rotate = rotate;
    m_config.info.dest[portIndex].flip = flip;

    return DP_STATUS_RETURN_SUCCESS;
}

int MMLASyncBlitStream::refFormatFillPQDestInfoData(uint32_t format, uint32_t index, uint32_t y_stride)
{
    switch (format)
    {
        // DP_COLOR_RGBA8888 define as same as DP_COLOR_RGBX8888
        case DP_COLOR_RGBX8888:
            m_config.info.dest[index].data.y_stride = y_stride;
            m_config.info.dest[index].data.vert_stride = 0;
            m_config.info.dest[index].data.uv_stride = 0;
            m_config.info.dest[index].data.plane_cnt = 1;
            m_config.buffer.dest[index].cnt = 1;
            m_config.buffer.dest[index].size[0] = m_config.info.dest[index].data.y_stride * m_config.info.dest[index].data.height;
            m_config.buffer.dest[index].size[1] = 0;
            m_config.buffer.dest[index].size[2] = 0;
            break;

        case DP_COLOR_BGRA8888:
            m_config.info.dest[index].data.y_stride = y_stride;
            m_config.info.dest[index].data.vert_stride = 0;
            m_config.info.dest[index].data.uv_stride = 0;
            m_config.info.dest[index].data.plane_cnt = 1;
            m_config.buffer.dest[index].cnt = 1;
            m_config.buffer.dest[index].size[0] = m_config.info.dest[index].data.y_stride * m_config.info.dest[index].data.height;
            m_config.buffer.dest[index].size[1] = 0;
            m_config.buffer.dest[index].size[2] = 0;
            break;

        case DP_COLOR_BGR888:
            m_config.info.dest[index].data.y_stride = y_stride;
            m_config.info.dest[index].data.vert_stride = 0;
            m_config.info.dest[index].data.uv_stride = 0;
            m_config.info.dest[index].data.plane_cnt = 1;
            m_config.buffer.dest[index].cnt = 1;
            m_config.buffer.dest[index].size[0] = m_config.info.dest[index].data.y_stride * m_config.info.dest[index].data.height;
            m_config.buffer.dest[index].size[1] = 0;
            m_config.buffer.dest[index].size[2] = 0;
            break;

        case DP_COLOR_YV12:
            m_config.info.dest[index].data.y_stride = y_stride;
            m_config.info.dest[index].data.vert_stride = 0;
            m_config.info.dest[index].data.uv_stride = ALIGN_CEIL((y_stride / 2), 16);
            m_config.info.dest[index].data.plane_cnt = 3;
            m_config.buffer.dest[index].cnt = 3;
            m_config.buffer.dest[index].size[0] = m_config.info.dest[index].data.y_stride * m_config.info.dest[index].data.height;
            m_config.buffer.dest[index].size[1] = m_config.info.dest[index].data.uv_stride * (m_config.info.dest[index].data.height / 2);
            m_config.buffer.dest[index].size[2] = m_config.buffer.dest[index].size[1];
            break;

        case DP_COLOR_RGB888:
            m_config.info.dest[index].data.y_stride = y_stride;
            m_config.info.dest[index].data.vert_stride = 0;
            m_config.info.dest[index].data.uv_stride = 0;
            m_config.info.dest[index].data.plane_cnt = 1;
            m_config.buffer.dest[index].cnt = 1;
            m_config.buffer.dest[index].size[0] = m_config.info.dest[index].data.y_stride * m_config.info.dest[index].data.height;
            m_config.buffer.dest[index].size[1] = 0;
            m_config.buffer.dest[index].size[2] = 0;
            break;

        case DP_COLOR_RGB565:
            m_config.info.dest[index].data.y_stride = y_stride;
            m_config.info.dest[index].data.vert_stride = 0;
            m_config.info.dest[index].data.uv_stride = 0;
            m_config.info.dest[index].data.plane_cnt = 1;
            m_config.buffer.dest[index].cnt = 1;
            m_config.buffer.dest[index].size[0]= m_config.info.dest[index].data.y_stride * m_config.info.dest[index].data.height;
            m_config.buffer.dest[index].size[1] = 0;
            m_config.buffer.dest[index].size[2] = 0;
            break;

        case DP_COLOR_YUYV:
            m_config.info.dest[index].data.y_stride = y_stride;
            m_config.info.dest[index].data.vert_stride = 0;
            m_config.info.dest[index].data.uv_stride = 0;
            m_config.info.dest[index].data.plane_cnt = 1;
            m_config.buffer.dest[index].cnt = 1;
            m_config.buffer.dest[index].size[0] = m_config.info.dest[index].data.y_stride * m_config.info.dest[index].data.height;
            m_config.buffer.dest[index].size[1] = 0;
            m_config.buffer.dest[index].size[2] = 0;
            break;

        default:
            HWC_LOGW("%s format(0x%x) unexpected", __FUNCTION__, format);
            return -EINVAL;
    }
    return 0;
}

void MMLASyncBlitStream::setPQMMLDest(const MMLPQ2ndOutputInfo& pq_dest, uint32_t index)
{
    // copy all info/buffer data from dest[0]
    m_config.info.dest[index] = m_config.info.dest[0];
    m_config.buffer.dest[index] = m_config.buffer.dest[0];

    // move MMLPQ2ndOutputInfo to dest1
    m_config.info.dest_cnt = 2;
    m_config.info.dest[index].data.width = pq_dest.width;
    m_config.info.dest[index].data.height = pq_dest.height;
    m_config.info.dest[index].data.format = pq_dest.format;
    m_config.info.dest[index].data.secure = false;
    m_config.info.dest[index].flip = false;
    m_config.info.dest[index].rotate = false;

    mml_rect pq_rect = {0, 0, pq_dest.width, pq_dest.height};
    m_config.info.dest[index].compose = pq_rect;

    m_config.buffer.dest_cnt = 2;
    m_config.buffer.dest[index].fd[0] = pq_dest.fd;
    m_config.buffer.dest[index].fd[1] = -1;
    m_config.buffer.dest[index].fd[2] = -1;
    m_config.buffer.dest[index].fence = -1;
    refFormatFillPQDestInfoData(pq_dest.format, 1, pq_dest.ystride);
    HWC_LOGV("Get MMLPQ2ndOut,w=%d,h=%d,fmt=%d,fd=%d,stride=%d,size[0]=%d,size[1]=%d,size[2]=%d",
              pq_dest.width, pq_dest.height, pq_dest.format, pq_dest.fd, pq_dest.ystride,
              m_config.buffer.dest[index].size[0], m_config.buffer.dest[index].size[1],
              m_config.buffer.dest[index].size[2]);
    return;
}

DP_STATUS_ENUM MMLASyncBlitStream::setPQParameter(uint32_t portIndex, const DpPqParam & pqParam)
{
    uint32_t pq_mem_id = 0;
    MMLPQ2ndOutputInfo pq_dest;
    PQHDRMetaDataInfo metadata;
    DpFW2MML_pqAll(pqParam, m_config.pq_param[portIndex]);
    //datasoace
    metadata.AOSPHDRDataSpace.dataSpace = pqParam.u.video.HDRMetadata.AOSPHDRDataSpace.dataSpace;
    //statuc metadata
    metadata.AOSPHDRStaticMetadata.numElements = pqParam.u.video.HDRMetadata.AOSPHDRStaticMetadata.numElements;
    metadata.AOSPHDRStaticMetadata.key = pqParam.u.video.HDRMetadata.AOSPHDRStaticMetadata.key;
    metadata.AOSPHDRStaticMetadata.metaData = pqParam.u.video.HDRMetadata.AOSPHDRStaticMetadata.metaData;
    //dynamic metadata
    metadata.AOSPHDRDynamicMetadata.size = pqParam.u.video.HDRMetadata.AOSPHDRDynamicMetadata.size;
    metadata.AOSPHDRDynamicMetadata.byteArray = pqParam.u.video.HDRMetadata.AOSPHDRDynamicMetadata.byteArray;
#ifdef USES_MMLPQ
    MMLPQParamParser::getMMLPQParmaAndEnableConfig(m_config.pq_param[portIndex],
        &(m_config.info.dest[portIndex].pq_config), m_config.info, &metadata, pqParam.u.video.grallocExtraHandle, &pq_mem_id, &pq_dest);
    HWC_LOGV("[DBG] mode : %d, en %d sharp %d",
             m_config.info.mode, m_config.info.dest[portIndex].pq_config.en, m_config.info.dest[portIndex].pq_config.en_sharp);
#endif
    m_config.pq_param[portIndex]->metadata_mem_id = pq_mem_id;

    // If AI SDR to HDR triggered, getMMLPQParmaAndEnableConfig() will set value into MMLPQ2ndOutputInfo.
    // Otherwise, MMLPQ2ndOutputInfo will keep initial value. (fd=-1, width=0, height=0, format=0)
    if (pq_dest.fd == -1 && pq_dest.width == 0 && pq_dest.height == 0 && pq_dest.format == 0)
    {
        // make sure there is no second dest for AI.
        m_config.info.dest_cnt = 1;
        m_config.buffer.dest_cnt = 1;
        return DP_STATUS_RETURN_SUCCESS;
    }

    // If MMLPQ2ndOutputInfo is not initial value,
    // we will move them into dest[1] and set related infomations.
    // dest[0] MML final output
    // dest[1] resize source buffer for AI
    DpFW2MML_pqAll(pqParam, m_config.pq_param[1]);
    m_config.pq_param[1]->metadata_mem_id = pq_mem_id;
    setPQMMLDest(pq_dest, 1);

    return DP_STATUS_RETURN_SUCCESS;
}

DP_STATUS_ENUM MMLASyncBlitStream::setUser(uint32_t /*eID*/)
{
    return DP_STATUS_RETURN_SUCCESS;
}

DP_STATUS_ENUM MMLASyncBlitStream::invalidate(struct timeval *endTime, struct timespec *endTimeTs)
{
    if (NULL != endTimeTs)
    {
        m_config.end.sec = static_cast<uint64_t>(endTimeTs->tv_sec);
        m_config.end.nsec = static_cast<uint64_t>(endTimeTs->tv_nsec);
    }
    else if (NULL != endTime)
    {
        m_config.end.sec = static_cast<uint64_t>(endTime->tv_sec);
        m_config.end.nsec = static_cast<uint64_t>(endTime->tv_usec) * 1000;
    }

    m_config.update = false;
    //IOCTL
    switch (m_config.info.mode)
    {
        case MML_MODE_MML_DECOUPLE:
            HWCMediator::getInstance().getOvlDevice(m_disp_id)->submitMML(m_disp_id, m_config);
            // In MDP, the fd of src or dst buf is closed by BlitStream flow
            // In MML, MML driver will keep fd cnt +1, so after HWC close fd prepare for MML
            if (-1 != m_config.buffer.src.fence)
            {
                ::protectedClose(m_config.buffer.src.fence);
                m_config.buffer.src.fence = -1;
            }

            for (uint32_t output = 0; output < MML_MAX_OUTPUTS; ++output)
            {
                if (output >= m_config.buffer.dest_cnt)
                    continue;

                if (-1 != m_config.buffer.dest[output].fence)
                {
                    ::protectedClose(m_config.buffer.dest[output].fence);
                    m_config.buffer.dest[output].fence = -1;
                }
            }

            // If PQ return MMLPQ2ndOutputInfo for AI SDR to HDR, we need to close dest[1] fd
            // dest[0] -> MML final output
            // dest[1] -> resize for AI model.
            if ((2 == m_config.info.dest_cnt) && (2 == m_config.buffer.dest_cnt))
            {
                if (-1 != m_config.buffer.dest[1].fd[0])
                {
                    ::protectedClose(m_config.buffer.dest[1].fd[0]);
                    m_config.buffer.dest[1].fd[0] = -1;
                }
            }

            if (m_job_id)
            {
                *m_job_id = m_config.job->jobid;
            }

            HWC_LOGD("%s(), jobid %d, fence %d", __FUNCTION__, m_config.job->jobid, m_config.job->fence);

            m_config.job->jobid = 0;

            break;

        case MML_MODE_RACING:
        case MML_MODE_DIRECT_LINK:
            m_config.job->fence = -1;
            break;

        default:
            HWC_LOGE("no match any mode %d",m_config.info.mode);
    }

    return DP_STATUS_RETURN_SUCCESS;
}

int32_t MMLASyncBlitStream::queryPaddingSide(uint32_t /*transform*/)
{
    return 0;
}

DP_STATUS_ENUM MMLASyncBlitStream::createJob(uint32_t &job_id, int32_t & /*fence*/)
{
    m_job_id = &job_id;
    return DP_STATUS_RETURN_SUCCESS;
}

DP_STATUS_ENUM MMLASyncBlitStream::cancelJob(uint32_t /*job_id*/)
{
    return DP_STATUS_RETURN_SUCCESS;
}

void MMLASyncBlitStream::setMMLMode(const int32_t& mode)
{
    switch (mode & HWC_MML_DISP_MODE_MASK)
    {
        case HWC_MML_DISP_DIRECT_LINK_LAYER:
            m_config.info.mode = MML_MODE_DIRECT_LINK;
            break;

        case HWC_MML_DISP_DIRECT_DECOUPLE_LAYER:
            m_config.info.mode = MML_MODE_RACING;
            break;

        case HWC_MML_DISP_DECOUPLE_LAYER:
            m_config.info.mode = MML_MODE_MML_DECOUPLE;
            break;

        default:
            HWC_LOGE("no matched any mml cap %d",m_config.info.mode);

    }
    return;
}

void MMLASyncBlitStream::setLayerID(const uint32_t& port_index, const uint64_t& layer_id)
{
    m_config.pq_param[port_index]->layer_id = static_cast<uint32_t>(layer_id);
}

void MMLASyncBlitStream::setIsPixelAlphaUsed(bool is_pixel_alpha_used)
{
    m_config.info.alpha = is_pixel_alpha_used;
    return;
}

int32_t MMLASyncBlitStream::getReleaseFence()
{
    return m_config.job->fence;
}

void MMLASyncBlitStream::DpRect2MMLRect(DpRect* src, mml_rect& dst)
{
    dst.left = static_cast<uint32_t>(src->x);
    dst.top = static_cast<uint32_t>(src->y);
    dst.width = static_cast<uint32_t>(src->w);
    dst.height = static_cast<uint32_t>(src->h);
}

void MMLASyncBlitStream::DpRect2MMLCrop(DpRect* src, mml_crop& dst)
{
    dst.r.left = static_cast<uint32_t>(src->x);
    dst.r.top = static_cast<uint32_t>(src->y);
    dst.r.width = static_cast<uint32_t>(src->w);
    dst.r.height = static_cast<uint32_t>(src->h);
    dst.x_sub_px = src->sub_x;
    dst.y_sub_px = src->sub_y;
    dst.w_sub_px = src->sub_w;
    dst.h_sub_px = src->sub_h;
}

void MMLASyncBlitStream::DpFW2MML_pqAll(const DpPqParam &pqParam, mml_pq_param *mmlPqParam)
{
    mmlPqParam->enable = pqParam.enable;
    mmlPqParam->scenario = (MEDIA_UNKNOWN == pqParam.scenario) ?
        MML_PQ_MEDIA_UNKNOWN : DpFW2MML_Scenario(pqParam.u.video.videoScenario);
    mmlPqParam->disp_id = pqParam.u.video.dispId;
    mmlPqParam->src_gamut = DpFW2MML_Gamut(pqParam.srcGamut);
    mmlPqParam->dst_gamut = DpFW2MML_Gamut(pqParam.dstGamut);
    mmlPqParam->video_param.video_id = pqParam.u.video.id;
    mmlPqParam->video_param.time_stamp = pqParam.u.video.timeStamp;
    mmlPqParam->video_param.ishdr2sdr = pqParam.u.video.isHDR2SDR;
    mmlPqParam->video_param.param_table = pqParam.u.video.paramTable;
    mmlPqParam->video_param.xml_mode_id = pqParam.u.video.xmlModeId;
    mmlPqParam->user_info = DpFW2MML_UserInfo(pqParam.u.video.userScenario);
}

mml_pq_user_info MMLASyncBlitStream::DpFW2MML_UserInfo(const USER_INFO_SCENARIO_ENUM& val)
{
    mml_pq_user_info ret = MML_PQ_USER_UNKNOWN;
    switch (val)
    {
        case INFO_HWC:
            return MML_PQ_USER_HWC;
        case INFO_GPU :
            return MML_PQ_USER_GPU;
        case INFO_UNKNOWN:
        default:
            return MML_PQ_USER_UNKNOWN;
    }
    return ret;
}

mml_pq_scenario MMLASyncBlitStream::DpFW2MML_Scenario(const VIDEO_INFO_SCENARIO_ENUM& val)
{
    mml_pq_scenario ret = MML_PQ_MEDIA_UNKNOWN;
    switch (val)
    {
        case INFO_GAME :
            return MML_PQ_MEDIA_GAME_NORMAL;
        case INFO_GAMEHDR:
            return MML_PQ_MEDIA_GAME_HDR;
        case INFO_VIDEO:
            return MML_PQ_MEDIA_VIDEO;
        case INFO_HDRVR:
            return MML_PQ_MEDIA_ISP_PREVIEW;
        default:
            return MML_PQ_MEDIA_UNKNOWN;
    }

    return ret;
}

mml_gamut MMLASyncBlitStream::DpFW2MML_Gamut(const DP_GAMUT_ENUM& val)
{
    mml_gamut ret = MML_GAMUT_UNKNOWN;

    switch (val)
    {
        case DP_GAMUT_SRGB :
            return MML_GAMUT_SRGB;
        case DP_GAMUT_DISPLAY_P3:
            return MML_GAMUT_DISPLAY_P3;
        case DP_GAMUT_BT601:
            return MML_GAMUT_BT601;
        case DP_GAMUT_BT709:
            return MML_GAMUT_BT709;
        case DP_GAMUT_BT2020:
            return MML_GAMUT_BT2020;
        default:
            return MML_GAMUT_UNKNOWN;
    }

    return ret;
}

void MMLASyncBlitStream::DpFW2MML_Orientation(uint32_t transform,
                                                uint16_t* rotate, bool* flip)
{
    uint16_t ret_rotate = MML_ROT_0;
    bool ret_flip = false;

    if (transform & DpAsyncBlitStream::ROT_90)
        ret_rotate += MML_ROT_90;

    if (transform & DpAsyncBlitStream::FLIP_H)
        ret_flip = true;

    if (transform & DpAsyncBlitStream::FLIP_V)
    {
        ret_rotate += MML_ROT_180;
        ret_flip = !ret_flip;
    }

    if (rotate)
        *rotate = ret_rotate;
    if (flip)
        *flip = ret_flip;
}

int32_t MMLASyncBlitStream::adjustSrcWidthHeightForFmt(mml_frame_info* query,
                                              DpColorFormat srcFormat, HWCLayer* layer)
{
    if (layer == NULL)
    {
        return 0;
    }

    const PrivateHandle* priv_handle = &layer->getPrivateHandle();
    switch(srcFormat)
    {
        case DP_COLOR_420_BLKP:
        case DP_COLOR_420_BLKP_UFO:
        case DP_COLOR_420_BLKP_10_H:
        case DP_COLOR_420_BLKP_10_V:
        case DP_COLOR_420_BLKP_UFO_10_H:
        case DP_COLOR_420_BLKP_UFO_10_V:
            query->src.width = priv_handle->y_stride;
            query->src.height = priv_handle->vstride;
            HWC_LOGV("Src format %d get W(%d), H(%d) from stride", srcFormat, query->src.width, query->src.height);
            break;
        default:
            HWC_LOGV("Src format %d don't need to adjust H,W", srcFormat);
    }
    return 0;
}

void MMLASyncBlitStream::fillMMLFrameInfo(mml_frame_info* query, uint32_t srcWidth,
                                uint32_t srcHeight, uint32_t dstWidth, uint32_t dstHeight,
                                int32_t Orientation, DpColorFormat srcFormat,
                                DpColorFormat dstFormat, DpPqParam *PqParam,
                                DpRect *srcCrop, uint32_t /*compress*/, HWCLayer* layer, bool isAlphaUsed)
{
    struct mml_pq_param pq_para;
    // Because mml define flip as 1 bit, can't assign var addr
    // use temp var to access
    uint16_t rotate = 0;
    bool flip = false;
    PQHDRMetaDataInfo metadata;

    memset(&pq_para, 0, sizeof(pq_para));

    query->src.width = static_cast<uint32_t>(srcWidth);
    query->src.height = static_cast<uint32_t>(srcHeight);
    query->src.format = static_cast<uint32_t>(srcFormat);
    // We only adjust src info because we set crop(width height) to mml_frame_info.
    // But for some formats, we have to send buffer stride vstride to mml_frame_info.
    // Otherwise, mml_drm_query_cap will return not support because of size invalid.
    adjustSrcWidthHeightForFmt(query, srcFormat, layer);

    query->dest_cnt = 1;
    query->dest[0].data.width = static_cast<uint32_t>(dstWidth);
    query->dest[0].data.height = static_cast<uint32_t>(dstHeight);

    DpFW2MML_Orientation(static_cast<uint32_t>(Orientation), &rotate, &flip);
    query->dest[0].rotate = rotate;
    query->dest[0].flip = flip;
    query->dest[0].data.format = static_cast<uint32_t>(dstFormat);

    DpRect2MMLCrop(srcCrop, query->dest[0].crop);
    DpFW2MML_pqAll(*PqParam, &pq_para);

    //dataspace
    metadata.AOSPHDRDataSpace.dataSpace = PqParam->u.video.HDRMetadata.AOSPHDRDataSpace.dataSpace;
    //static metadata
    metadata.AOSPHDRStaticMetadata.numElements = PqParam->u.video.HDRMetadata.AOSPHDRStaticMetadata.numElements;
    metadata.AOSPHDRStaticMetadata.key = PqParam->u.video.HDRMetadata.AOSPHDRStaticMetadata.key;
    metadata.AOSPHDRStaticMetadata.metaData = PqParam->u.video.HDRMetadata.AOSPHDRStaticMetadata.metaData;
    //dynamic metadata
    metadata.AOSPHDRDynamicMetadata.size = PqParam->u.video.HDRMetadata.AOSPHDRDynamicMetadata.size;
    metadata.AOSPHDRDynamicMetadata.byteArray = PqParam->u.video.HDRMetadata.AOSPHDRDynamicMetadata.byteArray;
#ifdef USES_MMLPQ
    query->mode = MML_MODE_UNKNOWN;
    MMLPQParamParser::getMMLPQParmaAndEnableConfig(&pq_para, &(query->dest[0].pq_config), *query, &metadata,
        PqParam->u.video.grallocExtraHandle);
#endif
    // set alpha
    query->alpha = isAlphaUsed;
}

bool MMLASyncBlitStream::queryHWSupport(uint32_t srcWidth, uint32_t srcHeight,
                                uint32_t dstWidth, uint32_t dstHeight, int32_t Orientation,
                                DpColorFormat srcFormat, DpColorFormat dstFormat,
                                DpPqParam *PqParam, DpRect *srcCrop, uint32_t compress,
                                HWCLayer* layer, bool isAlphaUsed)
{
    mml_frame_info local;
    memset(&local, 0, sizeof(mml_frame_info));
    mml_frame_info* query = &local;

    if (layer != NULL)
    {
        query = layer->getMMLCfg();
    }

    fillMMLFrameInfo(query, srcWidth, srcHeight, dstWidth, dstHeight, Orientation,
                     srcFormat, dstFormat, PqParam, srcCrop, compress, layer, isAlphaUsed);

    return ::queryHWSupport(query);
}

