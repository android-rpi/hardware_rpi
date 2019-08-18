#define LOG_TAG "allocator@2.0-service"
#include <android-base/logging.h>
#include <hidl/HidlTransportSupport.h>

#include "Allocator.h"

using android::hardware::configureRpcThreadpool;
using android::hardware::joinRpcThreadpool;
using android::status_t;
using android::sp;
using android::UNKNOWN_ERROR;

using android::hardware::graphics::allocator::V2_0::IAllocator;
using android::hardware::graphics::allocator::V2_0::implementation::Allocator;

int main() {
    configureRpcThreadpool(4, true);

    sp<IAllocator> service = new Allocator();

    status_t status = service->registerAsService();

    if (android::OK != status) {
        LOG(FATAL) << "Unable to register allocator service: " << status;
    }

    joinRpcThreadpool();
    return UNKNOWN_ERROR;
}
