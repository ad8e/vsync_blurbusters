#pragma once
#include "console.h"
#include "div_floor.h"
#include "timing.h"
#include <array>
#include <atomic>
#include <cmath>

extern double system_claimed_monitor_Hz; //may not be totally accurate. however, we assume it should be good enough. get this from system API
extern int total_scanlines; //we assume these are swept through at an even rate. get this from the system API

//see vsync.cpp for docs
namespace vscan {
uint64_t phase;
double period;

constexpr uint max_size = 64; //our function is O(1). the only tradeoff is space. so we might as well bump the size up even though it barely improves accuracy.

namespace circular {
uint64_t timepoints[max_size] = {}; //first element is calculated off the previous. so initialize them all to a indeterminate value (which is 0)
unsigned scanline[max_size] = {};
unsigned frame_of[max_size] = {};
} // namespace circular
uint index_end = 0;
uint index_begin = 0;
uint64_t& timepoint_at(uint x) { return circular::timepoints[x % max_size]; }
uint& scanline_at(uint x) { return circular::scanline[x % max_size]; }
uint& frame_at(uint x) { return circular::frame_of[x % max_size]; }
uint64_t sum_of_timepoints = 0;
uint64_t sum_of_unwrapped_scanlines = 0; //unwrapped scanline = each scanline has frame * total_scanlines already added to it. must be uint64_t so that it is wrap is consistent with the others. it gets added and multiplied
uint64_t sum_timepoint_timepoint = 0; //sum of squares of timepoints. will be used for error calculation (if I ever figure out how to do it)
uint64_t sum_timepoint_scanline = 0; //sum of timepoints * unwrapped scanlines
uint64_t sum_scanline_scanline = 0; //sum of squares of unwrapped scanlines.
uint elements() { return index_end - index_begin; }

void linear_regression() {
	//time for linear regression. what should the dependent and independent variables be?
	//there is noise in both the timepoint and scanline measurement.
	//todo: there is no noise in the timepoint. all the noise is in the scanline measurement. thus, our linear regression is wrong
	//should it should regress with time as the independent variable, even though we want to minimize time error? that would minimize error in the noisy variable. which makes more sense. but then the predicted variable might be slightly wrong?

	//naively, independent variable should be scanline. because it takes in a scanline, outputs a time, and you want the time to have the least error.
	//but linear regression is inherently flawed in the presence of a noisy independent variable
	//"Weak exogeneity. This essentially means that the predictor variables x can be treated as fixed values, rather than random variables. This means, for example, that the predictor variables are assumed to be error-free—that is, not contaminated with measurement errors. Although this assumption is not realistic in many settings, dropping it leads to significantly more difficult errors-in-variables models."
	//Deming regression is not appropriate, it measures perpendicular distance. https://en.wikipedia.org/wiki/Deming_regression
	//simple linear regression is fast and easy. we'll stick with simple linear regression for now. dunno about later.
	//problem statement: variables 1 and 2. all the error is in variable 1. want to fit a line to minimize error in variable 2.

	//https://en.wikipedia.org/wiki/Simple_linear_regression
	//https://math.stackexchange.com/questions/2826957/simplifying-beta-1-estimate-for-a-simple-linear-regression-model
	//https://www.cs.wustl.edu/~jain/iucee/ftp/k_14slr.pdf page 8
	//we are operating mod 2^64. so n average(x) average(y) will cause a problem. because division is not a function (it's multivalued). without division, we cannot take averages
	//to solve this, multiply by the number of elements, which will eliminate the division.
	//our formula is (n sum (x_i y_i) - n^2 x_y_) / (n sum (x_i^2) - n^2 x_^2) =
	//(n sum (x_i y_i) - sum_x sum_y) / (n sum (x_i^2) - sum_x sum_x)
	uint64_t numerator = elements() * sum_timepoint_scanline - sum_of_timepoints * sum_of_unwrapped_scanlines;
	uint64_t denominator = elements() * sum_scanline_scanline - sum_of_unwrapped_scanlines * sum_of_unwrapped_scanlines;
	double accurate_ticks_per_scanline = double(numerator) / denominator; //slope of regression line

	//after calculating the slope, we must calculate phase. however, we must place the origin at an existing timepoint. (we'd have precision issues if we placed the origin at 0.) we choose index_begin to be the origin.
	uint64_t unwrapped_scanline = frame_at(index_begin) * total_scanlines + scanline_at(index_begin);
	double scanline_average = (sum_of_unwrapped_scanlines - elements() * unwrapped_scanline) / double(elements());
	double timepoint_average = (sum_of_timepoints - elements() * timepoint_at(index_begin)) / double(elements());
	//x-axis: zero is the scanline associated to index_begin()
	//y-axis: zero is the timepoint associated to index_begin()

	double estimated_timepoint_at_index_vblank = timepoint_average - accurate_ticks_per_scanline * (scanline_average + scanline_at(index_begin)); //best guess for the timepoint of the vblank associated to index_begin())

	double phase_offset_from_estimated_vblank = (frame_at(index_end - 1) - frame_at(index_begin) + 1) * total_scanlines * accurate_ticks_per_scanline;
	double adjustment_for_floor_operation = -0.5 * accurate_ticks_per_scanline; //the scanline report is N for scanline [N, N+1). so subtract half a scanline
	phase = int64_t(phase_offset_from_estimated_vblank + estimated_timepoint_at_index_vblank + adjustment_for_floor_operation) + timepoint_at(index_begin);
	//outc("new phase", phase, accurate_ticks_per_scanline * total_scanlines, "backup estimate", timepoint_at(index_end - 1) - double(scanline_at(index_end - 1)) / total_scanlines * ticks_per_sec / 60 + ticks_per_sec / 60);
	period = accurate_ticks_per_scanline * total_scanlines;
}

void print_error(double accurate_ticks_per_scanline) {
	//shortcut formula.
	//https://www.cs.wustl.edu/~jain/iucee/ftp/k_14slr.pdf
	//https://www.colorado.edu/amath/sites/default/files/attached-files/ch12_0.pdf
	//our yiyi has zero as the origin point. that is very bad for rounding.
	//thus, we transform our origin to the midpoint. we use these transformations: E[(xi - x)^2] = E[xi^2 - x^2]. E[(xi - x)(yi - y)] = E[xiyi - xy]
	double yiyi = (elements() * sum_timepoint_timepoint - sum_of_timepoints * sum_of_timepoints) / elements();
	double a_yi = 0; //free, because our average y_i is zero. (we chose this as the origin)
	double bxiyi = accurate_ticks_per_scanline * (elements() * sum_timepoint_scanline - sum_of_timepoints * sum_of_unwrapped_scanlines) / elements();
	uint64_t SSE = yiyi - a_yi - bxiyi;
	auto error = std::sqrt(SSE / (elements() - 2));
	outc("vsync finder std dev:", error);
}

void new_value(uint64_t new_timepoint, uint scanline) {
	if (elements() == max_size) {
		sum_of_timepoints -= timepoint_at(index_begin);
		uint64_t unwrapped_scanline = frame_at(index_begin) * total_scanlines + scanline_at(index_begin);
		sum_of_unwrapped_scanlines -= unwrapped_scanline;
		sum_timepoint_timepoint -= timepoint_at(index_begin) * timepoint_at(index_begin);
		sum_timepoint_scanline -= timepoint_at(index_begin) * unwrapped_scanline;
		sum_scanline_scanline -= unwrapped_scanline * unwrapped_scanline;
		++index_begin;
	}
	timepoint_at(index_end) = new_timepoint;
	scanline_at(index_end) = scanline;
	double frame_advance_from_previous = (new_timepoint - timepoint_at(index_end - 1)) * system_claimed_monitor_Hz / ticks_per_sec; //benchmark off the previous. we might also consider benchmarking off index_begin, in the future. not sure.
	int scanline_diff_from_previous = scanline - scanline_at(index_end - 1);
	double advanced_frames = std::nearbyint(frame_advance_from_previous - scanline_diff_from_previous / double(total_scanlines));
	frame_at(index_end) = frame_at(index_end - 1) + int(advanced_frames);
	sum_of_timepoints += timepoint_at(index_end);
	uint64_t unwrapped_scanline = frame_at(index_end) * total_scanlines + scanline_at(index_end);
	sum_of_unwrapped_scanlines += unwrapped_scanline;
	sum_timepoint_timepoint += timepoint_at(index_end) * timepoint_at(index_end);
	sum_timepoint_scanline += timepoint_at(index_end) * unwrapped_scanline;
	sum_scanline_scanline += unwrapped_scanline * unwrapped_scanline;
	++index_end;
	if (elements() <= 2) { //don't need to care too much, whether it's 1 or 2 points.
		phase = timepoint_at(index_end - 1) - int64_t(ticks_per_sec * scanline_at(index_end - 1) / (total_scanlines * system_claimed_monitor_Hz));
		period = ticks_per_sec / system_claimed_monitor_Hz;
		return;
	}

	linear_regression();
}
} // namespace vscan