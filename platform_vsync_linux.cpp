//this file seems to clobber LLVM. make sure it's included afterward.
#pragma once
#if __linux__
#include "GLFW/glfw3native.h"
#include "console.h"
#include "glfw include.h"
#include "X11/extensions/Xrandr.h" //to get modeline information
#include "platform_vsync.h"

#define GLX_GLXEXT_PROTOTYPES //for glXGetSyncValuesOML
#include "GL/glx.h"

#define ANY_SYNC_SUPPORTED 1
#define SYNC_IN_RENDER_THREAD 1
#define SYNC_IN_SEPARATE_THREAD 0
#define SYNC_LINUX 1

GLXDrawable global_drawable;
Display* global_display;
int64_t ust_global; //timestamp
int64_t msc_global; //vertical retrace number
int64_t sbc_global; //swap buffer number

//run this after making the OpenGL context current
void prepare_sync() {
	global_drawable = glXGetCurrentDrawable();
	check(global_drawable != None, "couldn't get Drawable");
	global_display = glfwGetX11Display();
	check(global_display != NULL, "couldn't get X11 Display");
	std::string list_of_extensions = glXQueryExtensionsString(global_display, 0); //it's probably very inefficient to build a string this way
	check(list_of_extensions.find("GLX_OML_sync_control") != std::string::npos, "OML not supported");
	//see https://invent.kde.org/plasma/kwin/-/blob/master/src/backends/x11/standalone/x11_standalone_omlsynccontrolvsyncmonitor.cpp
	//check(glfwExtensionSupported("GLX_OML_sync_control"), "OML not supported"); //this is not the right way to check for the extension
}
namespace vscan {
extern uint64_t phase;
extern double period;
}

void get_sync_values() {
	auto old_ust = ust_global;
	auto old_msc = msc_global;
	bool result = glXGetSyncValuesOML(global_display, global_drawable, &ust_global, &msc_global, &sbc_global);
	check(result == 1, "OML failed");
	if (msc_global != old_msc) {
		vscan::phase = ust_global * 1000 + 500; //UST is in microseconds, the system clock is in nanoseconds. so we apply a very stupid transform here. this will fail if the main clock wraps around, but that takes 600 years, so I'm not worried
		vscan::period = (ust_global - old_ust) * 1000.0 / (msc_global - old_msc); //period is in nanoseconds
	}
	//good news: UST is benched to Linux's steady clock, not the realtime clock
	//outc("realtime, steady", std::chrono::high_resolution_clock::now().time_since_epoch().count(), now(), vscan::phase);
	//outc("UST was", ust_global, msc_global, sbc_global, now());
}

//call this after prepare_sync(); it uses the global_display
//this acquires modeline information
//how to map xrandr values to porch info: https://www.reddit.com/r/SolusProject/comments/hp96vl/mapping_for_xrandr_modeline_and_windows_porchsync/
//https://www.mythtv.org/wiki/Working_with_Modelines#Working_with_Modelines_by_Hand
void get_scanline_info() {
	auto X11window = glfwGetX11Window(window);
	XRRScreenResources* sr = XRRGetScreenResourcesCurrent(global_display, X11window);
	RRCrtc current_crtc = glfwGetX11Adapter(active_monitor);
	XRRCrtcInfo* ci = XRRGetCrtcInfo(global_display, sr, current_crtc);

	for (unsigned mode_number : zero_to(sr->nmode)) {
		XRRModeInfo& mode = sr->modes[mode_number];
		if (mode.id == ci->mode) { //we only grab the primary monitor
			active_scanlines = mode.height; //displayed screen size (such as 1080)
			total_scanlines = mode.vTotal; //total lines (such as 1125)
			porch_scanlines = active_scanlines - total_scanlines;
			//see http://howto-pages.org/ModeLines/ if further tutorial about modelines is wanted
			unsigned front_porch = mode.vSyncStart - mode.height;
			unsigned VBI = mode.vSyncEnd - mode.vSyncStart;
			unsigned back_porch = mode.vTotal - mode.vSyncEnd;
			scanlines_between_sync_and_first_displayed_line = VBI + back_porch;
			outc("vertical height", active_scanlines, "vertical total", total_scanlines, "front porch", front_porch, "VBI", VBI, "back porch", back_porch);
		}
	}
	XRRFreeCrtcInfo (ci);
	XRRFreeScreenResources(sr);
}
#endif