#ifndef HWC_HWCDISPLAY_H
#define HWC_HWCDISPLAY_H

#include <atomic>
#include <vector>
#include <queue>
#include <utils/RefBase.h>
#include <hardware/hwcomposer2.h>

#include "utils/tools.h"
#include "utils/fpscounter.h"

#include "hwcbuffer.h"
#include "hwclayer.h"
#include "hwc2_defs.h"
#include "color.h"
#include "color_histogram.h"
#include "display_dump.h"
#include "dispatcher.h"

#define STABLE_INACTIVE_SET_THRESHOLD 10
#define STABLE_FBT_CACHE_HIT_THRESHOLD (STABLE_INACTIVE_SET_THRESHOLD + 1)
#define MINIMUM_VISIBLE_LAYERS_SIZE 5
#define MINIMUM_INACTIVE_LAYERS_SIZE 3

#define REFRESH_CHANGED_DO_VALIDATE_NUM 1
#define OTHER_HOTPLUG_CHANGED_DO_VALIDATE_NUM 1

struct IntentInfo
{
    int32_t id;
    int32_t dynamic_range;
    int32_t intent;
};

struct LayerIdAndCompType
{
    LayerIdAndCompType()
    : id(0)
    , type(HWC2_COMPOSITION_INVALID)
    {
    };
    uint64_t id;
    int32_t type;
};

enum
{
    MTK_COMPOSITION_MODE_INIT,
    MTK_COMPOSITION_MODE_NORMAL,
    MTK_COMPOSITION_MODE_DECOUPLE,
};

enum HWC_DISPLAY_CONFIG_CHANGED : uint32_t
{
    HWC_DISPLAY_CONFIG_CHANGED_ACTIVE_CONFIG = 1 << 0,
    HWC_DISPLAY_CONFIG_CHANGED_WDMA = 1 << 1,
};

enum HWC_WDMA_STATUS : uint32_t
{
    HWC_WDMA_STATUS_DISABLE = 0,
    HWC_WDMA_STATUS_NEED_UPDATE = 1 << 0,
    HWC_WDMA_STATUS_AIBLD = 1 << 1,
    HWC_WDMA_STATUS_DISPLAY_DUMP = 1 << 2,
};


enum CONNECTION_STATE
{
    CONNECTION_STATE_PLUG_OUT = 0,
    CONNECTION_STATE_PLUG_IN,
};

struct SwitchConfigInfo
{
    int64_t index = -1;
    hwc2_config_t config = 0;
    nsecs_t applied_time = -1;
    nsecs_t refresh_time = -1;
    nsecs_t period = -1;
    bool apply = false;
    nsecs_t sf_target_time = -1;
    unsigned int present_fence_index = 0;
    int present_fence_fd = -1;
};

bool operator< (const SwitchConfigInfo& info1, const SwitchConfigInfo& info2);
bool operator> (const SwitchConfigInfo& info1, const SwitchConfigInfo& info2);

struct LayerSet
{
    int32_t stable_cnt;// for m_current_set stabe
    int cycle_stable_cnt;// for life cycle
    nsecs_t last_update;
    nsecs_t cycle_last_update;// for life cycle
    std::vector<uint64_t> set_ids;
    bool is_confirmed_caps;
};

class HWCDisplay : public RefBase
{
public:
    HWCDisplay(const uint64_t& disp_id, const CreateDisplayInfo& create_disp_info, const sp<IOverlayDevice>& ovl);

    void init();

    DbgLogger& editSetBufFromSfLog() { return m_set_buf_from_sf_log; }
    DbgLogger& editSetCompFromSfLog() { return m_set_comp_from_sf_log; }

    bool checkSkipValidate(const bool& is_validate_call);
    void createJob();
    void prepareSfLog();
    void prepareForValidation();
    void validate();
    void calculateMdpDstRoi();
    void countdowmSkipValiRelatedNumber();
    void beforePresent(const unsigned int num_plugin_display);

    void present();

    void afterPresent();

    void clear();

    bool isConnected() const;

    bool isValidated() const;

    void getChangedCompositionTypes(
        uint32_t* out_num_elem,
        hwc2_layer_t* out_layers,
        int32_t* out_types) const;

    void getCompositionMode(bool& has_changed, int& new_mode);

    sp<HWCLayer> getLayer(const hwc2_layer_t& layer_id);

    const std::vector<sp<HWCLayer> >& getVisibleLayersSortedByZ();
    const std::vector<sp<HWCLayer> >& getInvisibleLayersSortedByZ();
    const std::vector<sp<HWCLayer> >& getCommittedLayers();

    sp<HWCLayer> getClientTarget();

    int32_t getWidth(hwc2_config_t config = 0) const;
    int32_t getHeight(hwc2_config_t config = 0) const;
    nsecs_t getVsyncPeriod(hwc2_config_t config = 0) const;
    int32_t getDpiX(hwc2_config_t config = 0) const;
    int32_t getDpiY(hwc2_config_t config = 0) const;
    int32_t getConfigGroup(hwc2_config_t config = 0) const;
    uint32_t getNumConfigs() const;
    int32_t getConfigs(uint32_t* out_num_configs, hwc2_config_t* out_configs) const;
    int32_t getAttribute(hwc2_config_t config, int32_t attribute, int32_t* out_value) const;
    bool setActiveConfig(hwc2_config_t config);
    hwc2_config_t getActiveConfig();
    hwc2_config_t getActiveConfig(nsecs_t active_time);
    int32_t getSecure() const;

    void setPowerMode(const int32_t& mode);
    int32_t getPowerMode() { return m_power_mode; }

    void setVsyncEnabled(const int32_t& enabled);

    void getType(int32_t* out_type) const;

    uint64_t getId() const { return m_disp_id; }

    int32_t createLayer(hwc2_layer_t* out_layer, const bool& is_ct);
    int32_t destroyLayer(const hwc2_layer_t& layer);

    int32_t getMirrorSrc() const { return m_mir_src; }
    void setMirrorSrc(const int32_t& disp) { m_mir_src = disp; }

    void getGlesRange(int32_t* gles_head, int32_t* gles_tail) const
    {
        *gles_head = m_gles_head;
        *gles_tail = m_gles_tail;
    }

    void setGlesRange(const int32_t& gles_head, const int32_t& gles_tail);

    void updateGlesRange();

    void acceptChanges();

    int32_t getRetireFenceFd() const {
        return m_retire_fence_fd;
    }

    void setRetireFenceFd(const int32_t& retire_fence_fd, const bool& is_disp_connected);

    void clearAllFences();

    const std::vector<LayerIdAndCompType>& getPrevCompTypes() { return m_prev_comp_types; }

    const std::vector<int32_t>& getSFCompTypesBeforeValid() { return m_SF_comp_before_valid; }

    void moveChangedCompTypes(std::vector<sp<HWCLayer> >* changed_comp_types)
    {
        // todo: move ?
        m_changed_comp_types = *changed_comp_types;
    }

    void getReleaseFenceFds(
        uint32_t* out_num_elem,
        hwc2_layer_t* out_layer,
        int32_t* out_fence_fd);

    void getClientTargetSupport(
        const uint32_t& width, const uint32_t& height,
        const int32_t& format, const int32_t& dataspace);

    void setOutbuf(const buffer_handle_t& handle, const int32_t& release_fence_fd);
    sp<HWCBuffer> getOutbuf() { return m_outbuf;}

    void dump(String8* dump_str);

    void initPrevCompTypes();

    void initSFCompTypesBeforeValid();

    void buildVisibleAndInvisibleLayersSortedByZ();
    void buildCommittedLayers();

    int32_t getColorTransformHint() { return m_color_transform_hint; }
    int32_t setColorTransform(const float* matrix, const int32_t& hint);

    int32_t getColorMode() { return m_color_mode; }
    int32_t setColorMode(const int32_t& color_mode);

    bool needDisableVsyncOffset() { return m_need_av_grouping; }

    void setupPrivateHandleOfLayers();

    const std::vector<sp<HWCLayer> >& getLastCommittedLayers() const { return m_last_committed_layers; }
    void setLastCommittedLayers(const std::vector<sp<HWCLayer> >& last_committed_layers) { m_last_committed_layers = last_committed_layers; }

    bool isGeometryChanged()
    {
#ifndef MTK_USER_BUILD
        ATRACE_CALL();
#endif
        const auto& committed_layers = getCommittedLayers();
        const auto& last_committed_layers = getLastCommittedLayers();
        if (committed_layers != last_committed_layers)
            return true;

        bool is_dirty = false;

        for (auto& layer: committed_layers)
        {
            is_dirty |= layer->isStateChanged();
        }
        return is_dirty;
    }

    void removePendingRemovedLayers();
    void buildVisibleAndInvisibleLayer();

    bool isValid()
    {
        if (!isConnected() || getPowerMode() == HWC2_POWER_MODE_OFF)
            return false;

        return true;
    }

    void setJobDisplayOrientation();
    void setJobDisplayData();
    bool isForceGpuCompose();

    unsigned int getPrevAvailableInputLayerNum() const { return m_prev_available_input_layer_num; }
    void setPrevAvailableInputLayerNum(unsigned int availInputNum) { m_prev_available_input_layer_num = availInputNum; }

    void setValiPresentState(HWC_VALI_PRESENT_STATE val, const int32_t& line);

    HWC_VALI_PRESENT_STATE getValiPresentState() const { return m_vali_present_state; }

    bool isVisibleLayerChanged() const { return m_is_visible_layer_changed; }
    void checkVisibleLayerChange(const std::vector<sp<HWCLayer> > &prev_visible_layers);
    void setColorTransformForJob(DispatcherJob* const job);

    void setJobVideoTimeStamp();

    bool isGpuComposition() const { return m_use_gpu_composition; }
    bool isColorTransformOK() const { return m_color_transform_ok; }
    void setGpuComposition(bool enable) { m_use_gpu_composition = enable; }

    void addUnpresentCount();
    void decUnpresentCount();
    int getUnpresentCount() { return m_unpresent_count; }
    int getPrevUnpresentCount() { return m_prev_unpresent_count; }
#ifdef MTK_IN_DISPLAY_FINGERPRINT
    // for LED HBM (High Backlight Mode) control
    bool getIsHBM() const { return m_is_HBM; }
    void setIsHBM(const bool& val) { m_is_HBM = val; }
#endif

    void updateGlesRangeHwType();
    void moveChangedHWCRequests(std::vector<sp<HWCLayer> >* changed_hwc_requests)
    {
        m_changed_hwc_requests = *changed_hwc_requests;
    }
    void getChangedRequests(
        uint32_t* out_num_elem,
        hwc2_layer_t* out_layers,
        int32_t* out_requests) const;
    unsigned int getClientClearLayerNum() { return m_client_clear_layer_num; };

    void setJobStatus(const int& dispatcher_job_status) { m_dispathcer_job_status = dispatcher_job_status; }
    int getJobStatus() { return m_dispathcer_job_status; }

    void initDisplayConfigs();
    void getBootResolution(int32_t *width, int32_t *height);
    void updateDisplayConfigs(bool connected);
    void printDisplayConfigs();
    enum {
        INVALID_DRIVER_MODE_ID = 0xFF,
    };
    struct HDMIMode
    {
       int32_t width;
       int32_t height;
       int32_t refresh;
       int32_t dpiX;
       int32_t dpiY;
    };

    void setMSync2Enable(bool enable);
    void setMSync2ParamTable(std::shared_ptr<MSync2Data::ParamTable> param_table)
    {
        m_msync_2_0_param_table = param_table;
    }

    // color histogram function
    // initalize the color histogram
    void initColorHistogram(const sp<IOverlayDevice>& ovl);

    // get the capability of color histogram
    int32_t getContentSamplingAttribute(int32_t* format, int32_t* dataspace, uint8_t* mask);

    // enable or disable color histogram
    int32_t setContentSamplingEnabled(const int32_t enable, const uint8_t component_mask,
            const uint64_t max_frams);

    // get the result of color histogram
    int32_t getContentSample(const uint64_t max_frames, const uint64_t timestamp,
            uint64_t* frame_count, int32_t samples_size[4], uint64_t* samples[4]);

    // when PostHandler get a present fence, set the present fence to color histogram and switch
    // display config
    int32_t addPresentInfo(unsigned int index, int fd, hwc2_config_t active_config);

    // color mode with render intent
    void populateColorModeAndRenderIntent(const sp<IOverlayDevice>& ovl);
    int32_t getColorModeList(uint32_t* out_num_modes, int32_t* out_modes);
    int32_t getRenderIntents(const int32_t mode, uint32_t* out_num_intents, int32_t* out_intents);
    int32_t setColorModeWithRenderIntent(const int32_t mode, const int32_t intent);

    void setSfTargetTs(nsecs_t ts) { m_sf_target_ts = ts; }

    void calculatePerf(DispatcherJob* job);

    // follow the scenario or platform switch to choose cpu set
    void setScenarioHint(ComposerExt::ScenarioHint flag, ComposerExt::ScenarioHint mask);
    unsigned int updateCpuSet();
    void setDirtyCpuSet();

    // get the vsync period which the display is using
    int32_t getDisplayVsyncPeriod(
        hwc2_vsync_period_t* out_vsync_period);

    // set the active config for HWC 2.4
    int32_t setActiveConfigWithConstraints(
        hwc2_config_t config,
        hwc_vsync_period_change_constraints_t* vsync_period_change_constraints,
        hwc_vsync_period_change_timeline_t* out_timeline);

    // apply the display config which is from setActiveConfigWithConstraints when code flow start
    // to apply new display and layer configuration
    void updateActiveConfigWithConstraints();

    // measure the period of next vsync
    nsecs_t getNextVsyncPeriod(nsecs_t timestamp);

    // this function is called by SwitchConfigMonitor when the display config is used by driver
    void updateAppliedConfigState(int64_t index, hwc2_config_t config, nsecs_t period);

    // initialize the value of m_applied_period to config 0
    void initializeAppliedPeriod();

    // this function is called by SwitchConfigMonitor when the deadline is gone. then HWCDisplay
    // can check it whether the active config is packed with a frame
    bool needRequestRefresh(int64_t* index, nsecs_t* applied_time, nsecs_t* refresh_time,
            bool* check_again, nsecs_t* new_deadline);

    // request SwitchConfigMonitor start monitor a pending display config
    void monitorActiveConfigList();

    // get brightness capability and set brightness
    int32_t getBrightnessSupport(bool* support);
    int32_t setBrightness(float brightness);

    std::shared_ptr<DisplayDump> getDisplayDump()
    {
        return m_disp_dump;
    }

    // create the present index layer
    void createPresentIdxLayer(const uint32_t width, const uint32_t height,
            const uint32_t x, const uint32_t y);
    // destroy the present index layer
    void destroyPresentIdxLayer();

    bool isInternal() const;

    bool isSupportMml() const;

    bool isSupportDispPq() const;
    void setPrevSupportDispPq(bool prev_support_disp_pq) { m_prev_is_support_disp_pq = prev_support_disp_pq; }
    bool isPrevSupportDispPq() const { return m_prev_is_support_disp_pq; }

    bool isRpoSupported() const;

    uint32_t getDrmIdCurCrtc() const;
    uint32_t getDrmIdConnector() const;

    bool isConfigChanged() const { return m_config_changed != 0; }
    uint32_t getConfigChangedReason() { return m_config_changed; }

    // force validate if WDMA status changed
    void setWdmaEnable(uint32_t status, bool enable);
    void setWdmaStatus();
    uint32_t getWdmaStatus()
    {
        return m_wdma_status;
    }
    void getWdmaConfig(uint32_t& fmt, uint32_t& src_width, uint32_t& src_height,
                       uint32_t& dst_width, uint32_t& dst_height) const;

    bool isSupportBWMonitor() const;
    bool isSupportSmartComposition() const;

    void preRecognitionUnchangedLayer(const uint64_t& job_id);
    void preRecognitionInactiveSet(const uint64_t& /*job_id*/, const bool& /*is_validate_call*/) { return; }
    void postRecognitionInactiveSet(int32_t /*gles_head*/, int32_t /*gles_tail*/, uint32_t /*disp_caps*/) { return; }
    bool hasValidCurrentSet() const { return (m_current_set.stable_cnt > STABLE_INACTIVE_SET_THRESHOLD); }
    int32_t getCurrentSetStableCnt() const { return m_current_set.stable_cnt; }
    void clearCurrentSet();
    void setFbtUnchangedHint(bool fbt_unchagned) { m_fbt_unchanged_hint = fbt_unchagned;}
    bool isFbtUnchanged() { return m_fbt_unchanged_hint;}

    // query is support WB
    bool isSupportWb() const;
    bool isSupportWbParams(int dump_point,
                           unsigned int src_w, unsigned int src_h,
                           unsigned int dst_w, unsigned int dst_h) const;

    void onRefresh(unsigned int type);

    void decRefreshChanged()
    {
        int current = m_refresh_changed.load();
        if (current > 0)
            m_refresh_changed.compare_exchange_weak(current, current - 1);
    }

    int getRefreshChanged() const
    {
        return m_refresh_changed.load();
    }

    void onOtherHotplug();

    void decOtherHotplugChanged()
    {
        int current = m_other_hotplug_changed.load();
        if (current > 0)
            m_other_hotplug_changed.compare_exchange_weak(current, current - 1);
    }

    int getOtherHotplugChanged() const
    {
        return m_other_hotplug_changed.load();
    }

    bool getGpucSkipValidate() const
    {
        return m_gpuc_skip_validate;
    }

    void setGpucSkipValidate(bool gpuc_skip_validate)
    {
        m_gpuc_skip_validate = gpuc_skip_validate;
    }

    void updateDisplayInfo(const CreateDisplayInfo& create_disp_info)
    {
        m_type = create_disp_info.disp_type;
        m_is_internal = create_disp_info.is_internal;
        m_drm_id_cur_crtc = create_disp_info.drm_id_crtc_default;
        m_drm_id_connector = create_disp_info.drm_id_connector;
    }

    void updatePlugState(uint32_t type, CONNECTION_STATE state);
    CONNECTION_STATE getPlugState(uint32_t type);
    uint32_t detectConnectorTypeByPlugState(CONNECTION_STATE state);

private:
    bool needDoAvGrouping(const unsigned int num_plugin_display);
    void updateFps();
    void updateLayerPrevInfo();
    void offloadMMtoClient();

    void setLastAppGamePQ(const bool& on) { m_last_app_game_pq = on; }
    bool getLastAppGamePQ() const { return m_last_app_game_pq; }

    class Config {
        public:
            Config()
                : mId(0)
                , mSupport(false)
                , mDriverModeId(INVALID_DRIVER_MODE_ID)
                , mRefresh(0)
                , mAttributes()
        {}

            void setId(hwc2_config_t id) { mId = id; }
            hwc2_config_t getId() const { return mId; }
            void setSupport(bool support) { mSupport = support; }
            bool getSupport() const { return mSupport; }
            void setDriverModeId(uint32_t id) { mDriverModeId = id; }
            uint32_t getDriverModeId() const { return mDriverModeId; }
            void setRefresh(int32_t refresh) { mRefresh = refresh; }
            int32_t getRefresh() { return mRefresh; }
            void setAttribute(int32_t attribute, int32_t value)
            {
                mAttributes.emplace(attribute, value);
            }
            int32_t getAttribute(int32_t attribute) const
            {
                if (mAttributes.count(attribute) == 0) return -1;
                return mAttributes.at(attribute);
            }

        private:
            hwc2_config_t mId;
            bool mSupport;
            uint32_t mDriverModeId;
            int32_t mRefresh;
            std::unordered_map<int32_t, int32_t> mAttributes;
    };

    void updatePqModeId(bool force_update = false);

    // set the present fence for switch display config
    void fillSwitchConfigListWithPresentFence(unsigned int index, int fd);

    // generate the index for SwitchConfigInfo
    int64_t generateSwitchConfigSequence();

    // get the next pending active config. we use it to select a config which is monitored by
    // SwitchConfigMonitor
    bool getNextPendingActiveConfig(SwitchConfigInfo* info, nsecs_t* deadline);

    // if this frame has video or camera buffer, this function extends surfaceflinger target time.
    // then mdp and mml can get more time to process buffer.
    void extendSfTargetTime(DispatcherJob* job);

    void initDisplayDump(const sp<IOverlayDevice>& ovl);

    void setConfigChanged(const uint32_t& changed) { m_config_changed |= changed; }
    void clearConfigChanged() { m_config_changed = 0; }

    int32_t m_type;
    bool m_is_internal;
    uint32_t m_drm_id_cur_crtc;
    uint32_t m_drm_id_connector;
    uint32_t m_drm_id_crtc_pipe;

    enum DYNAMIC_OUTPUT
    {
        DYNAMIC_OUTPUT_HDMI = 0,
        DYNAMIC_OUTPUT_DP,
        DYNAMIC_OUTPUT_EDP,
        DYNAMIC_OUTPUT_MAX,
    };

    struct DynamicOutput
    {
        uint32_t connector_type;
        CONNECTION_STATE plug_state;
    };

    DynamicOutput m_dynamic_output[DYNAMIC_OUTPUT_MAX];

    DbgLogger m_set_buf_from_sf_log;
    DbgLogger m_set_comp_from_sf_log;

    bool m_prev_is_support_disp_pq;

    sp<HWCBuffer> m_outbuf;

    bool m_is_validated;
    std::vector<sp<HWCLayer> > m_changed_comp_types;

    uint64_t m_disp_id;
    hwc2_config_t m_active_config;
    hwc2_config_t m_sf_active_config;
    uint32_t m_config_changed;

    // this variable is for setActiveConfigWithConstraints
    std::mutex m_config_lock;
    // store the display config from SurfaceFlinger. we will remove the display config from this
    // list when the display ready to set to dispatcher thread
    std::priority_queue<SwitchConfigInfo, std::vector<SwitchConfigInfo>,
            std::greater<std::vector<SwitchConfigInfo>::value_type>> m_switch_config_list;
    // when the display config is ready to apply, we store them in this list. we will remove the
    // display config when driver is using it
    std::list<SwitchConfigInfo> m_applied_config_list;
    bool m_switch_config;
    // the vsync period which the display is using
    nsecs_t m_applied_period;
    // the vsync period which the last config is set to dispatcher thread
    nsecs_t m_active_period;
    std::atomic<int64_t> m_switch_config_counter{0};
    SwitchConfigInfo m_waited_config_info;

    std::map<uint64_t, sp<HWCLayer> > m_layers;
    mutable Mutex m_pending_removed_layers_mutex;
    mutable Mutex m_dump_lock;
    std::set<uint64_t> m_pending_removed_layers_id;
    std::vector<sp<HWCLayer> > m_visible_layers;
    std::vector<sp<HWCLayer> > m_invisible_layers;
    std::vector<sp<HWCLayer> > m_committed_layers;

    mutable Mutex m_update_config_mutex;

    // client target
    sp<HWCLayer> m_ct;

    int32_t m_gles_head;
    int32_t m_gles_tail;
    int32_t m_retire_fence_fd;
    int32_t m_mir_src;
    std::vector<LayerIdAndCompType> m_prev_comp_types;
    std::vector<int32_t> m_SF_comp_before_valid;
    int32_t m_power_mode;
    int32_t m_color_transform_hint;
    int32_t m_color_mode;
    int32_t m_prev_color_mode;
    int32_t m_render_intent;
    int32_t m_prev_render_intent;
    uint32_t m_hdr_type;
    uint32_t m_prev_hdr_type;
    int32_t m_presented_pq_mode_id;
    std::atomic<int32_t> m_pq_mode_id;

    bool m_need_av_grouping;
    bool m_use_gpu_composition;

    std::vector<sp<HWCLayer> > m_last_committed_layers;
    bool m_color_transform_ok;
    sp<ColorTransform> m_color_transform;
    CCORR_STATE m_ccorr_state;
    unsigned int m_prev_available_input_layer_num;

    HWC_VALI_PRESENT_STATE m_vali_present_state;
    bool m_is_visible_layer_changed;

    FpsCounter mFpsCounter;

    // display ID map to mirror count
    // TODO: m_unpresent_count should only be used when single validate,
    //       it can be removed after single validate is phase out.
    int m_unpresent_count;
    int m_prev_unpresent_count;
#ifdef MTK_IN_DISPLAY_FINGERPRINT
    // for LED HBM (High Backlight Mode) control
    bool m_is_HBM;
#endif

    unsigned int m_client_clear_layer_num;
    std::vector<sp<HWCLayer> > m_changed_hwc_requests;

    // mark having job for this run, this will hint whether
    // to clear layer state change and buffer change
    int m_dispathcer_job_status;

    bool m_last_app_game_pq;

    bool m_is_m4u_sec_inited;

    bool m_msync_2_0_enable_changed = false;
    int m_msync_2_0_enable = false;
    std::shared_ptr<MSync2Data::ParamTable> m_msync_2_0_param_table;

    std::shared_ptr<ColorHistogram> m_histogram;

    // the mapping table for PQ XML's mode id
    std::map<int32_t, std::map<int32_t, std::vector<IntentInfo>>> m_color_mode_with_render_intent;

    nsecs_t m_sf_target_ts = -1;
    nsecs_t m_prev_sf_target_ts = -1;   // check sf set ts every present
    std::string m_perf_ovl_atomic_work_time_str;

    bool m_need_free_fb_cache = false;

    // when we want to update the state of cpu set, set this flag to true
    bool m_dirty_cpu_set = false;
    // store the last ScenarioHint from other module
    ComposerExt::ScenarioHint m_scenario_hint = ComposerExt::ScenarioHint::kGeneral;
    // store the last cpu set after clear cpu set dirty
    unsigned int m_cpu_set = HWC_CPUSET_NONE;

    int m_prev_composition_mode = MTK_COMPOSITION_MODE_INIT;

    // the string of trace for extening surfaceflinger target time
    std::string m_perf_extend_sf_target_time_str;
    // store the number of visable video layer
    unsigned int m_num_video_layer;
    // store the number of visable camera layer
    unsigned int m_num_camera_layer;
    // store the previous extending sf target time
    nsecs_t m_prev_extend_sf_target_time;

    std::shared_ptr<DisplayDump> m_disp_dump;

    // store the layer id of present index layer
    hwc2_layer_t m_present_idx_layer_id = 0;

    // WDMA lock, to protect m_wdma_hrt_target
    std::mutex m_wdma_lock;

    // WDMA status, record how many WDMA feature enabled
    uint32_t m_wdma_status = HWC_WDMA_STATUS_DISABLE;
    // WDMA hrt target, which is WDMA user who costs more
    uint32_t m_wdma_hrt_target = HWC_WDMA_STATUS_DISABLE;
    // WDMA cost is used to compare when new WDMA feature enabled
    uint32_t m_wdma_hrt_cost = 0;

    // WDMA dump ROI
    Rect m_wdma_src_roi;

    // WDMA dst width & height
    uint32_t m_wdma_dst_width;
    uint32_t m_wdma_dst_height;

    // WDMA dump format
    uint32_t m_wdma_format;

    // WDMA dump point
    int m_wdma_dump_point;

    //current m_visible_layers may contains multi inactive layerset
    std::vector<LayerSet> m_layer_sets;
    //pick the best inactive layerset of m_layer_sets
    LayerSet m_current_set;
    //if the fbt buf is unchanged
    bool m_fbt_unchanged_hint;

    std::atomic_int m_refresh_changed;

    std::atomic_int m_other_hotplug_changed;

    // for gpu Cache skip validate
    bool m_gpuc_skip_validate;

    std::vector<std::shared_ptr<Config>> m_configs;
};

#endif
