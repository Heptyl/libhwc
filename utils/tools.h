#ifndef UTILS_TOOLS_H_
#define UTILS_TOOLS_H_

#ifndef DEBUG_LOG_TAG
#define DEBUG_LOG_TAG "TOOL"
#endif // DEBUG_LOG_TAG

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

#include <vector>

#include <utils/Errors.h>
#include <libladder.h>

#include "hwc_ui/GraphicBufferMapper.h"
#include "hwc_priv.h"
#include "ui/gralloc_extra.h"
#include "graphics_mtk_defs.h"
#include "gralloc_mtk_defs.h"
#include <gm/gm_pixel_format.h>
#include "utils/debug.h"
#include "hwc2_defs.h"
#include "dev_interface.h"
#include <hwc_feature_list.h>
#include "mtk-mml.h"
#include "overlay.h"

#include <android/hardware/graphics/common/1.2/types.h>
using android::hardware::graphics::common::V1_2::BufferUsage;
#include <gm/gm_buffer_usage.h>
using namespace mediatek::graphics::common;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreorder-ctor"
#include "DpDataType.h"
#pragma clang diagnostic pop

#define MTK_M4U_MAGICNO 'g'
#define MTK_M4U_T_SEC_INIT _IOW(MTK_M4U_MAGICNO, 50, int)
#define MTK_M4U_GZ_SEC_INIT _IOW(MTK_M4U_MAGICNO, 60, int)

#define LOCK_FOR_HW (BufferUsage::CPU_READ_RARELY | \
                     BufferUsage::CPU_WRITE_NEVER | \
                     BufferUsage::GPU_TEXTURE)

#define SWAP(a, b) do { typeof(a) __a = (a); (a) = (b); (b) = __a; } while (0)

#define WIDTH(rect) ((rect).right - (rect).left)
#define HEIGHT(rect) ((rect).bottom - (rect).top)
#define SIZE(rect) (WIDTH(rect) * HEIGHT(rect))

#define SVPLOGV(label, x, ...) HWC_LOGV(" #SVP [ %s ] " x, label, ##__VA_ARGS__)
#define SVPLOGD(label, x, ...) HWC_LOGD(" #SVP [ %s ] " x, label, ##__VA_ARGS__)
#define SVPLOGE(label, x, ...) HWC_LOGE(" #SVP [ %s ] " x, label, ##__VA_ARGS__)

#define RECTLOG(rect, x, ...) HWC_LOGD("RECT (%4d,%4d,%4d,%4d) - " x, rect.left, rect.top, rect.right, rect.bottom, ##__VA_ARGS__)

#define ALIGN_FLOOR(x,a)    ((x) & ~((a) - 1L))
#define ALIGN_CEIL(x,a)     (((x) + (a) - 1U) & ~((a) - 1U))
#define ALIGN_CEIL_SIGN(x,a)     (((x) + (a) - 1L) & ~((a) - 1L))

#define SYMBOL2ALIGN(x) ((x) == 0) ? 1 : ((x) << 1)

#define NOT_PRIVATE_FORMAT -1

#define HAL_PIXEL_FORMAT_DIM 0x204D4944

#define LOG_MAX_SIZE 512

#define MAX_ABORT_MSG 100
#define BOUNDARY_CHECK_ABORT_MSG(index)                     \
    (index = index < 0 ? 0 :                                \
    (index >= MAX_ABORT_MSG ? MAX_ABORT_MSG - 1 : index))
#define HWC_ABORT_MSG(x, ...)                                           \
        {                                                               \
            if (!isUserLoad())                                           \
            {                                                           \
                if (false) check_args(x, ##__VA_ARGS__);                \
                AbortMessager::getInstance().printf(x, ##__VA_ARGS__);  \
            }                                                           \
        }

#define SIGNAL_TIME_PENDING (std::numeric_limits<nsecs_t>::max())
#define SIGNAL_TIME_INVALID -1

#define CHECK_HDR10_META_VALID(ch) (ch == 16 /*TRANSFER_CHARACTOR_SMPTEST2084*/ || \
                                    ch == 14 /*TRANSFER_CHARACTOR_ITURBT2020_10*/ || \
                                    ch == 18 /*TRANSFER_CHARACTOR_ARIBSTD_B67*/)

class DispatcherJob;

typedef uint32_t SECHAND;

template<class T>
class ObjectWithChangedState
{
private:
    T m_obj;
    bool m_changed;

public:
    ObjectWithChangedState(const T& obj) : m_obj(obj), m_changed(false) {}

    inline void set(const T& obj)
    {
        m_changed = (obj != m_obj);
        m_obj = obj;
    }

    inline T const& get() const { return m_obj; }

    inline bool isChanged() const { return m_changed; };
};

inline void passFenceFd(int* dst_fd, int* src_fd)
{
    if (*dst_fd >= 0 && *dst_fd < 3)
    {
        HWC_LOGE("abort! pass fence dst fd %d", *dst_fd);
        abort();
    }

    if (*src_fd >= 0 && *src_fd < 3)
    {
        HWC_LOGE("abort! pass fence src fd %d", *src_fd);
        abort();
    }

    *dst_fd = *src_fd;
    *src_fd = -1;
}

inline int dupFenceFd(int* fd)
{
    const int32_t dup_fd = (*fd >= 0) ? dup(*fd) : -1;

    if ((3 > dup_fd && -1 < dup_fd) || (3 > *fd && -1 < *fd))
    {
        std::string stack;
        UnwindCurThreadBT(&stack);
        HWC_LOGW("dup Fence fd is zero call stack is (ori:%d dup:%d) %s",*fd ,dup_fd , stack.c_str());
        abort();
    }

    close(*fd);
    *fd = -1;
    return dup_fd;
}

inline void closeFenceFd(int* fd)
{
    if (3 > *fd && -1 < *fd)
    {
        std::string stack;
        UnwindCurThreadBT(&stack);
        HWC_LOGW("Fence fd is zero call stack is %s", stack.c_str());
        abort();
    }
    if (-1 == *fd)
        return;

    close(*fd);
    *fd = -1;
}

inline Rect getFixedRect(hwc_frect_t& src_cropf)
{
    int l = (int)(ceilf(src_cropf.left));
    int t = (int)(ceilf(src_cropf.top));
    int r = (int)(floorf(src_cropf.right));
    int b = (int)(floorf(src_cropf.bottom));
    return Rect(l, t, r, b);
}

inline unsigned int mapGrallocFormat(const unsigned int& format)
{
    switch (format)
    {
        case HAL_PIXEL_FORMAT_RGBA_8888:
        case HAL_PIXEL_FORMAT_BGRA_8888:
            return HAL_PIXEL_FORMAT_RGBA_8888;
        case HAL_PIXEL_FORMAT_RGBX_8888:
        case HAL_PIXEL_FORMAT_BGRX_8888:
        case HAL_PIXEL_FORMAT_IMG1_BGRX_8888:
            return HAL_PIXEL_FORMAT_RGBX_8888;
        case HAL_PIXEL_FORMAT_RGB_888:
            return HAL_PIXEL_FORMAT_RGB_888;
        case HAL_PIXEL_FORMAT_BGR_888:
            return HAL_PIXEL_FORMAT_BGR_888;
        case HAL_PIXEL_FORMAT_RGB_565:
            return HAL_PIXEL_FORMAT_RGB_565;
        case HAL_PIXEL_FORMAT_RGBA_1010102:
        case HAL_PIXEL_FORMAT_YCBCR_P010:
        case GM_PIXEL_FORMAT_P010_HYFBC:
            return HAL_PIXEL_FORMAT_RGBA_1010102;
        case HAL_PIXEL_FORMAT_I420:
        case HAL_PIXEL_FORMAT_YV12:
        case HAL_PIXEL_FORMAT_NV12_BLK:
        case HAL_PIXEL_FORMAT_NV12_BLK_FCM:
        case HAL_PIXEL_FORMAT_YUV_PRIVATE:
        case HAL_PIXEL_FORMAT_UFO:
        case HAL_PIXEL_FORMAT_UFO_AUO:
        case HAL_PIXEL_FORMAT_YCbCr_420_888:
        case HAL_PIXEL_FORMAT_YUV_PRIVATE_10BIT:
        case HAL_PIXEL_FORMAT_NV12_BLK_10BIT_H:
        case HAL_PIXEL_FORMAT_NV12_BLK_10BIT_H_JUMP:
        case HAL_PIXEL_FORMAT_NV12_BLK_10BIT_V:
        case HAL_PIXEL_FORMAT_NV12_BLK_10BIT_V_JUMP:
        case HAL_PIXEL_FORMAT_UFO_10BIT_H:
        case HAL_PIXEL_FORMAT_UFO_10BIT_H_JUMP:
        case HAL_PIXEL_FORMAT_UFO_10BIT_V:
        case HAL_PIXEL_FORMAT_UFO_10BIT_V_JUMP:
        case HAL_PIXEL_FORMAT_YUYV:
        case HAL_PIXEL_FORMAT_YCRCB_420_SP:
        case HAL_PIXEL_FORMAT_YCBCR_422_SP:
        case HAL_PIXEL_FORMAT_YCBCR_422_I:
        case HAL_PIXEL_FORMAT_I420_DI:
        case HAL_PIXEL_FORMAT_YV12_DI:
        case HAL_PIXEL_FORMAT_NV12:
        case GM_PIXEL_FORMAT_NV12_HYFBC:
            return HAL_PIXEL_FORMAT_YUYV;
    }
    HWC_LOGW("mapGrallocFormat: unexpected format(%#x)", format);
    return format;
}

unsigned int convertFormat4Bliter(const unsigned int format);

inline unsigned int getBitsPerPixel(const unsigned int format)
{
    switch (format)
    {
        case HAL_PIXEL_FORMAT_RGBA_8888:
        case HAL_PIXEL_FORMAT_RGBX_8888:
        case HAL_PIXEL_FORMAT_BGRA_8888:
        case HAL_PIXEL_FORMAT_BGRX_8888:
        case HAL_PIXEL_FORMAT_IMG1_BGRX_8888:
        case HAL_PIXEL_FORMAT_RGBA_1010102:
            return 32;

        case HAL_PIXEL_FORMAT_RGB_888:
        case HAL_PIXEL_FORMAT_BGR_888:
        case GM_PIXEL_FORMAT_P010_HYFBC:
        case HAL_PIXEL_FORMAT_YCBCR_P010:
            return 24;

        case HAL_PIXEL_FORMAT_RGB_565:
            return 16;

        case HAL_PIXEL_FORMAT_NV12_BLK_FCM:
        case HAL_PIXEL_FORMAT_NV12_BLK:
        case HAL_PIXEL_FORMAT_I420:
        case HAL_PIXEL_FORMAT_YV12:
        case HAL_PIXEL_FORMAT_YUV_PRIVATE:
        case HAL_PIXEL_FORMAT_YCbCr_422_I:
        case HAL_PIXEL_FORMAT_YUYV:
        case HAL_PIXEL_FORMAT_UFO:
        case HAL_PIXEL_FORMAT_UFO_AUO:
        case HAL_PIXEL_FORMAT_YCbCr_420_888:
        case HAL_PIXEL_FORMAT_YUV_PRIVATE_10BIT:
        case HAL_PIXEL_FORMAT_NV12_BLK_10BIT_H:
        case HAL_PIXEL_FORMAT_NV12_BLK_10BIT_H_JUMP:
        case HAL_PIXEL_FORMAT_NV12_BLK_10BIT_V:
        case HAL_PIXEL_FORMAT_NV12_BLK_10BIT_V_JUMP:
        case HAL_PIXEL_FORMAT_UFO_10BIT_H:
        case HAL_PIXEL_FORMAT_UFO_10BIT_H_JUMP:
        case HAL_PIXEL_FORMAT_UFO_10BIT_V:
        case HAL_PIXEL_FORMAT_UFO_10BIT_V_JUMP:
        case HAL_PIXEL_FORMAT_YCRCB_420_SP:
        case HAL_PIXEL_FORMAT_YCBCR_422_SP:
        case HAL_PIXEL_FORMAT_I420_DI:
        case HAL_PIXEL_FORMAT_YV12_DI:
        case HAL_PIXEL_FORMAT_NV12:
        case GM_PIXEL_FORMAT_NV12_HYFBC:
            return 16;

        default:
            HWC_LOGW("Not support format(%#x) for bpp", format);
            return 0;
    }
}

bool isHdrDataspace(const int32_t dataspace);

inline bool isTransparentFormat(const unsigned int format)
{
    switch (format)
    {
        case HAL_PIXEL_FORMAT_RGBA_8888:
        case HAL_PIXEL_FORMAT_BGRA_8888:
        case HAL_PIXEL_FORMAT_RGBA_1010102:
        case HAL_PIXEL_FORMAT_RGBA_FP16:
            return true;

        case HAL_PIXEL_FORMAT_RGBX_8888:
        case HAL_PIXEL_FORMAT_RGB_888:
        case HAL_PIXEL_FORMAT_BGR_888:
        case HAL_PIXEL_FORMAT_RGB_565:
        case HAL_PIXEL_FORMAT_YV12:
        case HAL_PIXEL_FORMAT_Y8:
        case HAL_PIXEL_FORMAT_Y16:
        case HAL_PIXEL_FORMAT_RAW16:
        case HAL_PIXEL_FORMAT_RAW10:
        case HAL_PIXEL_FORMAT_RAW_OPAQUE:
        case HAL_PIXEL_FORMAT_BLOB:
        case HAL_PIXEL_FORMAT_YCbCr_420_888:
        case HAL_PIXEL_FORMAT_YCbCr_422_SP:
        case HAL_PIXEL_FORMAT_YCrCb_420_SP:
        case HAL_PIXEL_FORMAT_YCbCr_422_I:
        // MTK pixel format
#ifndef MTK_BUILD_IMG_DDK
        case HAL_PIXEL_FORMAT_BGRX_8888:
#endif
        case HAL_PIXEL_FORMAT_I420:
        case HAL_PIXEL_FORMAT_YUV_PRIVATE:
        case HAL_PIXEL_FORMAT_NV12_BLK:
        case HAL_PIXEL_FORMAT_NV12_BLK_FCM:
        case HAL_PIXEL_FORMAT_IMG1_BGRX_8888:
        case HAL_PIXEL_FORMAT_YUYV:
        case HAL_PIXEL_FORMAT_I420_DI:
        case HAL_PIXEL_FORMAT_YV12_DI:
        case HAL_PIXEL_FORMAT_UFO:
        case HAL_PIXEL_FORMAT_UFO_AUO:
        case HAL_PIXEL_FORMAT_YUV_PRIVATE_10BIT:
        case HAL_PIXEL_FORMAT_NV12_BLK_10BIT_H:
        case HAL_PIXEL_FORMAT_NV12_BLK_10BIT_H_JUMP:
        case HAL_PIXEL_FORMAT_NV12_BLK_10BIT_V:
        case HAL_PIXEL_FORMAT_NV12_BLK_10BIT_V_JUMP:
        case HAL_PIXEL_FORMAT_UFO_10BIT_H:
        case HAL_PIXEL_FORMAT_UFO_10BIT_H_JUMP:
        case HAL_PIXEL_FORMAT_UFO_10BIT_V:
        case HAL_PIXEL_FORMAT_UFO_10BIT_V_JUMP:
        case HAL_PIXEL_FORMAT_NV12:
        case HAL_PIXEL_FORMAT_DIM:
        case HAL_PIXEL_FORMAT_YCBCR_P010:
        case GM_PIXEL_FORMAT_NV12_HYFBC:
        case GM_PIXEL_FORMAT_P010_HYFBC:
            return false;

        default:
            HWC_LOGW("Not support format(%#x) for transparent", format);
    }
    return false;
}

inline String8 getSessionModeString(HWC_DISP_MODE mode)
{
    // NOTE: these string literals need to match those in linux/disp_session.h
    switch (mode)
    {
        case HWC_DISP_SESSION_DIRECT_LINK_MODE:
            return String8("DL");

        case HWC_DISP_SESSION_DECOUPLE_MODE:
            return String8("DC");

        case HWC_DISP_SESSION_DIRECT_LINK_MIRROR_MODE:
            return String8("DLM");

        case HWC_DISP_SESSION_DECOUPLE_MIRROR_MODE:
            return String8("DCM");

        case HWC_DISP_INVALID_SESSION_MODE:
            return String8("INV");

        default:
            return String8("N/A");
    }
}

inline String8 getFormatString(const uint32_t& format)
{
    switch (format)
    {
        case HAL_PIXEL_FORMAT_RGBA_8888:
            return String8("rgba");

        case HAL_PIXEL_FORMAT_RGBX_8888:
            return String8("rgbx");

        case HAL_PIXEL_FORMAT_BGRA_8888:
            return String8("bgra");

        case HAL_PIXEL_FORMAT_BGRX_8888:
            return String8("bgra");

        case HAL_PIXEL_FORMAT_IMG1_BGRX_8888:
            return String8("img_bgrx");

        case HAL_PIXEL_FORMAT_RGBA_1010102:
            return String8("rgb10");

        case HAL_PIXEL_FORMAT_RGB_888:
            return String8("rgb");

        case HAL_PIXEL_FORMAT_BGR_888:
            return String8("bgr");

        case HAL_PIXEL_FORMAT_RGB_565:
            return String8("rgb565");

        case HAL_PIXEL_FORMAT_YUYV:
            return String8("yuyv");

        default:
            return String8::format("unknown_%d",format);
    }
}

DP_COLOR_ENUM mapDpFormat(const unsigned int& fmt);

inline unsigned int grallocColor2HalColor(unsigned int fmt, int info_format)
{
    unsigned int hal_format = 0;
    switch (info_format)
    {
        case NOT_PRIVATE_FORMAT:
            hal_format = fmt;
            break;
        case GRALLOC_EXTRA_BIT_CM_YV12:
            hal_format = HAL_PIXEL_FORMAT_YV12;
            break;
        case GRALLOC_EXTRA_BIT_CM_NV12:
            hal_format = HAL_PIXEL_FORMAT_NV12;
            break;
        case GRALLOC_EXTRA_BIT_CM_NV12_BLK:
            hal_format = HAL_PIXEL_FORMAT_NV12_BLK;
            break;
        case GRALLOC_EXTRA_BIT_CM_NV12_BLK_FCM:
            hal_format = HAL_PIXEL_FORMAT_NV12_BLK_FCM;
            break;
        case GRALLOC_EXTRA_BIT_CM_YUYV:
            hal_format = HAL_PIXEL_FORMAT_YCbCr_422_I;
            break;
        case GRALLOC_EXTRA_BIT_CM_I420:
            hal_format = HAL_PIXEL_FORMAT_I420;
            break;
        case GRALLOC_EXTRA_BIT_CM_UFO:
            hal_format = HAL_PIXEL_FORMAT_UFO;
            break;
        case GRALLOC_EXTRA_BIT_CM_NV12_BLK_10BIT_H:
            hal_format = HAL_PIXEL_FORMAT_NV12_BLK_10BIT_H;
            break;
        case GRALLOC_EXTRA_BIT_CM_NV12_BLK_10BIT_V:
            hal_format = HAL_PIXEL_FORMAT_NV12_BLK_10BIT_V;
            break;
        case GRALLOC_EXTRA_BIT_CM_UFO_10BIT_H:
            hal_format = HAL_PIXEL_FORMAT_UFO_10BIT_H;
            break;
        case GRALLOC_EXTRA_BIT_CM_UFO_10BIT_V:
            hal_format = HAL_PIXEL_FORMAT_UFO_10BIT_V;
            break;
        default:
            HWC_LOGE("%s, Gralloc format is invalid (0x%x)", __func__, fmt);
            break;
    }

    return hal_format;
}

bool isUserLoad();

bool isInternalLoad();

bool isSupportDmaBuf();

bool isNoDispatchThread();

struct PrivateHandle
{
    PrivateHandle()
        : ion_fd(-1)
        , fb_mva(0)
        , handle(NULL)
        , sec_handle(0)
        , width(0)
        , height(0)
        , vstride(0)
        , v_align(0)
        , y_stride(0)
        , y_align(0)
        , cbcr_align(0)
        , deinterlace(0)
        , format(0)
        , size(0)
        , usage(0)
        , prexform(0)
        , alloc_id(UINT64_MAX)
        , pq_enable(0)
        , pq_pos(0)
        , pq_orientation(0)
        , pq_table_idx(0)
        , glai_enable(0)
        , glai_inference(0)
        , frame_number(UINT64_MAX)
        , fbt_unchanged(0)
        , is_secure(0)
        , is_hdr(0)
    {
        memset(&pq_info, 0, sizeof(pq_info));
        memset(&ext_info, 0, sizeof(ext_info));
        memset(&hwc_ext_info, 0, sizeof(hwc_ext_info));
        memset(&ai_pq_info, 0, sizeof(ai_pq_info));
    }
    int ion_fd;
    int meta_fd;
    void* fb_mva;
    buffer_handle_t handle;
    SECHAND sec_handle;

    unsigned int width;
    unsigned int height;
    unsigned int vstride;
    unsigned int v_align;
    unsigned int y_stride;
    unsigned int y_align;
    unsigned int cbcr_align;
    unsigned int deinterlace;
    unsigned int format; // init with buffer info, but may be modified in HWC
    unsigned int format_original; // store format from buffer info
    int size; // total bytes allocated by gralloc
    unsigned int usage;

    uint32_t prexform;
    uint64_t alloc_id;

    uint32_t pq_enable;
    uint32_t pq_pos;
    uint32_t pq_orientation;
    uint32_t pq_table_idx;

    int          offset[6];  //four offset: y;cbcr;y-len;c-len,cb,cr, note: cb,cr only used for YUV formats with 3 plane, ex:YV12

    ge_pq_mira_vision_info_t pq_info;
    gralloc_extra_ion_sf_info_t ext_info;
    gralloc_extra_ion_hwc_info_t hwc_ext_info;
    ge_ai_pq_info_t ai_pq_info;

    int glai_enable;    // > 0: init model if not yet init, <=0, deinit model
    int glai_inference; // > 0: need neruo pilot inference
    uint64_t frame_number; // for BW Monitor
    uint32_t fbt_unchanged;// for FBT_cache_hit, 0: fbt buf is changed, 1:fbt buf is unchanged

    unsigned int is_secure;
    unsigned int is_hdr;
    //used for hdr10
    ge_hdr_info_t hdrInfo;

    //used for hdr10plus
    ge_hdr10p_dynamic_metadata_t hdrPlusInfo;
    //meta buffer layout
    gralloc_meta_dma_layout_t meta_layout;

    //used for dollby-vision  hdr
    /*!! check dolby data !!*/
    void * pHDRmetaData; // orginally marked?
};

inline unsigned int getGeTypeFromPrivateHandle(const PrivateHandle* priv_handle)
{
    return static_cast<unsigned int>(priv_handle->ext_info.status) & GRALLOC_EXTRA_MASK_TYPE;
}

int setPrivateHandlePQInfo(const buffer_handle_t& handle, PrivateHandle* priv_handle);

int getPrivateHandleLayerName(const buffer_handle_t& handle, std::string* name);

inline void calculateStride(PrivateHandle* priv_handle)
{
    priv_handle->v_align = 0;
    priv_handle->y_align = 0;
    priv_handle->cbcr_align = 0;

    unsigned int videobuffer_status = static_cast<unsigned int>(priv_handle->ext_info.videobuffer_status);
    if (videobuffer_status & 0x80000000)
    {
        unsigned int align = (videobuffer_status & 0x7FFFFFFF) >> 25;
        align = SYMBOL2ALIGN(align);
        priv_handle->y_stride = ALIGN_CEIL(priv_handle->width, align);
        priv_handle->y_align = align;

        align = (videobuffer_status & 0x01FFFFFF) >> 19;
        align = SYMBOL2ALIGN(align);
        priv_handle->cbcr_align = align;

        align = (videobuffer_status & 0x0007FFFF) >> 13;
        align = SYMBOL2ALIGN(align);
        priv_handle->v_align = align;
        priv_handle->vstride = ALIGN_CEIL(priv_handle->height, align);

        priv_handle->deinterlace = (videobuffer_status & 0x00001000) >> 11;
    }
    else
    {
        unsigned int format = priv_handle->format;
        if (format == HAL_PIXEL_FORMAT_YUV_PRIVATE)
        {
            format = grallocColor2HalColor(format,
                                           static_cast<unsigned int>(priv_handle->ext_info.status) & GRALLOC_EXTRA_MASK_CM);
        }
        if (format == HAL_PIXEL_FORMAT_NV12_BLK  || (format == HAL_PIXEL_FORMAT_UFO || format == HAL_PIXEL_FORMAT_UFO_AUO )
            || format == HAL_PIXEL_FORMAT_YCbCr_420_888)
        {
            HWC_LOGE("%s, cannot interpret buffer layout", __func__);
        }
    }
}

bool usageHasProtected(const unsigned int usage);
bool usageHasSecure(const unsigned int usage);
bool usageHasProtectedOrSecure(const unsigned int usage);
bool usageHasCameraOrientation(const unsigned int usage);

#if PROFILE_MAPPER
int profileMapper(buffer_handle_t handle, PrivateHandle* priv_handle);
#endif

inline bool checkHDR10PlusMetadata(ge_hdr10p_dynamic_metadata_t hdrPlusInfo)
{
    if (hdrPlusInfo.country_code == 0xB5 &&
        hdrPlusInfo.terminal_provider_code == 0x003C &&
        hdrPlusInfo.terminal_provider_oriented_code == 0x0001 &&
        hdrPlusInfo.application_identifier == 4 &&
        hdrPlusInfo.application_version == 1)
        return true;
    else
        return false;
}

inline int getPrivateHandleInfo(
    buffer_handle_t handle, PrivateHandle* priv_handle, std::string* name, const bool& is_outbuffer = false)
{
    if (NULL == handle)
    {
        HWC_LOGE("%s NULL handle !!!!!", __func__);
        return -EINVAL;
    }
    priv_handle->handle = handle;

#if PROFILE_MAPPER
    profileMapper(handle, priv_handle);
#endif
    int err = 0;
    int value_int = 0;

    err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_WIDTH, &priv_handle->width);
    err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_HEIGHT, &priv_handle->height);
    err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_STRIDE, &priv_handle->y_stride);
    err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_VERTICAL_STRIDE, &priv_handle->vstride);
    err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_FORMAT, &priv_handle->format);
    priv_handle->format_original = priv_handle->format;
    err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_ALLOC_SIZE, &priv_handle->size);
    err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_USAGE, &value_int);
    priv_handle->usage = static_cast<unsigned int>(value_int);
    if (HwcFeatureList::getInstance().getFeature().game_pq)
    {
        err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_PQ_MIRA_VISION_INFO, &priv_handle->pq_info);
    }
    err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_IOCTL_ION_SF_INFO, &priv_handle->ext_info);
    if (is_outbuffer)
    {
        err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_HWC_INFO, &priv_handle->hwc_ext_info);
    }

    if (priv_handle->vstride == 0)
    {
        priv_handle->vstride = priv_handle->height;
    }

    if (usageHasCameraOrientation(priv_handle->usage))
    {
        err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_ORIENTATION, &priv_handle->prexform);

        if (0 != (HAL_TRANSFORM_ROT_90 & priv_handle->prexform))
        {
            SWAP(priv_handle->width, priv_handle->height);
            err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_BYTE_2ND_STRIDE, &priv_handle->y_stride);
            err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_VERTICAL_2ND_STRIDE, &priv_handle->vstride);
        }
    }
    else
    {
        priv_handle->prexform = 0;
    }

    err |= getPrivateHandleLayerName(handle, name);

    if (err) HWC_LOGE("%s err(%d), (handle=%p)", __func__, err, handle);

    calculateStride(priv_handle);
    return err;
}

inline int getPrivateHandleInfoModifyPerFrame(
    buffer_handle_t handle, PrivateHandle* priv_handle)
{
    int err = 0;

    if (HwcFeatureList::getInstance().getFeature().game_pq >= 2)
    {
        err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_PQ_MIRA_VISION_INFO, &priv_handle->pq_info);
    }

    if (HwcFeatureList::getInstance().getFeature().has_glai)
    {
        ge_nn_model_info_t nn_model_info;

        err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_NN_MODEL_INFO, &nn_model_info);
        if (priv_handle->glai_enable != nn_model_info.enable)
        {
            HWC_LOGI("glai enable %d, inf %d", nn_model_info.enable, nn_model_info.inference);
        }
        priv_handle->glai_enable = nn_model_info.enable;
        priv_handle->glai_inference = nn_model_info.inference;
    }

    if (err) HWC_LOGE("%s err(%d), (handle=%p), (priv_handle:%p)", __func__, err, handle, priv_handle);

    priv_handle->handle = handle;

    return  err;
}

int getPrivateHandleFBT(buffer_handle_t handle, PrivateHandle* priv_handle, std::string* name);

inline int getPrivateHandleBuff(
    buffer_handle_t handle, PrivateHandle* priv_handle)
{
    if (NULL == handle)
    {
        HWC_LOGE("%s NULL handle !!!!!", __func__);
        return -EINVAL;
    }

    int err = 0;
    priv_handle->handle = handle;

    err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_ION_FD, &priv_handle->ion_fd);
    priv_handle->is_secure = 0;

    if (usageHasSecure(priv_handle->usage))
    {
        if (!isSupportDmaBuf())
        {
            err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_SECURE_HANDLE, &priv_handle->sec_handle);
        }
        priv_handle->is_secure = 1;
    }
    else if ((static_cast<unsigned int>(priv_handle->ext_info.status) & GRALLOC_EXTRA_MASK_SECURE) == GRALLOC_EXTRA_BIT_SECURE)
    {
        err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_SECURE_HANDLE_HWC, &priv_handle->sec_handle);
        priv_handle->is_secure = 1;
    }

    if (err != GRALLOC_EXTRA_OK)
    {
        HWC_LOGE("%s err(%d), (handle=%p) !!", __func__, err, handle);
        return -EINVAL;
    }

    return 0;
}

inline int getPrivateMetaData(
    buffer_handle_t handle, PrivateHandle* priv_handle)
{
    int err = 0;

    priv_handle->is_hdr = 0;

    err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_META_DMA_FD, &priv_handle->meta_fd);
    err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_HDR_INFO, &priv_handle->hdrInfo);
    err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_HDR10P_INFO, &priv_handle->hdrPlusInfo);
    err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_META_DMA_LAYOUT, &priv_handle->meta_layout);
    HWC_LOGD("metadata layout:%p", &priv_handle->meta_layout);
    HWC_LOGD("metadata layout start:%d", priv_handle->meta_layout.start);
    HWC_LOGD("metadata layout size:%d", priv_handle->meta_layout.size);
    HWC_LOGD("metadata layout block:%p", &priv_handle->meta_layout.block);

    if (CHECK_HDR10_META_VALID(priv_handle->hdrInfo.u4TransformCharacter) ||
        checkHDR10PlusMetadata(priv_handle->hdrPlusInfo) ||
        (priv_handle->meta_layout.block[GRALLOC_META_DMA_BLOCK_DV].offset != -1))
        priv_handle->is_hdr = 1;

    if (err != GRALLOC_EXTRA_OK)
    {
        HWC_LOGE("%s err(%x), (handle=%p)", __func__, err, handle);
        return -EINVAL;
    }

    return 0;
}

inline int getPrivateHandle(
    buffer_handle_t handle, PrivateHandle* priv_handle, std::string* name = nullptr, const bool& is_outbuffer = false)
{

    int err = getPrivateHandleInfo(handle, priv_handle, name, is_outbuffer);
    err |= getPrivateHandleInfoModifyPerFrame(handle, priv_handle);

    err |= getPrivateHandleBuff(handle, priv_handle);

    int buffer_type = ((unsigned int)priv_handle->ext_info.status & GRALLOC_EXTRA_MASK_TYPE);
    if (buffer_type == GRALLOC_EXTRA_BIT_TYPE_VIDEO)
        err |= getPrivateMetaData(handle,priv_handle);

    // put here so we won't print warning log for client target using protected
    bool is_protected = priv_handle->usage & static_cast<unsigned int>(BufferUsage::PROTECTED);
    bool is_secured = priv_handle->usage & static_cast<unsigned int>(GM_BUFFER_USAGE_PRIVATE_SECURE_DISPLAY);
    if (CC_UNLIKELY(is_protected != is_secured))
    {
        HWC_LOGW("%s(), is_protected %d != is_secured %d, %s",
                 __FUNCTION__, is_protected, is_secured, name ? name->c_str() : "no_name");
    }

    if (err != GRALLOC_EXTRA_OK)
        return -EINVAL;

    return 0;
}

inline int getIonFd(
    buffer_handle_t handle, PrivateHandle* priv_handle)
{
    priv_handle->handle = handle;
    int err = gralloc_extra_query(handle, GRALLOC_EXTRA_GET_ION_FD, &priv_handle->ion_fd);
    if (err != GRALLOC_EXTRA_OK) {
        HWC_LOGE("%s err(%d), (handle=%p) !!", __func__, err, handle);
        return -EINVAL;
    }

    return 0;
}

inline int getBufferDimensionInfo(buffer_handle_t handle, PrivateHandle* priv_handle)
{
    if (handle == NULL)
    {
        HWC_LOGE("%s with NULL buffer handle", __func__);
        return -EINVAL;
    }
    priv_handle->handle = handle;

    int err = 0;
    int value_int = 0;

    err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_WIDTH, &priv_handle->width);
    err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_HEIGHT, &priv_handle->height);
    err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_STRIDE, &priv_handle->y_stride);
    err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_VERTICAL_STRIDE, &priv_handle->vstride);
    err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_FORMAT, &priv_handle->format);
    priv_handle->format_original = priv_handle->format;
    err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_ALLOC_SIZE, &priv_handle->size);
    err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_IOCTL_ION_SF_INFO, &priv_handle->ext_info);
    err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_USAGE, &value_int);
    priv_handle->usage = static_cast<unsigned int>(value_int);

    if (priv_handle->vstride == 0)
    {
        priv_handle->vstride = priv_handle->height;
    }

    if (err)
    {
        HWC_LOGE("%s err(%d), (handle=%p)", __func__, err, handle);
    }

    calculateStride(priv_handle);

    return err;
}

inline int getIonSfInfo(
    buffer_handle_t handle, PrivateHandle* priv_handle)
{
    priv_handle->handle = handle;
    int err = gralloc_extra_query(handle, GRALLOC_EXTRA_GET_IOCTL_ION_SF_INFO, &priv_handle->ext_info);

    if (err != GRALLOC_EXTRA_OK) {
        HWC_LOGE("%s err(%d), (handle=%p) !!", __func__, err, handle);
        return -EINVAL;
    }

    return 0;
}

inline int getAllocId(
    buffer_handle_t handle, PrivateHandle* priv_handle)
{
    priv_handle->handle = handle;
    int err = gralloc_extra_query(handle, GRALLOC_EXTRA_GET_ID, &priv_handle->alloc_id);
    if (err != GRALLOC_EXTRA_OK) {
        HWC_LOGE("%s err(%d), (handle=%p) !!", __func__, err, handle);
        return -EINVAL;
    }

    return 0;
}

inline bool isSecure(const PrivateHandle* priv_handle)
{
    if (usageHasSecure(priv_handle->usage))
        return true;

    return false;
}

inline bool isCompressData(const PrivateHandle* priv_handle)
{
    bool is_compressed = false;

    if (priv_handle->ext_info.status2 & GRALLOC_EXTRA_MASK2_BUF_COMPRESSION_STATUS)
        is_compressed = true;

    return is_compressed;
}

inline bool isG2GCompressData(const PrivateHandle* priv_handle)
{
    bool isG2GCompressed = false;

    if ((priv_handle->ext_info.status2 & GRALLOC_EXTRA_MASK2_BUF_COMPRESSION_STATUS) &&
        (priv_handle->ext_info.status2 & GRALLOC_EXTRA_MASK2_GPU_TO_GPU))
        isG2GCompressed = true;

    return isG2GCompressed;
}

// set "extra sf info" for marking this buffer as secure or normal
// if the caller need to get the final ext_info (ex: for synchronizing ext_info of priv_handle)
// fill rt_ext_info
// and this function will return the ext_info when rt_ext_info != NULL
void setSecExtraSfStatus(
    bool is_secure, buffer_handle_t hand, gralloc_extra_ion_sf_info_t* rt_ext_info = NULL);

// query sec_handle in order to check if this handle is with a secure buffer or not
// if there is a secure buffer attached, return the secure handle by filling *sec_handel
// if there isn't, allocate a secure buffer and query the secure handle, then fill *sec_handle
inline int getSecureHwcBuf(buffer_handle_t hand,
                                  SECHAND* sec_handle,
                                  GRALLOC_EXTRA_SECURE_BUFFER_TYPE* option = NULL)
{
    if ((sec_handle == NULL) || (hand == 0))
    {
        HWC_LOGE("handle is not valid!");
        return -EINVAL;
    }

    *sec_handle = 0;
    SECHAND tmp_sec_handle = 0;
    int err = gralloc_extra_query(hand, GRALLOC_EXTRA_GET_SECURE_HANDLE_HWC, &tmp_sec_handle);
    SVPLOGV("sbuf(+)", "query (err:%d h:%p sh:%x)", err, hand, tmp_sec_handle);

    if ((GRALLOC_EXTRA_OK != err) || (tmp_sec_handle == 0))
    {
        SVPLOGD("sbuf(+)", "alloc (h:%p)", hand);
        err = gralloc_extra_perform(hand, GRALLOC_EXTRA_ALLOC_SECURE_BUFFER_HWC, option);
        if (GRALLOC_EXTRA_OK != err)
        {
            SVPLOGD("sbuf(+)", "alloc fail (err:%d)", err);
        }

        err = gralloc_extra_query(hand, GRALLOC_EXTRA_GET_SECURE_HANDLE_HWC, &tmp_sec_handle);

        SVPLOGD("sbuf(+)", "query again (err:%d sh:%x)", err, tmp_sec_handle);
        if ((GRALLOC_EXTRA_OK != err) || (tmp_sec_handle == 0))
        {
            SVPLOGE("sbuf(+)", "fail!!!! (err:%d sh:%x)", err, tmp_sec_handle);
        }
    }
    if (GRALLOC_EXTRA_OK == err)
        *sec_handle = tmp_sec_handle;

    return err;
}

inline int freeSecureHwcBuf(buffer_handle_t hand)
{
    SECHAND tmp_sec_handle = 0;
    int err = gralloc_extra_query(hand, GRALLOC_EXTRA_GET_SECURE_HANDLE_HWC, &tmp_sec_handle);
    SVPLOGV("sbuf(-)", "query (err:%d sh:%x)", err, tmp_sec_handle);

    if ((GRALLOC_EXTRA_OK == err) && (tmp_sec_handle != 0))
    {
        SVPLOGD("sbuf(-)", "free (h:%p)", hand);
        err = gralloc_extra_perform(hand, GRALLOC_EXTRA_FREE_SEC_BUFFER_HWC, NULL);
        if (GRALLOC_EXTRA_OK != err)
        {
            SVPLOGE("sbuf(-)", "free fail (err:%d)", err);
        }

        err = gralloc_extra_query(hand, GRALLOC_EXTRA_GET_SECURE_HANDLE_HWC, &tmp_sec_handle);
        SVPLOGD("sbuf(-)", "query again (err:%d sh:%x)", err, tmp_sec_handle);
        if ((GRALLOC_EXTRA_OK == err) && (tmp_sec_handle != 0))
        {
            SVPLOGE("sbuf(-)", "fail!!!! (err:%d sh:%x)", err, tmp_sec_handle);
        }
    }
    return err;
}

inline status_t handleSecureBuffer(bool is_secure,
                                          buffer_handle_t hand,
                                          SECHAND* sec_handle,
                                          gralloc_extra_ion_sf_info_t* rt_ext_info = NULL)
{
    if ((sec_handle == NULL) || (hand == 0))
    {
        HWC_LOGE("handle is not valid!");
        return -EINVAL;
    }

    *sec_handle = 0;
    if (is_secure)
    {
        SECHAND tmp_sec_handle;
        int err = getSecureHwcBuf(hand, &tmp_sec_handle);
        if (GRALLOC_EXTRA_OK != err)
        {
            SVPLOGE("handleSecureBuffer", "Failed to allocate secure memory");
            return -EINVAL;
        }
        else
        {
            *sec_handle = tmp_sec_handle;
        }
    }
    else
    {
        freeSecureHwcBuf(hand);
    }
    setSecExtraSfStatus(is_secure, hand, rt_ext_info);
    return NO_ERROR;
}

inline void rectifyRectWithPrexform(Rect* roi, PrivateHandle* priv_handle)
{
    uint32_t prexform = priv_handle->prexform;
    if (0 == prexform)
        return;

    int32_t w = static_cast<int32_t>(priv_handle->width);
    int32_t h = static_cast<int32_t>(priv_handle->height);

    if (0 != (prexform & HAL_TRANSFORM_ROT_90))
    {
        SWAP(w, h);
    }

    Rect tmp = *roi;

    if (0 != (prexform & HAL_TRANSFORM_FLIP_V))
    {
        tmp.top     = h - roi->bottom;
        tmp.bottom  = h - roi->top;
    }

    if (0 != (prexform & HAL_TRANSFORM_FLIP_H))
    {
        tmp.left    = w - roi->right;
        tmp.right   = w - roi->left;
    }

    if (0 != (prexform & HAL_TRANSFORM_ROT_90))
    {
        roi->top     = tmp.left;
        roi->bottom  = tmp.right;
        roi->right   = h - tmp.top;
        roi->left    = h - tmp.bottom;
    }
    else
    {
        *roi = tmp;
    }
}

inline void rectifyXformWithPrexform(uint32_t* transform, uint32_t prexform)
{
    if (0 == prexform)
        return;

    uint32_t tmp = 0;
    prexform ^= (*transform & HAL_TRANSFORM_ROT_180);
    if (0 != (prexform & HAL_TRANSFORM_ROT_90))
    {
        if (0 != (prexform & HAL_TRANSFORM_FLIP_H))
            tmp |= HAL_TRANSFORM_FLIP_V;
        if (0 != (prexform & HAL_TRANSFORM_FLIP_V))
            tmp |= HAL_TRANSFORM_FLIP_H;
    }
    else
    {
        tmp = prexform & HAL_TRANSFORM_ROT_180;
    }

    if ((*transform & HAL_TRANSFORM_ROT_90) != (prexform & HAL_TRANSFORM_ROT_90))
    {
        tmp |= HAL_TRANSFORM_ROT_90;
        if (0 != (prexform & HAL_TRANSFORM_ROT_90))
            tmp ^= HAL_TRANSFORM_ROT_180;
    }
    *transform = tmp;
}

void dupBufferHandle(buffer_handle_t input, buffer_handle_t* output);

void freeDuppedBufferHandle(buffer_handle_t handle);

void calculateCrop(Rect* src_crop, Rect* dst_crop, Rect& dst_buf, uint32_t xform);

// ---------------------------------------------------------------------------
// BlackBuffer is a class for clearing background
// it is a singleton with a 128x128 RGB565 black buffer in it
// clear backgound by bliting this buffer to the destination buffer
// for a secure destination,
// getSecureHandle() can attach a zeor-initialized secure buffer to the original black buffer
// we can blit this black secure buffer to the secure destination

class BlackBuffer
{
public:
    static BlackBuffer& getInstance();
    ~BlackBuffer();

    buffer_handle_t getHandle();

    // get secure handle, allocate secure buffer when m_sec_handle == 0
    void setSecure();
    void setNormal();

    Mutex m_lock;

private:
    BlackBuffer();

    buffer_handle_t m_handle;
};

class WhiteBuffer
{
public:
    static WhiteBuffer& getInstance();
    ~WhiteBuffer();

    buffer_handle_t getHandle();

    // get secure handle, allocate secure buffer when m_sec_handle == 0
    void setSecure();
    void setNormal();

    Mutex m_lock;

private:
    WhiteBuffer();

    buffer_handle_t m_handle;
};
// ---------------------------------------------------------------------------
class IONDevice
{
public:
    static IONDevice& getInstance();
    ~IONDevice();

    int ionImport(int* ion_fd, const bool& log_on = true);
    int ionImport(const int32_t& ion_fd, int32_t* new_ion_fd, const char* = nullptr);
    int ionClose(int share_fd, const bool& log_on = true);
    int ionCloseAndSet(int* share_fd, const int& value = -1, const bool& log_on = true);
    void* ionMMap(int ion_fd, size_t length, int prot, int flags, int shared_fd);
    int ionMUnmap(int fd, void* addr, size_t length);

private:
    IONDevice();
};

void regToGuiExt();

class AbortMessager
{
public:
    static AbortMessager& getInstance();
    ~AbortMessager();
    template<typename ...Args>
    void printf(const char* msg, Args... values);
    void flushOut();
    void abort();
private:
    AbortMessager();

    mutable Mutex m_lock;
    String8 m_msg_arr[MAX_ABORT_MSG];
    int m_begin;
};

template<typename ...Args>
void AbortMessager::printf(const char* msg, Args... values)
{
    Mutex::Autolock l(m_lock);

    char buf[LOG_MAX_SIZE] = {'\0'};

    if (msg != nullptr)
    {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-security"
        if (snprintf(buf, LOG_MAX_SIZE, msg, values...) < 0)
#pragma GCC diagnostic pop
        {
            HWC_LOGE("AbortMessager failure [%s]", msg);
            return;
        }
    }

    struct timeval tv;
    struct timezone tz;
    int hh, mm, ss;
    gettimeofday(&tv, &tz);
    hh = (tv.tv_sec / 3600) % 24;
    mm = (tv.tv_sec % 3600) / 60;
    ss = tv.tv_sec % 60;

    BOUNDARY_CHECK_ABORT_MSG(m_begin);
    m_msg_arr[m_begin].clear();
    m_msg_arr[m_begin].appendFormat("%02d:%02d:%02d.%06ld tid:%d %s", hh, mm, ss, tv.tv_usec, ::gettid(), buf);
    m_begin = (m_begin + 1) % MAX_ABORT_MSG;
}

#ifdef USE_HWC2

class HWCLayer;

int getSrcLeft(const sp<HWCLayer>& layer);
int getSrcTop(const sp<HWCLayer>& layer);
int getSrcWidth(const sp<HWCLayer>& layer);
int getSrcHeight(const sp<HWCLayer>& layer);

int getDstTop(const sp<HWCLayer>& layer);
int getDstBottom(const sp<HWCLayer>& layer);
int getDstLeft(const sp<HWCLayer>& layer);
int getDstRight(const sp<HWCLayer>& layer);

int getDstWidth(const sp<HWCLayer>& layer);
int getDstHeight(const sp<HWCLayer>& layer);

class HWCDisplay;

struct LightHwcLayer1;
void copyHWCLayerIntoLightHwcLayer1(const sp<HWCLayer>& from, LightHwcLayer1* to, bool skip_dup_acq = false);

// listSecure() checks if there is any secure content in the display dist
bool listForceGPUComp(const std::vector<sp<HWCLayer> >& layers);
bool listSecure(const std::vector<sp<HWCLayer> >& layers);

class DispatcherJob;

#define protectedClose(fd) protectedCloseImpl(fd, __func__, __LINE__)
#define protectedDup(fd) protectedDupImpl(fd, __func__, __LINE__)
void protectedCloseImpl(const int32_t& fd, const char* str, const int32_t& line);
int protectedDupImpl(const int32_t& fd, const char* str, const int32_t& line);

const char* getCompString(const int32_t& comp_type);
const char* getBlendString(const int32_t& blend);

inline int32_t dupCloseFd(const int32_t& fd)
{
    if (fd == -1)
        return -1;

    const int32_t dup_fd = dup(fd);

    if ((3 > dup_fd && -1 < dup_fd) || (3 > fd && -1 < fd))
    {
        std::string stack;
        UnwindCurThreadBT(&stack);
        HWC_LOGW("dupAndCloseFd(): dupFence fd is zero call stack is (ori:%d dup:%d) %s",
                 fd, dup_fd, stack.c_str());
        abort();
    }

    protectedClose(fd);
    return dup_fd;
}


inline int32_t dupCloseAndSetFd(int32_t* fd, const int value = -1)
{
    const int32_t dup_fd = dupCloseFd(*fd);
    *fd = value;
    return dup_fd;
}

inline bool isHwcRegionEqual(const hwc_region_t& lhs, const hwc_region_t& rhs)
{
    if (lhs.numRects != rhs.numRects)
    {
        if ((lhs.numRects + rhs.numRects) == 1)
        {
            const hwc_rect_t *tmp = lhs.numRects ? &lhs.rects[0] : &rhs.rects[0];
            if ((tmp->left == 0) && (tmp->top == 0) && (tmp->right == 0) && (tmp->bottom == 0))
            {
                return true;
            }
        }
        return false;
    }

    if (lhs.numRects == 0)
        return true;

    const size_t len = sizeof(hwc_rect_t) * rhs.numRects;
    return (memcmp(lhs.rects, rhs.rects, len) == 0);
}

inline void copyHwcRegion(hwc_region_t* lhs, const hwc_region_t& rhs)
{
    const size_t len = sizeof(hwc_rect_t) * rhs.numRects;
    if (lhs->numRects != rhs.numRects)
    {
        if (lhs->numRects != 0)
        {
            free((void*)(lhs->rects));
            lhs->rects = nullptr;
        }

        if (len != 0)
        {
            lhs->rects = (hwc_rect_t const*)malloc(len);
            if (lhs->rects == nullptr)
            {
                HWC_LOGE("malloc hwc_region failed: len=%zu", len);
                lhs->numRects = 0;
            }
            else
            {
                lhs->numRects = rhs.numRects;
            }
        }
        else
        {
            lhs->rects = nullptr;
            lhs->numRects = 0;
        }
    }

    if (rhs.numRects != 0 && lhs->rects != nullptr)
        memcpy((void*)lhs->rects, (void*)rhs.rects, len);
}

inline int32_t mapColorMode2DataSpace(int32_t color_mode)
{
    int32_t color_space = HAL_DATASPACE_UNKNOWN;
    switch (color_mode)
    {
        case HAL_COLOR_MODE_NATIVE:
        case HAL_COLOR_MODE_SRGB:
            color_space = HAL_DATASPACE_V0_SRGB;
            break;

        case HAL_COLOR_MODE_DCI_P3:
        case HAL_COLOR_MODE_DISPLAY_P3:
            color_space = HAL_DATASPACE_DISPLAY_P3;
            break;

        case HAL_COLOR_MODE_ADOBE_RGB:
            color_space = HAL_DATASPACE_ADOBE_RGB;
            break;

        case HAL_COLOR_MODE_STANDARD_BT601_625:
        case HAL_COLOR_MODE_STANDARD_BT601_625_UNADJUSTED:
            color_space = HAL_DATASPACE_V0_BT601_625;
            break;

        case HAL_COLOR_MODE_STANDARD_BT601_525:
        case HAL_COLOR_MODE_STANDARD_BT601_525_UNADJUSTED:
            color_space = HAL_DATASPACE_V0_BT601_525;
            break;

        case HAL_COLOR_MODE_STANDARD_BT709:
            color_space = HAL_DATASPACE_V0_BT709;
            break;
    }

    return color_space;
}

nsecs_t getFenceSignalTime(const int32_t& fd);

void setPQEnhance(const PrivateHandle& priv_handle,
                  uint32_t* pq_enhance,
                  const int32_t& hdr_scenario,
                  const bool& is_game);

void setPQParam(const uint64_t& dpy,
                DpPqParam* dppq_param,
                const uint32_t& is_enhance,
                const int32_t& pool_id,
                const int32_t& dst_dataspace,
                const int32_t& src_dataspace,
                const std::vector<int32_t>& hdr_static_metadata_keys,
                const std::vector<float>& hdr_static_metadata_values,
                const std::vector<uint8_t>& hdr_dynamic_metadata,
                const int32_t& hdr_scenario,
                const uint32_t& time_stamp,
                const buffer_handle_t& handle,
                const uint32_t& pq_table_idx,
                const bool& is_game,
                const int32_t& pq_mode_id);

void forceMdpOutputFormat(const int& dpy,
                          const PrivateHandle& priv_handle,
                          const bool& is_game,
                          uint32_t* output_format,
                          bool* compression);

void mapP3PqParam(DpPqParam* dppq_param, const int32_t& out_ds);
bool isP3(const int32_t& dataspace);

void dumpTimeToTrace();

// light version of hwc_layer_1 in hwc1 define, this struct is to
// avoid useing hwc1 structure directly in hwc2
struct LightHwcLayer1 {
    uint32_t transform;
    int32_t blending;
    hwc_frect_t sourceCropf;
    hwc_rect_t displayFrame;
    int acquireFenceFd;
    int releaseFenceFd;
    uint8_t planeAlpha;
    hwc_region_t surfaceDamage;
};

void getFdInfo(int fd, std::ostringstream* ss);
void getBufferName(const buffer_handle_t& handle, std::ostringstream* ss);
void getBufferHandleInfo(const buffer_handle_t& handle, std::ostringstream* ss);

inline uint32_t getPrivateFormat(const PrivateHandle& priv_handle)
{
    uint32_t private_format = static_cast<uint32_t>(NOT_PRIVATE_FORMAT);
    if (HAL_PIXEL_FORMAT_YUV_PRIVATE == priv_handle.format ||
            HAL_PIXEL_FORMAT_YCbCr_420_888 == priv_handle.format ||
            HAL_PIXEL_FORMAT_YUV_PRIVATE_10BIT == priv_handle.format)
    {
        private_format = (static_cast<uint32_t>(priv_handle.ext_info.status) & GRALLOC_EXTRA_MASK_CM);
    }
    return private_format;
}

int32_t initSecureM4U();
void copyMMLCfg(mml_submit* src, mml_submit* dst);
void copyOverlayPortParam(const OverlayPortParam& src, OverlayPortParam* dst);

bool boundaryCut(const hwc_frect_t& src_source_crop,
                 const hwc_rect_t& src_display_frame,
                 unsigned int transform,
                 const hwc_rect_t& display_boundry,
                 hwc_frect_t* dst_source_crop,
                 hwc_rect_t* dst_display_frame);

int uclamp_task(pid_t pid, uint32_t uclamp_min);
void changeCpuSet(pid_t tid, unsigned int cpu_set);

uint32_t calculateCpuMHz(float mc, nsecs_t remain_time);
uint32_t cpuMHzToUClamp(uint32_t cpu_mhz);
const HwcMCycleInfo& getScenarioMCInfo(DispatcherJob* job);

int mapComposerExtDisplayType(ComposerExt::DisplayType& dpy, hwc2_display_t* disp_id);

uint32_t calculateWdmaProxCost(uint64_t disp_id, uint32_t format, Rect roi, uint32_t dst_w, uint32_t dst_h);

int getPrivateHandleFBTInfo(buffer_handle_t handle, PrivateHandle* priv_handle);

#endif // USE_HWC2

#endif // UTILS_TOOLS_H_
