#define DEBUG_LOG_TAG "DataExpress"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include "data_express.h"

#include "utils/debug.h"

DataPackage::DataPackage(uint64_t sequence)
    : m_sequence(sequence)
{
}

DataExpress& DataExpress::getInstance()
{
    static DataExpress gInstance;
    return gInstance;
}

DataExpress::DataExpress()
{
}

DataPackage& DataExpress::requestPackage(uint64_t dpy, uint64_t sequence)
{
    std::lock_guard<std::mutex> lock(m_packages_mutex[dpy]);

    // return package if exist
    for (DataPackage& p : m_packages[dpy])
    {
        if (sequence == p.m_sequence)
        {
            return p;
        }
    }

    LOG_ALWAYS_FATAL_IF(m_packages[dpy].size() > 20, "dpy %" PRIu64 ", m_packages probably leaks", dpy);

    // not exist, create new one
    m_packages[dpy].emplace_back(sequence);
    return m_packages[dpy].back();
}

void DataExpress::findPackage(uint64_t dpy,
                              uint64_t sequence,
                              DataPackage** package,
                              DataPackage** late_package)
{
    if (!package)
    {
        HWC_LOGW("%s(), package == nulltpr", __FUNCTION__);
        return;
    }

    std::lock_guard<std::mutex> lock(m_packages_mutex[dpy]);

    *package = nullptr;
    if (late_package)
    {
        *late_package = nullptr;
    }

    // from old to new packages
    for (DataPackage& p : m_packages[dpy])
    {
        if (late_package &&
            p.m_sequence < sequence)
        {
            *late_package = &p;
        }

        if (p.m_sequence == sequence)
        {
            *package = &p;
            break;
        }
    }

}

void DataExpress::deletePackage(uint64_t dpy, uint64_t sequence)
{
    std::lock_guard<std::mutex> lock(m_packages_mutex[dpy]);

    m_packages[dpy].remove_if(
        [&](DataPackage& p)
        {
            return p.m_sequence <= sequence;
        });
}

void DataExpress::deletePackageAll(uint64_t dpy)
{
    deletePackage(dpy, UINT64_MAX);
}

void DataExpress::dump(String8 *dump_str)
{
    for (unsigned int dpy = 0; dpy < DisplayManager::MAX_DISPLAYS; dpy++)
    {
        std::lock_guard<std::mutex> lock(m_packages_mutex[dpy]);

        dump_str->appendFormat("dpy %u, m_packages size %zu\n", dpy, m_packages[dpy].size());
    }
}

