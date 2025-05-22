#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "nc_time.h"

static int64_t frequency = -1;

static int64_t GetCurrentTicks()
{
	if (frequency == -1)
	{
		LARGE_INTEGER li;
		QueryPerformanceFrequency(&li);
		frequency = li.QuadPart;
	}

	LARGE_INTEGER li;
	QueryPerformanceCounter(&li);
	return li.QuadPart;
}

void nc::Timer::Start()
{
	start_time = GetCurrentTicks();
	running = true;
}

void nc::Timer::Reset()
{
	start_time = GetCurrentTicks();
}

void nc::Timer::Stop(bool reset)
{
	start_time = 0;
	running = false;
}

bool nc::Timer::IsRunning() const { return running; }

float nc::Timer::Ellapsed() const
{
	if (!running)
		return 0.0f;

	return static_cast<float>((GetCurrentTicks() - start_time) / static_cast<double>(frequency));
}