# VGLite Vulkan Port

## Description

Port of VGLite clear.c test to Vulkan backend. This project implements the VGLite 2D graphics API using Vulkan as the rendering backend, enabling hardware-accelerated 2D graphics on platforms that support Vulkan.

The clear.c sample demonstrates the core VGLite workflow: initialize the library, allocate an off-screen buffer, perform clear operations, and save the result as a PNG image.

## Prerequisites

- **Vulkan SDK** (1.3 or newer)
- **CMake** (3.20 or newer)
- **GLFW** (bundled with the project)
- **Visual Studio 2022** (Windows) or **GCC/Clang** (Linux)

## Build (Windows)

1. Install the Vulkan SDK from [LunarG](https://vulkan.lunarg.com/)

2. Open a Developer Command Prompt for Visual Studio 2022

3. Configure and build:

```cmd
cd vglite_vulkan
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

4. The executable will be at `build\Release\vglite_clear.exe`

## Build (Linux)

1. Install the Vulkan SDK and development libraries:

```bash
# Ubuntu/Debian
sudo apt-get install vulkan-sdk libvulkan-dev

# Fedora
sudo dnf install vulkan-loader-devel vulkan-headers
```

2. Build the project:

```bash
cd vglite_vulkan
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

3. The executable will be at `build/vglite_clear`

## Build (Android)

1. Install Android SDK and NDK
   - SDK path: `E:\Android\Sdk`
   - NDK is typically at: `E:\Android\Sdk\ndk\<version>`

2. Build with Android NDK (arm64 example):

```cmd
cd vglite_vulkan
mkdir build-android && cd build-android
cmake .. -G "Ninja" ^
    -DCMAKE_TOOLCHAIN_FILE=E:/Android/Sdk/ndk/27.0.12077973/build/cmake/android.toolchain.cmake ^
    -DANDROID_ABI=arm64-v8a ^
    -DANDROID_PLATFORM=android-24
cmake --build .
```

3. Push to device and run:

```cmd
adb push bin/vglite_clear /data/local/tmp/
adb shell /data/local/tmp/vglite_clear
adb pull /data/local/tmp/clear.png .
```

## Usage

Run the clear test executable:

```bash
# Windows
.\vglite_clear.exe

# Linux
./vglite_clear
```

The program produces `clear.png` in the current working directory.

## API Usage Example

The clear.c test demonstrates basic VGLite API usage:

```c
#include "vg_lite.h"

// Initialize the VGLite library
vg_lite_init(0, 0);

// Allocate an off-screen buffer
vg_lite_buffer_t buffer;
buffer.width = 256;
buffer.height = 256;
buffer.format = VG_LITE_RGB565;
vg_lite_allocate(&buffer);

// Clear the entire buffer with blue
vg_lite_clear(&buffer, NULL, 0xFFFF0000);

// Clear a sub-rectangle with red
vg_lite_rectangle_t rect = { 64, 64, 64, 64 };
vg_lite_clear(&buffer, &rect, 0xFF0000FF);

// Finish rendering and save
vg_lite_finish();
vg_lite_save_png("clear.png", &buffer);
```

## Expected Output

The test generates a 256x256 PNG image with the following appearance:

- **Background**: Solid blue (entire buffer cleared with blue)
- **Rectangle**: A 64x64 red rectangle positioned at coordinates (64, 64)

The output file `clear.png` will be created in the working directory after the program completes successfully.
