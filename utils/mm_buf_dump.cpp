#define DEBUG_LOG_TAG "MM_BUF_DUMP"
#include "utils/mm_buf_dump.h"

#include <dlfcn.h>
#include <sys/mman.h>
#include <mmdump_fmt.h>
#include <graphics_mtk_defs.h>
#include <gm/gm_pixel_format.h>

#include "utils/debug.h"

using namespace mediatek::graphics::common;

MmBufDump& MmBufDump::getInstance()
{
    static MmBufDump gInstance;
    return gInstance;
}

MmBufDump::MmBufDump()
    : m_lib_handle(nullptr)
    , m_dump_ptr(nullptr)
{
    loadLib();
}

MmBufDump::~MmBufDump()
{
    releaseLib();
}

void MmBufDump::loadLib()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_lib_handle == nullptr)
    {
        m_lib_handle = dlopen(MM_DUMP_LIB_NAME, RTLD_NOW);
        if (m_lib_handle == nullptr)
        {
            HWC_LOGW("failed to dlopen %s: %s", MM_DUMP_LIB_NAME, dlerror());
            return;
        }

        void* ptr = dlsym(m_lib_handle, "dump2");
        m_dump_ptr = reinterpret_cast<mmdump2_func>(ptr);
        if (m_dump_ptr == nullptr)
        {
            HWC_LOGW("failed to dlsym dump2(): %s", dlerror());
            releaseLib();
        }
    }
}

void MmBufDump::releaseLib()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_lib_handle != nullptr)
    {
        m_dump_ptr = nullptr;
        dlclose(m_lib_handle);
        m_lib_handle = nullptr;
    }
}

void MmBufDump::dump(int32_t ion_fd, uint64_t uid, uint32_t size, uint32_t width,
                     uint32_t height, uint32_t color_space, uint32_t format,
                     uint32_t hstride, uint32_t vstride, const char* module)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_dump_ptr != nullptr)
    {
        void* ptr = mmap(nullptr, size, PROT_READ, MAP_SHARED, ion_fd, 0);
        if (ptr != reinterpret_cast<void*>(-1))
        {
            m_dump_ptr(ptr, uid, size, width, height, color_space,
                       convertHalFmt2MmDumpFmt(format), hstride,
                       vstride, module);
            munmap(ptr, size);
        }
        else
        {
            HWC_LOGW("failed to map ion_fd(%d), so give up dump buffer", ion_fd);
        }
    }
}

uint32_t MmBufDump::convertHalFmt2MmDumpFmt(uint32_t format)
{
    switch (format)
    {
        case HAL_PIXEL_FORMAT_RGBA_8888:
            return MMDUMP_FMT_ABGR8888;

        case HAL_PIXEL_FORMAT_RGBX_8888:
            return MMDUMP_FMT_XBGR8888;

        case HAL_PIXEL_FORMAT_RGB_888:
            return MMDUMP_FMT_BGR888;

        case HAL_PIXEL_FORMAT_BGR_888:
            return MMDUMP_FMT_RGB888;

        case HAL_PIXEL_FORMAT_RGB_565:
            return MMDUMP_FMT_RGB565;

        case HAL_PIXEL_FORMAT_BGRA_8888:
            return MMDUMP_FMT_ARGB8888;

        case HAL_PIXEL_FORMAT_YCBCR_422_SP:
            return MMDUMP_FMT_NV16;

        case HAL_PIXEL_FORMAT_YCRCB_420_SP:
            return MMDUMP_FMT_NV21;

        case HAL_PIXEL_FORMAT_YCBCR_422_I:
            return MMDUMP_FMT_YUYV;

        case HAL_PIXEL_FORMAT_RGBA_FP16:
            return MMDUMP_FMT_ABGRFP16;

        case HAL_PIXEL_FORMAT_RGBA_1010102:
            return MMDUMP_FMT_ABGR2101010;

        case HAL_PIXEL_FORMAT_YV12:
            return MMDUMP_FMT_YVU420;

        case HAL_PIXEL_FORMAT_BGRX_8888:
            return MMDUMP_FMT_XRGB8888;

        case HAL_PIXEL_FORMAT_YUYV:
            return MMDUMP_FMT_YUYV;

        case HAL_PIXEL_FORMAT_NV12:
            return MMDUMP_FMT_NV12;
    }

    return format;
}
