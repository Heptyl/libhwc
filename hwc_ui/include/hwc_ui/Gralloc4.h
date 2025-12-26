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

#ifndef HWC_UI_GRALLOC4_H
#define HWC_UI_GRALLOC4_H

#include <android/hardware/graphics/common/1.1/types.h>
#include <android/hardware/graphics/mapper/4.0/IMapper.h>
#include <gralloctypes/Gralloc4.h>
#include <utils/Errors.h>
#include <utils/StrongPointer.h>

#include <string>

#include <hwc_ui/Gralloc.h>
#include <hwc_ui/GraphicTypes.h>
#include <hwc_ui/Rect.h>

namespace hwc {

class Gralloc4Mapper : public GrallocMapper {
public:
    Gralloc4Mapper();

    bool isLoaded() const override;

    std::string dumpBuffer(buffer_handle_t bufferHandle, bool less = true) const override;
    std::string dumpBuffers(bool less = true) const;

    int createDescriptor(void* bufferDescriptorInfo, void* outBufferDescriptor) const override;

    int importBuffer(const android::hardware::hidl_handle& rawHandle,
                     buffer_handle_t* outBufferHandle) const override;

    void freeBuffer(buffer_handle_t bufferHandle) const override;

    int validateBufferSize(buffer_handle_t bufferHandle, uint32_t width, uint32_t height,
                           PixelFormat format, uint32_t layerCount, uint64_t usage,
                           uint32_t stride) const override;

    void getTransportSize(buffer_handle_t bufferHandle, uint32_t* outNumFds,
                          uint32_t* outNumInts) const override;

    int lock(buffer_handle_t bufferHandle, uint64_t usage, const Rect& bounds,
             int acquireFence, void** outData, int32_t* outBytesPerPixel,
             int32_t* outBytesPerStride) const override;

    int lock(buffer_handle_t bufferHandle, uint64_t usage, const Rect& bounds,
             int acquireFence, android_ycbcr* ycbcr) const override;

    int unlock(buffer_handle_t bufferHandle) const override;

    int isSupported(uint32_t width, uint32_t height, PixelFormat format, uint32_t layerCount,
                    uint64_t usage, bool* outSupported) const override;

    int getBufferId(buffer_handle_t bufferHandle, uint64_t* outBufferId) const override;
    int getName(buffer_handle_t bufferHandle, std::string* outName) const override;
    int getWidth(buffer_handle_t bufferHandle, uint64_t* outWidth) const override;
    int getHeight(buffer_handle_t bufferHandle, uint64_t* outHeight) const override;
    int getLayerCount(buffer_handle_t bufferHandle, uint64_t* outLayerCount) const override;
    int getPixelFormatRequested(buffer_handle_t bufferHandle,
                                ui::PixelFormat* outPixelFormatRequested) const override;
    int getPixelFormatFourCC(buffer_handle_t bufferHandle,
                             uint32_t* outPixelFormatFourCC) const override;
    int getPixelFormatModifier(buffer_handle_t bufferHandle,
                               uint64_t* outPixelFormatModifier) const override;
    int getUsage(buffer_handle_t bufferHandle, uint64_t* outUsage) const override;
    int getAllocationSize(buffer_handle_t bufferHandle,
                          uint64_t* outAllocationSize) const override;
    int getProtectedContent(buffer_handle_t bufferHandle,
                            uint64_t* outProtectedContent) const override;
    int getCompression(buffer_handle_t bufferHandle,
                       aidl::android::hardware::graphics::common::ExtendableType*
                                outCompression) const override;
    int getCompression(buffer_handle_t bufferHandle,
                       ui::Compression* outCompression) const override;
    int getInterlaced(buffer_handle_t bufferHandle,
                      aidl::android::hardware::graphics::common::ExtendableType* outInterlaced)
            const override;
    int getInterlaced(buffer_handle_t bufferHandle,
                      ui::Interlaced* outInterlaced) const override;
    int getChromaSiting(buffer_handle_t bufferHandle,
                        aidl::android::hardware::graphics::common::ExtendableType*
                                outChromaSiting) const override;
    int getChromaSiting(buffer_handle_t bufferHandle,
                        ui::ChromaSiting* outChromaSiting) const override;
    int getPlaneLayouts(buffer_handle_t bufferHandle,
                        std::vector<ui::PlaneLayout>* outPlaneLayouts) const override;
    int getDataspace(buffer_handle_t bufferHandle, ui::Dataspace* outDataspace) const override;
    int getBlendMode(buffer_handle_t bufferHandle, ui::BlendMode* outBlendMode) const override;
    int getSmpte2086(buffer_handle_t bufferHandle,
                     std::optional<ui::Smpte2086>* outSmpte2086) const override;
    int getCta861_3(buffer_handle_t bufferHandle,
                    std::optional<ui::Cta861_3>* outCta861_3) const override;
    int getSmpte2094_40(buffer_handle_t bufferHandle,
                        std::optional<std::vector<uint8_t>>* outSmpte2094_40) const override;

    int getDefaultPixelFormatFourCC(uint32_t width, uint32_t height, PixelFormat format,
                                    uint32_t layerCount, uint64_t usage,
                                    uint32_t* outPixelFormatFourCC) const override;
    int getDefaultPixelFormatModifier(uint32_t width, uint32_t height, PixelFormat format,
                                      uint32_t layerCount, uint64_t usage,
                                      uint64_t* outPixelFormatModifier) const override;
    int getDefaultAllocationSize(uint32_t width, uint32_t height, PixelFormat format,
                                 uint32_t layerCount, uint64_t usage,
                                 uint64_t* outAllocationSize) const override;
    int getDefaultProtectedContent(uint32_t width, uint32_t height, PixelFormat format,
                                   uint32_t layerCount, uint64_t usage,
                                   uint64_t* outProtectedContent) const override;
    int getDefaultCompression(uint32_t width, uint32_t height, PixelFormat format,
                              uint32_t layerCount, uint64_t usage,
                              aidl::android::hardware::graphics::common::ExtendableType*
                                      outCompression) const override;
    int getDefaultCompression(uint32_t width, uint32_t height, PixelFormat format,
                              uint32_t layerCount, uint64_t usage,
                              ui::Compression* outCompression) const override;
    int getDefaultInterlaced(uint32_t width, uint32_t height, PixelFormat format,
                             uint32_t layerCount, uint64_t usage,
                             aidl::android::hardware::graphics::common::ExtendableType*
                                     outInterlaced) const override;
    int getDefaultInterlaced(uint32_t width, uint32_t height, PixelFormat format,
                             uint32_t layerCount, uint64_t usage,
                             ui::Interlaced* outInterlaced) const override;
    int getDefaultChromaSiting(uint32_t width, uint32_t height, PixelFormat format,
                               uint32_t layerCount, uint64_t usage,
                               aidl::android::hardware::graphics::common::ExtendableType*
                                       outChromaSiting) const override;
    int getDefaultChromaSiting(uint32_t width, uint32_t height, PixelFormat format,
                               uint32_t layerCount, uint64_t usage,
                               ui::ChromaSiting* outChromaSiting) const override;
    int getDefaultPlaneLayouts(uint32_t width, uint32_t height, PixelFormat format,
                               uint32_t layerCount, uint64_t usage,
                               std::vector<ui::PlaneLayout>* outPlaneLayouts) const override;

    std::vector<android::hardware::graphics::mapper::V4_0::IMapper::MetadataTypeDescription>
    listSupportedMetadataTypes() const;

private:
    // Determines whether the passed info is compatible with the mapper.
    int validateBufferDescriptorInfo(
            android::hardware::graphics::mapper::V4_0::IMapper::BufferDescriptorInfo* descriptorInfo) const;

    template <class T>
    using DecodeFunction = android::status_t (*)(const android::hardware::hidl_vec<uint8_t>& input, T* output);

    template <class T>
    int get(
            buffer_handle_t bufferHandle,
            const android::hardware::graphics::mapper::V4_0::IMapper::MetadataType& metadataType,
            DecodeFunction<T> decodeFunction, T* outMetadata) const;

    template <class T>
    int getDefault(
            uint32_t width, uint32_t height, PixelFormat format, uint32_t layerCount,
            uint64_t usage,
            const android::hardware::graphics::mapper::V4_0::IMapper::MetadataType& metadataType,
            DecodeFunction<T> decodeFunction, T* outMetadata) const;

    template <class T>
    int metadataDumpHelper(
            const android::hardware::graphics::mapper::V4_0::IMapper::BufferDump& bufferDump,
            aidl::android::hardware::graphics::common::StandardMetadataType metadataType,
            DecodeFunction<T> decodeFunction, T* outT) const;
    int bufferDumpHelper(
            const android::hardware::graphics::mapper::V4_0::IMapper::BufferDump& bufferDump,
            std::ostringstream* outDump, uint64_t* outAllocationSize, bool less) const;

    android::sp<android::hardware::graphics::mapper::V4_0::IMapper> mMapper;
};

} // namespace hwc

#endif // HWC_UI_GRALLOC4_H
