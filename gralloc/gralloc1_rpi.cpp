#undef LOG_TAG
#define LOG_TAG "GRALLOC1-RPI"
//#define LOG_NDEBUG 0
#include <utils/Log.h>

#include "gralloc1_rpi.h"
#include "Gralloc1Device.h"

int gralloc_drm_bo_add_fb(struct gralloc_drm_bo_t *bo);

int drm_init(const struct drm_gralloc1_module_t *cmod) {
	struct drm_gralloc1_module_t *mod = (struct drm_gralloc1_module_t *) cmod;
	int err = 0;
	pthread_mutex_lock(&mod->mutex);
	if (!mod->drm) {
		mod->drm = gralloc_drm_create();
		if (!mod->drm)
			err = -EINVAL;
	}
	pthread_mutex_unlock(&mod->mutex);
	return err;
}

void drm_deinit(const struct drm_gralloc1_module_t *mod) {
	gralloc_drm_destroy(mod->drm);
}

int drm_register(const struct drm_gralloc1_module_t *mod, buffer_handle_t handle) {
	int err;
	err = drm_init(mod);
	if (err)
		return err;
	return gralloc_drm_handle_register(handle, mod->drm);
}

int drm_unregister(buffer_handle_t handle) {
	return gralloc_drm_handle_unregister(handle);
}

int drm_lock(buffer_handle_t handle, int usage, int x, int y, int w, int h, void **ptr) {
	struct gralloc_drm_bo_t *bo;
	int err;
	bo = gralloc_drm_bo_from_handle(handle);
	if (!bo)
		return -EINVAL;
	return gralloc_drm_bo_lock(bo, usage, x, y, w, h, ptr);
}

int drm_unlock(buffer_handle_t handle) {
	struct gralloc_drm_bo_t *bo;
	bo = gralloc_drm_bo_from_handle(handle);
	if (!bo)
		return -EINVAL;
	gralloc_drm_bo_unlock(bo);
	return 0;
}

int drm_free(buffer_handle_t handle) {
	struct gralloc_drm_bo_t *bo;
	bo = gralloc_drm_bo_from_handle(handle);
	if (!bo)
		return -EINVAL;
	gralloc_drm_bo_decref(bo);
	return 0;
}

int drm_alloc(const struct drm_gralloc1_module_t *mod, int w, int h, int format, int usage,
		buffer_handle_t *handle, int *stride) {
	struct gralloc_drm_bo_t *bo;
	int size, bpp, err;
	bpp = gralloc_drm_get_bpp(format);
	if (!bpp) return -EINVAL;
	bo = gralloc_drm_bo_create(mod->drm, w, h, format, usage);
	if (!bo) return -ENOMEM;
	if (bo->handle->usage & GRALLOC_USAGE_HW_FB) {
		err = gralloc_drm_bo_add_fb(bo);
		if (err) {
			ALOGE("failed to add fb");
			gralloc_drm_bo_decref(bo);
			return err;
		}
	}
	*handle = gralloc_drm_bo_get_handle(bo, stride);
	/* in pixels */
	*stride /= bpp;
	return 0;
}


static int drm_mod_perform(const struct drm_gralloc1_module_t *mod, int op, ...)
{
	struct drm_gralloc1_module_t *dmod = (struct drm_gralloc1_module_t *) mod;
	va_list args;
	int err;

	err = drm_init(dmod);
	if (err)
		return err;

	va_start(args, op);
	switch (op) {
	case GRALLOC_MODULE_PERFORM_GET_DRM_FD:
		{
			int *fd = va_arg(args, int *);
			*fd = gralloc_drm_get_fd(dmod->drm);
			err = 0;
		}
		break;
	default:
		err = -EINVAL;
		break;
	}
	va_end(args);

	return err;
}


static int gralloc1_device_open(const struct hw_module_t *module,
		const char *name, struct hw_device_t **device)
{
	  int status = -EINVAL;
	  if (!strcmp(name, GRALLOC_HARDWARE_MODULE_ID)) {
	      const struct drm_gralloc1_module_t *m = reinterpret_cast<const struct drm_gralloc1_module_t *>(module);
	      android::Gralloc1Device *dev = new android::Gralloc1Device(m);
	      *device = reinterpret_cast<hw_device_t *>(dev);
	      status = 0;
	  }
	  return status;
}

static struct hw_module_methods_t private_module_methods = {
	.open = gralloc1_device_open
};

struct drm_gralloc1_module_t HAL_MODULE_INFO_SYM = {
	.common = {
		.tag = HARDWARE_MODULE_TAG,
		.version_major = 0x100,
		.version_minor = 0,
		.id = GRALLOC_HARDWARE_MODULE_ID,
		.name = "RPi Graphics Memory Allocator",
		.author = "Peter Yoon",
		.methods = &private_module_methods
	},
	.perform = drm_mod_perform,
	.mutex = PTHREAD_MUTEX_INITIALIZER,
	.drm = NULL
};
