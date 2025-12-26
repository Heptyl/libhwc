#include "platform_common.h"
#undef DEBUG_LOG_TAG
#define DEBUG_LOG_TAG "PLTC"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include <utility>

#include <cutils/properties.h>

#include <DpAsyncBlitStream2.h>

#include "utils/tools.h"
#include "dispatcher.h"
#include "platform_common.h"
#include "platform_wrap.h"
#include "mml_asyncblitstream.h"
#include "glai_controller.h"
#include "gm/gm_pixel_format.h"

extern unsigned int mapDpOrientation(const uint32_t transform);

void PlatformCommon::initOverlay()
{
}

bool PlatformCommon::isUILayerValid(const sp<HWCLayer>& layer, int32_t* line)
{
    sp<HWCDisplay> disp = layer->getDisplay().promote();
    if (!disp)
    {
        *line = __LINE__;
        return false;
    }

    sp<IOverlayDevice> ovl_dev = HWCMediator::getInstance().getOvlDevice(disp->getId());
    if (ovl_dev == nullptr)
    {
        *line = __LINE__;
        return false;
    }

    const PrivateHandle& priv_hnd = layer->getPrivateHandle();

    if (!ovl_dev->isDisplaySupportedWidthAndHeight(priv_hnd.width, priv_hnd.height))
    {
        *line = __LINE__;
        return false;
    }

    if (isCompressData(&priv_hnd) && !m_config.disp_support_decompress)
    {
        *line = __LINE__;
        return false;
    }

    // filter G2G Compressed Buffer
    if (isG2GCompressData(&priv_hnd))
    {
        *line = __LINE__;
        return false;
    }

    if (layer->isNeedPQ(1))
    {
        HWC_LOGD("PQ id%" PRIu64 " version(%d) size(%d) reg_value(0x%x)",
            layer->getId(),priv_hnd.pq_info.version, priv_hnd.pq_info.value_size, priv_hnd.pq_info.reg_values[0]);
        *line = __LINE__;
        return false;
    }

    if (layer->getHdrScenario() != HWC_LAYER_HDR_NONE)
    {
        *line = __LINE__;
        return false;
    }

    if (priv_hnd.format != HAL_PIXEL_FORMAT_YUYV &&
        priv_hnd.format != HAL_PIXEL_FORMAT_YCbCr_422_I &&
        priv_hnd.format != HAL_PIXEL_FORMAT_IMG1_BGRX_8888 &&
        !(priv_hnd.format == HAL_PIXEL_FORMAT_RGBA_1010102 &&
         m_config.is_ovl_support_RGBA1010102) &&
        (priv_hnd.format < HAL_PIXEL_FORMAT_RGBA_8888 ||
         priv_hnd.format > HAL_PIXEL_FORMAT_BGRA_8888) &&
        priv_hnd.format != HAL_PIXEL_FORMAT_RGB_888)
    {
        *line = __LINE__;
        return false;
    }

    // handle RX HAL 10 bits format
    if ((priv_hnd.ext_info.magic == 0xF0F0) &&
        (priv_hnd.format == HAL_PIXEL_FORMAT_RGBA_1010102))
    {
        HWC_LOGV("Receive RX HAL 10bits buffer, needs handle with MDP.");
        *line = __LINE__;
        return false;
    }

    if ((disp->getId() == HWC_DISPLAY_PRIMARY) && (priv_hnd.format == HAL_PIXEL_FORMAT_NV12_BLK_10BIT_H ||
        priv_hnd.format == HAL_PIXEL_FORMAT_NV12_BLK_10BIT_H_JUMP ||
        priv_hnd.format == HAL_PIXEL_FORMAT_NV12_BLK_10BIT_V ||
        priv_hnd.format == HAL_PIXEL_FORMAT_NV12_BLK_10BIT_V_JUMP ||
        priv_hnd.format == HAL_PIXEL_FORMAT_UFO_10BIT_H ||
        priv_hnd.format == HAL_PIXEL_FORMAT_UFO_10BIT_H_JUMP ||
        priv_hnd.format == HAL_PIXEL_FORMAT_UFO_10BIT_V ||
        priv_hnd.format == HAL_PIXEL_FORMAT_UFO_10BIT_V_JUMP ||
        priv_hnd.format == mediatek::graphics::common::GM_PIXEL_FORMAT_P010_HYFBC))
    {
        HWC_LOGV("force 10 bits video uses MDP covert to ARGB2101010 for primary display");
        *line = __LINE__;
        return false;
    }

    // when Platform does not support 2-subsampled format with odd size of roi, we
    // should not use YUYV. If the roi size of YUYV buffer is odd, ovl shows abnormal
    // screen content. We want to avoid the layer was processed by UI or MM constantly.
    // Therefore we always use MM to process it.
    if (m_config.support_2subsample_with_odd_size_roi == false &&
        (priv_hnd.format == HAL_PIXEL_FORMAT_YUYV ||
         priv_hnd.format == HAL_PIXEL_FORMAT_YCbCr_422_I))
    {
        *line = __LINE__;
        return false;
    }

    switch (layer->getBlend())
    {
        case HWC2_BLEND_MODE_COVERAGE:
            // hw does not support HWC_BLENDING_COVERAGE
            *line = __LINE__;
            return false;

        case HWC2_BLEND_MODE_NONE:
            // opaqaue layer should ignore alpha channel
            if (priv_hnd.format == HAL_PIXEL_FORMAT_BGRA_8888)
            {
                *line = __LINE__;
                return false;
            }
    }

    if (priv_hnd.pq_enable || layer->isAIPQ())
    {
        // This layer must processed by MDP PQ
        return false;
    }

    if (!getHwDevice()->isConstantAlphaForRGBASupported())
    {
        // [NOTE]
        // 1. overlay engine does not support RGBX format
        //    the only exception is that the format is RGBX and the constant alpha is 0xFF
        //    in such a situation, the display driver would disable alpha blending automatically,
        //    treating this format as RGBA with ignoring the undefined alpha channel
        // 2. overlay does not support using constant alpah
        //    and premult blending at same time
        if ((layer->getPlaneAlpha() != 1.0f) &&
            (priv_hnd.format == HAL_PIXEL_FORMAT_RGBX_8888 ||
            priv_hnd.format == HAL_PIXEL_FORMAT_IMG1_BGRX_8888 ||
            layer->getBlend() == HWC2_BLEND_MODE_PREMULTIPLIED))
        {
            *line = __LINE__;
            return false;
        }
    }

    int w = getSrcWidth(layer);
    int h = getSrcHeight(layer);

    // ovl cannot accept <=0
    if (w <= 0 || h <= 0)
    {
        *line = __LINE__;
        return false;
    }

    // [NOTE]
    // Since OVL does not support float crop, adjust coordinate to interger
    // as what SurfaceFlinger did with hwc before version 1.2
    const int src_left = getSrcLeft(layer);
    const int src_top = getSrcTop(layer);

    // cannot handle source negative offset
    if (src_left < 0 || src_top < 0)
    {
        *line = __LINE__;
        return false;
    }

    // switch width and height for prexform with ROT_90
    if (0 != priv_hnd.prexform)
    {
        DbgLogger logger(DbgLogger::TYPE_DUMPSYS, 'I',
            "prexformUI:%d x:%u, prex:%d, f:%d/%d, s:%d/%d",
            m_config.prexformUI, layer->getTransform(), priv_hnd.prexform,
            WIDTH(layer->getDisplayFrame()), HEIGHT(layer->getDisplayFrame()), w, h);

        if (0 == m_config.prexformUI)
        {
            *line = __LINE__;
            return false;
        }

        if (0 != (priv_hnd.prexform & HAL_TRANSFORM_ROT_90))
            SWAP(w, h);
    }

    // cannot handle rotation
    if (layer->getTransform() != priv_hnd.prexform)
    {
        *line = __LINE__;
        return false;
    }

    // for scaling case
    if (WIDTH(layer->getDisplayFrame()) != w || HEIGHT(layer->getDisplayFrame()) != h)
    {
        if (!disp->isRpoSupported())
        {
            *line = __LINE__;
            return false;
        }
        else
        {
            const uint32_t src_crop_width = (layer->getXform() & HAL_TRANSFORM_ROT_90) ?
                                             static_cast<uint32_t>(HEIGHT(layer->getSourceCrop())) :
                                             static_cast<uint32_t>(WIDTH(layer->getSourceCrop()));
            if (src_crop_width > ovl_dev->getRszMaxWidthInput() &&
                (priv_hnd.format == HAL_PIXEL_FORMAT_RGBX_8888 ||
                 priv_hnd.format == HAL_PIXEL_FORMAT_BGRX_8888 ||
                 priv_hnd.format == HAL_PIXEL_FORMAT_RGB_888 ||
                 priv_hnd.format == HAL_PIXEL_FORMAT_RGB_565))
            {
                *line = __LINE__;
                return false;
            }
        }
    }

    if (getHwDevice()->getEthdrSupport(disp->getId()))
    {
        if ((layer->getSFCompositionType() != HWC2_COMPOSITION_CURSOR) &&
            ((w & 0x1) || (h & 0x1)))
        {
            *line = __LINE__;
            return false;
        }

        if (priv_hnd.format == HAL_PIXEL_FORMAT_RGBA_1010102)
        {
            // ethdr mixer not support 10bit RGBA
            *line = __LINE__;
            return false;
        }
    }

    *line = __LINE__;
    return true;
}

bool PlatformCommon::isMMLayerValid(const sp<HWCLayer>& layer, const int32_t pq_mode_id,
        int32_t* line)
{
    sp<HWCDisplay> disp = layer->getDisplay().promote();
    if (!disp)
    {
        *line = __LINE__;
        return false;
    }

    const PrivateHandle& priv_hnd = layer->getPrivateHandle();

    if (layer->getBlend() == HWC2_BLEND_MODE_COVERAGE)
    {
        // only use MM layer without any blending consumption
        *line = __LINE__;
        return false;
    }

    if (!getHwDevice()->isConstantAlphaForRGBASupported())
    {
        // [NOTE]
        // 1. overlay engine does not support RGBX format
        //    the only exception is that the format is RGBX and the constant alpha is 0xFF
        //    in such a situation, the display driver would disable alpha blending automatically,
        //    treating this format as RGBA with ignoring the undefined alpha channel
        // 2. overlay does not support using constant alpah
        //    and premult blending at same time
        if ((layer->getPlaneAlpha() != 1.0f) &&
            (priv_hnd.format == HAL_PIXEL_FORMAT_RGBX_8888 ||
            priv_hnd.format == HAL_PIXEL_FORMAT_IMG1_BGRX_8888 ||
            layer->getBlend() == HWC2_BLEND_MODE_PREMULTIPLIED))
        {
            *line = __LINE__;
            return false;
        }
    }

    const int srcWidth = getSrcWidth(layer);
    const int srcHeight = getSrcHeight(layer);
    const int dstWidth = WIDTH(layer->getDisplayFrame());
    const int dstHeight = HEIGHT(layer->getDisplayFrame());
    if (srcWidth < 4 || srcHeight < 4 ||
        dstWidth < 4 || dstHeight < 4)
    {
        // Prevent bliter error.
        // RGB serise buffer bound with HW limitation, must large than 3x3
        // YUV serise buffer need to prevent width/height align to 0
        *line = __LINE__;
        return false;
    }

    const int srcLeft = getSrcLeft(layer);
    const int srcTop = getSrcTop(layer);
    if (srcLeft < 0 || srcTop < 0)
    {
        // cannot handle source negative offset
        *line = __LINE__;
        return false;
    }

    int curr_private_format = NOT_PRIVATE_FORMAT;
    if (HAL_PIXEL_FORMAT_YUV_PRIVATE == priv_hnd.format ||
        HAL_PIXEL_FORMAT_YCbCr_420_888 == priv_hnd.format ||
        HAL_PIXEL_FORMAT_YUV_PRIVATE_10BIT == priv_hnd.format)
    {
        curr_private_format = (static_cast<unsigned int>(priv_hnd.ext_info.status) & GRALLOC_EXTRA_MASK_CM);
    }
    const unsigned int fmt = grallocColor2HalColor(priv_hnd.format, curr_private_format);
    if (DP_COLOR_UNKNOWN == mapDpFormat(fmt))
    {
        *line = __LINE__;
        return false;
    }

    if ((priv_hnd.format == HAL_PIXEL_FORMAT_RGBA_1010102 && !m_config.is_mdp_support_RGBA1010102) ||
        priv_hnd.format == HAL_PIXEL_FORMAT_RGBA_FP16)
    {
        *line = __LINE__;
        return false;
    }

    bool is_compressed = isCompressData(&priv_hnd);
    if (is_compressed && !m_config.mdp_support_decompress)
    {
        *line = __LINE__;
        return false;
    }

    if (is_compressed && (priv_hnd.format == HAL_PIXEL_FORMAT_RGB_565))
    {
        *line = __LINE__;
        return false;
    }

    int32_t layer_caps = 0;
    if (priv_hnd.format == HAL_PIXEL_FORMAT_RGBA_8888 ||
        priv_hnd.format == HAL_PIXEL_FORMAT_BGRA_8888 ||
        priv_hnd.format == HAL_PIXEL_FORMAT_RGBA_1010102)
    {
        if (!m_config.enable_rgba_rotate)
        {
            // MDP cannot handle RGBA scale and rotate.
            *line = __LINE__;
            return false;
        }

        // MDP doesn't support RGBA scale, it must handle by DISP_RSZ or GLES
        if ((priv_hnd.ext_info.magic == 0xF0F0) && (priv_hnd.format == HAL_PIXEL_FORMAT_RGBA_1010102))
        {
            HWC_LOGV("RGBA2101010 from RX HAL, do not care alpha.");
        }
        else if (layer->needScaling() &&
            !disp->isRpoSupported())
        {
            // Both of MDP and DISP cannot handle RGBA scaling
            *line = __LINE__;
            return false;
        }

        layer_caps |= layer->needRotate() ? HWC_MDP_ROT_LAYER : 0;
        if ((layer_caps & HWC_MDP_ROT_LAYER) &&
            (srcLeft != 0 || srcTop != 0 || srcWidth <= 8))
        {
            // MDP cannot handle RGBA rotate which buffer not align LR corner
            HWC_LOGD("RGBA rotate cannot handle by HWC such src(x,y,w,h)=(%d,%d,%d,%d) dst(w,h)=(%d,%d)",
                        srcLeft, srcTop, srcWidth, srcHeight, dstWidth, dstHeight);
            *line = __LINE__;
            return false;
        }
    }
    else if (priv_hnd.format == HAL_PIXEL_FORMAT_RGBX_8888 ||
             priv_hnd.format == HAL_PIXEL_FORMAT_BGRX_8888 ||
             priv_hnd.format == HAL_PIXEL_FORMAT_RGB_888 ||
             priv_hnd.format == HAL_PIXEL_FORMAT_RGB_565)
    {
        if (layer->needScaling())
        {
            if (!m_config.enable_rgbx_scaling &&
                !disp->isRpoSupported())
            {
                // Both of MDP and DISP cannot handle RGBX scaling
                *line = __LINE__;
                return false;
            }
            layer_caps |= m_config.enable_rgbx_scaling ? HWC_MDP_RSZ_LAYER : 0;
        }

        layer_caps |= layer->needRotate() ? HWC_MDP_ROT_LAYER : 0;
        if ((layer_caps & HWC_MDP_ROT_LAYER) &&
            (srcLeft != 0 || srcTop != 0 || srcWidth <= 8))
        {
            // MDP cannot handle RGBX rotate which buffer not align LR corner
            HWC_LOGD("RGBX rotate cannot handle by HWC such src(x,y,w,h)=(%d,%d,%d,%d) dst(w,h)=(%d,%d)",
                        srcLeft, srcTop, srcWidth, srcHeight, dstWidth, dstHeight);
            *line = __LINE__;
            return false;
        }

        if (!m_config.enable_rgba_rotate)
        {
            // MDP cannot handle RGBX scale and rotate.
            *line = __LINE__;
            return false;
        }
    }
    else
    {
        layer_caps |= layer->needRotate() ? HWC_MDP_ROT_LAYER : 0;
        const double& mdp_scale_percentage = m_config.mdp_scale_percentage;
        if (layer->needScaling() &&
            !(fabs(mdp_scale_percentage - 0.0f) < 0.05f))
        {
            layer_caps |= HWC_MDP_RSZ_LAYER;
        }
    }

    // if it is swdec with HDR content, uses default HDR flow in surfaceflinger
    if (isHdrDataspace(layer->getDataspace()) && layer->getHdrScenario() == HWC_LAYER_HDR_NONE)
    {
        *line = __LINE__;
        return false;
    }

    if (layer->getHdrScenario() != HWC_LAYER_HDR_NONE)
    {
        layer_caps |= HWC_MDP_HDR_LAYER;
    }

    // Because MDP can not process odd width and height, we will calculate the
    // crop area and roi later. Then we may adjust the size of source and
    // destination buffer. This behavior may cause that the scaling rate
    // increases, and therefore the scaling rate is over the limitation of MDP.
    int32_t dst_dataspace = layer->decideMdpOutDataspace();

    uint32_t pq_enhance = 0;
    DpPqParam dppq_param;
    const bool is_game = layer->isNeedPQ();
    setPQEnhance(layer->getPrivateHandle(), &pq_enhance, layer->getHdrScenario(), is_game);
    setPQParam(disp->getId(), &dppq_param, pq_enhance, layer->getPrivateHandle().ext_info.pool_id,
               dst_dataspace, layer->getDataspace(),
               layer->getHdrStaticMetadataKeys(), layer->getHdrStaticMetadataValues(),
               layer->getHdrDynamicMetadata(), layer->getHdrScenario(),
               layer->getPrivateHandle().ext_info.timestamp,
               layer->getPrivateHandle().handle, layer->getPrivateHandle().pq_table_idx, is_game,
               pq_mode_id);

    DpRect src_roi;
    src_roi.x = (int)(layer->getSourceCrop().left);
    src_roi.y = (int)(layer->getSourceCrop().top);
    src_roi.w = static_cast<int32_t>(WIDTH(layer->getSourceCrop()));
    src_roi.h = static_cast<int32_t>(HEIGHT(layer->getSourceCrop()));

    uint32_t compress_state = 0;
    if (is_compressed)
    {
        compress_state = DP_COMPRESS_SET_EN(compress_state);
    }
    if (isG2GCompressData(&priv_hnd))
    {
        compress_state = DP_COMPRESS_SET_G2G(compress_state);
    }

    uint32_t convert_format = layer->decideMdpOutputFormat(*disp);

    bool is_blit_valid =
        queryHWSupport(disp->isSupportMml(),
                    static_cast<uint32_t>(srcWidth),
                    static_cast<uint32_t>(srcHeight),
                    static_cast<uint32_t>(dstWidth),
                    static_cast<uint32_t>(dstHeight),
                    static_cast<int32_t>(mapDpOrientation(layer->getXform())),
                    mapDpFormat(priv_hnd.format),
                    mapDpFormat(convert_format),
                    &dppq_param,
                    &src_roi,
                    compress_state,
                    layer.get());

    HWC_LOGV("blt val %d, Src priv fmt %u dp fmt %u, Dst priv fmt %u dp fmt %u",
             is_blit_valid, priv_hnd.format, mapDpFormat(priv_hnd.format),
             convert_format, mapDpFormat(convert_format));

    // check layer force prop for debug use
    if (m_config.primary_force_pat && (disp->getId() == HWC_DISPLAY_PRIMARY)) {
        if (m_config.primary_force_pat == 1) { // Force to GPU
            HWC_LOGI("force primary disp to GPU by prop");
            *line = __LINE__;
            return false;
        } else if (m_config.primary_force_pat == 2) { // Force to MDP
            HWC_LOGI("force primary disp to MDP by prop");
            *line = __LINE__;
            return true;
        }
    }

    if (m_config.ext_force_pat && (disp->getId() == HWC_DISPLAY_EXTERNAL)) {
        if (m_config.ext_force_pat == 1) { // Force to GPU
            HWC_LOGI("force external disp to GPU by prop");
            *line = __LINE__;
            return false;
        } else if (m_config.ext_force_pat == 2) { // Force to MDP
             HWC_LOGI("force external disp to MDP by prop");
            *line = __LINE__;
            return true;
        }
    }

    if (!is_blit_valid)
    {
        *line = __LINE__;
        return false;
    }

    size_t video_size = (size_t)(srcWidth * srcHeight);
    const int secure = usageHasProtectedOrSecure(priv_hnd.usage);
    //bypass hyfbc check flow when it is secure or hdr, to force to mdp layer
    if ((priv_hnd.format == GM_PIXEL_FORMAT_NV12_HYFBC ||
         priv_hnd.format == GM_PIXEL_FORMAT_P010_HYFBC) &&
         !secure && !priv_hnd.is_hdr)
    {

        // patch to customer, covert 10bits to argb2101010
        if ((disp->getId() == HWC_DISPLAY_PRIMARY) && (priv_hnd.format == HAL_PIXEL_FORMAT_NV12_BLK_10BIT_H ||
            priv_hnd.format == HAL_PIXEL_FORMAT_NV12_BLK_10BIT_H_JUMP ||
            priv_hnd.format == HAL_PIXEL_FORMAT_NV12_BLK_10BIT_V ||
            priv_hnd.format == HAL_PIXEL_FORMAT_NV12_BLK_10BIT_V_JUMP ||
            priv_hnd.format == HAL_PIXEL_FORMAT_UFO_10BIT_H ||
            priv_hnd.format == HAL_PIXEL_FORMAT_UFO_10BIT_H_JUMP ||
            priv_hnd.format == HAL_PIXEL_FORMAT_UFO_10BIT_V ||
            priv_hnd.format == HAL_PIXEL_FORMAT_UFO_10BIT_V_JUMP ||
            priv_hnd.format == mediatek::graphics::common::GM_PIXEL_FORMAT_P010_HYFBC))
        {
            HWC_LOGV("force 10 bits video uses MDP covert to ARGB2101010 for primary display");
        }

        // check video size, if over limit, force to GPU
        // If do not need PQ, force to GPU
        else if (video_size > getLimitedVideoSize() || !pq_enhance)
        {
            *line = __LINE__;
            return false;
        }
        layer_caps |= HWC_MDP_RSZ_LAYER;
    }

    // For some legacy chip, ovl cannot support odd width/height correctly.
    // We should handle odd displayFrame size by client.
    // However, considering for performance and SVP scenarion,
    // ovl will be used for primary display or secure layer.
    if (!m_config.is_ovl_support_odd_size)
    {
        if (disp != nullptr &&
            (disp->getId() != HWC_DISPLAY_PRIMARY) && !secure &&
            (((dstWidth | dstHeight) & 0x01) != 0))
        {
            *line = __LINE__;
            return false;
        }
    }

    if (getHwDevice()->getEthdrSupport(disp->getId()))
    {
        if ((priv_hnd.format == HAL_PIXEL_FORMAT_RGBA_1010102) &&
            (priv_hnd.ext_info.magic != 0xF0F0))
        {
            // ethdr mixer not support 10bit RGBA
            // except buffer from RX HAL will be transformed to P010
            *line = __LINE__;
            return false;
        }
    }

    bool is_disp_valid = true;

    is_disp_valid = (disp->getId() == HWC_DISPLAY_PRIMARY) || secure ||
                    (((dstWidth | dstHeight) & 0x01) != 0);
    is_disp_valid |= (disp->getId() == HWC_DISPLAY_EXTERNAL);

    if (!is_disp_valid)
    {
        *line = __LINE__;
        return false;
    }

    layer->setLayerCaps(layer->getLayerCaps() | layer_caps);
    *line = __LINE__;
    return true;
}

bool PlatformCommon::queryHWSupport(bool is_support_mml, uint32_t srcWidth, uint32_t srcHeight,
                                uint32_t dstWidth, uint32_t dstHeight, int32_t Orientation,
                                DpColorFormat srcFormat, DpColorFormat dstFormat,
                                DpPqParam *PqParam, DpRect *srcCrop, uint32_t compress,
                                HWCLayer* layer)
{
    bool ret = false;

    if (is_support_mml &&
        PqParam->u.video.videoScenario != INFO_GAMEHDR)
    {

        bool is_alpha_used = false;
        if (layer != NULL)
        {
            is_alpha_used = layer->isPixelAlphaUsed();
        }

        HWC_LOGV("queryHWSupport for srcw %d, srch %d, srcFmt %d, dstw %d, dsth %d, dstFmt %d, rot %d, alpha %d",
                  srcWidth, srcHeight, srcFormat, dstWidth, dstHeight, dstFormat, Orientation, is_alpha_used);

        ret =
        MMLASyncBlitStream::queryHWSupport(srcWidth, srcHeight,
                                    dstWidth, dstHeight, Orientation,
                                    srcFormat, dstFormat, PqParam, srcCrop, compress,
                                    layer, is_alpha_used);

        // Only check H subsample for src and dst because
        // once a format set V subsample, it must set H subsample.
        // Thus, it only check if layer set H subsample.
        if (DP_COLOR_GET_H_SUBSAMPLE(dstFormat) || DP_COLOR_GET_H_SUBSAMPLE(srcFormat))
        {
            ret &=
            MMLASyncBlitStream::queryHWSupport(srcWidth - 1, srcHeight - 1,
                                              dstWidth + 2, dstHeight + 2, Orientation,
                                              srcFormat, dstFormat, PqParam, srcCrop, compress,
                                              NULL, is_alpha_used);
        }
    }
    else
    {
        ret =
        DpAsyncBlitStream2::queryHWSupport(srcWidth, srcHeight,
                                    dstWidth, dstHeight, Orientation,
                                    DP_COLOR_UNKNOWN, DP_COLOR_UNKNOWN, PqParam, srcCrop, compress);

        if (DP_COLOR_GET_H_SUBSAMPLE(dstFormat) || DP_COLOR_GET_H_SUBSAMPLE(srcFormat))
        {
            ret &= DpAsyncBlitStream2::queryHWSupport(srcWidth - 1, srcHeight - 1,
                                                      dstWidth + 2, dstHeight + 2, Orientation,
                                                      DP_COLOR_UNKNOWN, DP_COLOR_UNKNOWN, PqParam, srcCrop, compress);
        }
    }
    return ret;
}

bool PlatformCommon::isGlaiLayerValid(const sp<HWCLayer>& layer, int32_t* line)
{
    sp<HWCDisplay> disp = layer->getDisplay().promote();
    if (!disp)
    {
        *line = __LINE__;
        return false;
    }

    sp<IOverlayDevice> ovl_dev = HWCMediator::getInstance().getOvlDevice(disp->getId());
    if (ovl_dev == nullptr)
    {
        *line = __LINE__;
        return false;
    }

    const PrivateHandle& priv_hnd = layer->getPrivateHandle();

    if (priv_hnd.glai_enable <= 0)
    {
        *line = __LINE__;
        return false;
    }

    if (isCompressData(&priv_hnd) && !m_config.disp_support_decompress)
    {
        *line = __LINE__;
        return false;
    }

    // filter G2G Compressed Buffer
    if (isG2GCompressData(&priv_hnd))
    {
        *line = __LINE__;
        return false;
    }

    unsigned int w = priv_hnd.width;
    unsigned int h = priv_hnd.height;

    // switch width and height for prexform with ROT_90
    if (0 != priv_hnd.prexform)
    {
        DbgLogger logger(DbgLogger::TYPE_DUMPSYS, 'I',
            "prexformUI:%d x:%u, prex:%d, f:%d/%d, s:%d/%d",
            m_config.prexformUI, layer->getTransform(), priv_hnd.prexform,
            WIDTH(layer->getDisplayFrame()), HEIGHT(layer->getDisplayFrame()), w, h);

        if (0 == m_config.prexformUI)
        {
            *line = __LINE__;
            return false;
        }

        if (0 != (priv_hnd.prexform & HAL_TRANSFORM_ROT_90))
            SWAP(w, h);
    }

    // cannot handle rotation
    if (layer->getTransform() != priv_hnd.prexform)
    {
        *line = __LINE__;
        return false;
    }

    // check glai valid
    hwc_rect_t out_dst_roi;
    unsigned int out_fmt;
    int agent_id = layer->getGlaiAgentId();
    int val_result = GlaiController::getInstance().isGlaiLayerValid(agent_id,
                                                                    priv_hnd.handle,
                                                                    w,
                                                                    h,
                                                                    priv_hnd.format_original,
                                                                    priv_hnd.y_stride,
                                                                    out_dst_roi,
                                                                    out_fmt);

    if ((val_result & GlaiController::VAL_MODEL_LOADED) != 0)
    {
        layer->setGlaiAgentId(agent_id);
    }

    if ((val_result & GlaiController::VAL_OK) == 0)
    {
        *line = __LINE__;
        HWC_LOGW("%s(), %d, val_result 0x%x", __FUNCTION__, __LINE__, val_result);
        return false;
    }

    if (priv_hnd.glai_inference == false)
    {
        *line = __LINE__;
        return false;
    }

    layer->editGlaiDstRoi() = out_dst_roi;
    layer->setGlaiOutFormat(out_fmt);

    if (out_fmt != HAL_PIXEL_FORMAT_YUYV &&
        out_fmt != HAL_PIXEL_FORMAT_YCbCr_422_I &&
        out_fmt != HAL_PIXEL_FORMAT_IMG1_BGRX_8888 &&
        out_fmt != HAL_PIXEL_FORMAT_RGBA_FP16 &&
        !(out_fmt == HAL_PIXEL_FORMAT_RGBA_1010102 &&
         m_config.is_ovl_support_RGBA1010102) &&
        (out_fmt < HAL_PIXEL_FORMAT_RGBA_8888 ||
         out_fmt > HAL_PIXEL_FORMAT_BGRA_8888))
    {
        *line = __LINE__;
        return false;
    }

    switch (layer->getBlend())
    {
        case HWC2_BLEND_MODE_COVERAGE:
            // hw does not support HWC_BLENDING_COVERAGE
            *line = __LINE__;
            return false;

        case HWC2_BLEND_MODE_NONE:
            // opaqaue layer should ignore alpha channel
            if (out_fmt == HAL_PIXEL_FORMAT_BGRA_8888)
            {
                *line = __LINE__;
                return false;
            }
    }

    if (!getHwDevice()->isConstantAlphaForRGBASupported())
    {
        // [NOTE]
        // 1. overlay engine does not support RGBX format
        //    the only exception is that the format is RGBX and the constant alpha is 0xFF
        //    in such a situation, the display driver would disable alpha blending automatically,
        //    treating this format as RGBA with ignoring the undefined alpha channel
        // 2. overlay does not support using constant alpah
        //    and premult blending at same time
        if ((layer->getPlaneAlpha() != 1.0f) &&
            (out_fmt == HAL_PIXEL_FORMAT_RGBX_8888 ||
            out_fmt == HAL_PIXEL_FORMAT_IMG1_BGRX_8888 ||
            layer->getBlend() == HWC2_BLEND_MODE_PREMULTIPLIED))
        {
            *line = __LINE__;
            return false;
        }
    }

    unsigned int out_w = static_cast<unsigned int>(WIDTH(out_dst_roi));
    unsigned int out_h = static_cast<unsigned int>(HEIGHT(out_dst_roi));

    if (!ovl_dev->isDisplaySupportedWidthAndHeight(out_w, out_h))
    {
        *line = __LINE__;
        HWC_LOGW("%s(), %d", __FUNCTION__, __LINE__);
        return false;
    }

    // check model output size can handle by OVL
    if (WIDTH(layer->getDisplayFrame()) != WIDTH(out_dst_roi) ||
        HEIGHT(layer->getDisplayFrame()) != HEIGHT(out_dst_roi))
    {
        if (!disp->isRpoSupported())
        {
            *line = __LINE__;
            HWC_LOGW("%s(), %d", __FUNCTION__, __LINE__);
            return false;
        }
        else
        {
            const uint32_t src_width = out_w;
            if (src_width > ovl_dev->getRszMaxWidthInput() &&
                (out_fmt == HAL_PIXEL_FORMAT_RGBX_8888 ||
                 out_fmt == HAL_PIXEL_FORMAT_BGRX_8888 ||
                 out_fmt == HAL_PIXEL_FORMAT_RGB_888 ||
                 out_fmt == HAL_PIXEL_FORMAT_RGB_565))
            {
                *line = __LINE__;
                HWC_LOGW("%s(), %d", __FUNCTION__, __LINE__);
                return false;
            }
        }
    }
    *line = __LINE__;
    return true;
}

size_t PlatformCommon::getLimitedVideoSize()
{
    // 4k resolution
    return 3840 * 2160;
}

size_t PlatformCommon::getLimitedExternalDisplaySize()
{
    // 2k resolution
    return 2048 * 1080;
}

void PlatformCommon::updateConfigFromProperty()
{
    size_t num = sizeof(m_plat_switch_list) / sizeof(*m_plat_switch_list);
    for (size_t i = 0; i < num; i++)
    {
        char value[PROPERTY_VALUE_MAX] = {0};
        property_get(m_plat_switch_list[i].property, value, "-1");
        int res = atoi(value);
        if (res != -1)
        {
            if (res == 0)
            {
                m_config.plat_switch &= ~m_plat_switch_list[i].switch_item;
            }
            else
            {
                m_config.plat_switch |= m_plat_switch_list[i].switch_item;
            }
            HWC_LOGI("%s: overwrite plat_switch(0x%08x) with value(0x%08x, value=%s)",
                    __func__, m_plat_switch_list[i].switch_item,
                    m_plat_switch_list[i].switch_item & m_config.plat_switch, value);
        }
    }
}

PlatformCommon::PlatformConfig::PlatformConfig()
    : platform(PLATFORM_NOT_DEFINE)
    , compose_level(COMPOSE_DISABLE_ALL)
    , mirror_state(MIRROR_DISABLED)
    , mir_scale_ratio(0.0f)
    , format_mir_mhl(MIR_FORMAT_UNDEFINE)
    , prexformUI(1)
    , rdma_roi_update(0)
    , force_full_invalidate(false)
    , use_async_bliter_ultra(false)
    , wait_fence_for_display(false)
    , enable_smart_layer(false)
    , enable_rgba_rotate(false)
    , enable_rgbx_scaling(true)
    , av_grouping(true)
    , grouping_type(MTK_AV_GROUPING_SECURE_VP)
    , dump_buf_type('A')
    , dump_buf(0)
    , dump_buf_cont_type('A')
    , dump_buf_cont(0)
    , dump_buf_log_enable(false)
    , fill_black_debug(false)
    , always_setup_priv_hnd(false)
#ifdef MTK_USER_BUILD
    , wdt_trace(false)
#else
    , wdt_trace(true)
#endif
    , only_wfd_by_hwc(false)
    , only_wfd_by_dispdev(false)
    , blitdev_for_virtual(false)
    , is_support_ext_path_for_virtual(true)
    , is_skip_validate(true)
    , support_color_transform(false)
    , mdp_scale_percentage(1.f)
    , extend_mdp_capacity(false)
    , rpo_ui_max_src_width(0)
    , disp_support_decompress(false)
    , mdp_support_decompress(false)
    , mdp_support_compress(false)
    , remove_invisible_layers(true)
    , use_dataspace_for_yuv(false)
    , fill_hwdec_hdr(false)
    , is_support_mdp_pmqos(false)
    , is_support_mdp_pmqos_debug(false)
    , force_pq_index(-1)
    , is_mdp_support_RGBA1010102(false)
    , is_ovl_support_RGBA1010102(false)
    , support_2subsample_with_odd_size_roi(true)
    , enable_mm_buffer_dump(false)
    , dump_ovl_bits(0)
    , dbg_mdp_always_blit(false)
    , dbg_present_delay_time(0)
    , is_client_clear_support(false)
    , is_skip_hrt(true)
    , hint_id(0)
    , hint_name()
    , hint_name_shift(0)
    , hint_hwlayer_type(HWC_LAYER_TYPE_NONE)
    , cache_CT_private_hnd(true)
    , dynamic_switch_path(false)
    , tolerance_time_to_refresh(4 * 1000 * 1000)
    , is_ovl_support_odd_size(true)
    , force_mdp_output_format(0)
    , check_skip_client_color_transform(true)
    , plat_switch(0)
    , dbg_switch(0)
    , mml_switch(true)
    , ethdr_meta_support(false)
    , vir_disp_sup_num(1)
    , histogram_bin_number(32)
    , perf_prefer_below_cpu_mhz(400)
    , perf_reserve_time_for_wait_fence(us2ns(100))
    , is_bw_monitor_support(false)
    , is_smart_composition_support(false)
    , inactive_set_expired_cnt(1000)
    , inactive_set_expired_duration(s2ns(2))
    , bwm_skip_hrt_calc(false)
    , primary_force_pat(0)
    , ext_force_pat(0)
{
    char value[PROPERTY_VALUE_MAX] = {0};
    property_get("vendor.debug.hwc.blitdev_for_virtual", value, "-1");
    const int32_t num_value = atoi(value);
    if (-1 != num_value)
    {
        if (num_value)
        {
            blitdev_for_virtual = true;
            is_support_ext_path_for_virtual = true;
        }
        else
        {
            blitdev_for_virtual = false;
        }
    }

    memset(&cpu_set_index, 0, sizeof(cpu_set_index));
}
