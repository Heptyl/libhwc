#ifndef HWC_QUEUE_H_
#define HWC_QUEUE_H_

#include <utils/Vector.h>
#include <utils/threads.h>
#include <utils/String8.h>
#include <linux/mediatek_drm.h>

#include "hwc_ui/Rect.h"
#include "utils/tools.h"

using namespace android;
using hwc::Rect;

// ---------------------------------------------------------------------------

// DisplayBufferQueue manages a pool of display buffer slots.
class DisplayBufferQueue : public virtual RefBase
{
public:
    enum { NUM_BUFFER_SLOTS = 3 };
    enum { INVALID_BUFFER_SLOT = -1 };
    enum { NO_BUFFER_AVAILABLE = -1 };

    enum
    {
        QUEUE_TYPE_NONE = 0,
        QUEUE_TYPE_BLT  = 1,
        QUEUE_TYPE_OVL  = 2,
        QUEUE_TYPE_GLAI = 3,
        QUEUE_TYPE_AI_BLD_DISP = 4,
        QUEUE_TYPE_AI_BLD_MDP = 5,
        QUEUE_TYPE_NUM
    };

    struct ConsumerListener : public virtual RefBase
    {
        // onBufferQueued() is called when producer queues new frame
        virtual void onBufferQueued() = 0;
    };

    struct BufferParam
    {
        BufferParam()
            : disp_id(-1), pool_id(0)
            , width(0), height(0), pitch(0), format(0), size(0)
            , protect(false), dequeue_block(true), sw_usage(false)
            , compression(false)
        { }
        int disp_id;
        int pool_id;
        unsigned int width;
        unsigned int height;
        unsigned int pitch;
        unsigned int format;
        unsigned int size;
        bool protect;
        bool dequeue_block;
        bool sw_usage;
        bool compression;
    };

    // QueuedExtraInfo is used for producer to pass more infomation to consumer
    struct QueuedExtraInfo
    {
        QueuedExtraInfo()
            : is_sharpen(false)
        {
            src_crop.makeInvalid();
            dst_crop.makeInvalid();
            memset(&ethdr_meta, 0, sizeof(mtk_ethdr_metadata));
        }
        // src_crop is valid data region
        Rect src_crop;

        // dst_crop is used drawing region for consumer is needed
        Rect dst_crop;

        // is_sharpen is used to enable 2D sharpness
        bool is_sharpen;

        // pass HDR metadata to consumer
        struct mtk_ethdr_metadata ethdr_meta;
    };

    // DisplayBuffer is the buffer used by the producer/consumer sides
    // (i.e., PRODUCER <-> DisplayBufferQueue <-> CONSUMER)
    struct DisplayBuffer
    {
        DisplayBuffer()
            : src_handle(NULL)
            , out_handle(NULL)
            , out_ion_fd(-1)
            , out_sec_handle(0)
            , data_size(0)
            , data_pitch(0)
            , data_v_pitch(0)
            , data_format(0)
            , data_is_compress(false)
            , timestamp(0)
            , frame_num(0)
            , protect(false)
            , secure(false)
            , alpha_enable(0)
            , alpha(0xFF)
            , blending(0)
            , sequence(0)
            , acquire_fence(-1)
            , release_fence(-1)
            , index(INVALID_BUFFER_SLOT)
            , ext_sel_layer(-1)
            , dataspace(HAL_DATASPACE_UNKNOWN)
            , data_width(0)
            , data_height(0)
            , handle_stride(0)
            , alloc_id(UINT64_MAX)
            , hwc_layer_id(UINT64_MAX)
            , buffer_size(0)
            , compression(false)
        { }

        // src_handle is the source buffer handle
        buffer_handle_t src_handle;

        // out_handle is the output buffer handle
        buffer_handle_t out_handle;

        // out_ion_fd is for normal buffer
        int out_ion_fd;

        // out_sec_handle is for secure buffer
        SECHAND out_sec_handle;

        // data_size is memory allocated size
        unsigned int data_size;

        // data_pitch is valid data stride
        unsigned int data_pitch;

        // data_v_pitch is valid data v stride
        unsigned int data_v_pitch;

        // data_format is data format
        unsigned int data_format;

        // data_info is for producer fill extra information
        QueuedExtraInfo data_info;

        // data_is_compress is data_is_compress
        bool data_is_compress;

        // timestamp is the current timestamp for this buffer.
        int64_t timestamp;

        // frame_num is the number of the queued frame for this buffer.
        uint64_t frame_num;

        // protect means if this buffer is protected
        bool protect;

        // secure means if this buffer is secure
        bool secure;

        // alpha_enable is used for enabling constant alpha
        unsigned int alpha_enable;

        // alpha is used for setting the value of constant alpha
        unsigned char alpha;

        // blending is used for setting blending mode
        int blending;

        // sequence is used as a sequence number for profiling latency purpose
        uint64_t sequence;

        // acquire_fence is used for consumer to wait producer
        int acquire_fence;

        // release_fence is used for producer to wait consumer
        int release_fence;

        // index is the slot index of this buffer
        int index;

        // smart layer index
        int ext_sel_layer;

        int32_t dataspace;

        // data_width is the width of buffer
        unsigned int data_width;

        // data_height is the height of buffer
        unsigned int data_height;

        // handle_stride is the stride size from private handle
        unsigned int handle_stride;

        // alloc_id is the unique id from gralloc
        uint64_t alloc_id;

        uint64_t hwc_layer_id;

        // buffer_size is total bytes allocated by gralloc
        int buffer_size;

        bool compression;
    };

    DisplayBufferQueue(int type, uint64_t id = UINT_MAX);
    ~DisplayBufferQueue();

    uint64_t getId() { return m_id; }

    int32_t getReleaseFence() { return m_rel_fence_fd; }
    void setReleaseFence(int32_t fence_fd) { m_rel_fence_fd = fence_fd; }
    DisplayBuffer* getLastAcquiredBufEditable() { return &m_last_acquired_buf; }

    ////////////////////////////////////////////////////////////////////////
    // PRODUCER INTERFACE

    // setBufferParam() updates parameter
    status_t setBufferParam(BufferParam& param);

    // updateBufferSize() can change buffer size next time to dequeue
    status_t updateBufferSize(unsigned int width, unsigned int height);

    // dequeueBuffer() gets the next buffer slot index for the client to use,
    // this one can get secure buffer.
    status_t dequeueBuffer(DisplayBuffer* buffer, bool async, bool is_secure = false);

    // queueBuffer() returns a filled buffer to the DisplayBufferQueue.
    status_t queueBuffer(DisplayBuffer* buffer);

    // cancelBuffer() lets producer to give up a dequeued buffer
    status_t cancelBuffer(int index);

    // setSynchronousMode() set dequeueBuffer as sync or async
    status_t setSynchronousMode(bool enabled);

    enum QUEUE_DUMP_CONDITION
    {
        QUEUE_DUMP_NONE          = 0,
        QUEUE_DUMP_LAST_ACQUIRED = 1,
        QUEUE_DUMP_ALL_QUEUED    = 2,
    };
    // dump() is for debug purpose
    void dump(QUEUE_DUMP_CONDITION cond);

    ////////////////////////////////////////////////////////////////////////
    // CONSUMER INTERFACE

    // acquireBuffer() attempts to acquire the next pending buffer by consumer
    status_t acquireBuffer(DisplayBuffer* buffer, bool async = false);

    // releaseBuffer() releases a buffer slot from the consumer back
    status_t releaseBuffer(int index, int fence = -1);

    // setConsumerListener() sets a consumer listener to the DisplayBufferQueue.
    void setConsumerListener(const sp<ConsumerListener>& listener);

private:
    // reallocate() is used to reallocate buffer
    status_t reallocate(unsigned int idx, bool is_secure);

    // drainQueueLocked() drains the buffer queue when change to asynchronous mode
    status_t drainQueueLocked();

    // dumpLocked() is used to dump buffers
    void dumpLocked(int idx);

    // BufferSlot is a buffer slot that contains DisplayBuffer information
    // and holds a buffer state for buffer management
    struct BufferSlot
    {
        BufferSlot()
            : state(BufferSlot::FREE)
            , pool_id(0)
            , src_handle(NULL)
            , out_handle(NULL)
            , out_ion_fd(-1)
            , out_sec_handle(0)
            , data_size(0)
            , data_pitch(0)
            , data_v_pitch(0)
            , data_format(0)
            , data_is_compress(false)
            , timestamp(0)
            , frame_num(0)
            , protect(false)
            , secure(false)
            , alpha_enable(0)
            , alpha(0xFF)
            , blending(0)
            , sequence(0)
            , acquire_fence(-1)
            , release_fence(-1)
            , ext_sel_layer(-1)
            , dataspace(HAL_DATASPACE_UNKNOWN)
            , data_width(0)
            , data_height(0)
            , handle_stride(0)
            , alloc_id(UINT64_MAX)
            , hwc_layer_id(UINT64_MAX)
            , buffer_size(0)
            , compression(false)
        { }

        enum BufferState {
            FREE = 0,
            DEQUEUED = 1,
            QUEUED = 2,
            ACQUIRED = 3
        };

        // state is the current state of this buffer slot.
        BufferState state;

        // pool_id is used to identify if preallocated buffer pool could be used
        int pool_id;

        // src_handle is the source buffer handle
        buffer_handle_t src_handle;

        // out_handle is the output buffer handle
        buffer_handle_t out_handle;

        // out_ion_fd is for normal buffer
        int out_ion_fd;

        // out_sec_handle is for secure buffer
        SECHAND out_sec_handle;

        // data_size is data size
        unsigned int data_size;

        // data_pitch is valid data stride
        unsigned int data_pitch;

        // data_v_pitch is valid data v stride
        unsigned int data_v_pitch;

        // data_format is data format
        unsigned int data_format;

        // data_info is for producer fill extra information
        QueuedExtraInfo data_info;

        // data_is_compress is data_is_compress
        bool data_is_compress;

        // timestamp is the current timestamp for this buffer
        int64_t timestamp;

        // frame_num is the number of the queued frame for this buffer.
        uint64_t frame_num;

        // protect means if this buffer is protected
        bool protect;

        // secure means if this buffer is secure
        bool secure;

        // alpha_enable is used for enabling constant alpha
        unsigned int alpha_enable;

        // alpha is used for setting the value of constant alpha
        unsigned char alpha;

        // blending is used for setting blending mode
        int blending;

        // sequence is used as a sequence number for profiling latency purpose
        uint64_t sequence;

        // acquire_fence is a fence descriptor
        // used to signal buffer is filled by producer
        int acquire_fence;

        // release_fence is a fence descriptor
        // used to signal buffer is used by consumer
        int release_fence;

        // smart layer index
        int ext_sel_layer;

        int32_t dataspace;

        // data_width is the width of buffer
        unsigned int data_width;

        // data_width is the height of buffer
        unsigned int data_height;

        // handle_stride is the stride size from private handle
        unsigned int handle_stride;

        // alloc_id is the unique id from gralloc
        uint64_t alloc_id;

        uint64_t hwc_layer_id;

        // buffer_size is total bytes allocated by gralloc
        int buffer_size;

        bool compression;
    };

    BufferSlot m_slots[NUM_BUFFER_SLOTS];

    // m_client_name is used to debug
    String8 m_client_name;

    // m_queue_type is used to distiguish which engine creating queue
    int m_queue_type;

    // m_buffer_param is used to store buffer information
    BufferParam m_buffer_param;

    // m_buffer_ount is for the real buffer number in queue
    int m_buffer_count;

    // m_is_synchronous points whether we're in synchronous mode or not
    bool m_is_synchronous;

    // m_dequeue_condition condition used for dequeueBuffer in synchronous mode
    mutable Condition m_dequeue_condition;

    // m_queue is a FIFO of queued buffers used in synchronous mode
    typedef Vector<int> Fifo;
    Fifo m_queue;

    // m_mutex is the mutex used to prevent concurrent access to the member
    // variables of BufferQueue objects. It must be locked whenever the
    // member variables are accessed.
    mutable Mutex m_mutex;

    // m_frame_counter is the free running counter, incremented for every buffer
    // queued with the surface Texture.
    uint64_t m_frame_counter;

    // m_last_acquire_idx remembers index of last acquire buffer
    // and it is for dump purpose
    int m_last_acquire_idx;

    // these two are set by caller only, caller should handle race condition
    // queue will handle the close of fence
    int32_t m_rel_fence_fd;
    DisplayBuffer m_last_acquired_buf;

    sp<ConsumerListener> m_listener;

    uint64_t m_id;
};

#endif // HWC_QUEUE_H_
