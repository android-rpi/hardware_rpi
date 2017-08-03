//#define LOG_NDEBUG 0
#define LOG_TAG "HWC2-RPI"
#include <cutils/log.h>
#include <cutils/atomic.h>

#include "Hwc2Device.h"
#include "hwc2_rpi.h"

int gralloc_drm_bo_post(struct gralloc_drm_bo_t *bo);

static int hwc_post_fb0(buffer_handle_t handle)
{
	struct gralloc_drm_bo_t *bo;

	bo = gralloc_drm_bo_from_handle(handle);
	if (!bo)
		return -EINVAL;

	return gralloc_drm_bo_post(bo);
}

static int hwc_event_control(hwc_context* ctx, int disp, int event,
				int enabled)
{
	/**
	 * The API restricts 'enabled' as having boolean values only. Also for
	 * now there can be only one fixed display having identifier zero.
	 */
	if (event != HWC_EVENT_VSYNC || (enabled != 0 && enabled != 1) || disp)
		return -EINVAL;

	if (ctx->vsync_thread != NULL)
		ctx->vsync_thread->set_enabled(enabled);

	return 0;
}

int hwc_context::prepare(size_t /*numDisplays*/,
		       hwc_display_contents_1_t** /*displays*/)
{
	return 0;
}

int hwc_context::set(size_t /*numDisplays*/, hwc_display_contents_1_t** displays)
{
	size_t i = 0;
	for (i=0; i< displays[0]->numHwLayers; i++) {
		if (displays[0]->hwLayers[i].compositionType == HWC_FRAMEBUFFER_TARGET) {
			ALOGV("hwc_context::set() calling hwc_post_fb0()");
			hwc_post_fb0(displays[0]->hwLayers[i].handle);
			displays[0]->hwLayers[i].releaseFenceFd = -1;
		}
	}
	displays[0]->retireFenceFd = -1;

	return 0;
}

// query display attributes for a particular config
int hwc_context::getDisplayAttributes(int disp,
	uint32_t /*config*/, const uint32_t* attributes, int32_t* values)
{
	int attr = 0;
	gralloc_drm_t *drm = gralloc_module->drm;

	// support only 1 display for now
	if (disp > 0)
		return -EINVAL;

	while(attributes[attr] != HWC_DISPLAY_NO_ATTRIBUTE) {
		switch (attributes[attr]) {
			case HWC_DISPLAY_VSYNC_PERIOD:
				values[attr] = (int32_t)(vsync_thread->refresh_period);
				break;
			case HWC_DISPLAY_WIDTH:
				values[attr] = drm->primary.mode.hdisplay;
				break;
			case HWC_DISPLAY_HEIGHT:
				values[attr] = drm->primary.mode.vdisplay;
				break;
			case HWC_DISPLAY_DPI_X:
				values[attr] = drm->primary.xdpi * 1000;
				break;
			case HWC_DISPLAY_DPI_Y:
				values[attr] = drm->primary.ydpi * 1000;
				break;
		}
		attr++;
	}
	return 0;
}

static int hwc_device_close(hwc_context* ctx)
{
	if (ctx)
		delete ctx;

	if (ctx->vsync_thread != NULL)
		ctx->vsync_thread->requestExitAndWait();

	return 0;
}


/*****************************************************************************/


/* This is needed here as bionic itself is missing the prototype */
extern "C" int clock_nanosleep(clockid_t clock_id, int flags,
			const struct timespec *request,	struct timespec *remain);

/**
 * XXX: this code is temporary and comes from SurfaceFlinger.cpp
 * so I changed as little as possible since the code will be dropped
 * anyway, when real functionality will be implemented
 */
vsync_worker::vsync_worker(hwc_context *c)
	: ctx(c), enabled(false), next_fake_vsync(0)
{
	refresh_period = int64_t(SEC_TO_NANOSEC / 60.0);
	ALOGI("getting VSYNC period from thin air: %lld", refresh_period);
}

void vsync_worker::set_enabled(bool _enabled)
{
	android::Mutex::Autolock _l(lock);
	if (enabled != _enabled) {
		enabled = _enabled;
		condition.signal();
	}
}

void vsync_worker::wait_until_enabled()
{
	android::Mutex::Autolock _l(lock);

	while (!enabled) {
		condition.wait(lock);
	}
}

void vsync_worker::onFirstRef()
{
	run("vsync_thread", android::PRIORITY_URGENT_DISPLAY +
					android::PRIORITY_MORE_FAVORABLE);
}

bool vsync_worker::threadLoop()
{
	wait_until_enabled();

	const int64_t now = systemTime(CLOCK_MONOTONIC);
	int64_t next_vsync = next_fake_vsync;
	int64_t sleep = next_vsync - now;
	if (sleep < 0) {
		/* we missed, find where the next vsync should be */
		ALOGV("vsync missed!");
		sleep = (refresh_period - ((now - next_vsync) % refresh_period));
		next_vsync = now + sleep;
	}

	next_fake_vsync = next_vsync + refresh_period;

	struct timespec spec;
	spec.tv_sec  = next_vsync / SEC_TO_NANOSEC;
	spec.tv_nsec = next_vsync % SEC_TO_NANOSEC;

	int err;
	do {
		err = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &spec, NULL);
	} while (err < 0 && errno == EINTR);

	if (err == 0)
		ctx->device->hwc1Vsync(0, next_vsync);
	else
		ALOGE("clock_nanosleep failed with error %d ", err);

	return true;
}


/*****************************************************************************/


int gralloc_drm_init_kms(struct gralloc_drm_t *drm);

static int hwc_init_kms(struct drm_gralloc1_module_t *mod) {
	int err = 0;
	pthread_mutex_lock(&mod->mutex);
	if (!mod->drm) {
		mod->drm = gralloc_drm_create();
		if (!mod->drm)
			err = -EINVAL;
	}
	if (!err)
		err = gralloc_drm_init_kms(mod->drm);
	pthread_mutex_unlock(&mod->mutex);
	return err;
}

static int hwc2_device_open(const struct hw_module_t* module,
        const char* name, struct hw_device_t** device) {
    int status = -EINVAL;
	if (strcmp(name, HWC_HARDWARE_COMPOSER))
		return status;

    hwc_context *ctx = new hwc_context();

    int err = hw_get_module(GRALLOC_HARDWARE_MODULE_ID,
    		(const hw_module_t **)&ctx->gralloc_module);

	if (err != 0) {
		ALOGE("hwc_device_open failed!\n");
		return -errno;
	}

	err = hwc_init_kms(ctx->gralloc_module);
	if (err != 0) {
		ALOGE("failed hwc_init_kms() %d", err);
		return err;
	}

	ctx->vsync_thread = new vsync_worker(ctx);

    android::Hwc2Device *dev = new android::Hwc2Device(ctx);
    dev->common.tag = HARDWARE_DEVICE_TAG;
    dev->common.version = HWC_DEVICE_API_VERSION_2_0;
    dev->common.module = const_cast<hw_module_t*>(module);
    ctx->device = dev;

    *device = reinterpret_cast<hw_device_t *>(dev);
    status = 0;

    return status;
}

static struct hw_module_methods_t hwc2_module_methods = {
    .open = hwc2_device_open
};

hwc_module_t HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .id = HWC_HARDWARE_MODULE_ID,
        .name = "RPi Hardware Composer Module",
        .author = "Peter Yoon",
        .methods = &hwc2_module_methods,
    }
};
