#ifndef _vg_lite_h_
#define _vg_lite_h_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* VGLite API Error Codes */
typedef enum {
    VG_LITE_SUCCESS = 0,
    VG_LITE_INVALID_ARGUMENT,
    VG_LITE_OUT_OF_MEMORY,
    VG_LITE_GENERIC_ERROR
} vg_lite_error_t;

/* VGLite Buffer Format */
typedef enum {
    VG_LITE_RGB565 = 0
} vg_lite_buffer_format_t;

/* VGLite Buffer Structure */
typedef struct vg_lite_buffer {
    int32_t width;
    int32_t height;
    int32_t stride;
    vg_lite_buffer_format_t format;
    void* memory;
    void* handle;
} vg_lite_buffer_t;

/* VGLite Rectangle Structure */
typedef struct vg_lite_rectangle {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} vg_lite_rectangle_t;

/* VGLite API Functions */
vg_lite_error_t vg_lite_init(int32_t tess_width, int32_t tess_height);
vg_lite_error_t vg_lite_close(void);
vg_lite_error_t vg_lite_allocate(vg_lite_buffer_t* buffer);
vg_lite_error_t vg_lite_free(vg_lite_buffer_t* buffer);
vg_lite_error_t vg_lite_clear(vg_lite_buffer_t* target, vg_lite_rectangle_t* rect, uint32_t color);
vg_lite_error_t vg_lite_finish(void);
vg_lite_error_t vg_lite_save_png(const char* filename, vg_lite_buffer_t* buffer);

#ifdef __cplusplus
}
#endif

#endif /* _vg_lite_h_ */