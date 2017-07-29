#ifndef _GRALLOC_RPI_H_
#define _GRALLOC_RPI_H_

#include <hardware/gralloc1.h>

#include <gralloc_drm.h>
#include <gralloc_drm_priv.h>

int drm_init(const struct drm_gralloc1_module_t *cmod);
void drm_deinit(const struct drm_gralloc1_module_t *mod);
int drm_register(const struct drm_gralloc1_module_t *mod, buffer_handle_t handle);
int drm_unregister(buffer_handle_t handle);
int drm_lock(buffer_handle_t handle, int usage, int x, int y, int w, int h, void **ptr);
int drm_unlock(buffer_handle_t handle);
int drm_free(buffer_handle_t handle);
int drm_alloc(const struct drm_gralloc1_module_t *mod, int w, int h, int format, int usage, buffer_handle_t *handle, int *stride);

#endif /* _GRALLOC_RPI_H_ */
