#ifndef HWC_BLITER_ASYNC_H_
#define HWC_BLITER_ASYNC_H_
#ifdef HWC_PQ_DEMO
#include "DpAsyncBlitStream.h"
#endif
#include "DpAsyncBlitStream2.h"

#include <deque>

#include <utils/threads.h>

#include "composer.h"
#include "dispatcher.h"
#include "hwc_ui/Rect.h"
#include "queue.h"

using namespace android;
using hwc::Rect;

struct HWLayer;
struct PrivateHandle;
class DisplayBufferQueue;
class BliterNode;
struct BufferConfig;

// ---------------------------------------------------------------------------

class AsyncBliterHandler : public LayerHandler
{
public:
    AsyncBliterHandler(uint64_t dpy, const sp<OverlayEngine>& ovl_engine);
    virtual ~AsyncBliterHandler();

    // set() in AsyncBliterHandler is used to create release fence
    // in order to notify upper producers that layers are already consumed
    virtual void set(const sp<HWCDisplay>& display, DispatcherJob* job);

    // process() in AsyncBliterHandler is used to utilize DpFramework
    // to rescale and rotate input layers and pass the results to display driver
    virtual void process(DispatcherJob* job);

    // release fence by specified MDP job ID
    // This is useful when a mirror job is dropped, and
    // the blit processing needs to be skipped.
    // If the job_id is 0, it will release all fence of MDP job_id
    //
    // NOTE: Although AsyncBliterHandler is inherited from LayerHandler
    // LayerHandler does NOT have this function
    void nullop();
    void nullop(const uint32_t& job_id);

    // dump() in AsyncBliterHandler is used to dump debug data
    virtual int dump(char* buff, int buff_len, int dump_level);

    // cancelLayers in AsyncBliterHandler is used to cancel mm layers of job
    virtual void cancelLayers(DispatcherJob* job);

private:
    int prepareOverlayPortParam(unsigned int ovl_id,
                                DispatcherJob* job,
                                sp<DisplayBufferQueue> queue,
                                OverlayPortParam* ovl_port_param,
                                bool new_buf,
                                int32_t hw_layer_caps = 0);
    int setOverlayPortParam(unsigned int ovl_id, const OverlayPortParam& ovl_port_param);

    // setMirror() is used to create release fence for mirror source buffer
    // in order to notify previous engine know if it is consumed
    void setMirror(const sp<HWCDisplay>& display, DispatcherJob* job);

    // setBlack() is used to create mdp job for fill black content
    void setBlack(const sp<HWCDisplay>& display, DispatcherJob* job);

    void setBlackToWorker(const DispatcherJob* job);
    void setInvalidateToWorker(const DispatcherJob* job, uint32_t ovl_id);

    // cancelMirror() is used to cancel mirror dst buffer of dropped job
    void cancelMirror(DispatcherJob* job);

    void calculateMirRoiXform(uint32_t* xform, Rect* src_roi, Rect* dst_roi, DispatcherJob* job);

    void setPhyMirror(DispatcherJob* job);
    // processPhyMirror() is used to utilize DpFramework
    // to rescale and rotate mirror source buffer
    // in order for post engine to do composition
    void processPhyMirror(DispatcherJob* job);

    // processPhyMirror() is used to utilize DpFramework
    // to rescale and rotate mirror source buffer
    // in order for codec to video encoding
    void processVirMirror(DispatcherJob* job);

    // processVirBlack() is used to utilize DpFramework
    // to fill black content into virtual output buffer
    void processVirBlack(DispatcherJob* job);

    // bypassBlit() is used to check if source buffer is dirty, or
    // there has any updated source buffer could be used
    bool bypassBlit(HWLayer* hw_layer, uint32_t ovl_in);
    bool doBlit(HWLayer* hw_layer);

    sp<DisplayBufferQueue> getDisplayBufferQueue(HWLayer *hw_layer = nullptr);

    // configDisplayBufferQueue() is used to config display buffer queue
    void configDisplayBufferQueue(sp<DisplayBufferQueue> queue, PrivateHandle* priv_handle,
        const DisplayData* disp_data, const uint32_t& assigned_output_format,
        const bool& assigned_compression = false) const;

    // setDpConfig() is used to prepare configuration for DpFramwork
    status_t setDpConfig(PrivateHandle* src_priv_handle, const DisplayBufferQueue::DisplayBuffer* disp_buf,
        int32_t src_dataspace, int32_t dst_dataspace,
        BufferConfig* config, uint32_t ovl_in);

    // setDstDpConfig() is used to prepare destination configuration for DpFramwork
    status_t setDstDpConfig(PrivateHandle& dst_priv_handle, BufferConfig* config);

    // check the MM layer is MML layer or not
    bool isMMLLayer(const HWLayer* layer) const;

    // processFillBlack() is used to clear destination buffer by scaling a small black buffer
    void processFillBlack(PrivateHandle* priv_handle, int* fence, MdpJob &job, bool use_white = false);

    // is used to check the orientation and clear buffer if needed
    void clearBackground(buffer_handle_t handle,
                         const Rect* current_dst_roi,
                         int* fence,
                         MdpJob& job);

    // clearMdpJob is used to close unused fence of fill black job
    void clearMdpJob(MdpJob& job);

    // cancelFillBlackJob is used to cancel unused fill black job
    void cancelFillBlackJob(MdpJob& job);

    // remove PrevLayerInfo if that layer not exist anymore
    void cleanPrevLayerInfo(const std::vector<sp<HWCLayer> >* hwc_layers = nullptr);
    void savePrevLayerInfo(const sp<HWCLayer>& hwc_layer);
    int getPrevLayerInfoFence(const sp<HWCLayer>& hwc_layer);

    // m_dp_configs stores src/dst buffer and overlay input information
    BufferConfig* m_dp_configs;

    BliterNode* m_bliter_node;

    sp<DisplayBufferQueue> m_mirror_queue;

    struct PrevLayerInfo {
        uint64_t id;
        int job_done_fd;
    };

    std::list<PrevLayerInfo> m_prev_layer_info;

    class Worker {
    public:
        Worker();
        ~Worker();

        void post(std::function<void()> runnable);

    private:
        void run();

        std::thread m_thread;
        bool m_exit = false;
        std::queue<std::function<void()>> m_run_queue;
        std::mutex m_mutex;
        std::condition_variable m_cv;
    };

    std::shared_ptr<Worker> m_worker = nullptr;
};

#endif // HWC_BLITER_ASYNC_H_
