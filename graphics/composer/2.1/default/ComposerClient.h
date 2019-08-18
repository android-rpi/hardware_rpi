#ifndef ANDROID_HARDWARE_GRAPHICS_COMPOSER_V2_1_COMPOSERCLIENT_H
#define ANDROID_HARDWARE_GRAPHICS_COMPOSER_V2_1_COMPOSERCLIENT_H

#include <android/hardware/graphics/composer/2.1/IComposerClient.h>
#include <android/hardware/graphics/composer/2.1/IComposerCallback.h>
#include <composer-hal/2.1/ComposerResources.h>

#include "ComposerHal.h"
#include "ComposerCommandEngine.h"

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
using common::V1_0::Hdr;
using composer::V2_1::hal::ComposerResources;

class ComposerClient : public IComposerClient {
  public:
	ComposerClient(ComposerHal *hal);
	~ComposerClient();

    class HalEventCallback : public ComposerHal::EventCallback {
       public:
        HalEventCallback(const sp<IComposerCallback> callback, ComposerResources* resources)
            : mCallback(callback), mResources(resources) {}

        void onHotplug(Display display, IComposerCallback::Connection connected) {
            if (connected == IComposerCallback::Connection::CONNECTED) {
                mResources->addPhysicalDisplay(display);
            } else if (connected == IComposerCallback::Connection::DISCONNECTED) {
                mResources->removeDisplay(display);
            }
            auto ret = mCallback->onHotplug(display, connected);
            ALOGE_IF(!ret.isOk(), "failed to send onHotplug: %s", ret.description().c_str());
        }

        void onVsync(Display display, int64_t timestamp) {
            auto ret = mCallback->onVsync(display, timestamp);
            ALOGE_IF(!ret.isOk(), "failed to send onVsync: %s", ret.description().c_str());
        }

       protected:
        const sp<IComposerCallback> mCallback;
        ComposerResources* const mResources;
    };

    Return<void> registerCallback(const sp<IComposerCallback>& callback) override;
    Return<uint32_t> getMaxVirtualDisplayCount() override;
    Return<void> createVirtualDisplay(uint32_t width, uint32_t height, PixelFormat formatHint,
                                      uint32_t outputBufferSlotCount,
                                      IComposerClient::createVirtualDisplay_cb hidl_cb) override;
    Return<Error> destroyVirtualDisplay(Display display) override;
    Return<void> createLayer(Display display, uint32_t bufferSlotCount,
                             IComposerClient::createLayer_cb hidl_cb) override;
    Return<Error> destroyLayer(Display display, Layer layer) override;
    Return<void> getActiveConfig(Display display,IComposerClient::getActiveConfig_cb hidl_cb) override;
    Return<Error> getClientTargetSupport(Display display, uint32_t width, uint32_t height,
                                         PixelFormat format, Dataspace dataspace) override;
    Return<void> getColorModes(Display display, IComposerClient::getColorModes_cb hidl_cb) override;
    Return<void> getDisplayAttribute(Display display, Config config, IComposerClient::Attribute attribute,
                                     IComposerClient::getDisplayAttribute_cb hidl_cb) override;
    Return<void> getDisplayConfigs(Display display, IComposerClient::getDisplayConfigs_cb hidl_cb) override;
    Return<void> getDisplayName(Display display, IComposerClient::getDisplayName_cb hidl_cb) override;
    Return<void> getDisplayType(Display display, IComposerClient::getDisplayType_cb hidl_cb) override;
    Return<void> getDozeSupport(Display display, IComposerClient::getDozeSupport_cb hidl_cb) override;
    Return<void> getHdrCapabilities(Display display, IComposerClient::getHdrCapabilities_cb hidl_cb) override;
    Return<Error> setClientTargetSlotCount(Display display, uint32_t clientTargetSlotCount) override;
    Return<Error> setActiveConfig(Display display, Config config) override;
    Return<Error> setColorMode(Display display, ColorMode mode) override;
    Return<Error> setPowerMode(Display display, IComposerClient::PowerMode mode) override;
    Return<Error> setVsyncEnabled(Display display, IComposerClient::Vsync enabled) override;

    Return<Error> setInputCommandQueue(const MQDescriptorSync<uint32_t>& descriptor) override;
    Return<void> getOutputCommandQueue(IComposerClient::getOutputCommandQueue_cb hidl_cb) override;
    Return<void> executeCommands(uint32_t inLength, const hidl_vec<hidl_handle>& inHandles,
                                 IComposerClient::executeCommands_cb hidl_cb) override;

    void setOnClientDestroyed(std::function<void()> onClientDestroyed) {
        mOnClientDestroyed = onClientDestroyed;
    }

  private:

    ComposerHal *mHal;

    std::unique_ptr<ComposerResources> mResources;

    std::mutex mCommandEngineMutex;
    std::unique_ptr<ComposerCommandEngine> mCommandEngine;

    std::function<void()> mOnClientDestroyed;
    std::unique_ptr<HalEventCallback> mHalEventCallback;

    std::unique_ptr<ComposerResources> createResources() {
        return ComposerResources::create();
    }

    std::unique_ptr<ComposerCommandEngine> createCommandEngine() {
        return std::make_unique<ComposerCommandEngine>(mHal, mResources.get());
    }

    void destroyResources();
};

}  // namespace implementation
}  // namespace V2_1
}  // namespace composer
}  // namespace graphics
}  // namespace hardware
}  // namespace android

#endif  // ANDROID_HARDWARE_GRAPHICS_COMPOSER_V2_1_COMPOSERCLIENT_H
