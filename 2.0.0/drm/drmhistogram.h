#ifndef DRM_HISTOGRAM_H
#define DRM_HISTOGRAM_H

#include <stdint.h>
#include <memory>

#include <linux/mediatek_drm.h>
#include "dev_interface.h"
#include "drm/drmmoderesource.h"

using namespace android;

class DrmHistogramDevice
{
public:
    DrmHistogramDevice();
    ~DrmHistogramDevice();

    void initState(DrmModeResource* drm, mtk_drm_disp_caps_info& caps_info);

    int32_t getDeviceState();

    bool isHwSupport();

    int32_t getAttribute(int32_t* color_format, int32_t* dataspace, uint8_t* mask,
            uint32_t* max_bin);

    int32_t enableHistogram(const bool enable, const int32_t format, const uint8_t format_mask,
            const int32_t dataspace, const uint32_t bin_count);

    int32_t collectHistogram(uint32_t *fence_index, uint32_t* histogram_ptr[NUM_FORMAT_COMPONENTS]);

private:
    int32_t checkHwState();

    uint8_t calculateColorMask(int32_t format, uint32_t bit);

    void getFormatOrder(int32_t format, uint8_t order[NUM_FORMAT_COMPONENTS]);

    void getFormatBit(int32_t format, uint32_t bit[NUM_FORMAT_COMPONENTS]);

    MTK_DRM_CHIST_COLOR_FORMT getNthHistogramFormat(int32_t format, uint8_t format_mask, uint8_t n);

    uint32_t getFormatOrder(int32_t format, MTK_DRM_CHIST_COLOR_FORMT histogram_format);

private:
    DrmModeResource *m_drm;
    int32_t m_hw_state;

    uint32_t m_format_bit;
    int32_t m_format;
    int32_t m_dataspace;
    uint8_t m_mask;
    uint32_t m_max_bin;
    uint32_t m_max_channel;

    bool m_enable;
    uint8_t m_collected_mask;
    uint8_t m_channel_number;
    uint32_t m_bin_number;
    uint64_t m_total_bin_number;
};

#endif
