#pragma once
#include "console.h"
#include "glfw include.h"
#include "renderer_ui_globals.h"
#include "timing.h"
#include <atomic>
#include <cmath>
#include <span>

#if _MSC_VER
#define DISABLE_FONTS 1
#endif

//future: glfwGetMonitorContentScale. I don't really understand, but it seems necessary. not sure what exactly should be scaled - like UI element sizes, maybe
//future: difference between screen coords and pixel coords. mouse input is in screen coords. text must be aware of pixel coords

//future: depth. front to back. though, it might be necessary to have a mode which doesn't have depth? if nothing is overlapping?
//nah, it's pretty always useful. otherwise we have to render around objects - like a rectangle, and then the negative of that rectangle.
//things are rarely ever tiled in that way. so always render with depth

//should the shader be in pixel or GL coordinates?
//if pixel-aware, it must be in pixel coordinates. either the computer or the GPU will convert, and it would rather be the GPU.
//for example, line drawing is pixel-aware. UI is pixel aware.
//if stretched over the screen, better if GL coordinates. for example, moving bar and the sine wave are like that. it's a relative proportion of the screen.
//any kind of composition will prevent this, unless it's relative composition. but if you can drag the boundaries, then the mouse is pixel-determined, and then it's pixel aware again? well, it doesn't have to be. just cache the relative proportion.
//however, the graph is faster with GL coordinates. because the increment can be cached.

extern float user_desired_phase_offset;

void get_vsynctimes();

single_def double system_claimed_monitor_Hz; //may not be totally accurate. for example, some monitors have fractional Hz, and this is always an integer on Windows. I don't know if errors can be larger than that.

namespace render {
single_def bool must_clear_once = false;

single_def const bool double_buffered = true; //to achieve instant swaps mid-frame, this must be true on Windows. this flag means two buffers are used, but doesn't force vsync by itself
enum {
	no_vsync, //full throttle swapping
	double_buffer_vsync, //vsync based on the standard mechanism. even in windowed mode, it's one frame behind custom vsync
	wait_for_vsync_in_thread, //wait for the vblank signal, don't try to calculate periods and phases. next frame is calculated right after the previous frame renders. unfortunately, the waiting is consistently too late - it waits for next frame's vblank, not this frame's
	separate_heartbeat,
	sync_in_render_thread
};
#if _WIN32
single_def const int sync_mode = sync_in_render_thread;
#elif __linux__
single_def const int sync_mode = sync_in_render_thread;
//on linux: sync_mode = no_vsync causes tearing, whether double_buffered = true or false
#else
single_def const int sync_mode = double_buffer_vsync; //double_buffer_vsync is default choice
#endif
//future: alt-tabbing away and back makes the music bar very consistent. why?

#if _WIN32
single_def const bool input_and_render_separate_threads = true;
#elif __linux__
single_def const bool input_and_render_separate_threads = true; //apparently, Linux cannot tolerate this. very long skips between inputs
//however, I think randomizing phase is too important. we'll accept the high latency, for now
#define LINUX_WORKAROUND_FOR_CRAPPY_NOTIFICATIONS 0 //todo: figure out why input_polling_test.cpp is fine and this program is not
#else
static_assert(0, "need to test this platform");
#endif

single_def bool busy_wait_for_exact_swap = true; //the scanline display wants the swap to be at a precise time. it doesn't care that there are no inputs being processed; it just wants an accurate scanline.
single_def bool spam_swap = false; //keep swapping constantly. for scanline displays
single_def bool triangles_active = false; //these are set by input (which handles all UI elements) and read by rendering.
single_def bool text_active = false;
single_def bool clear_each_frame = true;
single_def bool last_frame_fail = false; //if the timer overflowed last frame

//when double_buffer_vsync, fullscreen, input_and_render_separate_threads, are all true on Windows, then input events aren't received during glfwSwapBuffers().
//in practice, this means the mouse input comes in on 16 ms intervals, and occasionally 33. this creates huge jerkiness, and inputs are often skipped in a frame
//putting console output right before swapBuffer() solves it, showing what a delicate operation it is. seeing the skip is reliant on the frame rendering being too fast.

//with double buffer vsync, on windows
//windowed mode is about 2.1 frames of lag
//fullscreen mode is about 3.3 frames of lag
//with double_buffer_vsync, fullscreen, input_and_render_separate_threads, white triangle should be below the mouse if it's a two-frame delay. but it's more delay than that!

float pixel_to_window_x(int x) { return x * 2 / float(screen_w) - 1; }
float pixel_to_window_y(int y) { return y * 2 / float(screen_h) - 1; }
float pixel_to_window_x(float x) { return x * 2 / float(screen_w) - 1; } //sometimes floats are passed in, because the desired spot is the middle of a pixel.
float pixel_to_window_y(float y) { return y * 2 / float(screen_h) - 1; }
std::tuple<float, float> pixel_to_window(int x, int y) { return {x * 2 / float(screen_w) - 1, y * 2 / float(screen_h) - 1}; }
std::tuple<float, float> pixel_to_window(float x, float y) { return {x * 2 / float(screen_w) - 1, y * 2 / float(screen_h) - 1}; }

template <typename F1, typename F2>
void print_shader_error(GLuint id, GLenum e, F1 getiv, F2 getinfolog) {
	GLint error_status;
	getiv(id, e, &error_status);
	if (error_status == GL_FALSE) {
		char error_log[512];
		getinfolog(id, 512, 0, error_log);
		error(error_log);
	}
}

inline void check_program_status(GLuint id, GLenum e) {
	print_shader_error(id, e, glGetProgramiv, glGetProgramInfoLog);
}

inline void compile_shader(GLuint shader) {
	glCompileShader(shader);
	print_shader_error(shader, GL_COMPILE_STATUS, glGetShaderiv, glGetShaderInfoLog);
}

GLuint compile_shaders(const char* vertex_shader_source, const char* fragment_shader_source) {
	GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
	GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(vertex_shader, 1, &vertex_shader_source, nullptr);
	glShaderSource(fragment_shader, 1, &fragment_shader_source, nullptr);
	compile_shader(vertex_shader);
	compile_shader(fragment_shader);
	GLuint shader_program = glCreateProgram();
	glAttachShader(shader_program, vertex_shader);
	glAttachShader(shader_program, fragment_shader);
	glLinkProgram(shader_program);
	check_program_status(shader_program, GL_LINK_STATUS);
	glDetachShader(shader_program, vertex_shader);
	glDetachShader(shader_program, fragment_shader);
	glDeleteShader(vertex_shader);
	glDeleteShader(fragment_shader);
	glUseProgram(shader_program);
#if !NDEBUG
	glValidateProgram(shader_program);
	print_shader_error(shader_program, GL_VALIDATE_STATUS, glGetProgramiv, glGetProgramInfoLog);
#endif
	return shader_program;
}

union VBO_item {
	float f;
	uint32_t b;
	VBO_item(uint32_t b_) : b(b_) {}
	VBO_item(float f_) : f(f_) {}
};

void increment_z();
void reset_z();
extern float global_z_order;

template <typename index_t>
struct GL_buffer {
	std::vector<VBO_item> vertices; //format: {x, y, luminance}, repeat.
	uint index_max_size = 512;
	index_t* indices = new index_t[index_max_size];
	int index_position = index_max_size; //we push indices in reverse! this is to make z-ordering work. it's irrelevant for fonts, but we do it anyway.
	//we need to push front-to-back for early z-kill. for opaque triangles.
	//we need to push back-to-front for transparent objects, like fonts.
	uint cached_vertex_capacity = 0;
	unsigned VAO, program;
	unsigned VBO;

	GL_buffer() { vertices.reserve(256 * 3); }

	void move_and_render() {
		glUseProgram(program);
		glBindVertexArray(VAO);
		glBindBuffer(GL_ARRAY_BUFFER, VBO); //necessary
		if (vertices.capacity() > cached_vertex_capacity) {
			glBufferData(GL_ARRAY_BUFFER, vertices.capacity() * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
			cached_vertex_capacity = vertices.capacity();
		}
		glBufferSubData(GL_ARRAY_BUFFER, 0, vertices.size() * sizeof(float), vertices.data());

		unsigned GL_index_type;
		if (sizeof(index_t) == 2)
			GL_index_type = GL_UNSIGNED_SHORT;
		if (sizeof(index_t) == 4)
			GL_index_type = GL_UNSIGNED_INT;
		glDrawElements(GL_TRIANGLES, index_max_size - index_position, GL_index_type, indices + index_position);

		//pre-emptive growth. it's more efficient to grow here, when we don't need to copy over elements from old to new
		//although, that's probably better to do after the frame has finished, not in the middle of drawing.
		if (vertices.size() > cached_vertex_capacity * 3 / 4) {
			glBufferData(GL_ARRAY_BUFFER, vertices.capacity() * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
			vertices.clear();
			vertices.reserve(vertices.capacity() * 2);
		}
		else
			vertices.clear();

		index_position = index_max_size;
	};

	//future: move color out of here. and move it into the VBO.
	void move_and_render_text() {
#if !DISABLE_FONTS
		glUseProgram(program);
		extern GLuint font_texture;

		//glBindBuffer(GL_ARRAY_BUFFER, font_VBO); //necessary?
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, font_texture);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC1_COLOR, GL_ONE_MINUS_SRC1_COLOR);
		move_and_render();
		glDisable(GL_BLEND);
#endif
	};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnarrowing"

	//only by double. please don't add so many elements in a single call that you overflow 2x max size.
	void expand_indices() {
		index_t* new_indices = new index_t[index_max_size * 2];
		memcpy(new_indices + index_max_size + index_position, indices + index_position, (index_max_size - index_position) * sizeof(index_t));
		index_position += index_max_size;
		index_max_size *= 2;
		delete[] indices;
		indices = new_indices;
	}

	//Z order
	void draw_quad(uint32_t c, float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3) {
		index_t i0 = vertices.size() / 3;
		vertices.insert(vertices.end(), {x0, y0, c, x1, y1, c, x2, y2, c, x3, y3, c});
		if (index_position < 6)
			expand_indices();
		indices[--index_position] = i0;
		indices[--index_position] = i0 + 1;
		indices[--index_position] = i0 + 2;
		indices[--index_position] = i0 + 1;
		indices[--index_position] = i0 + 2;
		indices[--index_position] = i0 + 3;
	}

	//xxyy: because the x has more in common. this makes patterns clearer
	void draw_ortho(uint32_t c, float x0, float x1, float y0, float y1) {
		draw_quad(c, x0, y0, x1, y0, x0, y1, x1, y1);
	}
	void draw_ortho_pixel(uint32_t color, float x0, float x1, float y0, float y1) {
		auto [a, c] = pixel_to_window(x0, y0);
		auto [b, d] = pixel_to_window(x1, y1);
		draw_quad(color, a, c, b, c, a, d, b, d);
	}

	void draw_triangle(float x0, float y0, uint32_t c0, float x1, float y1, uint32_t c1, float x2, float y2, uint32_t c2) {
		index_t i0 = vertices.size() / 3;
		vertices.insert(vertices.end(), {x0, y0, c0, x1, y1, c1, x2, y2, c2});
		if (index_position < 3)
			expand_indices();
		indices[--index_position] = i0;
		indices[--index_position] = i0 + 1;
		indices[--index_position] = i0 + 2;
	}
	void draw_triangle(uint32_t c, float x0, float y0, float x1, float y1, float x2, float y2) {
		draw_triangle(x0, y0, c, x1, y1, c, x2, y2, c);
	}

	void draw_text(uint32_t c, float cursor_x, float cursor_y, float texture_x, float texture_y, float w, float h) {
		index_t i0 = vertices.size() / 5;
		vertices.insert(vertices.end(), {cursor_x, cursor_y, texture_x, texture_y, c, cursor_x + w, cursor_y, texture_x + w, texture_y, c, cursor_x + w, cursor_y - h, texture_x + w, texture_y + h, c, cursor_x, cursor_y - h, texture_x, texture_y + h, c});
		if (index_position < 6)
			expand_indices();
		indices[--index_position] = i0;
		indices[--index_position] = i0 + 1;
		indices[--index_position] = i0 + 2;
		indices[--index_position] = i0;
		indices[--index_position] = i0 + 2;
		indices[--index_position] = i0 + 3;
	}

	void draw_line(uint32_t c, float x1, float y1, float x2, float y2) {
		float angle = atan2((y2 - y1) * screen_h, (x2 - x1) * screen_w);
		float sina = sin(angle) / screen_w;
		float cosa = cos(angle) / screen_h;
		draw_quad(c, x1 + sina, y1 - cosa, x2 + sina, y2 - cosa, x1 - sina, y1 + cosa, x2 - sina, y2 + cosa);
	}

	void draw_line(uint32_t c, float x1, float y1, float x2, float y2, float thickness_in_pixels) {
		float angle = atan2((y2 - y1) * screen_h, (x2 - x1) * screen_w);
		float sina = thickness_in_pixels * sin(angle) / screen_w;
		float cosa = thickness_in_pixels * cos(angle) / screen_h;
		draw_quad(c, x1 + sina, y1 - cosa, x2 + sina, y2 - cosa, x1 - sina, y1 + cosa, x2 - sina, y2 + cosa);
	}
	void draw_line_pixel(uint32_t color, float x0, float y0, float x1, float y1) {
		auto [a, c] = pixel_to_window(x0, y0);
		auto [b, d] = pixel_to_window(x1, y1);
		float angle = atan2((y1 - y0), (x1 - x0));
		float sina = sin(angle) / screen_w;
		float cosa = cos(angle) / screen_h;
		draw_quad(color, a + sina, c - cosa, b + sina, d - cosa, a - sina, c + cosa, b - sina, d + cosa);
	}
	void draw_line_pixel(uint32_t color, int x0, int y0, int x1, int y1) {
		auto [a, c] = pixel_to_window(x0, y0);
		auto [b, d] = pixel_to_window(x1, y1);
		float angle = atan2((y1 - y0), (x1 - x0));
		float sina = sin(angle) / screen_w;
		float cosa = cos(angle) / screen_h;
		draw_quad(color, a + sina, c - cosa, b + sina, d - cosa, a - sina, c + cosa, b - sina, d + cosa);
	}

#pragma GCC diagnostic pop
};
extern GL_buffer<uint32_t> triangles;
extern GL_buffer<uint16_t> text_vertices;

void draw_text(std::string_view text, float x, int y, uint32_t color);
void render_loop();
} // namespace render
