#include <string>
#include <sys/errno.h>
#include <gralloc_drm.h>
#include <gralloc_drm_priv.h>

int drm_init(const struct drm_module_t *cmod) {
	struct drm_module_t *mod = (struct drm_module_t *) cmod;
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

static int drm_mod_perform(const struct gralloc_module_t *mod, int op, ...)
{
	struct drm_module_t *dmod = (struct drm_module_t *) mod;
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

struct drm_module_t HAL_MODULE_INFO_SYM = {
	.base = {
		.common = {
			.tag = HARDWARE_MODULE_TAG,
			.version_major = 1,
			.version_minor = 0,
			.id = GRALLOC_HARDWARE_MODULE_ID,
			.name = "arpi gralloc",
			.author = "Peter Yoon",
		},
		.perform = drm_mod_perform,
	},
	.mutex = PTHREAD_MUTEX_INITIALIZER,
	.drm = NULL
};
