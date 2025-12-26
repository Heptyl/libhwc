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

#ifndef HWC_UI_GRALLOC2_H
#define HWC_UI_GRALLOC2_H

#include <string>

#include <android/hardware/graphics/common/1.1/types.h>
#include <android/hardware/graphics/mapper/2.0/IMapper.h>
#include <android/hardware/graphics/mapper/2.1/IMapper.h>
#include <utils/StrongPointer.h>

#include <hwc_ui/Gralloc.h>
#include <hwc_ui/GraphicTypes.h>
#include <hwc_ui/Rect.h>

namespace hwc {

class Gralloc2Mapper : public GrallocMapper {
public:
    Gralloc2Mapper();

    bool isLoaded() const override;

    int createDescriptor(void* bufferDescriptorInfo, void* outBufferDescriptor) const override;

    int importBuffer(const android::hardware::hidl_handle& rawHandle,
                      buffer_handle_t* outBufferHandle) const override;

    void freeBuffer(buffer_handle_t bufferHandle) const override;

    int validateBufferSize(buffer_handle_t bufferHandle, uint32_t width, uint32_t height,
                           hwc::PixelFormat format, uint32_t layerCount, uint64_t usage,
                           uint32_t stride) const override;

    void getTransportSize(buffer_handle_t bufferHandle, uint32_t* outNumFds,
                          uint32_t* outNumInts) const override;

    int lock(buffer_handle_t bufferHandle, uint64_t usage, const Rect& bounds,
             int acquireFence, void** outData, int32_t* outBytesPerPixel,
             int32_t* outBytesPerStride) const override;

    int lock(buffer_handle_t bufferHandle, uint64_t usage, const Rect& bounds,
             int acquireFence, android_ycbcr* ycbcr) const override;

    int unlock(buffer_handle_t bufferHandle) const override;

private:
    // Determines whether the passed info is compatible with the mapper.
    int validateBufferDescriptorInfo(
            android::hardware::graphics::mapper::V2_1::IMapper::BufferDescriptorInfo* descriptorInfo) const;

    android::sp<android::hardware::graphics::mapper::V2_0::IMapper> mMapper;
    android::sp<android::hardware::graphics::mapper::V2_1::IMapper> mMapperV2_1;
};

} // namespace hwc

#endif // HWC_UI_GRALLOC2_H