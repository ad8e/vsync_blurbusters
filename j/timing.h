#pragma once
#include <cstdint> //uint64_t
constexpr int64_t one_sec_in_100ns = 10000000;
extern const uint64_t ticks_per_sec; //if I comment this out, Intellisense complains it's undefined. if I don't, Intellisense complains it's ambiguous.
uint64_t now(); //on some systems: 2.5 us per call. https://unseen.in/qc_and_qpc.html
void native_sleep_at_most_100ns(uint64_t ns100); //best API, on Windows
bool sleep_at_most(int64_t ticks); //convenience conversion function
void accurate_sleep_until(uint64_t end_time, uint64_t current_time);
void accurate_sleep_until(uint64_t end_time);
//rule: if you already happen to know the current time, pass it in
//if you don't, then don't call now(). on some platforms (Linux), the now() time is unnecessary. on some platforms, it's necessary and will be called automatically.
void native_sleep_until_before(uint64_t ticks); //only exists on Linux

void improve_timer_resolution_on_Windows();
void reset_timer_resolution_on_Windows();

//future: https://docs.microsoft.com/en-us/windows/win32/dxtecharts/game-timing-and-multicore-processors
//don't call timer from multiple cores. clamp deltas to 0?

//for Windows
#define USE_UNDOCUMENTED_APIS true //the performance is too good to pass up
constexpr int64_t windows_timer_resolution = USE_UNDOCUMENTED_APIS ? 5000 : 10000;
