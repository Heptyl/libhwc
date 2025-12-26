#define DEBUG_LOG_TAG "TOL"

#include <utils/tools.h>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

#if PROFILE_MAPPER
#include <numeric>
#endif

#include <dlfcn.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <processgroup/processgroup.h>
#include <bit>

#include <cutils/properties.h>
#include <cutils/bitops.h>

#include <hwc_feature_list.h>

#include "grallocdev.h"

#include "transform.h"
#ifdef USE_HWC2
#include "hwc2.h"
#include <sync/sync.h>
#endif // USE_HWC2

#include "DpAsyncBlitStream.h"

#include "dispatcher.h"
#include "platform_wrap.h"

// for CheckIonSupport
#include <BufferAllocator/BufferAllocatorWrapper.h>
#include <BufferAllocator/BufferAllocator.h>

#define UI_PQ_VIDEO_ID 0xFFFFFF00

//#define FENCE_DEBUG

unsigned int convertFormat4Bliter(const unsigned int format)
{
    unsigned int bliter_format = mapGrallocFormat(format);

    // when Platform does not support 2-subsampled format with odd size of roi, we
    // should not use YUYV. If the roi size of YUYV buffer is odd, ovl shows abnormal
    // screen content. Therefore we change the format to RGB888.
    if (Platform::getInstance().m_config.support_2subsample_with_odd_size_roi == false &&
        bliter_format == HAL_PIXEL_FORMAT_YUYV)
    {
        return HAL_PIXEL_FORMAT_RGB_888;
    }
    return bliter_format;
}

bool isHdrDataspace(const int32_t dataspace)
{
    const int32_t standard = dataspace & HAL_DATASPACE_STANDARD_MASK;
    return standard == HAL_DATASPACE_STANDARD_BT2020;
}

DP_COLOR_ENUM mapDpFormat(const unsigned int& fmt)
{
    switch (fmt)
    {
        case HAL_PIXEL_FORMAT_RGBA_8888:
            return DP_COLOR_RGBA8888;

        case HAL_PIXEL_FORMAT_RGBX_8888:
            return DP_COLOR_RGBA8888;

        case HAL_PIXEL_FORMAT_BGRA_8888:
            return DP_COLOR_BGRA8888;

        case HAL_PIXEL_FORMAT_BGRX_8888:
        case HAL_PIXEL_FORMAT_IMG1_BGRX_8888:
            return DP_COLOR_BGRA8888;

        case HAL_PIXEL_FORMAT_RGBA_1010102:
            return DP_COLOR_RGBA1010102;

        case HAL_PIXEL_FORMAT_RGB_888:
            return DP_COLOR_RGB888;

        case HAL_PIXEL_FORMAT_BGR_888:
            return DP_COLOR_BGR888;

        case HAL_PIXEL_FORMAT_RGB_565:
            return DP_COLOR_RGB565;

        case HAL_PIXEL_FORMAT_NV12_BLK_FCM:
            return DP_COLOR_420_BLKI;

        case HAL_PIXEL_FORMAT_NV12_BLK:
            return DP_COLOR_420_BLKP;

        case HAL_PIXEL_FORMAT_I420:
            return DP_COLOR_I420;

        case HAL_PIXEL_FORMAT_I420_DI:
            return DP_COLOR_I420;

        case HAL_PIXEL_FORMAT_YV12:
            return DP_COLOR_YV12;

        case HAL_PIXEL_FORMAT_YV12_DI:
            return DP_COLOR_YV12;

        case HAL_PIXEL_FORMAT_YUYV:
        case HAL_PIXEL_FORMAT_YCbCr_422_I:
            return DP_COLOR_YUYV;

        case HAL_PIXEL_FORMAT_UFO:
            return DP_COLOR_420_BLKP_UFO;

        case HAL_PIXEL_FORMAT_UFO_AUO:
            return DP_COLOR_420_BLKP_UFO_AUO;

        case HAL_PIXEL_FORMAT_NV12:
            return DP_COLOR_NV12;

        case HAL_PIXEL_FORMAT_YCRCB_420_SP:
            return DP_COLOR_NV21;

        case HAL_PIXEL_FORMAT_NV12_BLK_10BIT_H:
            return DP_COLOR_420_BLKP_10_H;

        case HAL_PIXEL_FORMAT_NV12_BLK_10BIT_H_JUMP:
            return DP_COLOR_420_BLKP_10_H_JUMP;

        case HAL_PIXEL_FORMAT_NV12_BLK_10BIT_V:
            return DP_COLOR_420_BLKP_10_V;

        case HAL_PIXEL_FORMAT_NV12_BLK_10BIT_V_JUMP:
            return DP_COLOR_420_BLKP_10_V_JUMP;

        case HAL_PIXEL_FORMAT_UFO_10BIT_H:
            return DP_COLOR_420_BLKP_UFO_10_H;

        case HAL_PIXEL_FORMAT_UFO_10BIT_H_JUMP:
            return DP_COLOR_420_BLKP_UFO_10_H_JUMP;

        case HAL_PIXEL_FORMAT_UFO_10BIT_V:
            return DP_COLOR_420_BLKP_UFO_10_V;

        case HAL_PIXEL_FORMAT_UFO_10BIT_V_JUMP:
            return DP_COLOR_420_BLKP_UFO_10_V_JUMP;

        case HAL_PIXEL_FORMAT_YCBCR_P010:
            return DP_COLOR_NV12_10L;

        case GM_PIXEL_FORMAT_NV12_HYFBC :
            return DP_COLOR_NV12_HYFBC;

        case GM_PIXEL_FORMAT_P010_HYFBC:
            return DP_COLOR_P010_HYFBC;

        default:
            HWC_LOGE("Color format for DP is invalid (0x%x)", fmt);
            return DP_COLOR_UNKNOWN;
    }
    return DP_COLOR_UNKNOWN;
}

void calculateCrop(Rect* src_crop, Rect* dst_crop, Rect& dst_buf, uint32_t xform)
{
    Rect src_base(*src_crop);
    Rect dst_base(*dst_crop);

    dst_base.intersect(dst_buf, dst_crop);

    if (dst_base == *dst_crop)
    {
        // no crop happened, skip
        *src_crop = src_base;
    }
    else
    {
        // check inverse transform
        Rect in_base(dst_base);
        Rect in_crop(*dst_crop);
        if (Transform::ROT_0 != xform && in_base != in_crop)
        {
            if (Transform::ROT_90 & xform)
                xform ^= (Transform::FLIP_H | Transform::FLIP_V);

            Transform tr(xform);
            in_base = tr.transform(in_base);
            in_crop = tr.transform(in_crop);
        }

        // map dst crop to src crop

        // calculate rectangle ratio between two rectangles
        // horizontally and vertically
        const float ratio_h = src_base.getWidth() /
            static_cast<float>(in_base.getWidth());
        const float ratio_v = src_base.getHeight() /
            static_cast<float>(in_base.getHeight());

        // get result of the corresponding crop rectangle
        // add 0.5f to round the result to the nearest whole number
        src_crop->left = static_cast<int32_t>(src_base.left + 0.5f +
            (in_crop.left - in_base.left) * ratio_h);
        src_crop->top  = static_cast<int32_t>(src_base.top + 0.5f +
            (in_crop.top - in_base.top) * ratio_v);
        src_crop->right = static_cast<int32_t>(src_base.left + 0.5f +
            (in_crop.right - in_base.left) * ratio_h);
        src_crop->bottom = static_cast<int32_t>(src_base.top + 0.5f +
            (in_crop.bottom - in_base.top) * ratio_v);
    }
}

void dupBufferHandle(buffer_handle_t input, buffer_handle_t* output)
{
    if(input == NULL)
    {
        HWC_LOGE("%s try to dup null handle", __func__);
        return;
    }
    const size_t size = static_cast<size_t>(input->numFds + input->numInts) * sizeof(int);

    native_handle_t* dup_input = static_cast<native_handle_t*>(malloc(sizeof(native_handle_t) + size));

    if (nullptr == dup_input)
    {
        HWC_LOGE("To malloc() dup_input(%p) failed", dup_input);
        return;
    }

    memcpy(dup_input, input, sizeof(native_handle_t) + size);

    for (int i = 0; i < input->numFds; ++i)
    {
        dup_input->data[i] = ::dup(input->data[i]);
    }

    *output = dup_input;
}

void freeDuppedBufferHandle(buffer_handle_t handle)
{
    if(handle == NULL)
        return;

    for (int i = 0; i < handle->numFds; ++i)
    {
        ::protectedClose(handle->data[i]);
    }

    free(const_cast<native_handle_t*>(handle));
}

// ---------------------------------------------------------------------------
BlackBuffer& BlackBuffer::getInstance()
{
    static BlackBuffer gInstance;
    return gInstance;
}

BlackBuffer::BlackBuffer()
    : m_handle(0)
{
    GrallocDevice::AllocParam param;
    param.width  = 128;
    param.height = 128;
    param.format = HAL_PIXEL_FORMAT_RGB_565;
    param.usage  = static_cast<uint64_t>(BufferUsage::CPU_WRITE_RARELY);

    // allocate
    if (NO_ERROR != GrallocDevice::getInstance().alloc(param))
    {
        HWC_LOGW("fill black buffer by bliter - allocate buf fail");
        return;
    }
    m_handle = param.handle;

    // make this buffer black
    int value;
    int32_t err = GRALLOC_EXTRA_OK;
    int32_t ion_fd = -1;
    err = gralloc_extra_query(m_handle, GRALLOC_EXTRA_GET_ALLOC_SIZE, &value);
    if (err != GRALLOC_EXTRA_OK)
    {
        HWC_LOGE("%s Failed to get alloc size, err(%d), (handle=%p)", __func__, err, m_handle);
        return ;
    }
    size_t size = static_cast<size_t>(value);

    err = gralloc_extra_query(m_handle, GRALLOC_EXTRA_GET_ION_FD, &ion_fd);
    if (err != GRALLOC_EXTRA_OK)
    {
        HWC_LOGE("%s Failed to get ion fd, err(%d), (handle=%p)", __func__, err, m_handle);
        return;
    }

    int shared_fd = -1;
    int res = IONDevice::getInstance().ionImport(ion_fd, &shared_fd);
    if (res != 0 || shared_fd < 0)
    {
        HWC_LOGE("BlackBuf: ionImport is failed: %s(%d), ion_fd(%d)", strerror(errno), res, ion_fd);
        return;
    }

    void *ptr = nullptr;
    if (ion_fd != -1 && shared_fd != -1)
    {
        ptr = IONDevice::getInstance().ionMMap(ion_fd, size, PROT_READ | PROT_WRITE, MAP_SHARED,
                                               shared_fd);
    }

    if (ptr == nullptr || ptr == MAP_FAILED)
    {
        HWC_LOGE("BlackBuf: ion mmap fail");
    }
    else
    {
        memset(ptr, 0, size);
        if (IONDevice::getInstance().ionMUnmap(ion_fd, ptr, size) != 0)
        {
            HWC_LOGW("BlackBuf: failed to unmap buffer");
        }
        ptr = nullptr;
    }

    if (shared_fd != -1 && IONDevice::getInstance().ionClose(shared_fd))
    {
        HWC_LOGW("BlackBuf: ionClose is failed: %s , share_fd(%d)", strerror(errno), shared_fd);
        shared_fd = -1;
    }
}

BlackBuffer::~BlackBuffer()
{
    if (m_handle != 0)
        GrallocDevice::getInstance().free(m_handle);
}

buffer_handle_t BlackBuffer::getHandle()
{
    return m_handle;
}

void BlackBuffer::setSecure()
{
    if (1 == HwcFeatureList::getInstance().getFeature().svp)
    {
        // attach a zero-initialized secure buffer to the original buffer
        SVPLOGD("BlackBuf setSecure", "(h:%p)", m_handle);
        unsigned int sec_handle;
        GRALLOC_EXTRA_SECURE_BUFFER_TYPE option = GRALLOC_EXTRA_SECURE_BUFFER_TYPE_ZERO;
        getSecureHwcBuf(m_handle, &sec_handle, &option);
        setSecExtraSfStatus(true, m_handle);
    }
}

void BlackBuffer::setNormal()
{
    if (1 == HwcFeatureList::getInstance().getFeature().svp)
    {
        SVPLOGD("BlackBuf setNormal", "(h:%p)", m_handle);
        freeSecureHwcBuf(m_handle);
        setSecExtraSfStatus(false, m_handle);
    }
}

// ---------------------------------------------------------------------------
WhiteBuffer& WhiteBuffer::getInstance()
{
    static WhiteBuffer gInstance;
    return gInstance;
}

WhiteBuffer::WhiteBuffer()
    : m_handle(0)
{
    GrallocDevice::AllocParam param;
    param.width  = 128;
    param.height = 128;
    param.format = HAL_PIXEL_FORMAT_RGB_565;
    param.usage  = static_cast<uint64_t>(BufferUsage::CPU_WRITE_RARELY);

    // allocate
    if (NO_ERROR != GrallocDevice::getInstance().alloc(param))
    {
        HWC_LOGW("fill white buffer by bliter - allocate buf fail");
        return;
    }
    m_handle = param.handle;

    // make this buffer black
    int value;
    int32_t err = GRALLOC_EXTRA_OK;
    int32_t ion_fd = -1;
    err = gralloc_extra_query(m_handle, GRALLOC_EXTRA_GET_ALLOC_SIZE, &value);
    if (err != GRALLOC_EXTRA_OK)
    {
        HWC_LOGE("%s Failed to get alloc size, err(%d), (handle=%p)", __func__, err, m_handle);
        return ;
    }
    size_t size = static_cast<size_t>(value);

    err = gralloc_extra_query(m_handle, GRALLOC_EXTRA_GET_ION_FD, &ion_fd);
    if (err != GRALLOC_EXTRA_OK)
    {
        HWC_LOGE("%s Failed to get ion fd, err(%d), (handle=%p)", __func__, err, m_handle);
        return;
    }

    int shared_fd = -1;
    int res = IONDevice::getInstance().ionImport(ion_fd, &shared_fd);
    if (res != 0 || shared_fd < 0)
    {
        HWC_LOGE("WhiteBuf: ionImport is failed: %s(%d), ion_fd(%d)", strerror(errno), res, ion_fd);
        return;
    }

    void *ptr = nullptr;
    if (ion_fd != -1 && shared_fd != -1)
    {
        ptr = IONDevice::getInstance().ionMMap(ion_fd, size, PROT_READ | PROT_WRITE, MAP_SHARED,
                                               shared_fd);
    }

    if (ptr == nullptr || ptr == MAP_FAILED)
    {
        HWC_LOGE("WhiteBuf: ion mmap fail");
    }
    else
    {
        memset(ptr, 255, size);
        if (IONDevice::getInstance().ionMUnmap(ion_fd, ptr, size) != 0)
        {
            HWC_LOGW("WhiteBuf: failed to unmap buffer");
        }
        ptr = nullptr;
    }

    if (shared_fd != -1 && IONDevice::getInstance().ionClose(shared_fd))
    {
        HWC_LOGW("WhiteBuf: ionClose is failed: %s , share_fd(%d)", strerror(errno), shared_fd);
        shared_fd = -1;
    }
}

WhiteBuffer::~WhiteBuffer()
{
    if (m_handle != 0)
        GrallocDevice::getInstance().free(m_handle);
}

buffer_handle_t WhiteBuffer::getHandle()
{
    return m_handle;
}

void WhiteBuffer::setSecure()
{
    if (1 == HwcFeatureList::getInstance().getFeature().svp)
    {
        // attach a zero-initialized secure buffer to the original buffer
        SVPLOGD("WhiteBuf setSecure", "(h:%p)", m_handle);
        unsigned int sec_handle;
        GRALLOC_EXTRA_SECURE_BUFFER_TYPE option = GRALLOC_EXTRA_SECURE_BUFFER_TYPE_ZERO;
        getSecureHwcBuf(m_handle, &sec_handle, &option);
        setSecExtraSfStatus(true, m_handle);
    }
}

void WhiteBuffer::setNormal()
{
    if (1 == HwcFeatureList::getInstance().getFeature().svp)
    {
        SVPLOGD("WhiteBuf setNormal", "(h:%p)", m_handle);
        freeSecureHwcBuf(m_handle);
        setSecExtraSfStatus(false, m_handle);
    }
}
// ---------------------------------------------------------------------------
IONDevice& IONDevice::getInstance()
{
    static IONDevice gInstance;
    return gInstance;
}

IONDevice::IONDevice()
{
}

IONDevice::~IONDevice()
{
}

int IONDevice::ionCloseAndSet(int* share_fd, const int& value, const bool& log_on)
{
    int result = ionClose(*share_fd, log_on);
    if (result == 0)
    {
        *share_fd = value;
    }
    return result;
}

int IONDevice::ionImport(int* ion_fd, const bool& log_on)
{
    int new_ion_fd = ::dup(*ion_fd);
    if (new_ion_fd < 0)
    {
        HWC_LOGE("failed to dup ion fd for ionImport a new ion fd");
        return -1;
    }
    else if (3 > new_ion_fd && -1 < new_ion_fd)
    {
        HWC_LOGW("ionImport with a risky duplicated ion fd: %d", new_ion_fd);
    }

    if (log_on)
    {
        HWC_LOGD("[mm_ionImport] ion_fd(%d) -> share_fd(%d)", *ion_fd, new_ion_fd);
    }
    else
    {
        HWC_LOGV("[mm_ionImport] ion_fd(%d) -> share_fd(%d)", *ion_fd, new_ion_fd);
    }

    *ion_fd = new_ion_fd;

    return 0;
}

int IONDevice::ionImport(const int32_t& ion_fd, int32_t* new_ion_fd, const char* dbg_name)
{
    *new_ion_fd = ::dup(ion_fd);
    if (*new_ion_fd < 0)
    {
        HWC_LOGE("failed to dup ion fd for ionImport a new ion fd");
        return -1;
    }
    else if (3 > *new_ion_fd && -1 < *new_ion_fd)
    {
        HWC_LOGW("ionImport with a risky duplicated ion fd: %d", *new_ion_fd);
    }

    if (dbg_name != nullptr)
    {
        HWC_LOGD("[mm_ionImport] ion_fd(%d) -> new_ion_fd(%d) when %s",
                ion_fd, *new_ion_fd, dbg_name);
    }
    else
    {
        HWC_LOGD("[mm_ionImport] ion_fd(%d) -> new_ion_fd(%d)", ion_fd, *new_ion_fd);
    }

    return 0;
}

int IONDevice::ionClose(int share_fd, const bool& log_on)
{
    if (share_fd < 0)
    {
        HWC_LOGW("[mm_ionClose] Invalid Fd (%d)!", share_fd);
        return -1;
    }
    else if (3 > share_fd && -1 < share_fd)
    {
        HWC_LOGW("[mm_ionClose] close a risky shared fd: %d", share_fd);
    }
    protectedClose(share_fd);

    if (log_on)
    {
        HWC_LOGD("[mm_ionClose] share_fd(%d)", share_fd);
    }
    else
    {
        HWC_LOGV("[mm_ionClose] share_fd(%d)", share_fd);
    }

    return 0;
}

void* IONDevice::ionMMap(int ion_fd, size_t length, int prot, int flags, int shared_fd)
{
    void* ptr = NULL;
    ptr = mmap(NULL, length, prot, flags, shared_fd, 0);
    if (ptr == MAP_FAILED)
    {
        HWC_LOGE("failed to ionMMap[ion_fd:%d share_fd:%d length:%zu]",
                ion_fd, shared_fd, length);
    }

    return ptr;
}

int IONDevice::ionMUnmap(int fd, void* addr, size_t length)
{
    int res = munmap(addr, length);
    if (res < 0)
    {
        HWC_LOGE("failed to ionMUnmap[fd:%d addr:0x%p length:%zu]",
                fd, addr, length);
    }

    return res;
}

void setSecExtraSfStatus(
    bool is_secure, buffer_handle_t hand, gralloc_extra_ion_sf_info_t* rt_ext_info)
{
    if (0 == HwcFeatureList::getInstance().getFeature().svp)
        return;

    // rt_ext_info != NULL means that we have to return the ext_info
    // so use *rt_ext_info for getting ext_info when rt_ext_info != NULL
    // otherwise use tmp_ext_info
    gralloc_extra_ion_sf_info_t tmp_ext_info;
    gralloc_extra_ion_sf_info_t* ext_info = (NULL == rt_ext_info) ? &tmp_ext_info : rt_ext_info;
    int status = is_secure ? GRALLOC_EXTRA_BIT_SECURE : GRALLOC_EXTRA_BIT_NORMAL;

    // query  ->  set extra sf info  ->  perform
    gralloc_extra_query(hand, GRALLOC_EXTRA_GET_IOCTL_ION_SF_INFO, ext_info);
    gralloc_extra_sf_set_status(ext_info, GRALLOC_EXTRA_MASK_SECURE, status);
    gralloc_extra_perform(hand, GRALLOC_EXTRA_SET_IOCTL_ION_SF_INFO, ext_info);
}

AbortMessager& AbortMessager::getInstance()
{
    static AbortMessager gInstance;
    return gInstance;
}

AbortMessager::AbortMessager()
{
    m_begin = 0;
    for(int i = 0; i < MAX_ABORT_MSG; ++i)
        m_msg_arr[i].clear();
}

AbortMessager::~AbortMessager()
{
}

void AbortMessager::flushOut()
{
    Mutex::Autolock l(m_lock);
    for (int i = 0; i < MAX_ABORT_MSG; ++i)
    {
        int index = (m_begin + i) % MAX_ABORT_MSG;
        if (m_msg_arr[index].size())
            HWC_LOGD("[%d] %s", i, m_msg_arr[index].c_str());
    }
}

void AbortMessager::abort()
{
    flushOut();
    ::abort();
}

#ifdef USE_HWC2

const char* HWC2_COMPOSITION_INVALID_STR = "INV";
const char* HWC2_COMPOSITION_CLIENT_STR = "CLI";
const char* HWC2_COMPOSITION_DEVICE_STR = "DEV";
const char* HWC2_COMPOSITION_CURSOR_STR = "CUR";
const char* HWC2_COMPOSITION_SOLID_COLOR_STR = "SOL";
const char* HWC2_COMPOSITION_UNKNOWN_STR = "UNK";

const char* getCompString(const int32_t& comp_type)
{
    switch(comp_type)
    {
        case HWC2_COMPOSITION_INVALID:
            return HWC2_COMPOSITION_INVALID_STR;

        case HWC2_COMPOSITION_CLIENT:
            return HWC2_COMPOSITION_CLIENT_STR;

        case HWC2_COMPOSITION_DEVICE:
            return HWC2_COMPOSITION_DEVICE_STR;

        case HWC2_COMPOSITION_CURSOR:
            return HWC2_COMPOSITION_CURSOR_STR;

        case HWC2_COMPOSITION_SOLID_COLOR:
            return HWC2_COMPOSITION_SOLID_COLOR_STR;

        default:
            HWC_LOGE("%s unknown composition type:%d", __func__, comp_type);
            return HWC2_COMPOSITION_UNKNOWN_STR;
    }
}

const char* HWC2_BLEND_MODE_INVALID_STR = "INV";
const char* HWC2_BLEND_MODE_NONE_STR = "NON";
const char* HWC2_BLEND_MODE_PREMULTIPLIED_STR = "PRE";
const char* HWC2_BLEND_MODE_COVERAGE_STR = "COV";
const char* HWC2_BLEND_MODE_UNKNOWN_STR = "UNK";

const char* getBlendString(const int32_t& blend)
{
    switch(blend)
    {
        case HWC2_BLEND_MODE_INVALID:
            return HWC2_BLEND_MODE_INVALID_STR;

        case HWC2_BLEND_MODE_NONE:
            return HWC2_BLEND_MODE_NONE_STR;

        case HWC2_BLEND_MODE_PREMULTIPLIED:
            return HWC2_BLEND_MODE_PREMULTIPLIED_STR;

        case HWC2_BLEND_MODE_COVERAGE:
            return HWC2_BLEND_MODE_COVERAGE_STR;

        default:
            HWC_LOGE("%s unknown blend:%d", __func__, blend);
            return HWC2_BLEND_MODE_UNKNOWN_STR;
    }
}

void protectedCloseImpl(const int32_t& fd, const char* /*str*/, const int32_t& /*line*/)
{
    if (fd >= 0 && fd < 3)
    {
        HWC_LOGE("abort! close fd %d", fd);
        abort();
    }
#ifdef FENCE_DEBUG
    std::string stack;
    UnwindCurThreadBT(&stack);
    HWC_LOGW("close fd %d backtrace: %s", fd, stack.c_str());
#endif
    if (fd >= 0)
    {
        ::close(fd);
    }
    else if (fd < -1)
    {
        HWC_LOGW("A unknown fd(-1) to be freed");
    }
}

int protectedDupImpl(const int32_t& fd, const char* /*str*/, const int32_t& /*line*/)
{
    int dupFd = -1;
    if (fd >= 0 && fd < 3)
    {
        HWC_LOGE("abort! dup fd %d", fd);
        abort();
    }
    dupFd = dup(fd);

    if (dupFd >= 0 && dupFd < 3)
    {
        HWC_LOGE("abort! dupped fd %d", dupFd);
        abort();
    }

    return dupFd;
}


int getSrcLeft(const sp<HWCLayer>& layer)
{
    return (int)(ceilf(layer->getSourceCrop().left));
}

int getSrcTop(const sp<HWCLayer>& layer)
{
    return (int)(ceilf(layer->getSourceCrop().top));
}

int getSrcWidth(const sp<HWCLayer>& layer)
{
    const int left = (int)(ceilf(layer->getSourceCrop().left));
    const int right = (int)(floorf(layer->getSourceCrop().right));
    return (right - left);
}

int getSrcHeight(const sp<HWCLayer>& layer)
{
    const int top = (int)(ceilf(layer->getSourceCrop().top));
    const int bottom = (int)(floorf(layer->getSourceCrop().bottom));
    return (bottom - top);
}

int getDstTop(const sp<HWCLayer>& layer)
{
    return (int)(ceilf(layer->getDisplayFrame().top));
}

int getDstBottom(const sp<HWCLayer>& layer)
{
    return (int)(ceilf(layer->getDisplayFrame().bottom));
}

int getDstLeft(const sp<HWCLayer>& layer)
{
    return (int)(ceilf(layer->getDisplayFrame().left));
}

int getDstRight(const sp<HWCLayer>& layer)
{
    return (int)(ceilf(layer->getDisplayFrame().right));
}

int getDstWidth(const sp<HWCLayer>& layer)
{
    return WIDTH(layer->getDisplayFrame());
}

int getDstHeight(const sp<HWCLayer>& layer)
{
    return HEIGHT(layer->getDisplayFrame());
}

void copyHWCLayerIntoLightHwcLayer1(const sp<HWCLayer>& from, LightHwcLayer1* to, bool skip_dup_acq)
{
    to->transform = from->getTransform();
    to->displayFrame = from->getDisplayFrame();
    to->blending = from->getBlend();
    //todo:joen sourcecdrop f ? sourcecropi?
    to->sourceCropf = from->getSourceCrop();

    // check source crop valid
    Rect fixed_rect = getFixedRect(to->sourceCropf);
    const PrivateHandle& priv_handle = from->getPrivateHandle();
    if (fixed_rect.left + fixed_rect.getWidth() > static_cast<int32_t>(priv_handle.width))
    {
        HWC_LOGE("%s(), source crop, left %" PRId32 " + width %" PRId32 " > priv_handle.width %u",
                 __FUNCTION__, fixed_rect.left, fixed_rect.getWidth(), priv_handle.width);
        to->sourceCropf.left = 0;
        to->sourceCropf.right = priv_handle.width;
    }
    if (fixed_rect.top + fixed_rect.getHeight() > static_cast<int32_t>(priv_handle.height))
    {
        HWC_LOGE("%s(), source crop, top %" PRId32 " + height %" PRId32 " > priv_handle.height %u",
                 __FUNCTION__, fixed_rect.top, fixed_rect.getHeight(), priv_handle.height);
        to->sourceCropf.top = 0;
        to->sourceCropf.bottom = priv_handle.height;
    }

    // hwc layer acq fence life cycle is with hwc buf
    // job layer's dupped acq fence should be closed after use
    int from_fd = from->getAcquireFenceFd();
    if (from_fd < 0 || skip_dup_acq)
    {
        to->acquireFenceFd = -1;
    }
    else
    {
        to->acquireFenceFd = dup(from_fd);
        if (CC_UNLIKELY(to->acquireFenceFd < 0))
        {
            std::string stack;
            UnwindCurThreadBT(&stack);
            HWC_LOGE("%s(), dup fd fail call stack is (ori:%d dup:%d) %s",
                     __FUNCTION__, from_fd, to->acquireFenceFd, stack.c_str());
        }
    }
    to->releaseFenceFd = from->getReleaseFenceFd();
    to->planeAlpha = static_cast<uint8_t>(from->getPlaneAlpha() * 0xff);
    to->surfaceDamage = from->getDamage();

    HWC_LOGV("%s() id:%" PRIu64 " acq fence:%d,%d src_crop(%f,%f,%f,%f) dst_crop(%d,%d,%d,%d)",
             __FUNCTION__,
             from->getId(), to->acquireFenceFd, from_fd,
             to->sourceCropf.left, to->sourceCropf.top, to->sourceCropf.right, to->sourceCropf.bottom,
             to->displayFrame.left, to->displayFrame.top, to->displayFrame.right, to->displayFrame.bottom);
}

bool listForceGPUComp(const std::vector<sp<HWCLayer> >& layers)
{
    for (auto& layer : layers)
    {
        if (layer->getSFCompositionType() == HWC2_COMPOSITION_CLIENT)
            return true;
    }
    return false;
}

// listSecure() checks if there is any secure content in the display dist
bool listSecure(const std::vector<sp<HWCLayer> >& layers)
{
    for (auto& layer : layers)
    {
        if (layer->getHandle() == nullptr)
            continue;

        const unsigned int& usage = layer->getPrivateHandle().usage;
        if (usageHasProtectedOrSecure(usage))
            return true;
    }
    return false;
}

nsecs_t getFenceSignalTime(const int32_t& fd) {
    if (fd == -1) {
        return SIGNAL_TIME_INVALID;
    }

    struct sync_file_info* finfo = sync_file_info(fd);
    if (finfo == NULL) {
        return SIGNAL_TIME_INVALID;
    }
    if (finfo->status != 1) {
        sync_file_info_free(finfo);
        return SIGNAL_TIME_PENDING;
    }

    uint64_t timestamp = 0;
    struct sync_fence_info* pinfo = sync_get_fence_info(finfo);
    for (size_t i = 0; i < finfo->num_fences; i++) {
        if (pinfo[i].timestamp_ns > timestamp) {
            timestamp = pinfo[i].timestamp_ns;
        }
    }

    sync_file_info_free(finfo);
    return nsecs_t(timestamp);
}

int setPrivateHandlePQInfo(const buffer_handle_t& handle, PrivateHandle* priv_handle)
{
    if (priv_handle == nullptr || handle == nullptr)
    {
        return -EINVAL;
    }

    int err = 0;

    if (HwcFeatureList::getInstance().getFeature().ai_pq)
    {
        err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_AI_PQ_INFO, &(priv_handle->ai_pq_info));
    }
    if (HwcFeatureList::getInstance().getFeature().is_support_pq &&
        HwcFeatureList::getInstance().getFeature().pq_video_whitelist_support &&
        HwcFeatureList::getInstance().getFeature().video_transition)
    {
        ge_pq_scltm_info_t pq_info;
        err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_PQ_SCLTM_INFO, &pq_info);
        if (!err)
        {
            priv_handle->pq_enable = pq_info.scltmEnable;
            priv_handle->pq_pos = pq_info.scltmPosition;
            priv_handle->pq_orientation = pq_info.scltmOrientation;
            priv_handle->pq_table_idx = pq_info.scltmTableIndex;

            // error handling
            priv_handle->pq_pos = (priv_handle->pq_pos > 1024 ? 1024 : priv_handle->pq_pos);

            if (!Platform::getInstance().m_config.mdp_support_decompress &&
                isCompressData(priv_handle) && priv_handle->pq_enable)
            {
                HWC_LOGW("MDP PQ cannot be applied for compression buffer: handle:%p pq_enable:%d", handle, priv_handle->pq_enable);
                priv_handle->pq_enable = priv_handle->pq_pos = priv_handle->pq_orientation = priv_handle->pq_table_idx = 0;
            }

            if (!priv_handle->pq_enable && priv_handle->pq_pos)
            {
                HWC_LOGW("The PQ position(%d) should be zero because PQ is not enabled", priv_handle->pq_pos);
                priv_handle->pq_enable = priv_handle->pq_pos = priv_handle->pq_orientation = priv_handle->pq_table_idx = 0;
            }
            else if (priv_handle->pq_enable && !priv_handle->pq_pos)
            {
                HWC_LOGW("The PQ position(%d) should not zero because PQ is enabled", priv_handle->pq_pos);
                priv_handle->pq_enable = priv_handle->pq_pos = priv_handle->pq_orientation = priv_handle->pq_table_idx = 0;
            }
        }
    }
    else
    {
        priv_handle->pq_enable = priv_handle->pq_pos = priv_handle->pq_orientation = priv_handle->pq_table_idx = 0;
    }

    return err;
}

void setPQEnhance(const PrivateHandle& priv_handle,
                  uint32_t* pq_enhance,
                  const int32_t& hdr_scenario,
                  const bool& is_game)
{
    if (HwcFeatureList::getInstance().getFeature().is_support_pq)
    {
        const bool pq_on = ((priv_handle.ext_info.status2 & GRALLOC_EXTRA_MASK2_VIDEO_PQ) == GRALLOC_EXTRA_BIT2_VIDEO_PQ_ON);
        if (HwcFeatureList::getInstance().getFeature().pq_video_whitelist_support)
        {
            if (priv_handle.pq_enable)
            {
                if (NULL != pq_enhance)
                    *pq_enhance = PQ_DEFAULT_EN;
            }
            else if (pq_on)
            {
                if (NULL != pq_enhance)
                    *pq_enhance = PQ_ULTRARES_EN | PQ_COLOR_EN | PQ_DYN_CONTRAST_EN;
            }
        }
        else
        {
            if (pq_on)
            {
                if (NULL != pq_enhance)
                    *pq_enhance = PQ_DEFAULT_EN;

            }
        }
        if (NULL != pq_enhance)
        {
            if (priv_handle.ai_pq_info.param)
            {
                *pq_enhance = PQ_DEFAULT_EN;
            }
            // Only game HDR do not need to set pq_enhance. All of other HDR scenarios
            // need to set pq_enhance to PQ_DEFAULT_EN.
            *pq_enhance |= (is_game || (hdr_scenario != HWC_LAYER_HDR_NONE &&
                                        hdr_scenario != HWC_LAYER_HDR_GAME));
        }
    }
}

DP_GAMUT_ENUM mapPqGamut(const int32_t& dataspace)
{
    switch (dataspace & HAL_DATASPACE_STANDARD_MASK)
    {
        case HAL_DATASPACE_STANDARD_BT601_625:
        case HAL_DATASPACE_STANDARD_BT601_625_UNADJUSTED:
        case HAL_DATASPACE_STANDARD_BT601_525:
        case HAL_DATASPACE_STANDARD_BT601_525_UNADJUSTED:
            return DP_GAMUT_BT601;

        case HAL_DATASPACE_STANDARD_BT709:
            return DP_GAMUT_BT709;

        case HAL_DATASPACE_STANDARD_DCI_P3:
            return DP_GAMUT_DISPLAY_P3;

        case HAL_DATASPACE_STANDARD_BT2020:
            return DP_GAMUT_BT2020;
    }
    return DP_GAMUT_UNKNOWN;
}

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
                const int32_t& pq_mode_id)
{
    dppq_param->enable = is_enhance;
    dppq_param->scenario = MEDIA_VIDEO;
    if (is_game || hdr_scenario == HWC_LAYER_HDR_CAMERA_PREVIEW || hdr_scenario == HWC_LAYER_HDR_COMMON)
    {
        dppq_param->u.video.id = UI_PQ_VIDEO_ID;
    }
    else
    {
        dppq_param->u.video.id = static_cast<uint32_t>(pool_id);
    }

    if (isP3(src_dataspace))
    {
        dppq_param->dstGamut = mapPqGamut(dst_dataspace);
        if (dppq_param->dstGamut == DP_GAMUT_UNKNOWN)
        {
            HWC_LOGW("Not setup dstGamut, use BT601 as default");
            dppq_param->dstGamut = DP_GAMUT_BT601;
        }
        dppq_param->enable = PQ_CCORR_EN;
        dppq_param->scenario = MEDIA_VIDEO;
        dppq_param->srcGamut = DP_GAMUT_DISPLAY_P3;
    }
    else
    {
        dppq_param->dstGamut = mapPqGamut(dst_dataspace);
        dppq_param->srcGamut = mapPqGamut(src_dataspace);
    }

    dppq_param->u.video.dispId = static_cast<uint32_t>(dpy);
    dppq_param->u.video.timeStamp = time_stamp;
    dppq_param->u.video.grallocExtraHandle = handle;
    switch (hdr_scenario)
    {
        case HWC_LAYER_HDR_GAME:
            dppq_param->u.video.videoScenario = INFO_GAMEHDR;
            break;
        case HWC_LAYER_HDR_CAMERA_PREVIEW:
            dppq_param->u.video.videoScenario = INFO_HDRVR;
            break;
        case HWC_LAYER_HDR_VIDEO:
        case HWC_LAYER_HDR_COMMON:
            dppq_param->u.video.videoScenario = INFO_VIDEO;
            break;
        case HWC_LAYER_HDR_NONE:
            dppq_param->u.video.videoScenario = is_game ? INFO_GAME : INFO_VIDEO;
            break;
        default:
            HWC_LOGW("Not support hdr_scenario %d, set videoScenario to INFO_VIDEO by default", hdr_scenario);
            dppq_param->u.video.videoScenario = INFO_VIDEO;
            break;
    }
    dppq_param->u.video.userScenario = INFO_HWC;
    dppq_param->u.video.isHDR2SDR = 0;
    dppq_param->u.video.paramTable = pq_table_idx;

    dppq_param->u.video.HDRMetadata.AOSPHDRDataSpace.dataSpace = src_dataspace;
    dppq_param->u.video.HDRMetadata.AOSPHDRStaticMetadata.numElements = static_cast<uint32_t>(hdr_static_metadata_keys.size());
    dppq_param->u.video.HDRMetadata.AOSPHDRStaticMetadata.key = hdr_static_metadata_keys.data();
    dppq_param->u.video.HDRMetadata.AOSPHDRStaticMetadata.metaData = hdr_static_metadata_values.data();
    dppq_param->u.video.HDRMetadata.AOSPHDRDynamicMetadata.size = static_cast<uint32_t>(hdr_dynamic_metadata.size());
    dppq_param->u.video.HDRMetadata.AOSPHDRDynamicMetadata.byteArray = hdr_dynamic_metadata.data();

    dppq_param->u.video.xmlModeId = pq_mode_id;
}

bool isP3(const int32_t& dataspace)
{
    return (dataspace == HAL_DATASPACE_DISPLAY_P3);
}

int getPrivateHandleLayerName(const buffer_handle_t& handle, std::string *name)
{
    int err = 0;

    if (!name || !name->empty()) {
        return 0;
    }

    if (hwc::GraphicBufferMapper::getInstance().isSupportMetadata())
    {
        buffer_handle_t imported_handle;

        err = hwc::GraphicBufferMapper::getInstance().importBuffer(handle, &imported_handle);
        if (err)
        {
            HWC_LOGW("%s(), importBuffer fail, err %d", __FUNCTION__, err);
            std::ostringstream ss;
            getBufferHandleInfo(handle, &ss);
            HWC_LOGW("%s", ss.str().c_str());
            return err;
        }

        err = hwc::GraphicBufferMapper::getInstance().getName(imported_handle, name);
        if (err)
        {
            HWC_LOGW("%s(), getName fail, err %d", __FUNCTION__, err);
            err = hwc::GraphicBufferMapper::getInstance().freeBuffer(imported_handle);
            if (err)
            {
                HWC_LOGW("%s(), freeBuffer fail, err %d", __FUNCTION__, err);
            }
            return err;
        }

        err = hwc::GraphicBufferMapper::getInstance().freeBuffer(imported_handle);
        if (err)
        {
            HWC_LOGW("%s(), freeBuffer fail, err %d, %s", __FUNCTION__, err, name->c_str());
            err = 0;    // we still get the name
        }
    }
    else
    {
        gralloc_extra_ion_debug_t info;
        err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_IOCTL_ION_DEBUG, &info);
        *name = info.name;
    }

    if (err)
    {
        HWC_LOGW("%s(), get layer name fail, err %d, Mapper support %d",
                 __FUNCTION__,
                 err,
                 hwc::GraphicBufferMapper::getInstance().isSupportMetadata());
    }

    return err;
}

bool usageHasProtected(const unsigned int usage)
{
    return usage & static_cast<unsigned int>(BufferUsage::PROTECTED) ||
           usage & static_cast<unsigned int>(GM_BUFFER_USAGE_PRIVATE_SECURE_DISPLAY);
}

bool usageHasSecure(const unsigned int usage)
{
    // BufferUsage::PROTECTED is count as Secure in HWC
    // TODO: will leave only usageHasProtected in the end, remove this API on Android T
    //return usage & static_cast<unsigned int>(GM_BUFFER_USAGE_PRIVATE_SECURE_DISPLAY);
    return usageHasProtected(usage);
}

bool usageHasProtectedOrSecure(const unsigned int usage)
{
    // BufferUsage::PROTECTED is count as Secure in HWC
    // TODO: will leave only usageHasProtected in the end, remove this API on Android T
    //return usageHasProtected(usage) || usageHasSecure(usage);
    return usageHasProtected(usage);
}

bool usageHasCameraOrientation(const unsigned int usage)
{
    return usage & GM_BUFFER_USAGE_PRIVATE_CAMERA_ORIENTATION;
}

void dumpTimeToTrace()
{
    static nsecs_t lastDumpTime = 0;
    const nsecs_t dumpPeriod = 1e9;
    const nsecs_t now = systemTime(CLOCK_MONOTONIC);
    if (now - lastDumpTime > dumpPeriod) {
        lastDumpTime = now;

        // get current time
        auto now = std::chrono::system_clock::now();

        // get number of milliseconds for the current second
        // (remainder after division into seconds)
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        // convert to std::time_t in order to convert to std::tm (broken time)
        auto timer = std::chrono::system_clock::to_time_t(now);

        // convert to broken time
        std::tm* bt = std::localtime(&timer);

        if (bt)
        {
            std::ostringstream ss;
            ss << "dbg_ts: " << std::put_time(bt, "%m-%d %T"); // HH:MM:SS
            ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
            ATRACE_NAME(ss.str().c_str());
        }
    }
}

#if PROFILE_MAPPER
#include <cmath>
struct ProfileStruct {
    std::vector<float> mapper;
    std::vector<float> mapper_import;
    std::vector<float> ge;
    int cnt;
};
static std::unordered_map<std::string, struct ProfileStruct> profiles;
int profileMapper(buffer_handle_t handle, PrivateHandle* priv_handle)
{
    int ret = 0;
    nsecs_t tic, toc;

    if (!hwc::GraphicBufferMapper::getInstance().isSupportMetadata())
    {
        return -ENOSYS;
    }

    buffer_handle_t imported_handle;
    tic = systemTime(SYSTEM_TIME_MONOTONIC);
    ret = hwc::GraphicBufferMapper::getInstance().importBuffer(handle, &imported_handle);
    toc = systemTime(SYSTEM_TIME_MONOTONIC);
    if (ret)
    {
        HWC_LOGW("%s(), importBuffer fail, ret %d", __FUNCTION__, ret);
        return -ENOSYS;
    }

    std::string name;
    ret = hwc::GraphicBufferMapper::getInstance().getName(imported_handle, &name);
    if (ret)
    {
        HWC_LOGW("%s(), getName fail, ret %d", __FUNCTION__, ret);
        ret = hwc::GraphicBufferMapper::getInstance().freeBuffer(imported_handle);
        if (ret)
        {
            HWC_LOGW("%s(), freeBuffer fail, ret %d", __FUNCTION__, ret);
        }
        return -ENOSYS;
    }

    struct ProfileStruct& profile = profiles[name];

    profile.mapper_import.push_back(static_cast<float>(toc-tic));

    // get Mapper info
    uint64_t w, h, size, usage, alloc_id;
    hwc::ui::PixelFormat fmt;

    ret = 0;
    tic = systemTime(SYSTEM_TIME_MONOTONIC);
    ret |= hwc::GraphicBufferMapper::getInstance().getWidth(imported_handle, &w);
    ret |= hwc::GraphicBufferMapper::getInstance().getHeight(imported_handle, &h);
    ret |= hwc::GraphicBufferMapper::getInstance().getPixelFormatRequested(imported_handle, &fmt);
    ret |= hwc::GraphicBufferMapper::getInstance().getAllocationSize(imported_handle, &size);
    ret |= hwc::GraphicBufferMapper::getInstance().getUsage(imported_handle, &usage);
    ret |= hwc::GraphicBufferMapper::getInstance().getBufferId(imported_handle, &alloc_id);
    toc = systemTime(SYSTEM_TIME_MONOTONIC);
    profile.mapper.push_back(static_cast<float>(toc - tic));

    if (ret)
    {
        HWC_LOGW("%s(), mapper ret %d, %s", __FUNCTION__, ret, name.c_str());
    }

    ret = hwc::GraphicBufferMapper::getInstance().freeBuffer(imported_handle);
    if (ret)
    {
        HWC_LOGW("%s(), freeBuffer fail, ret %d, %s", __FUNCTION__, ret, name.c_str());
    }

    // get gralloc extra info
    int value_int = 0;
    ret = 0;
    tic = systemTime(SYSTEM_TIME_MONOTONIC);
    ret |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_WIDTH, &priv_handle->width);
    ret |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_HEIGHT, &priv_handle->height);
    ret |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_FORMAT, &priv_handle->format);
    ret |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_ALLOC_SIZE, &priv_handle->size);
    ret |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_USAGE, &value_int);
    ret |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_ID, &priv_handle->alloc_id);
    toc = systemTime(SYSTEM_TIME_MONOTONIC);
    profile.ge.push_back(static_cast<float>(toc - tic));

    if (ret)
    {
        HWC_LOGW("%s(), ge ret %d", __FUNCTION__, ret);
    }

    // check equal
    ret = 0;
    if (w != static_cast<uint64_t>(priv_handle->width) && (profile.cnt % 60 <= 5))
    {
        HWC_LOGE("%s(), w not match %" PRIu64 ", %u, %s", __FUNCTION__, w, priv_handle->width, name.c_str());
        ret = -1;
    }
    if (h != static_cast<uint64_t>(priv_handle->height) && (profile.cnt % 60 <= 5))
    {
        HWC_LOGE("%s(), h not match %" PRIu64 ", %u, %s", __FUNCTION__, w, priv_handle->height, name.c_str());
        ret = -1;
    }
    if (static_cast<int32_t>(fmt) != static_cast<int32_t>(priv_handle->format) && (profile.cnt % 60 <= 5))
    {
        HWC_LOGE("%s(), fmt not match %d, %u, %s", __FUNCTION__, fmt, priv_handle->format, name.c_str());
        ret = -1;
    }
    if (size != static_cast<uint64_t>(priv_handle->size) && (profile.cnt % 60 <= 5))
    {
        HWC_LOGE("%s(), size not match %" PRIu64 ", %u, %s", __FUNCTION__, size, priv_handle->size, name.c_str());
        ret = -1;
    }
    if (usage != static_cast<uint64_t>(value_int) && (profile.cnt % 60 <= 5))
    {
        HWC_LOGE("%s(), usage not match 0x%" PRIx64 ", 0x%x, LE32 same %d, %s",
                 __FUNCTION__, usage, value_int, (usage & 0xffffffff) == static_cast<uint64_t>(value_int), name.c_str());
        ret = -1;
    }
    if (alloc_id != static_cast<uint64_t>(priv_handle->alloc_id) && (profile.cnt % 60 <= 5))
    {
        HWC_LOGV("%s(), alloc_id not match %" PRIu64 ", %" PRIu64", %s", __FUNCTION__, alloc_id, priv_handle->alloc_id, name.c_str());
        //ret = -1;
    }

    if (ret)
    {
        profile.cnt++;
    }

    // check time
    if (profile.mapper.size() >= 100)
    {
        static auto profile_func = [&](const char* name, std::vector<float>& vec)
                                   {
                                       float size = static_cast<float>(vec.size());
                                       float max = *std::max_element(vec.begin(), vec.end());
                                       float min = *std::min_element(vec.begin(), vec.end());
                                       float avg = std::accumulate(vec.begin(), vec.end(), 0) / size;

                                       static auto var_func = [&](float accumulator, const float& val)
                                                              {
                                                                  return accumulator + (val - avg) * (val - avg) / size;
                                                              };
                                       float std = sqrtf(std::accumulate(vec.begin(), vec.end(), 0, var_func));

                                       HWC_LOGI("profileMapper(), %s, max %.1f, min %.1f, avg %.1f, std %.1f, [0] %.1f",
                                                name, max, min, avg, std, vec[0]);

                                       vec.clear();
                                   };

        HWC_LOGI("%s(), statistic for %s", __FUNCTION__, name.c_str());
        profile_func("mapper_import", profile.mapper_import);
        profile_func("mapper", profile.mapper);
        profile_func("ge", profile.ge);
    }

    return 0;
}
#endif

void getFdInfo(int fd, std::ostringstream* ss)
{
    static pid_t pid = getpid();
    *ss << "fdInfo: fd(" << fd << ") ";
    if (fd < 0)
    {
        *ss << "failed to get fd info" << endl;
        return;
    }

    std::ostringstream dir_path;
    dir_path << "/proc/" << pid << "/fd/" << fd;
    char buf[1024] = {0};
    ssize_t res = readlink(dir_path.str().c_str(), buf, sizeof(buf) - 1);
    if (res < 0)
    {
        *ss << "failed to read link" << endl;
    }
    else
    {
        *ss << "link= " << buf << endl;
    }
}

void getBufferName(const buffer_handle_t& handle, std::ostringstream* ss)
{
    *ss << "bufferName: handle(" << handle << ") ";
    if (handle == 0)
    {
        *ss << "failed to get buffer name" << endl;
    }
    else
    {
        std::string name;
        int res = getPrivateHandleLayerName(handle, &name);
        if (res != 0)
        {
            *ss << "failed to get buffer name" << endl;
        }
        else
        {
            *ss << name << endl;
        }
    }
}

void getBufferHandleInfo(const buffer_handle_t& handle, std::ostringstream* ss)
{
    *ss << "buffer handle(" << handle << ")";
    if (handle == 0)
    {
        *ss << "failed to get buffer handle info" << endl;
    }
    else
    {
        *ss << " numFds[" << handle->numFds << "]" << endl;
        for (size_t i = 0; i < static_cast<size_t>(handle->numFds); i++)
        {
            *ss << "buf_hnd_data[" << i << "]= ";
            getFdInfo(handle->data[i], ss);
        }
    }
}

int32_t initSecureM4U()
{
    int m4u_fd = -1;
    int ret = -1;

    m4u_fd = open("/proc/m4u", O_RDONLY);
    if (m4u_fd < 0)   {
        HWC_LOGE("open /proc/m4u failed! errno=%d, %s\n", errno, strerror(errno));
        return -1;
    }

    if (1 == HwcFeatureList::getInstance().getFeature().svp_on_mtee)
    {
        // call mtee m4u sec_init api
        ret = ioctl(m4u_fd, MTK_M4U_GZ_SEC_INIT, 3); //SEC_ID_WFD = 3
    }
    else
    {
        ret = ioctl(m4u_fd, MTK_M4U_T_SEC_INIT, NULL);
    }
    if (ret)
    {
        HWC_LOGE("m4u ioctl sec_init failed! errno=%d, %s\n", errno, strerror(errno));
        close(m4u_fd);
        return -1;
    }
    HWC_LOGI("m4u sec_init sucess\n");
    close(m4u_fd);
    return 0;
}

bool isUserLoad()
{
    static bool need_check = true;
    static bool is_user_build = false;

    if (CC_UNLIKELY(need_check))
    {
        need_check = false;

        char value[PROPERTY_VALUE_MAX] = {0};
        property_get("ro.build.type", value, "user");
        is_user_build = strcmp(value, "user") == 0;
        HWC_LOGI("is_user_build %d", is_user_build);
    }

    return is_user_build;
}

bool isInternalLoad()
{
#ifdef MTK_HWC_IS_INTERNAL_LOAD
    return true;
#else
    return false;
#endif
}

bool isSupportDmaBuf()
{
    static bool need_check = true;
    static bool is_support_dma_buf = false;

    if (CC_UNLIKELY(need_check))
    {
        need_check = false;

        if (BufferAllocator::CheckIonSupport()) {
            HWC_LOGI("use ion");
            is_support_dma_buf = false;
        } else {
            HWC_LOGI("use dma buf");
            is_support_dma_buf = true;
        }
    }

    return is_support_dma_buf;
}

void copyMMLCfg(mml_submit* src, mml_submit* dst)
{
    if (NULL == src || NULL== dst)
        return;

    struct mml_job* job = NULL;
    struct mml_pq_param *pq_param[MML_MAX_OUTPUTS] = {NULL, NULL};

    // keep dst pointer
    job = dst->job;
    for (int i = 0; i < MML_MAX_OUTPUTS; ++i)
        pq_param[i] = dst->pq_param[i];

    // cpy
    memcpy(dst, src, sizeof(mml_submit));

    // assign ori dst pointer back
    dst->job = job;
    for (int i = 0; i < MML_MAX_OUTPUTS; ++i)
        dst->pq_param[i] = pq_param[i];

    // cpy pointer mem
    if (src->job && dst->job)
        memcpy(dst->job, src->job, sizeof(mml_job));

    for (int i = 0; i < MML_MAX_OUTPUTS; ++i)
        if (src->pq_param[i] && dst->pq_param[i])
            memcpy(dst->pq_param[i], src->pq_param[i], sizeof(mml_pq_param));
}

void copyOverlayPortParam(const OverlayPortParam& src, OverlayPortParam* dst)
{
    if (NULL== dst)
        return;

    struct mml_submit* dst_mml_submit = NULL;

    // keep dst pointer
    dst_mml_submit = dst->mml_cfg;
    memcpy(dst, &src, sizeof(OverlayPortParam));

    // assign ori pointer back
    dst->mml_cfg = dst_mml_submit;

    if (NULL != src.mml_cfg)
    {
        if (NULL == dst->mml_cfg)
        {
            dst->allocMMLCfg();
        }
        copyMMLCfg(src.mml_cfg, dst->mml_cfg);
    }
}

bool boundaryCut(const hwc_frect_t& src_source_crop,
                 const hwc_rect_t& src_display_frame,
                 unsigned int transform,
                 const hwc_rect_t& display_boundry,
                 hwc_frect_t* dst_source_crop,
                 hwc_rect_t* dst_display_frame)
{
    if (src_display_frame.left >= display_boundry.right ||
        src_display_frame.top >= display_boundry.bottom ||
        src_display_frame.right <= display_boundry.left ||
        src_display_frame.bottom <= display_boundry.top)
    {
        // src_display_frame is out of display_boundry
        *dst_source_crop = hwc_frect_t{0.0f, 0.0f, 0.0f, 0.0f};
        *dst_display_frame = hwc_rect_t{0, 0, 0, 0};
        return true;
    }

    *dst_source_crop = src_source_crop;
    *dst_display_frame = src_display_frame;

    if (src_display_frame.left >= display_boundry.left &&
        src_display_frame.top >= display_boundry.top &&
        src_display_frame.right <= display_boundry.right &&
        src_display_frame.bottom <= display_boundry.bottom)
    {
        // src_display_frame does not over display_boundry, do nothing
        return false;
    }

    double ratioWidth = 0.0f;
    double ratioHeight = 0.0f;
    if (transform == HAL_TRANSFORM_ROT_90 ||
        transform == HAL_TRANSFORM_ROT_270)
    {
        ratioWidth = static_cast<double>(HEIGHT(src_source_crop) / WIDTH(src_display_frame));
        ratioHeight = static_cast<double>(WIDTH(src_source_crop) / HEIGHT(src_display_frame));
    }
    else
    {
        ratioWidth = static_cast<double>(WIDTH(src_source_crop) / WIDTH(src_display_frame));
        ratioHeight = static_cast<double>(HEIGHT(src_source_crop) / HEIGHT(src_display_frame));
    }

    int32_t leftDispMargin = display_boundry.left <= src_display_frame.left ? 0 :
                             (display_boundry.left - src_display_frame.left);
    int32_t topDispMargin = display_boundry.top <= src_display_frame.top ? 0 :
                             (display_boundry.top - src_display_frame.top);
    int32_t rightDispMargin = display_boundry.right >= src_display_frame.right ? 0 :
                             (src_display_frame.right - display_boundry.right);
    int32_t bottomDispMargin = display_boundry.bottom >= src_display_frame.bottom ? 0 :
                             (src_display_frame.bottom - display_boundry.bottom);
    if (leftDispMargin > 0) dst_display_frame->left = display_boundry.left;
    if (topDispMargin > 0) dst_display_frame->top = display_boundry.top;
    if (rightDispMargin > 0) dst_display_frame->right = display_boundry.right;
    if (bottomDispMargin > 0) dst_display_frame->bottom = display_boundry.bottom;

    switch (transform)
    {
        case HAL_TRANSFORM_ROT_90:
            dst_source_crop->left += topDispMargin > 0 ? static_cast<int32_t>(topDispMargin * ratioHeight + 0.5f) : 0;
            dst_source_crop->top += rightDispMargin > 0 ? static_cast<int32_t>(rightDispMargin * ratioWidth + 0.5f) : 0;
            dst_source_crop->right -= bottomDispMargin > 0 ? static_cast<int32_t>(bottomDispMargin * ratioHeight + 0.5f) : 0;
            dst_source_crop->bottom -= leftDispMargin > 0 ? static_cast<int32_t>(leftDispMargin * ratioWidth + 0.5f) : 0;
            break;

        case HAL_TRANSFORM_ROT_180:
            dst_source_crop->left += rightDispMargin > 0 ? static_cast<int32_t>(rightDispMargin * ratioWidth + 0.5f) : 0;
            dst_source_crop->top += bottomDispMargin > 0 ? static_cast<int32_t>(bottomDispMargin * ratioHeight + 0.5f) : 0;
            dst_source_crop->right -= leftDispMargin > 0 ? static_cast<int32_t>(leftDispMargin * ratioWidth + 0.5f) : 0;
            dst_source_crop->bottom -= topDispMargin > 0 ? static_cast<int32_t>(topDispMargin * ratioHeight + 0.5f) : 0;
            break;

        case HAL_TRANSFORM_ROT_270:
            dst_source_crop->left += bottomDispMargin > 0 ? static_cast<int32_t>(bottomDispMargin * ratioHeight + 0.5f) : 0;
            dst_source_crop->top += leftDispMargin > 0 ? static_cast<int32_t>(leftDispMargin * ratioWidth + 0.5f) : 0;
            dst_source_crop->right -= topDispMargin > 0 ? static_cast<int32_t>(topDispMargin * ratioHeight + 0.5f) : 0;
            dst_source_crop->bottom -= rightDispMargin > 0 ? static_cast<int32_t>(rightDispMargin * ratioWidth + 0.5f) : 0;
            break;

        default:
            dst_source_crop->left += leftDispMargin > 0 ? static_cast<int32_t>(leftDispMargin * ratioWidth + 0.5f) : 0;
            dst_source_crop->top += topDispMargin > 0 ? static_cast<int32_t>(topDispMargin * ratioHeight + 0.5f) : 0;
            dst_source_crop->right -= rightDispMargin > 0 ? static_cast<int32_t>(rightDispMargin * ratioWidth + 0.5f) : 0;
            dst_source_crop->bottom -= bottomDispMargin > 0 ? static_cast<int32_t>(bottomDispMargin * ratioHeight + 0.5f) : 0;
            break;
    }

    return true;
}

struct UclampAttr {
    uint32_t size;
    uint32_t not_used_param1;
    uint64_t flags;
    int32_t not_used_param2;
    int32_t not_used_param3;
    uint64_t not_used_param4;
    uint64_t not_used_param5;
    uint64_t not_used_param6;
    uint32_t min;
    uint32_t max;
};

static long uclamp_set(pid_t pid, UclampAttr * attr)
{
#ifdef __NR_sched_setattr
    return syscall(__NR_sched_setattr, pid, attr, 0);
#else
    HWC_LOGV("pid %ld, attr %p", static_cast<long>(pid), attr);
    return -1;
#endif
}

static long uclamp_get(pid_t pid, UclampAttr * attr)
{
#ifdef __NR_sched_getattr
    return syscall(__NR_sched_getattr, pid, attr, sizeof(UclampAttr), 0);
#else
    HWC_LOGV("pid %ld, attr %p", static_cast<long>(pid), attr);
    return -1;
#endif
}

int uclamp_task(pid_t pid, uint32_t uclamp_min)
{
    struct UclampAttr attr  = {};
    attr.size = sizeof(attr);
    attr.flags = (SCHED_FLAG_KEEP_ALL | SCHED_FLAG_UTIL_CLAMP); // keep all

    if (uclamp_get(pid, &attr) != 0 )
    {   // TODO: call only first time?
        HWC_LOGW("uclamp_get %s", strerror(errno));
    }

    HWC_LOGV("old PID: %ld uclamp min %d max %d", static_cast<long>(pid), attr.min, attr.max);

    if (uclamp_min > 1024) // MAX is 1024
    {
        uclamp_min = 1024;
    }

    if (attr.min != uclamp_min)
    {
        attr.size = sizeof(attr);
        attr.min = uclamp_min;
        attr.flags = (SCHED_FLAG_KEEP_ALL | SCHED_FLAG_UTIL_CLAMP_MIN); // keep all

        HWC_LOGD("new PID: %ld uclamp min %d", static_cast<long>(pid), attr.min);

        if (uclamp_set(pid, &attr) != 0)
        {
            HWC_LOGW("uclamp_set %s", strerror(errno));
        }
    }

    return 0;
}

static int setPerferredCpu(pid_t tid, unsigned int cpu_mask)
{
    cpu_set_t mask;
    CPU_ZERO(&mask);
    int count = popcount(cpu_mask);
    for (unsigned int i = 0; i < 32 && count > 0; i++)
    {
        if (cpu_mask & (1 << i))
        {
            CPU_SET(i, &mask);
            count--;
        }
    }
    return sched_setaffinity(tid, sizeof(mask), &mask);
}

static void changeCpuSet2System(pid_t tid, unsigned int cpu_mask)
{
    if (!SetTaskProfiles(tid, {"SFMainPolicy"}))
    {
        HWC_LOGE("failed to set cpuset of process(%d) to SFMainPolicy", tid);
    }

    int res = setPerferredCpu(tid, cpu_mask);
    if (res < 0)
    {
        HWC_LOGE("fail to sched_setaffinity(tid=%d,mask=0x%x): %d", tid, cpu_mask, res);
    }
}

static void changeCpuSet2Foreground(pid_t tid, unsigned int cpu_mask)
{
    if (!SetTaskProfiles(tid, {"ProcessCapacityNormal"}))
    {
        HWC_LOGE("failed to set cpuset of process(%d) to ProcessCapacityNormal", tid);
    }

    int res = setPerferredCpu(tid, cpu_mask);
    if (res < 0)
    {
        HWC_LOGE("fail to sched_setaffinity(tid=%d,mask=0x%x): %d", tid, cpu_mask, res);
    }
}

void changeCpuSet(pid_t tid, unsigned int cpu_set)
{
    unsigned int cpu_mask = 0;
    bool use_foreground = false;
    if (cpu_set == HWC_CPUSET_NONE)
    {
        // HWC is background service. if we do not have any special request, run it on little core.
        cpu_mask = Platform::getInstance().m_config.cpu_set_index.little;
    }
    else
    {
        if (cpu_set & HWC_CPUSET_LITTLE)
        {
            cpu_mask |= Platform::getInstance().m_config.cpu_set_index.little;
        }
        if (cpu_set & HWC_CPUSET_MIDDLE)
        {
            cpu_mask |= Platform::getInstance().m_config.cpu_set_index.middle;
            // background service default use little core only, HWC has to become foreground
            use_foreground = true;
        }
        if (cpu_set & HWC_CPUSET_BIG)
        {
            cpu_mask |= Platform::getInstance().m_config.cpu_set_index.big;
            // background service default use little core only, HWC has to become foreground
            use_foreground = true;
        }
    }

    if (use_foreground)
    {
        changeCpuSet2Foreground(tid, cpu_mask);
    }
    else
    {
        changeCpuSet2System(tid, cpu_mask);
    }
}

uint32_t calculateCpuMHz(float mc, nsecs_t remain_time)
{
    return static_cast<uint32_t>(ceilf(((mc) * 1000 * 1000 * 1000) / remain_time));
}

uint32_t cpuMHzToUClamp(uint32_t cpu_mhz)
{
    uint32_t error_ret_val = UINT32_MAX;

    if (cpu_mhz > Platform::getInstance().m_config.uclamp_cpu_table.back().cpu_mhz)
    {
        return error_ret_val;
    }

    for (auto& pair : Platform::getInstance().m_config.uclamp_cpu_table)
    {
        if (pair.cpu_mhz >= cpu_mhz)
        {
            return pair.uclamp;
        }
    }

    HWC_LOGW("cannot get uclamp, target_cpu_mhz %u", cpu_mhz);
    return error_ret_val;
}

const HwcMCycleInfo& getScenarioMCInfo(DispatcherJob* job)
{
    if (CC_UNLIKELY(Platform::getInstance().m_config.hwc_mcycle_table.empty()))
    {
        HWC_LOGE("hwc_mcycle_table empty");
        HWC_ASSERT(0);
    }

    if (CC_UNLIKELY(!job))
    {
        return Platform::getInstance().m_config.hwc_mcycle_table.back();
    }

    int num_ui = job->num_ui_layers + (job->fbt_exist ? 1 : 0);
    int num_mm = job->num_mm_layers + job->num_glai_layers; // TODO: glai
    // TODO: mm w/ different PQ
    int cur_id;

    if (num_mm)
    {
        switch (num_ui)
        {
            case 0:
                cur_id = HWC_MC_0U_1M;
                break;
            case 1:
                cur_id = HWC_MC_1U_1M;
                break;
            case 2:
                cur_id = HWC_MC_2U_1M;
                break;
            case 3:
                cur_id = HWC_MC_3U_1M;
                break;
            case 4:
                cur_id = HWC_MC_4U_1M;
                break;
            case 5:
                cur_id = HWC_MC_5U_1M;
                break;
            default:
                cur_id = HWC_MC_5U_1M;
                break;
        }
    }
    else
    {
        switch (num_ui)
        {
            case 1:
                cur_id = HWC_MC_1U;
                break;
            case 2:
                cur_id = HWC_MC_2U;
                break;
            case 3:
                cur_id = HWC_MC_3U;
                break;
            case 4:
                cur_id = HWC_MC_4U;
                break;
            case 5:
                cur_id = HWC_MC_5U;
                break;
            case 6:
                cur_id = HWC_MC_6U;
                break;
            default:
                cur_id = HWC_MC_6U;
                break;
        }
    }

    for (auto& p : Platform::getInstance().m_config.hwc_mcycle_table)
    {
        if (p.id != cur_id)
        {
            continue;
        }

        return p;
    }

    return Platform::getInstance().m_config.hwc_mcycle_table.back();
}

int mapComposerExtDisplayType(ComposerExt::DisplayType& dpy, hwc2_display_t* disp_id)
{
    switch (dpy)
    {
        case ComposerExt::DisplayType::kPrimary:
            *disp_id = HWC_DISPLAY_PRIMARY;
            break;
        case ComposerExt::DisplayType::kExternal:
            *disp_id = HWC_DISPLAY_EXTERNAL;
            break;
        case ComposerExt::DisplayType::kVirtual:
            *disp_id = HWC_DISPLAY_VIRTUAL;
            break;

        // Not support yet
        case ComposerExt::DisplayType::kInvalid:
        case ComposerExt::DisplayType::kBuiltIn2:
        default:
            return -ENOTSUP;
    }
    return 0;
}

bool isNoDispatchThread()
{
    static bool need_check = true;
    static bool is_no_dp = false;

    if (CC_UNLIKELY(need_check))
    {
        need_check = false;

        is_no_dp = (Platform::getInstance().m_config.plat_switch & HWC_PLAT_SWITCH_NO_DISPATCH_THREAD) != 0;

        HWC_LOGI("%s: %d", __FUNCTION__, is_no_dp);
    }

    return is_no_dp;
}

uint32_t calculateWdmaProxCost(uint64_t disp_id, uint32_t format, Rect roi, uint32_t dst_w, uint32_t dst_h)
{
    if (static_cast<uint32_t>(roi.getWidth()) > dst_w || static_cast<uint32_t>(roi.getHeight()) > dst_h)
    {
        const DisplayData* display_data = DisplayManager::getInstance().getDisplayData(disp_id);
        return dst_w * (dst_h / static_cast<uint32_t>(display_data->height)) * getBitsPerPixel(format);
    }
    else
    {
        return dst_w * getBitsPerPixel(format);
    }
}

int getPrivateHandleFBT(
    buffer_handle_t handle, PrivateHandle* priv_handle, std::string* name)
{
    if (NULL == handle)
    {
        HWC_LOGE("%s NULL handle !!!!!", __func__);
        return -EINVAL;
    }

    priv_handle->handle = handle;
    int err = getPrivateHandleInfo(handle, priv_handle, name);
    err |= getPrivateHandleInfoModifyPerFrame(handle, priv_handle);

    if (Platform::getInstance().m_config.is_smart_composition_support)
    {
        err |= getPrivateHandleFBTInfo(handle, priv_handle);
    }

    if (err != GRALLOC_EXTRA_OK)
    {
        HWC_LOGE("%s err(%d), (handle=%p)", __func__, err, handle);
        return -EINVAL;
    }

    err = gralloc_extra_query(handle, GRALLOC_EXTRA_GET_FB_MVA, &priv_handle->fb_mva);
    bool is_use_ion = (err != GRALLOC_EXTRA_OK);

    if (is_use_ion)
    {
        err = gralloc_extra_query(handle, GRALLOC_EXTRA_GET_ION_FD, &priv_handle->ion_fd);
        if (err != GRALLOC_EXTRA_OK)
        {
            HWC_LOGE("%s Failed to get ION fd, err(%d), (handle=%p) !!", __func__, err, handle);
            return -EINVAL;
        }
    }

    if (usageHasSecure(priv_handle->usage))
    {
        if (!isSupportDmaBuf())
        {
            err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_SECURE_HANDLE, &priv_handle->sec_handle);
        }
    }
    else if ((static_cast<unsigned int>(priv_handle->ext_info.status) & GRALLOC_EXTRA_MASK_SECURE) == GRALLOC_EXTRA_BIT_SECURE)
    {
        err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_SECURE_HANDLE_HWC, &priv_handle->sec_handle);
    }

    if (err != GRALLOC_EXTRA_OK)
    {
        HWC_LOGE("%s err(%d), (handle=%p) !!", __func__, err, handle);
        return -EINVAL;
    }

    return 0;
}

int getPrivateHandleFBTInfo(
    buffer_handle_t handle, PrivateHandle* priv_handle)
{
    int err = 0;

    // get fbt_cache_hit info for GPU Cahce
    ge_fbt_cache_info_t fbt_cahce_info;
    memset(&fbt_cahce_info, 0, sizeof(fbt_cahce_info));
    err |= gralloc_extra_query(handle, GRALLOC_EXTRA_GET_FBT_CACHE_INFO, &fbt_cahce_info);
    priv_handle->fbt_unchanged = fbt_cahce_info.fbt_cache_hit;

    HWC_LOGV("%s() ct_handle:%p alloc_id:%" PRIu64 " fbt_cache_hit:%d ", __func__, handle, priv_handle->alloc_id, fbt_cahce_info.fbt_cache_hit);
    if (err)
    {
        HWC_LOGE("%s err(%d), (handle=%p), (priv_handle:%p)", __func__, err, handle, priv_handle);
    }

    return  err;
}

#endif // USE_HWC2
