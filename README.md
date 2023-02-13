Mark Rejhon wants to create a cross-platform API for finding the vsync timings. This is the first step along that path. The demo is in `render_vsync_demo.cpp`.

The platform APIs are in `platform_vsync_linux.cpp` and `platform_vsync_windows.cpp`.

`vsync.cpp` turns a stream of timepoints from a wakeup thread into a period and phase pair, and is seriously complex. `vsync_with_scanline.cpp` turns a stream of accurate scanlines into a period and phase pair, and is simple linear regression. If your platform gives you the vsync period exactly, you don't need either of these.

The other files are helper files which you can ignore.

It works on Linux, using OML to get the vsync timepoint.

It works on Windows, using either scanlines or waiting. It's not clear which is preferred. On Intel GPUs, scanlines are better. On Nvidia, scanlines may have problems. The scanline mechanism is used by default. If you want to try the waiting mechanism, there are instructions at the top of `platform_vsync_windows.cpp` for switching over.

Guide for Linux:
1. install GLFW: `sudo apt install libglfw3-dev`
2. Compile: `g++ render_vsync_demo.cpp -std=c++20 -lGL -lglfw -lXrandr -Ij -lpthread -O2`

On Windows:
I used mingw-w64.
1. Install GLFW. mingw-w64 provides `pacman -S mingw-w64-x86_64-glfw`. For Visual Studio, vcpkg can be used.
2. Using MSYS2: `g++ render_vsync_demo.cpp -std=c++20 -Ij -lpthread -lglfw3 -lole32 -lgdi32 -lwinmm -O2`
