#undef LOG_TAG
#define LOG_TAG "GRALLOC-RPI"
//#define LOG_NDEBUG 0
#include <utils/Log.h>
#include <inttypes.h>

#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <pthread.h>
#include <errno.h>

#include <gralloc_drm.h>
#include <gralloc_drm_priv.h>

static int drm_init(const struct drm_gralloc1_module_t *cmod) {
	struct drm_gralloc1_module_t *mod = (struct drm_gralloc1_module_t *) cmod;
	int err = 0;
	pthread_mutex_lock(&mod->mutex);
	if (!mod->drm) {
		mod->drm = gralloc_drm_create();
		if (!mod->drm)
			err = -EINVAL;
	}
	pthread_mutex_unlock(&mod->mutex);
	return err;
}

static void drm_deinit(const struct drm_gralloc1_module_t *mod) {
	gralloc_drm_destroy(mod->drm);
}

static int drm_register(const struct drm_gralloc1_module_t *mod, buffer_handle_t handle) {
	int err;
	err = drm_init(mod);
	if (err)
		return err;
	return gralloc_drm_handle_register(handle, mod->drm);
}

static int drm_unregister(buffer_handle_t handle) {
	return gralloc_drm_handle_unregister(handle);
}

static int drm_lock(buffer_handle_t handle, int usage, int x, int y, int w, int h, void **ptr) {
	struct gralloc_drm_bo_t *bo;
	int err;
	bo = gralloc_drm_bo_from_handle(handle);
	if (!bo)
		return -EINVAL;
	return gralloc_drm_bo_lock(bo, usage, x, y, w, h, ptr);
}

static int drm_unlock(buffer_handle_t handle) {
	struct gralloc_drm_bo_t *bo;
	bo = gralloc_drm_bo_from_handle(handle);
	if (!bo)
		return -EINVAL;
	gralloc_drm_bo_unlock(bo);
	return 0;
}

static int drm_free(buffer_handle_t handle) {
	struct gralloc_drm_bo_t *bo;
	bo = gralloc_drm_bo_from_handle(handle);
	if (!bo)
		return -EINVAL;
	gralloc_drm_bo_decref(bo);
	return 0;
}

static int drm_alloc(const struct drm_gralloc1_module_t *mod, int w, int h, int format, int usage,
		buffer_handle_t *handle, int *stride) {
	struct gralloc_drm_bo_t *bo;
	int size, bpp, err;
	bpp = gralloc_drm_get_bpp(format);
	if (!bpp) return -EINVAL;
	bo = gralloc_drm_bo_create(mod->drm, w, h, format, usage);
	if (!bo) return -ENOMEM;
	if (gralloc_drm_bo_need_fb(bo)) {
		err = gralloc_drm_bo_add_fb(bo);
		if (err) {
			ALOGE("failed to add fb");
			gralloc_drm_bo_decref(bo);
			return err;
		}
	}
	*handle = gralloc_drm_bo_get_handle(bo, stride);
	/* in pixels */
	*stride /= bpp;
	return 0;
}


#include <hardware/gralloc1.h>
#include "gralloc_rpi.h"

namespace android {

GrallocImpl::GrallocImpl(const struct drm_gralloc1_module_t *module) {
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

GrallocImpl::~GrallocImpl() {
	if (mModule != nullptr) {
		drm_deinit(mModule);
	}
}

int GrallocImpl::CloseDevice(hw_device_t *device) {
	delete getImpl(device);
	return 0;
}

void GrallocImpl::GetCapabilities(struct gralloc1_device */*device*/, uint32_t *outCount,
                                  int32_t /*gralloc1_capability_t*/ *outCapabilities) {
	if (outCapabilities == nullptr) {
		*outCount = 1;
		return;
	}
	if (*outCount >= 1) {
		*outCapabilities = GRALLOC1_CAPABILITY_ON_ADAPTER;
		*outCount = 1;
	}
  return;
}

gralloc1_function_pointer_t GrallocImpl::GetFunction(gralloc1_device_t *device, int32_t function) {
    if (!device) {
        ALOGE("NULL gralloc1_device_t");
	    return nullptr;
	}

    constexpr auto lastFunction = static_cast<int32_t>(GRALLOC1_LAST_ADAPTER_FUNCTION);
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
            // Not provided, since we'll use ALLOCATE_WITH_ID
            return nullptr;
        case GRALLOC1_FUNCTION_ALLOCATE_WITH_ID:
            return reinterpret_cast<gralloc1_function_pointer_t>(AllocateWithId);
            /*if (mDevice != nullptr) {
                return asFP<GRALLOC1_PFN_ALLOCATE_WITH_ID>(allocateWithIdHook);
            } else {
                return nullptr;
            }*/
        case GRALLOC1_FUNCTION_RETAIN:
            return reinterpret_cast<gralloc1_function_pointer_t>(
                    managementHook<&GrallocImpl::retain>);
        case GRALLOC1_FUNCTION_RELEASE:
            return reinterpret_cast<gralloc1_function_pointer_t>(
                    managementHook<&GrallocImpl::release>);
        case GRALLOC1_FUNCTION_RETAIN_GRAPHIC_BUFFER:
            return reinterpret_cast<gralloc1_function_pointer_t>(RetainGraphicBuffer);
        case GRALLOC1_FUNCTION_GET_NUM_FLEX_PLANES:
            return reinterpret_cast<gralloc1_function_pointer_t>(
                    bufferHook<decltype(&Buffer::getNumFlexPlanes),
                    &Buffer::getNumFlexPlanes, uint32_t*>);
        case GRALLOC1_FUNCTION_LOCK:
            return reinterpret_cast<gralloc1_function_pointer_t>(
                    lockHook<void*, &GrallocImpl::lock>);
        case GRALLOC1_FUNCTION_LOCK_FLEX:
            return reinterpret_cast<gralloc1_function_pointer_t>(
                    lockHook<struct android_flex_layout,
                    &GrallocImpl::lockFlex>);
        case GRALLOC1_FUNCTION_LOCK_YCBCR:
            return reinterpret_cast<gralloc1_function_pointer_t>(
                    lockHook<struct android_ycbcr,
                    &GrallocImpl::lockYCbCr>);
        case GRALLOC1_FUNCTION_UNLOCK:
            return reinterpret_cast<gralloc1_function_pointer_t>(Unlock);
        case GRALLOC1_FUNCTION_INVALID:
            ALOGE("Invalid function descriptor");
            return nullptr;
    }

    ALOGE("Unknown function descriptor: %d", function);
    return nullptr;
}

gralloc1_error_t GrallocImpl::AllocateWithId(
        gralloc1_device_t* device, gralloc1_buffer_descriptor_t descriptorId,
        gralloc1_backing_store_t store, buffer_handle_t* outBuffer)
{
    auto descriptor = getImpl(device)->getDescriptor(descriptorId);
    if (!descriptor) {
        return GRALLOC1_ERROR_BAD_DESCRIPTOR;
    }

    buffer_handle_t bufferHandle = nullptr;
    auto error = getImpl(device)->allocate(descriptor, store, &bufferHandle);
    if (error != GRALLOC1_ERROR_NONE) {
        return error;
    }

    *outBuffer = bufferHandle;
    return error;
}


gralloc1_error_t GrallocImpl::createDescriptor(gralloc1_buffer_descriptor_t* outDescriptor) {
    auto descriptorId = sNextBufferDescriptorId++;
    std::lock_guard<std::mutex> lock(mDescriptorMutex);
    mDescriptors.emplace(descriptorId,
            std::make_shared<Descriptor>(this, descriptorId));

    ALOGV("Created descriptor %" PRIu64, descriptorId);

    *outDescriptor = descriptorId;
    return GRALLOC1_ERROR_NONE;
}

gralloc1_error_t GrallocImpl::destroyDescriptor(gralloc1_buffer_descriptor_t descriptor) {
    ALOGV("Destroying descriptor %" PRIu64, descriptor);

    std::lock_guard<std::mutex> lock(mDescriptorMutex);
    if (mDescriptors.count(descriptor) == 0) {
        return GRALLOC1_ERROR_BAD_DESCRIPTOR;
    }

    mDescriptors.erase(descriptor);
    return GRALLOC1_ERROR_NONE;
}

GrallocImpl::Buffer::Buffer(buffer_handle_t handle,
        gralloc1_backing_store_t store, const Descriptor& descriptor,
        uint32_t stride, bool wasAllocated)
  : mHandle(handle),
    mReferenceCount(1),
    mStore(store),
    mDescriptor(descriptor),
    mStride(stride),
    mWasAllocated(wasAllocated) {}

gralloc1_error_t GrallocImpl::allocate(const std::shared_ptr<Descriptor>& descriptor,
        gralloc1_backing_store_t store, buffer_handle_t* outBufferHandle) {
    ALOGV("allocate(%" PRIu64 ", %#" PRIx64 ")", descriptor->id, store);

    // If this function is being called, it's because we handed out its function
    // pointer, which only occurs when mDevice has been loaded successfully and
    // we are permitted to allocate
    int usage = static_cast<int>(descriptor->producerUsage) |
            static_cast<int>(descriptor->consumerUsage);
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

    *outBufferHandle = handle;
    auto buffer = std::make_shared<Buffer>(handle, store, *descriptor, stride,
            true);

    std::lock_guard<std::mutex> lock(mBufferMutex);
    mBuffers.emplace(handle, std::move(buffer));

    return GRALLOC1_ERROR_NONE;
}

gralloc1_error_t GrallocImpl::retain(const std::shared_ptr<Buffer>& buffer) {
    buffer->retain();
    return GRALLOC1_ERROR_NONE;
}

gralloc1_error_t GrallocImpl::release(const std::shared_ptr<Buffer>& buffer) {
    if (!buffer->release()) {
        return GRALLOC1_ERROR_NONE;
    }

    buffer_handle_t handle = buffer->getHandle();
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

    std::lock_guard<std::mutex> lock(mBufferMutex);
    mBuffers.erase(handle);
    return GRALLOC1_ERROR_NONE;
}

gralloc1_error_t GrallocImpl::retain(const android::GraphicBuffer* graphicBuffer) {
    ALOGV("retainGraphicBuffer(%p, %#" PRIx64 ")",
            graphicBuffer->getNativeBuffer()->handle, graphicBuffer->getId());

    buffer_handle_t handle = graphicBuffer->getNativeBuffer()->handle;
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

    Descriptor descriptor{this, sNextBufferDescriptorId++};
    descriptor.setDimensions(graphicBuffer->getWidth(),
            graphicBuffer->getHeight());
    descriptor.setFormat(graphicBuffer->getPixelFormat());
    descriptor.setProducerUsage(
            static_cast<gralloc1_producer_usage_t>(graphicBuffer->getUsage()));
    descriptor.setConsumerUsage(
            static_cast<gralloc1_consumer_usage_t>(graphicBuffer->getUsage()));
    auto buffer = std::make_shared<Buffer>(handle,
            static_cast<gralloc1_backing_store_t>(graphicBuffer->getId()),
            descriptor, graphicBuffer->getStride(), false);
    mBuffers.emplace(handle, std::move(buffer));
    return GRALLOC1_ERROR_NONE;
}

gralloc1_error_t GrallocImpl::lock(const std::shared_ptr<Buffer>& buffer,
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

gralloc1_error_t GrallocImpl::lockFlex(
        const std::shared_ptr<Buffer>& /*buffer*/,
        gralloc1_producer_usage_t /*producerUsage*/,
        gralloc1_consumer_usage_t /*consumerUsage*/,
        const gralloc1_rect_t& /*accessRegion*/,
        struct android_flex_layout* /*outData*/,
        const sp<Fence>& /*acquireFence*/) {
    return GRALLOC1_ERROR_UNSUPPORTED;
}
gralloc1_error_t GrallocImpl::lockYCbCr(
        const std::shared_ptr<Buffer>& /*buffer*/,
        gralloc1_producer_usage_t /*producerUsage*/,
        gralloc1_consumer_usage_t /*consumerUsage*/,
        const gralloc1_rect_t& /*accessRegion*/,
        struct android_ycbcr* /*outFlex*/,
        const sp<Fence>& /*acquireFence*/) {
    return GRALLOC1_ERROR_UNSUPPORTED;
}

gralloc1_error_t GrallocImpl::unlock(
        const std::shared_ptr<Buffer>& buffer,
        sp<Fence>* /*outReleaseFence*/)
{
	int result = drm_unlock(buffer->getHandle());
	if (result != 0) {
		ALOGE("gralloc0 unlock failed: %d", result);
	}
    return GRALLOC1_ERROR_NONE;
}

std::shared_ptr<GrallocImpl::Descriptor> GrallocImpl::getDescriptor(
        gralloc1_buffer_descriptor_t descriptor) {
    std::lock_guard<std::mutex> lock(mDescriptorMutex);
    if (mDescriptors.count(descriptor) == 0) {
        return nullptr;
    }
    return mDescriptors[descriptor];
}

std::shared_ptr<GrallocImpl::Buffer> GrallocImpl::getBuffer(buffer_handle_t bufferHandle) {
    std::lock_guard<std::mutex> lock(mBufferMutex);
    if (mBuffers.count(bufferHandle) == 0) {
        return nullptr;
    }
    return mBuffers[bufferHandle];
}

std::atomic<gralloc1_buffer_descriptor_t> GrallocImpl::sNextBufferDescriptorId(1);

} // namespace android


static int drm_mod_perform(const struct drm_gralloc1_module_t *mod, int op, ...)
{
	struct drm_gralloc1_module_t *dmod = (struct drm_gralloc1_module_t *) mod;
	va_list args;
	int err;

	err = drm_init(dmod);
	if (err)
		return err;

	va_start(args, op);
	switch (op) {
	case GRALLOC_MODULE_PERFORM_GET_DRM_FD:
		{
			int *fd = va_arg(args, int *);
			*fd = gralloc_drm_get_fd(dmod->drm);
			err = 0;
		}
		break;
	/* should we remove this and next ops, and make it transparent? */
	case GRALLOC_MODULE_PERFORM_GET_DRM_MAGIC:
		{
			int32_t *magic = va_arg(args, int32_t *);
			err = gralloc_drm_get_magic(dmod->drm, magic);
		}
		break;
	case GRALLOC_MODULE_PERFORM_AUTH_DRM_MAGIC:
		{
			int32_t magic = va_arg(args, int32_t);
			err = gralloc_drm_auth_magic(dmod->drm, magic);
		}
		break;
	case GRALLOC_MODULE_PERFORM_ENTER_VT:
		{
			err = gralloc_drm_set_master(dmod->drm);
		}
		break;
	case GRALLOC_MODULE_PERFORM_LEAVE_VT:
		{
			gralloc_drm_drop_master(dmod->drm);
			err = 0;
		}
		break;
	default:
		err = -EINVAL;
		break;
	}
	va_end(args);

	return err;
}

int drm_mod_open_fb0(struct drm_gralloc1_module_t *dmod, struct hw_device_t **dev);

static int gralloc1_device_open(const struct hw_module_t *module,
		const char *name, struct hw_device_t **device)
{
	  int status = -EINVAL;
	  if (!strcmp(name, GRALLOC_HARDWARE_MODULE_ID)) {
	      const struct drm_gralloc1_module_t *m = reinterpret_cast<const struct drm_gralloc1_module_t *>(module);
	      android::GrallocImpl *dev = new android::GrallocImpl(m);
	      *device = reinterpret_cast<hw_device_t *>(dev);
	      status = 0;
	  } else if (strcmp(name, GRALLOC_HARDWARE_FB0) == 0) {
		  struct drm_gralloc1_module_t *dmod = (struct drm_gralloc1_module_t *) module;
		  status = drm_mod_open_fb0(dmod, device);
	  }
	  return status;
}

static struct hw_module_methods_t private_module_methods = {
	.open = gralloc1_device_open
};

struct drm_gralloc1_module_t HAL_MODULE_INFO_SYM = {
	.common = {
		.tag = HARDWARE_MODULE_TAG,
		.version_major = 0x100,
		.version_minor = 0,
		.id = GRALLOC_HARDWARE_MODULE_ID,
		.name = "RPi Graphics Memory Allocator",
		.author = "Peter Yoon",
		.methods = &private_module_methods
	},
	.perform = drm_mod_perform,
	.mutex = PTHREAD_MUTEX_INITIALIZER,
	.drm = NULL
};
