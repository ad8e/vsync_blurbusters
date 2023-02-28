#pragma once
#include "helper.h"
#include "timing.h"

namespace render {
single_def int32_t screen_w = 0, screen_h = 0;

//on click, extrapolate the mouse to the current timepoint, and set the extrapolated position in mouse_x.
single_def double mouse_x_raw[2] = {0, 0}; //[0] is the more recent timepoint
single_def double mouse_y_raw[2] = {0, 0};
single_def uint64_t mouse_timepoint[2] = {0, 0};
double ticks_between_mouse_events = ticks_per_sec / 1000.0; //mouse sampling frequency. only has to be approximately correct. initial assumption is 1 ms. used to determine if the time after the last mouse input is because the mouse is still, or if it's just waiting for the next input

//the mouse position is derived from the raw mouse position by extrapolation. it must be set before usage.
//for clicks and renders, these are cached automatically
single_def double mouse_x = 0, mouse_y = 0;
single_def uint64_t click_time; //if you clicked, when it is. currently only used by reaction test. in the future, animations may also care?
single_def int mouse_button; //if you clicked, which button it is
bool middle_mouse_held_down = false;

//drag positions may originate from either clicks or raw positions. so they must be doubles.
single_def double previous_mouse_drag_x, previous_mouse_drag_y;

extern GLuint font_texture;
extern GLint pixel_to_window_uniform;
} // namespace render

single_def void (*active_key_callback)(int key, int scancode, int action, int mods); //"Default mapping for games typically want to use scancode to ensure a known physical location, whereas default mapping for interfaces which have a connection to the actual letter (e.g application shortcuts) want to use keycodes."
single_def void (*active_char_callback)(unsigned int codepoint);

single_def void (*active_drag_and_drop_callback)(string_view path);

struct ui_element;
single_def ui_element* target_being_dragged = nullptr;
