#define DEBUG_LOG_TAG "drmmodeutils"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include "drmmodeutils.h"
#include <cutils/log.h>
#include <inttypes.h>
#include <drm/drm_fourcc.h>

namespace simplehwc {

uint32_t getDrmBitsPerPixel(uint32_t format)
{
    switch (format)
    {
        case DRM_FORMAT_XRGB8888:
        case DRM_FORMAT_XBGR8888:
        case DRM_FORMAT_RGBX8888:
        case DRM_FORMAT_BGRX8888:
        case DRM_FORMAT_ARGB8888:
        case DRM_FORMAT_ABGR8888:
        case DRM_FORMAT_RGBA8888:
        case DRM_FORMAT_BGRA8888:
        case DRM_FORMAT_XRGB2101010:
        case DRM_FORMAT_XBGR2101010:
        case DRM_FORMAT_RGBX1010102:
        case DRM_FORMAT_BGRX1010102:
        case DRM_FORMAT_ARGB2101010:
        case DRM_FORMAT_ABGR2101010:
        case DRM_FORMAT_RGBA1010102:
        case DRM_FORMAT_BGRA1010102:
            return 32;

        case DRM_FORMAT_RGB888:
        case DRM_FORMAT_BGR888:
            return 24;

        case DRM_FORMAT_RGB565:
        case DRM_FORMAT_BGR565:
        case DRM_FORMAT_YUYV:
        case DRM_FORMAT_YVYU:
        case DRM_FORMAT_UYVY:
        case DRM_FORMAT_VYUY:
        case DRM_FORMAT_YVU422:
        case DRM_FORMAT_YUV422:
            return 16;

        case DRM_FORMAT_NV21:
        case DRM_FORMAT_NV12:
        case DRM_FORMAT_YVU420:
        case DRM_FORMAT_YUV420:
        case DRM_FORMAT_YVU411:
        case DRM_FORMAT_YUV411:
            return 12;

        case DRM_FORMAT_ABGR16161616F:
            return 64;

        case DRM_FORMAT_YVU444:
        case DRM_FORMAT_YUV444:
            return 24;

        default:
            // the frequency of these format are too low
            // process them in other switch
            switch (format)
            {
                case DRM_FORMAT_AYUV:
                    return 32;

                case DRM_FORMAT_NV42:
                case DRM_FORMAT_NV24:
                    return 24;

                case DRM_FORMAT_RG88:
                case DRM_FORMAT_GR88:
                case DRM_FORMAT_XRGB4444:
                case DRM_FORMAT_XBGR4444:
                case DRM_FORMAT_RGBX4444:
                case DRM_FORMAT_BGRX4444:
                case DRM_FORMAT_XRGB1555:
                case DRM_FORMAT_XBGR1555:
                case DRM_FORMAT_RGBX5551:
                case DRM_FORMAT_BGRX5551:
                case DRM_FORMAT_ARGB1555:
                case DRM_FORMAT_ABGR1555:
                case DRM_FORMAT_RGBA5551:
                case DRM_FORMAT_BGRA5551:
                case DRM_FORMAT_NV61:
                case DRM_FORMAT_NV16:
                    return 16;

                case DRM_FORMAT_C8:
                case DRM_FORMAT_R8:
                case DRM_FORMAT_RGB332:
                case DRM_FORMAT_BGR233:
                    return 8;
            }
            break;
    }
    ALOGW("Unknown format(%08x) to get bPP, use default 8", format);
    return 8;
}

uint32_t mapDispColorFormat(unsigned int format)
{
    switch (format)
    {
        case HAL_PIXEL_FORMAT_RGBA_8888:
            return DRM_FORMAT_ABGR8888;

        case HAL_PIXEL_FORMAT_RGBX_8888:
            return DRM_FORMAT_XBGR8888;

        case HAL_PIXEL_FORMAT_BGRA_8888:
            return DRM_FORMAT_ARGB8888;

        case HAL_PIXEL_FORMAT_RGB_888:
            return DRM_FORMAT_BGR888;

        case HAL_PIXEL_FORMAT_RGB_565:
            return DRM_FORMAT_RGB565;

        case HAL_PIXEL_FORMAT_RGBA_1010102:
            return DRM_FORMAT_ABGR2101010;

        case HAL_PIXEL_FORMAT_RGBA_FP16:
            return DRM_FORMAT_ABGR16161616F;

        case HAL_PIXEL_FORMAT_YV12:
            return DRM_FORMAT_YVU420;

        case HAL_PIXEL_FORMAT_YCBCR_422_SP:
            return DRM_FORMAT_NV16;

        case HAL_PIXEL_FORMAT_YCRCB_420_SP:
            return DRM_FORMAT_NV21;

        case HAL_PIXEL_FORMAT_YCBCR_422_I:
            return DRM_FORMAT_YUYV;
    }
    ALOGW("Not support format(%d), use default RGBA8888", format);
    return DRM_FORMAT_ARGB8888;
}

uint32_t mapDispInputColorFormat(unsigned int format)
{
    switch (format)
    {
        case HAL_PIXEL_FORMAT_RGBA_8888:
            return DRM_FORMAT_ABGR8888;

        case HAL_PIXEL_FORMAT_RGBX_8888:
            return DRM_FORMAT_XBGR8888;

        case HAL_PIXEL_FORMAT_BGRA_8888:
            return DRM_FORMAT_ARGB8888;

        case HAL_PIXEL_FORMAT_RGB_888:
            return DRM_FORMAT_BGR888;

        case HAL_PIXEL_FORMAT_RGB_565:
            return DRM_FORMAT_BGR565;

        case HAL_PIXEL_FORMAT_RGBA_1010102:
            return DRM_FORMAT_ABGR2101010;

        case HAL_PIXEL_FORMAT_RGBA_FP16:
            return DRM_FORMAT_ABGR16161616F;

        case HAL_PIXEL_FORMAT_YV12:
        case HAL_PIXEL_FORMAT_YCRCB_420_SP:
            return DRM_FORMAT_YUV422;
    }
    ALOGW("Not support input format(%d), use default RGBA8888", format);
    return DRM_FORMAT_ARGB8888;
}

uint32_t getPlaneNumberOfDispColorFormat(uint32_t format)
{
    switch (format)
    {
        case DRM_FORMAT_ARGB8888:
        case DRM_FORMAT_ABGR8888:
        case DRM_FORMAT_RGBA8888:
        case DRM_FORMAT_BGRA8888:
        case DRM_FORMAT_XRGB8888:
        case DRM_FORMAT_XBGR8888:
        case DRM_FORMAT_RGBX8888:
        case DRM_FORMAT_BGRX8888:
        case DRM_FORMAT_RGB565:
        case DRM_FORMAT_BGR565:
        case DRM_FORMAT_RGB888:
        case DRM_FORMAT_BGR888:
        case DRM_FORMAT_XRGB2101010:
        case DRM_FORMAT_XBGR2101010:
        case DRM_FORMAT_RGBX1010102:
        case DRM_FORMAT_BGRX1010102:
        case DRM_FORMAT_ARGB2101010:
        case DRM_FORMAT_ABGR2101010:
        case DRM_FORMAT_RGBA1010102:
        case DRM_FORMAT_BGRA1010102:
        case DRM_FORMAT_YUYV:
        case DRM_FORMAT_YVYU:
        case DRM_FORMAT_UYVY:
        case DRM_FORMAT_VYUY:
        case DRM_FORMAT_ABGR16161616F:
            return 1;

        case DRM_FORMAT_NV12:
        case DRM_FORMAT_NV21:
            return 2;

        case DRM_FORMAT_YUV420:
        case DRM_FORMAT_YVU420:
        case DRM_FORMAT_YUV422:
        case DRM_FORMAT_YVU422:
        case DRM_FORMAT_YUV444:
        case DRM_FORMAT_YVU444:
            return 3;

        default:
            // the frequency of these format are too low
            // process them in other switch
            switch (format)
            {
                case DRM_FORMAT_C8:
                case DRM_FORMAT_R8:
                case DRM_FORMAT_RG88:
                case DRM_FORMAT_GR88:
                case DRM_FORMAT_RGB332:
                case DRM_FORMAT_BGR233:
                case DRM_FORMAT_XRGB4444:
                case DRM_FORMAT_XBGR4444:
                case DRM_FORMAT_RGBX4444:
                case DRM_FORMAT_BGRX4444:
                case DRM_FORMAT_ARGB4444:
                case DRM_FORMAT_ABGR4444:
                case DRM_FORMAT_RGBA4444:
                case DRM_FORMAT_BGRA4444:
                case DRM_FORMAT_XRGB1555:
                case DRM_FORMAT_XBGR1555:
                case DRM_FORMAT_RGBX5551:
                case DRM_FORMAT_BGRX5551:
                case DRM_FORMAT_ARGB1555:
                case DRM_FORMAT_ABGR1555:
                case DRM_FORMAT_RGBA5551:
                case DRM_FORMAT_BGRA5551:
                case DRM_FORMAT_AYUV:
                    return 1;

                case DRM_FORMAT_NV16:
                case DRM_FORMAT_NV61:
                case DRM_FORMAT_NV24:
                case DRM_FORMAT_NV42:
                    return 2;

                case DRM_FORMAT_YUV410:
                case DRM_FORMAT_YVU410:
                case DRM_FORMAT_YUV411:
                case DRM_FORMAT_YVU411:
                    return 3;
            }
            break;
    }
    ALOGW("Failed to get plane number of color format(%x), return default value(1)", format);

    return 1;
}

uint32_t getHorizontalSubSampleOfDispColorFormat(uint32_t format)
{
    switch (format)
    {
        case DRM_FORMAT_NV12:
        case DRM_FORMAT_NV21:
        case DRM_FORMAT_YUV420:
        case DRM_FORMAT_YVU420:
        case DRM_FORMAT_YUV422:
        case DRM_FORMAT_YVU422:
        case DRM_FORMAT_YUYV:
        case DRM_FORMAT_YVYU:
        case DRM_FORMAT_UYVY:
        case DRM_FORMAT_VYUY:
            return 2;

        case DRM_FORMAT_YUV444:
        case DRM_FORMAT_YVU444:
            return 1;

        case DRM_FORMAT_RGB565:
        case DRM_FORMAT_BGR565:
        case DRM_FORMAT_RGB888:
        case DRM_FORMAT_BGR888:
        case DRM_FORMAT_XRGB8888:
        case DRM_FORMAT_XBGR8888:
        case DRM_FORMAT_RGBX8888:
        case DRM_FORMAT_BGRX8888:
        case DRM_FORMAT_ARGB8888:
        case DRM_FORMAT_ABGR8888:
        case DRM_FORMAT_RGBA8888:
        case DRM_FORMAT_BGRA8888:
        case DRM_FORMAT_XRGB2101010:
        case DRM_FORMAT_XBGR2101010:
        case DRM_FORMAT_RGBX1010102:
        case DRM_FORMAT_BGRX1010102:
        case DRM_FORMAT_ARGB2101010:
        case DRM_FORMAT_ABGR2101010:
        case DRM_FORMAT_RGBA1010102:
        case DRM_FORMAT_BGRA1010102:
        case DRM_FORMAT_ABGR16161616F:
            return 1;

        default:
            // the frequency of these format are too low
            // process them in other switch
            switch (format)
            {
                case DRM_FORMAT_YUV410:
                case DRM_FORMAT_YVU410:
                case DRM_FORMAT_YUV411:
                case DRM_FORMAT_YVU411:
                    return 4;

                case DRM_FORMAT_NV16:
                case DRM_FORMAT_NV61:
                    return 2;

                case DRM_FORMAT_NV24:
                case DRM_FORMAT_NV42:
                case DRM_FORMAT_AYUV:
                    return 1;

                case DRM_FORMAT_C8:
                case DRM_FORMAT_R8:
                case DRM_FORMAT_RG88:
                case DRM_FORMAT_GR88:
                case DRM_FORMAT_RGB332:
                case DRM_FORMAT_BGR233:
                case DRM_FORMAT_XRGB4444:
                case DRM_FORMAT_XBGR4444:
                case DRM_FORMAT_RGBX4444:
                case DRM_FORMAT_BGRX4444:
                case DRM_FORMAT_ARGB4444:
                case DRM_FORMAT_ABGR4444:
                case DRM_FORMAT_RGBA4444:
                case DRM_FORMAT_BGRA4444:
                case DRM_FORMAT_XRGB1555:
                case DRM_FORMAT_XBGR1555:
                case DRM_FORMAT_RGBX5551:
                case DRM_FORMAT_BGRX5551:
                case DRM_FORMAT_ARGB1555:
                case DRM_FORMAT_ABGR1555:
                case DRM_FORMAT_RGBA5551:
                case DRM_FORMAT_BGRA5551:
                    return 1;
            }
            break;
    }
    ALOGW("Failed to get plane number of color format(%x), return default value(1)", format);

    return 1;
}

uint32_t getVerticalSubSampleOfDispColorFormat(uint32_t format)
{
    switch (format)
    {
        case DRM_FORMAT_NV12:
        case DRM_FORMAT_NV21:
        case DRM_FORMAT_YUV420:
        case DRM_FORMAT_YVU420:
            return 2;

        case DRM_FORMAT_YUV422:
        case DRM_FORMAT_YVU422:
        case DRM_FORMAT_YUV444:
        case DRM_FORMAT_YVU444:
            return 1;

        case DRM_FORMAT_YUYV:
        case DRM_FORMAT_YVYU:
        case DRM_FORMAT_UYVY:
        case DRM_FORMAT_VYUY:
            return 1;

        case DRM_FORMAT_RGB565:
        case DRM_FORMAT_BGR565:
        case DRM_FORMAT_RGB888:
        case DRM_FORMAT_BGR888:
        case DRM_FORMAT_XRGB8888:
        case DRM_FORMAT_XBGR8888:
        case DRM_FORMAT_RGBX8888:
        case DRM_FORMAT_BGRX8888:
        case DRM_FORMAT_ARGB8888:
        case DRM_FORMAT_ABGR8888:
        case DRM_FORMAT_RGBA8888:
        case DRM_FORMAT_BGRA8888:
        case DRM_FORMAT_XRGB2101010:
        case DRM_FORMAT_XBGR2101010:
        case DRM_FORMAT_RGBX1010102:
        case DRM_FORMAT_BGRX1010102:
        case DRM_FORMAT_ARGB2101010:
        case DRM_FORMAT_ABGR2101010:
        case DRM_FORMAT_RGBA1010102:
        case DRM_FORMAT_BGRA1010102:
        case DRM_FORMAT_ABGR16161616F:
            return 1;

        default:
            // the frequency of these format are too low
            // process them in other switch
            switch (format)
            {
                case DRM_FORMAT_YUV410:
                case DRM_FORMAT_YVU410:
                    return 4;

                case DRM_FORMAT_NV16:
                case DRM_FORMAT_NV61:
                case DRM_FORMAT_NV24:
                case DRM_FORMAT_NV42:
                case DRM_FORMAT_YUV411:
                case DRM_FORMAT_YVU411:
                    return 2;

                case DRM_FORMAT_AYUV:
                    return 1;

                case DRM_FORMAT_C8:
                case DRM_FORMAT_R8:
                case DRM_FORMAT_RG88:
                case DRM_FORMAT_GR88:
                case DRM_FORMAT_RGB332:
                case DRM_FORMAT_BGR233:
                case DRM_FORMAT_XRGB4444:
                case DRM_FORMAT_XBGR4444:
                case DRM_FORMAT_RGBX4444:
                case DRM_FORMAT_BGRX4444:
                case DRM_FORMAT_ARGB4444:
                case DRM_FORMAT_ABGR4444:
                case DRM_FORMAT_RGBA4444:
                case DRM_FORMAT_BGRA4444:
                case DRM_FORMAT_XRGB1555:
                case DRM_FORMAT_XBGR1555:
                case DRM_FORMAT_RGBX5551:
                case DRM_FORMAT_BGRX5551:
                case DRM_FORMAT_ARGB1555:
                case DRM_FORMAT_ABGR1555:
                case DRM_FORMAT_RGBA5551:
                case DRM_FORMAT_BGRA5551:
                    return 1;
            }
            break;
    }
    ALOGW("Failed to get plane number of color format(%x), return default value(1)", format);

    return 1;
}

}  // namespace simplehwc

