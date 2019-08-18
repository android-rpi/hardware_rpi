#ifndef ANDROID_HARDWARE_GRAPHICS_COMPOSER_V2_1_COMPOSER_H
#define ANDROID_HARDWARE_GRAPHICS_COMPOSER_V2_1_COMPOSER_H

#include <android/hardware/graphics/composer/2.1/IComposer.h>

#include "ComposerClient.h"

namespace android {
namespace hardware {
namespace graphics {
namespace composer {
namespace V2_1 {
namespace implementation {

using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::sp;

class Composer : public IComposer {
  public:
    Composer();

    Return<void> getCapabilities(getCapabilities_cb _hidl_cb) override;
    Return<void> dumpDebugInfo(dumpDebugInfo_cb _hidl_cb) override;
    Return<void> createClient(createClient_cb _hidl_cb) override;

  private:

    bool waitForClientDestroyedLocked(std::unique_lock<std::mutex>& lock);
    void onClientDestroyed();
    IComposerClient* createClient();

    std::unique_ptr<ComposerHal> mHal;

    std::mutex mClientMutex;
    wp<IComposerClient> mClient;
    std::condition_variable mClientDestroyedCondition;

};

extern "C" IComposer* HIDL_FETCH_IComposer(const char* name);

}  // namespace implementation
}  // namespace V2_1
}  // namespace composer
}  // namespace graphics
}  // namespace hardware
}  // namespace android

#endif  // ANDROID_HARDWARE_GRAPHICS_COMPOSER_V2_1_COMPOSER_H
