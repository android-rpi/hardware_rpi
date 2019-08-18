#define LOG_TAG "mapper@2.0-Mapper"
//#define LOG_NDEBUG 0
#include <android-base/logging.h>
#include <utils/Log.h>
#include <inttypes.h>
#include <mapper-passthrough/2.0/GrallocBufferDescriptor.h>
#include <hardware/gralloc1.h>

#include "drm_mapper_rpi3.h"
#include "Mapper.h"

namespace android {
namespace hardware {
namespace graphics {
namespace mapper {
namespace V2_0 {
namespace implementation {

Mapper::Mapper() {
    ALOGV("Constructing");
    int error = hw_get_module(GRALLOC_HARDWARE_MODULE_ID,
            (const hw_module_t **)&mModule);
    if (error) {
        ALOGE("Failed to get mModule %d", error);
    }
}

static uint64_t getValidBufferUsageMask() {
	return BufferUsage::CPU_READ_MASK | BufferUsage::CPU_WRITE_MASK | BufferUsage::GPU_TEXTURE |
		   BufferUsage::GPU_RENDER_TARGET | BufferUsage::COMPOSER_OVERLAY |
		   BufferUsage::COMPOSER_CLIENT_TARGET | BufferUsage::PROTECTED |
		   BufferUsage::COMPOSER_CURSOR | BufferUsage::VIDEO_ENCODER |
		   BufferUsage::CAMERA_OUTPUT | BufferUsage::CAMERA_INPUT | BufferUsage::RENDERSCRIPT |
		   BufferUsage::VIDEO_DECODER | BufferUsage::SENSOR_DIRECT_DATA |
		   BufferUsage::GPU_DATA_BUFFER | BufferUsage::VENDOR_MASK |
		   BufferUsage::VENDOR_MASK_HI;
}

Return<void> Mapper::createDescriptor(const IMapper::BufferDescriptorInfo& descriptorInfo,
        createDescriptor_cb hidl_cb) {
	Error error = Error::NONE;
    if (!descriptorInfo.width || !descriptorInfo.height || !descriptorInfo.layerCount) {
        error = Error::BAD_VALUE;
    } else if (descriptorInfo.layerCount != 1) {
        error = Error::UNSUPPORTED;
    } else if (descriptorInfo.format == static_cast<PixelFormat>(0)) {
        error = Error::BAD_VALUE;
    }

    BufferDescriptor descriptor;
    if (error == Error::NONE) {
        const uint64_t validUsageBits = getValidBufferUsageMask();
        if (descriptorInfo.usage & ~validUsageBits) {
            ALOGW("buffer descriptor with invalid usage bits 0x%" PRIx64,
                    descriptorInfo.usage & ~validUsageBits);
        }
        descriptor = grallocEncodeBufferDescriptor(descriptorInfo);
	}
    hidl_cb(error, descriptor);
    return Void();
}

Return<void> Mapper::importBuffer(const hidl_handle& rawHandle,
		IMapper::importBuffer_cb hidl_cb) {
    if (!rawHandle.getNativeHandle()) {
        hidl_cb(Error::BAD_BUFFER, nullptr);
        return Void();
    }
    Error error = Error::NONE;
    native_handle_t* bufferHandle = native_handle_clone(rawHandle.getNativeHandle());
    if (!bufferHandle) {
        error = Error::NO_RESOURCES;
    }

    ALOGV("register(%p)", bufferHandle);
    int result = drm_register(mModule, bufferHandle);
    if (result != 0) {
        ALOGE("gralloc0 register failed: %d", result);
        native_handle_close(bufferHandle);
        native_handle_delete(bufferHandle);
        bufferHandle = nullptr;
        error = Error::NO_RESOURCES;
    }

    hidl_cb(error, bufferHandle);
    return Void();
}

Return<Error> Mapper::freeBuffer(void* buffer) {
    Error error = Error::NONE;
    native_handle_t* bufferHandle = static_cast<native_handle_t*>(buffer);
    if (!bufferHandle) {
        error = Error::BAD_BUFFER;
    }
    if (error == Error::NONE) {
        ALOGV("unregister(%p)", bufferHandle);
        int result = drm_unregister(bufferHandle);
        if (result != 0) {
            ALOGE("gralloc0 unregister failed: %d", result);
            error = Error::UNSUPPORTED;
        } else {
            native_handle_close(bufferHandle);
            native_handle_delete(bufferHandle);
        }
    }
    return error;
}


static Error getFenceFd(const hidl_handle& fenceHandle, base::unique_fd* outFenceFd) {
    auto handle = fenceHandle.getNativeHandle();
    if (handle && handle->numFds > 1) {
        ALOGE("invalid fence handle with %d fds", handle->numFds);
        return Error::BAD_VALUE;
    }
    int fenceFd = (handle && handle->numFds == 1) ? handle->data[0] : -1;
    if (fenceFd >= 0) {
        fenceFd = dup(fenceFd);
        if (fenceFd < 0) {
            return Error::NO_RESOURCES;
        }
    }
    outFenceFd->reset(fenceFd);
    return Error::NONE;
}

Return<void> Mapper::lock(void* buffer, uint64_t cpuUsage, const IMapper::Rect& accessRegion,
                  const hidl_handle& acquireFence, IMapper::lock_cb hidl_cb) {
    const native_handle_t* bufferHandle = static_cast<const native_handle_t*>(buffer);
    if (!bufferHandle) {
        hidl_cb(Error::BAD_BUFFER, nullptr);
        return Void();
    }

    const auto pUsage = static_cast<gralloc1_producer_usage_t>(cpuUsage);
    const auto cUsage = static_cast<gralloc1_consumer_usage_t>(cpuUsage
            & ~static_cast<uint64_t>(BufferUsage::CPU_WRITE_MASK));
    const auto usage = static_cast<int32_t>(pUsage | cUsage);

    const auto accessRect = gralloc1_rect_t{accessRegion.left, accessRegion.top,
                 accessRegion.width, accessRegion.height};

    base::unique_fd fenceFd;
    Error error = getFenceFd(acquireFence, &fenceFd);
    if (error != Error::NONE) {
        hidl_cb(error, nullptr);
        return Void();
    }
    sp<Fence> aFence{new Fence(fenceFd.release())};
    aFence->waitForever("Mapper::lock");

    void* data = nullptr;
    int result = drm_lock(bufferHandle, usage, accessRect.left, accessRect.top,
            accessRect.width, accessRect.height, &data);
    ALOGV("gralloc0 lock returned %d", result);

    if (result != 0) {
        hidl_cb(Error::UNSUPPORTED, nullptr);
    } else {
        hidl_cb(error, data);
    }
    return Void();
}


Return<void> Mapper::lockYCbCr(void* /*buffer*/, uint64_t /*cpuUsage*/, const IMapper::Rect& /*accessRegion*/,
                       const hidl_handle& /*acquireFence*/, IMapper::lockYCbCr_cb hidl_cb) {
    hidl_cb(Error::UNSUPPORTED, YCbCrLayout{});
    return Void();
}


static hidl_handle getFenceHandle(const base::unique_fd& fenceFd, char* handleStorage) {
    native_handle_t* handle = nullptr;
    if (fenceFd >= 0) {
        handle = native_handle_init(handleStorage, 1, 0);
        handle->data[0] = fenceFd;
    }
    return hidl_handle(handle);
}

Return<void> Mapper::unlock(void* buffer, IMapper::unlock_cb hidl_cb) {
    const native_handle_t* bufferHandle = static_cast<const native_handle_t*>(buffer);
    if (!bufferHandle) {
        hidl_cb(Error::BAD_BUFFER, nullptr);
        return Void();
    }

    int result = drm_unlock(bufferHandle);
	if (result != 0) {
		ALOGE("gralloc0 unlock failed: %d", result);
        hidl_cb(Error::UNSUPPORTED, nullptr);
        return Void();
	}

    base::unique_fd fenceFd;
    fenceFd.reset((Fence::NO_FENCE)->dup());
    NATIVE_HANDLE_DECLARE_STORAGE(fenceStorage, 1, 0);
    hidl_cb(Error::NONE, getFenceHandle(fenceFd, fenceStorage));
    return Void();
}


IMapper* HIDL_FETCH_IMapper(const char* /* name */) {
    return new Mapper();
}

}  // namespace implementation
}  // namespace V2_0
}  // namespace mapper
}  // namespace graphics
}  // namespace hardware
}  // namespace android
