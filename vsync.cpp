#pragma once
/*
goal: find the period and phase of the vblank signal.
data: reported vblank times. this data is from a thread that waits, is woken up by the system after a vblank, records the current time, then goes back to waiting. it cannot be woken up twice by the same vblank.

Obstacles:
these timepoints will be later than the actual vblank times by random amounts of time.
timepoints can skip over entire vblanks.
clock skew means timing is inconsistent (but still monotonic)

we assume that that timepoint delays are uncorrelated.
knowledge: the average timepoint delay is less than 0.1 ms. that means most timepoints are good

if the global minimum is far away from the local minimum, we simply reset: since most timepoints are good, we know we'll get good data soon anyway. no point in doing an expensive global search.

method: find a period/phase pair, which generates a set of vblank times, such that each reported timepoint is as close to its vblank time as small as possible. (minimize the sum of such distances)
definition: error := sum of such distances
key insight: at least two of the reported timepoints will land exactly on the vblank times.
proof:
1. if zero timepoints land on vblank times, then move the phase later, keeping the period fixed. this lowers the error.
2. if one timepoint lands on a vblank time, then keeping the phase fixed to that timepoint, we can increase or decrease the period in a neighborhood without intersecting any further timepoints. in that neighborhood, the error is a linear function of the period. hence, we can shift the period until another timepoint lands on a vblank time.
	specifically, (change in error) = (sum of frame differences to the timepoint) * (change in period). a frame difference is the difference in the frame number - so if timepoint A is 5 frames before the given timepoint, its frame difference is -5. then we sum these.
	(there's a special case where the linear function is zero, corresponding to the sum of frame differences being zero. in that case, we can still maintain the invariant, although it is no longer required)
	(there's also a problem where when the vblank can approach the second timepoint but not reach it, or else the timepoint will be after a new vblank time, and hence share a single frame with another timepoint. this would violate the way we gathered these timepoints, which cannot associate two timepoints to one vblank. we will ignore this problem and assume that each timepoint is fixed to its original vblank, even if its reported time is after the next vblank. this is for a very good reason, which will be described later: clock skew.)
the two special timepoints given by the theorem will be called "pivots".


algorithm strategy: we simply assume we know which frame each timepoint belongs to. we do this by guessing.
visualization: let the x-axis be the frame #.
let the y-axis be (distance to frame's vblank)
then, each timepoint can be graphed on this 2D plot, as (frame #, distance to vblank)
the two pivots will lie on the x-axis, having zero distance to their vblanks.
these two pivots will create a period/phase pair, if we know how many frames apart they are. which we do, since we are guessing frames.
every other timepoint must lie after its frame's vblank, and hence has a positive y value.
there is a lower convex hull: a subset of points such that for any two successive points on the lower convex hull, the line through them goes below all the timepoints in the plot.
we can draw a period/phase line, which goes through all its vblank points. it satisfies two properties:
	all timepoints must come after their vblanks
	at least two timepoints must land on the period/phase line
that means it must be one of the lines on this lower convex hull, since that is the definition of lines on the lower convex hull.
the line on the lower convex hull that crosses the middle (where middle = average frame #) is the one that specifies the optimal period/phase pair. any other line on the lower convex hull can be pivoted around one of its points, reducing error each time, until it reaches this middle line.

detailed strategy: we maintain a cache of points on the convex hull. this is sparse and easy to keep updated
when a new timepoint is added, the middle timepoint may no longer be between the two pivot points. if so, we find two points on the convex hull that do surround it.
since we always know the two pivot points, we know the period/phase pair, and we are done.

these are thresholds. if any of them is violated, we panic and reset the algorithm's state:
	error < 1/4 (average distance of timepoints to their frame baselines must be at most 1/4 the period)
	multi-frame timepoints < 1/3 (# of timepoints with multiple frames, as proportion of total timepoints)
	zero-frame timepoints disallowed (two timepoints in same frame)
consequences:
	error forces the period to n/2, where n is a positive integer. (that is, true period = n/2 * algorithm-reported period)
	multi-frame timepoint restriction forces the period to [0, 4/3]
	zero-frame forces the period to [1-e, infinity)
the only period that satisfies all three bounds is 1 - which means the true period and algorithm-reported period will be equal.

our current algorithm is flexible enough to tolerate clock skew. the distance of a timepoint to its frame baseline can be greater than the period. theoretically, this means the timepoint overlaps with the next frame, but we don't prohibit this. this must be allowed to prevent clock skew from creating logical violations!
	in turn, it allows us to keep the hard prohibition of zero-frame timepoints. each timepoint is associated to its true vblank, even if its reported time is after the next vblank.
to trigger clock skew problems with a rigid algorithm requires special circumstances: a pair of timepoints land close together, and another pair of timepoints lands close together. these two pairs clamp the period/phase to a fixed value. then, clock skew makes an intermediate timepoint contradict the claimed period/phase.
	that requires two timepoints to be delayed. which does happen, but not commonly.

asymptotes:
phase error = 1/size
period error = 1/size^2
runtime average time = log(size), maybe.
runtime max time = size. the convex hull would need to be fully browsed. which is possible: if time is accelerating, then every point is on the convex hull.
*/

/*
below is documentation for myself, not for others.

etc/vsync slow.cpp contains some logic and math, but I think most of it is obsolete now

future: implement frame shift

future: implement error adjustment, either average, truncated average or order statistic
	conclusions from testing: order statistic is probably better. however, I see no good way to implement order statistics without being O(n). even a tree won't work, because the errors change as the pivots change
	average should be smoothed. it would add in excess error if it was subtracted directly, as it varies too much.

future: check the error of the last 4 or so points only. if they are bad, then reset. this will detect monitor changes? nah, no point, just call the reset manually.

testing method: calculate the estimated offset, by running multiple finders together. one at high res, one at low res. then I'll see some asymptotes, and then I can plot them.
I did this by copying the vsync finder into a new scope, vf2. one vsync finder has 512 timepoints, the other has few (4, 8, or 16). I feed the same timepoint to both finders. then I compare the difference in phase. I use the long finder's average error, as it should be more consistent.
result: 4 timepoints, shift ~ .62 average error
8 timepoints, shift ~ .40 average error
4 timepoints, shift ~ .28 average error
however, these numbers are wrong. what really happened is that I ran three vsyncs simultaneously, and their existence caused contention in the vblank waiters. this increased the error by quite a lot, and increased the decay by a bit less.
doing things in the background, such as working in firefox, or running other vblank waiters, increases error. when I stopped all activity, average error dropped from 0.09 ms to 0.04 ms. average distance dropped from 92 to 62 ticks.
the error is quite wobbly for the short finders. it would be dangerous to subtract it, as my guess is that it would make the line less biased, but increase noise. (but, bias only exists if you can detect the underlying, otherwise it's no different from noise)
in the future, if I wanted to do this, I should probably run all three finders in one application, by copying it more than once. and, I should bench against the scanline algorithm.

the 512 finder is not exactly the ground truth. it is actually later, which decreases the diffs the same amount for each finder.
the 512's error is also a distribution around the true error. since we're dividing by a distribution, this will increase the diffs, multiplying the same amount for each finder.


this is much faster than the old dynamic programming, which occasionally took 1 ms! global searches are very slow.

I came back.
I want to add a frame shift. it will bump existing timepoints either up or down a frame, acknowledging that our previous frame guess may be wrong. it should test local changes - so only one timepoint is bumped.
this will help with recovery from errors.
the pivoting happens before the frame shift, because pivoting maintains the invariants. and all operations happen after the timepoint is added. (we want to report on fresh data, not stale data. it's data -> processing -> report, not processing -> data -> report. if you found a frame shift with old data, you should have reported it then.)

shift: if something moves downward, it probably becomes a new pivot, but not necessarily. would screw our convex hull
	it can only be moved downward if its frame was already at least 2 after the previous frame. because we're only testing local changes.
		so: its error would need to be quite high, and it needs to be a multiframe timepoint.
		the metric to determine success is the average error. so, we would pick the point which would move the period/phase (and hence the error) the least. how to calculate?
		it creates a fixed benefit, then varying downside
the only points that make sense to move upward are the pivots. if the pivot is exactly in the middle, then maybe 2 points are worth checking out. again, it can only be moved upward if its frame is already at least 2 after the previous frame.
	it creates a fixed downside, then a varying benefit.
how would we maintain our convex hull invariant?
	actually, not hard: if you're moving a pivot point up, then the points between that pivot point and the next convex hull point inclusive will now point to the previous pivot point. as long as it ceases to be a convex hull point
	if you're moving a point down, then first check if it becomes a convex hull point. if not, you're done. if it does, then first, move forward until two points on the convex hull draw a line that is below the moved point. let A be the first of those two points. then for (moved point, A], you must check if moved point is a better than its existing convex_at()
		and, check if it becomes a pivot point
	if it moves up or down, don't just assume it gains/loses the obvious convex hull/pivot point status. you still have to check.
	and, you have to run find_convex_line_backwards_from() on the moved point itself, too.
*/

#include "console.h"
#include "div_floor.h"
#include "timing.h"
#include <algorithm> //nth_element
#include <array>
#include <atomic>
#include <cmath>

#define phase_error_adjustment_order_statistic false
//goal: reach the accurate phase error, and also reduce wobble.
//turns out adjusting for phase error with an order statistic actually degrades wobble.
//that means, we need a lowpass filter on the phase error.
//it also means, we don't have to re-calculate order statistics every frame. we can do some cutoff action.
//if the value is above the current lowpass, it goes up by a constant fixed amount. this prevents runaway frames from causing issues.
//if the value is below the current lowpass, it gets averaged in.
//it's a nonlinear filter just like our order statistics

namespace vf {
std::atomic_uint64_t vblank_phase_atomic = 0; //can't use atomic frame_clock::time_point because compiler complains that it's not trivially copyable
//not sure if it must be initialized with 0, to prevent loading an undefined value. I am not familiar with the flow here
//it must be a uint64_t, not a double, because this is a circular clock. but the period can be a double, which marginally improves rounding accuracy.
std::atomic<double> vblank_period_atomic = ticks_per_sec / 60.0;

//how many timepoints to store in the circular buffer
constexpr uint max_size = 32; //4 or more. power of 2. (if =2, you only have 1 point when transitioning to a new value, so the pivot fails)
//256-sized finder takes 0.004 ms when calm. occasional spikes upward, up to 0.2 ms.
//the finder should take at most half the time it creates through improved accuracy. it's on a different thread, but we should still be nice with CPU.
//the error in the wakeup is 0.09 ms / size(). at 16 timepoints, the error in prediction is already reduced to the time spent
//there's also a consideration: the fewer points there are, the later it will be. a consistent bias that is hard to adjust for.
//at 32 points, it takes 0.002 ms when calm. occasional spikes upward, up to 0.015 ms. expected error is 0.003 ms, which is 0.2 frames at 1080.

namespace circular {
uint64_t timepoints[max_size]; //circular buffer
unsigned frame_of[max_size] = {}; //stores our guesses on which frame each timepoint belongs to. initialize to zero to avoid UB, since when adding the first point, its frame is 1 + previous frame.
//future: perhaps allow multiple timepoints in one frame, if we are to use D3DKMTGetScanLine.
//we disallow it for now. there is only one check to take care of, in new_value(). in the future, if we operate frame shifts, we might have more checks. though, that would probably require a completely different algorithm.
//for example, D3DKMTGetScanLine probably wants you to do a least-squares optimization, not this pivoting
bool multiframe[max_size] = {}; //whether this timepoint is 2 frames or more after its previous timepoint. maybe it's not efficient to store bools, but it's semantically a bool

//invariant: if you take the line given by a point and the convex hull point before it, then every previous point in the circular buffer lies above that line.
//this array is characterized by that invariant: for each input position, this contains the position of the convex hull point before it.
unsigned previous_point_on_convex_hull[max_size] = {};

//for each point on the convex hull, this stores the next point. for points off the convex hull, this contains junk.
//thus, next_point_on_convex_hull only contains a valid value if the input came from previous_point_on_convex_hull, and hence is a convex hull point. i.e. next(previous(x)) is valid, but previous(next(x)) is not valid.
unsigned next_point_on_convex_hull[max_size] = {};
} // namespace circular
uint index_end = 0; //the elements inside the circular buffer are at [index_begin % max_size, index_end % max_size). these are floating indices
uint index_begin = 0;
uint middle_pivot = 0; //lies in [midpoint, index_end). the midpoint has the average frame number.
//the two pivots of the line are convex_at(middle_pivot) and middle_pivot. so middle_pivot is actually the right pivot.
//pivot[0] < midpoint, pivot[1] >= midpoint
//occasionally, if middle_pivot lies exactly on the midpoint, then it's unclear whether the line should aim at the pivot before or the pivot after
//if this happens (which is rare, since max_size is even), then we'll do an "average of two pivots" special case when calculating the periods

uint sum_of_all_frames = 0; //we use this to find the midpoint timepoint, by taking an average
uint64_t sum_of_all_timepoints = 0; //we use this to find the error (timepoints minus frame baseline timepoints)
uint number_of_multiframes = 0; //each timepoint counts only once, no matter how many frames it skips. this best reflects its power - single exceptional jumps should only count as one, and if there are many large jumps, it doesn't matter whether you count them as 1 or many, they will cause a reset either way.

uint64_t period_numerator, period_denominator; //find_period_ratio() calculates these. they're kept between frames (but become stale until find_period_ratio() is run again)
//period ~ period_numerator/period_denominator. we store it in fractional form so we can do integer arithmetic without rounding.

uint64_t& timepoint_at(uint x) { return circular::timepoints[x % max_size]; } //modulo operation is automatically converted to & (max_size - 1)
uint& frame_at(uint x) { return circular::frame_of[x % max_size]; }
bool& multiframe_at(uint x) { return circular::multiframe[x % max_size]; }
uint& convex_at(uint x) { return circular::previous_point_on_convex_hull[x % max_size]; }
uint& convex_next(uint x) { return circular::next_point_on_convex_hull[x % max_size]; }
uint elements() { return index_end - index_begin; }

//return (t0 - t_base) / (d0 - d_base) <= (t1 - t_base) / (d1 - d_base)
bool ratio_lteq(uint64_t t0, uint64_t t1, uint64_t t_base, uint d0, uint d1, uint d_base) {
	//return n0 / f0 <= n1 / f1;
	int64_t n0 = t0 - t_base;
	int64_t n1 = t1 - t_base;
	int f0 = d0 - d_base;
	int f1 = d1 - d_base;
	return int64_t(n0 * f1 - n1 * f0) <= 0;
}

//<=. only used to verify correctness (so you can ignore this)
bool period_index_lteq(uint i0, uint i1, uint index_base) {
	//return n0 / f0 <= n1 / f1;
	int64_t n0 = timepoint_at(i0) - timepoint_at(index_base);
	int64_t n1 = timepoint_at(i1) - timepoint_at(index_base);
	int f0 = frame_at(i0) - frame_at(index_base);
	int f1 = frame_at(i1) - frame_at(index_base);
	//outc(n0, n1, f0, f1);
	return int64_t(n0 * f1 - n1 * f0) <= 0;
}

bool before(uint a, uint b) {
	return (int)(a - b) < 0;
}

//checks that the cached invariants are correct.
void reference_verify_correctness() {
	uint frame_sum = 0;
	uint64_t timepoint_sum = 0;
	uint multiframes = 0;
	for (uint x = index_begin; x != index_end; ++x) {
		frame_sum += frame_at(x);
		timepoint_sum += timepoint_at(x);
		multiframes += multiframe_at(x);
	}
	check(frame_sum == sum_of_all_frames, frame_sum, sum_of_all_frames);
	check(timepoint_sum == sum_of_all_timepoints, "timepoint mismatch", timepoint_sum, sum_of_all_timepoints);
	check(multiframes == number_of_multiframes, "multiframe mismatch", multiframes, number_of_multiframes);
	if (middle_pivot != index_end - 1) //most recent element has no convex_next possible
		check(convex_at(convex_next(middle_pivot)) == middle_pivot);
	uint pivot[2] = {convex_at(middle_pivot), middle_pivot};
	check(before(frame_at(pivot[0]) * elements(), sum_of_all_frames)); //first pivot is before the midpoint
	check(!before(frame_at(pivot[1]) * elements(), sum_of_all_frames)); //second pivot is after the midpoint
	for (uint index = index_begin; index < index_end; ++index) {
		if (before(sum_of_all_frames, frame_at(index) * elements())) //divide points into before the midpoint and after the midpoint
			check(period_index_lteq(pivot[1], index, pivot[0])); //points after the midpoint give a period at least as long as the second pivot. this means they're above the line.
		else
			check(period_index_lteq(index, pivot[0], pivot[1])); //points before the midpoint give a period at least as short as the first pivot. this means they're above the line.
	}
}
#define debug_outc_vsync(...) outc(__VA_ARGS__)
//#define debug_outc_vsync(...) ;

//goal: set convex_at(position)
//summary: check points of the convex hull, moving backward. each pair of convex hull points gives a line, where both points of the line are behind the parameter position.
//if the line is below the parameter position, that's success. every line behind will also be below the parameter position. convex_at(position) = second point of the line
//if the line is above the parameter position, connect the parameter position with the first point of the line, and continue searching
void find_convex_line_backwards_from(uint position) {
	convex_at(position) = position - 1;
	for (uint hull_iterator = position - 1;; hull_iterator = convex_at(hull_iterator)) {
		//the line to test is (convex_at(hull_iterator), hull_iterator).
		//if convex_at(hull_iterator) fell off the back end, we must recalculate it to be able to test the line
		if (before(convex_at(hull_iterator), index_begin)) {
			if (hull_iterator == index_begin) {
				convex_at(position) = index_begin;
				return;
			}
			else
				find_convex_line_backwards_from(hull_iterator);
		}

		uint hull_point_before = convex_at(hull_iterator);

		//checks if the implied period between position and hull_point_before is smaller than the implied period between hull_iterator and hull_point_before.
		//if true, then position is below the line.
		//<= is better than <. faster bailout.
		//if it's <, then if we have equally spaced timepoints, then every single convex_at() points at the rearmost element.
		//then, when it expires, they all point to an invalid element. so you trace all the way back, then trace all the way forward.
		//if it's <=, then every convex_at() points to the element just behind. you bail out immediately.
		bool point_below_line = ratio_lteq(timepoint_at(position), timepoint_at(hull_iterator), timepoint_at(hull_point_before), frame_at(position), frame_at(hull_iterator), frame_at(hull_point_before));
		if (point_below_line)
			convex_at(position) = hull_point_before;
		else
			return;
	}
}

//note it's unsigned 64-bit only. don't pass it signed things!
//0.5 rounds down. (? looks to me like it rounds up? why did I write that it rounds down?)
uint64_t rounded_divide(uint64_t n, uint64_t d) {
	return (n + d / 2) / d;
}

void find_period_ratio() {
	//the period is in an integer ratio. we don't want to divide the ratio yet, because that would introduce a rounding inaccuracy.
	//hence, we store the numerator and denominator.

	uint multiple_at_this_frame = frame_at(middle_pivot) * elements();

	//it's before the midpoint. moving forward once might not fix it completely. we repeat it until it's at the midpoint or past it
	while (before(multiple_at_this_frame, sum_of_all_frames)) {
		middle_pivot = convex_next(middle_pivot);
		multiple_at_this_frame = frame_at(middle_pivot) * elements();
	}

	if (multiple_at_this_frame == sum_of_all_frames) {
		//it's exactly at the midpoint. we should take an average of before and after
		//t0/f0 + t1/f1 = (t0f1 + t1f0)/(f0f1)
		//this improves integer division accuracy, but beware that it might cause overflow
		uint pivot_before = convex_at(middle_pivot);
		uint pivot_after = convex_next(middle_pivot);
		uint64_t t0 = timepoint_at(middle_pivot) - timepoint_at(pivot_before);
		uint64_t t1 = timepoint_at(pivot_after) - timepoint_at(middle_pivot);
		uint64_t f0 = frame_at(middle_pivot) - frame_at(pivot_before);
		uint64_t f1 = frame_at(pivot_after) - frame_at(middle_pivot);
		period_numerator = t0 * f1 + t1 * f0;
		period_denominator = f0 * f1 * 2;
	}
	else {
		uint pivot_before = convex_at(middle_pivot);
		uint64_t t0 = timepoint_at(middle_pivot) - timepoint_at(pivot_before);
		uint64_t f0 = frame_at(middle_pivot) - frame_at(pivot_before);

		period_numerator = t0;
		period_denominator = f0;
	}
	check(period_denominator != 0);
}

void set_period_phase();

double calc_error_in_shitty_way() { //throws away rounding information, and rounds improperly. oh well!
	//this is accurate.
	uint64_t error_from_baseline_times_period_denominator = period_denominator * (sum_of_all_timepoints - timepoint_at(middle_pivot) * elements()) - int(sum_of_all_frames - frame_at(middle_pivot) * elements()) * period_numerator;
	//this is not accurate. I could use rounded_divide(), but who cares
	uint64_t average_error_in_ticks = error_from_baseline_times_period_denominator / (elements() - 2) / period_denominator;

	return average_error_in_ticks;
}

void restart(uint64_t new_timepoint) {
	index_begin = index_end - 1;
	timepoint_at(index_begin) = new_timepoint;
	frame_at(index_begin) = 0;
	//convex_at(index_begin) = index_begin - 1; //don't need this, it's set when there are two elements
	//middle_pivot = index_begin; //don't need this, it's set when there are two elements
	sum_of_all_frames = 0;
	sum_of_all_timepoints = new_timepoint;
	number_of_multiframes = 0;
	debug_outc_vsync("restarting vsync"); //this is a bad sign
}

void new_value(uint64_t new_timepoint) {
	//technically, you could cause UB if the buffer contained timepoints 2^31 frames apart, causing division by 0.
	//however, that takes 172 days on a 144 Hz monitor. so we don't care.

	//debug_outc_vsync("starting", index_begin, "pivot", convex_at(middle_pivot), middle_pivot, "elements", index_end - index_begin, "frames", "sums", sum_of_all_frames, frame_at(convex_at(middle_pivot)) * (index_end - index_begin), frame_at(middle_pivot) * (index_end - index_begin));
	uint previous_element = index_end - 1;
	if (elements() >= 1) check(new_timepoint != timepoint_at(previous_element)); //this is a really degenerate case, and we don't want to handle it.
	if (elements() <= 1) { //special cases when there are too few elements, so we may not have enough information to reliably estimate the frame of the new timepoint
		timepoint_at(index_end) = new_timepoint;
		frame_at(index_end) = frame_at(previous_element) + 1;
		multiframe_at(index_end) = 0;
		sum_of_all_timepoints += timepoint_at(index_end);
		sum_of_all_frames += frame_at(index_end);
		number_of_multiframes += multiframe_at(index_end); //here for consistency. does nothing (it's 0)
		++index_end;
		if (elements() == 2) {
			middle_pivot = index_begin + 1;
			convex_at(index_begin + 1) = index_begin;
			convex_at(index_begin) = index_begin - 1;
			set_period_phase();
		}
		//if there's one point, don't bother setting the phase. it's probably junk info anyway.
		return;
	}

	//estimate the frame of the new timepoint
	uint this_frame = div_floor(period_denominator * elements() * (new_timepoint - timepoint_at(middle_pivot)) + period_numerator, period_numerator * elements()) + frame_at(middle_pivot);
	//(new timepoint - middle timepoint + period/size) / period + middle frame
	//a timepoint can be snapped into a frame even if it lands before that frame.
	//so if there are n timepoints, a frame should capture approximately [-1/n, (n-1)/n).
	//note that the phase error of the period/phase pair ~ 1/n. I don't know the constants though.
	//period error ~ 1/n^2, which means period error gives another 1/n to the phase error, since it's at the end. n * 1/n^2.
	//maybe in the future, I'll do a simulation with a uniform distribution.
	//at low sizes, we should be much more tolerant, since the period/phase might suffer from black swan events. still, the lower bound should be at least -1/2.
	//-1/2 can be ok. if there are 3 points, and the middle is delayed by 1/3, the timepoint differences will be 1, 4/3, 2/3. then it's -1/2. however, it would equally be valid to split this to 2 frames, then 1 frame.
	//going from 3->4, the bound is [-1/3, 2/3). which is about right.
	//technically, if the frame distance is higher, we should allow more tolerance, by adding the number of frames to elements() in the expression. however, we won't bother.

	//the average timepoint has error 0.05 ms. so timepoints with excess error should be tossed. we don't know what frame they are on, and our algorithm relies on correct frame guesses.
	//however, we don't know if it's the new timepoint which is wrong, or our old timepoints which are wrong. so we can't just toss one unless we are really sure.
	//I don't know that I can be bothered to separate out these timepoints. we'll just attempt to recover from errors, by testing shifting 4 points, as described in "I came back"

	//D3DKMTWaitForVerticalBlankEvent has average 0.05 ms error
	//IDirectDraw7::WaitForVerticalBlank has average 0.002 ms error. and is running up against some quantization error (0.0020526412155577067 * 2435886 ticks per sec = 5000 exactly)

	//3 elements with timepoints 0, 2, 3, will cause the vsync to report a 1.5x period time.
	//but these timepoints will be rejected no matter which period is chosen. if the frames are 1-1, it'll be rejected by error, since 1/2 > 1/4. if the frames are 2-1, it'll be rejected since 50% of the frames are multi-frames, which is greater than 1/3.
	//it makes sense to reject these 3 timepoints and start fresh: there is barely any information, so there is no loss in throwing it away. you'll get some better information soon.
	//so, there is also no point in handling these 3 points with a special case.
	bool is_multiframe = false;

	if (int(this_frame - frame_at(previous_element)) <= 0) { //two frames in the same period. this is not possible
		debug_outc_vsync("zero frame", new_timepoint - timepoint_at(previous_element), "period", period_numerator * 1000 / period_denominator / ticks_per_sec, "size", elements());
		//for now, just push the frame forward.
		this_frame = frame_at(previous_element) + 1;
		//future: may also consider attempting to push the previous frame back, if it was a multiframe.
	}
	else if (int(this_frame - frame_at(previous_element)) >= int((elements() + 2) / 2)) {
		debug_outc_vsync("long multi-frame", new_timepoint - timepoint_at(previous_element), "period", period_numerator * 1000 / period_denominator / ticks_per_sec, "size", elements());
		//this is a really long multiframe, and we no longer have confidence that we know its phase accurately. so restart.
		//technically, phase error is asymptotically 1/elements^2, so we should be fine even with a gap of elements^2 / 2. however, our frame guess has only 1/elements tolerance, so we don't want to push it too far.
		//though, the phase may be completely scrambled, so maybe it's not worth it to add in the new timepoint. still, we need some information, so I guess we'll leave it alone.
		//this is caused by alt-tab. it still receives vblank signals inconsistently

		//future: consider throwing away the new point as well? is it a good point?
		restart(new_timepoint);
		return;
	}
	else if (int(this_frame - frame_at(previous_element)) >= 2) {
		//debug_outc_vsync("multi-frame", new_timepoint - timepoint_at(previous_element), "period", period_numerator * 1000 / period_denominator / ticks_per_sec, "size", elements());
		//if it worth it to detect two multiframes in a row, and cause a restart?
		//no: the true period might be 1.5x the reported period. then multiframes will alternate with single frames.
		//then, the error would be 1/4 the period (it alternates 0 and 1/2). that's probably not enough to trigger error detection. so it must trigger multiframe detection
		//if 1/3 are multiframes, the period might be 4/3 the reported period. then the error would be 1/3 the period (alternates 0 1/3 2/3). that should be large enough to trigger too-high error, which we'll set at 1/4.
		is_multiframe = true;
	}

	//if the circular buffer is already full, we have an extra incoming element. so technically we have max_size + 1 elements to look at.
	//however, we do not want to behave as if both the oldest element, and the new incoming element, both exist simultaneously.
	//this is because it causes special cases, since we can't place the incoming element in the array
	sum_of_all_frames += this_frame;
	sum_of_all_timepoints += new_timepoint;
	number_of_multiframes += is_multiframe;
	if (index_end - index_begin == max_size) {
		sum_of_all_frames -= frame_at(index_begin); //the old value will be erased
		sum_of_all_timepoints -= timepoint_at(index_begin);
		number_of_multiframes -= multiframe_at(index_begin);
		++index_begin;
	}
	timepoint_at(index_end) = new_timepoint;
	frame_at(index_end) = this_frame;
	multiframe_at(index_end) = is_multiframe;
	uint this_index = index_end;
	++index_end;

	if (is_multiframe) {
		//this needs to prevent period = 1.5.
		//0, 1.5, 3. 3 timepoints, 1 multiframe. (2n+1) timepoints for n multiframes. this case must be caught; it isn't caught by the error threshold.
		//0, 1.3, 2.6, 4. 4 timepoints, 1 multiframe. (3n+1) timepoints for n multiframes. this case isn't important; it's already caught by the error threshold.
		if (number_of_multiframes * 3 >= elements() - 1) {
			debug_outc_vsync("multi-frame restart", new_timepoint - timepoint_at(previous_element), "period", period_numerator * 1000 / period_denominator / ticks_per_sec, "size", elements());
			restart(new_timepoint);
			return;
		}
	}

	find_convex_line_backwards_from(this_index);
	if (before(convex_at(this_index), middle_pivot)) {
		middle_pivot = this_index;
	}
	else
		convex_next(convex_at(this_index)) = this_index;

	//previous_pivot might have changed when running find_convex_line_backwards_from(this_index), since it might have fallen off the edge.
	while (1) {
		if (before(convex_at(middle_pivot), index_begin)) {
			find_convex_line_backwards_from(middle_pivot);
		}
		//if the oldest element expired, the middle pivot's line might not extend over the midline anymore. so move it backwards
		uint previous_pivot = convex_at(middle_pivot);
		if (!before(frame_at(previous_pivot) * elements(), sum_of_all_frames)) {
			convex_next(previous_pivot) = middle_pivot;
			middle_pivot = previous_pivot;
		}
		else
			break;
	}
	set_period_phase();
	//check error
	uint64_t error_from_baseline_times_period_denominator = period_denominator * (sum_of_all_timepoints - timepoint_at(middle_pivot) * elements()) - int(sum_of_all_frames - frame_at(middle_pivot) * elements()) * period_numerator;
	//uint64_t average_error_in_ticks = error_from_baseline_times_period_denominator / (elements() - 2) / period_denominator;
	//outc("vsync error is", average_error_in_ticks, "ticks", double(average_error_in_ticks) / ticks_per_sec * 1000, "ms");

	//if error >= period / 4
	//there are more than 2 elements if you arrived here, because 2 elements = early exit from function at beginning.
	//also, 2 elements don't participate in error calculation because they are pivot points and have their error artificially zeroed. that's why we use (elements() - 2) instead of elements().
	if (error_from_baseline_times_period_denominator >= (elements() - 2) * period_numerator / 4) {
		debug_outc_vsync("excess error", double(error_from_baseline_times_period_denominator / (elements() - 2) / period_denominator) / ticks_per_sec * 1000, "ms, period", period_numerator * 1000 / period_denominator / ticks_per_sec, "size", elements());
		restart(new_timepoint);
		return;
	}
#if !NDEBUG
	reference_verify_correctness(); //todo: maybe turn this off
	if (period_numerator / period_denominator < ticks_per_sec / 70 || period_numerator / period_denominator > ticks_per_sec / 50)
		debug_outc_vsync("inaccurate", period_denominator * ticks_per_sec / period_numerator, "size", elements()); //todo: this is not a good idea for other computers. also, maybe check if we don't have enough elements
#endif
}

#if phase_error_adjustment_order_statistic
//at 1, we want 1. at 2, we also want 1.
unsigned order_statistic(unsigned timepoint_count) {
	return std::lround(timepoint_count / (31 - std::countl_zero(timepoint_count) + 1));
}

//this returns the phase error times period_denominator. so divide by period_denominator after you get it.
//it uses order statistics. future: compare this method with the average error (which can be blown away by black swan timepoints). maybe truncation of highest error values + average would be better.
double estimate_phase_error() {
	if (elements() <= 2)
		return 0;

	uint64_t error[max_size]; //static? nah, stack is fast allocation.
	//we want to eliminate integer rounding errors. however, our period is a ratio. thus, we multiply all elements by the denominator.
	//that way, the period is the numerator.

	//first, we find an offset to compare to. this offset is lower than every other timepoint, so that we can use positive integer modulo, which is faster than signed integer modulo.
	uint64_t base_offset = timepoint_at(middle_pivot) * period_denominator;
	uint64_t lowest_timepoint = timepoint_at(index_begin) * period_denominator;
	auto shift_base_offset = div_ceil(base_offset - lowest_timepoint, period_numerator);
	base_offset -= shift_base_offset * period_numerator;

	uint increment = 0;
	for (uint x = index_begin; x != index_end; ++x) {
		error[increment] = (timepoint_at(x) * period_denominator - base_offset) % period_numerator;
		check(error[increment] < ticks_per_sec);
		++increment;
	}

	//skip the two pivot points. they're always unnaturally zero.
	check(increment == index_end - index_begin);
	unsigned order_number = order_statistic(increment - 2);
	std::nth_element(std::begin(error), std::begin(error) + order_number + 2, std::begin(error) + increment);

	const double multiplier_for_true_error = 1.0; //I'm too lazy to calculate the real constant, so this is an approximation.
	double estimate = error[order_number + 2] * multiplier_for_true_error / ((order_number + 2) * period_denominator);
	//outc(estimate);
	return estimate;
}
#endif

//communicate the period and phase with the client renderer.
//this introduces a rounding error from integer arithmetic, but we have no choice, because we must reduce interaction between the vsync finder and the renderer.
//and, the phase and period are the only atomic variables which can be varied independently. so we cannot expose our precise ratio.
//to reduce rounding error, we position the phase on the frame after the latest frame.
//this also helps when the phase and period are set at different times - slight variations in period will not cause the measurement to explode
void set_period_phase() {
	find_period_ratio();

	//phase ~ index_end + period
	//phase = round(index_end + period - timepoint_at(middle_pivot), period) * period + timepoint_at(middle_pivot)
	//period = n/d
	//phase = round(difference * d / n + 1) * n/d
#if phase_error_adjustment_order_statistic
	auto phase_error = estimate_phase_error();
	uint64_t phase = timepoint_at(middle_pivot) + rounded_divide((frame_at(index_end - 1) - frame_at(middle_pivot) + 1) * period_numerator, period_denominator) - phase_error;
#else
	uint64_t phase = timepoint_at(middle_pivot) + rounded_divide((frame_at(index_end - 1) - frame_at(middle_pivot) + 1) * period_numerator, period_denominator);
#endif
	double period = double(period_numerator) / period_denominator;

#if phase_error_adjustment_order_statistic
	constexpr int tests = 7;
	constexpr int tests_to_aggregate = 1000;
	double multipliers[tests] = {-2.0, -1.0, -0.5, 0, 0.5, 1.0, 2.0};
	static int skip_first_few = -5;
	static double previous[tests] = {};
	static double sums[tests] = {};
	static double squares[tests] = {};
	static double sum_error = 0;
	for (int x : std::views::iota(0, tests)) {
		double value = timepoint_at(middle_pivot) + (frame_at(index_end - 1) - frame_at(middle_pivot) + 1) * period_numerator / period_denominator - phase_error * multipliers[x];
		if (skip_first_few >= 0) {
			check(value - previous[x] < ticks_per_sec);
			sums[x] += value - previous[x];
			squares[x] += sq(value - previous[x]);
		}
		previous[x] = value;
	}
	sum_error += phase_error;
	++skip_first_few;
	if (skip_first_few % tests_to_aggregate == tests_to_aggregate - 1) {
		outc("average error", sum_error / sums[0]);
		for (int x : std::views::iota(0, tests)) {
			outc(x, std::sqrt(squares[x] * tests_to_aggregate / sq(sums[x]) - 1), "lower is better");
			sums[x] = 0;
			squares[x] = 0;
		}
		sum_error = 0;
	}
#endif

	vblank_phase_atomic.store(phase, std::memory_order_relaxed); //phase first - reduce wobbling.
	vblank_period_atomic.store(period, std::memory_order_relaxed);
}
} // namespace vf
