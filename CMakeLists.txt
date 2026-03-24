cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(vglite_vulkan LANGUAGES C)

# Vulkan SDK configuration
set(VULKAN_SDK_PATH "E:/VulkanSDK/1.3.296.0")

# Find Vulkan package
find_package(Vulkan REQUIRED)

# Set include directories
set(VGLITE_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/include")
set(VGLITE_THIRD_PARTY_DIR "${CMAKE_SOURCE_DIR}/third_party")

include_directories(
    ${VGLITE_INCLUDE_DIR}
    ${VGLITE_THIRD_PARTY_DIR}
    ${Vulkan_INCLUDE_DIRS}
)

# Link directories
link_directories(${Vulkan_LIBRARY_DIRS})

# VGLite source files
set(VGLITE_SOURCES
    src/main.c
    src/vulkan_context.c
    src/vulkan_buffer.c
    src/vg_lite.c
)

# Create executable target
add_executable(vglite_clear ${VGLITE_SOURCES})

# Link libraries
target_link_libraries(vglite_clear PRIVATE
    Vulkan::Vulkan
)

# Set output directory
set_target_properties(vglite_clear PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)