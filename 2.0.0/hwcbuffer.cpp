#define DEBUG_LOG_TAG "HWCBuffer"
#include "hwcbuffer.h"
#include "platform_wrap.h"

#define FBT_PRIVATE_HANDLE_MAP_SIZE 4

static void cleanPrivateHandleCache(std::unordered_map<uint64_t, PrivateHandle> &map)
{
    for (auto& it : map)
    {
        if (it.second.ion_fd < 0)
        {
            continue;
        }

        protectedClose(it.second.ion_fd);
    }
}

HWCBuffer::~HWCBuffer()
{
    if (getAcquireFenceFd() != -1)
        protectedClose(getAcquireFenceFd());

    if (getReleaseFenceFd() != -1)
        protectedClose(getReleaseFenceFd());

    if (getPrevReleaseFenceFd() != -1)
        protectedClose(getPrevReleaseFenceFd());

    if (m_hnd != nullptr)
        freeDuppedBufferHandle(m_hnd);

    cleanPrivateHandleCache(m_hnd_private_handle_cache);
}

void HWCBuffer::setReleaseFenceFd(const int32_t& fence_fd, const bool& is_disp_connected)
{
    if (fence_fd >= 0 && m_release_fence_fd != -1)
    {
        if (is_disp_connected)
        {
            HWC_LOGE("(%" PRIu64 ") fdleak detect: %s layer_id:%" PRId64 " release_fence_fd:%d hnd:%p",
                m_disp_id, __func__, m_layer_id, m_release_fence_fd, m_hnd);
            AbortMessager::getInstance().abort();
        }
        else
        {
            HWC_LOGW("(%" PRIu64 ") fdleak detect: %s layer_id:%" PRId64 " release_fence_fd:%d hnd:%p",
                m_disp_id, __func__, m_layer_id, m_release_fence_fd, m_hnd);
            ::protectedClose(m_release_fence_fd);
            m_release_fence_fd = -1;
        }
    }
    m_release_fence_fd = fence_fd;
    HWC_LOGV("(%" PRIu64 ") %s layer id:%" PRId64 " release_fence_fd:%d hnd:%p",
         m_disp_id, __func__, m_layer_id, m_release_fence_fd, m_hnd);
}

void HWCBuffer::setPrevReleaseFenceFd(const int32_t& fence_fd, const bool& is_disp_connected)
{
    if (fence_fd >= 0 && m_prev_release_fence_fd != -1)
    {
        if (is_disp_connected)
        {
            HWC_LOGE("(%" PRIu64 ") fdleak detect: %s layer id:%" PRId64 " prev_release_fence_fd:%d hnd:%p",
                m_disp_id, __func__, m_layer_id, m_prev_release_fence_fd, m_hnd);
            AbortMessager::getInstance().abort();
        }
        else
        {
            HWC_LOGE("(%" PRIu64 ") fdleak detect: %s layer id:%" PRId64 " prev_release_fence_fd:%d hnd:%p",
                m_disp_id, __func__, m_layer_id, m_prev_release_fence_fd, m_hnd);
            ::protectedClose(m_prev_release_fence_fd);
            m_prev_release_fence_fd = -1;
        }
    }
    m_prev_release_fence_fd = fence_fd;
    HWC_LOGV("(%" PRIu64 ") %s layer id:%" PRId64 " prev_release_fence_fd:%d hnd:%p",
         m_disp_id, __func__, m_layer_id, m_prev_release_fence_fd, m_hnd);
}

void HWCBuffer::setAcquireFenceFd(const int32_t& fence_fd)
{
    if (m_acquire_fence_fd != -1)
    {
        ::protectedClose(m_acquire_fence_fd);
    }

    m_acquire_fence_fd = fence_fd;
    HWC_LOGV("(%" PRIu64 ") %s layer id:%" PRId64 " acquire_fence_fd:%d hnd:%p",
        m_disp_id, __func__, m_layer_id, m_acquire_fence_fd, m_hnd);
}

int32_t HWCBuffer::afterPresent(const bool& should_clear_state, const bool& /*is_ct*/)
{
    recordPrevOriginalInfo();
    if (should_clear_state)
        setBufferChanged(false);
    return 0;
}

void HWCBuffer::setupPrivateHandle(std::string* name)
{
    if (m_hnd != nullptr)
    {
        int ret = 0;
        if (m_is_ct)
        {
            if (Platform::getInstance().m_config.cache_CT_private_hnd)
            {
                auto search = m_hnd_private_handle_cache.find(m_priv_hnd.alloc_id);
                if (search != m_hnd_private_handle_cache.end())
                {
                    memcpy(&m_priv_hnd, &(search->second), sizeof(PrivateHandle));
                    ret = getPrivateHandleInfoModifyPerFrame(m_hnd, &m_priv_hnd);
                    if (Platform::getInstance().m_config.is_smart_composition_support)
                    {
                        ret |= getPrivateHandleFBTInfo(m_hnd, &m_priv_hnd);
                    }
                }
                else
                {
                    ret = getPrivateHandleFBT(m_hnd, &m_priv_hnd, name);
                    PrivateHandle tmp;
                    memcpy(&tmp, &m_priv_hnd, sizeof(PrivateHandle));
                    if (m_hnd_private_handle_cache.size() >= FBT_PRIVATE_HANDLE_MAP_SIZE)
                    {
                        cleanPrivateHandleCache(m_hnd_private_handle_cache);
                        m_hnd_private_handle_cache.clear();
                    }

                    // dup fd, or this fd will be closed after buffer_handle_t is freed
                    tmp.ion_fd = ::dup(tmp.ion_fd);
                    if (tmp.ion_fd < 0)
                    {
                        HWC_LOGE("%s(), failed to dup fd", __FUNCTION__);
                    }

                    m_hnd_private_handle_cache.insert({m_priv_hnd.alloc_id, tmp});
                }
            }
            else
            {
                ret = getPrivateHandleFBT(m_hnd, &m_priv_hnd, name);
            }
        }
        else
        {
            ret = ::getPrivateHandle(m_hnd, &m_priv_hnd, name);
        }

        if (ret)
        {
            HWC_LOGE("%s(), getPrivateHandle fail, ret %d, m_is_ct %d", __FUNCTION__, ret, m_is_ct);
        }
    }
    else
    {
        // it's a dim layer?
        m_priv_hnd.format = HAL_PIXEL_FORMAT_RGBA_8888;
    }

    // In order to decrease Gralloc4 overhead, we update only when 1st compresssed buffer setup.
    if (HWCMediator::getInstance().getCompressionType() == 0 &&
        isCompressData(&m_priv_hnd) == true)
    {
        HWCMediator::getInstance().updateCompressionType(m_hnd);
    }
}

void HWCBuffer::setHandle(const buffer_handle_t& hnd)
{
    std::lock_guard<std::mutex> lock(m_set_hnd_lock);

    int32_t err = 0;
    if (hnd != nullptr)
    {
        err = gralloc_extra_query(hnd, GRALLOC_EXTRA_GET_ID, &m_priv_hnd.alloc_id);
        if (Platform::getInstance().m_config.is_bw_monitor_support ||
            Platform::getInstance().m_config.is_smart_composition_support)
        {
            ge_frame_number_info_t frameInfo;
            memset(&frameInfo, 0, sizeof(frameInfo));
            err |= gralloc_extra_query(hnd, GRALLOC_EXTRA_GET_FRAME_NUMBER_INFO, &frameInfo);
            m_priv_hnd.frame_number = frameInfo.frame_number;
        }
    }

    if (err)
        HWC_LOGE("%s gralloc_extra_query fail, err(%d), (handle=%p)", __func__, err, hnd);

    if (getPrevOriginalHandle() != hnd ||
        m_prev_original_hnd_alloc_id != m_priv_hnd.alloc_id ||
        m_prev_original_hnd_frame_number != m_priv_hnd.frame_number || err)
    {
        setBufferChanged(true);// add check frame_number
        HWC_LOGV("hnd:%p/%p, alloc_id:%" PRIu64 "/%" PRIu64 ", frame_number:%" PRIu64 "/%" PRIu64 ,
            getPrevOriginalHandle(), hnd, m_prev_original_hnd_alloc_id, m_priv_hnd.alloc_id,
            m_prev_original_hnd_frame_number, m_priv_hnd.frame_number);
    }

    m_original_hnd = hnd;
    m_original_hnd_alloc_id = m_priv_hnd.alloc_id;
    m_original_hnd_frame_number = m_priv_hnd.frame_number;
    if (m_hnd != nullptr)
    {
        freeDuppedBufferHandle(m_hnd);
        m_hnd = nullptr;
    }
    if (hnd != nullptr)
    {
        dupBufferHandle(hnd, &m_hnd);
    }
    else
    {
        m_hnd = nullptr;
    }
}
