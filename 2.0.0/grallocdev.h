#ifndef HWC_GRALLOC_DEV_H
#define HWC_GRALLOC_DEV_H

#include <vndk/hardware_buffer.h>
#include <utils/Errors.h>
#include <utils/Mutex.h>
#include <map>

using namespace android;

class GrallocDevice
{
public:
    static GrallocDevice& getInstance();
    virtual ~GrallocDevice();

    struct AllocParam
    {
        AllocParam()
            : width(0), height(0), format(0)
            , usage(0), handle(NULL), stride(0)
        { }

        unsigned int width;
        unsigned int height;
        unsigned int format;
        uint64_t usage;

        buffer_handle_t handle;
        int stride;
    };

    // allocate memory by gralloc driver
    status_t alloc(AllocParam& param);

    // free a previously allocated buffer
    status_t free(buffer_handle_t handle);

    // dump information of allocated buffers
    void dump() const;

private:
    GrallocDevice();

    std::map<buffer_handle_t, AHardwareBuffer*> m_buffers;
    mutable Mutex m_buffers_mutex;
};

#endif
