#include "vulkan_context.h"
#include "vg_lite.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef NDEBUG
#define ENABLE_VALIDATION_LAYERS 1
#else
#define ENABLE_VALIDATION_LAYERS 0
#endif

static vulkan_context_t g_context = {0};
static int g_initialized = 0;

#if ENABLE_VALIDATION_LAYERS
static const char* const VALIDATION_LAYERS[] = {
    "VK_LAYER_KHRONOS_validation"
};
static const uint32_t VALIDATION_LAYER_COUNT = 1;
static const char* const DEBUG_EXTENSIONS[] = {
    VK_EXT_DEBUG_REPORT_EXTENSION_NAME
};
static const uint32_t DEBUG_EXTENSION_COUNT = 1;

static VKAPI_ATTR VkBool32 VKAPI_CALL
debug_callback(VkDebugReportFlagsEXT flags,
               VkDebugReportObjectTypeEXT obj_type,
               uint64_t src_obj,
               size_t location,
               int32_t msg_code,
               const char* layer_prefix,
               const char* msg,
               void* user_data)
{
    (void)flags;
    (void)obj_type;
    (void)src_obj;
    (void)location;
    (void)msg_code;
    (void)user_data;
    
    fprintf(stderr, "[Vulkan Validation] %s: %s\n", layer_prefix, msg);
    return VK_FALSE;
}
#endif

vulkan_context_t* get_vulkan_context(void)
{
    return g_initialized ? &g_context : NULL;
}

static VkResult create_instance(void)
{
    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = NULL,
        .pApplicationName = "VGLite Vulkan",
        .applicationVersion = 1,
        .pEngineName = "VGLite",
        .engineVersion = 1,
        .apiVersion = VK_API_VERSION_1_0
    };

    uint32_t extension_count = 0;
    const char* extensions[16];

#if ENABLE_VALIDATION_LAYERS
    extensions[extension_count++] = VK_EXT_DEBUG_REPORT_EXTENSION_NAME;
#endif

#ifdef _WIN32
    extensions[extension_count++] = VK_KHR_SURFACE_EXTENSION_NAME;
    extensions[extension_count++] = "VK_KHR_win32_surface";
#endif

    VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .pApplicationInfo = &app_info,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = NULL,
        .enabledExtensionCount = extension_count,
        .ppEnabledExtensionNames = extensions
    };

#if ENABLE_VALIDATION_LAYERS
    create_info.enabledLayerCount = VALIDATION_LAYER_COUNT;
    create_info.ppEnabledLayerNames = VALIDATION_LAYERS;
#endif

    return vkCreateInstance(&create_info, NULL, &g_context.instance);
}

#if ENABLE_VALIDATION_LAYERS
static VkResult setup_debug_callback(void)
{
    VkDebugReportCallbackCreateInfoEXT create_info = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT,
        .pNext = NULL,
        .flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT,
        .pfnCallback = debug_callback,
        .pUserData = NULL
    };

    PFN_vkCreateDebugReportCallbackEXT func = 
        (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(
            g_context.instance, "vkCreateDebugReportCallbackEXT");
    
    if (func == NULL) {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
    
    return func(g_context.instance, &create_info, NULL, &g_context.debug_callback);
}
#endif

static VkResult select_physical_device(void)
{
    uint32_t device_count = 0;
    VkResult result = vkEnumeratePhysicalDevices(g_context.instance, &device_count, NULL);
    if (result != VK_SUCCESS || device_count == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPhysicalDevice* devices = malloc(device_count * sizeof(VkPhysicalDevice));
    if (!devices) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    result = vkEnumeratePhysicalDevices(g_context.instance, &device_count, devices);
    if (result != VK_SUCCESS) {
        free(devices);
        return result;
    }

    VkPhysicalDevice selected_device = VK_NULL_HANDLE;
    uint32_t selected_queue_family = UINT32_MAX;

    for (uint32_t i = 0; i < device_count; i++) {
        uint32_t queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &queue_family_count, NULL);

        VkQueueFamilyProperties* queue_families = malloc(queue_family_count * sizeof(VkQueueFamilyProperties));
        if (!queue_families) {
            continue;
        }

        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &queue_family_count, queue_families);

        for (uint32_t j = 0; j < queue_family_count; j++) {
            if (queue_families[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                VkPhysicalDeviceProperties props;
                vkGetPhysicalDeviceProperties(devices[i], &props);
                
                if (selected_device == VK_NULL_HANDLE || 
                    props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                    selected_device = devices[i];
                    selected_queue_family = j;
                    
                    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                        free(queue_families);
                        goto device_selected;
                    }
                }
            }
        }
        free(queue_families);
    }

device_selected:
    free(devices);

    if (selected_device == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    g_context.physical_device = selected_device;
    g_context.queue_family_index = selected_queue_family;
    return VK_SUCCESS;
}

static VkResult create_logical_device(void)
{
    float queue_priority = 1.0f;
    
    VkDeviceQueueCreateInfo queue_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .queueFamilyIndex = g_context.queue_family_index,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority
    };

    uint32_t device_extension_count = 0;
    const char* device_extensions[16];

#ifdef _WIN32
    device_extensions[device_extension_count++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
#endif

    VkDeviceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_create_info,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = NULL,
        .enabledExtensionCount = device_extension_count,
        .ppEnabledExtensionNames = device_extensions,
        .pEnabledFeatures = NULL
    };

#if ENABLE_VALIDATION_LAYERS
    create_info.enabledLayerCount = VALIDATION_LAYER_COUNT;
    create_info.ppEnabledLayerNames = VALIDATION_LAYERS;
#endif

    VkResult result = vkCreateDevice(g_context.physical_device, &create_info, NULL, &g_context.device);
    if (result != VK_SUCCESS) {
        return result;
    }

    vkGetDeviceQueue(g_context.device, g_context.queue_family_index, 0, &g_context.queue);
    return VK_SUCCESS;
}

static VkResult create_command_pool(void)
{
    VkCommandPoolCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = NULL,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = g_context.queue_family_index
    };

    return vkCreateCommandPool(g_context.device, &create_info, NULL, &g_context.command_pool);
}

static VkResult create_fence(void)
{
    VkFenceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0
    };

    return vkCreateFence(g_context.device, &create_info, NULL, &g_context.fence);
}

static VkResult create_command_buffer(void)
{
    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = NULL,
        .commandPool = g_context.command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };

    return vkAllocateCommandBuffers(g_context.device, &alloc_info, &g_context.command_buffer);
}

vg_lite_error_t vg_lite_init(int32_t tess_width, int32_t tess_height)
{
    (void)tess_width;
    (void)tess_height;

    if (g_initialized) {
        return VG_LITE_SUCCESS;
    }

    memset(&g_context, 0, sizeof(g_context));

    VkResult result = create_instance();
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create Vulkan instance: %d\n", result);
        return VG_LITE_GENERIC_ERROR;
    }

#if ENABLE_VALIDATION_LAYERS
    result = setup_debug_callback();
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to setup debug callback: %d\n", result);
        vkDestroyInstance(g_context.instance, NULL);
        return VG_LITE_GENERIC_ERROR;
    }
#endif

    result = select_physical_device();
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to select physical device: %d\n", result);
#if ENABLE_VALIDATION_LAYERS
        PFN_vkDestroyDebugReportCallbackEXT func = 
            (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(
                g_context.instance, "vkDestroyDebugReportCallbackEXT");
        if (func) {
            func(g_context.instance, g_context.debug_callback, NULL);
        }
#endif
        vkDestroyInstance(g_context.instance, NULL);
        return VG_LITE_GENERIC_ERROR;
    }

    result = create_logical_device();
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create logical device: %d\n", result);
        vkDestroyInstance(g_context.instance, NULL);
        return VG_LITE_GENERIC_ERROR;
    }

    result = create_command_pool();
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create command pool: %d\n", result);
        vkDestroyDevice(g_context.device, NULL);
        vkDestroyInstance(g_context.instance, NULL);
        return VG_LITE_GENERIC_ERROR;
    }

    result = create_fence();
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create fence: %d\n", result);
        vkDestroyCommandPool(g_context.device, g_context.command_pool, NULL);
        vkDestroyDevice(g_context.device, NULL);
        vkDestroyInstance(g_context.instance, NULL);
        return VG_LITE_GENERIC_ERROR;
    }

    result = create_command_buffer();
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create command buffer: %d\n", result);
        vkDestroyFence(g_context.device, g_context.fence, NULL);
        vkDestroyCommandPool(g_context.device, g_context.command_pool, NULL);
        vkDestroyDevice(g_context.device, NULL);
        vkDestroyInstance(g_context.instance, NULL);
        return VG_LITE_GENERIC_ERROR;
    }

    g_initialized = 1;
    return VG_LITE_SUCCESS;
}

vg_lite_error_t vg_lite_close(void)
{
    if (!g_initialized) {
        return VG_LITE_SUCCESS;
    }

    if (g_context.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(g_context.device);
    }

    if (g_context.fence != VK_NULL_HANDLE) {
        vkDestroyFence(g_context.device, g_context.fence, NULL);
    }

    if (g_context.command_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(g_context.device, g_context.command_pool, NULL);
    }

    if (g_context.device != VK_NULL_HANDLE) {
        vkDestroyDevice(g_context.device, NULL);
    }

#if ENABLE_VALIDATION_LAYERS
    if (g_context.debug_callback != VK_NULL_HANDLE) {
        PFN_vkDestroyDebugReportCallbackEXT func = 
            (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(
                g_context.instance, "vkDestroyDebugReportCallbackEXT");
        if (func) {
            func(g_context.instance, g_context.debug_callback, NULL);
        }
    }
#endif

    if (g_context.instance != VK_NULL_HANDLE) {
        vkDestroyInstance(g_context.instance, NULL);
    }

    memset(&g_context, 0, sizeof(g_context));
    g_initialized = 0;

    return VG_LITE_SUCCESS;
}
