/**
 * Windows platform main entry with GLFW window and Vulkan swapchain.
 * Demonstrates VGLite clear operations in a windowed animation loop.
 */

#ifdef _WIN32

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include "vg_lite.h"
#include "vulkan_context.h"

/* Window dimensions */
#define WINDOW_WIDTH 512
#define WINDOW_HEIGHT 512

/* Application state */
typedef struct {
    GLFWwindow* window;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    VkImage* swapchain_images;
    VkImageView* swapchain_image_views;
    uint32_t swapchain_image_count;
    uint32_t current_image_index;
    VkFormat surface_format;
    VkCommandPool command_pool;
    VkCommandBuffer command_buffer;
    VkSemaphore image_available_semaphore;
    VkSemaphore render_finished_semaphore;
    VkFence in_flight_fence;
    VkRenderPass render_pass;
    VkFramebuffer* framebuffers;
} app_state_t;

/* Check Vulkan result and print error */
#define VK_CHECK(call, msg) \
    do { \
        VkResult result = (call); \
        if (result != VK_SUCCESS) { \
            fprintf(stderr, "Vulkan error %d at %s:%d: %s\n", result, __FILE__, __LINE__, msg); \
            return 0; \
        } \
    } while (0)

/* Find memory type */
static uint32_t find_memory_type(VkPhysicalDevice physical_device,
                                  uint32_t type_filter,
                                  VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_props);

    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((type_filter & (1 << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return 0xFFFFFFFF;
}

/* Create Vulkan surface from GLFW window */
static int create_surface(app_state_t* app, VkInstance instance)
{
    VkResult result = glfwCreateWindowSurface(instance, app->window, NULL, &app->surface);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create window surface: %d\n", result);
        return 0;
    }
    return 1;
}

/* Select surface format */
static int select_surface_format(app_state_t* app, VkPhysicalDevice physical_device)
{
    uint32_t format_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, app->surface, &format_count, NULL);
    
    VkSurfaceFormatKHR* formats = (VkSurfaceFormatKHR*)malloc(format_count * sizeof(VkSurfaceFormatKHR));
    if (!formats) {
        fprintf(stderr, "Failed to allocate surface format array\n");
        return 0;
    }
    
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, app->surface, &format_count, formats);
    
    /* Prefer BGRA8 UNORM */
    app->surface_format = VK_FORMAT_B8G8R8A8_UNORM;
    for (uint32_t i = 0; i < format_count; i++) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM) {
            app->surface_format = formats[i].format;
            break;
        }
    }
    
    free(formats);
    return 1;
}

/* Create swapchain */
static int create_swapchain(app_state_t* app, VkPhysicalDevice physical_device, VkDevice device)
{
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, app->surface, &capabilities);
    
    /* Determine image count (2-3 images) */
    uint32_t image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount) {
        image_count = capabilities.maxImageCount;
    }
    if (image_count < 2) image_count = 2;
    if (image_count > 3) image_count = 3;
    
    /* Determine extent */
    VkExtent2D extent;
    if (capabilities.currentExtent.width == 0xFFFFFFFF) {
        extent.width = WINDOW_WIDTH;
        extent.height = WINDOW_HEIGHT;
        if (extent.width < capabilities.minImageExtent.width)
            extent.width = capabilities.minImageExtent.width;
        if (extent.width > capabilities.maxImageExtent.width)
            extent.width = capabilities.maxImageExtent.width;
        if (extent.height < capabilities.minImageExtent.height)
            extent.height = capabilities.minImageExtent.height;
        if (extent.height > capabilities.maxImageExtent.height)
            extent.height = capabilities.maxImageExtent.height;
    } else {
        extent = capabilities.currentExtent;
    }
    
    /* Determine pre-transform */
    VkSurfaceTransformFlagBitsKHR pre_transform;
    if (capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
        pre_transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    } else {
        pre_transform = capabilities.currentTransform;
    }
    
    /* Get present modes */
    uint32_t present_mode_count;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, app->surface, &present_mode_count, NULL);
    VkPresentModeKHR* present_modes = (VkPresentModeKHR*)malloc(present_mode_count * sizeof(VkPresentModeKHR));
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, app->surface, &present_mode_count, present_modes);
    
    /* Prefer FIFO for vsync */
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    for (uint32_t i = 0; i < present_mode_count; i++) {
        if (present_modes[i] == VK_PRESENT_MODE_FIFO_KHR) {
            present_mode = present_modes[i];
            break;
        }
    }
    free(present_modes);
    
    /* Create swapchain */
    VkSwapchainCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = NULL,
        .flags = 0,
        .surface = app->surface,
        .minImageCount = image_count,
        .imageFormat = app->surface_format,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = NULL,
        .preTransform = pre_transform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = present_mode,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE
    };
    
    VK_CHECK(vkCreateSwapchainKHR(device, &create_info, NULL, &app->swapchain), "vkCreateSwapchainKHR");
    
    /* Get swapchain images */
    vkGetSwapchainImagesKHR(device, app->swapchain, &app->swapchain_image_count, NULL);
    app->swapchain_images = (VkImage*)malloc(app->swapchain_image_count * sizeof(VkImage));
    vkGetSwapchainImagesKHR(device, app->swapchain, &app->swapchain_image_count, app->swapchain_images);
    
    /* Create image views */
    app->swapchain_image_views = (VkImageView*)malloc(app->swapchain_image_count * sizeof(VkImageView));
    for (uint32_t i = 0; i < app->swapchain_image_count; i++) {
        VkImageViewCreateInfo view_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .image = app->swapchain_images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = app->surface_format,
            .components = {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY
            },
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        VK_CHECK(vkCreateImageView(device, &view_info, NULL, &app->swapchain_image_views[i]), "vkCreateImageView");
    }
    
    return 1;
}

/* Create render pass */
static int create_render_pass(app_state_t* app, VkDevice device)
{
    VkAttachmentDescription color_attachment = {
        .flags = 0,
        .format = app->surface_format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    };
    
    VkAttachmentReference color_ref = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };
    
    VkSubpassDescription subpass = {
        .flags = 0,
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .inputAttachmentCount = 0,
        .pInputAttachments = NULL,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_ref,
        .pResolveAttachments = NULL,
        .pDepthStencilAttachment = NULL,
        .preserveAttachmentCount = 0,
        .pPreserveAttachments = NULL
    };
    
    VkRenderPassCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .attachmentCount = 1,
        .pAttachments = &color_attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 0,
        .pDependencies = NULL
    };
    
    VK_CHECK(vkCreateRenderPass(device, &create_info, NULL, &app->render_pass), "vkCreateRenderPass");
    return 1;
}

/* Create framebuffers */
static int create_framebuffers(app_state_t* app, VkDevice device)
{
    app->framebuffers = (VkFramebuffer*)malloc(app->swapchain_image_count * sizeof(VkFramebuffer));
    
    for (uint32_t i = 0; i < app->swapchain_image_count; i++) {
        VkFramebufferCreateInfo fb_info = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .renderPass = app->render_pass,
            .attachmentCount = 1,
            .pAttachments = &app->swapchain_image_views[i],
            .width = WINDOW_WIDTH,
            .height = WINDOW_HEIGHT,
            .layers = 1
        };
        VK_CHECK(vkCreateFramebuffer(device, &fb_info, NULL, &app->framebuffers[i]), "vkCreateFramebuffer");
    }
    
    return 1;
}

/* Create synchronization objects */
static int create_sync_objects(app_state_t* app, VkDevice device)
{
    VkSemaphoreCreateInfo semaphore_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0
    };
    
    VK_CHECK(vkCreateSemaphore(device, &semaphore_info, NULL, &app->image_available_semaphore), "vkCreateSemaphore image_available");
    VK_CHECK(vkCreateSemaphore(device, &semaphore_info, NULL, &app->render_finished_semaphore), "vkCreateSemaphore render_finished");
    
    VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = NULL,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };
    
    VK_CHECK(vkCreateFence(device, &fence_info, NULL, &app->in_flight_fence), "vkCreateFence");
    
    return 1;
}

/* Record command buffer for clear operations */
static void record_command_buffer(app_state_t* app, VkCommandBuffer cmd, VkFramebuffer framebuffer)
{
    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = NULL,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = NULL
    };
    
    vkBeginCommandBuffer(cmd, &begin_info);
    
    /* Transition image to color attachment optimal */
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = NULL,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = app->swapchain_images[app->current_image_index],
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };
    
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         0, 0, NULL, 0, NULL, 1, &barrier);
    
    /* Begin render pass - clear to blue */
    VkClearValue clear_value = {
        .color = { .float32 = { 0.0f, 0.0f, 1.0f, 1.0f } }  /* Blue background */
    };
    
    VkRenderPassBeginInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .pNext = NULL,
        .renderPass = app->render_pass,
        .framebuffer = framebuffer,
        .renderArea = {
            .offset = { 0, 0 },
            .extent = { WINDOW_WIDTH, WINDOW_HEIGHT }
        },
        .clearValueCount = 1,
        .pClearValues = &clear_value
    };
    
    vkCmdBeginRenderPass(cmd, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
    
    /* Clear a 64x64 rectangle at (64,64) to red */
    VkClearAttachment clear_attachment = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .colorAttachment = 0,
        .clearValue = {
            .color = { .float32 = { 1.0f, 0.0f, 0.0f, 1.0f } }  /* Red */
        }
    };
    
    VkClearRect clear_rect = {
        .rect = {
            .offset = { 64, 64 },
            .extent = { 64, 64 }
        },
        .baseArrayLayer = 0,
        .layerCount = 1
    };
    
    vkCmdClearAttachments(cmd, 1, &clear_attachment, 1, &clear_rect);
    
    vkCmdEndRenderPass(cmd);
    
    vkEndCommandBuffer(cmd);
}

/* Render one frame */
static int render_frame(app_state_t* app, vulkan_context_t* ctx)
{
    VkDevice device = ctx->device;
    
    /* Wait for previous frame */
    vkWaitForFences(device, 1, &app->in_flight_fence, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &app->in_flight_fence);
    
    /* Acquire next image */
    VkResult result = vkAcquireNextImageKHR(device, app->swapchain, UINT64_MAX,
                                            app->image_available_semaphore,
                                            VK_NULL_HANDLE,
                                            &app->current_image_index);
    
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        fprintf(stderr, "Swapchain out of date (not implemented for fixed window)\n");
        return 0;
    }
    
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        fprintf(stderr, "Failed to acquire swapchain image: %d\n", result);
        return 0;
    }
    
    /* Reset and record command buffer */
    vkResetCommandBuffer(app->command_buffer, 0);
    record_command_buffer(app, app->command_buffer, app->framebuffers[app->current_image_index]);
    
    /* Submit command buffer */
    VkPipelineStageFlags wait_stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    
    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = NULL,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &app->image_available_semaphore,
        .pWaitDstStageMask = &wait_stages,
        .commandBufferCount = 1,
        .pCommandBuffers = &app->command_buffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &app->render_finished_semaphore
    };
    
    VK_CHECK(vkQueueSubmit(ctx->queue, 1, &submit_info, app->in_flight_fence), "vkQueueSubmit");
    
    /* Present */
    VkPresentInfoKHR present_info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = NULL,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &app->render_finished_semaphore,
        .swapchainCount = 1,
        .pSwapchains = &app->swapchain,
        .pImageIndices = &app->current_image_index,
        .pResults = NULL
    };
    
    result = vkQueuePresentKHR(ctx->queue, &present_info);
    
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        fprintf(stderr, "Swapchain suboptimal (ignoring for fixed window)\n");
    }
    
    return 1;
}

/* Cleanup resources */
static void cleanup(app_state_t* app, VkDevice device, VkInstance instance)
{
    vkDeviceWaitIdle(device);
    
    if (app->framebuffers) {
        for (uint32_t i = 0; i < app->swapchain_image_count; i++) {
            if (app->framebuffers[i] != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(device, app->framebuffers[i], NULL);
            }
        }
        free(app->framebuffers);
    }
    
    if (app->render_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, app->render_pass, NULL);
    }
    
    if (app->in_flight_fence != VK_NULL_HANDLE) {
        vkDestroyFence(device, app->in_flight_fence, NULL);
    }
    
    if (app->render_finished_semaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(device, app->render_finished_semaphore, NULL);
    }
    
    if (app->image_available_semaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(device, app->image_available_semaphore, NULL);
    }
    
    if (app->command_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, app->command_pool, NULL);
    }
    
    if (app->swapchain_image_views) {
        for (uint32_t i = 0; i < app->swapchain_image_count; i++) {
            if (app->swapchain_image_views[i] != VK_NULL_HANDLE) {
                vkDestroyImageView(device, app->swapchain_image_views[i], NULL);
            }
        }
        free(app->swapchain_image_views);
    }
    
    if (app->swapchain_images) {
        free(app->swapchain_images);
    }
    
    if (app->swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, app->swapchain, NULL);
    }
    
    if (app->surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance, app->surface, NULL);
    }
}

/* Key callback for ESC handling */
static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    (void)scancode;
    (void)mods;
    
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
}

/* Windows main entry point */
int main_windows(void)
{
    app_state_t app = {0};
    
    /* Initialize GLFW */
    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        return 1;
    }
    
    /* Tell GLFW not to create OpenGL context */
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    
    /* Create window */
    app.window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "VGLite Clear", NULL, NULL);
    if (!app.window) {
        fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return 1;
    }
    
    /* Set key callback for ESC */
    glfwSetKeyCallback(app.window, key_callback);
    
    /* Initialize VGLite */
    vg_lite_error_t err = vg_lite_init(0, 0);
    if (err != VG_LITE_SUCCESS) {
        fprintf(stderr, "vg_lite_init failed: %d\n", err);
        glfwDestroyWindow(app.window);
        glfwTerminate();
        return 1;
    }
    
    /* Get Vulkan context from VGLite */
    vulkan_context_t* ctx = get_vulkan_context();
    if (!ctx) {
        fprintf(stderr, "Failed to get Vulkan context\n");
        vg_lite_close();
        glfwDestroyWindow(app.window);
        glfwTerminate();
        return 1;
    }
    
    /* Create surface */
    if (!create_surface(&app, ctx->instance)) {
        vg_lite_close();
        glfwDestroyWindow(app.window);
        glfwTerminate();
        return 1;
    }
    
    /* Check surface support */
    VkBool32 surface_supported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(ctx->physical_device, ctx->queue_family_index, app.surface, &surface_supported);
    if (!surface_supported) {
        fprintf(stderr, "Surface not supported by physical device\n");
        cleanup(&app, ctx->device, ctx->instance);
        vg_lite_close();
        glfwDestroyWindow(app.window);
        glfwTerminate();
        return 1;
    }
    
    /* Select surface format */
    if (!select_surface_format(&app, ctx->physical_device)) {
        cleanup(&app, ctx->device, ctx->instance);
        vg_lite_close();
        glfwDestroyWindow(app.window);
        glfwTerminate();
        return 1;
    }
    
    /* Create swapchain */
    if (!create_swapchain(&app, ctx->physical_device, ctx->device)) {
        cleanup(&app, ctx->device, ctx->instance);
        vg_lite_close();
        glfwDestroyWindow(app.window);
        glfwTerminate();
        return 1;
    }
    
    /* Create render pass */
    if (!create_render_pass(&app, ctx->device)) {
        cleanup(&app, ctx->device, ctx->instance);
        vg_lite_close();
        glfwDestroyWindow(app.window);
        glfwTerminate();
        return 1;
    }
    
    /* Create framebuffers */
    if (!create_framebuffers(&app, ctx->device)) {
        cleanup(&app, ctx->device, ctx->instance);
        vg_lite_close();
        glfwDestroyWindow(app.window);
        glfwTerminate();
        return 1;
    }
    
    /* Create command pool */
    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = NULL,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = ctx->queue_family_index
    };
    
    VK_CHECK(vkCreateCommandPool(ctx->device, &pool_info, NULL, &app.command_pool), "vkCreateCommandPool");
    
    /* Create command buffer */
    VkCommandBufferAllocateInfo cmd_alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = NULL,
        .commandPool = app.command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };
    
    VK_CHECK(vkAllocateCommandBuffers(ctx->device, &cmd_alloc_info, &app.command_buffer), "vkAllocateCommandBuffers");
    
    /* Create synchronization objects */
    if (!create_sync_objects(&app, ctx->device)) {
        cleanup(&app, ctx->device, ctx->instance);
        vg_lite_close();
        glfwDestroyWindow(app.window);
        glfwTerminate();
        return 1;
    }
    
    printf("Window created. Press ESC to close.\n");
    
    /* Animation loop */
    while (!glfwWindowShouldClose(app.window)) {
        render_frame(&app, ctx);
        glfwPollEvents();
    }
    
    /* Cleanup */
    cleanup(&app, ctx->device, ctx->instance);
    vg_lite_close();
    glfwDestroyWindow(app.window);
    glfwTerminate();
    
    printf("Window closed.\n");
    return 0;
}

#endif /* _WIN32 */
