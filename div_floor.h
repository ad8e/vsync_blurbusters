#pragma once
#include <cstdint>

//future: https://stackoverflow.com/a/30824434 is probably better.

//division rounding to positive infinity. positive numbers only please
uint64_t div_ceil(uint64_t a, uint64_t b) {
	//return a/b + (a % b != 0); //https://stackoverflow.com/a/2745763
	return (a + b - 1) / b;
};

//division rounding to positive infinity
int64_t div_ceil(int64_t a, uint64_t b) {
	int64_t b_ = b; //otherwise we get killed by sign conversions
	return int64_t(a / b_) + int64_t(a % b_ > 0); //https://stackoverflow.com/a/30824434
};

//division rounding to negative infinity. can handle negative first value, but not negative second value
int64_t div_floor(int64_t a, uint64_t b) {
	if (a < 0) return -1 - (-1 - a) / b;
	return a / b;
};

uint64_t div_floor(uint64_t a, uint64_t b) {
	return a / b;
};

int div_floor(int a, unsigned b) {
	if (a < 0) return -1 - (-1 - a) / b;
	return a / b;
};

unsigned div_floor(unsigned a, unsigned b) {
	return a / b;
};

inline uint64_t positive_modulo(int64_t a, uint64_t b) {
	return a - div_floor(a, b) * b;
}
