cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(vglite_vulkan LANGUAGES C)

# Vulkan SDK configuration (for Windows/Linux)
if(NOT ANDROID)
    set(VULKAN_SDK_PATH "E:/VulkanSDK/1.3.296.0")
endif()

# Find Vulkan package (platform-specific)
if(ANDROID)
    # Android NDK configuration
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DVK_USE_PLATFORM_ANDROID_KHR")
    
    # Vulkan is part of Android NDK
    set(Vulkan_FOUND TRUE)
    set(Vulkan_INCLUDE_DIRS "${ANDROID_NDK}/sources/third_party/vulkan/src/include")
    set(Vulkan_LIBRARIES vulkan)
else()
    find_package(Vulkan REQUIRED)
endif()

# Set include directories
set(VGLITE_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/include")
set(VGLITE_THIRD_PARTY_DIR "${CMAKE_SOURCE_DIR}/third_party")

include_directories(
    ${VGLITE_INCLUDE_DIR}
    ${VGLITE_THIRD_PARTY_DIR}
    ${Vulkan_INCLUDE_DIRS}
)

# Link directories
if(NOT ANDROID)
    link_directories(${Vulkan_LIBRARY_DIRS})
endif()

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
    ${Vulkan_LIBRARIES}
)

# Set output directory
set_target_properties(vglite_clear PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)