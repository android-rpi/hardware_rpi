#ifndef ANDROID_HARDWARE_GRAPHICS_ALLOCATOR_V2_0_ALLOCATOR_H
#define ANDROID_HARDWARE_GRAPHICS_ALLOCATOR_V2_0_ALLOCATOR_H

#include <android/hardware/graphics/allocator/2.0/IAllocator.h>
#include <android/hardware/graphics/mapper/2.0/IMapper.h>
#include <mapper-passthrough/2.0/GrallocBufferDescriptor.h>

namespace android {
namespace hardware {
namespace graphics {
namespace allocator {
namespace V2_0 {
namespace implementation {

using common::V1_0::BufferUsage;
using mapper::V2_0::BufferDescriptor;
using mapper::V2_0::Error;
using mapper::V2_0::IMapper;
using mapper::V2_0::passthrough::grallocDecodeBufferDescriptor;

class Allocator : public IAllocator {
  public:
    Allocator();
    ~Allocator();
    Return<void> dumpDebugInfo(dumpDebugInfo_cb _hidl_cb) override;
    Return<void> allocate(const BufferDescriptor& descriptor, uint32_t count,
                IAllocator::allocate_cb hidl_cb) override;
  private:
    Error allocateOneBuffer(const IMapper::BufferDescriptorInfo& descInfo,
                buffer_handle_t* outBufferHandle, uint32_t *outStride);
    void freeBuffers(const std::vector<const native_handle_t*>& buffers);

    struct drm_module_t* mModule;
};

}  // namespace implementation
}  // namespace V2_0
}  // namespace allocator
}  // namespace graphics
}  // namespace hardware
}  // namespace android

#endif  // ANDROID_HARDWARE_GRAPHICS_ALLOCATOR_V2_0_ALLOCATOR_H
