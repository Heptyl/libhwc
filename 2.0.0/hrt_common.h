#ifndef HWC_HRT_INTERFACE_H
#define HWC_HRT_INTERFACE_H

#include <sstream>
#include <vector>

#include <utils/StrongPointer.h>
#include <utils/String8.h>
#include <utils/RefBase.h>

#include "display.h"
#include "dev_interface.h"

using namespace android;

class HWCDisplay;
struct HrtLayerConfig;

class HrtCommon : public RefBase
{
public:
    HrtCommon()
        : m_is_validate_separate(false)
    {
        memset(m_layer_config_len, 0, sizeof(m_layer_config_len));
        memset(m_hrt_config_list, 0, sizeof(m_hrt_config_list));
        memset(m_hrt_config_len, 0, sizeof(m_hrt_config_len));
    }

    virtual ~HrtCommon() {}

    virtual bool isEnabled() const;

    virtual bool isRPOEnabled() const;

    virtual void dump(String8* str, const hwc2_display_t& disp_id) const;

    virtual void printQueryValidLayerResult() = 0;

    static void printLog(const std::string& str);

    virtual void modifyMdpDstRoiIfRejectedByRpo();

    virtual void init(const std::vector<sp<HWCDisplay>>& displays, const std::vector<sp<IOverlayDevice>>& disp_devs, int32_t ovl_type);

    virtual void init(const std::vector<sp<HWCDisplay>>& displays, const std::vector<sp<IOverlayDevice>>& disp_devs, uint64_t disp_id);

    virtual void run(const bool& is_skip_validate);

    void simpleLayeringRule();

    virtual void fillLayerConfigList() = 0;

    virtual void fillDispLayer() = 0;

    virtual void fillLayerInfoOfDispatcherJob();

    virtual void setCompType();

    virtual bool queryValidLayer() = 0;

    // this function only called before hrt caculate for updating hwc_gles range
    void updateGlesRangeForDisplaysBeforHrt();

    // this function only called after hrt for updating hwtype in gles_range
    void updateGlesRangeHwTypeForDisplays();

    virtual void onHotplug(uint64_t /*disp_id*/, bool /*connected*/) {}

protected:
    void init(const std::vector<sp<HWCDisplay>>& displays, const std::vector<sp<IOverlayDevice>>& disp_devs);

protected:
    bool m_is_validate_separate;

    std::vector<uint64_t> m_disp_id_list;
    sp<HWCDisplay> m_displays[DisplayManager::MAX_DISPLAYS];
    sp<IOverlayDevice> m_disp_devs[DisplayManager::MAX_DISPLAYS];

    std::stringstream m_hrt_result[DisplayManager::MAX_DISPLAYS];
    unsigned int m_layer_config_len[DisplayManager::MAX_DISPLAYS];
    HrtLayerConfig* m_hrt_config_list[DisplayManager::MAX_DISPLAYS];
    unsigned int m_hrt_config_len[DisplayManager::MAX_DISPLAYS];
};

class HrtHelper
{
public:
    static HrtHelper& getInstance();

    ~HrtHelper() {}

    void dump(String8* str, const hwc2_display_t& disp_id);

    void init(const std::vector<sp<HWCDisplay>>& displays, const std::vector<sp<IOverlayDevice>>& disp_devs, bool is_validate_separate);

    void run(const bool& is_skip_validate);

    void run(uint64_t disp_id, const bool& is_skip_validate);

    void onHotplug(uint64_t disp_id, bool connected);

private:
    HrtHelper();

private:
    enum
    {
        HRT_TYPE_OVL = 0,
        HRT_TYPE_BLITDEV,
        HRT_TYPE_NUM
    };

    bool m_is_validate_separate;

    std::vector<sp<HrtCommon>> m_hrt;
};

#endif
