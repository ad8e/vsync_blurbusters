#pragma once
#define enable_console true
#include <csignal>
#if enable_console
#include <cstdio>
#elif _MSC_VER
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")
#endif
#include <array>
#include <charconv>
#include <cstdlib>
#include <cstring> //memcpy
#include <span>
#include <string>
#include <tuple>
#include <vector>

//ghetto version of unreachable. since meson still doesn't let me configure C++23
//https://en.cppreference.com/w/cpp/utility/unreachable
[[noreturn]] inline void unreachable()
{
#if __GNUC__ // GCC, Clang, ICC
    __builtin_unreachable();
#elif _MSC_VER // MSVC
    __assume(false);
#endif
}

/*
three possible methods:
1. output whatever you receive as you go along. doesn't allow for output into strings
2. cache into a function-local vector, then output at the end
3. cache into a thread-local vector, then output at the end. that's a bad idea, thread_local variables require a lot of synchronization

1 seems to produce lower filesize than 2, 4 KB
2 and 3 are way faster than 1

2 is slightly smaller than 3 in vsync. but in my testcase, 3 is slightly smaller than 2. I guess because there's no threading in my testcase.
*/

#define debug_console_output_choice false
#if debug_console_output_choice
#include <iostream>
#define print_choice(x) std::cout << (x)
#else
#define print_choice(x)
#endif

using std::string_view;

constexpr size_t max_buffer_size = 4096;
//future: perhaps turn position and max_buffer_size into pointers instead. because to_chars likes pointers
//but then it becomes hard to find the remaining buffer capacity. max_size - (ptr - base). so let's just leave it as an integer

void o(string_view s, char* buffer, size_t& position) {
	size_t to_write = std::min(max_buffer_size - position, s.size());
	memcpy(&buffer[position], s.data(), to_write);
	position += to_write;
};

//integers can convert to chars, which is messy. so we rename it o_c instead of o
void o_c(char c, char* buffer, size_t& position) {
	if (position != max_buffer_size) {
		buffer[position] = c;
		++position;
	}
	//std::cout << "char" << c << std::string(1, c);
};

//somehow, converting to std::string at the endpoint is better than converting it beforehand, then passing to string_view.
//probably has something to do with prvalues being ganked by giving them a name.
//well, I switched to std::to_chars. haven't checked the new behavior
template <typename T>
void o_convert(T s_pre_convert, char* buffer, size_t& position) {
	using std::to_chars;
	if (auto [ptr, ec] = to_chars(&buffer[position], &buffer[max_buffer_size], s_pre_convert); ec == std::errc()) {
		position += ptr - &buffer[position];
	}
	else {
		//whatever. it's a harmless error then, it's clearly too long
	}
	//std::cout << "conv" << std::to_string(s_pre_convert);
};

//get rid of me after gcc std::to_chars starts supporting floats
template <typename T>
void o_convert_string(T s_pre_convert, char* buffer, size_t& position) {
	using std::to_string;
	std::string s = to_string(s_pre_convert);
	auto to_write = std::min(max_buffer_size - position, s.size());
	memcpy(&buffer[position], s.data(), to_write);
	position += to_write;
};

//works with any to_chars() in std:: or in global scope
template <typename T>
concept convertible_std_to_chars = requires(T a) { std::to_chars(nullptr, nullptr, a); };
template <typename T>
concept convertible_blank_to_chars = requires(T a) { to_chars(nullptr, nullptr, a); };
template <typename T>
concept convertible_to_chars = convertible_std_to_chars<T> || convertible_blank_to_chars<T>;

struct no_space {
} ns; //eliminate automatic whitespace. use outc("abc", ns, "def");

//both before and after need to agree that a space should appear, for a space to be added
inline constexpr char no_whitespace_before[] = {'@', '/', '\\', '?', '!', '.', ',', ';', ':', ')', ']', '}', '\'', '\"', '\r', '\n', '\t', ' '};
inline constexpr char no_whitespace_after[] = {'\r', '\n', '\t', '{', '[', '(', '#', '$', '/', '\\', ' '}; //having ' ' in there means zero spaces to auto-space, one space to force a space, two spaces to create an extra space. forcing spaces is useful for arbitrary symbols

//we could label it constexpr to only manage whitespace for constexpr (compile-time) objects
//but that would be unexpected for runtime users trying to switch strings
//future: if the string is known at compile time, append ' ' to it instead of outputting twice
//can't use std::ranges because std::array<> doesn't allow for unknown size initialization, and [] has no end()
bool whitespace_before(std::string_view str) {
	if (str.empty()) return false;
	for (char n : no_whitespace_before)
		if (str.front() == n) return false;
	return true;
}

bool whitespace_after(std::string_view str) {
	if (str.empty()) return false;
	for (char n : no_whitespace_after)
		if (str.back() == n) return false;
	return true;
}

//needed, for '\n'
bool whitespace_before(char c) {
	for (char n : no_whitespace_before)
		if (c == n) return false;
	return true;
}

bool whitespace_after(char c) {
	for (char n : no_whitespace_after)
		if (c == n) return false;
	return true;
}

template <typename T>
concept meaningful_to_check_whitespace = requires(T a) { whitespace_before(std::string_view(a)); } || std::is_same_v<T, char> || std::is_same_v<T, unsigned char>;

/*
template <typename T>
concept is_tuple = requires(T a) {
	std::tuple_size_v<T>; //seems to fail, returning true for arrays. and also causes problems for any if statement that comes below the tuple constexpr if. I don't really understand concepts, I guess
	//std::get<0>(a); //will fail for empty tuples. we want to detect both tuples and pairs, and this detects both. also seems to return true for arrays
	//std::tuple_element<0>(a); //also returns true for arrays
};*/

//https://stackoverflow.com/a/48458312
template <typename>
struct is_tuple : std::false_type {};
template <typename... T>
struct is_tuple<std::tuple<T...>> : std::true_type {};
template <typename A, typename B>
struct is_tuple<std::pair<A, B>> : std::true_type {};

//helper constant for variant type. https://en.cppreference.com/w/cpp/utility/variant/visit
template <typename>
inline constexpr bool dependent_false = false;

//pointers should be printed in hex, because special constants (like BAADF00D) are in hex
//we offset from an arbitrary base address inside the process memory. this gives shorter (readable) addresses. but we only do it in debug mode, as it has a performance penalty
//if it's a special pointer constant, like nullptr or 0xBAADF00D, we don't offset, and print as-is.
#if !NDEBUG
//#include <algorithm>
int* arbitrary_memory_to_offset_pointer_addresses_from = new int;
#endif
void o_convert_pointer(uintptr_t w, char* buffer, size_t& position) {
#if !NDEBUG
	//std::array special_constants{0ull, 0xABABABABABABABAB, 0xBAADF00DBAADF00D, 0xFEEEFEEEFEEEFEEE};
	//if (std::find(special_constants.begin(), special_constants.end(), w) == special_constants.end())
	//	w -= uintptr_t(arbitrary_memory_to_offset_pointer_addresses_from);

	//other idea: test if the distance to the base pointer is more than 2^32. if so, it's a special pointer.
	//faster. accommodates unlisted special pointers
	//fails for large programs.
	//with 32-bit, we'd need other special constants anyway.
	//there is std::make_signed, but we won't use it for now.
	//might fail for nullptr if addresses are too small?
	if (std::abs(intptr_t(w - intptr_t(arbitrary_memory_to_offset_pointer_addresses_from))) < 1ll << 32)
		w -= intptr_t(arbitrary_memory_to_offset_pointer_addresses_from);
#endif
	using std::to_chars;
	if (auto [ptr, ec] = to_chars(&buffer[position], &buffer[max_buffer_size], (intptr_t)w, 16); ec == std::errc()) {
		position += ptr - &buffer[position];
	}
	else {
		//whatever. it's a harmless error then, it's clearly too long
	}
};

void outc_internal(bool insert_space, char* buffer, size_t& position) {
	return;
}

//insert_space is "if necessary". so if the next thing starts with a space, don't insert a space.
//return true if this term wants whitespace after it. because we recurse on tuples, so we need to know what happened inside
template <typename T, typename... Args>
void outc_internal(bool insert_space, char* buffer, size_t& position, T s, Args&&... args) {
	bool wsa = true;
	using type = std::remove_cvref_t<T>;
	//nuking this costs 1 KB, so it's not the source of the extra file size
	if constexpr (meaningful_to_check_whitespace<type>) {
		if (insert_space && whitespace_before(s))
			o_c(' ', buffer, position);
		wsa = whitespace_after(s);
	}
	else if (insert_space)
		o_c(' ', buffer, position);

	if constexpr (std::is_same_v<type, no_space>) {
		wsa = false;
	}
	else if constexpr (requires { o(s, buffer, position); }) { //callable directly.
		//else if constexpr (requires { o(s, buffer, position); }) {
		//we can't use a freestanding concept, because position is a reference.
		//and, seems like you can't declare variables inside a concept
		print_choice("direct");
		o(s, buffer, position);
	}
	else if constexpr (std::is_same_v<type, char> || std::is_same_v<type, unsigned char>) {
		print_choice("direct char");
		o_c(s, buffer, position);
	}
	else if constexpr (std::is_same_v<type, bool>) { //to_chars disallows bools. so we'll do it manually
		print_choice("bool");
		o(s ? "true" : "false", buffer, position);
	}
	else if constexpr (convertible_to_chars<type>) {
		print_choice("convert");
		o_convert(s, buffer, position);
	}
	else if constexpr (requires { to_string(s); }) {
		print_choice("convert string");
		o_convert_string(s, buffer, position);
		//static_assert(dependent_false<T>, "string conversions cause a lot of file size, this catches them. somehow, only a few conversions are causing a lot of trouble");
	}
	//else if constexpr (requires { std::span<typename type::value_type>(s); }) { //viewable through span
	//	print_choice("span");
	//	bool first = true;
	//	for (auto& x : s) {
	//		if (first) {
	//			outc_internal(false, buffer, position, x); //we already output a space, don't output another
	//			first = false;
	//		}
	//		else {
	//			outc_internal(true, buffer, position, x);
	//		}
	//	}
	//}
	else if constexpr (requires { s.begin(); s.end(); }) { //range-based for
		print_choice("range-based");
		bool first = true;
		for (auto& x : s) {
			if (first) {
				outc_internal(false, buffer, position, x);
				first = false;
			}
			else {
				outc_internal(true, buffer, position, x);
			}
		}
	}
	else if constexpr (std::is_pointer<type>::value) {
		o_convert_pointer(uintptr_t(s), buffer, position);
	}
	else if constexpr (is_tuple<type>::value) {
		print_choice("tuple");
		bool false_once_then_true_after = false;
		auto lambda_for_tuple_argument = [&](const auto& tuple_argument) {
			outc_internal(false_once_then_true_after, buffer, position, tuple_argument);
			false_once_then_true_after = true;
		};
		std::apply([&](auto&... x) { (..., lambda_for_tuple_argument(x)); }, s); //https://stackoverflow.com/a/45498003
	}
	else {
		static_assert(dependent_false<T>, "didn't match any types in console.h");
	}
	if (sizeof...(args) > 0)
		outc_internal(wsa, buffer, position, std::forward<Args>(args)...);
};

template <typename... Args>
void outc(Args&&... args) {
	char buffer[max_buffer_size]; //change the buffer, then write everything out at once.
	size_t position = 0;

	outc_internal(false, buffer, position, std::forward<Args>(args)..., '\n'); //strings only print in emscripten when they end in \n
#if !NDEBUG
	fflush(stdout); //without this, a crash in msys2 doesn't output the console messages
#endif

#if enable_console
	//fwrite is atomic on POSIX and Windows.
	//https://stackoverflow.com/a/2220574
	fwrite(buffer, sizeof(char), position, stdout);
	//C++ iostreams suck with emscripten. and in general too.
	//EM_ASM_({console.log(UTF8ToString($0))}, s.c_str());
#endif
}

//same, but no newline. the n is for newline (or no newline)
template <typename... Args>
void outn(Args&&... args) {
	char buffer[max_buffer_size];
	size_t position = 0;
	outc_internal(false, buffer, position, std::forward<Args>(args)...);
#if enable_console
	fwrite(buffer, sizeof(char), position, stdout);
#endif
}

template <typename... Args>
std::string out_string(Args&&... args) {
	char buffer[max_buffer_size]; //might as well start with a char buffer. we have to shrink it afterward anyway, which requires a memory operation
	size_t position = 0;
	outc_internal(false, buffer, position, std::forward<Args>(args)...);
	return std::string(buffer, buffer + position);
}

/*
//probably clobbers something. causes segfault
#ifndef _MSC_VER
#define error(...) outc(__VA_ARGS__); __builtin_trap(); unreachable()
#else
#define error(...) outc(__VA_ARGS__); std::raise(SIGABRT)
#endif
*/

//noreturn silences llvm warning about control reaching end without returning
//note: check() copy-pastes this. if you edit this, edit check() too.
template <typename... Args>
[[noreturn]] inline void error(Args&&... args) {
	outc(std::forward<Args>(args)...);
	//maybe flush?
#ifndef _MSC_VER
	__builtin_trap(); //zero frames added. used to lose frame information when compiled with clang, but seems fixed with clang 6.0.
	unreachable(); //silence noreturn warnings
#else
	std::raise(SIGABRT); //1 frame added
	//abort(); //adds two frames to the debugger
	//throw; //adds a ton of stack frames
	//assert(false); //adds a huge number of useless stack frames
	//__debugbreak(); //todo: test this
#endif
}

/*
//clobbers anything named check, which happens in boost
#define check(b, ...) \
	{if (!b) { error(__VA_ARGS__); }}

#define check(b) \
	{if (!b) { error(); }}
*/

//note: copy-pasted from error(), in order to omit 1 frame on stacktrace. if you edit this, edit error() too.
template <typename... Args>
inline void check(bool b, Args&&... args) {
	if (!b) [[unlikely]] {
		outc(std::forward<Args>(args)...);
		//maybe flush?
#ifndef _MSC_VER
		__builtin_trap();
		unreachable();
#else
		std::raise(SIGABRT);
#endif
	}
}

/*
template <typename... Args>
inline void check(bool b, Args&&... args) {
	if (!b) error(std::forward<Args>(args)...);
}*/

#ifndef NDEBUG
#define error_assert(...) error(__VA_ARGS__)
//#include <source_location> //compiler complains
//#define error_assert(...) (outc(std::source_location::current().function_name(), std::source_location::current().line()), error(__VA_ARGS__ ))
#else
#define error_assert(...) unreachable()
#endif
#define check_assert(b, ...) \
	if (!(b)) [[unlikely]] \
	error_assert(__VA_ARGS__)
#define output_line_number outc(std::source_location::current().function_name(), std::source_location::current().line())
/*
template <typename... Args>
inline void check_assert(bool b, Args&&... args) {
	if (!b) error_assert(std::forward<Args>(args)...);
}*/
