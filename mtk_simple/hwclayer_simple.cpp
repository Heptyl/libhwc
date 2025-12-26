#define DEBUG_LOG_TAG "hwclayer_simple"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include "hwclayer_simple.h"

namespace simplehwc {

std::atomic<uint64_t> HWCLayer_simple::id_count(0);

HWCLayer_simple::HWCLayer_simple()
    : m_id(++id_count)
{
}

HWCLayer_simple::~HWCLayer_simple()
{
}

uint64_t HWCLayer_simple::getId()
{
    return m_id;
}

}  // namespace simplehwc

