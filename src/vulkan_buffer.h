#ifndef _vulkan_buffer_h_
#define _vulkan_buffer_h_

#include <vulkan/vulkan.h>
#include "vg_lite.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Internal structure to hold Vulkan image resources.
 * Stored in vg_lite_buffer_t::handle.
 */
typedef struct vulkan_buffer_handle {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkFormat vk_format;
} vulkan_buffer_handle_t;

/**
 * Allocate a Vulkan-backed buffer.
 * Creates VkImage, allocates device memory, and creates image view.
 * 
 * @param buffer Buffer structure with width, height, format set
 * @return VG_LITE_SUCCESS on success, error code otherwise
 */
vg_lite_error_t vg_lite_allocate(vg_lite_buffer_t* buffer);

/**
 * Free a Vulkan-backed buffer.
 * Destroys image view, image, and frees device memory.
 * 
 * @param buffer Buffer to free
 * @return VG_LITE_SUCCESS on success, error code otherwise
 */
vg_lite_error_t vg_lite_free(vg_lite_buffer_t* buffer);

#ifdef __cplusplus
}
#endif

#endif /* _vulkan_buffer_h_ */
