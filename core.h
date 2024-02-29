#pragma once
#include <iostream>

#if defined __GNUC__ || defined __llvm__
	#define WS_LIKELY(x)       __builtin_expect(!!(x), 1)
	#define WS_UNLIKELY(x)     __builtin_expect(!!(x), 0)
#else
	#define WS_LIKELY(x)       (x)
	#define WS_UNLIKELY(x)     (x)
#endif

// TODO: muti param
#define WS_ASSERT(x) if(!(x)) { printf("Assertion Fail"); __builtin_trap(); }
#define WS_ASSERT_WITHPARAM(x, ...) if(!(x)) { printf("Assertion Fail: {0}", __VA_ARGS__); __builtin_trap(); }