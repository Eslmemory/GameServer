#pragma once
#include <stdint.h>
#include <syscall.h>

namespace WebServer {
	
	uint32_t GetThreadId();
	uint64_t GetCurrentMS();
}