/*
 * Copyright (C) 2007 The Android Open Source Project
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

#define ATRACE_TAG ATRACE_TAG_GRAPHICS
//#define LOG_NDEBUG 0

#include <hwc_ui/GraphicBufferMapper.h>

#include <grallocusage/GrallocUsageConversion.h>

// We would eliminate the non-conforming zero-length array, but we can't since
// this is effectively included from the Linux kernel
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wzero-length-array"
#include <sync/sync.h>
#pragma clang diagnostic pop

#include <utils/Log.h>
#include <utils/Trace.h>

#include <hwc_ui/Gralloc.h>
#include <hwc_ui/Gralloc2.h>
#include <hwc_ui/Gralloc4.h>

#include <system/graphics.h>

namespace hwc {
// ---------------------------------------------------------------------------

GraphicBufferMapper& GraphicBufferMapper::getInstance()
{
    static GraphicBufferMapper gInstance;
    return gInstance;
}

GraphicBufferMapper::GraphicBufferMapper() {
    mMapper = std::make_unique<const Gralloc4Mapper>();
    if (mMapper->isLoaded()) {
        mMapperVersion = Version::GRALLOC_4;
        return;
    }
    mMapper = std::make_unique<const Gralloc2Mapper>();
    if (mMapper->isLoaded()) {
        mMapperVersion = Version::GRALLOC_2;
        return;
    }

    mMapperVersion = Version::INVALID;
}

void GraphicBufferMapper::dumpBuffer(buffer_handle_t bufferHandle, std::string& result,
                                     bool less) const {
    result.append(mMapper->dumpBuffer(bufferHandle, less));
}

void GraphicBufferMapper::dumpBufferToSystemLog(buffer_handle_t bufferHandle, bool less) {
    std::string s;
    GraphicBufferMapper::getInstance().dumpBuffer(bufferHandle, s, less);
    ALOGD("%s", s.c_str());
}

int GraphicBufferMapper::importBuffer(buffer_handle_t rawHandle,
        uint32_t width, uint32_t height, uint32_t layerCount,
        PixelFormat format, uint64_t usage, uint32_t stride,
        buffer_handle_t* outHandle)
{
    ATRACE_CALL();

    buffer_handle_t bufferHandle;
    int error = mMapper->importBuffer(android::hardware::hidl_handle(rawHandle), &bufferHandle);
    if (error != 0) {
        ALOGW("importBuffer(%p) failed: %d", rawHandle, error);
        return error;
    }

    error = mMapper->validateBufferSize(bufferHandle, width, height, format, layerCount, usage,
                                        stride);
    if (error != 0) {
        ALOGE("validateBufferSize(%p) failed: %d", rawHandle, error);
        freeBuffer(bufferHandle);
        return static_cast<int>(error);
    }

    *outHandle = bufferHandle;

    return 0;
}

int GraphicBufferMapper::importBuffer(buffer_handle_t rawHandle,
                                      buffer_handle_t* outHandle)
{
    ATRACE_CALL();

    buffer_handle_t bufferHandle;
    int error = mMapper->importBuffer(android::hardware::hidl_handle(rawHandle), &bufferHandle);
    if (error != 0) {
        ALOGW("importBuffer(%p) failed: %d", rawHandle, error);
        return error;
    }

    *outHandle = bufferHandle;

    return 0;
}

void GraphicBufferMapper::getTransportSize(buffer_handle_t handle,
            uint32_t* outTransportNumFds, uint32_t* outTransportNumInts)
{
    mMapper->getTransportSize(handle, outTransportNumFds, outTransportNumInts);
}

int GraphicBufferMapper::freeBuffer(buffer_handle_t handle)
{
    ATRACE_CALL();

    mMapper->freeBuffer(handle);

    return 0;
}

int GraphicBufferMapper::lock(buffer_handle_t handle, uint32_t usage, const Rect& bounds,
                              void** vaddr, int32_t* outBytesPerPixel,
                              int32_t* outBytesPerStride) {
    return lockAsync(handle, usage, bounds, vaddr, -1, outBytesPerPixel, outBytesPerStride);
}

int GraphicBufferMapper::lockYCbCr(buffer_handle_t handle, uint32_t usage,
        const Rect& bounds, android_ycbcr *ycbcr)
{
    return lockAsyncYCbCr(handle, usage, bounds, ycbcr, -1);
}

int GraphicBufferMapper::unlock(buffer_handle_t handle)
{
    int32_t fenceFd = -1;
    int error = unlockAsync(handle, &fenceFd);
    if (error == 0 && fenceFd >= 0) {
        sync_wait(fenceFd, -1);
        close(fenceFd);
    }
    return error;
}

int GraphicBufferMapper::lockAsync(buffer_handle_t handle, uint32_t usage, const Rect& bounds,
                                   void** vaddr, int fenceFd, int32_t* outBytesPerPixel,
                                   int32_t* outBytesPerStride) {
    return lockAsync(handle, usage, usage, bounds, vaddr, fenceFd, outBytesPerPixel,
                     outBytesPerStride);
}

int GraphicBufferMapper::lockAsync(buffer_handle_t handle, uint64_t producerUsage,
                                   uint64_t consumerUsage, const Rect& bounds, void** vaddr,
                                   int fenceFd, int32_t* outBytesPerPixel,
                                   int32_t* outBytesPerStride) {
    ATRACE_CALL();

    const uint64_t usage = static_cast<uint64_t>(
            android_convertGralloc1To0Usage(producerUsage, consumerUsage));
    return mMapper->lock(handle, usage, bounds, fenceFd, vaddr, outBytesPerPixel,
                         outBytesPerStride);
}

int GraphicBufferMapper::lockAsyncYCbCr(buffer_handle_t handle,
        uint32_t usage, const Rect& bounds, android_ycbcr *ycbcr, int fenceFd)
{
    ATRACE_CALL();

    return mMapper->lock(handle, usage, bounds, fenceFd, ycbcr);
}

int GraphicBufferMapper::unlockAsync(buffer_handle_t handle, int *fenceFd)
{
    ATRACE_CALL();

    *fenceFd = mMapper->unlock(handle);

    return 0;
}

int GraphicBufferMapper::isSupported(uint32_t width, uint32_t height,
                                     PixelFormat format, uint32_t layerCount,
                                     uint64_t usage, bool* outSupported) {
    return mMapper->isSupported(width, height, format, layerCount, usage, outSupported);
}

int GraphicBufferMapper::getBufferId(buffer_handle_t bufferHandle, uint64_t* outBufferId) {
    return mMapper->getBufferId(bufferHandle, outBufferId);
}

int GraphicBufferMapper::getName(buffer_handle_t bufferHandle, std::string* outName) {
    return mMapper->getName(bufferHandle, outName);
}

int GraphicBufferMapper::getWidth(buffer_handle_t bufferHandle, uint64_t* outWidth) {
    return mMapper->getWidth(bufferHandle, outWidth);
}

int GraphicBufferMapper::getHeight(buffer_handle_t bufferHandle, uint64_t* outHeight) {
    return mMapper->getHeight(bufferHandle, outHeight);
}

int GraphicBufferMapper::getLayerCount(buffer_handle_t bufferHandle, uint64_t* outLayerCount) {
    return mMapper->getLayerCount(bufferHandle, outLayerCount);
}

int GraphicBufferMapper::getPixelFormatRequested(buffer_handle_t bufferHandle,
                                                 ui::PixelFormat* outPixelFormatRequested) {
    return mMapper->getPixelFormatRequested(bufferHandle, outPixelFormatRequested);
}

int GraphicBufferMapper::getPixelFormatFourCC(buffer_handle_t bufferHandle,
                                              uint32_t* outPixelFormatFourCC) {
    return mMapper->getPixelFormatFourCC(bufferHandle, outPixelFormatFourCC);
}

int GraphicBufferMapper::getPixelFormatModifier(buffer_handle_t bufferHandle,
                                                uint64_t* outPixelFormatModifier) {
    return mMapper->getPixelFormatModifier(bufferHandle, outPixelFormatModifier);
}

int GraphicBufferMapper::getUsage(buffer_handle_t bufferHandle, uint64_t* outUsage) {
    return mMapper->getUsage(bufferHandle, outUsage);
}

int GraphicBufferMapper::getAllocationSize(buffer_handle_t bufferHandle,
                                           uint64_t* outAllocationSize) {
    return mMapper->getAllocationSize(bufferHandle, outAllocationSize);
}

int GraphicBufferMapper::getProtectedContent(buffer_handle_t bufferHandle,
                                             uint64_t* outProtectedContent) {
    return mMapper->getProtectedContent(bufferHandle, outProtectedContent);
}

int GraphicBufferMapper::getCompression(
        buffer_handle_t bufferHandle,
        aidl::android::hardware::graphics::common::ExtendableType* outCompression) {
    return mMapper->getCompression(bufferHandle, outCompression);
}

int GraphicBufferMapper::getCompression(buffer_handle_t bufferHandle,
                                        ui::Compression* outCompression) {
    return mMapper->getCompression(bufferHandle, outCompression);
}

int GraphicBufferMapper::getInterlaced(
        buffer_handle_t bufferHandle,
        aidl::android::hardware::graphics::common::ExtendableType* outInterlaced) {
    return mMapper->getInterlaced(bufferHandle, outInterlaced);
}

int GraphicBufferMapper::getInterlaced(buffer_handle_t bufferHandle,
                                       ui::Interlaced* outInterlaced) {
    return mMapper->getInterlaced(bufferHandle, outInterlaced);
}

int GraphicBufferMapper::getChromaSiting(
        buffer_handle_t bufferHandle,
        aidl::android::hardware::graphics::common::ExtendableType* outChromaSiting) {
    return mMapper->getChromaSiting(bufferHandle, outChromaSiting);
}

int GraphicBufferMapper::getChromaSiting(buffer_handle_t bufferHandle,
                                         ui::ChromaSiting* outChromaSiting) {
    return mMapper->getChromaSiting(bufferHandle, outChromaSiting);
}

int GraphicBufferMapper::getPlaneLayouts(buffer_handle_t bufferHandle,
                                         std::vector<ui::PlaneLayout>* outPlaneLayouts) {
    return mMapper->getPlaneLayouts(bufferHandle, outPlaneLayouts);
}

int GraphicBufferMapper::getDataspace(buffer_handle_t bufferHandle,
                                      ui::Dataspace* outDataspace) {
    return mMapper->getDataspace(bufferHandle, outDataspace);
}

int GraphicBufferMapper::getBlendMode(buffer_handle_t bufferHandle,
                                      ui::BlendMode* outBlendMode) {
    return mMapper->getBlendMode(bufferHandle, outBlendMode);
}

int GraphicBufferMapper::getSmpte2086(buffer_handle_t bufferHandle,
                                      std::optional<ui::Smpte2086>* outSmpte2086) {
    return mMapper->getSmpte2086(bufferHandle, outSmpte2086);
}

int GraphicBufferMapper::getCta861_3(buffer_handle_t bufferHandle,
                                     std::optional<ui::Cta861_3>* outCta861_3) {
    return mMapper->getCta861_3(bufferHandle, outCta861_3);
}

int GraphicBufferMapper::getSmpte2094_40(
        buffer_handle_t bufferHandle, std::optional<std::vector<uint8_t>>* outSmpte2094_40) {
    return mMapper->getSmpte2094_40(bufferHandle, outSmpte2094_40);
}

int GraphicBufferMapper::getDefaultPixelFormatFourCC(uint32_t width, uint32_t height,
                                                     PixelFormat format, uint32_t layerCount,
                                                     uint64_t usage,
                                                     uint32_t* outPixelFormatFourCC) {
    return mMapper->getDefaultPixelFormatFourCC(width, height, format, layerCount, usage,
                                                outPixelFormatFourCC);
}

int GraphicBufferMapper::getDefaultPixelFormatModifier(uint32_t width, uint32_t height,
                                                       PixelFormat format, uint32_t layerCount,
                                                       uint64_t usage,
                                                       uint64_t* outPixelFormatModifier) {
    return mMapper->getDefaultPixelFormatModifier(width, height, format, layerCount, usage,
                                                  outPixelFormatModifier);
}

int GraphicBufferMapper::getDefaultAllocationSize(uint32_t width, uint32_t height,
                                                  PixelFormat format, uint32_t layerCount,
                                                  uint64_t usage,
                                                  uint64_t* outAllocationSize) {
    return mMapper->getDefaultAllocationSize(width, height, format, layerCount, usage,
                                             outAllocationSize);
}

int GraphicBufferMapper::getDefaultProtectedContent(uint32_t width, uint32_t height,
                                                    PixelFormat format, uint32_t layerCount,
                                                    uint64_t usage,
                                                    uint64_t* outProtectedContent) {
    return mMapper->getDefaultProtectedContent(width, height, format, layerCount, usage,
                                               outProtectedContent);
}

int GraphicBufferMapper::getDefaultCompression(
        uint32_t width, uint32_t height, PixelFormat format, uint32_t layerCount, uint64_t usage,
        aidl::android::hardware::graphics::common::ExtendableType* outCompression) {
    return mMapper->getDefaultCompression(width, height, format, layerCount, usage, outCompression);
}

int GraphicBufferMapper::getDefaultCompression(uint32_t width, uint32_t height,
                                               PixelFormat format, uint32_t layerCount,
                                               uint64_t usage,
                                               ui::Compression* outCompression) {
    return mMapper->getDefaultCompression(width, height, format, layerCount, usage, outCompression);
}

int GraphicBufferMapper::getDefaultInterlaced(
        uint32_t width, uint32_t height, PixelFormat format, uint32_t layerCount, uint64_t usage,
        aidl::android::hardware::graphics::common::ExtendableType* outInterlaced) {
    return mMapper->getDefaultInterlaced(width, height, format, layerCount, usage, outInterlaced);
}

int GraphicBufferMapper::getDefaultInterlaced(uint32_t width, uint32_t height,
                                              PixelFormat format, uint32_t layerCount,
                                              uint64_t usage, ui::Interlaced* outInterlaced) {
    return mMapper->getDefaultInterlaced(width, height, format, layerCount, usage, outInterlaced);
}

int GraphicBufferMapper::getDefaultChromaSiting(
        uint32_t width, uint32_t height, PixelFormat format, uint32_t layerCount, uint64_t usage,
        aidl::android::hardware::graphics::common::ExtendableType* outChromaSiting) {
    return mMapper->getDefaultChromaSiting(width, height, format, layerCount, usage,
                                           outChromaSiting);
}

int GraphicBufferMapper::getDefaultChromaSiting(uint32_t width, uint32_t height,
                                                PixelFormat format, uint32_t layerCount,
                                                uint64_t usage,
                                                ui::ChromaSiting* outChromaSiting) {
    return mMapper->getDefaultChromaSiting(width, height, format, layerCount, usage,
                                           outChromaSiting);
}

int GraphicBufferMapper::getDefaultPlaneLayouts(
        uint32_t width, uint32_t height, PixelFormat format, uint32_t layerCount, uint64_t usage,
        std::vector<ui::PlaneLayout>* outPlaneLayouts) {
    return mMapper->getDefaultPlaneLayouts(width, height, format, layerCount, usage,
                                           outPlaneLayouts);
}

// ---------------------------------------------------------------------------
}; // namespace hwc
