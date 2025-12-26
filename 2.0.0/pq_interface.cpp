#define DEBUG_LOG_TAG "IPqDevice"

#include "pq_interface.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <fcntl.h>
#include <sys/ioctl.h>

#include <ddp_pq.h>
#include <ddp_drv.h>

#include "ai_blulight_defender.h"
#include "utils/tools.h"
#include "utils/debug.h"

#ifdef USES_PQSERVICE
#include <aidlcommonsupport/NativeHandle.h>
#endif

#ifndef MTK_HWC_USE_DRM_DEVICE
#include "legacy/pqdev_legacy.h"
#else
#include "drm/drmpq.h"
#endif

#ifdef USES_PQSERVICE
using ::aidl::vendor::mediatek::hardware::pq_aidl::IAiBldCallback;

static bool getPqServiceName(std::string& pq_service_name)
{
    if (IPictureQuality_AIDL::descriptor == nullptr)
    {
        pq_service_name = "";
        return false;
    }
    else
    {
        pq_service_name = std::string() + IPictureQuality_AIDL::descriptor + "/default";
        return true;
    }
}
#endif

IPqDevice::IPqDevice()
    : m_pq_fd(-1)
    , m_use_ioctl(false)
{
#ifdef USES_PQSERVICE
    Mutex::Autolock lock(m_lock);
    m_pq_service = getPqServiceLocked(true, 0);
#endif
}

IPqDevice::~IPqDevice()
{
    if (m_pq_fd != -1)
    {
        protectedClose(m_pq_fd);
    }
}

bool IPqDevice::setColorTransform(const float* matrix, const int32_t& hint)
{
    if (isColorTransformIoctl())
    {
        return setColorTransformViaIoctl(matrix, hint);
    }
    else
    {
#ifdef USES_PQSERVICE
        return setColorTransformViaService(matrix, hint);
#else
        return false;
#endif
    }
}

bool IPqDevice::isColorTransformIoctl()
{
    Mutex::Autolock lock(m_lock);
    return m_use_ioctl;
}

void IPqDevice::useColorTransformIoctl(int32_t useIoctl)
{
    Mutex::Autolock lock(m_lock);
    m_use_ioctl = useIoctl;
}

void IPqDevice::setGamePQHandle(const buffer_handle_t& handle)
{
#ifdef USES_PQSERVICE
    Mutex::Autolock lock(m_lock);

    std::shared_ptr<IPictureQuality_AIDL> pq_service = getPqServiceLocked();
    if (pq_service == nullptr)
    {
        HWC_LOGE("%s: cannot find PQ service!", __func__);
        return;
    }
    else
    {
        if (handle == nullptr)
        {
            HWC_LOGW("%s: handle is null", __func__);
            return;
        }
        ATRACE_NAME("call gamePQHandle impl");
        Result result = Result::NOT_SUPPORTED;
        ndk::ScopedFileDescriptor fd = ndk::ScopedFileDescriptor(dup(handle->data[0]));
        ScopedAStatus status = pq_service->setGamePQHandle(fd, &result);
        if (!status.isOk() || result != Result::OK)
        {
            HWC_LOGW("%s: fail, res %d, status: %d", __func__, result, status.getStatus());
        }
    }
#else
    (void) handle;
#endif
}

int IPqDevice::setDisplayPqMode(const uint64_t disp_id, const uint32_t disp_unique_id,
        const int32_t pq_mode_id, const int prev_present_fence)
{
    int pq_fence_fd = -1;
#ifdef USES_PQSERVICE
    pq_fence_fd = setDisplayPqModeViaService(disp_id, disp_unique_id, pq_mode_id,
            prev_present_fence);
#else
    (void) disp_id;
    (void) disp_unique_id;
    (void) pq_mode_id;
    (void) prev_present_fence;
#endif
    return pq_fence_fd;
}

#ifdef USES_PQSERVICE

std::shared_ptr<IPictureQuality_AIDL> IPqDevice::getPqServiceLocked(bool timeout_break, int retry_limit)
{
    if (HwcFeatureList::getInstance().getFeature().is_support_pq <= 0)
    {
        return nullptr;
    }

    if (m_pq_service)
    {
        return m_pq_service;
    }

    int retryCount = 0;
    std::string pq_service_name;

    if (getPqServiceName(pq_service_name))
    {
        m_pq_service = IPictureQuality_AIDL::fromBinder(::ndk::SpAIBinder(
                           AServiceManager_checkService(pq_service_name.c_str())));
    }
    else
    {
        HWC_LOGI("%s(), PQ service is not ready", __FUNCTION__);
        m_pq_service = nullptr;
    }

    while (m_pq_service == nullptr)
    {
        if (retryCount >= retry_limit)
        {
            HWC_LOGE("Can't get PQ service tried (%d) times", retryCount);
            if (timeout_break)
            {
                break;
            }
        }
        usleep(100000); //sleep 100 ms to wait for next get service

        if (getPqServiceName(pq_service_name))
        {
            m_pq_service = IPictureQuality_AIDL::fromBinder(::ndk::SpAIBinder(
                               AServiceManager_checkService(pq_service_name.c_str())));
        }
        else
        {
            HWC_LOGI("%s(), PQ service is not ready, retryCount %d", __FUNCTION__, retryCount);
            m_pq_service = nullptr;
        }
        retryCount++;
    }

    if (m_pq_service)
    {
        m_aibld_callback = ndk::SharedRefBase::make<AiBldCallback>();
        ScopedAStatus status = m_pq_service->registerAIBldCb(m_aibld_callback);
        if (!status.isOk())
        {
            HWC_LOGW("%s: fail, status: %d", __func__, status.getStatus());
        }

        m_hal_death_recipint = std::make_unique<ScopedDeathRecipient>(onBinderDied, this);
        m_hal_death_recipint->linkToDeath(m_pq_service->asBinder().get());
    }

    return m_pq_service;
}

bool IPqDevice::setColorTransformViaService(const float* matrix, const int32_t& hint)
{
    Mutex::Autolock lock(m_lock);

    bool res = false;

    std::shared_ptr<IPictureQuality_AIDL> pq_service = getPqServiceLocked();
    if (pq_service == nullptr)
    {
        HWC_LOGE("%s: cannot find PQ service!", __func__);
        res = false;
    }
    else
    {
        const unsigned int dimension = 4;
        std::array<std::array<float, 4>, 4> send_matrix;
        for (unsigned int i = 0; i < dimension; ++i)
        {
            DbgLogger logger(DbgLogger::TYPE_HWC_LOG, 'D', "matrix ");
            for (unsigned int j = 0; j < dimension; ++j)
            {
                send_matrix[i][j] = matrix[i * dimension + j];
                logger.printf("%f,", send_matrix[i][j]);
            }
        }
        Result result = Result::INVALID_STATE;
        ScopedAStatus status = pq_service->setColorTransform(send_matrix, hint, 1, &result);
        if (!status.isOk() || result != Result::OK)
        {
            HWC_LOGW("%s: fail, result: %d, status: %d", __func__, result, status.getStatus());
            res = false;
        }
        else
        {
            res = true;
        }
    }

    return res;
}

int IPqDevice::setDisplayPqModeViaService(const uint64_t disp_id, const uint32_t disp_unique_id,
        const int32_t pq_mode_id, const int prev_present_fence)
{
    Mutex::Autolock lock(m_lock);

    int pq_fence_fd = -1;

    std::shared_ptr<IPictureQuality_AIDL> pq_service = getPqServiceLocked();
    if (pq_service == nullptr)
    {
        HWC_LOGE("%s: cannot find PQ service!", __func__);
    }
    else
    {
        if (disp_unique_id == 0)
        {
            HWC_LOGW("%s: set pq mode with a dubious unique id[%u]", __func__, disp_unique_id);
        }
        uint64_t di = disp_id;
        int32_t pmi = pq_mode_id;

        ndk::ScopedFileDescriptor tmpHandle;
        if (prev_present_fence >= 0)
        {
            tmpHandle = ndk::ScopedFileDescriptor(dup(prev_present_fence));
        }

        parcelable_setColorModeWithFence _aidl_return;

        ScopedAStatus status = pq_service->setColorModeWithFence(pmi, tmpHandle,
                    static_cast<int32_t>(di), static_cast<int32_t>(disp_unique_id), &_aidl_return);
        if (!status.isOk())
        {
            HWC_LOGW("%s: fail, status: %d", __func__, status.getStatus());
        }
        if (_aidl_return.retval == Result::OK)
        {
            // the release (ParcelFileDescriptor) is to change ownership (move)
            // unique_fd's move operator will call its release()
            // so it won't be close when exit scoped
            // therefore, there is no need to dup it
            pq_fence_fd = _aidl_return.pqFenceHdl.release();
            HWC_LOGD("%s: success, pq_fence_fd: %d", __func__, pq_fence_fd);
        }
        else
        {
            HWC_LOGI("%s: result: %d, pq_fence_fd: %d", __func__, _aidl_return.retval, pq_fence_fd);
        }
    }

    return pq_fence_fd;
}
#endif

bool IPqDevice::supportPqXml()
{
    return m_pq_xml_parser.hasXml();;
}

const std::vector<PqModeInfo>& IPqDevice::getRenderIntent(int32_t color_mode)
{
    return m_pq_xml_parser.getRenderIntent(color_mode);
}

int IPqDevice::getCcorrIdentityValue()
{
    return DEFAULT_IDENTITY_VALUE;
}

void IPqDevice::setAiBldBuffer(const buffer_handle_t& handle, uint32_t pf_fence_idx)
{
#ifdef USES_PQSERVICE
    Mutex::Autolock lock(m_lock);

    std::shared_ptr<IPictureQuality_AIDL> pq_service = getPqServiceLocked();
    if (pq_service == nullptr)
    {
        HWC_LOGE("%s: cannot find PQ service!", __func__);
    }
    else
    {
        ATRACE_NAME("call pq setAIBldBuffer");
        Result result = Result::INVALID_STATE;

        ScopedAStatus status = pq_service->setAIBldBuffer(::android::dupToAidl(handle),
                                                          static_cast<int32_t>(pf_fence_idx), &result);
        if (!status.isOk() || result != Result::OK)
        {
            HWC_LOGW("%s: fail, result: %d, status: %d", __func__, result, status.getStatus());
        }
    }
#else
    (void) handle;
    (void) pf_fence_idx;
#endif
}

void IPqDevice::afterPresent()
{
#ifdef USES_PQSERVICE
    if (HwcFeatureList::getInstance().getFeature().is_support_pq > 0)
    {
        Mutex::Autolock lock(m_lock);
        if (!m_pq_service)
        {
            getPqServiceLocked(true, 0);
        }
    }
#endif
}

void IPqDevice::resetPqService()
{
#ifdef USES_PQSERVICE
    AiBluLightDefender::getInstance().setEnable(false);
    Mutex::Autolock lock(m_lock);
    m_pq_service = nullptr;
#endif
}

IPqDevice* getPqDevice()
{
#ifndef MTK_HWC_USE_DRM_DEVICE
    return &PqDeviceLegacy::getInstance();
#else
    return &PqDeviceDrm::getInstance();
#endif
}

#ifdef USES_PQSERVICE
void IPqDevice::onBinderDied(void* cookie) {
    HWC_LOGI("PQ service died");

    if (cookie == nullptr)
    {
        return;
    }
    IPqDevice* pq_device = static_cast<IPqDevice*>(cookie);

    pq_device->resetPqService();
}

ScopedAStatus IPqDevice::AiBldCallback::AIBldeEnable(int32_t /*featureId*/, int32_t /*enable*/)
{
    // not used
    return ScopedAStatus::ok();
}

ScopedAStatus IPqDevice::AiBldCallback::AIBldParams(const Ai_bld_config& aiBldConfig)
{
    HWC_LOGI("%s(), enable %d, fps %d", __FUNCTION__, aiBldConfig.enable, aiBldConfig.fps);

    AiBluLightDefender::getInstance().setEnable(aiBldConfig.enable,
                                                static_cast<uint32_t>(aiBldConfig.fps),
                                                static_cast<uint32_t>(aiBldConfig.width),
                                                static_cast<uint32_t>(aiBldConfig.height),
                                                static_cast<uint32_t>(aiBldConfig.format));
    return ScopedAStatus::ok();
}

ScopedAStatus IPqDevice::AiBldCallback::perform(int32_t /*op_code*/,
                                                const std::optional<std::vector<uint8_t>> &/*input_params*/,
                                                const std::optional<std::vector<ndk::ScopedFileDescriptor>> &/*input_handles*/)
{
    return ScopedAStatus::ok();
}

IPqDevice::ScopedDeathRecipient::ScopedDeathRecipient(AIBinder_DeathRecipient_onBinderDied on_binder_died, void* cookie)
    : m_cookie(cookie)
{
    m_recipient = AIBinder_DeathRecipient_new(on_binder_died);
}

void IPqDevice::ScopedDeathRecipient::linkToDeath(AIBinder* binder)
{
    binder_status_t linked = AIBinder_linkToDeath(binder, m_recipient, m_cookie);
    if (linked != STATUS_OK)
    {
        HWC_LOGE("%s(), cound not link death recipient to HAL death", __FUNCTION__);
    }
}

IPqDevice::ScopedDeathRecipient::~ScopedDeathRecipient()
{
    AIBinder_DeathRecipient_delete(m_recipient);
}
#endif
