#ifndef _HWC_CONTEXT_H_
#define _HWC_CONTEXT_H_

namespace android {

class hwc_context {
  public :
    hwc_context();
    int hwc_post(buffer_handle_t handle);

    uint32_t  width;
    uint32_t  height;
    int       format;
    float     fps;
    float     xdpi;
    float     ydpi;

  private:
    struct drm_module_t *mModule;
};

} // namespace anroid

#endif // _HWC_CONTEXT_H_
