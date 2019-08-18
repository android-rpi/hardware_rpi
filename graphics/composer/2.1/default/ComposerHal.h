#ifndef _COMPOSERHAL_H
#define _COMPOSERHAL_H

#include <android/hardware/graphics/composer/2.1/IComposerClient.h>
#include <composer-hal/2.1/ComposerResources.h>

#include "Hwc2Device.h"

namespace android {
namespace hardware {
namespace graphics {
namespace composer {
namespace V2_1 {
namespace implementation {

using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::sp;

using common::V1_0::PixelFormat;
using common::V1_0::Dataspace;
using common::V1_0::ColorMode;
using composer::V2_1::hal::ComposerResources;

class ComposerHal {
  public:
	ComposerHal();

	std::string dumpDebugInfo();

    class EventCallback {
       public:
        virtual ~EventCallback() = default;
        virtual void onHotplug(Display display, IComposerCallback::Connection connected) = 0;
        virtual void onVsync(Display display, int64_t timestamp) = 0;
    };

    void registerEventCallback(EventCallback* callback);
    void unregisterEventCallback();

    Error createLayer(Display display, Layer* outLayer);
    Error destroyLayer(Display display, Layer layer);
    Error getClientTargetSupport(Display display, uint32_t width, uint32_t height,
                                 PixelFormat format, Dataspace dataspace);
    Error getDisplayAttribute(Display display, Config config,
                              IComposerClient::Attribute attribute, int32_t* outValue);
    Error getDisplayName(Display display, hidl_string* outName);

    Error setVsyncEnabled(Display display, IComposerClient::Vsync enabled);
    Error setClientTarget(Display display, buffer_handle_t target, int32_t acquireFence,
                          int32_t dataspace, const std::vector<hwc_rect_t>& damage);
    Error validateDisplay(Display display, std::vector<Layer>* outChangedLayers,
                          std::vector<IComposerClient::Composition>* outCompositionTypes,
                          uint32_t* outDisplayRequestMask, std::vector<Layer>* outRequestedLayers,
                          std::vector<uint32_t>* outRequestMasks);
    Error presentDisplay(Display display, int32_t* outPresentFence,
                         std::vector<Layer>* outLayers, std::vector<int32_t>* outReleaseFences);
    Error acceptDisplayChanges(Display display);

    Error setLayerCompositionType(Display display, Layer layer, int32_t type);

  private:

    static void hotplugHook(hwc2_callback_data_t callbackData, hwc2_display_t display,
                            int32_t connected) {
        auto hal = static_cast<ComposerHal*>(callbackData);
        hal->mEventCallback->onHotplug(display,
                                       static_cast<IComposerCallback::Connection>(connected));
    }

    static void vsyncHook(hwc2_callback_data_t callbackData, hwc2_display_t display,
                          int64_t timestamp) {
        auto hal = static_cast<ComposerHal*>(callbackData);
        hal->mEventCallback->onVsync(display, timestamp);
    }

    std::unique_ptr<Hwc2Device> mDevice;

    std::unordered_set<hwc2_capability_t> mCapabilities;

    EventCallback* mEventCallback = nullptr;
    std::atomic<bool> mMustValidateDisplay{true};
};

}  // namespace implementation
}  // namespace V2_1
}  // namespace composer
}  // namespace graphics
}  // namespace hardware
}  // namespace android

#endif  // _COMPOSERHAL_H
