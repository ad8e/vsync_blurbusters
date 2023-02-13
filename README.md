Mark Rejhon wants to create a cross-platform API for finding the vsync timings. This is the first step along that path. The demo is in `render_vsync_demo.cpp`.

The platform APIs are in `platform_vsync_linux.cpp` and `platform_vsync_windows.cpp`.

`vsync.cpp` turns a stream of timepoints from a wakeup thread into a period and phase pair, and is seriously complex. `vsync_with_scanline.cpp` turns a stream of accurate scanlines into a period and phase pair, and is simple linear regression. If your platform gives you the vsync period exactly, you don't need either of these.

The other files are helper files which you can ignore.

It works on Linux, using OML to get the vsync timepoint.

It works on Windows, using either scanlines or waiting. It's not clear which is preferred. On Intel GPUs, scanlines are better. On Nvidia, scanlines may have problems.

Guide:
1. install GLFW. On Linux, that's `sudo apt install libglfw3-dev`
2. Compile. On Linux, that's `g++ render_vsync_demo.cpp -std=c++20 -lGL -lglfw -lXrandr -Ij -lpthread -O2`