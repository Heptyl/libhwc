#ifndef HWC_GLAI_HANDLER_H_
#define HWC_GLAI_HANDLER_H_

#include <utils/threads.h>

#include "composer.h"
#include "dispatcher.h"
#include "queue.h"

struct HWLayer;
struct PrivateHandle;
class DisplayBufferQueue;
class HWCDisplay;
// ---------------------------------------------------------------------------

class GlaiHandler : public LayerHandler
{
public:
    GlaiHandler(uint64_t dpy, const sp<OverlayEngine>& ovl_engine);
    virtual ~GlaiHandler();

    // set() is used to create release fence
    // in order to notify upper producers that layers are already consumed
    virtual void set(const sp<HWCDisplay>& display, DispatcherJob* job);

    // process() is used to utilize DpFramework
    // to rescale and rotate input layers and pass the results to display driver
    virtual void process(DispatcherJob* job);

    // dump() is used to dump debug data
    virtual int dump(char* buff, int buff_len, int dump_level);

    // is used to cancel glai layers of job
    virtual void cancelLayers(DispatcherJob* job);

private:
    int prepareOverlayPortParam(unsigned int ovl_id,
                                DispatcherJob* job,
                                sp<DisplayBufferQueue> queue,
                                OverlayPortParam* ovl_port_param,
                                bool new_buf);
    int setOverlayPortParam(unsigned int ovl_id, const OverlayPortParam& ovl_port_param);

    bool doGlai(HWLayer* hw_layer);

    sp<DisplayBufferQueue> getDisplayBufferQueue(HWLayer *hw_layer = nullptr);

    void configDisplayBufferQueue(sp<DisplayBufferQueue> queue,
                                  const HWLayer* hw_layer) const;

    // remove PrevLayerInfo if that layer not exist anymore
    void cleanPrevLayerInfo(const std::vector<sp<HWCLayer> >* hwc_layers = nullptr);
    void savePrevLayerInfo(const sp<HWCLayer>& hwc_layer);
    int getPrevLayerInfoFence(const sp<HWCLayer>& hwc_layer);

    struct PrevLayerInfo {
        uint64_t id;
        int job_done_fd;
    };

    std::list<PrevLayerInfo> m_prev_layer_info;
};

#endif // HWC_GLAI_HANDLER_H_