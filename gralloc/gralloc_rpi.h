#ifndef _GRALLOC_RPI_H_
#define _GRALLOC_RPI_H_

#include <ui/Fence.h>
#include <ui/GraphicBuffer.h>

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

static const auto GRALLOC1_CAPABILITY_ON_ADAPTER =
        static_cast<gralloc1_capability_t>(GRALLOC1_LAST_CAPABILITY + 1);
static const auto GRALLOC1_FUNCTION_RETAIN_GRAPHIC_BUFFER =
        static_cast<gralloc1_function_descriptor_t>(GRALLOC1_LAST_FUNCTION + 1);
static const auto GRALLOC1_FUNCTION_ALLOCATE_WITH_ID =
        static_cast<gralloc1_function_descriptor_t>(GRALLOC1_LAST_FUNCTION + 2);
static const auto GRALLOC1_FUNCTION_LOCK_YCBCR =
        static_cast<gralloc1_function_descriptor_t>(GRALLOC1_LAST_FUNCTION + 3);
static const auto GRALLOC1_LAST_ADAPTER_FUNCTION = GRALLOC1_FUNCTION_LOCK_YCBCR;

namespace android {

#define getImpl(exp) reinterpret_cast<GrallocImpl *>(exp)

class GrallocImpl : public gralloc1_device_t
{
public:
	GrallocImpl(const struct drm_gralloc1_module_t* module);
	~GrallocImpl();

private:
	struct Descriptor;
	class Buffer;
	static int CloseDevice(hw_device_t *device);
	static void GetCapabilities(struct gralloc1_device *device, uint32_t *count,
            int32_t /*gralloc1_capability_t*/ *capabilities);
	static gralloc1_function_pointer_t GetFunction(struct gralloc1_device *device,
	        int32_t /*gralloc1_function_descriptor_t*/ descriptor);
    static void Dump(gralloc1_device_t* /*device*/, uint32_t* size, char* /*buffer*/) {
        *size = 0;
    }
    static gralloc1_error_t CreateDescriptor(gralloc1_device_t *device,
            gralloc1_buffer_descriptor_t *descriptor) {
    	if (!device) return GRALLOC1_ERROR_BAD_DESCRIPTOR;
    	else return getImpl(device)->createDescriptor(descriptor);
    }
    static gralloc1_error_t DestroyDescriptor(gralloc1_device_t* device,
            gralloc1_buffer_descriptor_t descriptor) {
    	if (!device) return GRALLOC1_ERROR_BAD_DESCRIPTOR;
    	else return getImpl(device)->destroyDescriptor(descriptor);
    }
    static gralloc1_error_t SetConsumerUsage(gralloc1_device_t* device,
            gralloc1_buffer_descriptor_t descriptor, gralloc1_consumer_usage_t usage) {
        return callDescriptorFunction(device, descriptor, &Descriptor::setConsumerUsage, usage);
    }
    static gralloc1_error_t SetDimensions(gralloc1_device_t* device,
            gralloc1_buffer_descriptor_t descriptor, uint32_t width, uint32_t height) {
        return callDescriptorFunction(device, descriptor, &Descriptor::setDimensions,
                width, height);
    }
    static gralloc1_error_t SetFormat(gralloc1_device_t* device,
            gralloc1_buffer_descriptor_t descriptor, int32_t format) {
        return callDescriptorFunction(device, descriptor, &Descriptor::setFormat, format);
    }
    static gralloc1_error_t SetProducerUsage(gralloc1_device_t* device,
            gralloc1_buffer_descriptor_t descriptor, gralloc1_producer_usage_t usage) {
        return callDescriptorFunction(device, descriptor,
                &Descriptor::setProducerUsage, usage);
    }
    static gralloc1_error_t GetConsumerUsage(gralloc1_device_t* device, buffer_handle_t bufferHandle,
            gralloc1_consumer_usage_t* usage) {
        *usage = GRALLOC1_CONSUMER_USAGE_NONE;
        return callBufferFunction(device, bufferHandle,
                &Buffer::getConsumerUsage, usage);
    }
    static gralloc1_error_t GetProducerUsage(gralloc1_device_t* device, buffer_handle_t bufferHandle,
            gralloc1_producer_usage_t* usage) {
        *usage = GRALLOC1_PRODUCER_USAGE_NONE;
        return callBufferFunction(device, bufferHandle,
                &Buffer::getProducerUsage, usage);
    }
    static gralloc1_error_t AllocateWithId(gralloc1_device_t* device,
            gralloc1_buffer_descriptor_t descriptors,
            gralloc1_backing_store_t id, buffer_handle_t* outBuffer);
    static gralloc1_error_t RetainGraphicBuffer(gralloc1_device_t* device,
            const GraphicBuffer* buffer) {
        return getImpl(device)->retain(buffer);
    }
    static gralloc1_error_t Unlock(gralloc1_device_t* device,
            buffer_handle_t bufferHandle, int32_t* outReleaseFenceFd) {
        auto impl = getImpl(device);
        auto buffer = impl->getBuffer(bufferHandle);
        if (!buffer) {
            return GRALLOC1_ERROR_BAD_HANDLE;
        }
        sp<Fence> releaseFence = Fence::NO_FENCE;
        auto error = impl->unlock(buffer, &releaseFence);
        if (error == GRALLOC1_ERROR_NONE) {
            *outReleaseFenceFd = releaseFence->dup();
        }
        return error;
    }

    gralloc1_error_t createDescriptor(gralloc1_buffer_descriptor_t *descriptor);
    gralloc1_error_t destroyDescriptor(gralloc1_buffer_descriptor_t descriptor);
    gralloc1_error_t allocate(const std::shared_ptr<Descriptor>& descriptor,
            gralloc1_backing_store_t id, buffer_handle_t* outBuffer);
    gralloc1_error_t retain(const std::shared_ptr<Buffer>& buffer);
    gralloc1_error_t release(const std::shared_ptr<Buffer>& buffer);
    gralloc1_error_t retain(const GraphicBuffer* buffer);
    gralloc1_error_t lock(const std::shared_ptr<Buffer>& buffer,
            gralloc1_producer_usage_t producerUsage,
            gralloc1_consumer_usage_t consumerUsage,
            const gralloc1_rect_t& accessRegion, void** outData,
            const sp<Fence>& acquireFence);
    gralloc1_error_t lockFlex(const std::shared_ptr<Buffer>& buffer,
            gralloc1_producer_usage_t producerUsage,
            gralloc1_consumer_usage_t consumerUsage,
            const gralloc1_rect_t& accessRegion,
            struct android_flex_layout* outFlex,
            const sp<Fence>& acquireFence);
    gralloc1_error_t lockYCbCr(const std::shared_ptr<Buffer>& buffer,
            gralloc1_producer_usage_t producerUsage,
            gralloc1_consumer_usage_t consumerUsage,
            const gralloc1_rect_t& accessRegion,
            struct android_ycbcr* outFlex,
            const sp<Fence>& acquireFence);
    gralloc1_error_t unlock(const std::shared_ptr<Buffer>& buffer,
            sp<Fence>* outReleaseFence);

    struct Descriptor : public std::enable_shared_from_this<Descriptor> {
        Descriptor(GrallocImpl* impl,
        		gralloc1_buffer_descriptor_t id)
          : impl(impl),
            id(id),
            width(0),
            height(0),
            format(HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED),
            producerUsage(GRALLOC1_PRODUCER_USAGE_NONE),
            consumerUsage(GRALLOC1_CONSUMER_USAGE_NONE) {}

        gralloc1_error_t setDimensions(uint32_t w, uint32_t h) {
            width = w;
            height = h;
            return GRALLOC1_ERROR_NONE;
        }

        gralloc1_error_t setFormat(int32_t f) {
            format = f;
            return GRALLOC1_ERROR_NONE;
        }

        gralloc1_error_t setProducerUsage(gralloc1_producer_usage_t usage) {
            producerUsage = usage;
            return GRALLOC1_ERROR_NONE;
        }

        gralloc1_error_t setConsumerUsage(gralloc1_consumer_usage_t usage) {
            consumerUsage = usage;
            return GRALLOC1_ERROR_NONE;
        }

        GrallocImpl* const impl;
        const gralloc1_buffer_descriptor_t id;

        uint32_t width;
        uint32_t height;
        int32_t format;
        gralloc1_producer_usage_t producerUsage;
        gralloc1_consumer_usage_t consumerUsage;
    };

    template <typename ...Args>
    static gralloc1_error_t callDescriptorFunction(gralloc1_device_t* device,
            gralloc1_buffer_descriptor_t descriptor,
            gralloc1_error_t (Descriptor::*member)(Args...), Args... args) {
        auto descriptorPtr = getImpl(device)->getDescriptor(descriptor);
        if (!descriptorPtr) return GRALLOC1_ERROR_BAD_DESCRIPTOR;
        return ((*descriptorPtr).*member)(std::forward<Args>(args)...);
    }

    class Buffer {
    public:
        Buffer(buffer_handle_t handle, gralloc1_backing_store_t store,
                const Descriptor& descriptor, uint32_t stride,
                bool wasAllocated);

        buffer_handle_t getHandle() const { return mHandle; }

        void retain() { ++mReferenceCount; }

        // Returns true if the reference count has dropped to 0, indicating that
        // the buffer needs to be released
        bool release() { return --mReferenceCount == 0; }

        bool wasAllocated() const { return mWasAllocated; }

        gralloc1_error_t getBackingStore(
                gralloc1_backing_store_t* outStore) const {
            *outStore = mStore;
            return GRALLOC1_ERROR_NONE;
        }

        gralloc1_error_t getConsumerUsage(
                gralloc1_consumer_usage_t* outUsage) const {
            *outUsage = mDescriptor.consumerUsage;
            return GRALLOC1_ERROR_NONE;
        }

        gralloc1_error_t getDimensions(uint32_t* outWidth,
                uint32_t* outHeight) const {
            *outWidth = mDescriptor.width;
            *outHeight = mDescriptor.height;
            return GRALLOC1_ERROR_NONE;
        }

        gralloc1_error_t getFormat(int32_t* outFormat) const {
            *outFormat = mDescriptor.format;
            return GRALLOC1_ERROR_NONE;
        }

        gralloc1_error_t getNumFlexPlanes(uint32_t* outNumPlanes) const {
            // TODO: This is conservative, and we could do better by examining
            // the format, but it won't hurt anything for now
            *outNumPlanes = 4;
            return GRALLOC1_ERROR_NONE;
        }

        gralloc1_error_t getProducerUsage(
                gralloc1_producer_usage_t* outUsage) const {
            *outUsage = mDescriptor.producerUsage;
            return GRALLOC1_ERROR_NONE;
        }

        gralloc1_error_t getStride(uint32_t* outStride) const {
            *outStride = mStride;
            return GRALLOC1_ERROR_NONE;
        }

    private:
        const buffer_handle_t mHandle;
        size_t mReferenceCount;

        // Since we're adapting to gralloc0, there will always be a 1:1
        // correspondence between buffer handles and backing stores, and the
        // backing store ID will be the same as the GraphicBuffer unique ID
        const gralloc1_backing_store_t mStore;

        const Descriptor mDescriptor;
        const uint32_t mStride;

        // Whether this buffer allocated in this process (as opposed to just
        // being retained here), which determines whether to free or unregister
        // the buffer when this Buffer is released
        const bool mWasAllocated;
    };

    template <typename ...Args>
    static gralloc1_error_t callBufferFunction(gralloc1_device_t* device,
            buffer_handle_t bufferHandle,
            gralloc1_error_t (Buffer::*member)(Args...) const, Args... args) {
        auto buffer = getImpl(device)->getBuffer(bufferHandle);
        if (!buffer) GRALLOC1_ERROR_BAD_HANDLE;
        return ((*buffer).*member)(std::forward<Args>(args)...);
    }

    template <typename MF, MF memFunc, typename ...Args>
    static gralloc1_error_t bufferHook(gralloc1_device_t* device,
            buffer_handle_t bufferHandle, Args... args) {
        return GrallocImpl::callBufferFunction(device, bufferHandle,
                memFunc, std::forward<Args>(args)...);
    }

    template <gralloc1_error_t (GrallocImpl::*member)(
            const std::shared_ptr<Buffer>& buffer)>
    static gralloc1_error_t managementHook(gralloc1_device_t* device,
            buffer_handle_t bufferHandle) {
        auto impl = getImpl(device);
        auto buffer = impl->getBuffer(bufferHandle);
        if (!buffer) {
            return GRALLOC1_ERROR_BAD_HANDLE;
        }
        return ((*impl).*member)(buffer);
    }

    template <typename OUT, gralloc1_error_t (GrallocImpl::*member)(
            const std::shared_ptr<Buffer>&, gralloc1_producer_usage_t,
            gralloc1_consumer_usage_t, const gralloc1_rect_t&, OUT*,
            const sp<Fence>&)>
    static gralloc1_error_t lockHook(gralloc1_device_t* device,
            buffer_handle_t bufferHandle,
            uint64_t /*gralloc1_producer_usage_t*/ uintProducerUsage,
            uint64_t /*gralloc1_consumer_usage_t*/ uintConsumerUsage,
            const gralloc1_rect_t* accessRegion, OUT* outData,
            int32_t acquireFenceFd) {
        auto impl = getImpl(device);

        // Exactly one of producer and consumer usage must be *_USAGE_NONE,
        // but we can't check this until the upper levels of the framework
        // correctly distinguish between producer and consumer usage
        /*
        bool hasProducerUsage =
                uintProducerUsage != GRALLOC1_PRODUCER_USAGE_NONE;
        bool hasConsumerUsage =
                uintConsumerUsage != GRALLOC1_CONSUMER_USAGE_NONE;
        if (hasProducerUsage && hasConsumerUsage ||
                !hasProducerUsage && !hasConsumerUsage) {
            return static_cast<int32_t>(GRALLOC1_ERROR_BAD_VALUE);
        }
        */

        auto producerUsage =
                static_cast<gralloc1_producer_usage_t>(uintProducerUsage);
        auto consumerUsage =
                static_cast<gralloc1_consumer_usage_t>(uintConsumerUsage);

        if (!outData) {
            const auto producerCpuUsage = GRALLOC1_PRODUCER_USAGE_CPU_READ |
                    GRALLOC1_PRODUCER_USAGE_CPU_WRITE;
            if ((producerUsage & producerCpuUsage) != 0) {
                return GRALLOC1_ERROR_BAD_VALUE;
            }
            if ((consumerUsage & GRALLOC1_CONSUMER_USAGE_CPU_READ) != 0) {
                return GRALLOC1_ERROR_BAD_VALUE;
            }
        }

        auto buffer = impl->getBuffer(bufferHandle);
        if (!buffer) {
            return GRALLOC1_ERROR_BAD_HANDLE;
        }

        if (!accessRegion) {
            ALOGE("accessRegion is null");
            return GRALLOC1_ERROR_BAD_VALUE;
        }

        sp<Fence> acquireFence{new Fence(acquireFenceFd)};
        auto error = ((*impl).*member)(buffer, producerUsage, consumerUsage,
                *accessRegion, outData, acquireFence);
        return error;
    }

    const struct drm_gralloc1_module_t* mModule;

    std::shared_ptr<Descriptor> getDescriptor(gralloc1_buffer_descriptor_t descriptor);
    std::shared_ptr<Buffer> getBuffer(buffer_handle_t bufferHandle);
    static std::atomic<gralloc1_buffer_descriptor_t> sNextBufferDescriptorId;
    std::mutex mDescriptorMutex;
    std::unordered_map<gralloc1_buffer_descriptor_t, std::shared_ptr<Descriptor>> mDescriptors;
    std::mutex mBufferMutex;
    std::unordered_map<buffer_handle_t, std::shared_ptr<Buffer>> mBuffers;
};

} // namespace android

#endif /* _GRALLOC_RPI_H_ */
