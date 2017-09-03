#undef LOG_TAG
#define LOG_TAG "Gralloc1Device"
//#define LOG_NDEBUG 0
#include <utils/Log.h>
#include <inttypes.h>

#include "gralloc1_rpi.h"
#include "Gralloc1Device.h"

#include <grallocusage/GrallocUsageConversion.h>

namespace android {

Gralloc1Device::Gralloc1Device(const struct drm_gralloc1_module_t *module) {
    ALOGV("Constructing");
	common.tag = HARDWARE_DEVICE_TAG;
	common.version = 1;
	common.module = const_cast<hw_module_t *>(&module->common);
	common.close = CloseDevice;
	getFunction = GetFunction;
	getCapabilities = GetCapabilities;

	mModule = module;
    int error = drm_init(mModule);
    if (error) {
        ALOGE("Failed drm_init() %d", error);
    }
}

Gralloc1Device::~Gralloc1Device() {
	if (mModule != nullptr) {
		drm_deinit(mModule);
	}
}

int Gralloc1Device::CloseDevice(hw_device_t *device) {
	delete getImpl(device);
	return 0;
}

void Gralloc1Device::GetCapabilities(struct gralloc1_device */*device*/, uint32_t *outCount,
		     int32_t */*outCapabilities*/) {
    *outCount = 0;
}

gralloc1_function_pointer_t Gralloc1Device::GetFunction(gralloc1_device_t *device, int32_t function) {
    if (!device) {
        ALOGE("NULL gralloc1_device_t");
	    return nullptr;
	}

    constexpr auto lastFunction = static_cast<int32_t>(GRALLOC1_LAST_FUNCTION);
    if (function < 0 || function > lastFunction) {
        ALOGE("Invalid function descriptor");
        return nullptr;
    }

    auto descriptor = static_cast<gralloc1_function_descriptor_t>(function);
    switch (descriptor) {
        case GRALLOC1_FUNCTION_DUMP:
            return reinterpret_cast<gralloc1_function_pointer_t>(Dump);
        case GRALLOC1_FUNCTION_CREATE_DESCRIPTOR:
            return reinterpret_cast<gralloc1_function_pointer_t>(CreateDescriptor);
        case GRALLOC1_FUNCTION_DESTROY_DESCRIPTOR:
            return reinterpret_cast<gralloc1_function_pointer_t>(DestroyDescriptor);
        case GRALLOC1_FUNCTION_SET_CONSUMER_USAGE:
            return reinterpret_cast<gralloc1_function_pointer_t>(SetConsumerUsage);
        case GRALLOC1_FUNCTION_SET_DIMENSIONS:
            return reinterpret_cast<gralloc1_function_pointer_t>(SetDimensions);
        case GRALLOC1_FUNCTION_SET_FORMAT:
            return reinterpret_cast<gralloc1_function_pointer_t>(SetFormat);
        case GRALLOC1_FUNCTION_SET_PRODUCER_USAGE:
            return reinterpret_cast<gralloc1_function_pointer_t>(SetProducerUsage);
        case GRALLOC1_FUNCTION_GET_BACKING_STORE:
            return reinterpret_cast<gralloc1_function_pointer_t>(
                    bufferHook<decltype(&Buffer::getBackingStore),
                    &Buffer::getBackingStore, gralloc1_backing_store_t*>);
        case GRALLOC1_FUNCTION_GET_CONSUMER_USAGE:
            return reinterpret_cast<gralloc1_function_pointer_t>(GetConsumerUsage);
        case GRALLOC1_FUNCTION_GET_DIMENSIONS:
            return reinterpret_cast<gralloc1_function_pointer_t>(
                    bufferHook<decltype(&Buffer::getDimensions),
                    &Buffer::getDimensions, uint32_t*, uint32_t*>);
        case GRALLOC1_FUNCTION_GET_FORMAT:
            return reinterpret_cast<gralloc1_function_pointer_t>(
                    bufferHook<decltype(&Buffer::getFormat),
                    &Buffer::getFormat, int32_t*>);
        case GRALLOC1_FUNCTION_GET_PRODUCER_USAGE:
            return reinterpret_cast<gralloc1_function_pointer_t>(GetProducerUsage);
        case GRALLOC1_FUNCTION_GET_STRIDE:
            return reinterpret_cast<gralloc1_function_pointer_t>(
                    bufferHook<decltype(&Buffer::getStride),
                    &Buffer::getStride, uint32_t*>);
        case GRALLOC1_FUNCTION_ALLOCATE:
	    return reinterpret_cast<gralloc1_function_pointer_t>(Allocate);
        case GRALLOC1_FUNCTION_RETAIN:
            return reinterpret_cast<gralloc1_function_pointer_t>(Retain);
        case GRALLOC1_FUNCTION_RELEASE:
            return reinterpret_cast<gralloc1_function_pointer_t>(
                    managementHook<&Gralloc1Device::release>);
        case GRALLOC1_FUNCTION_GET_NUM_FLEX_PLANES:
            return reinterpret_cast<gralloc1_function_pointer_t>(
                    bufferHook<decltype(&Buffer::getNumFlexPlanes),
                    &Buffer::getNumFlexPlanes, uint32_t*>);
        case GRALLOC1_FUNCTION_LOCK:
            return reinterpret_cast<gralloc1_function_pointer_t>(
                    lockHook<void*, &Gralloc1Device::lock>);
        case GRALLOC1_FUNCTION_LOCK_FLEX:
            return reinterpret_cast<gralloc1_function_pointer_t>(
                    lockHook<struct android_flex_layout,
                    &Gralloc1Device::lockFlex>);
        case GRALLOC1_FUNCTION_UNLOCK:
            return reinterpret_cast<gralloc1_function_pointer_t>(Unlock);
        case GRALLOC1_FUNCTION_INVALID:
            ALOGE("Invalid function descriptor");
            return nullptr;
    }

    ALOGE("Unknown function descriptor: %d", function);
    return nullptr;
}

gralloc1_error_t Gralloc1Device::Allocate(gralloc1_device_t* device,
        uint32_t numDescriptors,
        const gralloc1_buffer_descriptor_t* descriptors,
        buffer_handle_t* outBuffers)
{
    if (!outBuffers) {
        return GRALLOC1_ERROR_UNDEFINED;
    }

    auto adapter = getImpl(device);

    gralloc1_error_t error = GRALLOC1_ERROR_NONE;
    uint32_t i;
    for (i = 0; i < numDescriptors; i++) {
        auto descriptor = adapter->getDescriptor(descriptors[i]);
        if (!descriptor) {
            error = GRALLOC1_ERROR_BAD_DESCRIPTOR;
            break;
        }

        buffer_handle_t bufferHandle = nullptr;
        error = adapter->allocate(descriptors[i], descriptor, &bufferHandle);
        if (error != GRALLOC1_ERROR_NONE) {
            break;
        }

        outBuffers[i] = bufferHandle;
    }

    if (error == GRALLOC1_ERROR_NONE) {
        if (numDescriptors > 1) {
            error = GRALLOC1_ERROR_NOT_SHARED;
        }
    } else {
        for (uint32_t j = 0; j < i; j++) {
            adapter->release(adapter->getBuffer(outBuffers[j]));
            outBuffers[j] = nullptr;
        }
    }

    return error;
}


gralloc1_error_t Gralloc1Device::createDescriptor(gralloc1_buffer_descriptor_t* outDescriptor) {
    auto descriptorId = sNextBufferDescriptorId++;
    std::lock_guard<std::mutex> lock(mDescriptorMutex);
    mDescriptors.emplace(descriptorId,
            std::make_shared<Descriptor>(this, descriptorId));

    ALOGV("Created descriptor %" PRIu64, descriptorId);

    *outDescriptor = descriptorId;
    return GRALLOC1_ERROR_NONE;
}

gralloc1_error_t Gralloc1Device::destroyDescriptor(gralloc1_buffer_descriptor_t descriptor) {
    ALOGV("Destroying descriptor %" PRIu64, descriptor);

    std::lock_guard<std::mutex> lock(mDescriptorMutex);
    if (mDescriptors.count(descriptor) == 0) {
        return GRALLOC1_ERROR_BAD_DESCRIPTOR;
    }

    mDescriptors.erase(descriptor);
    return GRALLOC1_ERROR_NONE;
}

Gralloc1Device::Buffer::Buffer(buffer_handle_t handle,
        gralloc1_backing_store_t store, const Descriptor& descriptor,
        uint32_t stride, bool wasAllocated)
  : mHandle(handle),
    mReferenceCount(1),
    mStore(store),
    mDescriptor(descriptor),
    mStride(stride),
    mWasAllocated(wasAllocated) {}

gralloc1_error_t Gralloc1Device::allocate(
        gralloc1_buffer_descriptor_t id,
        const std::shared_ptr<Descriptor>& descriptor,
        buffer_handle_t* outBufferHandle)
{
    ALOGV("allocate(%" PRIu64 ")", id);

    // If this function is being called, it's because we handed out its function
    // pointer, which only occurs when mDevice has been loaded successfully and
    // we are permitted to allocate
    int usage = android_convertGralloc1To0Usage(
            descriptor->producerUsage, descriptor->consumerUsage);
    buffer_handle_t handle = nullptr;
    int stride = 0;
    ALOGV("Calling alloc(%p, %u, %u, %i, %u)", mModule, descriptor->width,
            descriptor->height, descriptor->format, usage);
    auto error = drm_alloc(mModule,
    		static_cast<int>(descriptor->width),
            static_cast<int>(descriptor->height), descriptor->format,
            usage, &handle, &stride);
    if (error != 0) {
        ALOGE("gralloc0 allocation failed: %d (%s)", error,
                strerror(-error));
        return GRALLOC1_ERROR_NO_RESOURCES;
    }
    ALOGV("allocated(%p)", handle);
	((struct gralloc_drm_handle_t *)handle)->backing_store = id;

    *outBufferHandle = handle;
    auto buffer = std::make_shared<Buffer>(handle, id, *descriptor, stride,
            true);

    std::lock_guard<std::mutex> lock(mBufferMutex);
    mBuffers.emplace(handle, std::move(buffer));

    return GRALLOC1_ERROR_NONE;
}

gralloc1_error_t Gralloc1Device::retain(const std::shared_ptr<Buffer>& buffer) {
    std::lock_guard<std::mutex> lock(mBufferMutex);
    buffer->retain();
    return GRALLOC1_ERROR_NONE;
}

gralloc1_error_t Gralloc1Device::retain(buffer_handle_t handle) {
    ALOGV("retain(%p)", handle);

    std::lock_guard<std::mutex> lock(mBufferMutex);
    if (mBuffers.count(handle) != 0) {
        mBuffers[handle]->retain();
        return GRALLOC1_ERROR_NONE;
    }

    ALOGV("Calling registerBuffer(%p)", handle);
    int result = drm_register(mModule, handle);
    if (result != 0) {
        ALOGE("gralloc0 register failed: %d", result);
        return GRALLOC1_ERROR_NO_RESOURCES;
    }

	struct gralloc_drm_handle_t *drm_handle =
		(struct gralloc_drm_handle_t *) handle;

    gralloc1_buffer_descriptor_t id = drm_handle->backing_store;
    gralloc1_producer_usage_t producerUsage;
    gralloc1_consumer_usage_t consumerUsage;
    android_convertGralloc0To1Usage((int32_t)drm_handle->usage,
            (uint64_t *)&producerUsage, (uint64_t *)&consumerUsage);

    Descriptor descriptor{this, id};
    descriptor.setDimensions(drm_handle->width, drm_handle->height);
    descriptor.setFormat(drm_handle->format);
    descriptor.setProducerUsage(producerUsage);
    descriptor.setConsumerUsage(consumerUsage);
    auto buffer = std::make_shared<Buffer>(handle, id,
            descriptor, drm_handle->stride, false);
    mBuffers.emplace(handle, std::move(buffer));
    return GRALLOC1_ERROR_NONE;
}

gralloc1_error_t Gralloc1Device::release(const std::shared_ptr<Buffer>& buffer) {
    std::lock_guard<std::mutex> lock(mBufferMutex);
    buffer_handle_t handle = buffer->getHandle();
    ALOGV("release(%p)", handle);
    if (!buffer->release()) {
        return GRALLOC1_ERROR_NONE;
    }

    if (buffer->wasAllocated()) {
        ALOGV("Calling free(%p)", handle);
        int result = drm_free(handle);
        if (result != 0) {
            ALOGE("gralloc0 free failed: %d", result);
        }
    } else {
        ALOGV("Calling unregisterBuffer(%p)", handle);
        int result = drm_unregister(handle);
        if (result != 0) {
            ALOGE("gralloc0 unregister failed: %d", result);
        }
    }

    mBuffers.erase(handle);
    return GRALLOC1_ERROR_NONE;
}


gralloc1_error_t Gralloc1Device::lock(const std::shared_ptr<Buffer>& buffer,
        gralloc1_producer_usage_t producerUsage, gralloc1_consumer_usage_t consumerUsage,
        const gralloc1_rect_t& accessRegion, void** outData, const sp<Fence>& acquireFence) {
        acquireFence->waitForever("Gralloc1On0Adapter::lock");
        int result = drm_lock(buffer->getHandle(),
                static_cast<int32_t>(producerUsage | consumerUsage),
                accessRegion.left, accessRegion.top, accessRegion.width,
                accessRegion.height, outData);
        ALOGV("gralloc0 lock returned %d", result);
        if (result != 0) {
            return GRALLOC1_ERROR_UNSUPPORTED;
        }
    return GRALLOC1_ERROR_NONE;
}

gralloc1_error_t Gralloc1Device::lockFlex(
        const std::shared_ptr<Buffer>& /*buffer*/,
        gralloc1_producer_usage_t /*producerUsage*/,
        gralloc1_consumer_usage_t /*consumerUsage*/,
        const gralloc1_rect_t& /*accessRegion*/,
        struct android_flex_layout* /*outData*/,
        const sp<Fence>& /*acquireFence*/) {
    return GRALLOC1_ERROR_UNSUPPORTED;
}

gralloc1_error_t Gralloc1Device::unlock(
        const std::shared_ptr<Buffer>& buffer,
        sp<Fence>* /*outReleaseFence*/)
{
	int result = drm_unlock(buffer->getHandle());
	if (result != 0) {
		ALOGE("gralloc0 unlock failed: %d", result);
	}
    return GRALLOC1_ERROR_NONE;
}

std::shared_ptr<Gralloc1Device::Descriptor> Gralloc1Device::getDescriptor(
        gralloc1_buffer_descriptor_t descriptor) {
    std::lock_guard<std::mutex> lock(mDescriptorMutex);
    if (mDescriptors.count(descriptor) == 0) {
        return nullptr;
    }
    return mDescriptors[descriptor];
}

std::shared_ptr<Gralloc1Device::Buffer> Gralloc1Device::getBuffer(buffer_handle_t bufferHandle) {
    std::lock_guard<std::mutex> lock(mBufferMutex);
    if (mBuffers.count(bufferHandle) == 0) {
        return nullptr;
    }
    return mBuffers[bufferHandle];
}

std::atomic<gralloc1_buffer_descriptor_t> Gralloc1Device::sNextBufferDescriptorId(1);

} // namespace android
