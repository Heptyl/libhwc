#define DEBUG_LOG_TAG "DBQ"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <linux/dma-buf.h>

#include <cutils/properties.h>

#include <hwc_feature_list.h>

#include "utils/debug.h"
#include "utils/tools.h"
#include "overlay.h"
#include "display.h"
#include "queue.h"
#include "sync.h"
#include "platform_wrap.h"
#include "hwc2.h"
#include "grallocdev.h"

#include <android/hardware/graphics/common/1.2/types.h>
using android::hardware::graphics::common::V1_2::BufferUsage;
#include <gm/gm_buffer_usage.h>
using namespace mediatek::graphics::common;

// Macros for including the BufferQueue name in log messages
#define QLOGV(x, ...) HWC_LOGV("(%s:%p) " x, m_client_name.string(), this, ##__VA_ARGS__)
#define QLOGD(x, ...) HWC_LOGD("(%s:%p) " x, m_client_name.string(), this, ##__VA_ARGS__)
#define QLOGI(x, ...) HWC_LOGI("(%s:%p) " x, m_client_name.string(), this, ##__VA_ARGS__)
#define QLOGW(x, ...) HWC_LOGW("(%s:%p) " x, m_client_name.string(), this, ##__VA_ARGS__)
#define QLOGE(x, ...) HWC_LOGE("(%s:%p) " x, m_client_name.string(), this, ##__VA_ARGS__)

//#define QUEUE_DEBUG
#ifdef QUEUE_DEBUG
#define DBG_LOGD(x, ...) HWC_LOGD("(%s:%p) " x, m_client_name.string(), this, ##__VA_ARGS__)
#define DBG_LOGW(x, ...) HWC_LOGW("(%s:%p) " x, m_client_name.string(), this, ##__VA_ARGS__)
#else
#define DBG_LOGD(x, ...)
#define DBG_LOGW(x, ...)
#endif

#define HWC_ATRACE_BUFFER_INDEX(string, index)                                \
    if (ATRACE_ENABLED()) {                                                   \
        char ___traceBuf[1024];                                               \
        int ret = snprintf(___traceBuf, 1024, "%s: %d", string, index);       \
        if (ret >= 0 && ret < 1024) {                                         \
            android::ScopedTrace ___bufTracer(ATRACE_TAG, ___traceBuf);       \
        }                                                                     \
    }

// ---------------------------------------------------------------------------

DisplayBufferQueue::DisplayBufferQueue(int type, uint64_t id)
    : m_queue_type(type)
    , m_is_synchronous(true)
    , m_frame_counter(0)
    , m_last_acquire_idx(INVALID_BUFFER_SLOT)
    , m_rel_fence_fd(-1)
    , m_listener(NULL)
    , m_id(id)
{
    m_buffer_count = NUM_BUFFER_SLOTS;

    if (m_queue_type <= QUEUE_TYPE_NONE || m_queue_type >= QUEUE_TYPE_NUM)
    {
        QLOGE("Initialize with invalid Type (%d), m_id(%" PRIu64 ")", m_queue_type, m_id);
        m_queue_type = QUEUE_TYPE_NONE;

        m_client_name = String8::format("noinit");
    }
    else
    {
#ifdef MTK_USER_BUILD
        switch (m_queue_type)
        {
            case QUEUE_TYPE_BLT:
                m_client_name = "q2";
                break;
            case QUEUE_TYPE_OVL:
                m_client_name = "q1";
                break;
            case QUEUE_TYPE_GLAI:
                m_client_name = "q3";
                break;
            case QUEUE_TYPE_AI_BLD_DISP:
                m_client_name = "q4";
                break;
            case QUEUE_TYPE_AI_BLD_MDP:
                m_client_name = "q5";
                break;
            default:
                m_client_name = "q?";
                break;
        }
#else
        switch (m_queue_type)
        {
            case QUEUE_TYPE_BLT:
                m_client_name = "blt";
                break;
            case QUEUE_TYPE_OVL:
                m_client_name = "ovl";
                break;
            case QUEUE_TYPE_GLAI:
                m_client_name = "glai";
                break;
            case QUEUE_TYPE_AI_BLD_DISP:
                m_client_name = "bld_d";
                break;
            case QUEUE_TYPE_AI_BLD_MDP:
                m_client_name = "bld_m";
                break;
            default:
                m_client_name = "???";
                break;
        }
#endif
        QLOGI("Buffer queue is created with size(%d), m_id(%" PRIu64 "), %s",
              m_buffer_count, m_id, m_client_name.c_str());
    }
}

DisplayBufferQueue::~DisplayBufferQueue()
{
    QLOGI("Buffer queue is destroyed, m_id(%" PRIu64 ")", m_id);

    if (m_last_acquired_buf.index != INVALID_BUFFER_SLOT)
    {
        QLOGI("%s(), m_id(%" PRIu64 "), release buf", __FUNCTION__, m_id);
        // release previous buffer
        if (releaseBuffer(m_last_acquired_buf.index, m_rel_fence_fd) != NO_ERROR)
        {
            QLOGW("%s(), m_id(%" PRIu64 "), releaseBuffer fail", __FUNCTION__, m_id);
            ::protectedClose(m_rel_fence_fd);
        }
    }

    for (unsigned int i = 0; i < static_cast<unsigned int>(m_buffer_count); i++)
    {
        BufferSlot* slot = &m_slots[i];

        if (NULL == slot->out_handle) continue;

        if (slot->release_fence != -1) ::protectedClose(slot->release_fence);

        if (slot->out_handle != nullptr)
        {
            QLOGI("Free Slot(%d), handle=%p, %u -> 0",
                i, slot->out_handle, slot->data_size);

            GrallocDevice::getInstance().free(slot->out_handle);
        }

        slot->out_handle = NULL;
        slot->data_size = 0;
    }

    m_listener = NULL;
}

status_t DisplayBufferQueue::setBufferParam(BufferParam& param)
{
    AutoMutex l(m_mutex);

    m_buffer_param = param;

    return NO_ERROR;
}

status_t DisplayBufferQueue::updateBufferSize(unsigned int width, unsigned int height)
{
    AutoMutex l(m_mutex);

    if (m_buffer_param.width != width ||
        m_buffer_param.height != height)
    {
        m_buffer_param.width = width;
        m_buffer_param.height = height;
        m_buffer_param.size = (width * height * getBitsPerPixel(m_buffer_param.format) / 8);
    }

    return NO_ERROR;
}

status_t DisplayBufferQueue::reallocate(unsigned int idx, bool is_secure)
{
    BufferSlot* slot = &m_slots[idx];

    slot->data_format = m_buffer_param.format;
    slot->data_width = m_buffer_param.width;
    slot->data_height = m_buffer_param.height;

    if (slot->out_handle &&
        slot->data_size == m_buffer_param.size &&
        slot->compression == m_buffer_param.compression &&
        slot->secure == is_secure)
    {
        return NO_ERROR;
    }

    QLOGI("Reallocate Slot(%u), pool(%d -> %d) size(%d -> %d) secure(%d -> %d)",
          idx, slot->pool_id, m_buffer_param.pool_id,
          slot->data_size, m_buffer_param.size,
          slot->secure, is_secure);

    // allocate new buffer
    {
        HWC_ATRACE_CALL();

        // release old buffer
        if (slot->out_handle)
        {
            QLOGD("Free Old Slot(%d), handle=%p", idx, slot->out_handle);

            GrallocDevice::getInstance().free(slot->out_handle);
            slot->out_handle = NULL;
        }

        {
            GrallocDevice::AllocParam param;
            param.width  = m_buffer_param.width;
            param.height = m_buffer_param.height;
            param.format = m_buffer_param.format;
            param.usage  = static_cast<uint64_t>(BufferUsage::COMPOSER_OVERLAY);
            // this buffer will be accessed by SW, so add SW flag
            param.usage |= m_buffer_param.sw_usage ? (BufferUsage::CPU_READ_OFTEN | BufferUsage::CPU_WRITE_OFTEN) : 0;
            param.usage |= m_buffer_param.compression ? (BufferUsage::GPU_RENDER_TARGET | BufferUsage::GPU_TEXTURE) : 0;
            param.usage |= is_secure ? static_cast<unsigned int>(BufferUsage::PROTECTED) : 0;
            param.usage |= is_secure ? static_cast<unsigned int>(GM_BUFFER_USAGE_PRIVATE_SECURE_DISPLAY) : 0;
            if (param.format == HAL_PIXEL_FORMAT_YCBCR_P010)
            {
                param.usage |= BufferUsage::VIDEO_DECODER;
            }

            if (NO_ERROR != GrallocDevice::getInstance().alloc(param))
            {
                QLOGE("Failed to allocate memory size(w=%d,h=%d,fmt=%d,usage=%" PRIx64 ")",
                    param.width, param.height, param.format, param.usage);
                slot->out_handle = NULL;
                slot->data_size = 0;
                return -EINVAL;
            }

            slot->out_handle = param.handle;
            slot->pool_id = m_buffer_param.pool_id;
        }

        slot->data_size = m_buffer_param.size;
        slot->protect = m_buffer_param.protect;

        slot->out_ion_fd = -1;
        slot->out_sec_handle = 0;

        PrivateHandle priv_handle;
        status_t err = getBufferDimensionInfo(slot->out_handle, &priv_handle);
        err |= getPrivateHandleBuff(slot->out_handle, &priv_handle);
        err |= getAllocId(slot->out_handle, &priv_handle);
        err |= getIonSfInfo(slot->out_handle, &priv_handle);
        if (NO_ERROR != err)
        {
            GrallocDevice::getInstance().free(slot->out_handle);
            slot->out_handle = NULL;
            slot->data_size = 0;
            return -EINVAL;
        }

        slot->out_ion_fd = priv_handle.ion_fd;
        slot->out_sec_handle = priv_handle.sec_handle;
        slot->secure = is_secure;

        if (priv_handle.format == HAL_PIXEL_FORMAT_YCBCR_P010)
            slot->data_pitch = slot->data_width * 2;
        else
            slot->data_pitch = priv_handle.y_stride;
        slot->handle_stride = priv_handle.y_stride;
        slot->alloc_id = priv_handle.alloc_id;
        slot->buffer_size = priv_handle.size;
        slot->data_v_pitch = priv_handle.vstride;
        slot->data_is_compress = isCompressData(&priv_handle);
        slot->compression = m_buffer_param.compression;
        if (m_buffer_param.compression && !isCompressData(&priv_handle))
        {
            QLOGE("%s(): Failed to allocate a compressed buffer!", __func__);
        }
        QLOGI("Alloc Slot(%d), handle=%p c=%d w=%d h=%d p=%d ys=%d vs=%d f=%d sec=%d sh=%x, usage=0x%x",
            idx, slot->out_handle, slot->compression, slot->data_width, slot->data_height,
            slot->data_pitch, slot->handle_stride, slot->data_v_pitch, slot->data_format,
            slot->secure, slot->out_sec_handle,
            priv_handle.usage);

        // set debug name
        if (isSupportDmaBuf())
        {
            if (ioctl(priv_handle.ion_fd, DMA_BUF_SET_NAME,
                      (std::string("DBQ_") + std::to_string(m_id) + std::string("_") + std::to_string(idx)).c_str()))
            {
                HWC_LOGI("DMA_BUF_SET_NAME fail");
            }
        }

    }

    return NO_ERROR;
}

status_t DisplayBufferQueue::drainQueueLocked()
{
    while (m_is_synchronous && !m_queue.isEmpty())
    {
        m_dequeue_condition.wait(m_mutex);
    }

    return NO_ERROR;
}

status_t DisplayBufferQueue::dequeueBuffer(
    DisplayBuffer* buffer, bool async, bool is_secure)
{
    HWC_ATRACE_CALL();

    AutoMutex l(m_mutex);

    unsigned int found_idx;

    bool tryAgain = true;
    while (tryAgain)
    {
        bool found = false;
        for (unsigned int i = 0; i < static_cast<unsigned int>(m_buffer_count); i++)
        {
            const int state = m_slots[i].state;
            if (state == BufferSlot::FREE)
            {
                // return the oldest of the free buffers to avoid
                // stalling the producer if possible.
                if (!found)
                {
                    found = true;
                    found_idx = i;
                }
                else if (m_slots[i].frame_num < m_slots[found_idx].frame_num)
                {
                    found_idx = i;
                }
            }
        }

        // if no buffer is found, wait for a buffer to be released
        tryAgain = !found;
        if (tryAgain)
        {
            if (CC_LIKELY(m_buffer_param.dequeue_block))
            {
                QLOGW("dequeueBuffer: cannot find available buffer, wait...");
                status_t res = m_dequeue_condition.waitRelative(m_mutex, ms2ns(16));
                QLOGW("dequeueBuffer: wake up to find available buffer (%s)",
                        (res == TIMED_OUT) ? "TIME OUT" : "WAKE");
            }
            else
            {
                QLOGW("dequeueBuffer: cannot find available buffer, exit...");
                return -EBUSY;
            }
        }
    }

    const unsigned int idx = found_idx;
    if (idx >= static_cast<unsigned int>(m_buffer_count))
    {
        DBG_LOGW("%s(), idx %d invalid", __FUNCTION__, idx);
        return -EBUSY;
    }

    HWC_ATRACE_BUFFER_INDEX("dequeue", idx);

    reallocate(idx, is_secure);

    // buffer is now in DEQUEUED state
    m_slots[idx].state = BufferSlot::DEQUEUED;

    buffer->out_handle           = m_slots[idx].out_handle;
    buffer->out_ion_fd           = m_slots[idx].out_ion_fd;
    buffer->out_sec_handle       = m_slots[idx].out_sec_handle;
    buffer->data_size            = m_slots[idx].data_size;
    buffer->data_pitch           = m_slots[idx].data_pitch;
    buffer->data_v_pitch         = m_slots[idx].data_v_pitch;
    buffer->data_format          = m_slots[idx].data_format;
    buffer->data_width           = m_slots[idx].data_width;
    buffer->data_height          = m_slots[idx].data_height;
    buffer->data_is_compress     = m_slots[idx].data_is_compress;
    buffer->handle_stride        = m_slots[idx].handle_stride;
    buffer->data_info.src_crop.makeInvalid();
    buffer->data_info.dst_crop.makeInvalid();
    buffer->data_info.is_sharpen = false;
    buffer->timestamp            = m_slots[idx].timestamp;
    buffer->frame_num            = m_slots[idx].frame_num;
    buffer->release_fence        = m_slots[idx].release_fence;
    buffer->index                = static_cast<int>(idx);
    buffer->ext_sel_layer        = -1;
    m_slots[idx].release_fence   = -1;
    buffer->alloc_id             = m_slots[idx].alloc_id;
    buffer->buffer_size          = m_slots[idx].buffer_size;
    buffer->compression          = m_slots[idx].compression;
    buffer->secure               = m_slots[idx].secure;

    DBG_LOGD("dequeueBuffer (idx=%d, fence=%d) (handle=%p, ion=%d) p=%d v_p=%d c=%d",
        idx, buffer->release_fence, buffer->out_handle, buffer->out_ion_fd, buffer->data_pitch, buffer->data_v_pitch, buffer->compression);

    // wait release fence
    if (!async)
    {
        sp<SyncFence> fence(new SyncFence(static_cast<uint64_t>(m_buffer_param.disp_id)));
        fence->wait(buffer->release_fence, 1000, DEBUG_LOG_TAG);
        buffer->release_fence = -1;
    }

    return NO_ERROR;
}

status_t DisplayBufferQueue::queueBuffer(DisplayBuffer* buffer)
{
    HWC_ATRACE_CALL();

    sp<ConsumerListener> listener;

    {
        AutoMutex l(m_mutex);

        HWC_ATRACE_BUFFER_INDEX("queue", buffer->index);

        if (buffer->index < 0 || buffer->index >= m_buffer_count)
        {
            QLOGE("queueBuffer: slot index out of range [0, %d]: %d",
                     m_buffer_count, buffer->index);
            return -EINVAL;
        }

        const unsigned int idx = static_cast<unsigned int>(buffer->index);

        if (m_slots[idx].state != BufferSlot::DEQUEUED)
        {
            QLOGE("queueBuffer: slot %u is not owned by the client (state=%d)",
                  idx, m_slots[idx].state);
            return -EINVAL;
        }

        // if queue not empty, means consumer is slower than producer
        // * in sync mode, may cause lag (but size 1 should be OK for triple buffer)
        // * in async mode, frame drop
        //bool dump_fifo = false;
        bool dump_fifo = false;
        if (true == m_is_synchronous)
        {
            // fifo depth 1 is ok for multiple buffer, but 2 would cause lag
            if (1 < m_queue.size())
            {
                QLOGW("queued:%zu (lag), type:%d", m_queue.size(), m_queue_type);
                dump_fifo = true;
            }
        }
        else
        {
            // frame drop is fifo is not empty
            if (0 < m_queue.size())
            {
                QLOGW("queued:%zu (drop frame), type:%d", m_queue.size(), m_queue_type);
                dump_fifo = true;
            }
        }

        // dump current fifo data, and the new coming one
        if (true == dump_fifo)
        {
            // print from the oldest to the latest queued buffers
            const BufferSlot *slot = NULL;

            Fifo::const_iterator i(m_queue.begin());
            while (i != nullptr && i != m_queue.end())
            {
                slot = &(m_slots[*i]);
                QLOGD("    [idx:%d] handle:%p", *i, slot->out_handle);
                ++i;
            }

            QLOGD("NEW [idx:%u] handle:%p", idx, m_slots[idx].out_handle);
        }

        if (m_is_synchronous)
        {
            // In synchronous mode we queue all buffers in a FIFO
            m_queue.push_back(static_cast<int>(idx));

            listener = m_listener;
        }
        else
        {
            // In asynchronous mode we only keep the most recent buffer.
            if (m_queue.empty())
            {
                m_queue.push_back(static_cast<int>(idx));
            }
            else
            {
                Fifo::iterator front(m_queue.begin());
                if (front && *front >= 0 && *front < m_buffer_count)
                {
                    // buffer currently queued is freed
                    m_slots[*front].state = BufferSlot::FREE;
                    // and we record the new buffer index in the queued list
                    *front = static_cast<int>(idx);
                }
                else
                {
                    QLOGE("%s(): front invalid", __FUNCTION__);
                    m_queue.clear();
                    m_queue.push_back(static_cast<int>(idx));
                }
            }

            listener = m_listener;
        }

        m_slots[idx].src_handle           = buffer->src_handle;
        m_slots[idx].data_info.src_crop   = buffer->data_info.src_crop;
        m_slots[idx].data_info.dst_crop   = buffer->data_info.dst_crop;
        m_slots[idx].data_info.is_sharpen = buffer->data_info.is_sharpen;
        m_slots[idx].data_info.ethdr_meta = buffer->data_info.ethdr_meta;
        m_slots[idx].dataspace            = buffer->dataspace;
        m_slots[idx].timestamp            = buffer->timestamp;
        m_slots[idx].state                = BufferSlot::QUEUED;
        m_slots[idx].frame_num            = (++m_frame_counter);
        m_slots[idx].alpha_enable         = buffer->alpha_enable;
        m_slots[idx].alpha                = buffer->alpha;
        m_slots[idx].blending             = buffer->blending;
        m_slots[idx].sequence             = buffer->sequence;
        m_slots[idx].acquire_fence        = buffer->acquire_fence;
        m_slots[idx].ext_sel_layer        = buffer->ext_sel_layer;
        m_slots[idx].compression          = buffer->compression;
        m_slots[idx].hwc_layer_id         = buffer->hwc_layer_id;
        DBG_LOGD("(%d) queueBuffer (idx=%d, fence=%d) c=%d p=%d v_p=%d",
            m_buffer_param.disp_id, idx, m_slots[idx].acquire_fence, m_slots[idx].compression,
            m_slots[idx].data_pitch, m_slots[idx].data_v_pitch);

        m_dequeue_condition.broadcast();

        if (DisplayManager::m_profile_level & PROFILE_BLT)
        {
            HWC_ATRACE_INT(m_client_name.string(), static_cast<int32_t>(m_queue.size()));
        }
    }

    if (listener != NULL) listener->onBufferQueued();

    return NO_ERROR;
}

status_t DisplayBufferQueue::cancelBuffer(int index)
{
    HWC_ATRACE_CALL();
    HWC_ATRACE_BUFFER_INDEX("cancel", index);

    AutoMutex l(m_mutex);

    if (index < 0 || index >= m_buffer_count)
    {
        QLOGE("cancelBuffer: slot index out of range [0, %d]: %d",
                m_buffer_count, index);
        return -EINVAL;
    }

    const unsigned int idx = static_cast<unsigned int>(index);
    if (m_slots[idx].state != BufferSlot::DEQUEUED)
    {
        QLOGE("cancelBuffer: slot %u is not owned by the client (state=%d)",
                idx, m_slots[idx].state);
        return -EINVAL;
    }

    QLOGD("cancelBuffer (%u)", idx);

    m_slots[idx].state = BufferSlot::FREE;
    m_slots[idx].frame_num = 0;
    m_slots[idx].acquire_fence = -1;
    m_slots[idx].release_fence = -1;

    m_dequeue_condition.broadcast();
    return NO_ERROR;
}

status_t DisplayBufferQueue::setSynchronousMode(bool enabled)
{
    AutoMutex l(m_mutex);

    if (m_is_synchronous != enabled)
    {
        // drain the queue when changing to asynchronous mode
        if (!enabled) drainQueueLocked();

        m_is_synchronous = enabled;
        m_dequeue_condition.broadcast();
    }

    return NO_ERROR;
}

void DisplayBufferQueue::dumpLocked(int /*idx*/)
{
}

void DisplayBufferQueue::dump(QUEUE_DUMP_CONDITION /*cond*/)
{
}

status_t DisplayBufferQueue::acquireBuffer(
    DisplayBuffer* buffer, bool async)
{
    HWC_ATRACE_CALL();

    AutoMutex l(m_mutex);

    // check if queue is empty
    // In asynchronous mode the list is guaranteed to be one buffer deep.
    // In synchronous mode we use the oldest buffer.
    if (!m_queue.empty())
    {
        Fifo::iterator front(m_queue.begin());
        if (!front)
        {
            DBG_LOGW("%s(), front == nulltpr", __FUNCTION__);
            buffer->index = INVALID_BUFFER_SLOT;
            return NO_BUFFER_AVAILABLE;
        }
        if (*front < 0 || *front >= m_buffer_count)
        {
            DBG_LOGW("%s(), front %d invalid", __FUNCTION__, *front);
            buffer->index = INVALID_BUFFER_SLOT;
            return NO_BUFFER_AVAILABLE;
        }

        unsigned int idx = static_cast<unsigned int>(*front);

        HWC_ATRACE_BUFFER_INDEX("acquire", idx);

        // buffer is now in ACQUIRED state
        m_slots[idx].state = BufferSlot::ACQUIRED;

        buffer->out_handle           = m_slots[idx].out_handle;
        buffer->out_ion_fd           = m_slots[idx].out_ion_fd;
        buffer->out_sec_handle       = m_slots[idx].out_sec_handle;
        buffer->data_size            = m_slots[idx].data_size;
        buffer->data_pitch           = m_slots[idx].data_pitch;
        buffer->data_v_pitch         = m_slots[idx].data_v_pitch;
        buffer->data_format          = m_slots[idx].data_format;
        buffer->data_width           = m_slots[idx].data_width;
        buffer->data_height          = m_slots[idx].data_height;
        buffer->data_is_compress     = m_slots[idx].data_is_compress;
        buffer->handle_stride        = m_slots[idx].handle_stride;
        buffer->dataspace            = m_slots[idx].dataspace;
        buffer->data_info.src_crop   = m_slots[idx].data_info.src_crop;
        buffer->data_info.dst_crop   = m_slots[idx].data_info.dst_crop;
        buffer->data_info.is_sharpen = m_slots[idx].data_info.is_sharpen;
        buffer->data_info.ethdr_meta = m_slots[idx].data_info.ethdr_meta;
        buffer->timestamp            = m_slots[idx].timestamp;
        buffer->frame_num            = m_slots[idx].frame_num;
        buffer->protect              = m_slots[idx].protect;
        buffer->secure               = m_slots[idx].secure;
        buffer->alpha_enable         = m_slots[idx].alpha_enable;
        buffer->alpha                = m_slots[idx].alpha;
        buffer->blending             = m_slots[idx].blending;
        buffer->sequence             = m_slots[idx].sequence;
        buffer->acquire_fence        = m_slots[idx].acquire_fence;
        buffer->index                = static_cast<int>(idx);
        buffer->ext_sel_layer        = m_slots[idx].ext_sel_layer;
        m_slots[idx].acquire_fence = -1;
        buffer->alloc_id             = m_slots[idx].alloc_id;
        buffer->hwc_layer_id         = m_slots[idx].hwc_layer_id;
        buffer->buffer_size          = m_slots[idx].buffer_size;
        buffer->compression          = m_slots[idx].compression;

        DBG_LOGD("acquireBuffer (idx=%d, fence=%d) c=%d p=%d v_p=%d",
            idx, buffer->acquire_fence, buffer->compression, buffer->data_pitch, buffer->data_v_pitch);

        // remember last acquire buffer's index
        m_last_acquire_idx = static_cast<int>(idx);

        m_queue.erase(front);
        m_dequeue_condition.broadcast();

        if (DisplayManager::m_profile_level & PROFILE_TRIG)
        {
            HWC_ATRACE_INT(m_client_name.string(), static_cast<int32_t>(m_queue.size()));
        }
    }
    else
    {
        DBG_LOGW("acquireBuffer fail");

        buffer->index = INVALID_BUFFER_SLOT;
        return NO_BUFFER_AVAILABLE;
    }

    // wait acquire fence
    if (!async)
    {
        sp<SyncFence> fence(new SyncFence(static_cast<uint64_t>(m_buffer_param.disp_id)));
        fence->wait(buffer->acquire_fence, 1000, DEBUG_LOG_TAG);
        buffer->acquire_fence = -1;
    }

    return NO_ERROR;
}

status_t DisplayBufferQueue::releaseBuffer(int index, int fence)
{
    HWC_ATRACE_CALL();
    HWC_ATRACE_BUFFER_INDEX("release", index);

    AutoMutex l(m_mutex);

    if (index == INVALID_BUFFER_SLOT) return -EINVAL;

    if (index < 0 || index >= m_buffer_count)
    {
        QLOGE("releaseBuffer: slot index out of range [0, %d]: %d, fence:%d",
                m_buffer_count, index, fence);
        return -EINVAL;
    }

    if (m_slots[index].state != BufferSlot::ACQUIRED)
    {
        QLOGE("attempted to release buffer(%d) fence:%d with state(%d)",
            index, fence, m_slots[index].state);
        return -EINVAL;
    }

    m_slots[index].state = BufferSlot::FREE;
    if (m_slots[index].release_fence != -1)
    {
        QLOGW("release fence existed! buffer(%d) with state(%d) fence:%d",
            index, m_slots[index].state, fence);
        ::protectedClose(m_slots[index].release_fence);
    }
    m_slots[index].release_fence = fence;

    DBG_LOGD("releaseBuffer (idx=%d, fence=%d)", index, fence);

    m_dequeue_condition.broadcast();
    return NO_ERROR;
}

void DisplayBufferQueue::setConsumerListener(
    const sp<ConsumerListener>& listener)
{
    QLOGI("setConsumerListener");
    AutoMutex l(m_mutex);
    m_listener = listener;
}
