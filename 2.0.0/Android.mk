# build hwcomposer static library

LOCAL_PATH := $(call my-dir)

#
# the header file of hwcomposer static library
#
include $(CLEAR_VARS)

LOCAL_MODULE := hwcomposer_headers

LOCAL_EXPORT_C_INCLUDE_DIRS := \
	$(TOP)/$(MTK_ROOT)/hardware/hwcomposer \
	$(TOP)/$(MTK_ROOT)/hardware/hwcomposer/include \
	$(TOP)/$(MTK_ROOT)/hardware/libhwcomposer/mtk_simple/include \
	$(TOP)/$(MTK_ROOT)/hardware/gralloc_extra/include \
	$(TOP)/$(MTK_ROOT)/hardware/dpframework/include \
	$(LOCAL_PATH)/.. \
	$(LOCAL_PATH)/../hwc_ui/include \
	$(TOP)/$(MTK_ROOT)/hardware/power/include \
	$(TOP)/$(MTK_ROOT)/external/libudf/libladder


ifndef MTK_GENERIC_HAL
# When MTK_GENERIC_HAL has defined, we dyanmic judge the platform via display info,
# and therefore we include the related platform header in source code.
# If MTK_GENERIC_HAL is not defined, this library only support one platform, so we
# include the platform header only.
LOCAL_EXPORT_C_INCLUDE_DIRS += \
	$(LOCAL_PATH)/../$(TARGET_BOARD_PLATFORM)
endif

ifeq ($(filter PQ_OFF no, $(MTK_PQ_SUPPORT)),)
LOCAL_EXPORT_C_INCLUDE_DIRS += \
	$(TOP)/$(MTK_ROOT)/hardware/pq/v2.0/include
endif

include $(BUILD_HEADER_LIBRARY)


#
# hwcomposer.xxxx.a for hwcomposer shared library
#
include $(CLEAR_VARS)

ifdef MTK_GENERIC_HAL
LOCAL_MODULE := hwcomposer.mtk_common.$(MTK_HWC_VERSION)
else
LOCAL_MODULE := hwcomposer.$(TARGET_BOARD_PLATFORM).$(MTK_HWC_VERSION)
endif

LOCAL_PROPRIETARY_MODULE := true
LOCAL_MODULE_OWNER := mtk

LOCAL_MODULE_CLASS := STATIC_LIBRARIES

LOCAL_ADDITIONAL_DEPENDENCIES := $(LOCAL_PATH)/Android.mk

LOCAL_SHARED_LIBRARIES := \
	libdpframework \
	libged \
	libnativewindow \
	android.hardware.graphics.common@1.2 \
	android.hardware.graphics.composer@2.3 \
	android.hardware.graphics.mapper@2.0 \
	android.hardware.graphics.mapper@2.1 \
	libcomposer_ext \
	libladder \
	libpq_prot \
	libgralloctypes \
	libpqparamparser \
	libmml \
	libdmabufheap \
	libsync \
	libxml2 \
	android.hardware.graphics.composer3-V1-ndk

LOCAL_STATIC_LIBRARIES := \
	libarect \
	libmath \
	libgrallocusage \
	libaidlcommonsupport

LOCAL_HEADER_LIBRARIES := \
	media_plugin_headers \
	libgralloc_metadata_headers \
	libhardware_headers \
	hwcomposer_headers \
	libpq_headers \
	libnpagent_headers \
	libsync_headers \
	libgralloc_extra_headers \
	libladder_headers

LOCAL_CFLAGS := \
	-DLOG_TAG=\"hwcomposer\"

ifneq ($(wildcard vendor/mediatek/internal/hwcomposer_simple),)
	LOCAL_CFLAGS += -DMTK_HWC_IS_INTERNAL_LOAD
endif

ifeq ($(HAVE_AEE_FEATURE),yes)
	LOCAL_SHARED_LIBRARIES += libaedv
	LOCAL_HEADER_LIBRARIES += libaed_headers
	LOCAL_CFLAGS += -DMTK_HWC_HAVE_AEE_FEATURE
endif

LOCAL_SRC_FILES := \
	hwc2.cpp \
	dispatcher.cpp \
	worker.cpp \
	display.cpp \
	event.cpp \
	overlay.cpp \
	queue.cpp \
	sync.cpp \
	composer.cpp \
	bliter_async.cpp \
	bliter_ultra.cpp \
	glai_handler.cpp \
	glai_controller.cpp \
	ai_blulight_defender.cpp \
	display_dump.cpp \
	platform_common.cpp \
	../utils/tools.cpp \
	../utils/debug.cpp \
	../utils/transform.cpp \
	../utils/swwatchdog.cpp \
	../utils/fpscounter.cpp \
	../utils/mm_buf_dump.cpp \
	../utils/perfhelper.cpp \
	../hwc_ui/Gralloc.cpp \
	../hwc_ui/Gralloc2.cpp \
	../hwc_ui/Gralloc4.cpp \
	../hwc_ui/GraphicBufferMapper.cpp \
	../hwc_ui/PixelFormat.cpp \
	../hwc_ui/Rect.cpp \
	color.cpp \
	asyncblitdev.cpp \
	hwc2_defs.cpp \
	hwcbuffer.cpp \
	hwclayer.cpp \
	hwcdisplay.cpp \
	dev_interface.cpp \
	pq_interface.cpp \
	grallocdev.cpp \
	hrt_common.cpp \
	platform_wrap.cpp \
	mml_asyncblitstream.cpp \
	data_express.cpp \
	color_histogram.cpp \
	pq_xml_parser.cpp \
	led_device.cpp \
	index_buffer_generator.cpp

ifeq ($(MTK_DX_HDCP_SUPPORT),yes)
LOCAL_CFLAGS += -DFT_HDCP_FEATURE
endif

ifdef MTK_GENERIC_HAL
# When MTK_GENERIC_HAL has defined, we dyanmic judge the platform via display info,
# so we need to add the related platform code.
LOCAL_SRC_FILES += \
	../ld20/platform6885.cpp \
	../ld20/platform6983.cpp \
	../ld20/platform6879.cpp \
	../ld20/platform6895.cpp \
	../ld20/platform6855.cpp \
	../ld20/platform6886.cpp \
	../ld20/platform6985.cpp
else
# If MTK_GENERIC_HAL is not defined, this library only support one platform, so we
# add the platform code only.
LOCAL_SRC_FILES += \
	../$(TARGET_BOARD_PLATFORM)/platform.cpp
endif

ifdef MTK_GENERIC_HAL
# MTK_GENERIC_HAL support projects are DRM project, so we can define it directly.
HWC_USE_DRM_DEVICE := yes
else
ifeq ($(MTK_HWC_USE_DRM_DEVICE), yes)
HWC_USE_DRM_DEVICE := yes

LOCAL_SRC_FILES += \
	hdmi_interface.cpp

LOCAL_SHARED_LIBRARIES += \
	vendor.mediatek.hardware.hdmi@1.2 \
	vendor.mediatek.hardware.hdmi@1.3

LOCAL_CFLAGS += -DUSES_HDMISERVICE
endif
endif

ifeq ($(HWC_USE_DRM_DEVICE), yes)
LOCAL_CFLAGS += -DMTK_HWC_USE_DRM_DEVICE
LOCAL_SHARED_LIBRARIES += \
        libdrm

LOCAL_SRC_FILES += \
	drm/drmdev.cpp \
	drm/drmmodeinfo.cpp \
	drm/drmmodeproperty.cpp \
	drm/drmmoderesource.cpp \
	drm/drmmodecrtc.cpp \
	drm/drmmodeplane.cpp \
	drm/drmmodeencoder.cpp \
	drm/drmmodeconnector.cpp \
	drm/drmmodeutils.cpp \
	drm/drmobject.cpp \
	drm/drmhrt.cpp \
	drm/drmpq.cpp \
	drm/drmhistogram.cpp
else
LOCAL_SRC_FILES += \
	legacy/hwdev.cpp \
	legacy/hrt.cpp \
	legacy/pqdev_legacy.cpp
endif

ifeq ($(strip $(TARGET_BUILD_VARIANT)), user)
LOCAL_CFLAGS += -DMTK_USER_BUILD
endif

LOCAL_CFLAGS += -DUSE_SYSTRACE

LOCAL_CFLAGS += -DMTK_HWC_VER_2_0

LOCAL_CFLAGS += -DUSE_HWC2

LOCAL_CFLAGS += -DUSE_SWWATCHDOG

ifeq ($(FPGA_EARLY_PORTING), yes)
LOCAL_CFLAGS += -DFPGA_EARLY_PORTING
endif

# if MTK_PQ_SUPPORT != PQ_OFF or no (need check both, comment by ME7)
ifeq ($(filter PQ_OFF no, $(MTK_PQ_SUPPORT)),)
	LOCAL_SHARED_LIBRARIES += \
		libbinder_ndk \
		vendor.mediatek.hardware.pq_aidl-V1-ndk
	LOCAL_CFLAGS += -DUSES_PQSERVICE
	LOCAL_CFLAGS += -DUSES_MMLPQ
endif

ifeq ($(MTK_BASIC_PACKAGE), yes)
	LOCAL_CFLAGS += -DMTK_BASIC_PACKAGE
endif

#LOCAL_CFLAGS += -DMTK_HWC_PROFILING
LOCAL_CFLAGS += -DMTK_IN_DISPLAY_FINGERPRINT

ifdef MTK_GENERIC_HAL
# Use to control platform_warp for creating PlatformCommon
LOCAL_CFLAGS += -DMTK_GENERIC_HAL
else
LOCAL_CFLAGS += -DHWC_PLATFORM_ID=_$(MTK_PLATFORM)
endif

ifdef MTK_GENERIC_HAL
# all MTK_GENERIC_HAL projects support MTK_HDR_SET_DISPLAY_COLOR, so we can define it directly
LOCAL_CFLAGS += -DMTK_HDR_SET_DISPLAY_COLOR
else
mtk5G_platforms := mt6885 mt6873 mt6853
ifeq ($(TARGET_BOARD_PLATFORM), $(filter $(TARGET_BOARD_PLATFORM), $(mtk5G_platforms)))
LOCAL_CFLAGS += -DMTK_HDR_SET_DISPLAY_COLOR
endif
endif

ifeq ($(MTK_HDMI_RXVC_MULTIDISP_SUPPORT), yes)
LOCAL_CFLAGS += -DMTK_HDMI_RXVC_MULTIDISP_SUPPORT
endif

ifeq ($(strip $(MTK_DRM_PIXEL_SHIFT_SUPPORT)), yes)
LOCAL_CFLAGS += -DMTK_DRM_PIXEL_SHIFT
endif

LOCAL_CFLAGS += -Wconversion \
	-Wimplicit \
	-Wunused \
	-Wformat \
	-Wunreachable-code \
	-Wsign-compare \
	-Wall \
	-Werror

include $(MTK_STATIC_LIBRARY)

