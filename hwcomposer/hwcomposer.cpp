/*
 * Copyright (C) 2012 Intel Corporation. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *		http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>
#include <hardware/gralloc.h>

#include <gralloc_drm.h>
#include <gralloc_drm_priv.h>
#include <gralloc_drm_handle.h>

#include <fcntl.h>
#include <errno.h>

#include <cutils/log.h>
#include <cutils/atomic.h>

#include <hardware/hwcomposer.h>

#include <EGL/egl.h>

#define LOG_TAG "hwcomposer"

#include <Condition.h>
#include <Mutex.h>
#include <Thread.h>
#include <StrongPointer.h>

#define SEC_TO_NANOSEC (1000 * 1000 * 1000)

static int hwc_device_open(const struct hw_module_t* module, const char* name,
		struct hw_device_t** device);

static struct hw_module_methods_t hwc_module_methods = {
	open: hwc_device_open
};

hwc_module_t HAL_MODULE_INFO_SYM = {
	common: {
		tag: HARDWARE_MODULE_TAG,
		version_major: 1,
		version_minor: 0,
		id: HWC_HARDWARE_MODULE_ID,
		name: "Intel hwcomposer module",
		author: "Intel",
		methods: &hwc_module_methods,
		dso: NULL,
		{0}
	}
};


/**
* Fake VSync class.
* To provide refresh timestamps to the surface flinger, using the
* same fake mechanism as SF uses on its own, and this is because one
* cannot start using hwc until it provides certain mandatory things - the
* refresh time stamps being one of them.
*/
class vsync_worker : public android::Thread {
public:
	vsync_worker(struct hwc_context_t& hwc);
	void set_enabled(bool enabled);
private:
	virtual void onFirstRef();
	virtual bool threadLoop();
	void wait_until_enabled();
private:
	struct hwc_context_t& dev;
	mutable android::Mutex lock;
	android::Condition condition;
	bool enabled;
	mutable int64_t next_fake_vsync;
	int64_t refresh_period;
};

struct hwc_context_t {
	hwc_composer_device_1 device;
	struct drm_gralloc1_module_t *gralloc_module;
	hwc_procs_t *procs;
	android::sp<vsync_worker> vsync_thread;
};

static void hwc_register_procs(hwc_composer_device_1 *dev,
			hwc_procs_t const* procs)
{
	struct hwc_context_t* ctx = (struct hwc_context_t*)dev;
	ctx->procs = (hwc_procs_t *) procs;
}

static int hwc_event_control(hwc_composer_device_1 *dev, int disp, int event,
				int enabled)
{
	hwc_context_t* ctx = (hwc_context_t*)dev;

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

static int hwc_prepare(hwc_composer_device_1 *dev, size_t numDisplays,
	hwc_display_contents_1_t** displays)
{
	struct hwc_context_t* ctx = (struct hwc_context_t *) &dev->common;

	// SurfaceFlinger wants to handle the complete composition
	if (displays[0]->numHwLayers == 0)
		return 0;

	int topmost = displays[0]->numHwLayers;
	if (displays[0]->numHwLayers > 0)
		topmost--;

	if (displays[0]->hwLayers->flags & HWC_GEOMETRY_CHANGED) {
		for (int i=topmost; i>=0; i--) {
			displays[0]->hwLayers[i].compositionType = HWC_FRAMEBUFFER;
		}
	}
	return 0;
}


static int hwc_set(hwc_composer_device_1 *dev,
		size_t numDisplays, hwc_display_contents_1_t** displays)
{
	struct hwc_context_t* ctx = (struct hwc_context_t*)dev;
	EGLBoolean success;

	// display is turning off
	if (!displays[0]->dpy)
		return 0;

	success = eglSwapBuffers((EGLDisplay)displays[0]->dpy,
		(EGLSurface)displays[0]->sur);

	if (!success)
		return HWC_EGL_ERROR;

	return 0;
}

// toggle display on or off
static int hwc_blank(struct hwc_composer_device_1* dev, int disp, int blank)
{
	// dummy implementation for now
	return 0;
}

// query number of different configurations available on display
static int hwc_get_display_cfgs(struct hwc_composer_device_1* dev, int disp,
	uint32_t* configs, size_t* numConfigs)
{
	// support just one config per display for now
	*configs = 1;
	*numConfigs = 1;

	return 0;
}

// query display attributes for a particular config
static int hwc_get_display_attrs(struct hwc_composer_device_1* dev, int disp,
	uint32_t config, const uint32_t* attributes, int32_t* values)
{
	int attr = 0;
	struct hwc_context_t* ctx = (struct hwc_context_t *) &dev->common;

	gralloc_drm_t *drm = ctx->gralloc_module->drm;

	// support only 1 display for now
	if (disp > 0)
		return -EINVAL;

	while(attributes[attr] != HWC_DISPLAY_NO_ATTRIBUTE) {
		switch (attr) {
			case HWC_DISPLAY_VSYNC_PERIOD:
				values[attr] = drm->primary.mode.vrefresh;
				break;
			case HWC_DISPLAY_WIDTH:
				values[attr] = drm->primary.mode.hdisplay;
				break;
			case HWC_DISPLAY_HEIGHT:
				values[attr] = drm->primary.mode.vdisplay;
				break;
			case HWC_DISPLAY_DPI_X:
				values[attr] = drm->primary.xdpi;
				break;
			case HWC_DISPLAY_DPI_Y:
				values[attr] = drm->primary.ydpi;
				break;
		}
		attr++;
	}
	return 0;
}

static int hwc_device_close(struct hw_device_t *dev)
{
	struct hwc_context_t* ctx = (struct hwc_context_t*)dev;

	if (ctx)
		free(ctx);

	if (ctx->vsync_thread != NULL)
		ctx->vsync_thread->requestExitAndWait();

	return 0;
}

/*****************************************************************************/

static int hwc_device_open(const struct hw_module_t* module, const char* name,
		struct hw_device_t** device)
{
	int status = -EINVAL;
	if (strcmp(name, HWC_HARDWARE_COMPOSER))
		return status;

	struct hwc_context_t *dev;
	dev = (hwc_context_t*)calloc(1, sizeof(*dev));

	/* initialize the procs */
	dev->device.common.tag = HARDWARE_DEVICE_TAG;
	dev->device.common.version = HWC_DEVICE_API_VERSION_1_0;
	dev->device.common.module = const_cast<hw_module_t*>(module);
	dev->device.common.close = hwc_device_close;

	dev->device.prepare = hwc_prepare;
	dev->device.set = hwc_set;
	dev->device.blank = hwc_blank;
	dev->device.getDisplayAttributes = hwc_get_display_attrs;
	dev->device.getDisplayConfigs = hwc_get_display_cfgs;
	dev->device.registerProcs = hwc_register_procs;
	dev->device.eventControl = hwc_event_control;

	int err = hw_get_module(GRALLOC_HARDWARE_MODULE_ID,
		(const hw_module_t **)&dev->gralloc_module);
	if (err != 0) {
		ALOGE("hwc_device_open failed!\n");
		return -errno;
	}

	*device = &dev->device.common;

	dev->vsync_thread = new vsync_worker(*dev);

	ALOGD("Intel hwcomposer module");

	return 0;
}

/* This is needed here as bionic itself is missing the prototype */
extern "C" int clock_nanosleep(clockid_t clock_id, int flags,
			const struct timespec *request,	struct timespec *remain);

/**
 * XXX: this code is temporary and comes from SurfaceFlinger.cpp
 * so I changed as little as possible since the code will be dropped
 * anyway, when real functionality will be implemented
 */
vsync_worker::vsync_worker(struct hwc_context_t& mydev)
	: dev(mydev), enabled(false), next_fake_vsync(0)
{
	int64_t refresh = 0;
	framebuffer_device_t*	fbdev;

	int err = framebuffer_open((const hw_module_t *)dev.gralloc_module, &fbdev);
	if (err)
		ALOGE("framebuffer_open failed (%s)", strerror(-err));
	else
		refresh = int64_t(SEC_TO_NANOSEC / fbdev->fps);

	if (refresh == 0) {
		refresh = int64_t(SEC_TO_NANOSEC / 60.0);
		ALOGW("getting VSYNC period from thin air: %lld", refresh);
	} else
		ALOGW("getting VSYNC period from fb HAL: %lld", refresh);

	refresh_period = refresh;
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
		dev.procs->vsync(dev.procs, 0, next_vsync);
	else
		ALOGE("clock_nanosleep failed with error %d ", err);

	return true;
}

