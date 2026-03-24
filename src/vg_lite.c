#include "vg_lite.h"
#include "vulkan_context.h"
#include "vulkan_buffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

/* Track if there's a pending command buffer to submit */
static int g_has_pending_command = 0;

static VkRenderPass g_render_pass = VK_NULL_HANDLE;
static VkFramebuffer g_framebuffer = VK_NULL_HANDLE;
static vg_lite_buffer_t* g_current_target = NULL;
static int g_in_render_pass = 0;
static int g_command_buffer_begun = 0;
static VkCommandBuffer g_command_buffer = VK_NULL_HANDLE;

/**
 * Convert ABGR color (VGLite format) to Vulkan clear color.
 * VGLite: 0xAARRGGBB (A=bits 31-24, B=bits 23-16, G=bits 15-8, R=bits 7-0)
 * Vulkan: float RGBA in [0, 1]
 */
static void abgr_to_clear_color(uint32_t abgr, VkClearColorValue* out_color)
{
    out_color->float32[0] = ((abgr >> 0) & 0xFF) / 255.0f;  /* R */
    out_color->float32[1] = ((abgr >> 8) & 0xFF) / 255.0f;  /* G */
    out_color->float32[2] = ((abgr >> 16) & 0xFF) / 255.0f; /* B */
    out_color->float32[3] = ((abgr >> 24) & 0xFF) / 255.0f;  /* A */
}

static void transition_image_layout(VkCommandBuffer cmd,
                                    VkImage image,
                                    VkImageLayout old_layout,
                                    VkImageLayout new_layout)
{
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = NULL,
        .srcAccessMask = 0,
        .dstAccessMask = 0,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED &&
        new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               new_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dst_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED &&
               new_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }

    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, NULL, 0, NULL, 1, &barrier);
}

static vg_lite_error_t create_render_pass(vulkan_context_t* ctx, VkFormat format)
{
    if (g_render_pass != VK_NULL_HANDLE) {
        return VG_LITE_SUCCESS;
    }

    VkAttachmentDescription attachment = {
        .flags = 0,
        .format = format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
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
        .pAttachments = &attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 0,
        .pDependencies = NULL
    };

    VkResult result = vkCreateRenderPass(ctx->device, &create_info, NULL, &g_render_pass);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create render pass: %d\n", result);
        return VG_LITE_GENERIC_ERROR;
    }

    return VG_LITE_SUCCESS;
}

static vg_lite_error_t create_framebuffer(vulkan_context_t* ctx,
                                          vg_lite_buffer_t* target,
                                          vulkan_buffer_handle_t* handle)
{
    if (g_framebuffer != VK_NULL_HANDLE && g_current_target != target) {
        vkDestroyFramebuffer(ctx->device, g_framebuffer, NULL);
        g_framebuffer = VK_NULL_HANDLE;
    }

    if (g_framebuffer != VK_NULL_HANDLE) {
        return VG_LITE_SUCCESS;
    }

    vg_lite_error_t err = create_render_pass(ctx, handle->vk_format);
    if (err != VG_LITE_SUCCESS) {
        return err;
    }

    VkFramebufferCreateInfo fb_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .renderPass = g_render_pass,
        .attachmentCount = 1,
        .pAttachments = &handle->view,
        .width = (uint32_t)target->width,
        .height = (uint32_t)target->height,
        .layers = 1
    };

    VkResult result = vkCreateFramebuffer(ctx->device, &fb_info, NULL, &g_framebuffer);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create framebuffer: %d\n", result);
        return VG_LITE_GENERIC_ERROR;
    }

    g_current_target = target;
    return VG_LITE_SUCCESS;
}

static vg_lite_error_t begin_render_pass(vulkan_context_t* ctx,
                                         vg_lite_buffer_t* target,
                                         vulkan_buffer_handle_t* handle)
{
    if (g_in_render_pass) {
        return VG_LITE_SUCCESS;
    }

    vg_lite_error_t err = create_framebuffer(ctx, target, handle);
    if (err != VG_LITE_SUCCESS) {
        return err;
    }

    transition_image_layout(g_command_buffer, handle->image,
                           VK_IMAGE_LAYOUT_UNDEFINED,
                           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    VkRenderPassBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .pNext = NULL,
        .renderPass = g_render_pass,
        .framebuffer = g_framebuffer,
        .renderArea = {
            .offset = {0, 0},
            .extent = {(uint32_t)target->width, (uint32_t)target->height}
        },
        .clearValueCount = 0,
        .pClearValues = NULL
    };

    vkCmdBeginRenderPass(g_command_buffer, &begin_info, VK_SUBPASS_CONTENTS_INLINE);
    g_in_render_pass = 1;

    return VG_LITE_SUCCESS;
}

static void end_render_pass(void)
{
    if (g_in_render_pass) {
        vkCmdEndRenderPass(g_command_buffer);
        g_in_render_pass = 0;
    }
}

vg_lite_error_t vg_lite_clear(vg_lite_buffer_t* target,
                              vg_lite_rectangle_t* rect,
                              uint32_t color)
{
    vulkan_context_t* ctx = get_vulkan_context();
    if (!ctx) {
        return VG_LITE_GENERIC_ERROR;
    }

    if (!target || !target->handle) {
        return VG_LITE_INVALID_ARGUMENT;
    }

    vulkan_buffer_handle_t* handle = (vulkan_buffer_handle_t*)target->handle;

    if (!g_command_buffer_begun) {
        if (g_command_buffer == VK_NULL_HANDLE) {
            VkCommandBufferAllocateInfo alloc_info = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .pNext = NULL,
                .commandPool = ctx->command_pool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = 1
            };

            VkResult result = vkAllocateCommandBuffers(ctx->device, &alloc_info, &g_command_buffer);
            if (result != VK_SUCCESS) {
                fprintf(stderr, "Failed to allocate command buffer: %d\n", result);
                return VG_LITE_OUT_OF_MEMORY;
            }
        }

        VkCommandBufferBeginInfo begin_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = NULL,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = NULL
        };

        VkResult result = vkBeginCommandBuffer(g_command_buffer, &begin_info);
        if (result != VK_SUCCESS) {
            fprintf(stderr, "Failed to begin command buffer: %d\n", result);
            return VG_LITE_GENERIC_ERROR;
        }
        g_command_buffer_begun = 1;
    }

    VkClearColorValue clear_color;
    abgr_to_clear_color(color, &clear_color);

    if (rect == NULL) {
        VkImageSubresourceRange range = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        };

        end_render_pass();

        transition_image_layout(g_command_buffer, handle->image,
                               VK_IMAGE_LAYOUT_UNDEFINED,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        vkCmdClearColorImage(g_command_buffer, handle->image,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            &clear_color, 1, &range);

        transition_image_layout(g_command_buffer, handle->image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    } else {
        int32_t x = rect->x;
        int32_t y = rect->y;
        int32_t w = rect->width;
        int32_t h = rect->height;

        if (x < 0) { w += x; x = 0; }
        if (y < 0) { h += y; y = 0; }
        if (x + w > target->width) { w = target->width - x; }
        if (y + h > target->height) { h = target->height - y; }

        if (w <= 0 || h <= 0) {
            return VG_LITE_SUCCESS;
        }

        vg_lite_error_t err = begin_render_pass(ctx, target, handle);
        if (err != VG_LITE_SUCCESS) {
            return err;
        }

        VkClearAttachment clear_attachment = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .colorAttachment = 0,
            .clearValue = { .color = clear_color }
        };

        VkClearRect clear_rect = {
            .rect = {
                .offset = {x, y},
                .extent = {(uint32_t)w, (uint32_t)h}
            },
            .baseArrayLayer = 0,
            .layerCount = 1
        };

        vkCmdClearAttachments(g_command_buffer, 1, &clear_attachment, 1, &clear_rect);
    }

    g_has_pending_command = 1;
    return VG_LITE_SUCCESS;
}

vg_lite_error_t vg_lite_finish(void)
{
    vulkan_context_t* ctx = get_vulkan_context();
    if (!ctx) {
        return VG_LITE_GENERIC_ERROR;
    }

    if (!g_has_pending_command) {
        return VG_LITE_SUCCESS;
    }

    end_render_pass();

    VkResult result = vkEndCommandBuffer(g_command_buffer);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to end command buffer: %d\n", result);
        return VG_LITE_GENERIC_ERROR;
    }

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = NULL,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = NULL,
        .pWaitDstStageMask = NULL,
        .commandBufferCount = 1,
        .pCommandBuffers = &g_command_buffer,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = NULL
    };

    result = vkQueueSubmit(ctx->queue, 1, &submit_info, ctx->fence);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to submit command buffer: %d\n", result);
        return VG_LITE_GENERIC_ERROR;
    }

    result = vkWaitForFences(ctx->device, 1, &ctx->fence, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to wait for fence: %d\n", result);
        return VG_LITE_GENERIC_ERROR;
    }

    result = vkResetFences(ctx->device, 1, &ctx->fence);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to reset fence: %d\n", result);
        return VG_LITE_GENERIC_ERROR;
    }

    result = vkResetCommandBuffer(g_command_buffer, 0);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to reset command buffer: %d\n", result);
        return VG_LITE_GENERIC_ERROR;
    }

    if (g_framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(ctx->device, g_framebuffer, NULL);
        g_framebuffer = VK_NULL_HANDLE;
    }

    if (g_render_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(ctx->device, g_render_pass, NULL);
        g_render_pass = VK_NULL_HANDLE;
    }

    g_current_target = NULL;
    g_command_buffer_begun = 0;
    g_has_pending_command = 0;

    return VG_LITE_SUCCESS;
}

/**
 * Mark that a command buffer has been recorded and needs to be submitted.
 * This should be called by functions that record commands (like vg_lite_clear).
 */
void vg_lite_set_pending_command(int pending)
{
    g_has_pending_command = pending;
}

/**
 * Find a suitable memory type for a buffer.
 */
static int find_memory_type(VkPhysicalDevice physical_device,
                            uint32_t type_filter,
                            VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_props);

    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((type_filter & (1 << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return (int)i;
        }
    }
    return -1;
}

/**
 * Save a buffer to a PNG file.
 * Performs GPU readback and converts RGB565 to RGB888.
 */
vg_lite_error_t vg_lite_save_png(const char* filename, vg_lite_buffer_t* buffer)
{
    /* Validate parameters */
    if (!filename || !buffer) {
        return VG_LITE_INVALID_ARGUMENT;
    }

    if (!buffer->handle) {
        return VG_LITE_INVALID_ARGUMENT;
    }

    vulkan_context_t* ctx = get_vulkan_context();
    if (!ctx) {
        return VG_LITE_GENERIC_ERROR;
    }

    vulkan_buffer_handle_t* handle = (vulkan_buffer_handle_t*)buffer->handle;
    int32_t width = buffer->width;
    int32_t height = buffer->height;

    /* Calculate buffer size for RGB565 (2 bytes per pixel) */
    VkDeviceSize buffer_size = (VkDeviceSize)width * height * 2;

    /* Create staging buffer - host visible and host coherent */
    VkBufferCreateInfo staging_buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .size = buffer_size,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = NULL
    };

    VkBuffer staging_buffer;
    VkResult result = vkCreateBuffer(ctx->device, &staging_buffer_info, NULL, &staging_buffer);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create staging buffer: %d\n", result);
        return VG_LITE_OUT_OF_MEMORY;
    }

    /* Allocate memory for staging buffer */
    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(ctx->device, staging_buffer, &mem_reqs);

    int mem_type = find_memory_type(ctx->physical_device,
                                    mem_reqs.memoryTypeBits,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mem_type < 0) {
        fprintf(stderr, "Failed to find suitable memory type for staging buffer\n");
        vkDestroyBuffer(ctx->device, staging_buffer, NULL);
        return VG_LITE_OUT_OF_MEMORY;
    }

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = NULL,
        .allocationSize = mem_reqs.size,
        .memoryTypeIndex = (uint32_t)mem_type
    };

    VkDeviceMemory staging_memory;
    result = vkAllocateMemory(ctx->device, &alloc_info, NULL, &staging_memory);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to allocate staging memory: %d\n", result);
        vkDestroyBuffer(ctx->device, staging_buffer, NULL);
        return VG_LITE_OUT_OF_MEMORY;
    }

    result = vkBindBufferMemory(ctx->device, staging_buffer, staging_memory, 0);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to bind staging memory: %d\n", result);
        vkFreeMemory(ctx->device, staging_memory, NULL);
        vkDestroyBuffer(ctx->device, staging_buffer, NULL);
        return VG_LITE_GENERIC_ERROR;
    }

    /* Create a command buffer for the copy operation */
    VkCommandBufferAllocateInfo cmd_alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = NULL,
        .commandPool = ctx->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };

    VkCommandBuffer cmd_buffer;
    result = vkAllocateCommandBuffers(ctx->device, &cmd_alloc_info, &cmd_buffer);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to allocate command buffer: %d\n", result);
        vkFreeMemory(ctx->device, staging_memory, NULL);
        vkDestroyBuffer(ctx->device, staging_buffer, NULL);
        return VG_LITE_GENERIC_ERROR;
    }

    /* Begin command buffer recording */
    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = NULL,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = NULL
    };

    result = vkBeginCommandBuffer(cmd_buffer, &begin_info);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to begin command buffer: %d\n", result);
        vkFreeCommandBuffers(ctx->device, ctx->command_pool, 1, &cmd_buffer);
        vkFreeMemory(ctx->device, staging_memory, NULL);
        vkDestroyBuffer(ctx->device, staging_buffer, NULL);
        return VG_LITE_GENERIC_ERROR;
    }

    /* Transition image layout from UNDEFINED to TRANSFER_SRC_OPTIMAL */
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = NULL,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = handle->image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    vkCmdPipelineBarrier(cmd_buffer,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         0, NULL,
                         0, NULL,
                         1, &barrier);

    /* Copy image to staging buffer */
    VkImageSubresourceLayers subresource = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel = 0,
        .baseArrayLayer = 0,
        .layerCount = 1
    };

    VkBufferImageCopy region = {
        .bufferOffset = 0,
        .bufferRowLength = (uint32_t)width,
        .bufferImageHeight = (uint32_t)height,
        .imageSubresource = subresource,
        .imageOffset = { 0, 0, 0 },
        .imageExtent = { (uint32_t)width, (uint32_t)height, 1 }
    };

    vkCmdCopyImageToBuffer(cmd_buffer,
                           handle->image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           staging_buffer,
                           1,
                           &region);

    /* End command buffer recording */
    result = vkEndCommandBuffer(cmd_buffer);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to end command buffer: %d\n", result);
        vkFreeCommandBuffers(ctx->device, ctx->command_pool, 1, &cmd_buffer);
        vkFreeMemory(ctx->device, staging_memory, NULL);
        vkDestroyBuffer(ctx->device, staging_buffer, NULL);
        return VG_LITE_GENERIC_ERROR;
    }

    /* Submit the command buffer and wait for completion */
    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = NULL,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = NULL,
        .pWaitDstStageMask = NULL,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd_buffer,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = NULL
    };

    result = vkQueueSubmit(ctx->queue, 1, &submit_info, ctx->fence);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to submit command buffer: %d\n", result);
        vkFreeCommandBuffers(ctx->device, ctx->command_pool, 1, &cmd_buffer);
        vkFreeMemory(ctx->device, staging_memory, NULL);
        vkDestroyBuffer(ctx->device, staging_buffer, NULL);
        return VG_LITE_GENERIC_ERROR;
    }

    /* Wait for the fence to signal completion */
    result = vkWaitForFences(ctx->device, 1, &ctx->fence, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to wait for fence: %d\n", result);
        vkFreeCommandBuffers(ctx->device, ctx->command_pool, 1, &cmd_buffer);
        vkFreeMemory(ctx->device, staging_memory, NULL);
        vkDestroyBuffer(ctx->device, staging_buffer, NULL);
        return VG_LITE_GENERIC_ERROR;
    }

    /* Reset fence for reuse */
    vkResetFences(ctx->device, 1, &ctx->fence);

    /* Free the command buffer */
    vkFreeCommandBuffers(ctx->device, ctx->command_pool, 1, &cmd_buffer);

    /* Map the staging buffer memory and read the data */
    void* mapped_data = NULL;
    result = vkMapMemory(ctx->device, staging_memory, 0, VK_WHOLE_SIZE, 0, &mapped_data);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to map staging memory: %d\n", result);
        vkFreeMemory(ctx->device, staging_memory, NULL);
        vkDestroyBuffer(ctx->device, staging_buffer, NULL);
        return VG_LITE_GENERIC_ERROR;
    }

    /* Get actual row pitch using vkGetImageSubresourceLayout */
    VkImageSubresource subresource_layout = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel = 0,
        .arrayLayer = 0
    };

    VkSubresourceLayout layout;
    vkGetImageSubresourceLayout(ctx->device, handle->image, &subresource_layout, &layout);

    /* Allocate buffer for RGB888 conversion (3 bytes per pixel) */
    uint8_t* rgb888_data = (uint8_t*)malloc(width * height * 3);
    if (!rgb888_data) {
        fprintf(stderr, "Failed to allocate RGB888 buffer\n");
        vkUnmapMemory(ctx->device, staging_memory);
        vkFreeMemory(ctx->device, staging_memory, NULL);
        vkDestroyBuffer(ctx->device, staging_buffer, NULL);
        return VG_LITE_OUT_OF_MEMORY;
    }

    /* Convert RGB565 to RGB888 */
    /* RGB565 format: R (5 bits), G (6 bits), B (5 bits) in 16-bit value */
    uint8_t* src = (uint8_t*)mapped_data;
    uint8_t* dst = rgb888_data;

    /* Use the row pitch from the layout, or default to width * 2 */
    VkDeviceSize row_pitch = (layout.rowPitch > 0) ? layout.rowPitch : (VkDeviceSize)width * 2;

    for (int32_t y = 0; y < height; y++) {
        uint16_t* row = (uint16_t*)(src + y * row_pitch);
        for (int32_t x = 0; x < width; x++) {
            uint16_t pixel = row[x];

            /* Extract RGB565 components */
            uint8_t r5 = (pixel >> 11) & 0x1F;
            uint8_t g6 = (pixel >> 5) & 0x3F;
            uint8_t b5 = pixel & 0x1F;

            /* Scale to 8-bit values */
            dst[0] = (r5 * 255) / 31;  /* R: 5 bits -> 8 bits */
            dst[1] = (g6 * 255) / 63;  /* G: 6 bits -> 8 bits */
            dst[2] = (b5 * 255) / 31;  /* B: 5 bits -> 8 bits */

            dst += 3;
        }
    }

    /* Write PNG file */
    int write_result = stbi_write_png(filename, width, height, 3, rgb888_data, width * 3);
    if (!write_result) {
        fprintf(stderr, "Failed to write PNG file: %s\n", filename);
        free(rgb888_data);
        vkUnmapMemory(ctx->device, staging_memory);
        vkFreeMemory(ctx->device, staging_memory, NULL);
        vkDestroyBuffer(ctx->device, staging_buffer, NULL);
        return VG_LITE_GENERIC_ERROR;
    }

    /* Cleanup */
    free(rgb888_data);
    vkUnmapMemory(ctx->device, staging_memory);
    vkFreeMemory(ctx->device, staging_memory, NULL);
    vkDestroyBuffer(ctx->device, staging_buffer, NULL);

    return VG_LITE_SUCCESS;
}