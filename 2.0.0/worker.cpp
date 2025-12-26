#define DEBUG_LOG_TAG "WKR"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include "utils/debug.h"
#include "dispatcher.h"
#include "overlay.h"
#include "worker.h"
#include "display.h"
#include "composer.h"
#include "bliter_async.h"
#include "sync.h"
#include "platform_wrap.h"
#include "glai_handler.h"

// ---------------------------------------------------------------------------

status_t HWCThread::readyToRun()
{
    m_state = HWC_THREAD_IDLE;

    return NO_ERROR;
}

void HWCThread::waitLocked()
{
    // make sure m_state is not out of expect
    assert((m_state == HWC_THREAD_TRIGGER) || (m_state == HWC_THREAD_IDLE));

    int timeout_count = 0;
    while (m_state == HWC_THREAD_TRIGGER)
    {

        // WORKAROUND: After HWCThread know which display uses itself,
        // it can set a more reasonable timeout value
        if (m_condition.waitRelative(m_lock, ms2ns(50)) == TIMED_OUT)
        {
            int sem_value;
            sem_getvalue(&m_event, &sem_value);

            if (20 == timeout_count)
            {
                HWC_LOGE("Timed out waiting for %s (cnt=%d/val=%d)",
                         m_thread_name.c_str(), timeout_count, sem_value);
            }

            if (timeout_count & 0x1)
            {
                HWC_LOGW("Timed out waiting for %s (cnt=%d/val=%d)",
                         m_thread_name.c_str(), timeout_count, sem_value);
            }

            timeout_count++;
        }
    }
}

void HWCThread::wait()
{
    HWC_ATRACE_CALL();
    AutoMutex l(m_lock);
    HWC_LOGD("Waiting for %s...", m_thread_name.c_str());
    waitLocked();
}

void HWCThread::stop()
{
    HWC_ATRACE_CALL();
    requestExit();
    {
        // trigger threadLoop break latest sem_wait,
        // or following join() will be blocked by sem_wait forever.
        AutoMutex l(m_lock);
        sem_post(&m_event);
        if (Platform::getInstance().m_config.dbg_switch & HWC_DBG_SWITCH_DEBUG_SEMAPHORE)
        {
            int32_t sem_value;
            sem_getvalue(&m_event, &sem_value);
            HWC_ATRACE_INT(m_semaphore_name.c_str(), sem_value);
        }
    }
    join();
    {
        // set to idle state, avoid wait to a thread that exit with trigger state
        AutoMutex l(m_lock);
        m_state = HWC_THREAD_IDLE;
        m_condition.signal();
    }
}

// ---------------------------------------------------------------------------

LayerComposer::LayerComposer(uint64_t dpy, const sp<OverlayEngine>& ovl_engine)
    : m_disp_id(dpy)
{
    m_mm_handler = new AsyncBliterHandler(m_disp_id, ovl_engine);
    m_ui_handler = new ComposerHandler(m_disp_id, ovl_engine);

    if (m_mm_handler == NULL || m_ui_handler == NULL)
    {
        HWC_LOGE("NULL LayerComposer handler");
    }

    if (HwcFeatureList::getInstance().getFeature().has_glai)
    {
        m_glai_handler = new GlaiHandler(m_disp_id, ovl_engine);
        if (m_glai_handler == NULL)
        {
            HWC_LOGE("m_glai_handler == NULL");
        }
    }
}

LayerComposer::~LayerComposer()
{
    m_mm_handler = NULL;
    m_ui_handler = NULL;
    m_glai_handler = NULL;
}

void LayerComposer::set(
    const sp<HWCDisplay>& display,
    DispatcherJob* job)
{
    if (m_mm_handler == NULL || m_ui_handler == NULL)
    {
        HWC_LOGE("NULL LayerComposer handler");
        return;
    }

    m_mm_handler->set(display, job);
    m_ui_handler->set(display, job);

    if (m_glai_handler)
    {
        m_glai_handler->set(display, job);
    }
    else if (job->num_glai_layers != 0)
    {
        HWC_LOGE("m_glai_handler is NULL");
    }
}

void LayerComposer::trigger(DispatcherJob* job)
{
    if (m_mm_handler == NULL || m_ui_handler == NULL)
    {
        HWC_LOGE("NULL LayerComposer handler");
        return;
    }

    if (job == NULL)
        return;

    if (0 == job->num_mm_layers && !job->is_black_job)
        m_mm_handler->nullop();
    else if (!isNoDispatchThread())
        m_mm_handler->process(job);

    if (!isNoDispatchThread())
    {
        m_ui_handler->process(job);

        if (m_glai_handler)
        {
            m_glai_handler->process(job);
        }
        else if (job->num_glai_layers != 0)
        {
            HWC_LOGE("m_glai_handler == null");
        }
    }

    job = NULL;
}

void LayerComposer::cancelLayers(DispatcherJob* job)
{
    if (m_mm_handler == NULL || m_ui_handler == NULL)
    {
        HWC_LOGE("NULL LayerComposer handler");
        return;
    }

    m_mm_handler->cancelLayers(job);
    m_ui_handler->cancelLayers(job);

    if (m_glai_handler)
    {
        m_glai_handler->cancelLayers(job);
    }
    else if (job->num_glai_layers != 0)
    {
        HWC_LOGE("m_glai_handler == null");
    }
}

void LayerComposer::nullop()
{
    if (m_mm_handler == NULL)
    {
        HWC_LOGE("NULL LayerComposer handler");
        return;
    }

    m_mm_handler->nullop();
}
