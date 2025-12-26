#ifndef UTILS_MM_BUF_DUMP
#define UTILS_MM_BUF_DUMP

#include <stdint.h>
#include <mutex>

#include <mmdump.h>

#define MM_DUMP_LIB_NAME "libmmdump.so"

class MmBufDump
{
public:
    static MmBufDump& getInstance();
    ~MmBufDump();

    void dump(int32_t ion_fd, uint64_t uid, uint32_t size, uint32_t width, uint32_t height,
              uint32_t color_space, uint32_t format, uint32_t hstride, uint32_t vstride,
              const char* module);

private:
    MmBufDump();

    void loadLib();
    void releaseLib();

    uint32_t convertHalFmt2MmDumpFmt(uint32_t format);

    mutable std::mutex m_mutex;
    void* m_lib_handle;
    mmdump2_func m_dump_ptr;
};

#endif
