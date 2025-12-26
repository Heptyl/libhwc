#ifndef HWC_GLAI_CONTROLLER_H_
#define HWC_GLAI_CONTROLLER_H_

#include <hardware/hwcomposer_defs.h>

// ---------------------------------------------------------------------------

namespace android
{
class String8;
}

class GlaiController
{
public:
    struct InferenceParam
    {
        int agent_id;
        int in_fd;
        size_t in_size;
        int in_acquire_fence;

        int out_fd;
        size_t out_size;
        int out_release_fence;

        int* inference_done_fence;
        buffer_handle_t buffer_handle;
    };

    enum
    {
        VAL_FAIL = 0,
        VAL_OK = 1 << 0,
        VAL_MODEL_LOADED = 1 << 1,
    };

    struct Model
    {
        // true when first isGlaiLayerValid, false when layer destroyed
        bool valid = false;
        // this is not related to resource management
        // there should have no actual life cycle control in here
        // model is created by EGL, and lives in NeuroPilot

        int agent_id = -1;

        unsigned int in_format = 0;
        unsigned int out_format = 0;
        bool in_compress = false;
        bool out_compress = false;
        uint32_t in_w = 0;
        uint32_t in_h = 0;
        uint32_t out_w = 0;
        uint32_t out_h = 0;
        uint32_t in_stride = 0;
        uint32_t out_stride = 0;
    };

public:
    static GlaiController& getInstance();
    int isGlaiLayerValid(int& agent_id,
                         const buffer_handle_t& handle,
                         const unsigned int w,
                         const unsigned int h,
                         const unsigned int format,
                         const unsigned int y_stride,
                         hwc_rect_t& out_dst_roi,
                         unsigned int& out_fmt);

    void dump(android::String8* dump_str) const;

    const Model* getModel(const int agent_id) const;
    int cleanModel(const int agent_id);

    int inference(InferenceParam& param);

    void setInferenceWoFence(bool in) { m_inference_wo_fence = in; }

protected:
    GlaiController();

    int loadModel(int& agent_id, const buffer_handle_t& handle);

protected:
    Model m_model;

    bool m_inference_wo_fence = false;
};

#endif // HWC_GLAI_CONTROLLER_H_
