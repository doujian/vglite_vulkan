/**
 * VGLite Headless Linux Test
 * 
 * Offscreen rendering without display/window - pure Vulkan headless mode.
 * Creates clear.png with blue background and red rectangle.
 */

#include "vg_lite.h"
#include <stdio.h>
#include <stdlib.h>

int main_linux(void)
{
    printf("VGLite Headless Linux Test\n");
    printf("==========================\n");
    
    /* Initialize VGLite (headless mode - no display) */
    vg_lite_error_t err = vg_lite_init(0, 0);
    if (err != VG_LITE_SUCCESS) {
        fprintf(stderr, "vg_lite_init failed with error: %d\n", err);
        return 1;
    }
    printf("vg_lite_init() - OK\n");
    
    /* Allocate 256x256 RGB565 buffer */
    vg_lite_buffer_t buffer = {0};
    buffer.width = 256;
    buffer.height = 256;
    buffer.format = VG_LITE_RGB565;
    
    err = vg_lite_allocate(&buffer);
    if (err != VG_LITE_SUCCESS) {
        fprintf(stderr, "vg_lite_allocate failed with error: %d\n", err);
        vg_lite_close();
        return 1;
    }
    printf("vg_lite_allocate(256x256 RGB565) - OK\n");
    
    /* Clear full buffer to blue (ABGR: 0xFFFF0000) */
    err = vg_lite_clear(&buffer, NULL, 0xFFFF0000);
    if (err != VG_LITE_SUCCESS) {
        fprintf(stderr, "vg_lite_clear (full) failed with error: %d\n", err);
        vg_lite_free(&buffer);
        vg_lite_close();
        return 1;
    }
    printf("vg_lite_clear (blue background) - OK\n");
    
    /* Clear 64x64 rect at (64,64) to red (ABGR: 0xFF0000FF) */
    vg_lite_rectangle_t rect = {64, 64, 64, 64};
    err = vg_lite_clear(&buffer, &rect, 0xFF0000FF);
    if (err != VG_LITE_SUCCESS) {
        fprintf(stderr, "vg_lite_clear (rect) failed with error: %d\n", err);
        vg_lite_free(&buffer);
        vg_lite_close();
        return 1;
    }
    printf("vg_lite_clear (red rect 64x64 at 64,64) - OK\n");
    
    /* Wait for GPU to finish */
    err = vg_lite_finish();
    if (err != VG_LITE_SUCCESS) {
        fprintf(stderr, "vg_lite_finish failed with error: %d\n", err);
        vg_lite_free(&buffer);
        vg_lite_close();
        return 1;
    }
    printf("vg_lite_finish() - OK\n");
    
    /* Save PNG */
    err = vg_lite_save_png("clear.png", &buffer);
    if (err != VG_LITE_SUCCESS) {
        fprintf(stderr, "vg_lite_save_png failed with error: %d\n", err);
        vg_lite_free(&buffer);
        vg_lite_close();
        return 1;
    }
    printf("vg_lite_save_png(clear.png) - OK\n");
    
    /* Cleanup */
    err = vg_lite_free(&buffer);
    if (err != VG_LITE_SUCCESS) {
        fprintf(stderr, "vg_lite_free failed with error: %d\n", err);
        vg_lite_close();
        return 1;
    }
    printf("vg_lite_free() - OK\n");
    
    err = vg_lite_close();
    if (err != VG_LITE_SUCCESS) {
        fprintf(stderr, "vg_lite_close failed with error: %d\n", err);
        return 1;
    }
    printf("vg_lite_close() - OK\n");
    
    printf("\n=== clear.png generated successfully! ===\n");
    return 0;
}