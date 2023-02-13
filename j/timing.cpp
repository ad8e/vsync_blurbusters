
//timing functions: now() returns ticks. ticks_per_sec is a constant initialized at startup.
//sleep functions: if it says "native", it's a native interface and is the most efficient thing on that platform. if it doesn't, then it may do some extra conversion.

//you may want to call improve_timer_resolution_on_Windows().
//and if your application is minimized, call reset_timer_resolution_on_Windows()

//this is much better than the native C++ standard library.
//"MSVC: this_thread::sleep_for(1ms) uses the wall clock instead of the monotonic one. change the clock and hang forever. Last I heard it was an ABI-breaking fix and thus not released."

//this has been tested on Windows 8.1. spinsleep is accurate to about 10 us.
//this has not been tested at all on Linux. it's definitely not working well there because benchmarking is needed

#pragma once
#define BENCHMARK_SLEEP false
#if BENCHMARK_SLEEP
#include "console.h"
#endif

#include "timing.h"
#include <thread>
#include "xmmintrin.h" //_mm_pause

//the variable ticks_per_sec is statically initialized. this file needs to go at the top of the cpp list, to prevent static initialization fiasco.

//Linux: raw monotonic clock keeps track of absolute time accurately, but can slew the rate pretty hard.
//https://stackoverflow.com/questions/14270300/what-is-the-difference-between-clock-monotonic-clock-monotonic-raw
//make sure the timekeeping function and the sleep() function use the same clock.
//Linux's clock_nanosleep() only supports monotonic, not raw monotonic, so the timer must use monotonic as well.

//https://github.com/ros2/rcutils/issues/43#issuecomment-320954506
//for Macs

//unsynchronized times on different cores are only an issue on old computers.

//C++ std's high resolution clock uses an unfortunate realtime clock on Linux https://stackoverflow.com/a/41203433/
//also, mingw uses a shitty clock for high resolution clock on Windows

#include <cstdint> //uint64_t
#if _WIN32
#include <profileapi.h> //QueryPerformanceCounter
//std::chrono wraps QueryPerformanceCounter, just with conversion.
//we don't want to type that huge chain of :: from std::chrono anyway, so let's wrap it ourselves without the conversion.

//The frequency of the performance counter is fixed at system boot and is consistent across all processors. Therefore, the frequency need only be queried upon application initialization, and the result can be cached.
//https://docs.microsoft.com/en-us/windows/win32/api/profileapi/nf-profileapi-queryperformancefrequency

const uint64_t ticks_per_sec = []() {
	LARGE_INTEGER li;
	QueryPerformanceFrequency(&li); //no need to check return value "on any system that runs Windows XP or later"
	return li.QuadPart;
}();

uint64_t now() {
	LARGE_INTEGER li;
	QueryPerformanceCounter(&li);
	return li.QuadPart;
}

#else
//verified good on Linux and Mac, 2020
//in the future, we will want to use clock_gettime64(). as of 2020, libstdc++ seems to be tracking this.
//GCC does exactly what we would. https://github.com/gcc-mirror/gcc/blob/master/libstdc%2B%2B-v3/src/c%2B%2B11/chrono.cc#L90
//so does clang. https://github.com/llvm/llvm-project/blob/main/libcxx/src/chrono.cpp#L288
#include <chrono>
const uint64_t ticks_per_sec = std::chrono::steady_clock::period::den;
uint64_t now() {
	return std::chrono::steady_clock::now().time_since_epoch().count();
}
#endif

#if _WIN32
#define WIN32_LEAN_AND_MEAN
#include "windows.h"
//#include <timeapi.h> //timeBeginPeriod
//#if _MSC_VER
//#pragma comment(lib, "winmm.lib")
//#endif
//NtSetTimerResolution, an undocumented Windows API. https://stackoverflow.com/a/31411628
//sets multimedia timers to 0.5 ms, which is better than our 1 ms.
//and it actually works great. beware of power usage though. (play nice!)

//https://stackoverflow.com/a/31411628
typedef LONG NTSTATUS;
static NTSTATUS(__stdcall* NtDelayExecution)(BOOL Alertable, PLARGE_INTEGER DelayInterval) = (NTSTATUS(__stdcall*)(BOOL, PLARGE_INTEGER))GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtDelayExecution");
static NTSTATUS(__stdcall* NtSetTimerResolution)(IN ULONG RequestedResolution, IN BOOLEAN Set, OUT PULONG ActualResolution) = (NTSTATUS(__stdcall*)(ULONG, BOOLEAN, PULONG))GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtSetTimerResolution");
//we use Nt rather than Zw.

void improve_timer_resolution_on_Windows() {
#if USE_UNDOCUMENTED_APIS
	ULONG actualResolution;
	NtSetTimerResolution(5000, true, &actualResolution);
#else
#if BENCHMARK_SLEEP
	check(timeBeginPeriod(1) == TIMERR_NOERROR, "failed to change timer resolution");
#else
	timeBeginPeriod(1);
#endif
#endif
};

void reset_timer_resolution_on_Windows() {
#if USE_UNDOCUMENTED_APIS
	ULONG actualResolution;
	NtSetTimerResolution(156250, true, &actualResolution);
#else
#if BENCHMARK_SLEEP
	check(timeEndPeriod(1) == TIMERR_NOERROR, "failed to change timer resolution");
#else
	timeEndPeriod(1);
#endif
#endif
};

#define TIMER_WAIT 0
#if TIMER_WAIT
	//from my testing on Win 8.1, this uses the same mechanism as Sleep(). if you don't call timeBeginPeriod(1), it jumps up to 15 ms sleeps
	//disadvantage: need to hold a timer object.
	//disadvantage: suffers much more often from overruns and underruns than Sleep(), and the sizes of those over/underruns are much bigger too. so this is worse.
	//advantage: can sleep for less than one millisecond, rather than jumping from 0 to 2 milliseconds like Sleep() does.
	//asking for (n, n+1) is equivalent to calling Sleep(n). the exception is n = 0.
	//asking for integers n gives a range of sleeps: (n-1, n+1). which is terrible
	//this might be less performant than Sleep. it's just a guess, but a 0 ms call takes 4 us for timers, and 0-1 us for Sleep.
	//https://blat-blatnik.github.io/computerBear/making-accurate-sleep-function/ claims that timers are better, but I don't see it.
	//future: if high-res timers are available, maybe we don't need to set the timer period. only supported on Windows 10+. currently undocumented
	//"using CREATE_WAITABLE_TIMER_HIGH_RESOLUTION will provide sleeps as low as about 500 microseconds, while our current approach provides about 1 millisecond sleep" https://go-review.googlesource.com/c/go/+/248699/
	//but it's only as good as using the 0.5 ms period API. the advantage is not setting the timer period, but the disadvantage is the performance

	//Many libraries ([Go](https://groups.google.com/a/chromium.org/g/scheduler-dev/c/0GlSPYreJeY), [Python](https://github.com/python/cpython/issues/89592), [perhaps Rust later](https://github.com/rust-lang/rust/issues/43376), [VirtualBox](https://www.virtualbox.org/browser/vbox/trunk/src/VBox/Runtime/r3/win/timer-win.cpp#L312)) are switching over to high-resolution timers. It's called an "IRTimer", available since Windows 8. https://github.com/python/cpython/issues/65501#issuecomment-1093653203

	//maybe post this somewhere: `CreateWaitableTimer(x)`, without the `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` flag, is equivalent to calling `Sleep(n)` if `x` is inside `(n, n+1)`, `n>0`. If `x=n`, then it waits between `(n-1, n+1)`, which is terrible. This means `CreateWaitableTimer()` is also using the same underlying source as `Sleep()`, and the instability at integers means there is error introduced in the middle.

#if USE_UNDOCUMENTED_APIS
//https://bugs.python.org/issue45429
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
#endif

struct timer_wait {
	HANDLE hTimer;
	timer_wait() {
#if USE_UNDOCUMENTED_APIS
		hTimer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_MODIFY_STATE);
		if (hTimer)
			return; //successfully created high-resolution timer
#endif
		hTimer = CreateWaitableTimer(NULL, FALSE, NULL);
		check_assert(hTimer); //todo, is the check necessary
	}
	//converts from ticks to native format, then calls wait_for()
	void wait_for_convert(uint64_t ticks) {
		wait_for_in_100ns_ticks(ticks * 10000000 / ticks_per_sec);
	}
	void wait_for_in_100ns_ticks(uint64_t ns100) {
		LARGE_INTEGER lpDueTime;
		lpDueTime.QuadPart = -ns100;
		check_assert(SetWaitableTimer(hTimer, &lpDueTime, 0, NULL, NULL, FALSE)); //todo, is the check necessary
		check_assert(WaitForSingleObject(hTimer, INFINITE) == WAIT_OBJECT_0); //todo, is the check necessary
	}
	~timer_wait() { CloseHandle(hTimer); }
};
#endif

#elif __linux__
#include <time.h>
#endif
//if you call the native sleep function, it will hit your asked-for value but might overrun up to expected_overrun
//on Windows, it's usually 1 ms if the timer period is 1 ms. sometimes a little more, like 1.05 ms. but changing to 1.2 ms doesn't seem to improve the stability of the vsync line
//an old note by me says it was 1 ms on Linux, but haven't checked recently.

#if _WIN32 && USE_UNDOCUMENTED_APIS
const int64_t expected_overrun = ticks_per_sec / 1900; //1/2000 corresponds to 0.5 ms.
//but even after 0.5 ms, there is an occasional overrun. it's often 0-20 us. occasionally 50 us.
//however, spinloops also have overruns! 0-15 us. so no point in going to 1800. 1900 works well on my machine
//oddly, 1950 creates a bunch of spinloop overruns, rather than sleep overruns. I don't understand this. they're small, 0-2 us
//adjust this so that when BENCHMARK_SLEEP is on, the sum of sleep overrun reports + spinloop overrun reports are rare.
#else
const int64_t expected_overrun = ticks_per_sec / 1000;
#endif

#if _WIN32 && USE_UNDOCUMENTED_APIS
void native_sleep_at_most_100ns(uint64_t ns100) {
	//https://stackoverflow.com/questions/54582249/64bit-precision-sleep-function
	LARGE_INTEGER interval;
	if (ns100 <= one_sec_in_100ns / 1900)
		return;
	interval.QuadPart = -(int64_t)(ns100 - one_sec_in_100ns / 1900);
	NtDelayExecution(false, &interval);
}
#endif

#if __linux__
timespec ns_to_linux_struct(uint64_t ns) {
	timespec ts;
	ts.tv_sec = ns / 1000000000;
	ts.tv_nsec = ns % 1000000000;
	return ts;
}

void native_sleep_ns(uint64_t ns) {
	//"Most popular solution seems to be nanosleep() API. However if you want < 2ms sleep with high resolution than you need to also use sched_setscheduler call to set the thread/process for real-time scheduling. If you don't than nanosleep() acts just like obsolete usleep which had resolution of ~10ms. Another possibility is to use alarms."
	//https://stackoverflow.com/a/41862592/
	//future: I will need to implement this

	//gcc linux uses nanosleep for std::this_thread::sleep_at_most(): http://www.martani.net/2011/07/nanosleep-usleep-and-sleep-precision.html
	//with clock_nanosleep, we can select the monotonic clock: https://stackoverflow.com/questions/7794955/why-is-clock-nanosleep-preferred-over-nanosleep-to-create-sleep-times-in-c
	//https://stackoverflow.com/questions/3523442/difference-between-clock-realtime-and-clock-monotonic
	//http://man7.org/linux/man-pages/man2/nanosleep.2.html says nanosleep uses MONOTONIC on linux anyway, so nanosleep is fine.
	//the spec says POSIX.1 specification for clock_settime(2) says that discontinuous changes in CLOCK_REALTIME should not affect nanosleep()
	//so Linux is semi-defying the spec, although the spec also says that end_time changes should not affect sleep times. so really, linux is fine and the spec is stupid.
	auto time_as_linux_struct = ns_to_linux_struct(ns);
	clock_nanosleep(CLOCK_MONOTONIC, 0, &time_as_linux_struct, nullptr);
}

//clock_nanosleep supports an absolute version
void native_sleep_until_before(uint64_t ticks) {
	auto time_as_linux_struct = ns_to_linux_struct(ticks - expected_overrun);
	clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &time_as_linux_struct, nullptr);
}
#endif

//return true if actually waited
//unit is ticks, which may not be the native interface.
bool sleep_at_most(int64_t ticks) {
#if _WIN32
#if USE_UNDOCUMENTED_APIS
	//https://stackoverflow.com/questions/54582249/64bit-precision-sleep-function
	LARGE_INTEGER interval;
	if (ticks <= expected_overrun)
		return false;
	interval.QuadPart = -(int64_t)(one_sec_in_100ns * (ticks - expected_overrun) / ticks_per_sec);
	NtDelayExecution(false, &interval);
	return true;
#else
	//mingw-w64 implements sleep_at_most() very poorly. 15.6 ms delay whether media timers are changed or not. it used to work better, but now it doesn't.
	//VS's sleep_at_most() also wraps Sleep() with some annoying millisecond rounding.
	//on Windows, use the native APIs directly. every other sleep function is a crappy wrapper around Sleep(), and Sleep is a crappy wrapper around NtDelayExecution(), with low resolution and a special case at 0-1.
	//Windows's Sleep() doesn't want to sleep for <1ms. but with our new undocumented APIs, it can.
	//in Windows 10, there are new high-res waitable timers (that still probably use the same mechanism, just without needing to change the period), but at least on Win 8.1, they have other problems
	uint64_t sleep_milliseconds = 1000 * ticks / ticks_per_sec;
	if (sleep_milliseconds == 0) return false;
	Sleep(sleep_milliseconds - 1); //"Note that a ready thread is not guaranteed to run immediately. Consequently, the thread may not run until some time after the sleep interval elapses"
	//when testing, -1 is necessary. asking for 1 ms sleep gives a [1 ms, 2 ms) sleep. sometimes it goes a little over 2 ms, but we'll ignore that
	return true;
#endif
#elif __linux__
	native_sleep_ns(ticks - expected_overrun);
	return true;
#endif
}

//for platforms which haven't been implemented yet
void sleep_backup(int64_t ticks) {
	uint64_t sleep_nanoseconds = (ticks - expected_overrun) * 1000000000.0 / ticks_per_sec; //convert to double to avoid rounding issues
	//uint64_t sleep_nanoseconds = ticks * 1000000000.0 / ticks_per_sec; //convert to double to avoid rounding issues //sleep_at_most is at least the requested amount
	std::this_thread::sleep_for(std::chrono::nanoseconds(sleep_nanoseconds));
	//thread::sleep_for() is broken in MSVC: https://www.reddit.com/r/cpp/comments/l755me/stdchrono_question_of_the_day_whats_the_result_of/gl64qg7/
}

#if BENCHMARK_SLEEP
bool benchmark_short_sleeps = []() {
	outc("testing sleep accuracy:");

#if _WIN32
	improve_timer_resolution_on_Windows();
	for (int x = 0; x < 20; ++x) {
		auto sleep_time = x / 5; //run each ms option 5 times
		auto now_ = now();
		Sleep(sleep_time);
		auto end = now();

		outc("asked for", sleep_time, "ms from Sleep(), actual wait", (end - now_) * 1000000 / ticks_per_sec, "us");
		//asking for N ms gives [N, N+1). ~8% chance of running over N+1 by 0-40 us
	}
#if USE_UNDOCUMENTED_APIS
	for (int x = 0; x < 20; ++x) {
		auto sleep_time = x * 2000;
		auto now_ = now();
		LARGE_INTEGER interval;
		interval.QuadPart = -(int64_t)(sleep_time);
		NtDelayExecution(false, &interval);
		auto end = now();

		outc("asked for", sleep_time / 10000.f, "ms from NtDelayExecution, actual wait", (end - now_) * 1000000 / ticks_per_sec, "us, extra", (end - now_) * 1000000 / ticks_per_sec - sleep_time / 10.f);
		//result: asking for n ms, where n can be fractional, gives [n, n+T] ms, where T is the amount specified by the timeBeginPeriod. it works great with ZwSetTimerResolution at 0.5 ms. (Sleep()'s input doesn't have the precision to.)
	}
#if TIMER_WAIT
	timer_wait tw;
	for (int x = 0; x < 20; ++x) {
		auto sleep_time = x * 2000; //0.2 ms increments
		auto now_ = now();
		tw.wait_for_in_100ns_ticks(sleep_time);
		auto end = now();

		outc("asked for", sleep_time / 10000.f, "ms from timers, actual wait", (end - now_) * 1000000 / ticks_per_sec, "us, extra", (end - now_) * 1000000 / ticks_per_sec - sleep_time / 10.f);
		//see the timer definition for results
	}
#endif
	reset_timer_resolution_on_Windows();
#endif

#else
	for (int x = 0; x < 20; ++x) {
		auto sleep_time = x * ticks_per_sec / 5000; //200 microsecond increments
		auto now_ = now();
		native_sleep_at_most(sleep_time);
		auto end = now();

		outc("asked for", sleep_time * 1000000 / ticks_per_sec, "us, actual wait", (end - now_) * 1000000 / ticks_per_sec);
		//outc("frame delay ", float(frame_clock::period::den) / (time_frame_end - time_frame_start));
	}
#endif
	return 0;
}();
#endif

//spinwait sleep for slightly more than than the asked-for period, but basically equal. uses a monotonic clock, so good for small and accurate timepoints, but not for long periods
//we expect the times to be in ticks, where ticks are from timing.h. use timing.h's now().
void accurate_sleep_until(uint64_t end_time, uint64_t current_time) {
	if (int64_t(current_time - end_time) > 0) {
#if BENCHMARK_SLEEP
		outc("tried to wait after event already passed", (current_time - end_time) * 1000000 / ticks_per_sec, "us");
#endif
		return;
	}

	if (int64_t(end_time - current_time - expected_overrun) > 0) {
		sleep_at_most(end_time - current_time);
		current_time = now();
	}

#if BENCHMARK_SLEEP
	if (int64_t(current_time - end_time) > 0) {
		outc("sleep overrun", (current_time - end_time) * 1000000 / ticks_per_sec, "us");
		return;
	}
	else if (int64_t(end_time - current_time - expected_overrun * 3 / 2) > 0) { //we use 1.5 as our threshold because underruns are less important
		outc("sleep underrun", (current_time - end_time) * 1000000 / ticks_per_sec, "us");
		return;
	}
#endif

	while (int64_t(end_time - current_time) > 0) {
		//"YieldProcessor() macro from windows.h expands to the undocumented _mm_pause intrinsic, which ultimately expands to the pause instruction in 32-bit and 64-bit code."
		//idea: double spinloop. use asm("pause") until its overflow time. then switch to straight spinloop
		//nope, the performance gap has disappeared! so let's just use _mm_pause()
		_mm_pause();
		//same as asm("pause");

		//SwitchToThread(); //causes massive overruns if there is contention - such as dragging my Firefox window around. "about twice as fast as Thread.Sleep(0). yields only to threads on same processor" https://stackoverflow.com/questions/1413630/switchtothread-thread-yield-vs-thread-sleep0-vs-thead-sleep1
		//YieldProcessor(); //causes overruns of 385 us when dragging Firefox. for HyperThreading
		//except...I tried it later. it has the same overruns as without: 0-50 us.
		//https://github.com/microsoft/STL/issues/680

		//future for ARM: https://stackoverflow.com/questions/70069855/is-there-a-yield-intrinsic-on-arm
		//Sleep(0); //causes overruns of 500 us when dragging Firefox
		current_time = now();
	}

#if BENCHMARK_SLEEP
	if (int64_t(current_time - end_time) > 0) outc("spinloop overrun:", (current_time - end_time) * 1000000 / ticks_per_sec, "us");
#endif
}

void accurate_sleep_until(uint64_t end_time) {
#if __linux__ //doesn't need to know the current time
	native_sleep_until_before(end_time);

	uint64_t current_time = now();
	while (int64_t(end_time - current_time) > 0) {
		_mm_pause();
		current_time = now();
	}
#else
	accurate_sleep_until(end_time, now());
#endif
}