#pragma once

#include "glfw include.h"
#include "platform_vsync.cpp"
#include "renderer.h"
#include "timing.h"
#include "vsync.cpp"
#include <atomic>

#if DISABLE_FONTS
//#include "font_renderer_dummy.cpp"
#else
#include "font_renderer.cpp"
#endif

uint64_t busy_force = ticks_per_sec / 1000; //wake up the GPU and CPU early, putting them in a higher power state, and improving consistency of timing. not currently used

bool time_to_exit();

float user_desired_phase_offset = 0; //from 0 to 1. as a ratio of a single frame.
//low value = top of screen, near-1 = bottom of screen.
//future idea: don't present if you're early, use the swapchain instead.

#if SYNC_IN_SEPARATE_THREAD
uint64_t vblank_time() {
	while (wait_for_vblank()) {
		outc("failure to receive vsync heartbeat (computer probably went to sleep)");
		vf::restart(now());
	}
	//native_sleep_at_most(random_number() / float(random_fo::max()) * ticks_per_sec / 10); //add artificial noise, up to 100 ms
	return now();
};

void get_vsynctimes() {
	//vblank_time(); //discarding the first timepoint doesn't help.
	while (!time_to_exit()) {
		auto newest_timepoint = vblank_time();
		//native_sleep_at_most(ticks_per_sec / 120); //idea: the massive jumps in vsync cut when the mouse moves are because the rendering is colliding with something. so maybe sleeping will offset this thread? result: nope, doesn't help.
		//it also doesn't help if I change input_and_render_separate_threads to false.
		vf::new_value(newest_timepoint);
		//outc("vsync finder took", 1000 * (now() - newest_timepoint) / float(ticks_per_sec)); //this is for benchmarking the finder
		//if (vf::elements() > 16) outc("jitter in vblank signal", 1000 * vf::calc_error_in_shitty_way() / ticks_per_sec); //this is for benchmarking the input signal accuracy
		//somehow, outputting here causes the tearline to wobble!

		//static uint64_t vblank_history[2] = {};
		//auto phase = vf::vblank_phase_atomic.load();
		//uint64_t difference_this_frame = phase - vblank_history[1];
		//uint64_t difference_last_frame = vblank_history[1] - vblank_history[0];
		//int milli_error = int(std::log(float(difference_this_frame) / difference_last_frame) * 1000);
		//if (milli_error != 0)
		//	outc("self-inconsistency", milli_error);
		//vblank_history[0] = vblank_history[1];
		//vblank_history[1] = phase;
		//note that pressing 2 then 3 causes a giant shift, because it's not receiving any vblank signals

		//this is used to compare two finders to each other, to see which one is lagging. one will have many points, one will have few
		//vf2::new_value(newest_timepoint);
		//double diff = int64_t(vf::vblank_phase_atomic - vf2::vblank_phase_atomic);
		//if (vf::elements() == vf::max_size && vf2::elements() == vf2::max_size) {
		//	double diff_ratio = diff / vf2::calc_error();
		//	static double trailing_diff_ratio = 2.0 / vf::max_size;
		//	trailing_diff_ratio = 0.9999 * trailing_diff_ratio + 0.0001 * diff_ratio;
		//	static double trailing_diff_abs = 0;
		//	trailing_diff_abs = 0.9999 * trailing_diff_abs + 0.0001 * diff;
		//	static double trailing_error = 0;
		//	trailing_error = 0.9999 * trailing_error + 0.0001 * vf2::calc_error();
		//	outc(trailing_diff_ratio, trailing_diff_abs, trailing_error);
		//}
	}
}
#endif

void swap_now() {
	if (render::double_buffered)
		glfwSwapBuffers(window);
	else
		glFlush();
}

//no longer used. but there are still some good ideas. todo: move them over

//the heartbeat is still better than double-buffered vsync, even if the phase is random. might change once we add sleeping to the double buffer vsync
//when in windowed mode, frames are forced to boundaries, and I can't tell when they are. so just use the cheap sleep function

//future: we want to do some sleeping, with double buffering and with easy vsync.
//our idea is to sleep until just before the next frame. unfortunately, if frames start slipping, then we can never recover.

void send_junk_floats_to_GPU_to_wake_it_up() {
	//accurate_sleep_until(next_frame_vblank - render_allowance - busy_force, time_after_render);
	{
		float junk = 0.0;
		glBufferSubData(GL_ARRAY_BUFFER, 0, 1 * 4, &junk); //wake up the GPU from power throttling. send one float
		//glfwSwapBuffers(window); //way too expensive for Intel HD4000
	}
}

void tell_system_whether_to_wait_for_vsync() {
	using namespace render;
	if ((sync_mode == double_buffer_vsync)) { //} || !fullscreen) {
		glfwSwapInterval(1); //turns on Vsync by waiting for the frame.
		//in windowed mode, double buffer vsync still kills us.
		//vsync-free has less one frame of lag from the mouse when windowed. even if we don't time it at all, it's still better than double buffer vsync.
	}
	else {
		//this forces double buffering vsync on, just a different type. don't turn it on when trying to manually wait-and-swap.
		//doesn't work on my old T530 in Windows; it reports "supported" but then still changes nothing (the sine wave animation still works). what about other people's machines
		//on my new Linux Framework, it works but causes lag. so don't use it.
		//if (glfwExtensionSupported("WGL_EXT_swap_control_tear") || glfwExtensionSupported("GLX_EXT_swap_control_tear")) {
		//	outc("supported");
		//	glfwSwapInterval(-1);
		//	return;
		//}

		glfwSwapInterval(0);
	}
}
