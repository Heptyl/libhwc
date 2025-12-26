/*
 * Copyright 2019 The Android Open Source Project
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

#include <hwc_ui/Gralloc4.h>

#include <hidl/ServiceManagement.h>
#include <hwbinder/IPCThreadState.h>

#include <inttypes.h>
#include <log/log.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wzero-length-array"
#include <sync/sync.h>
#pragma clang diagnostic pop

using aidl::android::hardware::graphics::common::ExtendableType;
using aidl::android::hardware::graphics::common::PlaneLayoutComponentType;
using aidl::android::hardware::graphics::common::StandardMetadataType;
using android::hardware::hidl_vec;
using android::hardware::graphics::common::V1_2::BufferUsage;
using android::hardware::graphics::mapper::V4_0::BufferDescriptor;
using android::hardware::graphics::mapper::V4_0::Error;
using android::hardware::graphics::mapper::V4_0::IMapper;
using BufferDump = android::hardware::graphics::mapper::V4_0::IMapper::BufferDump;
using MetadataDump = android::hardware::graphics::mapper::V4_0::IMapper::MetadataDump;
using MetadataType = android::hardware::graphics::mapper::V4_0::IMapper::MetadataType;
using MetadataTypeDescription =
        android::hardware::graphics::mapper::V4_0::IMapper::MetadataTypeDescription;

namespace gralloc4 = android::gralloc4;

namespace hwc {

namespace {

static constexpr Error kTransactionError = Error::NO_RESOURCES;

uint64_t getValidUsageBits() {
    static const uint64_t validUsageBits = []() -> uint64_t {
        uint64_t bits = 0;
        for (const auto bit :
             android::hardware::hidl_enum_range<android::hardware::graphics::common::V1_2::BufferUsage>()) {
            bits = bits | bit;
        }
        return bits;
    }();
    return validUsageBits;
}

static inline IMapper::Rect sGralloc4Rect(const Rect& rect) {
    IMapper::Rect outRect{};
    outRect.left = rect.left;
    outRect.top = rect.top;
    outRect.width = rect.width();
    outRect.height = rect.height();
    return outRect;
}
static inline void sBufferDescriptorInfo(std::string name, uint32_t width, uint32_t height,
                                         PixelFormat format, uint32_t layerCount, uint64_t usage,
                                         IMapper::BufferDescriptorInfo* outDescriptorInfo) {
    outDescriptorInfo->name = name;
    outDescriptorInfo->width = width;
    outDescriptorInfo->height = height;
    outDescriptorInfo->layerCount = layerCount;
    outDescriptorInfo->format = static_cast<android::hardware::graphics::common::V1_2::PixelFormat>(format);
    outDescriptorInfo->usage = usage;
    outDescriptorInfo->reservedSize = 0;
}

} // anonymous namespace

Gralloc4Mapper::Gralloc4Mapper() {
    mMapper = IMapper::getService();
    if (mMapper == nullptr) {
        ALOGI("mapper 4.x is not supported");
        return;
    }
    if (mMapper->isRemote()) {
        LOG_ALWAYS_FATAL("gralloc-mapper must be in passthrough mode");
    }
}

bool Gralloc4Mapper::isLoaded() const {
    return mMapper != nullptr;
}

int Gralloc4Mapper::validateBufferDescriptorInfo(
    IMapper::BufferDescriptorInfo* descriptorInfo) const {
    uint64_t validUsageBits = getValidUsageBits();

    if (descriptorInfo->usage & ~validUsageBits) {
        ALOGE("buffer descriptor contains invalid usage bits 0x%" PRIx64,
              descriptorInfo->usage & ~validUsageBits);
        return -EINVAL;
    }
    return 0;
}

int Gralloc4Mapper::createDescriptor(void* bufferDescriptorInfo,
                                     void* outBufferDescriptor) const {
    if (!mMapper) {
        LOG_ALWAYS_FATAL("%s(), mMapper == nullptr", __FUNCTION__);
        return -EPIPE;
    }

    IMapper::BufferDescriptorInfo* descriptorInfo =
            static_cast<IMapper::BufferDescriptorInfo*>(bufferDescriptorInfo);
    BufferDescriptor* outDescriptor = static_cast<BufferDescriptor*>(outBufferDescriptor);

    int status = validateBufferDescriptorInfo(descriptorInfo);
    if (status) {
        return status;
    }

    Error error;
    auto hidl_cb = [&](const auto& tmpError, const auto& tmpDescriptor) {
        error = tmpError;
        if (error != Error::NONE) {
            return;
        }
        *outDescriptor = tmpDescriptor;
    };

    android::hardware::Return<void> ret = mMapper->createDescriptor(*descriptorInfo, hidl_cb);

    return static_cast<int>((ret.isOk()) ? error : kTransactionError);
}

int Gralloc4Mapper::importBuffer(const android::hardware::hidl_handle& rawHandle,
                                 buffer_handle_t* outBufferHandle) const {
    if (!mMapper) {
        LOG_ALWAYS_FATAL("%s(), mMapper == nullptr", __FUNCTION__);
        return -EPIPE;
    }

    Error error;
    auto ret = mMapper->importBuffer(rawHandle, [&](const auto& tmpError, const auto& tmpBuffer) {
        error = tmpError;
        if (error != Error::NONE) {
            return;
        }
        *outBufferHandle = static_cast<buffer_handle_t>(tmpBuffer);
    });

    return static_cast<int>((ret.isOk()) ? error : kTransactionError);
}

void Gralloc4Mapper::freeBuffer(buffer_handle_t bufferHandle) const {
    if (!mMapper) {
        LOG_ALWAYS_FATAL("%s(), mMapper == nullptr", __FUNCTION__);
        return;
    }

    auto buffer = const_cast<native_handle_t*>(bufferHandle);
    auto ret = mMapper->freeBuffer(buffer);

    auto error = (ret.isOk()) ? static_cast<Error>(ret) : kTransactionError;
    ALOGE_IF(error != Error::NONE, "freeBuffer(%p) failed with %d", buffer, error);
}

int Gralloc4Mapper::validateBufferSize(buffer_handle_t bufferHandle, uint32_t width,
                                       uint32_t height, PixelFormat format,
                                       uint32_t layerCount, uint64_t usage,
                                       uint32_t stride) const {
    if (!mMapper) {
        LOG_ALWAYS_FATAL("%s(), mMapper == nullptr", __FUNCTION__);
        return -EPIPE;
    }

    IMapper::BufferDescriptorInfo descriptorInfo;
    sBufferDescriptorInfo("validateBufferSize", width, height, format, layerCount, usage,
                          &descriptorInfo);

    auto buffer = const_cast<native_handle_t*>(bufferHandle);
    auto ret = mMapper->validateBufferSize(buffer, descriptorInfo, stride);

    return static_cast<int>((ret.isOk()) ? static_cast<Error>(ret) : kTransactionError);
}

void Gralloc4Mapper::getTransportSize(buffer_handle_t bufferHandle, uint32_t* outNumFds,
                                      uint32_t* outNumInts) const {
    if (!mMapper) {
        LOG_ALWAYS_FATAL("%s(), mMapper == nullptr", __FUNCTION__);
        return;
    }

    *outNumFds = uint32_t(bufferHandle->numFds);
    *outNumInts = uint32_t(bufferHandle->numInts);

    Error error;
    auto buffer = const_cast<native_handle_t*>(bufferHandle);
    auto ret = mMapper->getTransportSize(buffer,
                                         [&](const auto& tmpError, const auto& tmpNumFds,
                                             const auto& tmpNumInts) {
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

int Gralloc4Mapper::lock(buffer_handle_t bufferHandle, uint64_t usage, const Rect& bounds,
                         int acquireFence, void** outData, int32_t* outBytesPerPixel,
                         int32_t* outBytesPerStride) const {
    if (!mMapper) {
        LOG_ALWAYS_FATAL("%s(), mMapper == nullptr", __FUNCTION__);
        return -EPIPE;
    }

    std::vector<ui::PlaneLayout> planeLayouts;
    int err = getPlaneLayouts(bufferHandle, &planeLayouts);

    if (err == 0 && !planeLayouts.empty()) {
        if (outBytesPerPixel) {
            int32_t bitsPerPixel = static_cast<int32_t>(planeLayouts.front().sampleIncrementInBits);
            for (const auto& planeLayout : planeLayouts) {
                if (bitsPerPixel != planeLayout.sampleIncrementInBits) {
                    bitsPerPixel = -1;
                }
            }
            if (bitsPerPixel >= 0 && bitsPerPixel % 8 == 0) {
                *outBytesPerPixel = bitsPerPixel / 8;
            } else {
                *outBytesPerPixel = -1;
            }
        }
        if (outBytesPerStride) {
            int32_t bytesPerStride = static_cast<int32_t>(planeLayouts.front().strideInBytes);
            for (const auto& planeLayout : planeLayouts) {
                if (bytesPerStride != planeLayout.strideInBytes) {
                    bytesPerStride = -1;
                }
            }
            if (bytesPerStride >= 0) {
                *outBytesPerStride = bytesPerStride;
            } else {
                *outBytesPerStride = -1;
            }
        }
    }

    auto buffer = const_cast<native_handle_t*>(bufferHandle);

    IMapper::Rect accessRegion = sGralloc4Rect(bounds);

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
                             [&](const auto& tmpError, const auto& tmpData) {
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

int Gralloc4Mapper::lock(buffer_handle_t bufferHandle, uint64_t usage, const Rect& bounds,
                         int acquireFence, android_ycbcr* outYcbcr) const {
    if (!outYcbcr) {
        return -EINVAL;
    }

    std::vector<ui::PlaneLayout> planeLayouts;
    int error = getPlaneLayouts(bufferHandle, &planeLayouts);
    if (error != 0) {
        return error;
    }

    void* data = nullptr;
    error = lock(bufferHandle, usage, bounds, acquireFence, &data, nullptr, nullptr);
    if (error != 0) {
        return error;
    }

    android_ycbcr ycbcr;

    ycbcr.y = nullptr;
    ycbcr.cb = nullptr;
    ycbcr.cr = nullptr;
    ycbcr.ystride = 0;
    ycbcr.cstride = 0;
    ycbcr.chroma_step = 0;

    for (const auto& planeLayout : planeLayouts) {
        for (const auto& planeLayoutComponent : planeLayout.components) {
            if (!gralloc4::isStandardPlaneLayoutComponentType(planeLayoutComponent.type)) {
                continue;
            }
            if (0 != planeLayoutComponent.offsetInBits % 8) {
                unlock(bufferHandle);
                return -EINVAL;
            }

            uint8_t* tmpData = static_cast<uint8_t*>(data) + planeLayout.offsetInBytes +
                    (planeLayoutComponent.offsetInBits / 8);
            size_t sampleIncrementInBytes;

            auto type = static_cast<PlaneLayoutComponentType>(planeLayoutComponent.type.value);
            switch (type) {
                case PlaneLayoutComponentType::Y:
                    if ((ycbcr.y != nullptr) || (planeLayoutComponent.sizeInBits != 8) ||
                        (planeLayout.sampleIncrementInBits != 8)) {
                        unlock(bufferHandle);
                        return -EINVAL;
                    }
                    ycbcr.y = tmpData;
                    ycbcr.ystride = static_cast<size_t>(planeLayout.strideInBytes);
                    break;

                case PlaneLayoutComponentType::CB:
                case PlaneLayoutComponentType::CR:
                    if (planeLayout.sampleIncrementInBits % 8 != 0) {
                        unlock(bufferHandle);
                        return -EINVAL;
                    }

                    sampleIncrementInBytes = static_cast<size_t>(planeLayout.sampleIncrementInBits / 8);
                    if ((sampleIncrementInBytes != 1) && (sampleIncrementInBytes != 2)) {
                        unlock(bufferHandle);
                        return -EINVAL;
                    }

                    if (ycbcr.cstride == 0 && ycbcr.chroma_step == 0) {
                        ycbcr.cstride = static_cast<size_t>(planeLayout.strideInBytes);
                        ycbcr.chroma_step = sampleIncrementInBytes;
                    } else {
                        if ((static_cast<int64_t>(ycbcr.cstride) != planeLayout.strideInBytes) ||
                            (ycbcr.chroma_step != sampleIncrementInBytes)) {
                            unlock(bufferHandle);
                            return -EINVAL;
                        }
                    }

                    if (type == PlaneLayoutComponentType::CB) {
                        if (ycbcr.cb != nullptr) {
                            unlock(bufferHandle);
                            return -EINVAL;
                        }
                        ycbcr.cb = tmpData;
                    } else {
                        if (ycbcr.cr != nullptr) {
                            unlock(bufferHandle);
                            return -EINVAL;
                        }
                        ycbcr.cr = tmpData;
                    }
                    break;
                default:
                    break;
            };
        }
    }

    *outYcbcr = ycbcr;
    return static_cast<int>(Error::NONE);
}

int Gralloc4Mapper::unlock(buffer_handle_t bufferHandle) const {
    if (!mMapper) {
        LOG_ALWAYS_FATAL("%s(), mMapper == nullptr", __FUNCTION__);
        return -EPIPE;
    }

    auto buffer = const_cast<native_handle_t*>(bufferHandle);

    int releaseFence = -1;
    Error error;
    auto ret = mMapper->unlock(buffer, [&](const auto& tmpError, const auto& tmpReleaseFence) {
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

    if (!ret.isOk()) {
        error = kTransactionError;
    }

    if (error != Error::NONE) {
        ALOGE("unlock(%p) failed with %d", buffer, error);
    }

    return releaseFence;
}

int Gralloc4Mapper::isSupported(uint32_t width, uint32_t height, PixelFormat format,
                                uint32_t layerCount, uint64_t usage,
                                bool* outSupported) const {
    if (!mMapper) {
        LOG_ALWAYS_FATAL("%s(), mMapper == nullptr", __FUNCTION__);
        return -EPIPE;
    }

    IMapper::BufferDescriptorInfo descriptorInfo;
    sBufferDescriptorInfo("isSupported", width, height, format, layerCount, usage, &descriptorInfo);

    Error error;
    auto ret = mMapper->isSupported(descriptorInfo,
                                    [&](const auto& tmpError, const auto& tmpSupported) {
                                        error = tmpError;
                                        if (error != Error::NONE) {
                                            return;
                                        }
                                        if (outSupported) {
                                            *outSupported = tmpSupported;
                                        }
                                    });

    if (!ret.isOk()) {
        error = kTransactionError;
    }

    if (error != Error::NONE) {
        ALOGE("isSupported(%u, %u, %d, %u, ...) failed with %d", width, height, format, layerCount,
              error);
    }

    return static_cast<int>(error);
}

template <class T>
int Gralloc4Mapper::get(buffer_handle_t bufferHandle, const MetadataType& metadataType,
                        DecodeFunction<T> decodeFunction, T* outMetadata) const {
    if (!mMapper) {
        LOG_ALWAYS_FATAL("%s(), mMapper == nullptr", __FUNCTION__);
        return -EPIPE;
    }

    if (!outMetadata) {
        return -EINVAL;
    }

    hidl_vec<uint8_t> vec;
    Error error;
    auto ret = mMapper->get(const_cast<native_handle_t*>(bufferHandle), metadataType,
                            [&](const auto& tmpError, const hidl_vec<uint8_t>& tmpVec) {
                                error = tmpError;
                                vec = tmpVec;
                            });

    if (!ret.isOk()) {
        error = kTransactionError;
    }

    if (error != Error::NONE) {
        ALOGE("get(%s, %" PRIu64 ", ...) failed with %d", metadataType.name.c_str(),
              metadataType.value, error);
        return static_cast<int>(error);
    }

    return decodeFunction(vec, outMetadata);
}

int Gralloc4Mapper::getBufferId(buffer_handle_t bufferHandle, uint64_t* outBufferId) const {
    return get(bufferHandle, gralloc4::MetadataType_BufferId, android::gralloc4::decodeBufferId,
               outBufferId);
}

int Gralloc4Mapper::getName(buffer_handle_t bufferHandle, std::string* outName) const {
    return get(bufferHandle, gralloc4::MetadataType_Name, android::gralloc4::decodeName, outName);
}

int Gralloc4Mapper::getWidth(buffer_handle_t bufferHandle, uint64_t* outWidth) const {
    return get(bufferHandle, gralloc4::MetadataType_Width, android::gralloc4::decodeWidth, outWidth);
}

int Gralloc4Mapper::getHeight(buffer_handle_t bufferHandle, uint64_t* outHeight) const {
    return get(bufferHandle, gralloc4::MetadataType_Height, android::gralloc4::decodeHeight, outHeight);
}

int Gralloc4Mapper::getLayerCount(buffer_handle_t bufferHandle,
                                  uint64_t* outLayerCount) const {
    return get(bufferHandle, gralloc4::MetadataType_LayerCount, gralloc4::decodeLayerCount,
               outLayerCount);
}

int Gralloc4Mapper::getPixelFormatRequested(buffer_handle_t bufferHandle,
                                            ui::PixelFormat* outPixelFormatRequested) const {
    return get(bufferHandle, gralloc4::MetadataType_PixelFormatRequested,
               gralloc4::decodePixelFormatRequested, outPixelFormatRequested);
}

int Gralloc4Mapper::getPixelFormatFourCC(buffer_handle_t bufferHandle,
                                         uint32_t* outPixelFormatFourCC) const {
    return get(bufferHandle, gralloc4::MetadataType_PixelFormatFourCC,
               gralloc4::decodePixelFormatFourCC, outPixelFormatFourCC);
}

int Gralloc4Mapper::getPixelFormatModifier(buffer_handle_t bufferHandle,
                                           uint64_t* outPixelFormatModifier) const {
    return get(bufferHandle, gralloc4::MetadataType_PixelFormatModifier,
               gralloc4::decodePixelFormatModifier, outPixelFormatModifier);
}

int Gralloc4Mapper::getUsage(buffer_handle_t bufferHandle, uint64_t* outUsage) const {
    return get(bufferHandle, gralloc4::MetadataType_Usage, gralloc4::decodeUsage, outUsage);
}

int Gralloc4Mapper::getAllocationSize(buffer_handle_t bufferHandle,
                                      uint64_t* outAllocationSize) const {
    return get(bufferHandle, gralloc4::MetadataType_AllocationSize, gralloc4::decodeAllocationSize,
               outAllocationSize);
}

int Gralloc4Mapper::getProtectedContent(buffer_handle_t bufferHandle,
                                        uint64_t* outProtectedContent) const {
    return get(bufferHandle, gralloc4::MetadataType_ProtectedContent,
               gralloc4::decodeProtectedContent, outProtectedContent);
}

int Gralloc4Mapper::getCompression(buffer_handle_t bufferHandle,
                                   ExtendableType* outCompression) const {
    return get(bufferHandle, gralloc4::MetadataType_Compression, gralloc4::decodeCompression,
               outCompression);
}

int Gralloc4Mapper::getCompression(buffer_handle_t bufferHandle,
                                   ui::Compression* outCompression) const {
    if (!outCompression) {
        return -EINVAL;
    }
    ExtendableType compression;
    int error = getCompression(bufferHandle, &compression);
    if (error) {
        return error;
    }
    if (!gralloc4::isStandardCompression(compression)) {
        return -EPROTO;
    }
    *outCompression = gralloc4::getStandardCompressionValue(compression);
    return 0;
}

int Gralloc4Mapper::getInterlaced(buffer_handle_t bufferHandle,
                                  ExtendableType* outInterlaced) const {
    return get(bufferHandle, gralloc4::MetadataType_Interlaced, gralloc4::decodeInterlaced,
               outInterlaced);
}

int Gralloc4Mapper::getInterlaced(buffer_handle_t bufferHandle,
                                  ui::Interlaced* outInterlaced) const {
    if (!outInterlaced) {
        return -EINVAL;
    }
    ExtendableType interlaced;
    int error = getInterlaced(bufferHandle, &interlaced);
    if (error) {
        return error;
    }
    if (!gralloc4::isStandardInterlaced(interlaced)) {
        return -EPROTO;
    }
    *outInterlaced = gralloc4::getStandardInterlacedValue(interlaced);
    return 0;
}

int Gralloc4Mapper::getChromaSiting(buffer_handle_t bufferHandle,
                                    ExtendableType* outChromaSiting) const {
    return get(bufferHandle, gralloc4::MetadataType_ChromaSiting, gralloc4::decodeChromaSiting,
               outChromaSiting);
}

int Gralloc4Mapper::getChromaSiting(buffer_handle_t bufferHandle,
                                    ui::ChromaSiting* outChromaSiting) const {
    if (!outChromaSiting) {
        return -EINVAL;
    }
    ExtendableType chromaSiting;
    int error = getChromaSiting(bufferHandle, &chromaSiting);
    if (error) {
        return error;
    }
    if (!gralloc4::isStandardChromaSiting(chromaSiting)) {
        return -EPROTO;
    }
    *outChromaSiting = gralloc4::getStandardChromaSitingValue(chromaSiting);
    return 0;
}

int Gralloc4Mapper::getPlaneLayouts(buffer_handle_t bufferHandle,
                                    std::vector<ui::PlaneLayout>* outPlaneLayouts) const {
    return get(bufferHandle, gralloc4::MetadataType_PlaneLayouts, gralloc4::decodePlaneLayouts,
               outPlaneLayouts);
}

int Gralloc4Mapper::getDataspace(buffer_handle_t bufferHandle,
                                 ui::Dataspace* outDataspace) const {
    if (!outDataspace) {
        return -EINVAL;
    }
    aidl::android::hardware::graphics::common::Dataspace dataspace;
    int error = get(bufferHandle, gralloc4::MetadataType_Dataspace, gralloc4::decodeDataspace,
                    &dataspace);
    if (error) {
        return error;
    }

    // Gralloc4 uses stable AIDL dataspace but the rest of the system still uses HIDL dataspace
    *outDataspace = static_cast<ui::Dataspace>(dataspace);
    return 0;
}

int Gralloc4Mapper::getBlendMode(buffer_handle_t bufferHandle,
                                 ui::BlendMode* outBlendMode) const {
    return get(bufferHandle, gralloc4::MetadataType_BlendMode, gralloc4::decodeBlendMode,
               outBlendMode);
}

int Gralloc4Mapper::getSmpte2086(buffer_handle_t bufferHandle,
                                 std::optional<ui::Smpte2086>* outSmpte2086) const {
    return get(bufferHandle, gralloc4::MetadataType_Smpte2086, gralloc4::decodeSmpte2086,
               outSmpte2086);
}

int Gralloc4Mapper::getCta861_3(buffer_handle_t bufferHandle,
                                std::optional<ui::Cta861_3>* outCta861_3) const {
    return get(bufferHandle, gralloc4::MetadataType_Cta861_3, gralloc4::decodeCta861_3,
               outCta861_3);
}

int Gralloc4Mapper::getSmpte2094_40(buffer_handle_t bufferHandle,
                                    std::optional<std::vector<uint8_t>>* outSmpte2094_40) const {
    return get(bufferHandle, gralloc4::MetadataType_Smpte2094_40, gralloc4::decodeSmpte2094_40,
               outSmpte2094_40);
}

template <class T>
int Gralloc4Mapper::getDefault(uint32_t width, uint32_t height, PixelFormat format,
                               uint32_t layerCount, uint64_t usage,
                               const MetadataType& metadataType,
                               DecodeFunction<T> decodeFunction, T* outMetadata) const {
    if (!mMapper) {
        LOG_ALWAYS_FATAL("%s(), mMapper == nullptr", __FUNCTION__);
        return -EPIPE;
    }

    if (!outMetadata) {
        return -EINVAL;
    }

    IMapper::BufferDescriptorInfo descriptorInfo;
    sBufferDescriptorInfo("getDefault", width, height, format, layerCount, usage, &descriptorInfo);

    hidl_vec<uint8_t> vec;
    Error error;
    auto ret = mMapper->getFromBufferDescriptorInfo(descriptorInfo, metadataType,
                                                    [&](const auto& tmpError,
                                                        const hidl_vec<uint8_t>& tmpVec) {
                                                        error = tmpError;
                                                        vec = tmpVec;
                                                    });

    if (!ret.isOk()) {
        error = kTransactionError;
    }

    if (error != Error::NONE) {
        ALOGE("getDefault(%s, %" PRIu64 ", ...) failed with %d", metadataType.name.c_str(),
              metadataType.value, error);
        return static_cast<int>(error);
    }

    return decodeFunction(vec, outMetadata);
}

int Gralloc4Mapper::getDefaultPixelFormatFourCC(uint32_t width, uint32_t height,
                                                PixelFormat format, uint32_t layerCount,
                                                uint64_t usage,
                                                uint32_t* outPixelFormatFourCC) const {
    return getDefault(width, height, format, layerCount, usage,
                      gralloc4::MetadataType_PixelFormatFourCC, gralloc4::decodePixelFormatFourCC,
                      outPixelFormatFourCC);
}

int Gralloc4Mapper::getDefaultPixelFormatModifier(uint32_t width, uint32_t height,
                                                  PixelFormat format, uint32_t layerCount,
                                                  uint64_t usage,
                                                  uint64_t* outPixelFormatModifier) const {
    return getDefault(width, height, format, layerCount, usage,
                      gralloc4::MetadataType_PixelFormatModifier,
                      gralloc4::decodePixelFormatModifier, outPixelFormatModifier);
}

int Gralloc4Mapper::getDefaultAllocationSize(uint32_t width, uint32_t height,
                                             PixelFormat format, uint32_t layerCount,
                                             uint64_t usage,
                                             uint64_t* outAllocationSize) const {
    return getDefault(width, height, format, layerCount, usage,
                      gralloc4::MetadataType_AllocationSize, gralloc4::decodeAllocationSize,
                      outAllocationSize);
}

int Gralloc4Mapper::getDefaultProtectedContent(uint32_t width, uint32_t height,
                                               PixelFormat format, uint32_t layerCount,
                                               uint64_t usage,
                                               uint64_t* outProtectedContent) const {
    return getDefault(width, height, format, layerCount, usage,
                      gralloc4::MetadataType_ProtectedContent, gralloc4::decodeProtectedContent,
                      outProtectedContent);
}

int Gralloc4Mapper::getDefaultCompression(uint32_t width, uint32_t height, PixelFormat format,
                                          uint32_t layerCount, uint64_t usage,
                                          ExtendableType* outCompression) const {
    return getDefault(width, height, format, layerCount, usage, gralloc4::MetadataType_Compression,
                      gralloc4::decodeCompression, outCompression);
}

int Gralloc4Mapper::getDefaultCompression(uint32_t width, uint32_t height, PixelFormat format,
                                          uint32_t layerCount, uint64_t usage,
                                          ui::Compression* outCompression) const {
    if (!outCompression) {
        return -EINVAL;
    }
    ExtendableType compression;
    int error = getDefaultCompression(width, height, format, layerCount, usage, &compression);
    if (error) {
        return error;
    }
    if (!gralloc4::isStandardCompression(compression)) {
        return -EPROTO;
    }
    *outCompression = gralloc4::getStandardCompressionValue(compression);
    return 0;
}

int Gralloc4Mapper::getDefaultInterlaced(uint32_t width, uint32_t height, PixelFormat format,
                                         uint32_t layerCount, uint64_t usage,
                                         ExtendableType* outInterlaced) const {
    return getDefault(width, height, format, layerCount, usage, gralloc4::MetadataType_Interlaced,
                      gralloc4::decodeInterlaced, outInterlaced);
}

int Gralloc4Mapper::getDefaultInterlaced(uint32_t width, uint32_t height, PixelFormat format,
                                         uint32_t layerCount, uint64_t usage,
                                         ui::Interlaced* outInterlaced) const {
    if (!outInterlaced) {
        return -EINVAL;
    }
    ExtendableType interlaced;
    int error = getDefaultInterlaced(width, height, format, layerCount, usage, &interlaced);
    if (error) {
        return error;
    }
    if (!gralloc4::isStandardInterlaced(interlaced)) {
        return -EPROTO;
    }
    *outInterlaced = gralloc4::getStandardInterlacedValue(interlaced);
    return 0;
}

int Gralloc4Mapper::getDefaultChromaSiting(uint32_t width, uint32_t height, PixelFormat format,
                                           uint32_t layerCount, uint64_t usage,
                                           ExtendableType* outChromaSiting) const {
    return getDefault(width, height, format, layerCount, usage, gralloc4::MetadataType_ChromaSiting,
                      gralloc4::decodeChromaSiting, outChromaSiting);
}

int Gralloc4Mapper::getDefaultChromaSiting(uint32_t width, uint32_t height, PixelFormat format,
                                           uint32_t layerCount, uint64_t usage,
                                           ui::ChromaSiting* outChromaSiting) const {
    if (!outChromaSiting) {
        return -EINVAL;
    }
    ExtendableType chromaSiting;
    int error =
            getDefaultChromaSiting(width, height, format, layerCount, usage, &chromaSiting);
    if (error) {
        return error;
    }
    if (!gralloc4::isStandardChromaSiting(chromaSiting)) {
        return -EPROTO;
    }
    *outChromaSiting = gralloc4::getStandardChromaSitingValue(chromaSiting);
    return 0;
}

int Gralloc4Mapper::getDefaultPlaneLayouts(
        uint32_t width, uint32_t height, PixelFormat format, uint32_t layerCount, uint64_t usage,
        std::vector<ui::PlaneLayout>* outPlaneLayouts) const {
    return getDefault(width, height, format, layerCount, usage, gralloc4::MetadataType_PlaneLayouts,
                      gralloc4::decodePlaneLayouts, outPlaneLayouts);
}

std::vector<MetadataTypeDescription> Gralloc4Mapper::listSupportedMetadataTypes() const {
    if (!mMapper) {
        LOG_ALWAYS_FATAL("%s(), mMapper == nullptr", __FUNCTION__);
        return {};
    }

    hidl_vec<MetadataTypeDescription> descriptions;
    Error error;
    auto ret = mMapper->listSupportedMetadataTypes(
            [&](const auto& tmpError, const auto& tmpDescriptions) {
                error = tmpError;
                descriptions = tmpDescriptions;
            });

    if (!ret.isOk()) {
        error = kTransactionError;
    }

    if (error != Error::NONE) {
        ALOGE("listSupportedMetadataType() failed with %d", error);
        return {};
    }

    return static_cast<std::vector<MetadataTypeDescription>>(descriptions);
}

template <class T>
int Gralloc4Mapper::metadataDumpHelper(const BufferDump& bufferDump,
                                       StandardMetadataType metadataType,
                                       DecodeFunction<T> decodeFunction, T* outT) const {
    const auto& metadataDump = bufferDump.metadataDump;

    auto itr =
            std::find_if(metadataDump.begin(), metadataDump.end(),
                         [&](const MetadataDump& tmpMetadataDump) {
                             if (!gralloc4::isStandardMetadataType(tmpMetadataDump.metadataType)) {
                                 return false;
                             }
                             return metadataType ==
                                     gralloc4::getStandardMetadataTypeValue(
                                             tmpMetadataDump.metadataType);
                         });
    if (itr == metadataDump.end()) {
        return -EINVAL;
    }

    return decodeFunction(itr->metadata, outT);
}

int Gralloc4Mapper::bufferDumpHelper(const BufferDump& bufferDump, std::ostringstream* outDump,
                                     uint64_t* outAllocationSize, bool less) const {
    uint64_t bufferId;
    std::string name;
    uint64_t width;
    uint64_t height;
    uint64_t layerCount;
    ui::PixelFormat pixelFormatRequested;
    uint32_t pixelFormatFourCC;
    uint64_t pixelFormatModifier;
    uint64_t usage;
    uint64_t allocationSize;
    uint64_t protectedContent;
    ExtendableType compression;
    ExtendableType interlaced;
    ExtendableType chromaSiting;
    std::vector<ui::PlaneLayout> planeLayouts;

    int error = metadataDumpHelper(bufferDump, StandardMetadataType::BUFFER_ID,
                                   gralloc4::decodeBufferId, &bufferId);
    if (error != 0) {
        return error;
    }
    error = metadataDumpHelper(bufferDump, StandardMetadataType::NAME, gralloc4::decodeName, &name);
    if (error != 0) {
        return error;
    }
    error = metadataDumpHelper(bufferDump, StandardMetadataType::WIDTH, gralloc4::decodeWidth,
                               &width);
    if (error != 0) {
        return error;
    }
    error = metadataDumpHelper(bufferDump, StandardMetadataType::HEIGHT, gralloc4::decodeHeight,
                               &height);
    if (error != 0) {
        return error;
    }
    error = metadataDumpHelper(bufferDump, StandardMetadataType::LAYER_COUNT,
                               gralloc4::decodeLayerCount, &layerCount);
    if (error != 0) {
        return error;
    }
    error = metadataDumpHelper(bufferDump, StandardMetadataType::PIXEL_FORMAT_REQUESTED,
                               gralloc4::decodePixelFormatRequested, &pixelFormatRequested);
    if (error != 0) {
        return error;
    }
    error = metadataDumpHelper(bufferDump, StandardMetadataType::PIXEL_FORMAT_FOURCC,
                               gralloc4::decodePixelFormatFourCC, &pixelFormatFourCC);
    if (error != 0) {
        return error;
    }
    error = metadataDumpHelper(bufferDump, StandardMetadataType::PIXEL_FORMAT_MODIFIER,
                               gralloc4::decodePixelFormatModifier, &pixelFormatModifier);
    if (error != 0) {
        return error;
    }
    error = metadataDumpHelper(bufferDump, StandardMetadataType::USAGE, gralloc4::decodeUsage,
                               &usage);
    if (error != 0) {
        return error;
    }
    error = metadataDumpHelper(bufferDump, StandardMetadataType::ALLOCATION_SIZE,
                               gralloc4::decodeAllocationSize, &allocationSize);
    if (error != 0) {
        return error;
    }
    error = metadataDumpHelper(bufferDump, StandardMetadataType::PROTECTED_CONTENT,
                               gralloc4::decodeProtectedContent, &protectedContent);
    if (error != 0) {
        return error;
    }
    error = metadataDumpHelper(bufferDump, StandardMetadataType::COMPRESSION,
                               gralloc4::decodeCompression, &compression);
    if (error != 0) {
        return error;
    }
    error = metadataDumpHelper(bufferDump, StandardMetadataType::INTERLACED,
                               gralloc4::decodeInterlaced, &interlaced);
    if (error != 0) {
        return error;
    }
    error = metadataDumpHelper(bufferDump, StandardMetadataType::CHROMA_SITING,
                               gralloc4::decodeChromaSiting, &chromaSiting);
    if (error != 0) {
        return error;
    }
    error = metadataDumpHelper(bufferDump, StandardMetadataType::PLANE_LAYOUTS,
                               gralloc4::decodePlaneLayouts, &planeLayouts);
    if (error != 0) {
        return error;
    }

    if (outAllocationSize) {
        *outAllocationSize = allocationSize;
    }
    double allocationSizeKiB = static_cast<double>(allocationSize) / 1024;

    *outDump << "+ name:" << name << ", id:" << bufferId << ", size:" << allocationSizeKiB
             << "KiB, w/h:" << width << "x" << height << ", usage: 0x" << std::hex << usage
             << std::dec << ", req fmt:" << static_cast<int32_t>(pixelFormatRequested)
             << ", fourcc/mod:" << pixelFormatFourCC << "/" << pixelFormatModifier
             << ", compressed: ";

    if (less) {
        bool isCompressed = !gralloc4::isStandardCompression(compression) ||
                (gralloc4::getStandardCompressionValue(compression) != ui::Compression::NONE);
        *outDump << std::boolalpha << isCompressed << "\n";
    } else {
        *outDump << gralloc4::getCompressionName(compression) << "\n";
    }

    bool firstPlane = true;
    for (const auto& planeLayout : planeLayouts) {
        if (firstPlane) {
            firstPlane = false;
            *outDump << "\tplanes: ";
        } else {
            *outDump << "\t        ";
        }

        for (size_t i = 0; i < planeLayout.components.size(); i++) {
            const auto& planeLayoutComponent = planeLayout.components[i];
            *outDump << gralloc4::getPlaneLayoutComponentTypeName(planeLayoutComponent.type);
            if (i < planeLayout.components.size() - 1) {
                *outDump << "/";
            } else {
                *outDump << ":\t";
            }
        }
        *outDump << " w/h:" << planeLayout.widthInSamples << "x" << planeLayout.heightInSamples
                 << ", stride:" << planeLayout.strideInBytes
                 << " bytes, size:" << planeLayout.totalSizeInBytes;
        if (!less) {
            *outDump << ", inc:" << planeLayout.sampleIncrementInBits
                     << " bits, subsampling w/h:" << planeLayout.horizontalSubsampling << "x"
                     << planeLayout.verticalSubsampling;
        }
        *outDump << "\n";
    }

    if (!less) {
        *outDump << "\tlayer cnt: " << layerCount << ", protected content: " << protectedContent
                 << ", interlaced: " << gralloc4::getInterlacedName(interlaced)
                 << ", chroma siting:" << gralloc4::getChromaSitingName(chromaSiting) << "\n";
    }

    return 0;
}

std::string Gralloc4Mapper::dumpBuffer(buffer_handle_t bufferHandle, bool less) const {
    if (!mMapper) {
        LOG_ALWAYS_FATAL("%s(), mMapper == nullptr", __FUNCTION__);
        return "";
    }

    auto buffer = const_cast<native_handle_t*>(bufferHandle);

    BufferDump bufferDump;
    Error error;
    auto ret = mMapper->dumpBuffer(buffer, [&](const auto& tmpError, const auto& tmpBufferDump) {
        error = tmpError;
        bufferDump = tmpBufferDump;
    });

    if (!ret.isOk()) {
        error = kTransactionError;
    }

    if (error != Error::NONE) {
        ALOGE("dumpBuffer() failed with %d", error);
        return "";
    }

    std::ostringstream stream;
    stream.precision(2);

    int err = bufferDumpHelper(bufferDump, &stream, nullptr, less);
    if (err != 0) {
        ALOGE("bufferDumpHelper() failed with %d", err);
        return "";
    }

    return stream.str();
}

std::string Gralloc4Mapper::dumpBuffers(bool less) const {
    if (!mMapper) {
        LOG_ALWAYS_FATAL("%s(), mMapper == nullptr", __FUNCTION__);
        return "";
    }

    hidl_vec<BufferDump> bufferDumps;
    Error error;
    auto ret = mMapper->dumpBuffers([&](const auto& tmpError, const auto& tmpBufferDump) {
        error = tmpError;
        bufferDumps = tmpBufferDump;
    });

    if (!ret.isOk()) {
        error = kTransactionError;
    }

    if (error != Error::NONE) {
        ALOGE("dumpBuffer() failed with %d", error);
        return "";
    }

    uint64_t totalAllocationSize = 0;
    std::ostringstream stream;
    stream.precision(2);

    stream << "Imported gralloc buffers:\n";

    for (const auto& bufferDump : bufferDumps) {
        uint64_t allocationSize = 0;
        int err = bufferDumpHelper(bufferDump, &stream, &allocationSize, less);
        if (err != 0) {
            ALOGE("bufferDumpHelper() failed with %d", err);
            return "";
        }
        totalAllocationSize += allocationSize;
    }

    double totalAllocationSizeKiB = static_cast<double>(totalAllocationSize) / 1024;
    stream << "Total imported by gralloc: " << totalAllocationSizeKiB << "KiB\n";
    return stream.str();
}

} // namespace hwc
