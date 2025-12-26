#define DEBUG_LOG_TAG "ColorHistogram"
#include "color_histogram.h"

#include <errno.h>
#include <utils/Errors.h>
#include <cutils/properties.h>
#include <cutils/bitops.h>
#include <algorithm>

#include "utils/debug.h"
#include "utils/tools.h"
#include "sync.h"
#include "platform_wrap.h"

FenceState::FenceState(unsigned int index, int fence_fd, hwc2_config_t active_config)
    : m_index(index)
    , m_fence_fd(fence_fd)
    , m_invalid(false)
    , m_is_signal(false)
    , m_signal_time(0)
    , m_active_config(active_config)
{
}

FenceState::~FenceState()
{
    if (m_fence_fd > 0)
    {
        closeFenceFd(&m_fence_fd);
    }
}

unsigned int FenceState::getFenceIndex()
{
    return m_index;
}

bool FenceState::needCheck()
{
    return !m_invalid && !m_is_signal;
}

bool FenceState::isSignal()
{
    return m_is_signal;
}

int FenceState::waitSignal()
{
    int res =  SyncFence::waitWithoutCloseFd(m_fence_fd, 1000, "FenceState");
    if (res == 0)
    {
        m_is_signal = true;
    }
    else if (res < 0)
    {
        m_invalid = true;
    }
    m_signal_time = SyncFence::getSignalTime(m_fence_fd);
    return res;
}

uint64_t FenceState::getSingalTime()
{
    if (m_signal_time == 0)
    {
        m_signal_time = SyncFence::getSignalTime(m_fence_fd);
        if (m_signal_time == static_cast<uint64_t>(SIGNAL_TIME_INVALID))
        {
            m_invalid = true;
        }
        else if (m_signal_time != static_cast<uint64_t>(SIGNAL_TIME_PENDING))
        {
            m_is_signal = true;
        }
    }
    return m_signal_time;
}

hwc2_config_t FenceState::getActiveConfig()
{
    return m_active_config;
}

//=============================================================================

FrameHistogram::FrameHistogram(uint8_t mask, uint8_t channel_number, uint32_t bin_number)
    : m_pf_index(0)
    , m_start(0)
    , m_end(0)
    , m_refresh(0)
    , m_present_count(0)
    , m_mask(mask)
    , m_channel_number(0)
    , m_bin_number(0)
    , m_total_bin_number(0)
    , m_data(nullptr)
{
    uint32_t total_bin_number = channel_number * bin_number;
    if (total_bin_number)
    {
        m_data = new uint32_t[total_bin_number];
    }
    if (m_data)
    {
        memset(m_data, 0, sizeof(*m_data) * total_bin_number);
        m_channel_number = channel_number;
        m_bin_number = bin_number;
        m_total_bin_number = total_bin_number;
        uint8_t offset_count = 0;
        for (uint8_t i = 0; i < NUM_FORMAT_COMPONENTS; i++)
        {
            uint8_t mask = m_mask >> i;
            if (mask & 0x01)
            {
                m_channel_data[i] = m_data + bin_number * offset_count;
                offset_count++;
            }
            else
            {
                m_channel_data[i] = nullptr;
            }
        }
    }
    else
    {
        for (uint8_t i = 0; i < NUM_FORMAT_COMPONENTS; i++)
        {
            m_channel_data[i] = nullptr;
        }
    }
}

FrameHistogram::~FrameHistogram()
{
    if (m_data)
    {
        delete[] m_data;
    }
}

//=============================================================================

HistogramCollector::HistogramCollector()
    : m_head(0)
    , m_tail(0)
    , m_temp_pos(0)
    , m_frame_count(0)
    , m_total_present_count(0)
    , m_histogram_overflow(false)
    , m_data(nullptr)
    , m_mask(0)
    , m_max_frames(0)
    , m_channel_number(0)
    , m_bin_number(0)
    , m_total_bin_number(0)
{
    for (uint8_t i = 0; i < NUM_FORMAT_COMPONENTS; i++)
    {
        m_channel_data[i] = nullptr;
    }
}

HistogramCollector::~HistogramCollector()
{
    if (m_data != nullptr)
    {
        delete[] m_data;
    }
}

int32_t HistogramCollector::resize(uint64_t max_frames, uint8_t mask, uint8_t channel_number,
        uint32_t bin_number)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (max_frames >= UINT32_MAX)
    {
        max_frames = UINT32_MAX - 1;
        HWC_LOGW("%s: the requested size is too large, change the size to %" PRIu64,
                __func__, max_frames);
    }

    int32_t res = 0;
    uint32_t total_bin_number = channel_number * bin_number;
    if (m_max_frames != max_frames || m_mask != mask || m_channel_number != channel_number ||
            m_bin_number != bin_number)
    {
        uint32_t total_bin_number = channel_number * bin_number;
        if (m_data != nullptr)
        {
            delete[] m_data;
            m_data = nullptr;
        }
        if (total_bin_number)
        {
            m_data = new uint64_t[total_bin_number];
        }
        if (m_data == nullptr)
        {
            res = -ENOMEM;
            m_channel_number = 0;
            m_bin_number = 0;
            m_total_bin_number = 0;
        }
        else
        {
            m_max_frames = static_cast<size_t>(max_frames);
            m_mask = mask;
            memset(m_data, 0, sizeof(*m_data) * total_bin_number);
            m_channel_number = channel_number;
            m_bin_number = bin_number;
            m_total_bin_number = total_bin_number;
            uint8_t offset_count = 0;
            for (uint8_t i = 0; i < NUM_FORMAT_COMPONENTS; i++)
            {
                uint8_t mask = m_mask >> i;
                if (mask & 0x01)
                {
                    m_channel_data[i] = m_data + bin_number * offset_count;
                    offset_count++;
                }
                else
                {
                    m_channel_data[i] = nullptr;
                }
            }
        }
        m_frame_list.resize(m_max_frames + 1);
    }
    else
    {
        memset(m_data, 0, sizeof(*m_data) * total_bin_number);
    }
    m_head = 0;
    m_tail = 0;
    m_temp_pos = 0;
    m_frame_count = 0;
    m_total_present_count = 0;
    m_histogram_overflow = false;

    return res;
}

void HistogramCollector::push(std::shared_ptr<FrameHistogram> frame)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::shared_ptr<FrameHistogram> remove_frame = nullptr;
    if (m_frame_list.size() >= m_max_frames)
    {
        remove_frame = m_frame_list.front();
        m_frame_list.pop_front();
    }

    if (m_frame_list.size() > 0)
    {
        std::shared_ptr<FrameHistogram> last_frame = m_frame_list.back();
        if (last_frame != nullptr)
        {
            last_frame->m_end = frame->m_start;
        }
    }
    m_frame_list.push_back(frame);

    if (remove_frame != nullptr)
    {
        decreaseBinDataLocked(remove_frame);
    }
    increaseBinDataLocked(frame);
}

void HistogramCollector::increaseBinDataLocked(std::shared_ptr<FrameHistogram> frame)
{
    if (frame->m_total_bin_number != m_total_bin_number)
    {
        HWC_LOGW("%s: the bin number is different: %" PRIu64 ",%" PRIu64,
                __func__, frame->m_total_bin_number, m_total_bin_number);
        return;
    }
    if (!m_histogram_overflow)
    {
        for (uint32_t i = 0; i < m_total_bin_number; i++)
        {
            uint64_t increment = frame->m_data[i] * frame->m_present_count;
            uint64_t amount = m_data[i] + increment;
            if (CC_UNLIKELY(amount < m_data[i] || amount < increment))
            {
                m_data[i] = std::numeric_limits<uint64_t>::max();
                m_histogram_overflow = true;
            }
            else
            {
                m_data[i] = amount;
            }
        }
        m_total_present_count += frame->m_present_count;
    }
}

void HistogramCollector::decreaseBinDataLocked(std::shared_ptr<FrameHistogram> frame)
{
    if (frame->m_total_bin_number != m_total_bin_number)
    {
        HWC_LOGW("%s: the bin number is different: %" PRIu64 ",%" PRIu64,
                __func__, frame->m_total_bin_number, m_total_bin_number);
        return;
    }
    if (!m_histogram_overflow)
    {
        for (uint32_t i = 0; i < m_total_bin_number; i++)
        {
            m_data[i] -= frame->m_data[i] * frame->m_present_count;
        }
        m_total_present_count -= frame->m_present_count;
    }
}

void HistogramCollector::dump(String8* dump_str, std::string prefix)
{
    bool enable_debug = (Platform::getInstance().m_config.plat_switch &
            HWC_PLAT_SWITCH_DEBUG_HISTOGRAM) != 0;
    std::lock_guard<std::mutex> lock(m_mutex);
    dump_str->appendFormat("%sm_max_frames: %zu\n", prefix.c_str(), m_max_frames);
    dump_str->appendFormat("%sm_channel_number: %u\n", prefix.c_str(), m_channel_number);
    dump_str->appendFormat("%sm_bin_number: %u\n", prefix.c_str(), m_bin_number);
    dump_str->appendFormat("%sm_total_bin_number: %" PRIu64 "\n", prefix.c_str(), m_total_bin_number);
    dump_str->appendFormat("%ssize: %zu\n", prefix.c_str(), m_frame_list.size());

    if (enable_debug)
    {
        for (size_t i = 0; i < NUM_FORMAT_COMPONENTS; i++)
        {
            uint8_t mask = m_mask >> i;
            if (mask & 0x01)
            {
                dump_str->appendFormat("%schannel[%zu]\n", prefix.c_str(), i);
                for (size_t j = 0; j < m_bin_number; j++)
                {
                    dump_str->appendFormat("%s\tdata[%zu]:%" PRIu64 "\n", prefix.c_str(), j,
                            m_channel_data[i][j]);
                }
            }
        }

        dump_str->appendFormat("%sframe_count:%zu head:%zu  tail:%zu  temp:%zu  tpc=%" PRIu64 "\n",
                prefix.c_str(), m_frame_count, m_head, m_tail, m_temp_pos, m_total_present_count);
        for (size_t i = 0; i < m_frame_list.size(); i++)
        {
            if (m_frame_list[i] != nullptr)
            {
                dump_str->appendFormat("%s[%zu] idx=%u s:%" PRIu64" e:%" PRIu64 " ac=%" PRIu64
                        " pc=%" PRIu64 " data=",
                        prefix.c_str(), i, m_frame_list[i]->m_pf_index, m_frame_list[i]->m_start,
                        m_frame_list[i]->m_end, m_frame_list[i]->m_refresh,
                        m_frame_list[i]->m_present_count);
                for (size_t j = 0; j < NUM_FORMAT_COMPONENTS; j++)
                {
                    dump_str->appendFormat("%d,", m_frame_list[i]->m_channel_data[j] ?
                            m_frame_list[i]->m_channel_data[j][0] : 0);
                }
                dump_str->appendFormat("\n");
            }
        }
    }
}

void HistogramCollector::prepareTempDataForLastFrame()
{
    if (m_frame_count > 0)
    {
        std::shared_ptr<FrameHistogram> last_frame = nullptr;
        last_frame = m_frame_list[m_tail];
        last_frame->m_end = static_cast<uint64_t>(systemTime());
        last_frame->m_present_count = static_cast<uint64_t>(lround(static_cast<float>(
                last_frame->m_end - last_frame->m_start) / last_frame->m_refresh));
    }
}

void HistogramCollector::group(const uint64_t max_frame, const uint64_t timestamp,
        uint64_t* frame_count, int32_t samples_size[NUM_FORMAT_COMPONENTS],
        uint64_t* samples[NUM_FORMAT_COMPONENTS])
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (uint8_t i = 0; i < NUM_FORMAT_COMPONENTS; i++)
    {
        uint8_t mask = m_mask >> i;
        samples_size[i] = (mask & 0x01) ? static_cast<int32_t>(m_bin_number) : 0;
    }

    if (m_frame_count == 0)
    {
        *frame_count = 0;
        initialiContentSampleLocked(samples_size, samples);
        return;
    }

    prepareTempDataForLastFrame();
    uint64_t expected_frame_count = (max_frame == 0) ? m_max_frames : max_frame;
    if (expected_frame_count > UINT32_MAX)
    {
        expected_frame_count = UINT32_MAX;
    }

    if (expected_frame_count == m_max_frames && timestamp == 0)
    {
        if (m_histogram_overflow)
        {
            groupWithFrameCountLocked(static_cast<size_t>(expected_frame_count), frame_count,
                    samples_size, samples);
        }
        else
        {
            groupAllLocked(frame_count, samples_size, samples);
        }
    }
    else if (!(expected_frame_count == m_max_frames) && timestamp == 0)
    {
        groupWithFrameCountLocked(static_cast<size_t>(expected_frame_count), frame_count,
                samples_size, samples);
    }
    else
    {
        groupWithTimestampAndFrameCountLocked(static_cast<size_t>(expected_frame_count),
                timestamp, frame_count, samples_size, samples);
    }
}

void HistogramCollector::groupAllLocked(uint64_t* frame_count,
        int32_t samples_size[NUM_FORMAT_COMPONENTS], uint64_t* samples[NUM_FORMAT_COMPONENTS])
{
    if (frame_count != nullptr)
    {
        *frame_count = m_frame_count;
    }
    for (size_t i = 0; i < NUM_FORMAT_COMPONENTS; i++)
    {
        if (samples != nullptr && samples[i] != nullptr && samples_size[i] > 0)
        {
            uint32_t min_size = std::min(static_cast<uint32_t>(samples_size[i]), m_bin_number);
            memcpy(samples[i], m_channel_data[i], sizeof(*samples[i]) * min_size);
        }
    }
    increaseContentSampleLocked(samples_size, samples, m_tail, 0);
}

void HistogramCollector::groupWithFrameCountLocked(const size_t max_frame,
        uint64_t* frame_count, int32_t samples_size[NUM_FORMAT_COMPONENTS],
        uint64_t* samples[NUM_FORMAT_COMPONENTS])
{
    size_t count = max_frame;
    initialiContentSampleLocked(samples_size, samples);
    size_t pos = m_tail;
    while (count > 0)
    {
        count -= increaseContentSampleLocked(samples_size, samples, pos, count);
        if (pos == m_head)
        {
            break;
        }
        pos = prev(pos);
    }
    *frame_count = max_frame - count;
}

void HistogramCollector::groupWithTimestampAndFrameCountLocked(const size_t max_frame,
        const uint64_t timestamp, uint64_t* frame_count,
        int32_t samples_size[NUM_FORMAT_COMPONENTS], uint64_t* samples[NUM_FORMAT_COMPONENTS])
{
    size_t count = max_frame;
    initialiContentSampleLocked(samples_size, samples);
    size_t pos = m_tail;
    while (count > 0)
    {
        if (m_frame_list[pos]->m_start < timestamp)
        {
            break;
        }
        count -= increaseContentSampleLocked(samples_size, samples, pos, count);
        if (pos == m_head)
        {
            break;
        }
        pos = prev(pos);
    }
    *frame_count = max_frame - count;
}

void HistogramCollector::initialiContentSampleLocked(int32_t samples_size[NUM_FORMAT_COMPONENTS],
        uint64_t* samples[NUM_FORMAT_COMPONENTS])
{
    for (size_t i = 0; i < NUM_FORMAT_COMPONENTS; i++)
    {
        if (samples != nullptr && samples[i] != nullptr && samples_size[i] > 0)
        {
            memset(samples[i], 0, sizeof(*samples[i]) * static_cast<uint32_t>(samples_size[i]));
        }
    }
}

size_t HistogramCollector::increaseContentSampleLocked(int32_t samples_size[NUM_FORMAT_COMPONENTS],
        uint64_t* samples[NUM_FORMAT_COMPONENTS], size_t pos, size_t remained_count)
{
    std::shared_ptr<FrameHistogram> frame = m_frame_list[pos];
    size_t present_count = 0;
    size_t frame_present_count = 0;
    if (frame->m_present_count > UINT32_MAX)
    {
        frame_present_count = UINT32_MAX;
    }
    else
    {
        frame_present_count = static_cast<size_t>(frame->m_present_count);
    }
    if (remained_count == 0)
    {
        present_count = frame_present_count;
    }
    else
    {
        present_count = remained_count > frame_present_count ? frame_present_count :
                remained_count;
    }
    for (size_t i = 0; i < NUM_FORMAT_COMPONENTS; i++)
    {
        if (samples != nullptr && samples[i] != nullptr && samples_size[i] > 0)
        {
            uint32_t min_size = std::min(static_cast<uint32_t>(samples_size[i]),
                    frame->m_bin_number);
            for (size_t j = 0; j < min_size; j++)
            {
                uint64_t increment = static_cast<uint64_t>(frame->m_channel_data[i][j]) *
                        present_count;
                uint64_t amount = samples[i][j] + increment;
                if (CC_UNLIKELY(amount < samples[i][j] || amount < increment))
                {
                    samples[i][j] = std::numeric_limits<uint64_t>::max();
                }
                else
                {
                    samples[i][j] = amount;
                }
            }
        }
    }

    return present_count;
}

size_t HistogramCollector::next(size_t pos)
{
    if (SIZE_MAX == pos)
    {
        return 0;
    }
    return (pos + 1) % m_frame_list.size();
}

size_t HistogramCollector::prev(size_t pos)
{
    if (pos == 0)
    {
        return m_frame_list.size() - 1;
    }
    return pos - 1;
}

std::shared_ptr<FrameHistogram> HistogramCollector::getTempFrameHistogram()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::shared_ptr<FrameHistogram>& frame = m_frame_list[m_temp_pos];
    if (frame == nullptr)
    {
        frame = std::make_shared<FrameHistogram>(m_mask, m_channel_number, m_bin_number);
    }
    return frame;
}

void HistogramCollector::pushTempFrameHistogram()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::shared_ptr<FrameHistogram> remove_frame = nullptr;
    std::shared_ptr<FrameHistogram> temp_frame = m_frame_list[m_temp_pos];
    std::shared_ptr<FrameHistogram> last_frame = nullptr;
    if (m_frame_count >= m_max_frames)
    {
        remove_frame = m_frame_list[m_head];
    }
    if (m_frame_count > 0)
    {
        last_frame = m_frame_list[m_tail];
        last_frame->m_end = temp_frame->m_start;
        last_frame->m_present_count = static_cast<uint64_t>(lround(static_cast<float>(
                last_frame->m_end - last_frame->m_start) / last_frame->m_refresh));
    }
    m_tail = m_temp_pos;
    m_temp_pos = next(m_temp_pos);

    if (remove_frame != nullptr)
    {
        decreaseBinDataLocked(remove_frame);
        m_head = next(m_head);
    }
    else
    {
        m_frame_count++;
    }
    if (last_frame != nullptr)
    {
        increaseBinDataLocked(last_frame);
    }
}

//=============================================================================

ColorHistogram::ColorHistogram(uint64_t disp_id)
    : m_disp_id(disp_id)
    , m_hw_state(HISTOGRAM_STATE_NO_SUPPORT)
    , m_format(0)
    , m_dataspace(0)
    , m_mask(0)
    , m_max_bin(0)
    , m_active_bin(0)
    , m_enable(HWC2_DISPLAYED_CONTENT_SAMPLING_INVALID)
    , m_state(COLOR_HISTOGRAM_STATE_STOP)
    , m_collected_mask(0)
    , m_collected_max_frame(0)
    , m_channel_number(0)
    , m_stop_guarder(true)
    , m_retry_count(0)
    , m_active_config(0)
    , m_refresh(0)
{
    m_histogram = getHwDevice();
    if (m_histogram == nullptr)
    {
        return;
    }

    m_hw_state = m_histogram->isHwcFeatureSupported(HWC_FEATURE_COLOR_HISTOGRAM) ?
            HISTOGRAM_STATE_SUPPORT : HISTOGRAM_STATE_NO_SUPPORT;
    if (m_hw_state != HISTOGRAM_STATE_SUPPORT)
    {
        return;
    }

    int res = m_histogram->getHistogramAttribute(&m_format, &m_dataspace, &m_mask, &m_max_bin);
    if (res != NO_ERROR)
    {
        HWC_LOGW("(%" PRIu64 ")failed to initial color histogram: %d", m_disp_id,res);
        m_hw_state = HISTOGRAM_STATE_NO_SUPPORT;
    }
    else
    {
        updateActiveBinNumber();
    }
}

ColorHistogram::~ColorHistogram()
{
}

void ColorHistogram::updateActiveBinNumber()
{
    uint32_t target = Platform::getInstance().m_config.histogram_bin_number;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_enable == HWC2_DISPLAYED_CONTENT_SAMPLING_ENABLE)
    {
        return;
    }

    if (target != 0 && (m_max_bin % target) == 0)
    {
        m_active_bin = target;
    }
    else
    {
        m_active_bin = m_max_bin;
    }
}

int32_t ColorHistogram::getAttribute(int32_t* format, int32_t* dataspace, uint8_t* mask)
{
    if (m_hw_state != HISTOGRAM_STATE_SUPPORT)
    {
        return HWC2_ERROR_UNSUPPORTED;
    }

    if (format == nullptr || dataspace == nullptr || mask == nullptr)
    {
        HWC_LOGW("%s: parameter has nullptr[f=%p, ds=%p, sc=%p]",
                __func__, format, dataspace, mask);
        return HWC2_ERROR_BAD_PARAMETER;
    }
    *format = m_format;
    *dataspace = m_dataspace;
    *mask = m_mask;

    return HWC2_ERROR_NONE;
}

int32_t ColorHistogram::setContentSamplingEnabled(const int32_t enable, const uint8_t component_mask,
        const uint64_t max_frames)
{
    if (enable == HWC2_DISPLAYED_CONTENT_SAMPLING_INVALID)
    {
        return HWC2_ERROR_BAD_PARAMETER;
    }

    if (m_hw_state != HISTOGRAM_STATE_SUPPORT)
    {
        return HWC2_ERROR_UNSUPPORTED;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_enable == enable)
    {
        return HWC2_ERROR_NONE;
    }

    if (enable == HWC2_DISPLAYED_CONTENT_SAMPLING_ENABLE)
    {
        uint8_t check_mask = component_mask | m_mask;
        if (check_mask != m_mask)
        {
            HWC_LOGW("(%" PRIu64 ") failed to enable sampling with invalid mask: [0x%" PRIx8 "|0x%" PRIx8 "]",
                    m_disp_id, component_mask, m_mask);
            return HWC2_ERROR_BAD_PARAMETER;
        }
        HWC_LOGI("(%" PRIu64 ") enable color histogram: mask[%" PRIx8"] and frame[%" PRIu64 "]",
                m_disp_id, component_mask, max_frames);
        m_collected_mask = component_mask == 0 ? m_mask : component_mask;
        m_collected_max_frame = max_frames;

        m_channel_number = static_cast<uint8_t>(popcount(m_collected_mask));
        m_collector.resize(max_frames, m_collected_mask, m_channel_number, m_active_bin);

        std::lock_guard<std::mutex> locker(m_mutex_control_guarder);
        m_stop_guarder = false;
        m_guarder = std::thread(&ColorHistogram::gatherThread, this);
        m_enable = enable;
    }
    else if (enable == HWC2_DISPLAYED_CONTENT_SAMPLING_DISABLE)
    {
        HWC_LOGI("(%" PRIu64 ") disable color histogram", m_disp_id);
        {
            std::lock_guard<std::mutex> lock_guarder(m_mutex_control_guarder);
            m_pf_table.clear();
            m_stop_guarder = true;
        }
        if (m_guarder.joinable())
        {
            m_condition_guarder.notify_one();
            m_guarder.join();
        }
        m_enable = enable;
    }
    else
    {
        HWC_LOGW("(%" PRIu64 ") failed to enable sampling with invlaid enable parameter(%d)",
                m_disp_id, enable);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    return HWC2_ERROR_NONE;
}

int32_t ColorHistogram::getContentSample(const uint64_t max_frames, const uint64_t timestamp,
        uint64_t* frame_count, int32_t samples_size[NUM_FORMAT_COMPONENTS],
        uint64_t* samples[NUM_FORMAT_COMPONENTS])
{
    HWC_ATRACE_NAME("getContentSample");
    if (m_hw_state != HISTOGRAM_STATE_SUPPORT)
    {
        return HWC2_ERROR_UNSUPPORTED;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_state == COLOR_HISTOGRAM_STATE_ERROR)
    {
        return HWC2_ERROR_BAD_PARAMETER;
    }

    if (frame_count == nullptr)
    {
        return HWC2_ERROR_BAD_PARAMETER;
    }

    m_collector.group(max_frames, timestamp, frame_count, samples_size, samples);

    return HWC2_ERROR_NONE;
}

int32_t ColorHistogram::addPresentInfo(unsigned int index, int fd, hwc2_config_t active_config)
{
    if (m_hw_state != HISTOGRAM_STATE_SUPPORT)
    {
        return INVALID_OPERATION;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_enable != HWC2_DISPLAYED_CONTENT_SAMPLING_ENABLE)
    {
        return NO_ERROR;
    }

    std::lock_guard<std::mutex> lock_pf(m_mutex_control_guarder);
    int dup_fd = dup(fd);
    HWC_LOGD("%s: index=%u  fd=%d", __func__, index, dup_fd);
    std::shared_ptr<FenceState> ptr = std::make_shared<FenceState>(index, dup_fd, active_config);
    if (m_pf_table.size() > 16)
    {
        HWC_LOGW("(%" PRIu64 ")%s: too many present info, so clear all", m_disp_id, __func__);
        m_pf_table.clear();
    }
    m_pf_table.push_back(ptr);
    m_condition_guarder.notify_one();

    return NO_ERROR;
}

void ColorHistogram::dump(String8* dump_str)
{
    if (dump_str == nullptr)
    {
        return;
    }

    if (m_histogram == nullptr)
    {
        return;
    }

    updateActiveBinNumber();
    bool enable_debug = (Platform::getInstance().m_config.plat_switch &
            HWC_PLAT_SWITCH_DEBUG_HISTOGRAM) != 0;

    dump_str->appendFormat("\n");
    dump_str->appendFormat("Color Histogram State:\n");
    dump_str->appendFormat("[device capability]\n");
    dump_str->appendFormat("\thw_state: %d\n", m_hw_state);
    if (m_hw_state != HISTOGRAM_STATE_SUPPORT)
    {
        dump_str->appendFormat("\n");
        return;
    }
    dump_str->appendFormat("\tformat: %d\n", m_format);
    dump_str->appendFormat("\tdataspace: %d\n", m_dataspace);
    dump_str->appendFormat("\tmask: %d\n", m_mask);
    dump_str->appendFormat("\tbin: %u(%u)\n", m_active_bin, m_max_bin);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        dump_str->appendFormat("[device state]\n");
        dump_str->appendFormat("\tenable: %d\n", m_enable);
        dump_str->appendFormat("\tstate: %d\n", m_state);
        dump_str->appendFormat("\tchanne_number: %u\n", m_channel_number);
        dump_str->appendFormat("\tmask: %u\n", m_collected_mask);
        dump_str->appendFormat("\tmax_frame: %" PRIu64 "\n", m_collected_max_frame);
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex_control_guarder);
        dump_str->appendFormat("[thread state]\n");
        dump_str->appendFormat("\tstop: %d\n", m_stop_guarder);
        dump_str->appendFormat("[pf table]\n");
        dump_str->appendFormat("\tsize: %zu\n", m_pf_table.size());
        if (enable_debug)
        {
            size_t i = 0;
            for (auto iter = m_pf_table.begin(); iter != m_pf_table.end(); ++iter, i++)
            {
                dump_str->appendFormat("\t[%zu] idx=%u fd=%d invalid=%d signal=%d time=%" PRIu64 "\n",
                        i, (*iter)->m_index, (*iter)->m_fence_fd,
                        (*iter)->m_invalid, (*iter)->m_is_signal,
                        (*iter)->m_signal_time);
            }
        }
    }

    dump_str->appendFormat("[histogram data]\n");
    m_collector.dump(dump_str, "\t");
    dump_str->appendFormat("\n");
}

void ColorHistogram::gatherThread()
{
    m_histogram->enableHistogram(true, m_format, m_collected_mask, m_dataspace, m_active_bin);
    m_retry_count = 0;
    size_t pf_table_size = 0;
    while (true)
    {
        HWC_ATRACE_NAME("gatherThread");
        std::shared_ptr<FenceState> fstate = nullptr;
        {
            std::unique_lock<std::mutex> lock_guarder(m_mutex_control_guarder);
            if (m_stop_guarder)
            {
                m_histogram->enableHistogram(false, m_format, m_collected_mask, m_dataspace, m_active_bin);
                break;
            }

            if (m_pf_table.empty() || m_retry_count >= 5)
            {
                if (m_retry_count >= 5)
                {
                    HWC_LOGD("%s: try_count=%u, force waiting", __func__, m_retry_count);
                }
                m_retry_count = 0;
                m_condition_guarder.wait(lock_guarder);
                continue;
            }
            else
            {
                // find a valid and unsigned buffer
                for (auto iter = m_pf_table.begin(); iter != m_pf_table.end(); ++iter)
                {
                    if ((*iter)->needCheck())
                    {
                        fstate = *iter;
                        break;
                    }
                }
                if (fstate == nullptr)
                {
                    auto res = m_condition_guarder.wait_for(lock_guarder, std::chrono::milliseconds(32));
                    if (res == std::cv_status::timeout)
                    {
                        m_retry_count++;
                    }
                    else
                    {
                        continue;
                    }
                }
            }
        }

        DbgLogger logger(DbgLogger::TYPE_HWC_LOG, 'D', nullptr);
        if (fstate != nullptr)
        {
            logger.printf("[%s] pf_table_size=%zu, wait_fence=%u| ", DEBUG_LOG_TAG, pf_table_size,
                    fstate->getFenceIndex());
            fstate->waitSignal();
        }
        else
        {
            logger.printf("[%s] pf_table_size=%zu, skip_wait_fence| ", DEBUG_LOG_TAG,
                    pf_table_size);
        }

        // try to get a histogram from driver
        unsigned int histogram_index = 0;
        std::shared_ptr<FrameHistogram> frame_histogram = m_collector.getTempFrameHistogram();
        int res = 0;
        if (frame_histogram != nullptr && frame_histogram->m_data)
        {
            res = m_histogram->collectHistogram(&histogram_index, frame_histogram->m_channel_data);
            logger.printf("res=%d, get_ch=%u| ", res, histogram_index);
        }

        // try to find the signal time for histogram
        bool find_present_time = false;
        if (res >= 0)
        {
            std::lock_guard<std::mutex> lock(m_mutex_control_guarder);
            // find the same fence index for histogram
            auto target_fence = m_pf_table.end();
            for (auto iter = m_pf_table.begin(); iter != m_pf_table.end(); ++iter)
            {
                uint64_t signal_time = (*iter)->getSingalTime();
                if ((*iter)->getFenceIndex() == histogram_index)
                {
                    if ((*iter)->isSignal())
                    {
                        frame_histogram->m_pf_index = histogram_index;
                        frame_histogram->m_start = signal_time;
                        if (m_active_config != (*iter)->getActiveConfig() || m_refresh == 0)
                        {
                            m_active_config = (*iter)->getActiveConfig();
                            m_refresh = static_cast<uint64_t>(
                                    DisplayManager::getInstance().getDisplayData(m_disp_id,
                                    m_active_config)->refresh);
                        }
                        frame_histogram->m_refresh = m_refresh;
                        find_present_time = true;
                        target_fence = iter;
                    }
                    else
                    {
                        HWC_LOGW("get histogram with unsiganled fence(%u)", histogram_index);
                    }
                    break;
                }
            }

            // remove uselss fence data
            if (target_fence != m_pf_table.end())
            {
                logger.printf("remove_fence=%u~%u| ", (*m_pf_table.begin())->getFenceIndex(),
                        (*target_fence)->getFenceIndex());
                std::advance(target_fence, 1);
                m_pf_table.erase(m_pf_table.begin(), target_fence);
            }
        }

        // push the histogram to database
        if (find_present_time)
        {
            logger.printf("push_data=%u", histogram_index);
            m_collector.pushTempFrameHistogram();
        }
        else
        {
            logger.printf("present_time_not_found");
        }
    }
}
