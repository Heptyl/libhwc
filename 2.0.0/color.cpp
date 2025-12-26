#define DEBUG_LOG_TAG "COL"
#include "color.h"
#include "utils/debug.h"
#include "pq_interface.h"

ColorTransform::ColorTransform(const float* in_matrix, const int32_t& in_hint, const bool& in_dirty)
    : hint(in_hint) , dirty(in_dirty), force_disable_color(false)
{
    matrix.resize(COLOR_MATRIX_DIM);
    for (auto& row : matrix)
    {
        row.resize(COLOR_MATRIX_DIM);
    }

    for (unsigned int i = 0; i < COLOR_MATRIX_DIM; ++i)
    {
        for (unsigned int j = 0; j < COLOR_MATRIX_DIM; ++j)
        {
            matrix[i][j] = in_matrix[i * COLOR_MATRIX_DIM + j];
        }
    }
}

ColorTransform::ColorTransform(
    const std::vector<std::vector<float> >& in_matrix,
    const int32_t& in_hint,
    const bool& in_dirty,
    const bool& in_force_disable_color)
    : matrix(in_matrix), hint(in_hint) , dirty(in_dirty), force_disable_color(in_force_disable_color)
{}

ColorTransform::ColorTransform(const int32_t& in_hint, const bool& in_dirty, const bool& in_force_disable_color)
    : hint(in_hint), dirty(in_dirty), force_disable_color(in_force_disable_color)
{
    switch (hint)
    {
        case HAL_COLOR_TRANSFORM_IDENTITY:
            matrix.resize(COLOR_MATRIX_DIM);
            for (auto& row : matrix)
            {
                row.resize(COLOR_MATRIX_DIM);
            }

            for (unsigned int i = 0; i < COLOR_MATRIX_DIM; ++i)
            {
                matrix[i][i] = 1.f;
            }
            break;

        default:
            HWC_LOGE("Constructor of ColorTransform does not accept other hints exclude identity");
    }
}

void ColorTransform::dump(String8* dump_str)
{
    dump_str->appendFormat("Color Matrix\n");
    for (unsigned int i = 0; i < COLOR_MATRIX_DIM; ++i)
    {
        dump_str->appendFormat("[ ");
        for (unsigned int j = 0; j < COLOR_MATRIX_DIM; ++j)
        {
            dump_str->appendFormat("%5f ", matrix[i][j]);
        }
        dump_str->appendFormat("]\n");
    }
}

bool ColorTransform::isIdentity() const
{
    return hint == HAL_COLOR_TRANSFORM_IDENTITY ? true : false;
}
