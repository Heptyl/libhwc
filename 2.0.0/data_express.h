#pragma once

#include <list>
#include <mutex>
#include <optional>
#include <thread>

#include "display.h"
#include "msync.h"

// can store data that does not need maintain life cylce, ex. no fd
struct DataPackage
{
    DataPackage(uint64_t sequence);

    // explicit no copy constructor, no copy assignment operator
    DataPackage(DataPackage const&) = delete;
    DataPackage& operator=(DataPackage const&) = delete;

    uint64_t m_sequence;    // match with HWCDispatcher::m_sequence

    std::optional<MSync2Data> m_msync2_data;

    bool m_need_free_fb_cache = false;
};

namespace android {
class String8;
}

class DataExpress
{
public:
    static DataExpress& getInstance();

    // request package for sequence id, create new or get existed package.
    // package are created only on composer main thread (sf main thread)
    DataPackage& requestPackage(uint64_t dpy, uint64_t sequence);

    // find corresponding sequence package and late package
    // late_package will happens, if frame are droped, receiver may need this info
    // only overlay thread can use late_package
    void findPackage(uint64_t dpy,
                     uint64_t sequence,
                     DataPackage** package,
                     DataPackage** late_package = nullptr);

    // packages are deleted @ OverlayEngine::threadLoop or VirPostHandler::process if mirror
    // all pass sequence packages are deleted
    void deletePackage(uint64_t dpy, uint64_t sequence);
    void deletePackageAll(uint64_t dpy);

    void dump(android::String8 *dump_str);

private:
    DataExpress();

private:
    mutable std::mutex m_packages_mutex[DisplayManager::MAX_DISPLAYS];
    std::list<DataPackage> m_packages[DisplayManager::MAX_DISPLAYS];
};
