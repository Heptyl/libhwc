#ifndef HWC_COMPOSER_H_
#define HWC_COMPOSER_H_

#include "dispatcher.h"

using namespace android;

struct HWLayer;
class DispatcherJob;
struct DisplayData;
class OverlayEngine;
class HWCDisplay;

// ---------------------------------------------------------------------------

class LayerHandler : public LightRefBase<LayerHandler>
{
public:
    LayerHandler(uint64_t dpy, const sp<OverlayEngine>& ovl_engine);
    virtual ~LayerHandler();

    // set() is used to set data for next round
    virtual void set(const sp<HWCDisplay>& display, DispatcherJob* job) = 0;

    // process() is used to ask composer to work
    virtual void process(DispatcherJob* job) = 0;

    // nullop() is used to clear state
    virtual void nullop() { }

    // release fence by specified marker
    virtual void nullop(const uint32_t& /*job_id*/) { }

    // release fence by specified marker
    virtual void nullop(int /*marker*/, int /*fd*/) { }

    // dump() is used for debugging purpose
    virtual int dump(char* /*buff*/, int /*buff_len*/, int /*dump_level*/) { return 0; }

    // cancelLayers() is used to cancel layers of dropped job
    virtual void cancelLayers(DispatcherJob* /*job*/) { }

protected:
    // m_disp_id is used to identify
    // LayerHandler needs to handle layers in which display
    uint64_t m_disp_id;

    // m_ovl_engine is used for LayerHandler to config overlay engine
    sp<OverlayEngine> m_ovl_engine;
};

class ComposerHandler : public LayerHandler
{
public:
    ComposerHandler(uint64_t dpy, const sp<OverlayEngine>& ovl_engine);

    // set() in ComposerHandler is used to get release fence from display driver
    virtual void set(const sp<HWCDisplay>& display, DispatcherJob* job);

    // process() in ComposerHandler is used to wait each layer is ready to use
    // and fill configuration to display driver
    virtual void process(DispatcherJob* job);

    // cancelLayers() in ComposerHandler is used to cancel each ui layers of job
    virtual void cancelLayers(DispatcherJob* job);

private:
    void setOverlayPortParam(DispatcherJob* job,
                             OverlayPortParam* ovl_port_param,
                             HWLayer* hw_layer);

};

#endif // HWC_COMPOSER_H_
