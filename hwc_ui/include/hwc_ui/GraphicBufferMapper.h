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

#ifndef HWC_UI_BUFFER_MAPPER_H
#define HWC_UI_BUFFER_MAPPER_H

#include <stdint.h>
#include <sys/types.h>

#include <memory>

#include <hwc_ui/GraphicTypes.h>
#include <hwc_ui/PixelFormat.h>
#include <hwc_ui/Rect.h>

namespace hwc {

// ---------------------------------------------------------------------------

class GrallocMapper;

class GraphicBufferMapper
{
public:
    enum Version {
        INVALID = -1,
        GRALLOC_2 = 0,
        GRALLOC_4,
    };

    static GraphicBufferMapper& getInstance();

    void dumpBuffer(buffer_handle_t bufferHandle, std::string& result, bool less = true) const;
    static void dumpBufferToSystemLog(buffer_handle_t bufferHandle, bool less = true);

    // The imported outHandle must be freed with freeBuffer when no longer
    // needed. rawHandle is owned by the caller.
    int importBuffer(buffer_handle_t rawHandle,
            uint32_t width, uint32_t height, uint32_t layerCount,
            PixelFormat format, uint64_t usage, uint32_t stride,
            buffer_handle_t* outHandle);

    // hwc don't have these info(w,h,..etc), skip validateBufferSize
    int importBuffer(buffer_handle_t rawHandle, buffer_handle_t* outHandle);

    int freeBuffer(buffer_handle_t handle);

    void getTransportSize(buffer_handle_t handle,
            uint32_t* outTransportNumFds, uint32_t* outTransportNumInts);

    int lock(buffer_handle_t handle, uint32_t usage, const Rect& bounds, void** vaddr,
             int32_t* outBytesPerPixel = nullptr, int32_t* outBytesPerStride = nullptr);

    int lockYCbCr(buffer_handle_t handle,
            uint32_t usage, const Rect& bounds, android_ycbcr *ycbcr);

    int unlock(buffer_handle_t handle);

    int lockAsync(buffer_handle_t handle, uint32_t usage, const Rect& bounds, void** vaddr,
                  int fenceFd, int32_t* outBytesPerPixel = nullptr,
                  int32_t* outBytesPerStride = nullptr);

    int lockAsync(buffer_handle_t handle, uint64_t producerUsage, uint64_t consumerUsage,
                  const Rect& bounds, void** vaddr, int fenceFd,
                  int32_t* outBytesPerPixel = nullptr, int32_t* outBytesPerStride = nullptr);

    int lockAsyncYCbCr(buffer_handle_t handle,
            uint32_t usage, const Rect& bounds, android_ycbcr *ycbcr,
            int fenceFd);

    int unlockAsync(buffer_handle_t handle, int *fenceFd);

    int isSupported(uint32_t width, uint32_t height, PixelFormat format,
                    uint32_t layerCount, uint64_t usage, bool* outSupported);

    /**
     * Gets the gralloc metadata associated with the buffer.
     *
     * These functions are supported by gralloc 4.0+.
     */
    int getBufferId(buffer_handle_t bufferHandle, uint64_t* outBufferId);
    int getName(buffer_handle_t bufferHandle, std::string* outName);
    int getWidth(buffer_handle_t bufferHandle, uint64_t* outWidth);
    int getHeight(buffer_handle_t bufferHandle, uint64_t* outHeight);
    int getLayerCount(buffer_handle_t bufferHandle, uint64_t* outLayerCount);
    int getPixelFormatRequested(buffer_handle_t bufferHandle,
                                ui::PixelFormat* outPixelFormatRequested);
    int getPixelFormatFourCC(buffer_handle_t bufferHandle, uint32_t* outPixelFormatFourCC);
    int getPixelFormatModifier(buffer_handle_t bufferHandle, uint64_t* outPixelFormatModifier);
    int getUsage(buffer_handle_t bufferHandle, uint64_t* outUsage);
    int getAllocationSize(buffer_handle_t bufferHandle, uint64_t* outAllocationSize);
    int getProtectedContent(buffer_handle_t bufferHandle, uint64_t* outProtectedContent);
    int getCompression(
            buffer_handle_t bufferHandle,
            aidl::android::hardware::graphics::common::ExtendableType* outCompression);
    int getCompression(buffer_handle_t bufferHandle, ui::Compression* outCompression);
    int getInterlaced(
            buffer_handle_t bufferHandle,
            aidl::android::hardware::graphics::common::ExtendableType* outInterlaced);
    int getInterlaced(buffer_handle_t bufferHandle, ui::Interlaced* outInterlaced);
    int getChromaSiting(
            buffer_handle_t bufferHandle,
            aidl::android::hardware::graphics::common::ExtendableType* outChromaSiting);
    int getChromaSiting(buffer_handle_t bufferHandle, ui::ChromaSiting* outChromaSiting);
    int getPlaneLayouts(buffer_handle_t bufferHandle,
                        std::vector<ui::PlaneLayout>* outPlaneLayouts);
    int getDataspace(buffer_handle_t bufferHandle, ui::Dataspace* outDataspace);
    int getBlendMode(buffer_handle_t bufferHandle, ui::BlendMode* outBlendMode);
    int getSmpte2086(buffer_handle_t bufferHandle, std::optional<ui::Smpte2086>* outSmpte2086);
    int getCta861_3(buffer_handle_t bufferHandle, std::optional<ui::Cta861_3>* outCta861_3);
    int getSmpte2094_40(buffer_handle_t bufferHandle,
                        std::optional<std::vector<uint8_t>>* outSmpte2094_40);

    /**
     * Gets the default metadata for a gralloc buffer allocated with the given parameters.
     *
     * These functions are supported by gralloc 4.0+.
     */
    int getDefaultPixelFormatFourCC(uint32_t width, uint32_t height, PixelFormat format,
                                    uint32_t layerCount, uint64_t usage,
                                    uint32_t* outPixelFormatFourCC);
    int getDefaultPixelFormatModifier(uint32_t width, uint32_t height, PixelFormat format,
                                      uint32_t layerCount, uint64_t usage,
                                      uint64_t* outPixelFormatModifier);
    int getDefaultAllocationSize(uint32_t width, uint32_t height, PixelFormat format,
                                 uint32_t layerCount, uint64_t usage,
                                 uint64_t* outAllocationSize);
    int getDefaultProtectedContent(uint32_t width, uint32_t height, PixelFormat format,
                                   uint32_t layerCount, uint64_t usage,
                                   uint64_t* outProtectedContent);
    int getDefaultCompression(
            uint32_t width, uint32_t height, PixelFormat format, uint32_t layerCount,
            uint64_t usage,
            aidl::android::hardware::graphics::common::ExtendableType* outCompression);
    int getDefaultCompression(uint32_t width, uint32_t height, PixelFormat format,
                              uint32_t layerCount, uint64_t usage,
                              ui::Compression* outCompression);
    int getDefaultInterlaced(
            uint32_t width, uint32_t height, PixelFormat format, uint32_t layerCount,
            uint64_t usage,
            aidl::android::hardware::graphics::common::ExtendableType* outInterlaced);
    int getDefaultInterlaced(uint32_t width, uint32_t height, PixelFormat format,
                             uint32_t layerCount, uint64_t usage,
                             ui::Interlaced* outInterlaced);
    int getDefaultChromaSiting(
            uint32_t width, uint32_t height, PixelFormat format, uint32_t layerCount,
            uint64_t usage,
            aidl::android::hardware::graphics::common::ExtendableType* outChromaSiting);
    int getDefaultChromaSiting(uint32_t width, uint32_t height, PixelFormat format,
                               uint32_t layerCount, uint64_t usage,
                               ui::ChromaSiting* outChromaSiting);
    int getDefaultPlaneLayouts(uint32_t width, uint32_t height, PixelFormat format,
                               uint32_t layerCount, uint64_t usage,
                               std::vector<ui::PlaneLayout>* outPlaneLayouts);

    const GrallocMapper& getGrallocMapper() const {
        return reinterpret_cast<const GrallocMapper&>(*mMapper);
    }

    Version getMapperVersion() const { return mMapperVersion; }

    bool isSupportMetadata() const { return mMapperVersion >= Version::GRALLOC_4; }
private:
    GraphicBufferMapper();

    std::unique_ptr<const GrallocMapper> mMapper;

    Version mMapperVersion;
};

// ---------------------------------------------------------------------------

}; // namespace hwc

#endif // HWC_UI_BUFFER_MAPPER_H

