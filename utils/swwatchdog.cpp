#undef LOG_TAG
#define LOG_TAG "SWWatchDog"

#include <log/log.h>

// thread related headers
#include <processgroup/sched_policy.h>
#include <system/thread_defs.h>
#include <sys/resource.h>
#include <thread>

#include <inttypes.h>
#include <memory>
#include <map>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <atomic>
#include <string>
#include <sstream>
#include <iomanip>

#ifdef USE_HWC2
#define DEBUG_LOG_TAG "SWWatchDog"
#include "swwatchdog.h"
#include "utils/debug.h"
#define WDLOGW HWC_LOGW
#define WDLOGE HWC_LOGE
#else
#include "ui_ext/SWWatchDog.h"
#define WDLOGW ALOGW
#define WDLOGE ALOGE
#endif

#define COUNT_TP_DUR_MS(from, to) \
        std::chrono::duration_cast<std::chrono::milliseconds>(to - from).count()
#define COUNT_DUR_MS(dur) \
        std::chrono::duration_cast<std::chrono::milliseconds>(dur).count()

namespace android {

const SWWatchDog::anchor_id_t SWWatchDog::NO_ANCHOR     = 0;
const SWWatchDog::msecs_t SWWatchDog::DEFAULT_TIMER     = 1000;
const SWWatchDog::msecs_t SWWatchDog::DEFAULT_THRESHOLD = 800;

typedef std::chrono::steady_clock WDT_CLOCK;

//==================================================================================================
// WDTAnchor
//
class WDTAnchor {
public:
    WDTAnchor()
        : mAnchorTime(WDT_CLOCK::now())
        , mThreshold(SWWatchDog::DEFAULT_THRESHOLD)
        , mTid(gettid())
    {
    }

    WDTAnchor(const WDTAnchor& src) {
        mAnchorTime = src.mAnchorTime;
        mThreshold  = src.mThreshold;
        mMsg        = src.mMsg;
        mTid        = src.mTid;
        mNotify     = src.mNotify;
    }

    explicit WDTAnchor(const std::shared_ptr<WDTAnchor>& src) {
        mAnchorTime = src->mAnchorTime;
        mThreshold  = src->mThreshold;
        mMsg        = src->mMsg;
        mTid        = src->mTid;
        mNotify     = src->mNotify;
    }

    ~WDTAnchor() {}

    WDTAnchor& operator= (const WDTAnchor& src) {
        if (&src != this) {
            mAnchorTime = src.mAnchorTime;
            mThreshold  = src.mThreshold;
            mMsg        = src.mMsg;
            mTid        = src.mTid;
            mNotify     = src.mNotify;
        }
        return *this;
    }

    WDTAnchor& operator= (const std::shared_ptr<WDTAnchor>& src) {
        if (src.get() == this) {
            return *this;
        }
        if (src != nullptr) {
            mAnchorTime = src->mAnchorTime;
            mThreshold  = src->mThreshold;
            mMsg        = src->mMsg;
            mTid        = src->mTid;
            mNotify     = src->mNotify;
        } else {
            mAnchorTime = WDT_CLOCK::now();
            mThreshold  = std::chrono::milliseconds(SWWatchDog::DEFAULT_THRESHOLD);
            mMsg        = "";
            mTid        = gettid();
            mNotify.reset();
        }
        return *this;
    }

    SWWatchDog::anchor_id_t getID() const {
        return reinterpret_cast<SWWatchDog::anchor_id_t>(this);
    }

    bool isTimeout(WDT_CLOCK::time_point now = WDT_CLOCK::now()) const {
        return now > (mAnchorTime + mThreshold);
    }

private:
    friend class SWWatchDogTimer;
    WDT_CLOCK::time_point mAnchorTime;
    std::chrono::milliseconds mThreshold;
    std::string mMsg;
    pid_t mTid;
    std::shared_ptr<SWWatchDog::Recipient> mNotify;
};

//==================================================================================================
// SWWatchDogTimer
//
class SWWatchDogTimer {
private:
    SWWatchDogTimer()
        : mDuringOnTimeout(false)
        , mSuspend(false)
        , mTimer(SWWatchDog::DEFAULT_TIMER)
        , mKeepRunning(false)
    {
#ifndef DISABLE_SWWDT
        // Start WatchDog timer
        mKeepRunning.store(true);
        mThread = std::thread([&](){ threadMain(); });
        pthread_setname_np(mThread.native_handle(), "SWWatchDog");

        pid_t tid = pthread_gettid_np(mThread.native_handle());
        setpriority(PRIO_PROCESS, static_cast<id_t>(tid), HAL_PRIORITY_URGENT_DISPLAY);
        set_sched_policy(tid, SP_FOREGROUND);
#endif
    }

    ~SWWatchDogTimer() {
        mKeepRunning.store(false);
        if (mThread.joinable()) {
            resume(0); // can be any num, don't care
            mThread.join();
        }
    }

public:
    static SWWatchDogTimer& getInstance() {
        static std::mutex sMutex;
        static SWWatchDogTimer* instance = nullptr;
        std::lock_guard<std::mutex> lock(sMutex);
        if (instance == nullptr) {
            instance = new SWWatchDogTimer();
        }
        return *instance;
    }

    void dump(std::string& result) const {
        // if during onTimeout, the mTableMutex was already locked.
        if (mDuringOnTimeout.load()) {
            dumpLocked(result);
        } else {
            std::lock_guard<std::mutex> lock(mTableMutex);
            dumpLocked(result);
        }
    }

    void suspend(uint64_t id) {
        std::lock_guard<std::mutex> lock(mMutex);
        mRequestResumeIds.remove(id);
        if (mRequestResumeIds.empty())
        {
            mSuspend.store(true);
        }
    }

    void resume(uint64_t id) {
        std::lock_guard<std::mutex> lock(mMutex);
        mRequestResumeIds.push_back(id);
        mSuspend.store(false);
        mCondition.notify_all();
    }

    bool setTickNotify(const std::shared_ptr<SWWatchDog::Recipient>& notify) {
        std::lock_guard<std::mutex> lock(mTickMutex);
        mTickNotify = notify;
        return true;
    }

    SWWatchDog::anchor_id_t setAnchor(const std::shared_ptr<SWWatchDog::Recipient>& notify,
                                      const std::chrono::milliseconds& threshold,
                                      const std::string& msg) {
        if (notify == nullptr) {
            WDLOGE("[SW_WDT] Set an anchor w/o notify. <<%s>>", msg.c_str());
            return SWWatchDog::NO_ANCHOR;
        }

        auto anchor = std::make_shared<WDTAnchor>();
        if (anchor == nullptr) {
            WDLOGE("[SW_WDT] Not enough of memory, could not set anchor! <<%s>>", msg.c_str());
            return SWWatchDog::NO_ANCHOR;
        }

        SWWatchDog::anchor_id_t id = anchor->getID();
        anchor->mThreshold    = threshold;
        anchor->mMsg          = msg;
        anchor->mNotify       = notify;
        notify->onSetAnchor(id, anchor->mTid, anchor->mMsg, threshold.count());
        {
            std::lock_guard<std::mutex> lock(mTableMutex);
            mAnchorTable[id] = anchor;
        }
        return id;
    }

    bool delAnchor(const SWWatchDog::anchor_id_t& id) {

        std::shared_ptr<WDTAnchor> anchor;
        {
            std::lock_guard<std::mutex> lock(mTableMutex);
            auto anchor_iter = mAnchorTable.find(id);
            if (anchor_iter == mAnchorTable.end()) {
                WDLOGE("[SW_WDT] delAnchor: the anchor(%" PRIxPTR ") is not in the pool", id);
                return false;
            }
            anchor = anchor_iter->second;
            if (anchor == nullptr) {
                WDLOGE("[SW_WDT] delAnchor: the anchor(%" PRIxPTR ") is NULL", id);
                return false;
            }
            if (anchor->mTid != gettid()) {
                WDLOGW("[SW_WDT] delAnchor: the anchor(%" PRIxPTR ") can not been deleted in different thread", id);
                return false;
            }

            mAnchorTable.erase(anchor_iter);
        }

        WDT_CLOCK::time_point now = WDT_CLOCK::now();
        SWWatchDog::msecs_t spendTime = COUNT_TP_DUR_MS(anchor->mAnchorTime, now);
        anchor->mNotify->onDelAnchor(id, anchor->mTid, anchor->mMsg, anchor->mThreshold.count(),
                                     spendTime, anchor->isTimeout(now));

        return true;
    }

    void setTimer(const std::chrono::milliseconds& timer) {
        std::lock_guard<std::mutex> lock(mMutex);
        mTimer = timer;
    }

private:
    // Should be protected by mTableMutex
    mutable std::mutex mTableMutex;
    std::map<SWWatchDog::anchor_id_t, std::shared_ptr<WDTAnchor> > mAnchorTable;
    std::atomic<bool> mDuringOnTimeout;

    // Should be protected by mTickMutex
    mutable std::mutex mTickMutex;
    std::shared_ptr<SWWatchDog::Recipient> mTickNotify;

    // Should be protected by mMutex
    mutable std::mutex mMutex;
    std::condition_variable mCondition;
    std::atomic<bool> mSuspend;
    std::chrono::milliseconds mTimer;
    std::list<uint64_t> mRequestResumeIds;

    // Thread
    std::thread mThread;
    std::atomic<bool> mKeepRunning;

    void threadMain() {
        bool loopAgain = false;
        do {
            loopAgain = threadLoop();
        } while (mKeepRunning.load() && loopAgain);
    }

    bool threadLoop() {
        {
            std::unique_lock<std::mutex> lock(mMutex);
            if (mSuspend.load()) {
                mCondition.wait(lock);
            }
        }

        if (!mKeepRunning.load()) {
            return false;
        }

        WDT_CLOCK::time_point now = WDT_CLOCK::now();
        {
            std::lock_guard<std::mutex> lock(mTableMutex);

            // check Watchdog timeout for each thread
            for (auto&& anchor_iter : mAnchorTable) {
                std::shared_ptr<WDTAnchor> anchor = anchor_iter.second;
                if (anchor == nullptr) {
                    WDLOGE("[SW_WDT] There is an empty anchor(%" PRIxPTR ") in the pool.",
                        anchor_iter.first);
                    continue;
                }

                // notify anchor if timeout
                if (anchor->mNotify != nullptr) {
                    if (anchor->isTimeout(now)) {
                        SWWatchDog::msecs_t spendTime = COUNT_TP_DUR_MS(anchor->mAnchorTime, now);
                        mDuringOnTimeout.store(true);
                        anchor->mNotify->onTimeout(anchor->getID(), anchor->mTid,
                                                   anchor->mMsg, anchor->mThreshold.count(),
                                                   spendTime);
                        mDuringOnTimeout.store(false);
                    }
                } else {
                    WDLOGE("[SW_WDT] There is an anchor(%" PRIxPTR ") w/o notify.", anchor->getID());
                    continue;
                }
            }
        }

        // notify onTick if needed.
        {
            std::lock_guard<std::mutex> lock(mTickMutex);
            if (mTickNotify != nullptr) {
                mTickNotify->onTick();
            }
        }

        // sleep a period time, but wakeup at thread resume or terminate.
        if (mKeepRunning.load()) {
            std::unique_lock<std::mutex> lock(mMutex);
            mCondition.wait_for(lock, mTimer);
            return true;
        }
        return false;
    }

    void dumpLocked(std::string& result) const {
        WDT_CLOCK::time_point now = WDT_CLOCK::now();
        std::ostringstream stream;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            stream << "WDT Anchor Num: " << std::setw(3) << mAnchorTable.size()
                << "\tWDT Suspend=" << (mSuspend ? "Yes" : "No") << std::endl;
        }
        stream << "--------------------------------------------------" << std::endl;
        int idx = 0;
        for (const auto& anchor_iter : mAnchorTable) {
            const std::shared_ptr<WDTAnchor>& anchor = anchor_iter.second;
            stream << "    [" << std::setw(2) << idx++ << "]  ";
            if (anchor == nullptr) {
                stream << " No anchor" << std::endl;
            } else {
                if (anchor->isTimeout(now)){
                    stream << "*";
                } else {
                    stream << " ";
                }
                stream << "id=" << std::hex << anchor->getID() << std::dec
                    << " tid=" << std::setw(6) << anchor->mTid
                    << "\tthreshold=" << std::setw(6) << anchor->mThreshold.count() << "ms"
                    << "\tspend=" << std::setw(6) << COUNT_TP_DUR_MS(anchor->mAnchorTime, now) << "ms"
                    << "   <<" << anchor->mMsg << ">>" << std::endl;
            }
        }
        stream << "--------------------------------------------------" << std::endl;
        result.append(stream.str());
    }

};

//==================================================================================================
// SWWatchDog
//
SWWatchDog::SWWatchDog(const msecs_t& threshold)
    : mThreshold(threshold)
    , mNotify(std::make_shared<Recipient>())
{
}

SWWatchDog::~SWWatchDog() {}

void SWWatchDog::dump(std::string& result) {
#ifndef DISABLE_SWWDT
    SWWatchDogTimer::getInstance().dump(result);
#else
    (void)(result);
#endif
}

void SWWatchDog::suspend(uint64_t id) {
#ifndef DISABLE_SWWDT
    SWWatchDogTimer::getInstance().suspend(id);
#endif
}

void SWWatchDog::resume(uint64_t id) {
#ifndef DISABLE_SWWDT
    SWWatchDogTimer::getInstance().resume(id);
#endif
}

SWWatchDog::anchor_id_t SWWatchDog::setAnchor(const std::string& msg, const msecs_t& threshold) {
#ifndef DISABLE_SWWDT
    std::lock_guard<std::mutex> lock(mDataMutex);
    if (threshold > 0) {
        return SWWatchDogTimer::getInstance().setAnchor(mNotify,
            std::chrono::milliseconds(threshold), msg);
    } else {
        return SWWatchDogTimer::getInstance().setAnchor(mNotify, mThreshold, msg);
    }
#else
    (void)(msg);
    (void)(threshold);
    return NO_ANCHOR;
#endif
}

bool SWWatchDog::delAnchor(const anchor_id_t& id) {
#ifndef DISABLE_SWWDT
    return SWWatchDogTimer::getInstance().delAnchor(id);
#else
    (void)(id);
    return false;
#endif
}

bool SWWatchDog::setTickNotify(const std::shared_ptr<Recipient>& notify) {
#ifndef DISABLE_SWWDT
    return SWWatchDogTimer::getInstance().setTickNotify(notify);
#else
    (void)(notify);
    return false;
#endif
}

bool SWWatchDog::setWDTNotify(const std::shared_ptr<Recipient>& notify) {
    std::lock_guard<std::mutex> lock(mDataMutex);
    mNotify = notify;
    return true;
}

void SWWatchDog::setTimer(const msecs_t& timer) {
#ifndef DISABLE_SWWDT
    SWWatchDogTimer::getInstance().setTimer(std::chrono::milliseconds(timer));
#else
    (void)(timer);
#endif
}

void SWWatchDog::setThreshold(const msecs_t& threshold) {
    std::lock_guard<std::mutex> lock(mDataMutex);
    mThreshold = std::chrono::milliseconds(threshold);
}

SWWatchDog::msecs_t SWWatchDog::getThreshold() const {
    std::lock_guard<std::mutex> lock(mDataMutex);
    return mThreshold.count();
}

SWWatchDog SWWatchDog::DEFAULT_WDT;

//==================================================================================================
// SWWatchDog::Recipient
//
void SWWatchDog::Recipient::onTimeout(const anchor_id_t& id, const pid_t& tid,
                                      const std::string& msg, const msecs_t& threshold,
                                      const msecs_t& spendTime) {
    mTimeoutCount++;
    // log reduce, only print log at index: 1, 3, 7, 11, 15,...
    if ((mTimeoutCount % 4) == 3 || mTimeoutCount == 1) {
        WDLOGW("[SW_WDT] Thread(%d) timeout. id=%" PRIxPTR " <<%s>> "
              "spend/threshold: %" PRId64 "/%" PRId64 " ms",
              tid, id, msg.c_str(), spendTime, threshold);
    }
}

void SWWatchDog::Recipient::onDelAnchor(const anchor_id_t& id, const pid_t& tid,
                                        const std::string& msg, const msecs_t& threshold,
                                        const msecs_t& spendTime, const bool& isTimeout) {
    if (isTimeout) {
        WDLOGW("[SW_WDT] Thread(%d) id=%" PRIxPTR " <<%s>> spend/threshold: %" PRId64 "/%" PRId64 " ms",
              tid, id, msg.c_str(), spendTime, threshold);
    }
}

};  // namespace android
