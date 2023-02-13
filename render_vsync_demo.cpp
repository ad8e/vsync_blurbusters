#define DISABLE_FONTS 1
//miscellaneous helper files I carry around
#include "timing.cpp"
#include "console.h"
#include "renderer.h"
#include "render_present.cpp"
#include "frame_time_measurement.cpp"
#include <thread>
#include <atomic>
#include <mutex>

#include "platform_vsync.cpp" //platform-specific APIs for finding the vsync poing
#include "vsync.cpp" //calculates phase and period when vsync is grabbed in a separate thread
#include "vsync_with_scanline.cpp" //calculates phase and period when the scanline is grabbed in the render thread

namespace render {
GL_buffer<uint32_t> triangles;

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

	//variables for the animation
	bool bar_flip = 0; //flips every full run
	bool color_flip = 0; //flips every frame
	float bar_x = 0;

	while (!time_to_exit()) {
		glfwPollEvents();

		uint64_t time_at_frame_start = now();

#if SYNC_LINUX
		get_sync_values();
#endif

#if SYNC_IN_RENDER_THREAD && SCANLINE_VSYNC
		uint64_t scanline;
		if (sync_mode == sync_in_render_thread) {
			scanline = get_scanline();
			vscan::new_value(time_at_frame_start, scanline); //we reuse the time at frame start. that forces our scanline operation to be next to it, so there is no decision on where in a frame the scanline retrieval should be.
			update_scanline_boundaries();
		}
#endif

		time_previous_frame_start = time_at_frame_start;
		uint64_t vblank_phase;
		double vblank_period;
#if SYNC_IN_RENDER_THREAD
		vblank_period = vscan::period;
		vblank_phase = vscan::phase;
#elif SYNC_IN_SEPARATE_THREAD
		vblank_period = vf::vblank_period_atomic.load(std::memory_order_relaxed);
		vblank_phase = vf::vblank_phase_atomic.load(std::memory_order_relaxed);
#endif
		bool vblank_wait_mechanism_active = (sync_mode == sync_in_render_thread) || (sync_mode == separate_heartbeat);

		bool measure_GPU_time_spent = false;
		if (!spam_swap && vblank_wait_mechanism_active)
			measure_GPU_time_spent =
				single_frame_time < vblank_period / ticks_per_sec ||
				frame_time < vblank_period / ticks_per_sec ||
				time_at_frame_start - time_previous_frame_start < vblank_period;
		if (measure_GPU_time_spent)
			generate_OpenGL_Queries();

#if ANY_SYNC_SUPPORTED
		uint64_t time_when_image_will_be_seen;
		bool syncing_to_vblank = measure_GPU_time_spent && frame_time < vblank_period / ticks_per_sec;
		if (syncing_to_vblank) {
			if ((uint64_t)std::abs(int64_t(vblank_phase - time_at_frame_start)) > ticks_per_sec * 4) {
				//phase is totally out of whack. don't bother calculating
				syncing_to_vblank = false;
				goto skip_vsync_location_calculation;
			}
			//outc("phase", int64_t(vblank_phase_from_wait - vblank_phase) * 1000.0 / ticks_per_sec); //the wakeup vsync mechanism is earlier than the scanline mechanism! this output produces negative values. that's because the wakeup is at the beginning of the front porch, not the vsync.
			double delay_from_timer_and_swap_relation = GPU_swap_delay_in_ms / 1000;
			double seconds_to_spend_rendering = (frame_time + render_overrun_in_ms / 1000 + delay_from_timer_and_swap_relation); //from now until the frame is presented on screen
			double adjustment_for_image_presentation_late_in_frame = (double)(scanlines_between_sync_and_first_displayed_line - porch_scanlines) / total_scanlines; //to beginning of front porch
			double appearance_time_after_vsync_in_ticks = vblank_period * (user_desired_phase_offset + adjustment_for_image_presentation_late_in_frame); //when the frame is presented on screen. right now, it aims for the end of the active display = beginning of the porch. this is because trying to render when the displayed lines go out seems to cause severe issues, so we avoid the end of the porch.
			int64_t time_rel_vblank_phase = time_at_frame_start - vblank_phase;
			int periods_to_move_forward_from_vblank = ceil(time_rel_vblank_phase + seconds_to_spend_rendering * ticks_per_sec - appearance_time_after_vsync_in_ticks) / vblank_period;
			time_when_image_will_be_seen = vblank_phase + uint64_t(appearance_time_after_vsync_in_ticks + periods_to_move_forward_from_vblank * vblank_period);
			if (int64_t(last_frame_vblank_target + int64_t(vblank_period / 4) - time_when_image_will_be_seen) > 0)
				time_when_image_will_be_seen += int64_t(vblank_period); //you rendered super fast and are trying to render the same frame. so wait another frame.
			last_frame_vblank_target = time_when_image_will_be_seen;

			uint64_t target_render_start_time = time_when_image_will_be_seen - int64_t(seconds_to_spend_rendering * ticks_per_sec);

			if (int64_t(target_render_start_time - time_at_frame_start) > int64_t(ticks_per_sec / 30))
				target_render_start_time = time_at_frame_start + ticks_per_sec / 30;
			glfwPollEvents();
		}
	skip_vsync_location_calculation:
#else
		//no improved vsync support is available
#endif

		if (measure_GPU_time_spent) {
			glQueryCounter(query_double_buffer[index_next_query_available % frame_time_buffer_size], GL_TIMESTAMP);
			++index_next_query_available;
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
		if (vblank_wait_mechanism_active) {
			//timer before the swap. that is a bad thing; it will delay the swap. it will also be less accurate since we want to include the swap in the measured time
			//however, we have a wait operation. we cannot query the timestamp after the wait or it will become a self-fulfilling prophecy: times take long because you wait, then you wait for longer times.
			if (measure_GPU_time_spent) {
				glQueryCounter(query_double_buffer[index_next_query_available % frame_time_buffer_size], GL_TIMESTAMP);
				++index_next_query_available;
			}
			if (syncing_to_vblank)
				accurate_sleep_until(time_when_image_will_be_seen - int64_t(GPU_swap_delay_in_ms * ticks_per_sec / 1000));
			swap_now();
		}
		else
#endif
		{
			swap_now();
			if (measure_GPU_time_spent) {
				glQueryCounter(query_double_buffer[index_next_query_available % frame_time_buffer_size], GL_TIMESTAMP);
				++index_next_query_available;
				glFlush(); //this is necessary! otherwise, it waits for next frame and then reports a 16.6 ms frame time.
			}
		}

		if (measure_GPU_time_spent) {
			uint max_queries_to_try = 2;
			while (int(index_next_query_available - index_lagging_GPU_time_to_retrieve) > 0 && max_queries_to_try--) {
				GLuint64 time_after_render;
				GLint done = 0;

				glGetQueryObjectiv(query_double_buffer[index_lagging_GPU_time_to_retrieve % frame_time_buffer_size], GL_QUERY_RESULT_AVAILABLE, &done);

				if (done) {
					if (index_lagging_GPU_time_to_retrieve % 2) { //frame end timepoint
						glGetQueryObjectui64v(query_double_buffer[index_lagging_GPU_time_to_retrieve % frame_time_buffer_size], GL_QUERY_RESULT, &time_after_render);
						finished_time_retrieval(time_after_render);
					}
					else {
						//frame start timepoint
						glGetQueryObjectui64v(query_double_buffer[index_lagging_GPU_time_to_retrieve % frame_time_buffer_size], GL_QUERY_RESULT, &GPU_start_times[(index_lagging_GPU_time_to_retrieve / 2) % GPU_timestamp_buffer_size]);
					}
					++index_lagging_GPU_time_to_retrieve;
				}
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
	extern double frame_time;
	frame_time = 0.5 / monitor_Hz;
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
