#ifndef ANDROID_HARDWARE_GRAPHICS_MAPPER_V2_0_MAPPER_H
#define ANDROID_HARDWARE_GRAPHICS_MAPPER_V2_0_MAPPER_H

#include <ui/Fence.h>
#include <android/hardware/graphics/mapper/2.0/IMapper.h>

namespace android {
namespace hardware {
namespace graphics {
namespace mapper {
namespace V2_0 {
namespace implementation {

using common::V1_0::BufferUsage;
using common::V1_0::PixelFormat;
using mapper::V2_0::passthrough::grallocEncodeBufferDescriptor;

class Mapper : public IMapper {
  public:
    Mapper();

    Return<void> createDescriptor(const IMapper::BufferDescriptorInfo& descriptorInfo,
        createDescriptor_cb hidl_cb) override;

    Return<void> importBuffer(const hidl_handle& rawHandle,
		IMapper::importBuffer_cb hidl_cb) override;

    Return<Error> freeBuffer(void* buffer) override;

    Return<void> lock(void* buffer, uint64_t cpuUsage, const IMapper::Rect& accessRegion,
                  const hidl_handle& acquireFence, IMapper::lock_cb hidl_cb) override;

    Return<void> lockYCbCr(void* buffer, uint64_t cpuUsage, const IMapper::Rect& accessRegion,
            const hidl_handle& acquireFence, IMapper::lockYCbCr_cb hidl_cb) override;

    Return<void> unlock(void* buffer, IMapper::unlock_cb hidl_cb) override;

  private:
    struct drm_module_t* mModule;
};

extern "C" IMapper* HIDL_FETCH_IMapper(const char* name);

}  // namespace implementation
}  // namespace V2_0
}  // namespace mapper
}  // namespace graphics
}  // namespace hardware
}  // namespace android

#endif  // ANDROID_HARDWARE_GRAPHICS_MAPPER_V2_0_MAPPER_H
