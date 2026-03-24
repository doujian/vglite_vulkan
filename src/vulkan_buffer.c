#include "vulkan_buffer.h"
#include "vulkan_context.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static VkFormat vg_lite_format_to_vk(vg_lite_buffer_format_t format)
{
    switch (format) {
        case VG_LITE_RGB565:
            return VK_FORMAT_R5G6B5_UNORM_PACK16;
        default:
            return VK_FORMAT_UNDEFINED;
    }
}

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

static int is_format_supported(VkPhysicalDevice physical_device,
                               VkFormat format,
                               VkImageTiling tiling,
                               VkImageUsageFlags usage)
{
    VkImageFormatProperties props;
    VkResult result = vkGetPhysicalDeviceImageFormatProperties(
        physical_device,
        format,
        VK_IMAGE_TYPE_2D,
        tiling,
        usage,
        0,
        &props
    );
    return result == VK_SUCCESS;
}

vg_lite_error_t vg_lite_allocate(vg_lite_buffer_t* buffer)
{
    vulkan_context_t* ctx = get_vulkan_context();
    if (!ctx) {
        return VG_LITE_GENERIC_ERROR;
    }

    if (!buffer || buffer->width <= 0 || buffer->height <= 0) {
        return VG_LITE_INVALID_ARGUMENT;
    }

    vulkan_buffer_handle_t* handle = calloc(1, sizeof(vulkan_buffer_handle_t));
    if (!handle) {
        return VG_LITE_OUT_OF_MEMORY;
    }

    VkFormat requested_format = vg_lite_format_to_vk(buffer->format);
    VkFormat selected_format = requested_format;
    
    const VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    
    if (!is_format_supported(ctx->physical_device, selected_format,
                             VK_IMAGE_TILING_OPTIMAL, usage)) {
        selected_format = VK_FORMAT_R8G8B8A8_UNORM;
        
        if (!is_format_supported(ctx->physical_device, selected_format,
                                 VK_IMAGE_TILING_OPTIMAL, usage)) {
            free(handle);
            return VG_LITE_GENERIC_ERROR;
        }
    }

    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = selected_format,
        .extent = {
            .width = (uint32_t)buffer->width,
            .height = (uint32_t)buffer->height,
            .depth = 1
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = NULL,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };

    VkResult result = vkCreateImage(ctx->device, &image_info, NULL, &handle->image);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create image: %d\n", result);
        free(handle);
        return VG_LITE_OUT_OF_MEMORY;
    }

    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(ctx->device, handle->image, &mem_reqs);

    int mem_type = find_memory_type(ctx->physical_device,
                                    mem_reqs.memoryTypeBits,
                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mem_type < 0) {
        fprintf(stderr, "Failed to find suitable memory type\n");
        vkDestroyImage(ctx->device, handle->image, NULL);
        free(handle);
        return VG_LITE_OUT_OF_MEMORY;
    }

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = NULL,
        .allocationSize = mem_reqs.size,
        .memoryTypeIndex = (uint32_t)mem_type
    };

    result = vkAllocateMemory(ctx->device, &alloc_info, NULL, &handle->memory);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to allocate image memory: %d\n", result);
        vkDestroyImage(ctx->device, handle->image, NULL);
        free(handle);
        return VG_LITE_OUT_OF_MEMORY;
    }

    result = vkBindImageMemory(ctx->device, handle->image, handle->memory, 0);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to bind image memory: %d\n", result);
        vkFreeMemory(ctx->device, handle->memory, NULL);
        vkDestroyImage(ctx->device, handle->image, NULL);
        free(handle);
        return VG_LITE_GENERIC_ERROR;
    }

    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .image = handle->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = selected_format,
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

    result = vkCreateImageView(ctx->device, &view_info, NULL, &handle->view);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create image view: %d\n", result);
        vkFreeMemory(ctx->device, handle->memory, NULL);
        vkDestroyImage(ctx->device, handle->image, NULL);
        free(handle);
        return VG_LITE_GENERIC_ERROR;
    }

    handle->vk_format = selected_format;
    buffer->handle = handle;
    buffer->memory = handle;
    buffer->stride = buffer->width;

    return VG_LITE_SUCCESS;
}

vg_lite_error_t vg_lite_free(vg_lite_buffer_t* buffer)
{
    if (!buffer || !buffer->handle) {
        return VG_LITE_INVALID_ARGUMENT;
    }

    vulkan_context_t* ctx = get_vulkan_context();
    if (!ctx) {
        return VG_LITE_GENERIC_ERROR;
    }

    vulkan_buffer_handle_t* handle = (vulkan_buffer_handle_t*)buffer->handle;

    if (handle->view != VK_NULL_HANDLE) {
        vkDestroyImageView(ctx->device, handle->view, NULL);
    }

    if (handle->image != VK_NULL_HANDLE) {
        vkDestroyImage(ctx->device, handle->image, NULL);
    }

    if (handle->memory != VK_NULL_HANDLE) {
        vkFreeMemory(ctx->device, handle->memory, NULL);
    }

    free(handle);

    memset(buffer, 0, sizeof(vg_lite_buffer_t));

    return VG_LITE_SUCCESS;
}
