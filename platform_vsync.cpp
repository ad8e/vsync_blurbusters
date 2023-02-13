
#pragma once
#include "glfw include.h"
#if _WIN32
#include "platform_vsync_windows.cpp"
#elif __linux__
#include "platform_vsync_linux.cpp"
#endif

unsigned get_refresh_rate() {
	//"This function must only be called from the main thread"
	check(active_monitor != nullptr, "???");
	auto error_check = glfwGetVideoMode(active_monitor);
	check(error_check, "getting refresh rate failed somehow");
	return error_check->refreshRate;
}

//these are signed because you do math with them. (add, subtract, divide)
int total_scanlines; //such as 1125
int active_scanlines; //such as 1080
int porch_scanlines; //porch = 1125 - 1080
int scanlines_between_sync_and_first_displayed_line = 1; //VBI + back porch. it's at least 1.
