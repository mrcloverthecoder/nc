#pragma once

#include <stdint.h>

namespace nc
{
	class Timer
	{
	public:
		void Start();
		void Reset();
		void Stop(bool reset = false);
		bool IsRunning() const;

		// NOTE: Returns seconds ellapsed
		float Ellapsed() const;

	private:
		int64_t start_time = 0;
		bool running = false;
	};
}