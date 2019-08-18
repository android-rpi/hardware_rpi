#define LOG_TAG "composer@2.0-Composer"
//#define LOG_NDEBUG 0
#include <android-base/logging.h>
#include <utils/Log.h>

#include "Composer.h"

namespace android {
namespace hardware {
namespace graphics {
namespace composer {
namespace V2_1 {
namespace implementation {

Composer::Composer() {
    mHal = std::make_unique<ComposerHal>();
}

Return<void> Composer::getCapabilities(getCapabilities_cb hidl_cb) {
    std::vector<Capability> caps;
    caps.push_back(Capability::PRESENT_FENCE_IS_NOT_RELIABLE);

    hidl_vec<Capability> caps_reply;
    caps_reply.setToExternal(caps.data(), caps.size());
    hidl_cb(caps_reply);
    return Void();
}

Return<void> Composer::dumpDebugInfo(dumpDebugInfo_cb hidl_cb) {
	hidl_cb(mHal->dumpDebugInfo());
    return Void();
}

Return<void> Composer::createClient(createClient_cb hidl_cb) {
    std::unique_lock<std::mutex> lock(mClientMutex);
    if (!waitForClientDestroyedLocked(lock)) {
        hidl_cb(Error::NO_RESOURCES, nullptr);
        return Void();
    }
    sp<IComposerClient> client = createClient();
    if (!client) {
        hidl_cb(Error::NO_RESOURCES, nullptr);
        return Void();
    }
    mClient = client;
    hidl_cb(Error::NONE, client);
    return Void();
}

bool Composer::waitForClientDestroyedLocked(std::unique_lock<std::mutex>& lock) {
    if (mClient != nullptr) {
        using namespace std::chrono_literals;
        ALOGD("waiting for previous client to be destroyed");
        mClientDestroyedCondition.wait_for(
            lock, 1s, [this]() -> bool { return mClient.promote() == nullptr; });
        if (mClient.promote() != nullptr) {
            ALOGD("previous client was not destroyed");
        } else {
            mClient.clear();
        }
    }
    return mClient == nullptr;
}

void Composer::onClientDestroyed() {
    std::lock_guard<std::mutex> lock(mClientMutex);
    mClient.clear();
    mClientDestroyedCondition.notify_all();
}

IComposerClient* Composer::createClient() {
    auto client = new ComposerClient(mHal.get());

    auto clientDestroyed = [this]() { onClientDestroyed(); };
    client->setOnClientDestroyed(clientDestroyed);

    return client;
}

IComposer* HIDL_FETCH_IComposer(const char* /* name */) {
    return new Composer();
}

}  // namespace implementation
}  // namespace V2_1
}  // namespace composer
}  // namespace graphics
}  // namespace hardware
}  // namespace android
