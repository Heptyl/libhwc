#define DEBUG_LOG_TAG "GLAI_CTRL"

#include "glai_controller.h"

#include "utils/debug.h"

#include <NpAgentShim.h>

#include <utils/String8.h>

#include <sstream>
#include <vector>

#ifdef USE_SWWATCHDOG
#include "utils/swwatchdog.h"
#endif

using std::endl;

GlaiController& GlaiController::getInstance()
{
    static GlaiController gInstance;
    return gInstance;
}

GlaiController::GlaiController()
{
}

int GlaiController::loadModel(int& agent_id, const buffer_handle_t& handle)
{
    HWC_ATRACE_CALL();
#ifdef USE_SWWATCHDOG
    SWWatchDog::AutoWDT _wdt("[GLAI_CTRL] loadModel", 500);
#endif

    HWC_LOGI("%s(), agent_id %d", __FUNCTION__, agent_id);

    m_model.valid = false;

    m_model.agent_id = NpAgent_gpuCreate(handle);
    if (m_model.agent_id <= 0)
    {
        HWC_LOGE("NpAgent_gpuCreate fail, ret %d", m_model.agent_id);
        return -EINVAL;
    }

    agent_id = m_model.agent_id;

    NpAgentAttributes* attributes = nullptr;
    int ret = 0;
    ret = NpAgentAttributes_create(m_model.agent_id, &attributes);
    if (ret != RESULT_NO_ERROR) {
        return ret;
    }

    ret = NpAgentAttributes_getInputFormat(attributes, &m_model.in_format);
    if (ret != RESULT_NO_ERROR)
    {
        HWC_LOGE("NpAgentAttributes_getInputFormat, agent_id %d, ret %d", agent_id, ret);
        NpAgentAttributes_release(attributes);
        return ret;
    }

    ret = NpAgentAttributes_getOutputFormat(attributes, &m_model.out_format);
    if (ret != RESULT_NO_ERROR)
    {
        HWC_LOGE("NpAgentAttributes_getOutputFormat, agent_id %d, ret %d", agent_id, ret);
        NpAgentAttributes_release(attributes);
        return ret;
    }

    uint32_t value = 0;
    ret = NpAgentAttributes_getInputCompressionMode(attributes, &value);
    if (ret != RESULT_NO_ERROR)
    {
        HWC_LOGE("NpAgentAttributes_getInputCompressionMode, agent_id %d, ret %d", agent_id, ret);
        NpAgentAttributes_release(attributes);
        return ret;
    }
    m_model.in_compress = value != COMPRESSION_NONE;

    ret = NpAgentAttributes_getOutputCompressionMode(attributes, &value);
    if (ret != RESULT_NO_ERROR)
    {
        HWC_LOGE("NpAgentAttributes_getOutputCompressionMode, agent_id %d, ret %d", agent_id, ret);
        NpAgentAttributes_release(attributes);
        return ret;
    }
    m_model.out_compress = value != COMPRESSION_NONE;

    ret = NpAgentAttributes_getInputHeightWidth(attributes, &m_model.in_h, &m_model.in_w);
    if (ret != RESULT_NO_ERROR)
    {
        HWC_LOGE("NpAgentAttributes_getInputHeightWidth, agent_id %d, ret %d", agent_id, ret);
        NpAgentAttributes_release(attributes);
        return ret;
    }

    ret = NpAgentAttributes_getOutputHeightWidth(attributes, &m_model.out_h, &m_model.out_w);
    if (ret != RESULT_NO_ERROR)
    {
        HWC_LOGE("NpAgentAttributes_getOutputHeightWidth, agent_id %d, ret %d", agent_id, ret);
        NpAgentAttributes_release(attributes);
        return ret;
    }

    ret = NpAgentAttributes_getInputStride(attributes, &m_model.in_stride);
    if (ret != RESULT_NO_ERROR)
    {
        HWC_LOGE("NpAgentAttributes_getInputStride, agent_id %d, ret %d", agent_id, ret);
        NpAgentAttributes_release(attributes);
        return ret;
    }

    ret = NpAgentAttributes_getOutputStride(attributes, &m_model.out_stride);
    if (ret != RESULT_NO_ERROR)
    {
        HWC_LOGE("NpAgentAttributes_getOutputStride, agent_id %d, ret %d", agent_id, ret);
        NpAgentAttributes_release(attributes);
        return ret;
    }

    NpAgentAttributes_release(attributes);

    m_model.valid = true;
    dump(nullptr);
    return 0;
}

const GlaiController::Model* GlaiController::getModel(const int agent_id) const
{
    if (m_model.valid)
    {
        if (m_model.agent_id != agent_id)
        {
            HWC_LOGW("%s(), with wrong id %d", __FUNCTION__, agent_id);
            dump(nullptr);
            return nullptr;
        }

        return &m_model;
    }
    else
    {
        HWC_LOGW("%s(), id %d while model not valid", __FUNCTION__, agent_id);
        return nullptr;
    }
}

int GlaiController::cleanModel(const int agent_id)
{
    HWC_ATRACE_CALL();
    #ifdef USE_SWWATCHDOG
        SWWatchDog::AutoWDT _wdt("[GLAI_CTRL] cleanModel", 500);
    #endif

    HWC_LOGI("%s(), agent_id %d", __FUNCTION__, agent_id);

    if (m_model.valid)
    {
        if (m_model.agent_id != agent_id)
        {
            HWC_LOGW("%s(), with wrong id %d", __FUNCTION__, agent_id);
            dump(nullptr);
            return -EINVAL;
        }

        NpAgent_release(agent_id);
        m_model.valid = false;
    }
    else
    {
        HWC_LOGW("%s(), id %d while model not valid", __FUNCTION__, agent_id);
        return -EINVAL;
    }
    return 0;
}

int GlaiController::isGlaiLayerValid(int& agent_id,
                                     const buffer_handle_t& handle,
                                     const unsigned int w,
                                     const unsigned int h,
                                     const unsigned int format,
                                     const unsigned int y_stride,
                                     hwc_rect_t& out_dst_roi,
                                     unsigned int& out_fmt)
{
    HWC_ATRACE_CALL();
    int val_result = VAL_FAIL;

    if (m_model.valid)
    {
        if (m_model.agent_id != agent_id)
        {
            HWC_LOGE("unexpected two agent_id at the same time");
            dump(nullptr);
            return val_result;
        }
    }
    else
    {
        if (agent_id > 0)
        {
            HWC_LOGE("agent_id %d already > 0? resoruce leak?", agent_id)
        }

        int ret = loadModel(agent_id, handle);
        if (ret)
        {
            HWC_LOGE("loadModel fail");
            return val_result;
        }
        val_result |= VAL_MODEL_LOADED;
    }

#ifdef USE_SWWATCHDOG
    SWWatchDog::AutoWDT _wdt("[GLAI_CTRL] validate", 500);
#endif
    int ret;
    ret = NpAgent_validateInput(agent_id, format, h, w, y_stride);
    if (ret != RESULT_NO_ERROR)
    {
        // glai assign agent id and it's buffer should not compare fail
        HWC_LOGW("val in fail, agent_id %d, ret %d, fmt %u, h %u, w %u, stride %u",
                 agent_id, ret,
                 format, h, w, y_stride);
        return val_result;
    }

    out_dst_roi.left = 0;
    out_dst_roi.top = 0;
    out_dst_roi.right = static_cast<int>(m_model.out_w);
    out_dst_roi.bottom = static_cast<int>(m_model.out_h);
    out_fmt = m_model.out_format;
    return val_result | VAL_OK;
}

void GlaiController::dump(String8* dump_str) const
{
    if (!m_model.valid)
    {
        if (dump_str)
        {
            dump_str->appendFormat("GlaiController: model not valid");
        }
        return;
    }

    std::ostringstream ss;
    ss << "GlaiController:" << endl;
    ss << "id: " << m_model.agent_id << endl;
    ss << "format: in " << m_model.in_format << " out " << m_model.out_format << endl;
    ss << "compress: in " << m_model.in_compress << " out " << m_model.out_compress << endl;
    ss << "in: w " << m_model.in_w << " h " << m_model.in_h << " stride " << m_model.in_stride << endl;
    ss << "out: w " << m_model.out_w << " h " << m_model.out_h << " stride " << m_model.out_stride << endl;
    ss << endl;

    if (dump_str)
    {
        dump_str->append(ss.str().c_str());
    }
    else
    {
        HWC_LOGI("%s", ss.str().c_str());
    }
}

int GlaiController::inference(InferenceParam& param)
{
    HWC_ATRACE_CALL();
#ifdef USE_SWWATCHDOG
    SWWatchDog::AutoWDT _wdt("[GLAI_CTRL] inference", 500);
#endif

    if (!param.inference_done_fence)
    {
        HWC_LOGE("need assign inference_done_fence");
        return -EINVAL;
    }

    int ret = 0;
    NpAgentExecution* execution = nullptr;
    ret = NpAgentExecution_create(&execution);
    if (ret != RESULT_NO_ERROR)
    {
        HWC_LOGE("NpAgentExecution_create fail");
        return ret;
    }

    ret = NpAgentExecution_setInput(execution, param.in_fd, param.in_size);
    if (ret != RESULT_NO_ERROR)
    {
        HWC_LOGE("NpAgentExecution_setInput fail, fd %d, size %zu", param.in_fd, param.in_size);
        NpAgentExecution_release(execution);
        return ret;
    }

    ret = NpAgentExecution_setOutput(execution, param.out_fd, param.out_size);
    if (ret != RESULT_NO_ERROR)
    {
        HWC_LOGE("NpAgentExecution_setOutput fail, fd %d, size %zu", param.out_fd, param.out_size);
        NpAgentExecution_release(execution);
        return ret;
    }

    NpAgent_gpuUpdate(param.agent_id, param.buffer_handle, OPTION_BOOST_VALUE);

    if (CC_UNLIKELY(m_inference_wo_fence))
    {
        ret = NpAgent_compute(param.agent_id, execution);
    }
    else
    {
        ret = NpAgent_computeWithFence(param.agent_id,
                                       execution,
                                       param.in_acquire_fence,
                                       param.out_release_fence,
                                       param.inference_done_fence,
                                       0);   // duration not working, expect set next vsync + period to npagent, not hwc decide duration*/
    }

    if (ret != RESULT_NO_ERROR)
    {
        HWC_LOGE("NpAgent_computeWithFence fail, agent_id %d, execution %p, in acq fence %d, inference fence %d",
                 param.agent_id,
                 execution,
                 param.in_acquire_fence,
                 *param.inference_done_fence);
        *param.inference_done_fence = -1;
        NpAgentExecution_release(execution);
        return ret;
    }

    NpAgentExecution_release(execution);
    return 0;
}

