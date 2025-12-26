/*
 * Copyright 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <hwc_ui/Gralloc2.h>

#include <hidl/ServiceManagement.h>
#include <hwbinder/IPCThreadState.h>

#include <inttypes.h>
#include <log/log.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wzero-length-array"
#include <sync/sync.h>
#pragma clang diagnostic pop

using android::hardware::graphics::common::V1_1::BufferUsage;
using android::hardware::graphics::common::V1_1::PixelFormat;
using android::hardware::graphics::mapper::V2_0::BufferDescriptor;
using android::hardware::graphics::mapper::V2_0::Error;
using android::hardware::graphics::mapper::V2_0::YCbCrLayout;
using android::hardware::graphics::mapper::V2_1::IMapper;

namespace hwc {

namespace {

static constexpr Error kTransactionError = Error::NO_RESOURCES;

uint64_t getValid10UsageBits() {
    static const uint64_t valid10UsageBits = []() -> uint64_t {
        using android::hardware::graphics::common::V1_0::BufferUsage;
        uint64_t bits = 0;
        for (const auto bit : android::hardware::hidl_enum_range<BufferUsage>()) {
            bits = bits | bit;
        }
        return bits;
    }();
    return valid10UsageBits;
}

uint64_t getValid11UsageBits() {
    static const uint64_t valid11UsageBits = []() -> uint64_t {
        using android::hardware::graphics::common::V1_1::BufferUsage;
        uint64_t bits = 0;
        for (const auto bit : android::hardware::hidl_enum_range<BufferUsage>()) {
            bits = bits | bit;
        }
        return bits;
    }();
    return valid11UsageBits;
}

static inline IMapper::Rect sGralloc2Rect(const Rect& rect) {
    IMapper::Rect outRect{};
    outRect.left = rect.left;
    outRect.top = rect.top;
    outRect.width = rect.width();
    outRect.height = rect.height();
    return outRect;
}

}  // anonymous namespace

Gralloc2Mapper::Gralloc2Mapper() {
    mMapper = android::hardware::graphics::mapper::V2_0::IMapper::getService();
    if (mMapper == nullptr) {
        ALOGW("mapper 2.x is not supported");
        return;
    }
    if (mMapper->isRemote()) {
        LOG_ALWAYS_FATAL("gralloc-mapper must be in passthrough mode");
    }

    // IMapper 2.1 is optional
    mMapperV2_1 = IMapper::castFrom(mMapper);
}

bool Gralloc2Mapper::isLoaded() const {
    return mMapper != nullptr;
}

int Gralloc2Mapper::validateBufferDescriptorInfo(
        IMapper::BufferDescriptorInfo* descriptorInfo) const {
    uint64_t validUsageBits = getValid10UsageBits();
    if (mMapperV2_1 != nullptr) {
        validUsageBits = validUsageBits | getValid11UsageBits();
    }

    if (descriptorInfo->usage & ~validUsageBits) {
        ALOGE("buffer descriptor contains invalid usage bits 0x%" PRIx64,
              descriptorInfo->usage & ~validUsageBits);
        return -EINVAL;
    }
    return 0;
}

int Gralloc2Mapper::createDescriptor(void* bufferDescriptorInfo,
                                     void* outBufferDescriptor) const {
    if (!mMapper) {
        LOG_ALWAYS_FATAL("%s(), mMapper == nullptr", __FUNCTION__);
        return -EPIPE;
    }

    IMapper::BufferDescriptorInfo* descriptorInfo =
            static_cast<IMapper::BufferDescriptorInfo*>(bufferDescriptorInfo);
    BufferDescriptor* outDescriptor = static_cast<BufferDescriptor*>(outBufferDescriptor);

    int status = validateBufferDescriptorInfo(descriptorInfo);
    if (status != 0) {
        return status;
    }

    Error error;
    auto hidl_cb = [&](const auto& tmpError, const auto& tmpDescriptor)
                   {
                       error = tmpError;
                       if (error != Error::NONE) {
                           return;
                       }

                       *outDescriptor = tmpDescriptor;
                   };

    android::hardware::Return<void> ret;
    if (mMapperV2_1 != nullptr) {
        ret = mMapperV2_1->createDescriptor_2_1(*descriptorInfo, hidl_cb);
    } else {
        const android::hardware::graphics::mapper::V2_0::IMapper::BufferDescriptorInfo info = {
                descriptorInfo->width,
                descriptorInfo->height,
                descriptorInfo->layerCount,
                static_cast<android::hardware::graphics::common::V1_0::PixelFormat>(descriptorInfo->format),
                descriptorInfo->usage,
        };
        ret = mMapper->createDescriptor(info, hidl_cb);
    }

    return static_cast<int>((ret.isOk()) ? error : kTransactionError);
}

int Gralloc2Mapper::importBuffer(const android::hardware::hidl_handle& rawHandle,
                                 buffer_handle_t* outBufferHandle) const {
    if (!mMapper) {
        LOG_ALWAYS_FATAL("%s(), mMapper == nullptr", __FUNCTION__);
        return -EPIPE;
    }

    Error error;
    auto ret = mMapper->importBuffer(rawHandle,
            [&](const auto& tmpError, const auto& tmpBuffer)
            {
                error = tmpError;
                if (error != Error::NONE) {
                    return;
                }

                *outBufferHandle = static_cast<buffer_handle_t>(tmpBuffer);
            });

    return static_cast<int>((ret.isOk()) ? error : kTransactionError);
}

void Gralloc2Mapper::freeBuffer(buffer_handle_t bufferHandle) const {
    if (!mMapper) {
        LOG_ALWAYS_FATAL("%s(), mMapper == nullptr", __FUNCTION__);
        return;
    }

    auto buffer = const_cast<native_handle_t*>(bufferHandle);
    auto ret = mMapper->freeBuffer(buffer);

    auto error = (ret.isOk()) ? static_cast<Error>(ret) : kTransactionError;
    ALOGE_IF(error != Error::NONE, "freeBuffer(%p) failed with %d",
            buffer, error);
}

int Gralloc2Mapper::validateBufferSize(buffer_handle_t bufferHandle, uint32_t width,
                                       uint32_t height, hwc::PixelFormat format,
                                       uint32_t layerCount, uint64_t usage,
                                       uint32_t stride) const {
    if (mMapperV2_1 == nullptr) {
        return 0;
    }

    IMapper::BufferDescriptorInfo descriptorInfo = {};
    descriptorInfo.width = width;
    descriptorInfo.height = height;
    descriptorInfo.layerCount = layerCount;
    descriptorInfo.format = static_cast<android::hardware::graphics::common::V1_1::PixelFormat>(format);
    descriptorInfo.usage = usage;

    auto buffer = const_cast<native_handle_t*>(bufferHandle);
    auto ret = mMapperV2_1->validateBufferSize(buffer, descriptorInfo, stride);

    return static_cast<int>((ret.isOk()) ? static_cast<Error>(ret) : kTransactionError);
}

void Gralloc2Mapper::getTransportSize(buffer_handle_t bufferHandle, uint32_t* outNumFds,
                                      uint32_t* outNumInts) const {
    *outNumFds = uint32_t(bufferHandle->numFds);
    *outNumInts = uint32_t(bufferHandle->numInts);

    if (mMapperV2_1 == nullptr) {
        return;
    }

    Error error;
    auto buffer = const_cast<native_handle_t*>(bufferHandle);
    auto ret = mMapperV2_1->getTransportSize(buffer,
            [&](const auto& tmpError, const auto& tmpNumFds, const auto& tmpNumInts) {
                error = tmpError;
                if (error != Error::NONE) {
                    return;
                }

                *outNumFds = tmpNumFds;
                *outNumInts = tmpNumInts;
            });

    error = (ret.isOk()) ? error : kTransactionError;

    ALOGE_IF(error != Error::NONE, "getTransportSize(%p) failed with %d", buffer, error);
}

int Gralloc2Mapper::lock(buffer_handle_t bufferHandle, uint64_t usage, const Rect& bounds,
                         int acquireFence, void** outData, int32_t* outBytesPerPixel,
                         int32_t* outBytesPerStride) const {
    if (!mMapper) {
        LOG_ALWAYS_FATAL("%s(), mMapper == nullptr", __FUNCTION__);
        return -EPIPE;
    }

    if (outBytesPerPixel) {
        *outBytesPerPixel = -1;
    }
    if (outBytesPerStride) {
        *outBytesPerStride = -1;
    }
    auto buffer = const_cast<native_handle_t*>(bufferHandle);

    IMapper::Rect accessRegion = sGralloc2Rect(bounds);

    // put acquireFence in a hidl_handle
    android::hardware::hidl_handle acquireFenceHandle;
    NATIVE_HANDLE_DECLARE_STORAGE(acquireFenceStorage, 1, 0);
    if (acquireFence >= 0) {
        auto h = native_handle_init(acquireFenceStorage, 1, 0);
        if (h != nullptr)
        {
            h->data[0] = acquireFence;
        }
        acquireFenceHandle = h;
    }

    Error error;
    auto ret = mMapper->lock(buffer, usage, accessRegion, acquireFenceHandle,
            [&](const auto& tmpError, const auto& tmpData)
            {
                error = tmpError;
                if (error != Error::NONE) {
                    return;
                }

                *outData = tmpData;
            });

    // we own acquireFence even on errors
    if (acquireFence >= 0) {
        close(acquireFence);
    }

    error = (ret.isOk()) ? error : kTransactionError;

    ALOGW_IF(error != Error::NONE, "lock(%p, ...) failed: %d", bufferHandle, error);

    return static_cast<int>(error);
}

int Gralloc2Mapper::lock(buffer_handle_t bufferHandle, uint64_t usage, const Rect& bounds,
                         int acquireFence, android_ycbcr* ycbcr) const {
    if (!mMapper) {
        LOG_ALWAYS_FATAL("%s(), mMapper == nullptr", __FUNCTION__);
        return -EPIPE;
    }

    auto buffer = const_cast<native_handle_t*>(bufferHandle);

    IMapper::Rect accessRegion = sGralloc2Rect(bounds);

    // put acquireFence in a hidl_handle
    android::hardware::hidl_handle acquireFenceHandle;
    NATIVE_HANDLE_DECLARE_STORAGE(acquireFenceStorage, 1, 0);
    if (acquireFence >= 0) {
        auto h = native_handle_init(acquireFenceStorage, 1, 0);
        if (h != nullptr)
        {
            h->data[0] = acquireFence;
        }
        acquireFenceHandle = h;
    }

    YCbCrLayout layout;
    Error error;
    auto ret = mMapper->lockYCbCr(buffer, usage, accessRegion,
            acquireFenceHandle,
            [&](const auto& tmpError, const auto& tmpLayout)
            {
                error = tmpError;
                if (error != Error::NONE) {
                    return;
                }

                layout = tmpLayout;
            });

    if (error == Error::NONE) {
        ycbcr->y = layout.y;
        ycbcr->cb = layout.cb;
        ycbcr->cr = layout.cr;
        ycbcr->ystride = static_cast<size_t>(layout.yStride);
        ycbcr->cstride = static_cast<size_t>(layout.cStride);
        ycbcr->chroma_step = static_cast<size_t>(layout.chromaStep);
    }

    // we own acquireFence even on errors
    if (acquireFence >= 0) {
        close(acquireFence);
    }

    return static_cast<int>((ret.isOk()) ? error : kTransactionError);
}

int Gralloc2Mapper::unlock(buffer_handle_t bufferHandle) const {
    if (!mMapper) {
        LOG_ALWAYS_FATAL("%s(), mMapper == nullptr", __FUNCTION__);
        return -EPIPE;
    }

    auto buffer = const_cast<native_handle_t*>(bufferHandle);

    int releaseFence = -1;
    Error error;
    auto ret = mMapper->unlock(buffer,
            [&](const auto& tmpError, const auto& tmpReleaseFence)
            {
                error = tmpError;
                if (error != Error::NONE) {
                    return;
                }

                auto fenceHandle = tmpReleaseFence.getNativeHandle();
                if (fenceHandle && fenceHandle->numFds == 1) {
                    int fd = dup(fenceHandle->data[0]);
                    if (fd >= 0) {
                        releaseFence = fd;
                    } else {
                        ALOGD("failed to dup unlock release fence");
                        sync_wait(fenceHandle->data[0], -1);
                    }
                }
            });

    error = (ret.isOk()) ? error : kTransactionError;
    if (error != Error::NONE) {
        ALOGE("unlock(%p) failed with %d", buffer, error);
    }

    return releaseFence;
}

} // namespace hwc
