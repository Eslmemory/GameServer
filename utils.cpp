#include "utils.h"
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>

namespace WebServer {

	uint32_t GetThreadId() {
		return syscall(SYS_gettid);
	}

	uint64_t GetCurrentMS() {
		struct timeval tv;
		gettimeofday(&tv, nullptr);
		return tv.tv_sec * 1000ul + tv.tv_usec / 1000;
	}
}