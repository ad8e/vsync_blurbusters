#pragma once
#include <array>
#include <cmath>
#include <numbers>
#include <ranges>
#include <span>
#include <vector>

using std::array;
using std::pair;
using std::span;
using std::string_view;
using std::tuple;
using std::vector;
using uint = unsigned;
using u32 = uint32_t;
using u64 = uint64_t;
using int32 = int32_t;
using int64 = int64_t;

#ifndef single_def
#define single_def
//#define single_def inline
#endif

auto zero_to(unsigned n) { return std::ranges::views::iota(0u, n); }
