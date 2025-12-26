# build simple hwcomposer shared library

ifneq ($(wildcard vendor/mediatek/internal/hwcomposer_simple),)

LOCAL_PATH := $(call my-dir)

#
# the header file of libsimplehwc shared library
#
include $(CLEAR_VARS)

LOCAL_MODULE := libsimplehwc_headers

LOCAL_EXPORT_C_INCLUDE_DIRS := \
	$(TOP)/$(MTK_ROOT)/hardware/hwcomposer \
	$(TOP)/$(MTK_ROOT)/hardware/hwcomposer/include \
	$(TOP)/$(MTK_ROOT)/hardware/gralloc_extra/include \
	$(LOCAL_PATH)/.. \
	$(LOCAL_PATH)/drm_simple \
	$(LOCAL_PATH)/include \
	$(LOCAL_PATH)/../hwc_ui/include \
	$(TOP)/system/core/libsync/include \
	$(TOP)/system/core/libsync \
	$(TOP)/system/core/include \
	$(TOP)/system/core/base/include \
        $(TOP)/frameworks/native/libs/ui/include \
	frameworks/native/libs/nativewindow/include \
	frameworks/native/libs/nativebase/include \
	frameworks/native/libs/arect/include \
	$(TOP)/$(MTK_ROOT)/external/libudf/libladder \
	$(TOP)/external/libdrm/include

include $(BUILD_HEADER_LIBRARY)

#
# libsimplehwc.so for hwcomposer.xxxx.a
#
include $(CLEAR_VARS)

LOCAL_ADDITIONAL_DEPENDENCIES := $(LOCAL_PATH)/Android.mk

LOCAL_SHARED_LIBRARIES := \
	libsync \
	libutils \
	libcutils \
	liblog \
	libdl \
	libdrm \
	libbinder \
	libbase \
	libhidlbase \
	libhwbinder \
	libhidltransport \
	libnativewindow \
	android.hardware.graphics.common@1.2 \
	android.hardware.graphics.composer@2.3 \
	android.hardware.graphics.mapper@2.0 \
	android.hardware.graphics.mapper@2.1 \
	android.hardware.graphics.mapper@4.0 \
	libladder \
	libgralloctypes \
	libgralloc_extra \
	android.hardware.graphics.composer3-V1-ndk

LOCAL_STATIC_LIBRARIES := \
	libarect \
	libmath \
	libgrallocusage \

LOCAL_HEADER_LIBRARIES := \
	media_plugin_headers \
	libgralloc_metadata_headers \
	libhardware_headers \
	libsimplehwc_headers \
	libpq_headers \
	libladder_headers

LOCAL_CFLAGS := \
	-DLOG_TAG=\"simplehwc\"

LOCAL_SRC_FILES := \
	hwc2_simple.cpp \
	hwcdisplay_simple.cpp \
	hwclayer_simple.cpp \
	fake_vsync.cpp \
	dev_interface_simple.cpp \
	debug_simple.cpp \
	../hwc_ui/Gralloc.cpp \
	../hwc_ui/Gralloc2.cpp \
	../hwc_ui/Gralloc4.cpp \
	../hwc_ui/GraphicBufferMapper.cpp \
	../hwc_ui/PixelFormat.cpp \
	../hwc_ui/Rect.cpp

LOCAL_CFLAGS += -DMTK_HWC_USE_DRM_DEVICE
LOCAL_SHARED_LIBRARIES += \
        libdrm

LOCAL_SRC_FILES += \
	drm_simple/drmdev.cpp \
	drm_simple/drmmodeconnector.cpp \
	drm_simple/drmmodecrtc.cpp \
	drm_simple/drmmodeencoder.cpp \
	drm_simple/drmmodeinfo.cpp \
	drm_simple/drmmodeplane.cpp \
	drm_simple/drmmodeproperty.cpp \
	drm_simple/drmmoderesource.cpp \
	drm_simple/drmmodeutils.cpp \
	drm_simple/drmobject.cpp

ifeq ($(strip $(TARGET_BUILD_VARIANT)), user)
LOCAL_CFLAGS += -DMTK_USER_BUILD
endif

#LOCAL_CFLAGS += -DUSE_SYSTRACE
LOCAL_CFLAGS += -DFB_CACHED_ENABLE
LOCAL_CFLAGS += -DMTK_HWC_VER_2_0

LOCAL_CFLAGS += -DUSE_HWC2

ifeq ($(FPGA_EARLY_PORTING), yes)
LOCAL_CFLAGS += -DFPGA_EARLY_PORTING
endif

LOCAL_CFLAGS += -Wconversion \
	-Wimplicit \
	-Wunused \
	-Wformat \
	-Wunreachable-code \
	-Wsign-compare \
	-Wall \
	-Werror

LOCAL_MODULE := libsimplehwc

LOCAL_PROPRIETARY_MODULE := true
LOCAL_MODULE_OWNER := mtk

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_MULTILIB := first
include $(MTK_SHARED_LIBRARY)

endif
