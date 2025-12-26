#ifndef __HWC_PQ_DEV_H__
#define __HWC_PQ_DEV_H__

#include <utils/Mutex.h>
#include <utils/RefBase.h>
#include <cutils/native_handle.h>

#include "pq_xml_parser.h"

#ifdef USES_PQSERVICE
#include <aidl/vendor/mediatek/hardware/pq_aidl/IPictureQuality_AIDL.h>
#include <aidl/vendor/mediatek/hardware/pq_aidl/BnAiBldCallback.h>

#include <android/binder_manager.h>
#include <android/binder_process.h>

using ndk::ScopedAStatus;
using ::aidl::vendor::mediatek::hardware::pq_aidl::IPictureQuality_AIDL;
using ::aidl::vendor::mediatek::hardware::pq_aidl::Ai_bld_config;
using ::aidl::vendor::mediatek::hardware::pq_aidl::Result;
using ::aidl::vendor::mediatek::hardware::pq_aidl::parcelable_setColorModeWithFence;
using ::aidl::vendor::mediatek::hardware::pq_aidl::BnAiBldCallback;

#endif

using namespace android;

#define DEFAULT_IDENTITY_VALUE 1024

class IPqDevice : public RefBase
{
public:
    IPqDevice();
    ~IPqDevice();

    virtual bool setColorTransform(const float* matrix, const int32_t& hint);

    virtual bool isColorTransformIoctl();
    virtual void useColorTransformIoctl(int32_t useIoctl);

    virtual void setGamePQHandle(const buffer_handle_t& handle);

    virtual int setDisplayPqMode(const uint64_t disp_id, const uint32_t disp_unique_id,
            const int32_t pq_mode_id, const int prev_present_fence);

    virtual bool supportPqXml();

    virtual const std::vector<PqModeInfo>& getRenderIntent(int32_t color_mode);

    virtual int getCcorrIdentityValue();

    virtual void setAiBldBuffer(const buffer_handle_t& handle, uint32_t pf_fence_idx);

    virtual void afterPresent();

    virtual void resetPqService();

#ifdef USES_PQSERVICE
private:
    static void onBinderDied(void* cookie);

protected:
    class AiBldCallback : public BnAiBldCallback
    {
public:
        AiBldCallback() {}
        ~AiBldCallback() {}
        virtual ScopedAStatus AIBldeEnable(int32_t featureId, int32_t enable) override;
        virtual ScopedAStatus AIBldParams(const Ai_bld_config& aiBldConfig) override;
        virtual ScopedAStatus perform(int32_t op_code,
                                      const std::optional<std::vector<uint8_t>> &input_params,
                                      const std::optional<std::vector<ndk::ScopedFileDescriptor>> &input_handles) override;
    };

    class ScopedDeathRecipient
    {
public:
        explicit ScopedDeathRecipient(AIBinder_DeathRecipient_onBinderDied on_binder_died, void* cookie);
        ScopedDeathRecipient(const ScopedDeathRecipient&) = delete;
        ScopedDeathRecipient& operator=(ScopedDeathRecipient const&) = delete;
        void linkToDeath(AIBinder* binder);
        ~ScopedDeathRecipient();

private:
        AIBinder_DeathRecipient* m_recipient;
        void* m_cookie;
    };
#endif

protected:
    virtual void checkAndOpenIoctl() = 0;

    virtual bool setColorTransformViaIoctl(const float* matrix, const int32_t& hint) = 0;
#ifdef USES_PQSERVICE
    virtual std::shared_ptr<IPictureQuality_AIDL> getPqServiceLocked(bool timeout_break = false, int retry_limit = 10);
    virtual bool setColorTransformViaService(const float* matrix, const int32_t& hint);
    virtual int setDisplayPqModeViaService(const uint64_t disp_id, const uint32_t disp_unique_id,
            const int32_t pq_mode_id, const int prev_present_fence);
#endif

protected:
    int m_pq_fd;

    Mutex m_lock;
    bool m_use_ioctl;

#ifdef USES_PQSERVICE
    std::shared_ptr<IPictureQuality_AIDL> m_pq_service;
    std::shared_ptr<AiBldCallback> m_aibld_callback;
    std::unique_ptr<ScopedDeathRecipient> m_hal_death_recipint;
#endif

    PqXmlParser m_pq_xml_parser;
};

IPqDevice* getPqDevice();

#endif
