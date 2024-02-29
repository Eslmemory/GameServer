#pragma once
#include <memory>

namespace WebServer {
	template<typename T>
	class Singleton {
	public:
		static T* GetInstance() {
			static T instance;
			return &instance;
		}
	};
}