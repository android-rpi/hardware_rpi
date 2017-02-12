LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := gralloc.$(TARGET_PRODUCT)
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := gralloc_rpi.cpp gralloc_fb.cpp

LOCAL_C_INCLUDES := \
	external/drm_gralloc \
	external/libdrm \
	external/libdrm/include/drm

LOCAL_SHARED_LIBRARIES := \
	libgralloc_drm \
	liblog \
	libui \
	libGLESv1_CM

LOCAL_CFLAGS += -Wno-c++11-narrowing

include $(BUILD_SHARED_LIBRARY)
