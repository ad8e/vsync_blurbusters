#pragma once
#include "helper.h"
#include "renderer.h"

double GPU_swap_delay_undocumented = 0.0023;
//Intel HD 4000, 1366x768, and 1920x1080: 0.69 ms. 0.7 is too long for this GPU
//Intel Iris Xe Graphics G7 80EUs, 2560x1440: about 2.3 ms?

double render_overrun_buffer_room = 0.0008; //you wait, then render, then wait, then swap. this specifies how much extra time the render sometimes takes, compared to frame_time_smoothed.
//there exists a constant term, at least. because on my Intel HD 4000, rendering nearly nothing still results in overruns.

//the GPU timestamps are async: you might not be able to retrieve them until a long time later. this causes index_lagging_GPU_time_to_retrieve
//OpenGL Insights reports that availability might be 5 frames late. P500: "Depending on the frame rate and CPU overhead, the number of rendering frames the GPU is running behind the CPU can be observed to be as high as five frames, depending on the GPUs performance and driver settings"
//so we'll use a 16 frame circular buffer. 8 frames double-buffered. 2 measurements per frame.
//we replace old entries without checking if they've really been finished; we just assume everything is finished after 5 frames. 8 > 5 provides buffer room.
//actually, it can be even longer than that. in the sine wave animation, when frames are being spammed out with no regards to vsync, I see a few OpenGL errors with glGetQueryObjectiv. this is with an 8 frame buffer. so I doubled them again.
constexpr uint frame_time_buffer_size = 128;

//process: during rendering, render start time written to render. then converted to render finish time. then start time written to swap. then converted to swap finish time.
//the input index is mod 2: 0 for start, 1 for end.
//the output index is [0, 4 * frame_time_buffer_size). mod 4: 0 is render, 1 is swap, 2 is both.
//we choose mod 4, even though there are three values, so that it overflows a 32-bit integer without issue
array<uint, frame_time_buffer_size> query_to_frame_time_map;

array<GLuint, frame_time_buffer_size> query_circular_buffer;
array<GLuint64, frame_time_buffer_size> frame_time_history;
uint index_next_query_available = 0;
uint index_lagging_GPU_time_to_retrieve = 0; //should be less than index_next_query_available
uint index_next_frame_time_available = 0;
double frame_time_smoothed = 0.002; //in seconds. currently not used. just set equal to frame_time_single
double frame_time_single = 0.002; //the most recently stored frame time, in seconds
double swap_time = 0.001;
double render_time = 0.001;

uint64_t frames_since_discarded = 64;
void smooth_frame_time(double new_time, double& old_time) {
	//note: if the screen is minimized, its timing is still counted! you get a super long frame time. which then can fuck up the frame time estimates.
	//there's no way to distinguish between super-slow frames (for example, while waiting for a Fourier transform calculation) vs just being minimized. we can't throw away the information even though it looks bad, because it might not be bad, and then it would be important.
	//thus, garbage goes into the lowpass estimation algorithm. which is not designed to handle it.
	auto cap_frame_time = 2.0 / system_claimed_monitor_Hz; //2 frames max. we can impose this low cap because all high frame times cause equivalent behavior - the renderer simply stops trying to measure things
	if (new_time < cap_frame_time) {
		++frames_since_discarded;
	}
	else {
		if (frames_since_discarded >= 64 && new_time > 2 * old_time) {
			frames_since_discarded = 0;
			return; //don't change the frame_time_smoothed. this is an exceptional frame. we can do this only once in a while.
		}
		else {
			frames_since_discarded = 0;
			new_time = cap_frame_time;
		}
	}
	//cap. this minimizes distortion; if frame times are so long, the algorithm isn't doing anything productive anyway.
	//when a too-long frame appears, the estimated frame time leaps. this causes the render loop to think it will take too long to sync to the vblank. hence it simply spam-renders.
	//spam-rendering has the positive (coincidental) consequence that it feeds more times into the time-estimation mechanism, flushing away the incorrect time judgment. so it's a very self-correcting behavior.
	//we can accelerate this by discarding high jumps, as long as it's only one in a long while.

	//in the future, we might use a distribution approach. it will estimate percentiles. in our current approach, we still have to worry about variance, so it's not that great.
	//that is, current stability might predict future stability. maybe.
	//we want a nonlinear filter. if the frame time jumps up, we should jump up quickly. if the frame time drops, we should drop slowly. because there's a nonlinearity - missing the vsync is very bad, but being early is fine.
	//thus, square the input times - this applies an upward nonlinearity.
	//then we apply a linear filter to the squares.
	//squaring creates a restriction: the filter coefficients must all be positive. taking the square root of a negative value makes no sense, and a linear filter can create negative values.
	//so the clever phase-locking filter of the audio thread is not allowed.

	//L2, but with exponential weight. this represents the recency bias more accurately - more recent information is better.
	//except we don't have the frame time...so it's not that great. but oh well.

	const double decay_time_Hz = 5;
	double remainder_exponential = std::exp(-(new_time + 0.004) * decay_time_Hz); //we add 4 ms of power because frame_time_single is not the time between frames - it's the time to render a single frame, without the waiting. but it doesn't make sense for it to weigh 0.
	old_time = old_time * remainder_exponential + new_time * (1 - remainder_exponential);
	//outc("frame time", frame_time_smoothed);
}

//apparently, you can reuse queries. no need to delete them after you use them once.
//which is good, because both deleting and creating queries is expensive

void GPU_timestamp_retrieve() {
	unsigned output_muxed = query_to_frame_time_map[index_lagging_GPU_time_to_retrieve % frame_time_buffer_size];
	unsigned output_index = output_muxed / 4;
	unsigned output_which = output_muxed % 4;
	//unsigned input_index = index_lagging_GPU_time_to_retrieve / 2;
	unsigned input_which = index_lagging_GPU_time_to_retrieve % 2;
	//outc("retrieving", output_index, output_which, index_lagging_GPU_time_to_retrieve, input_which);
	if (input_which == 0) {
		//start timepoint
		glGetQueryObjectui64v(query_circular_buffer[index_lagging_GPU_time_to_retrieve % frame_time_buffer_size], GL_QUERY_RESULT, &frame_time_history[output_index % frame_time_buffer_size]);
	}
	else {
		//end timepoint
		uint64_t timestamp;
		//for (int i : zero_to(1000)) //this checks how expensive the query retrieval is, for the actual result. with 1000 times and Query deletion off, it starts to jitter. but at least it stays at 1 bar, so it's cheaper than the availability check
		glGetQueryObjectui64v(query_circular_buffer[index_lagging_GPU_time_to_retrieve % frame_time_buffer_size], GL_QUERY_RESULT, &timestamp);
		double new_time = (timestamp - frame_time_history[output_index % frame_time_buffer_size]) / pow(10, 9);
		outc("new time", output_which, new_time, frame_time_single);
		switch (output_which) {
		case 0:
			render_time = new_time;
			frame_time_single = render_time + swap_time;
			break;
		case 1:
			swap_time = new_time;
			frame_time_single = render_time + swap_time;
			break;
		case 2:
			render_time *= new_time / frame_time_single; //renormalize
			swap_time *= new_time / frame_time_single; //renormalize
			frame_time_single = new_time;
			break;
		}
		frame_time_smoothed = frame_time_single; //for now. todo
	}
	//update_frame_time(frame_time);

	++index_lagging_GPU_time_to_retrieve;
}

//which: 0 is render only, 1 is swap only, 2 is both. 3 is input, which technically can be deduced from (index_next_query_available % 2), and hence is redundant
//only matters for outputs; inputs don't need to store.
void GPU_timestamp_send(uint which = 3) {
	glQueryCounter(query_circular_buffer[index_next_query_available % frame_time_buffer_size], GL_TIMESTAMP);
	query_to_frame_time_map[index_next_query_available % frame_time_buffer_size] = index_next_frame_time_available * 4 + which;
	if (which != 3) {
		++index_next_frame_time_available;
		glFlush();
		//this is necessary! otherwise, it waits for next frame and then reports a 16.6 ms frame time.
		//checking the 10000 glFlush benchmark above, it appears glFlush is basically free. so this is no problem.
		//send_junk_floats_to_GPU_to_wake_it_up(); //this is insufficient. just buffering data does not cause the timer to activate
	}
	++index_next_query_available;
}