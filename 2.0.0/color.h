#ifndef HWC_COLOR_H_
#define HWC_COLOR_H_

// In this file, including any file under libhwcompsoer folder is prohibited!!!
#include <vector>
#include <utils/String8.h>
#include <system/graphics.h>
#include <utils/RefBase.h>
#include <utils/LightRefBase.h>

#include "pq_interface.h"

using namespace android;
struct ColorTransform : public android::LightRefBase<ColorTransform>
{
    ColorTransform(const float* in_matrix, const int32_t& in_hint, const bool& in_dirty);

    ColorTransform(const std::vector<std::vector<float> >& in_matrix, const int32_t& in_hint,
                   const bool& in_dirty, const bool& in_force_disable_color);

    ColorTransform(const int32_t& in_hint, const bool& in_dirty, const bool& in_force_disable_color);

    void dump(String8* dump_str);

    std::vector<std::vector<float> > matrix;

    bool isIdentity() const;

    int32_t hint;

    bool dirty;

    bool force_disable_color;
};

const unsigned int COLOR_MATRIX_DIM = 4;

enum CCORR_STATE {
    NOT_FBT_ONLY,
    FIRST_FBT_ONLY,
    STILL_FBT_ONLY
};

inline int32_t transFloatToIntForCCORR(const float& val)
{
    static int identity_val = getPqDevice()->getCcorrIdentityValue();
    return static_cast<int32_t>(val * identity_val + 0.5f);
}
#endif // HWC_COLOR_H_
