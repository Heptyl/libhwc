#ifndef HWC_WORKER_H_
#define HWC_WORKER_H_

#include <utils/threads.h>

#include <semaphore.h>
#include <string>

using namespace android;

class DispatcherJob;
class LayerHandler;
class OverlayEngine;
class HWCDisplay;

// ---------------------------------------------------------------------------

// HWC_THREAD_STATE is used for presenting thread's status
enum HWC_THREAD_STATE
{
    HWC_THREAD_IDLE     = 0,
    HWC_THREAD_TRIGGER  = 1,
};

class HWCThread : public Thread
{
public:
    HWCThread() : m_state(HWC_THREAD_IDLE)
    {
        sem_init(&m_event, 0, 0);
    }

    // wait() is used to wait until thread is idle
    void wait();

    // stop is used to exit the thread.
    // NOTE: this function cannot be called by the thread itself,
    //       or it will cause dead lock by join function.
    void stop();

protected:
    mutable Mutex m_lock;
    Condition m_condition;
    sem_t m_event;

    HWC_THREAD_STATE m_state;
    std::string m_thread_name;
    std::string m_queue_name;
    std::string m_semaphore_name;

    void waitLocked();

private:
    // readyToRun() would be used to adjust thread priority
    virtual status_t readyToRun();
};

class LayerComposer : public virtual RefBase
{
public:
    LayerComposer(uint64_t dpy, const sp<OverlayEngine>& ovl_engine);
    virtual ~LayerComposer();

    // set() is used to config needed data for each layer
    // ex: release fence
    void set(const sp<HWCDisplay>& display, DispatcherJob* job);

    // trigger() is used to start processing layers
    void trigger(DispatcherJob* job);

    // cancelLayers is used to cancel layers of job
    void cancelLayers(DispatcherJob* job);

    virtual void nullop();

private:
    // m_disp_id is used to identify this thread is used by which display
    uint64_t m_disp_id;

    // m_handler is the main processing module to handle layers in ComposeThread
    sp<LayerHandler> m_mm_handler;
    sp<LayerHandler> m_ui_handler;

    sp<LayerHandler> m_glai_handler;
};
#endif // HWC_WORKER_H_
