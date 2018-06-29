LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := gralloc.rpi3
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := Gralloc1Device.cpp gralloc1_rpi.cpp

LOCAL_C_INCLUDES := \
	external/drm_gralloc \
	external/libdrm \
	external/libdrm/include/drm \
	system/core/libgrallocusage/include

LOCAL_SHARED_LIBRARIES := \
	libgralloc_drm \
	libgralloc_kms \
	liblog \
	libui

LOCAL_CFLAGS += -Wno-c++11-narrowing \
	-Wno-unused-value \
	-Wno-unused-variable \
	-Wno-switch

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := libgralloc_kms
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := gralloc_drm_kms.cpp

LOCAL_C_INCLUDES := \
	external/drm_gralloc \
	external/libdrm

LOCAL_SHARED_LIBRARIES := \
	libdrm \
	libgralloc_drm \
	liblog \
	libcutils

LOCAL_CFLAGS += \
	-Wno-unused-variable

include $(BUILD_SHARED_LIBRARY)
