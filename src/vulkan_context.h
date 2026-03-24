#ifndef _vulkan_context_h_
#define _vulkan_context_h_

#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Vulkan context structure holding all core Vulkan objects
 * needed for VGLite operations.
 */
typedef struct {
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue queue;
    uint32_t queue_family_index;
    VkCommandPool command_pool;
    VkCommandBuffer command_buffer;
    VkFence fence;
    VkDebugReportCallbackEXT debug_callback;
} vulkan_context_t;

/**
 * Get the global Vulkan context.
 * Returns NULL if vg_lite_init() has not been called.
 */
vulkan_context_t* get_vulkan_context(void);

#ifdef __cplusplus
}
#endif

#endif /* _vulkan_context_h_ */
