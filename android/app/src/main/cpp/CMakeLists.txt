cmake_minimum_required(VERSION 3.22.1)

project(vglite_vulkan LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(VGLITE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../src)
set(VGLITE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../include)
set(VGLITE_THIRD_PARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../third_party)

# Build native_app_glue as a static library
add_library(native_app_glue STATIC
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
)

target_include_directories(native_app_glue PUBLIC
    ${ANDROID_NDK}/sources/android/native_app_glue
)

add_library(vglite_vulkan SHARED
    main_android.cpp
    ${VGLITE_SOURCE_DIR}/vg_lite.c
    ${VGLITE_SOURCE_DIR}/vulkan_context.c
    ${VGLITE_SOURCE_DIR}/vulkan_buffer.c
)

target_include_directories(vglite_vulkan PRIVATE
    ${VGLITE_INCLUDE_DIR}
    ${VGLITE_THIRD_PARTY_DIR}
    ${ANDROID_NDK}/sources/third_party/vulkan/src/include
    ${ANDROID_NDK}/sources/android/native_app_glue
)

target_link_libraries(vglite_vulkan
    android
    log
    vulkan
    native_app_glue
)

target_include_directories(vglite_vulkan PRIVATE
    ${VGLITE_INCLUDE_DIR}
    ${VGLITE_THIRD_PARTY_DIR}
    ${ANDROID_NDK}/sources/third_party/vulkan/src/include
    ${ANDROID_NDK}/sources/android/native_app_glue
)

target_link_libraries(vglite_vulkan
    android
    log
    vulkan
    native_app_glue
)

set_target_properties(vglite_vulkan PROPERTIES
    C_EXTENSIONS ON
    C_STANDARD_REQUIRED ON
)
