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

#ifndef HWC_UI_GRALLOC_H
#define HWC_UI_GRALLOC_H

#include <gralloctypes/Gralloc4.h>
#include <hidl/HidlSupport.h>
#include <utils/StrongPointer.h>

#include <string>

#include <hwc_ui/GraphicTypes.h>
#include <hwc_ui/PixelFormat.h>
#include <hwc_ui/Rect.h>

namespace hwc {

// A wrapper to IMapper
class GrallocMapper {
public:
    virtual ~GrallocMapper();

    virtual bool isLoaded() const = 0;

    virtual std::string dumpBuffer(buffer_handle_t /*bufferHandle*/, bool /*less*/) const {
        return "";
    }

    virtual int createDescriptor(void* bufferDescriptorInfo,
                                 void* outBufferDescriptor) const = 0;

    // Import a buffer that is from another HAL, another process, or is
    // cloned.
    //
    // The returned handle must be freed with freeBuffer.
    virtual int importBuffer(const android::hardware::hidl_handle& rawHandle,
                             buffer_handle_t* outBufferHandle) const = 0;

    virtual void freeBuffer(buffer_handle_t bufferHandle) const = 0;

    virtual int validateBufferSize(buffer_handle_t bufferHandle, uint32_t width,
                                   uint32_t height, hwc::PixelFormat format,
                                   uint32_t layerCount, uint64_t usage,
                                   uint32_t stride) const = 0;

    virtual void getTransportSize(buffer_handle_t bufferHandle, uint32_t* outNumFds,
                                  uint32_t* outNumInts) const = 0;

    // The ownership of acquireFence is always transferred to the callee, even
    // on errors.
    virtual int lock(buffer_handle_t bufferHandle, uint64_t usage, const Rect& bounds,
                     int acquireFence, void** outData, int32_t* outBytesPerPixel,
                     int32_t* outBytesPerStride) const = 0;

    // The ownership of acquireFence is always transferred to the callee, even
    // on errors.
    virtual int lock(buffer_handle_t bufferHandle, uint64_t usage, const Rect& bounds,
                    int acquireFence, android_ycbcr* ycbcr) const = 0;

    // unlock returns a fence sync object (or -1) and the fence sync object is
    // owned by the caller
    virtual int unlock(buffer_handle_t bufferHandle) const = 0;

    // isSupported queries whether or not a buffer with the given width, height,
    // format, layer count, and usage can be allocated on the device.  If
    // *outSupported is set to true, a buffer with the given specifications may be successfully
    // allocated if resources are available.  If false, a buffer with the given specifications will
    // never successfully allocate on this device. Note that this function is not guaranteed to be
    // supported on all devices, in which case a status_t of -ENOSYS will be returned.
    virtual int isSupported(uint32_t /*width*/, uint32_t /*height*/,
                            hwc::PixelFormat /*format*/, uint32_t /*layerCount*/,
                            uint64_t /*usage*/, bool* /*outSupported*/) const {
        return -ENOSYS;
    }

    virtual int getBufferId(buffer_handle_t /*bufferHandle*/,
                            uint64_t* /*outBufferId*/) const {
        return -ENOSYS;
    }
    virtual int getName(buffer_handle_t /*bufferHandle*/, std::string* /*outName*/) const {
        return -ENOSYS;
    }
    virtual int getWidth(buffer_handle_t /*bufferHandle*/, uint64_t* /*outWidth*/) const {
        return -ENOSYS;
    }
    virtual int getHeight(buffer_handle_t /*bufferHandle*/, uint64_t* /*outHeight*/) const {
        return -ENOSYS;
    }
    virtual int getLayerCount(buffer_handle_t /*bufferHandle*/,
                              uint64_t* /*outLayerCount*/) const {
        return -ENOSYS;
    }
    virtual int getPixelFormatRequested(buffer_handle_t /*bufferHandle*/,
                                        ui::PixelFormat* /*outPixelFormatRequested*/) const {
        return -ENOSYS;
    }
    virtual int getPixelFormatFourCC(buffer_handle_t /*bufferHandle*/,
                                     uint32_t* /*outPixelFormatFourCC*/) const {
        return -ENOSYS;
    }
    virtual int getPixelFormatModifier(buffer_handle_t /*bufferHandle*/,
                                       uint64_t* /*outPixelFormatModifier*/) const {
        return -ENOSYS;
    }
    virtual int getUsage(buffer_handle_t /*bufferHandle*/, uint64_t* /*outUsage*/) const {
        return -ENOSYS;
    }
    virtual int getAllocationSize(buffer_handle_t /*bufferHandle*/,
                                  uint64_t* /*outAllocationSize*/) const {
        return -ENOSYS;
    }
    virtual int getProtectedContent(buffer_handle_t /*bufferHandle*/,
                                    uint64_t* /*outProtectedContent*/) const {
        return -ENOSYS;
    }
    virtual int getCompression(
            buffer_handle_t /*bufferHandle*/,
            aidl::android::hardware::graphics::common::ExtendableType* /*outCompression*/) const {
        return -ENOSYS;
    }
    virtual int getCompression(buffer_handle_t /*bufferHandle*/,
                               ui::Compression* /*outCompression*/) const {
        return -ENOSYS;
    }
    virtual int getInterlaced(
            buffer_handle_t /*bufferHandle*/,
            aidl::android::hardware::graphics::common::ExtendableType* /*outInterlaced*/) const {
        return -ENOSYS;
    }
    virtual int getInterlaced(buffer_handle_t /*bufferHandle*/,
                              ui::Interlaced* /*outInterlaced*/) const {
        return -ENOSYS;
    }
    virtual int getChromaSiting(
            buffer_handle_t /*bufferHandle*/,
            aidl::android::hardware::graphics::common::ExtendableType* /*outChromaSiting*/) const {
        return -ENOSYS;
    }
    virtual int getChromaSiting(buffer_handle_t /*bufferHandle*/,
                                ui::ChromaSiting* /*outChromaSiting*/) const {
        return -ENOSYS;
    }
    virtual int getPlaneLayouts(buffer_handle_t /*bufferHandle*/,
                                std::vector<ui::PlaneLayout>* /*outPlaneLayouts*/) const {
        return -ENOSYS;
    }
    virtual int getDataspace(buffer_handle_t /*bufferHandle*/,
                             ui::Dataspace* /*outDataspace*/) const {
        return -ENOSYS;
    }
    virtual int getBlendMode(buffer_handle_t /*bufferHandle*/,
                             ui::BlendMode* /*outBlendMode*/) const {
        return -ENOSYS;
    }
    virtual int getSmpte2086(buffer_handle_t /*bufferHandle*/,
                             std::optional<ui::Smpte2086>* /*outSmpte2086*/) const {
        return -ENOSYS;
    }
    virtual int getCta861_3(buffer_handle_t /*bufferHandle*/,
                            std::optional<ui::Cta861_3>* /*outCta861_3*/) const {
        return -ENOSYS;
    }
    virtual int getSmpte2094_40(
            buffer_handle_t /*bufferHandle*/,
            std::optional<std::vector<uint8_t>>* /*outSmpte2094_40*/) const {
        return -ENOSYS;
    }

    virtual int getDefaultPixelFormatFourCC(uint32_t /*width*/, uint32_t /*height*/,
                                            PixelFormat /*format*/, uint32_t /*layerCount*/,
                                            uint64_t /*usage*/,
                                            uint32_t* /*outPixelFormatFourCC*/) const {
        return -ENOSYS;
    }
    virtual int getDefaultPixelFormatModifier(uint32_t /*width*/, uint32_t /*height*/,
                                              PixelFormat /*format*/, uint32_t /*layerCount*/,
                                              uint64_t /*usage*/,
                                              uint64_t* /*outPixelFormatModifier*/) const {
        return -ENOSYS;
    }
    virtual int getDefaultAllocationSize(uint32_t /*width*/, uint32_t /*height*/,
                                         PixelFormat /*format*/, uint32_t /*layerCount*/,
                                         uint64_t /*usage*/,
                                         uint64_t* /*outAllocationSize*/) const {
        return -ENOSYS;
    }
    virtual int getDefaultProtectedContent(uint32_t /*width*/, uint32_t /*height*/,
                                           PixelFormat /*format*/, uint32_t /*layerCount*/,
                                           uint64_t /*usage*/,
                                           uint64_t* /*outProtectedContent*/) const {
        return -ENOSYS;
    }
    virtual int getDefaultCompression(
            uint32_t /*width*/, uint32_t /*height*/, PixelFormat /*format*/,
            uint32_t /*layerCount*/, uint64_t /*usage*/,
            aidl::android::hardware::graphics::common::ExtendableType* /*outCompression*/) const {
        return -ENOSYS;
    }
    virtual int getDefaultCompression(uint32_t /*width*/, uint32_t /*height*/,
                                      PixelFormat /*format*/, uint32_t /*layerCount*/,
                                      uint64_t /*usage*/,
                                      ui::Compression* /*outCompression*/) const {
        return -ENOSYS;
    }
    virtual int getDefaultInterlaced(
            uint32_t /*width*/, uint32_t /*height*/, PixelFormat /*format*/,
            uint32_t /*layerCount*/, uint64_t /*usage*/,
            aidl::android::hardware::graphics::common::ExtendableType* /*outInterlaced*/) const {
        return -ENOSYS;
    }
    virtual int getDefaultInterlaced(uint32_t /*width*/, uint32_t /*height*/,
                                     PixelFormat /*format*/, uint32_t /*layerCount*/,
                                     uint64_t /*usage*/,
                                     ui::Interlaced* /*outInterlaced*/) const {
        return -ENOSYS;
    }
    virtual int getDefaultChromaSiting(
            uint32_t /*width*/, uint32_t /*height*/, PixelFormat /*format*/,
            uint32_t /*layerCount*/, uint64_t /*usage*/,
            aidl::android::hardware::graphics::common::ExtendableType* /*outChromaSiting*/) const {
        return -ENOSYS;
    }
    virtual int getDefaultChromaSiting(uint32_t /*width*/, uint32_t /*height*/,
                                       PixelFormat /*format*/, uint32_t /*layerCount*/,
                                       uint64_t /*usage*/,
                                       ui::ChromaSiting* /*outChromaSiting*/) const {
        return -ENOSYS;
    }
    virtual int getDefaultPlaneLayouts(
            uint32_t /*width*/, uint32_t /*height*/, PixelFormat /*format*/,
            uint32_t /*layerCount*/, uint64_t /*usage*/,
            std::vector<ui::PlaneLayout>* /*outPlaneLayouts*/) const {
        return -ENOSYS;
    }

    virtual std::vector<android::hardware::graphics::mapper::V4_0::IMapper::MetadataTypeDescription>
    listSupportedMetadataTypes() const {
        return {};
    }
};

} // namespace hwc

#endif // HWC_UI_GRALLOC_H