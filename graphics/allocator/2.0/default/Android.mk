LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := android.hardware.graphics.allocator@2.0-service.rpi3
LOCAL_VENDOR_MODULE := true
LOCAL_PROPRIETARY_MODULE := true
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_MODULE_TAGS := optional

LOCAL_INIT_RC := android.hardware.graphics.allocator@2.0-service.rpi3.rc

LOCAL_SRC_FILES := \
        drm_gralloc_rpi3.cpp \
        Allocator.cpp \
        service.cpp

LOCAL_SHARED_LIBRARIES := \
        android.hardware.graphics.allocator@2.0 \
        libhidlbase \
        libhidltransport \
        libbase \
        libutils \
        libcutils \
        liblog \
        libui \
        libdrm \
        libgralloc_drm

LOCAL_HEADER_LIBRARIES := android.hardware.graphics.mapper@2.0-passthrough_headers

LOCAL_C_INCLUDES := \
        external/drm_gralloc \
        external/libdrm \
        external/libdrm/include/drm \
        system/core/libgrallocusage/include \
        system/core/libutils/include

LOCAL_CFLAGS += \
        -Wall \
        -Werror

include $(BUILD_EXECUTABLE)
