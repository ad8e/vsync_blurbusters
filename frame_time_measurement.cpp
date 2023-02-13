#pragma once
#include "helper.h"
#include "renderer.h"

float GPU_swap_delay_in_ms = 0.69; //tailored for Intel HD 4000. no idea about other GPUs. 0.7 is too long for this GPU
//there is no consistency.
//new graphics card: Intel Iris Xe Graphics G7 80EUs
//the delay is determined by the frequency of the GPU, and varies by 40% of the frame. you can see the GPU's frequency step around as its frequency changes.
//to check this, I installed tlp, and changed these configurations, which forced the GPU to its lowest frequency, and stabilized the tearline at 2/5 down the screen:
//INTEL_GPU_MAX_FREQ_ON_AC=100
//INTEL_GPU_BOOST_FREQ_ON_AC=100
//INTEL_GPU_MIN_FREQ_ON_AC=100

float render_overrun_in_ms = 0.8; //you wait, then render, then wait, then swap. this specifies how much extra time the render sometimes takes, compared to frame_time.
//there exists a constant term, at least. because on my Intel HD 4000, rendering nearly nothing still results in overruns.

//the GPU timestamps are async: you might not be able to retrieve them until a long time later. this causes index_lagging_GPU_time_to_retrieve
//OpenGL Insights reports that availability might be 5 frames late. P500: "Depending on the frame rate and CPU overhead, the number of rendering frames the GPU is running behind the CPU can be observed to be as high as five frames, depending on the GPUs performance and driver settings"
//so we'll use a 16 frame circular buffer. 8 frames double-buffered. 2 measurements per frame.
//we replace old entries without checking if they've really been finished; we just assume everything is finished after 5 frames. 8 > 5 provides buffer room.
//actually, it can be even longer than that. in the sine wave animation, when frames are being spammed out with no regards to vsync, I see a few OpenGL errors with glGetQueryObjectiv. this is with an 8 frame buffer. so I doubled them again.
constexpr uint frame_time_buffer_size = 32;
constexpr uint GPU_timestamp_buffer_size = frame_time_buffer_size / 2;

//we don't use a circular buffer because we are grabbing memory from strange locations, and the notation doesn't fit very well
array<GLuint, frame_time_buffer_size> query_double_buffer = {}; //initialize to 0: glDeleteQueries ignores zeros, and in the first few loops, we delete queries that haven't been created yet.
uint index_next_query_available = 0;
uint index_lagging_GPU_time_to_retrieve = 0; //should be less than index_next_query_available
array<GLuint64, frame_time_buffer_size / 2> GPU_start_times;
double frame_time = 0; //in seconds.
double single_frame_time = 0; //the most recently stored frame time, in seconds

const uint generate_queries_per_batch = 2; //power of 2, <= frame_time_buffer_size / 2. (higher batches will decrease the effective buffer size and cause some frames to take longer, in a spiky fashion. higher throughput, more jitter)

//single_frame_time is an (unstated) argument. it's pulled as a global.
void update_frame_time() {
	//we want a nonlinear filter. if the frame time jumps up, we should jump up quickly. if the frame time drops, we should drop slowly. because there's a nonlinearity - missing the vsync is very bad, but being early is fine.
	//thus, square the input times - this applies an upward nonlinearity.
	//then we apply a linear filter to the squares.
	//squaring creates a restriction: the filter coefficients must all be positive. taking the square root of a negative value makes no sense, and a linear filter can create negative values.
	//so the clever phase-locking filter of the audio thread is not allowed.

	//nah, we have an even better mechanism. it's like L2, but with exponential weight.
	//this represents the recency bias more accurately - more recent information is better.

	const double decay_time_Hz = 5;
	double remainder_exponential = std::exp(-(single_frame_time + 0.004) * decay_time_Hz); //we subtract add 4 ms because single_frame_time is not the time between frames - it's the time to render a single frame. that means it can be tiny, and hence be given no weight.
	frame_time = frame_time * remainder_exponential + single_frame_time * (1 - remainder_exponential);
	//outc("frame time", frame_time);
}

void generate_OpenGL_Queries() {
	//glGenQueries() is effectively instant CPU-wise, but it's heavy on the GPU
	//performance: my Intel GPU swap line quantum is 16 px, on a 1920/1080 screen. so each quantum is 1/67.5 of the screen time, 0.2 ms.
	//then, generating 64 queries takes 3 quanta. thus, generating multiple queries simultaneously is not free. I don't know what the constant factor is (for generating 1 query), but I think max latency is a bigger problem than throughput as long as the frame time is less than the screen refresh rate. so we'll generate as few queries per batch as possible
	//generating queries in batches causes gaps in the sine wave animation. changing the batch size causes the gaps to change.
	if (index_next_query_available % generate_queries_per_batch == 0 || generate_queries_per_batch == 2) {
		//glDeleteQueries(generate_queries_per_batch, &query_double_buffer[index_next_query_available % frame_time_buffer_size]); //deletion is very expensive! it's making the sine wave animation jitter like crazy. but if you don't delete old queries, the GPU pauses for a while at program shutdown while it cleans up the Queries you didn't delete
		//we can delete either here, or after they are finished retrieving. I think it's better to delete them here, so there is exactly one deletion per frame, rather than after retrieval, when there might be two deletions per frame, and sometimes zero. deleting them here does cause more Queries to be alive simultaneously
		//if we keep 65536 Queries alive by not deleting anything and having a 65536-size buffer, it does cause longer horizontal bars over time. that means as queries accumulate, they are causing problems.
		//that means having stale Queries causes performance issues. so we'll delete them as they finish, rather than here
		glGenQueries(generate_queries_per_batch, &query_double_buffer[index_next_query_available % frame_time_buffer_size]);
	}
}

uint64_t frames_since_discarded = 64;

void finished_time_retrieval(uint64_t GPU_finish_time) {
	glDeleteQueries(generate_queries_per_batch, &query_double_buffer[(index_lagging_GPU_time_to_retrieve - 1) % frame_time_buffer_size]); //either this delete is active, or the one in generate_OpenGL_Queries() is active, but not both. check its compatriot in generate_OpenGL_Queries() for documentation
	unsigned index = index_lagging_GPU_time_to_retrieve / 2;
	uint64_t time_taken_in_GPU_ticks = GPU_finish_time - GPU_start_times[index % GPU_timestamp_buffer_size];
	single_frame_time = time_taken_in_GPU_ticks / std::pow(10, 9);

	//note: if the screen is minimized, its timing is still counted! you get a super long frame time. which then can fuck up the frame time estimates.
	//there's no way to distinguish between super-slow frames (for example, while waiting for a Fourier transform calculation) vs just being minimized. we can't throw away the information even though it looks bad, because it might not be bad, and then it would be important.
	//thus, garbage goes into the lowpass estimation algorithm. which is not designed to handle it.
	auto cap_frame_time = 2.0 / system_claimed_monitor_Hz; //2 frames max. we can impose this low cap because all high frame times cause equivalent behavior - the renderer simply stops trying to measure things
	if (single_frame_time < cap_frame_time) {
		++frames_since_discarded;
	}
	else {
		if (frames_since_discarded >= 64) {
			frames_since_discarded = 0;
			return; //don't change the frame_time. this is an exceptional frame. we can do this only once in a while.
		}
		else {
			frames_since_discarded = 0;
			single_frame_time = cap_frame_time;
		}
	}
	//cap. this minimizes distortion; if frame times are so long, the algorithm isn't doing anything productive anyway.
	//when a too-long frame appears, the estimated frame time leaps. this causes the render loop to think it will take too long to sync to the vblank. hence it simply spam-renders.
	//spam-rendering has the positive (coincidental) consequence that it feeds more times into the time-estimation mechanism, flushing away the incorrect time judgment. so it's a very self-correcting behavior.
	//actually, we can discard high jumps, as long as it's only one in a long while.

	//in the future, we might use a distribution approach. it will estimate percentiles. in our current approach, we still have to worry about variance, so it's not that great.
	//that is, current stability might predict future stability. maybe.

	update_frame_time();
}
