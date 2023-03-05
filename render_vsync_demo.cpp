#define DISABLE_FONTS 1
#if _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
//miscellaneous helper files I carry around
#include "timing.cpp"
#include "console.h"
#include "renderer.h"
#include "render_present.cpp"
#include "frame_time_measurement.cpp"
#include <thread>
#include <atomic>
#include <mutex>
#if LOAD_WITH_GLAD
#include "etc/glad.c"
#endif

#include "platform_vsync.cpp" //platform-specific APIs for finding the vsync point
#include "vsync.cpp" //calculates phase and period when vsync is grabbed in a separate thread
#include "vsync_with_scanline.cpp" //calculates phase and period when the scanline is grabbed in the render thread

#define MEASURE_SWAP 1

namespace render {
GL_buffer<uint32_t> triangles;

//for timestamps
bool lt_circular(uint64_t a, uint64_t b) { return int64_t(a - b) < 0; }
bool lt_circular(uint32_t a, uint32_t b) { return int32_t(a - b) < 0; }
//disallow conversions, since comparison of unsigned is the whole point
template <class T, class U>
bool lt_circular(T a, U b) = delete;

void render_loop() {
	glfwMakeContextCurrent(window);
	tell_system_whether_to_wait_for_vsync();
#if LOAD_WITH_GLAD
	check(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress), "GLAD initialization failed");
#endif

#if SYNC_LINUX
	prepare_sync();
#endif
	get_scanline_info();

	triangles.program = compile_shaders(R"(#version 330 core
layout (location = 0) in mediump vec2 pos;
layout (location = 1) in mediump vec4 vertex_color;
out mediump vec4 pixel_color;

void main() {
	gl_Position = vec4(pos.x, pos.y, 0.0, 1.0);
	pixel_color = vertex_color;
})",
		R"(#version 330 core
out mediump vec4 color;
in mediump vec4 pixel_color;

void main() {
	color = vec4(pixel_color.z, pixel_color.y, pixel_color.x, 1.0);
})");

	glGenVertexArrays(1, &triangles.VAO);
	glBindVertexArray(triangles.VAO);
	glGenBuffers(1, &triangles.VBO);
	glBindBuffer(GL_ARRAY_BUFFER, triangles.VBO);
	glBufferData(GL_ARRAY_BUFFER, triangles.vertices.size() * sizeof(float), 0, GL_DYNAMIC_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 12, (void*)0);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, GL_BGRA, GL_UNSIGNED_INT_2_10_10_10_REV, GL_TRUE, 12, (void*)(2 * sizeof(float)));
	glEnableVertexAttribArray(1);
	glClearColor(1.0, 1.0, 1.0, 1.0);
	glViewport(0, 0, render::screen_w, render::screen_h);

	auto time_previous_frame_start = now();
#if ANY_SYNC_SUPPORTED
	uint64_t last_frame_vblank_target = time_previous_frame_start;
#endif

#if _WIN32
	bool fast_timer_on_Windows = true;
	improve_timer_resolution_on_Windows();
#endif

	glGenQueries(frame_time_buffer_size, query_circular_buffer.data());
	//variables for the animation
	bool bar_flip = 0; //flips every full run
	bool color_flip = 0; //flips every frame
	float bar_x = 0;

	while (!time_to_exit()) {
		glfwPollEvents();

		//uint64_t time_at_frame_start = now() + int64_t(generate_noise_for_timepoint.next_float() * ticks_per_sec / 60 / 16); //adds noise to the timepoint, for checking performance of the vsync finder
		uint64_t time_at_frame_start = now(); //if spam_swap is true, no need to call this. oh well. synchronizing the behavior would be too annoying, as spam_swap can change between frames, and then the previous timestamp would be out of whack. easier to just always call the timestamp.

		//vscan gives slightly less error if the scanline is before the timepoint. however, it's marginal: 0.0042 ms vs 0.0044 ms. it wobbles too. hard to tell if it's just noise.
		//if it's spam-swapping, we could get it only once per vsync. however, I think I don't care.
#if SYNC_IN_RENDER_THREAD && SCANLINE_VSYNC
		uint64_t scanline;
		if (sync_mode == sync_in_render_thread) {
			scanline = get_scanline();
			vscan::new_value(time_at_frame_start, scanline); //we reuse the time at frame start. that forces our scanline operation to be next to it, so there is no decision on where in a frame the scanline retrieval should be.
			update_scanline_boundaries();
		}
#endif

#if SYNC_LINUX
		get_sync_values();
#endif

		//whether you are trying to sync to the vsync point by waiting and swapping at a tearline
		bool vsync_period_phase_info_available = (sync_mode == separate_heartbeat) || (sync_mode == sync_in_render_thread);

		//we want to measure GPU time to get more accurate waits.
		//if the frames are taking too long, then we wouldn't be able to make use of GPU time anyway; the only possible strategy is to spam-swap, in which case the burden of measuring GPU time is a problem
		//GPU timestamps are slow and heavy. CPU time is just a signal to check if we should measure this.
		//(GPU time is short) || (CPU time between frames < vblank_period) = start measuring GPU time.
		//thus, we only measure GPU time if we expect frame times to be below one frame, meeting one of the following conditions:
		//1. the average GPU time is generally short enough (frame_time_smoothed)
		//2. the most recent GPU time was short (frame_time_single). this enables a faster recovery - a single good frame leads to more measurements of more good frames.
		//3. the CPU time is approximately equal to vblank_period - then sometimes we measure, sometimes not. this is a recovery mechanism and only needs to occasionally work.
		//CPU time is capped from below by the vblank period, so there's no point in trying to be more reliable than grabbing the occasional instances where it dips below from noise.
		//when the CPU time drops below, then GPU time measurement will kick in, and it'll stay measuring GPU time if it's appropriate.
		bool measure_GPU_time_spent = false;

#if ANY_SYNC_SUPPORTED
		uint64_t vblank_phase;
		double vblank_period;
		if (sync_mode == sync_in_render_thread) {
			vblank_phase = vscan::phase;
			vblank_period = vscan::period;
		}
		else if (sync_mode == separate_heartbeat) {
			vblank_phase = vf::vblank_phase_atomic.load(std::memory_order_relaxed);
			vblank_period = vf::vblank_period_atomic.load(std::memory_order_relaxed);
		}
		else
			error_assert("implement me");

		if (!spam_swap && vsync_period_phase_info_available)
			measure_GPU_time_spent =
				frame_time_single < vblank_period / ticks_per_sec ||
				frame_time_smoothed < vblank_period / ticks_per_sec ||
				time_at_frame_start - time_previous_frame_start < vblank_period;

		//if frames might be on time, it's worth checking the GPU time.
		//if frames are surely on time, it's worth syncing to vblank.
		bool wait_and_tear = measure_GPU_time_spent && frame_time_smoothed < vblank_period / ticks_per_sec; //we need this. be safe if the vsync finder returns junk values. so bail out after calculation

		//if period is more than one second. it's probably bogus information.
		//if phase is more than 100 seconds away. it's not likely to be accurate.
		//in both cases, just ignore it and spam-swap until we get real data
		if (vblank_period > ticks_per_sec || (uint64_t)std::abs(int64_t(vblank_phase - time_at_frame_start)) > ticks_per_sec * 10) {
			wait_and_tear = false;
		}

		uint64_t target_render_start_time;
		uint64_t target_swap_time;
		if (wait_and_tear) {
			//outc("phase", int64_t(vblank_phase_from_wait - vblank_phase) * 1000.0 / ticks_per_sec); //the wakeup vsync mechanism is earlier than the scanline mechanism! this output produces negative values. that's because the wakeup is at the beginning of the front porch, not the vsync.

			double time_between_render_start_and_tearline = frame_time_smoothed + render_overrun_buffer_room + GPU_swap_delay_undocumented;
			double adjustment_for_image_presentation_late_in_frame = (double)(scanlines_between_sync_and_first_displayed_line - porch_scanlines) / total_scanlines;
			double tearline_time_after_sync = user_desired_phase_offset + adjustment_for_image_presentation_late_in_frame; //aims for the end of the active display = beginning of the porch. this is because trying to render when the displayed lines go out seems to cause severe issues, so we avoid the end of the porch.
			int64_t time_rel_vblank_phase = time_at_frame_start - vblank_phase;
			int periods_to_move_forward_from_vblank = ceil((time_rel_vblank_phase + time_between_render_start_and_tearline * ticks_per_sec) / vblank_period - tearline_time_after_sync);
			uint64_t vblank_target = vblank_phase + uint64_t(periods_to_move_forward_from_vblank * vblank_period);
			if (int64_t(vblank_target - last_frame_vblank_target) < vblank_period / 2) {
				//auto distance_to_ceil = [](double f) { return ceil(f) - f; };
				//outc("extra wait, extra room was", distance_to_ceil((time_rel_vblank_phase + time_between_render_start_and_tearline * ticks_per_sec) / vblank_period - tearline_time_after_sync), periods_to_move_forward_from_vblank);
				++periods_to_move_forward_from_vblank; //you rendered super fast and are trying to render the same frame. so wait another frame.
			}
			last_frame_vblank_target = vblank_phase + uint64_t(periods_to_move_forward_from_vblank * vblank_period); //re-calculate it in case periods changed

			//tearline_time = vblank_phase + uint64_t((tearline_time_after_sync + periods_to_move_forward_from_vblank) * vblank_period);

			target_render_start_time = vblank_phase + uint64_t((tearline_time_after_sync + periods_to_move_forward_from_vblank) * vblank_period - time_between_render_start_and_tearline * ticks_per_sec);
			target_swap_time = vblank_phase + uint64_t((tearline_time_after_sync + periods_to_move_forward_from_vblank) * vblank_period - (GPU_swap_delay_undocumented + swap_time) * ticks_per_sec);
		}
#endif

		time_previous_frame_start = time_at_frame_start;
		//it's possible this isn't capturing the CPU-side of the rendering. maybe todo.
		if (measure_GPU_time_spent) {
			GPU_timestamp_send();
		}
		//rendering starts now; we've done all the waiting we want and gathered all the information we will have.
		auto time_at_render_start = now();

		//the animation. generates the triangles needed
		{
			//if (left_click_dragging) { //we use this to not buffer anything, and only swap. this determines that the large spikes are not caused by syncing issues between bufferSubData and the device rendering thread.
			bool single_bar = false;
			if (single_bar) glClear(GL_COLOR_BUFFER_BIT); //lowers fps from 760 to 580.
			//note: this will double clear on viewport change

			uint32_t color = 1047961;
			uint32_t ocolor = 1072693964;
			uint32_t white = 1073741823;

			if (color_flip) std::swap(color, ocolor);
			color_flip = !color_flip;
			auto bar = (bar_flip) ? 428867789 : 1073112064;

			bar_x += 0.02f;
			if (bar_x > 0.99f) {
				bar_x = -2;
				if (!single_bar)
					bar_flip = !bar_flip;
			}
			float xoffset = 3 * (10) / float(screen_w); //for triangle size
			float yoffset = 3 * (17.32f) / float(screen_h);
			//current mouse
			float screenx = 2 * mouse_x / float(screen_w) - 1.0f;
			float screeny = 2 * mouse_y / float(screen_h) - 1.0f;
			triangles.draw_triangle(screenx - xoffset, screeny + yoffset, color, screenx + xoffset, screeny + yoffset, ocolor, screenx, screeny, white);

			//indicator at left of screen
			triangles.draw_triangle(-2.f, 0.f, color, -0.98f, 2.f, color, -0.98f, -2.f, color);

			//quad for the moving bar
			triangles.draw_ortho(bar, bar_x, bar_x + 0.04f, (bar_x - 1) / 2, 1.f);
		}

		triangles.move_and_render();

#if ANY_SYNC_SUPPORTED
		if (busy_wait_for_exact_swap && wait_and_tear && now() <= target_swap_time) {
			//we have a wait operation. which means we must split the GPU measurement in two.

#if MEASURE_SWAP
			if (measure_GPU_time_spent)
				GPU_timestamp_send(0);
#else
			if (measure_GPU_time_spent)
				GPU_timestamp_send(2);
#endif
			accurate_sleep_until(target_swap_time);

#if MEASURE_SWAP
			if (measure_GPU_time_spent)
				GPU_timestamp_send();
#endif

			swap_now();

#if MEASURE_SWAP
			if (measure_GPU_time_spent) {
				GPU_timestamp_send(1);
			}
#endif
		}
		else
#endif
		{
			swap_now(); //Linux: if I turn this off, the input lag fixes itself! so glFlush is clobbering the latency of the event system
			if (measure_GPU_time_spent)
				GPU_timestamp_send(2);

			//it's important to measure the time that swap takes, because it can be 6 ms.
			//Intel HD 4000
			//if we measure before the swap, without glFlush(), the timer reports 0.026 ms per frame
			//if we measure after the swap, with glFlush(), the timer reports 0.53 ms per frame.
			//the swap takes 0.5 ms

			//Iris Xe: the delay is 0-6 ms, determined by the frequency of the GPU. you can see the GPU's frequency step around as its frequency changes.
			//to check this, I installed tlp, and changed these configurations, which forced the GPU to its lowest frequency, and stabilized the tearline at 2/5 down the screen:
			//INTEL_GPU_MAX_FREQ_ON_AC=100
			//INTEL_GPU_BOOST_FREQ_ON_AC=100
			//INTEL_GPU_MIN_FREQ_ON_AC=100

			//vf::new_value(now()); //for testing how accurate GPU wakeup is. turn on double buffer vsync, (it's not possible with separate_heartbeat_vsync, because it collides with the vsync finder. you'd need to copy a new one into a separate namespace)
			//if (vf::elements() > 16) outc("jitter:", vf::calc_error_in_shitty_way() * 1000.0 / ticks_per_sec);
		}

		if (measure_GPU_time_spent) {
			//uint max_queries_to_retrieve_per_frame = 2; //if you get 1 query, you're treading. if you get 2, you're moving forward. consuming 2 over time is good enough to recover from any delay. we don't want to ask for queries more than necessary.
			//however, now there can be either 2 or 4 queries. it's probably better not to set a limit.

			//I tried to measure performance of the Query operations by running Query 100 times, and looking at the sine wave animation. I turned on GPU timestamp measurement and turned off the vblank sync.
			//then, comment out glDeleteQueries(), because that is causing most of the jitter, which is making further jitter hard to see
			//"if (!spam_swap) measure_GPU_time_spent =" -> "if (1)"
			//"if (measure_GPU_time_spent) {" -> "if (0) {", where the vblank phase operations are
			//results are below in the zero_to(1000) comments
			while (lt_circular(index_lagging_GPU_time_to_retrieve, index_next_query_available)) {
				GLint done = 0;

				//for (int i : zero_to(1000)) //this checks how expensive the query availability retrieval is. interestingly, with Query deletion on, the sine wave animation is _more_ consistent when checking 100 times, than when checking once! it stops twitching back and forth different times per frame, and starts twitching evenly across frames. I assume that's bad even though it looks good.
				//if I check 1000 times, and turn Query deletion off, then the animation starts skipping 2 bars (32 pixels) instead of 1 bar (16 pixel). so it's pretty expensive
				glGetQueryObjectiv(query_circular_buffer[index_lagging_GPU_time_to_retrieve % frame_time_buffer_size], GL_QUERY_RESULT_AVAILABLE, &done);

				if (done)
					GPU_timestamp_retrieve();
				else
					break;
			}
		}
	}
}
} // namespace render

void mouse_cursor_callback(GLFWwindow* window, double xpos, double ypos) {
	render::mouse_x = xpos + 0.5;
	render::mouse_y = render::screen_h - ypos - 0.5;
	user_desired_phase_offset = ypos / render::screen_h;
}

int main(int argc, char** argv) {
	if (!glfwInit()) return -1;
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, true);
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, true);

	using namespace render;
	if (render::sync_mode != double_buffer_vsync && !double_buffered) glfwWindowHint(GLFW_DOUBLEBUFFER, 0); //turn off double buffering if it's not used

	active_monitor = glfwGetPrimaryMonitor();
	const GLFWvidmode* mode = glfwGetVideoMode(active_monitor);
	window = glfwCreateWindow(mode->width, mode->height, argv[0], active_monitor, nullptr);
	render::screen_w = mode->width;
	render::screen_h = mode->height;

	if (!window) {
		glfwTerminate();
		return -1;
	}
	glfwSetCursorPosCallback(window, mouse_cursor_callback);

	auto monitor_Hz = get_refresh_rate();
	extern double system_claimed_monitor_Hz;
	system_claimed_monitor_Hz = monitor_Hz;
	if (sync_mode == sync_in_render_thread)
		vscan::period = ticks_per_sec / double(monitor_Hz);
	else if (sync_mode == separate_heartbeat)
		vf::vblank_period_atomic.store(ticks_per_sec / double(monitor_Hz), std::memory_order_relaxed);

#if SYNC_IN_SEPARATE_THREAD
	if (render::sync_mode == separate_heartbeat) {
		std::thread vsync_timer(get_vsynctimes);
		vsync_timer.detach();
	}
#endif

	render::render_loop();
	glfwTerminate();
}
