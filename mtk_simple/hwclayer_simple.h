#pragma once
#include <vector>
#include <map>

namespace simplehwc {

class HWCLayer_simple
{
public:
    static std::atomic<uint64_t> id_count;

    HWCLayer_simple();
    ~HWCLayer_simple();

    uint64_t getId();

private:
    uint64_t m_id;
};

}  // namespace simplehwc

