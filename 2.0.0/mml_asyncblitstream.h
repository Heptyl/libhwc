#ifndef MML_ASYNC_BLIT_STREAM_H
#define MML_ASYNC_BLIT_STREAM_H

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreorder-ctor"
#include "DpDataType.h"
#pragma clang diagnostic pop

#include "DpAsyncBlitStream.h"
#include "mtk-mml.h"
#include <MMLPQParserStruct.h>
#include "hwclayer.h"

#ifdef USES_MMLPQ
#include <PQParamParser.h>
#endif

#define PQ_ENHANCE_STEPS        1024

class MMLASyncBlitStream
{
public:
    MMLASyncBlitStream(uint64_t dpy);
    ~MMLASyncBlitStream();
    DP_STATUS_ENUM createJob(uint32_t &job_id, int32_t &fence);
    DP_STATUS_ENUM cancelJob(uint32_t job_id = 0);
    DP_STATUS_ENUM setConfigBegin(uint32_t job_id,
                               int32_t  enhancePos = PQ_ENHANCE_STEPS,
                               int32_t  enhanceDir = 0);
    DP_STATUS_ENUM setSrcBuffer(void     **pVAList,
                             uint32_t *pSizeList,
                             uint32_t planeNumber,
                             int32_t  fenceFd = -1);
    // for ION file descriptor
    DP_STATUS_ENUM setSrcBuffer(int32_t  fileDesc,
                             uint32_t *pSizeList,
                             uint32_t planeNumber,
                             int32_t  fenceFd = -1);
    DP_STATUS_ENUM setSrcConfig(int32_t           width,
                             int32_t           height,
                             int32_t           yPitch,
                             int32_t           uvPitch,
                             DpColorFormat     format,
                             DP_PROFILE_ENUM   profile = DP_PROFILE_BT601,
                             DpInterlaceFormat field   = eInterlace_None,
                             DpSecure          secure  = DP_SECURE_NONE,
                             bool              doFlush = true,
                             uint32_t          compress = 0);
    DP_STATUS_ENUM setSrcCrop(uint32_t portIndex,
                           DpRect   roi);
    DP_STATUS_ENUM setDstBuffer(uint32_t portIndex,
                             void     **pVABaseList,
                             uint32_t *pSizeList,
                             uint32_t planeNumber,
                             int32_t  fenceFd = -1);
    // for ION file descriptor
    DP_STATUS_ENUM setDstBuffer(uint32_t portIndex,
                             int32_t  fileDesc,
                              uint32_t *pSizeList,
                              uint32_t planeNumber,
                              int32_t  fenceFd = -1);
    DP_STATUS_ENUM setDstConfig(uint32_t          portIndex,
                              int32_t           width,
                              int32_t           height,
                              int32_t           yPitch,
                              int32_t           uvPitch,
                              DpColorFormat     format,
                              DP_PROFILE_ENUM   profile = DP_PROFILE_BT601,
                              DpInterlaceFormat field   = eInterlace_None,
                              DpRect            *pROI   = 0,
                              DpSecure          secure  = DP_SECURE_NONE,
                              bool              doFlush = true,
                              uint32_t          compress = 0,
                              int32_t           vertPitch = 0);
    DP_STATUS_ENUM setConfigEnd();
    DP_STATUS_ENUM setOrientation(uint32_t portIndex,
                                uint32_t transform);
    DP_STATUS_ENUM setPQParameter(uint32_t portIndex,
                                const DpPqParam &pqParam);
    DP_STATUS_ENUM setUser(uint32_t eID = 0);
    // The API to trigger MML
    DP_STATUS_ENUM invalidate(struct timeval *endTime = NULL, struct timespec *endTimeTs = NULL);

    int refFormatFillPQDestInfoData(uint32_t format, uint32_t index, uint32_t y_stride);
    void setPQMMLDest(const MMLPQ2ndOutputInfo& pq_dest, uint32_t index);

    static int32_t queryPaddingSide(uint32_t transform);
    void setMMLMode(const int32_t& mode);
    mml_submit* getMMLSubmit() { return &m_config; }
    void setLayerID(const uint32_t& port_index, const uint64_t& layer_id);
    void setIsPixelAlphaUsed(bool is_pixel_alpha_used);
    int32_t getReleaseFence();

    static void DpFW2MML_pqAll(const DpPqParam &pqParam, mml_pq_param *mmlPqParam);
    static void DpRect2MMLCrop(DpRect* src, mml_crop& dst);
    static void DpRect2MMLRect(DpRect* src, mml_rect& dst);
    static mml_pq_user_info DpFW2MML_UserInfo(const USER_INFO_SCENARIO_ENUM& val);
    static mml_pq_scenario DpFW2MML_Scenario(const VIDEO_INFO_SCENARIO_ENUM& val);
    static mml_gamut DpFW2MML_Gamut(const DP_GAMUT_ENUM& val);
    static void DpFW2MML_Orientation(uint32_t transform, uint16_t* rotate, bool* flip);
    static bool queryHWSupport(uint32_t srcWidth,
                                uint32_t srcHeight,
                                uint32_t dstWidth,
                                uint32_t dstHeight,
                                int32_t Orientation = 0,
                                DpColorFormat srcFormat = DP_COLOR_UNKNOWN,
                                DpColorFormat dstFormat = DP_COLOR_UNKNOWN,
                                DpPqParam *PqParam = 0,
                                DpRect *srcCrop = 0,
                                uint32_t compress = 0,
                                HWCLayer* layer = NULL,
                                bool isAlphaUsed = false);
    static void fillMMLFrameInfo(mml_frame_info* query, uint32_t srcWidth,
                                uint32_t srcHeight,
                                uint32_t dstWidth,
                                uint32_t dstHeight,
                                int32_t Orientation,
                                DpColorFormat srcFormat,
                                DpColorFormat dstFormat,
                                DpPqParam *PqParam,
                                DpRect *srcCrop,
                                uint32_t compress,
                                HWCLayer* layer,
                                bool isAlphaUsed);
    static int32_t adjustSrcWidthHeightForFmt(mml_frame_info* query,
                                DpColorFormat srcFormat, HWCLayer* layer);
private:
    // because MML kernel don't want to follow old blitstream API,
    // we need to save the addr set by create
    uint32_t* m_job_id;
    // current config
    struct mml_submit m_config;
    // for ioctl
    uint64_t m_disp_id;
};

#endif // MML_ASYNC_BLIT_STREAM_H
