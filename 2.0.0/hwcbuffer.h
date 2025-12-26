#ifndef HWC_HWCBUFFER_H
#define HWC_HWCBUFFER_H

#include <utils/RefBase.h>
#include "utils/tools.h"

class HWCBuffer : public android::LightRefBase<HWCBuffer>
{
public:
    HWCBuffer(const uint64_t& disp_id, const int64_t& layer_id, const bool& is_ct):
        m_hnd(nullptr),
        m_original_hnd(nullptr),
        m_prev_original_hnd(nullptr),
        m_original_hnd_alloc_id(0),
        m_prev_original_hnd_alloc_id(0),
        m_original_hnd_frame_number(0),
        m_prev_original_hnd_frame_number(0),
        m_release_fence_fd(-1),
        m_prev_release_fence_fd(-1),
        m_acquire_fence_fd(-1),
        m_is_ct(is_ct),
        m_disp_id(disp_id),
        m_layer_id(layer_id),
        m_buffer_changed(false)
    { }

    ~HWCBuffer();
    void setHandle(const buffer_handle_t& hnd);
    buffer_handle_t getPrevOriginalHandle() const { return m_prev_original_hnd; }
    void recordPrevOriginalInfo()
    {
        m_prev_original_hnd = m_original_hnd;
        m_prev_original_hnd_alloc_id = m_original_hnd_alloc_id;
        m_prev_original_hnd_frame_number = m_original_hnd_frame_number;
    }

    buffer_handle_t getHandle() const { return m_hnd; }

    void setReleaseFenceFd(const int32_t& fence_fd, const bool& is_disp_connected);
    int32_t getReleaseFenceFd() { return m_release_fence_fd; }

    void setPrevReleaseFenceFd(const int32_t& fence_fd, const bool& is_disp_connected);
    int32_t getPrevReleaseFenceFd() { return m_prev_release_fence_fd; }

    void setAcquireFenceFd(const int32_t& fence_fd);
    int32_t getAcquireFenceFd() { return m_acquire_fence_fd; }

    const PrivateHandle& getPrivateHandle() { return m_priv_hnd; }
    PrivateHandle& getEditablePrivateHandle() { return m_priv_hnd; }

    int32_t afterPresent(const bool& should_clear_state = true, const bool& is_ct = false);

    void setBufferChanged(const bool& buf_changed) { m_buffer_changed = buf_changed; }
    bool isBufferChanged() const { return m_buffer_changed; }

    void setupPrivateHandle(std::string* name);
private:
    buffer_handle_t m_hnd;
    buffer_handle_t m_original_hnd;
    buffer_handle_t m_prev_original_hnd;
    uint64_t m_original_hnd_alloc_id;
    uint64_t m_prev_original_hnd_alloc_id;
    uint64_t m_original_hnd_frame_number;
    uint64_t m_prev_original_hnd_frame_number;
    int32_t m_release_fence_fd;
    int32_t m_prev_release_fence_fd;
    int32_t m_acquire_fence_fd;
    bool m_is_ct;
    PrivateHandle m_priv_hnd;
    uint64_t m_disp_id;
    int64_t m_layer_id;
    bool m_buffer_changed;
    std::unordered_map<uint64_t, PrivateHandle> m_hnd_private_handle_cache;
    std::string name;
    // For setHandle to avoid multi-thread race condition
    std::mutex m_set_hnd_lock;
};

#endif
