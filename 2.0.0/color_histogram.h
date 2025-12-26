#ifndef HWC_COLOR_HISTOGRAM_H
#define HWC_COLOR_HISTOGRAM_H

#include <mutex>
#include <thread>
#include <map>
#include <vector>
#include <memory>
#include <deque>

#include <utils/String8.h>
#include <utils/Timers.h>
#include <hardware/hwcomposer2.h>

#include "dev_interface.h"

using namespace android;

enum
{
    COLOR_HISTOGRAM_STATE_STOP = 0,
    COLOR_HISTOGRAM_STATE_START,
    COLOR_HISTOGRAM_STATE_ERROR,
    COLOR_HISTOGRAM_STATE_NUM,
};

class FenceState
{
public:
    FenceState(unsigned int index, int fence_fd, hwc2_config_t active_config);
    ~FenceState();

    unsigned int getFenceIndex();
    bool needCheck();
    bool isSignal();
    int waitSignal();
    uint64_t getSingalTime();
    hwc2_config_t getActiveConfig();

private:
public:
    unsigned int m_index;
    int m_fence_fd;
    bool m_invalid;
    bool m_is_signal;
    uint64_t m_signal_time;
    hwc2_config_t m_active_config;
};

class FrameHistogram
{
public:
    FrameHistogram(uint8_t mask, uint8_t channel_number, uint32_t bin_number);
    ~FrameHistogram();

public:
    unsigned int m_pf_index;
    uint64_t m_start;
    uint64_t m_end;
    uint64_t m_refresh;
    uint64_t m_present_count;
    uint8_t m_mask;
    uint8_t m_channel_number;
    uint32_t m_bin_number;
    uint64_t m_total_bin_number;
    uint32_t* m_data;
    uint32_t* m_channel_data[NUM_FORMAT_COMPONENTS];
};

class HistogramCollector
{
public:
    HistogramCollector();
    ~HistogramCollector();

    int32_t resize(uint64_t max_frames, uint8_t mask, uint8_t channel_number, uint32_t bin_number);

    void push(std::shared_ptr<FrameHistogram> frame);

    void dump(String8* dump_str, std::string prefix = "");

    void group(const uint64_t max_frame, const uint64_t timestamp, uint64_t* frame_count,
            int32_t samples_size[NUM_FORMAT_COMPONENTS], uint64_t* samples[NUM_FORMAT_COMPONENTS]);

    std::shared_ptr<FrameHistogram> getTempFrameHistogram();

    void pushTempFrameHistogram();

private:
    void increaseBinDataLocked(std::shared_ptr<FrameHistogram> frame);

    void decreaseBinDataLocked(std::shared_ptr<FrameHistogram> frame);

    void groupAllLocked(uint64_t* frame_count, int32_t samples_size[NUM_FORMAT_COMPONENTS],
            uint64_t* samples[NUM_FORMAT_COMPONENTS]);

    void groupWithFrameCountLocked(const size_t max_frame, uint64_t* frame_count,
            int32_t samples_size[NUM_FORMAT_COMPONENTS], uint64_t* samples[NUM_FORMAT_COMPONENTS]);

    void groupWithTimestampAndFrameCountLocked(const size_t max_frame, const uint64_t timestamp,
            uint64_t* frame_count, int32_t samples_size[NUM_FORMAT_COMPONENTS],
            uint64_t* samples[NUM_FORMAT_COMPONENTS]);

    void initialiContentSampleLocked(int32_t samples_size[NUM_FORMAT_COMPONENTS],
            uint64_t* samples[NUM_FORMAT_COMPONENTS]);

    size_t increaseContentSampleLocked(int32_t samples_size[NUM_FORMAT_COMPONENTS],
            uint64_t* samples[NUM_FORMAT_COMPONENTS], size_t pos, size_t remained_count);

    size_t next(size_t pos);

    size_t prev(size_t pos);

    void prepareTempDataForLastFrame();

private:
    std::mutex m_mutex;
    std::deque<std::shared_ptr<FrameHistogram> > m_frame_list;
    size_t m_head;
    size_t m_tail;
    size_t m_temp_pos;
    size_t m_frame_count;
    uint64_t m_total_present_count;
    bool m_histogram_overflow;
    uint64_t* m_data;
    uint64_t* m_channel_data[NUM_FORMAT_COMPONENTS];

    uint8_t m_mask;
    size_t m_max_frames;
    uint8_t m_channel_number;
    uint32_t m_bin_number;
    uint64_t m_total_bin_number;
};

class ColorHistogram
{
public:
    ColorHistogram(uint64_t disp_id);
    ~ColorHistogram();

    int32_t getAttribute(int32_t* format, int32_t* dataspace, uint8_t* mask);

    int32_t setContentSamplingEnabled(const int32_t enable, const uint8_t component_mask,
            const uint64_t max_frames);

    int32_t getContentSample(const uint64_t max_frames, const uint64_t timestamp,
            uint64_t* frame_count, int32_t samples_size[NUM_FORMAT_COMPONENTS],
            uint64_t* samples[NUM_FORMAT_COMPONENTS]);

    int32_t addPresentInfo(unsigned int index, int fd, hwc2_config_t active_config);

    void dump(String8* dump_str);

private:
    void gatherThread();

    void updateActiveBinNumber();

private:
    uint64_t m_disp_id;
    sp<IOverlayDevice> m_histogram;

    // capability
    int32_t m_hw_state;
    int32_t m_format;
    int32_t m_dataspace;
    uint8_t m_mask;
    uint32_t m_max_bin;
    uint32_t m_active_bin;

    // state
    std::mutex m_mutex;
    int32_t m_enable;
    int32_t m_state;
    uint8_t m_collected_mask;
    uint64_t m_collected_max_frame;
    uint8_t m_channel_number;

    // guarder thread
    std::mutex m_mutex_control_guarder;
    std::condition_variable m_condition_guarder;
    std::thread m_guarder;
    bool m_stop_guarder;
    uint32_t m_retry_count;
    hwc2_config_t m_active_config;
    uint64_t m_refresh;

    // present fence info
    std::list<std::shared_ptr<FenceState> > m_pf_table;

    // histogram data
    HistogramCollector m_collector;
};

#endif
