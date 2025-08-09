#pragma once

#include <chrono>
#include <stdint.h>
#include "diva.h"

enum Button : int32_t
{
	Button_Triangle    = 0,
	Button_Circle      = 1,
	Button_Cross       = 2,
	Button_Square      = 3,
	Button_Up          = 4,
	Button_Right       = 5,
	Button_Down        = 6,
	Button_Left        = 7,
	Button_L1          = 8,
	Button_L2          = 9,
	Button_L3          = 10,
	Button_L4          = 11,
	Button_R1          = 12,
	Button_R2          = 13,
	Button_R3          = 14,
	Button_R4          = 15,
	// Button_LStickUp    = 16,
	// Button_LStickDown  = 17,
	// Button_LStickLeft  = 18,
	// Button_LStickRight = 19,
	// Button_RStickUp    = 20,
	// Button_RStickDown  = 21,
	// Button_RStickLeft  = 22,
	// Button_RStickRight = 23,
	Button_LStick      = 24,
	Button_RStick      = 25,
	Button_Max,
	Button_None
};

enum Stick : int32_t
{
	Stick_L   = 0,
	Stick_R   = 1,
	Stick_Max = 2
};

struct ButtonState
{
	struct StateData
	{
		bool down{};
		bool up{};
		bool tapped{};
		bool released{};
		std::chrono::steady_clock::time_point time;
	};

	std::vector<StateData> data;

	inline StateData& Push(const std::chrono::steady_clock::time_point& time)
	{
		// Erase data older than 100ms
		// TODO: This could be optimized. This is guaranteed to be in descending order, so we could do a binary search
		// to find the beginning point for notes to erase and remove them all out in one go.
		while (!data.empty() && std::chrono::duration_cast<std::chrono::milliseconds>(time - data.back().time).count() > 100) 
		{
			data.pop_back();
		}

		// Push new note to the front, make sure there is at least 2.
		do 
		{
			data.insert(data.begin(), StateData{false, false, false, false, time});
		} while (data.size() < 2);

		return data[0];
	}
	
	inline bool IsDown() const { return data[0].down; }
	inline bool IsTapped() const { return data[0].tapped; }
	inline bool IsUp() const { return data[0].up; }
	inline bool IsReleased() const { return data[0].released; }
	bool IsTappedInNearFrames() const;
};

struct StickState
{
	float distance;      // NOTE: Distance from rest position (0.0, 0.0)
	float prev_distance;
	bool returning;      // NOTE: Stick is travelling towards rest position
	bool flicked;        // NOTE: Stick was flicked in this frame
	bool flick_block;    // NOTE: Flick action has already been registered
};

struct MacroState
{
	ButtonState buttons[Button_Max];
	StickState sticks[2];
	float hold_sensivity;
	float sensivity;
	int32_t device;
	diva::vec2 stick_deadzone;

	MacroState()
	{
		memset(buttons, 0, sizeof(buttons));
		memset(sticks, 0, sizeof(sticks));
		hold_sensivity = 0.75f;
		sensivity = 0.5f;
		device = InputDevice_Unknown;
		stick_deadzone = { 0.15f, 0.15f };
	}

	bool Update(void* internal_handler, int32_t player_index);
	void UpdateSticks(diva::InputState* input_state, const std::chrono::steady_clock::time_point& time);
	bool GetStarHit() const;
	bool GetDoubleStarHit() const;
	bool GetStarHitCancel() const;

	uint64_t GetDownBitfield() const;
	uint64_t GetTappedBitfield() const;
	uint64_t GetReleasedBitfield() const;
	uint64_t GetTappedInNearFramesBitfield() const;
};

namespace nc
{
	void BlockInputs();
	void UnblockInputs();

	// NOTE: These functions return input even when it's blocked
	bool IsButtonTappedOrRepeat(diva::InputState* input_state, int32_t key);

	void InstallInputHooks();
}

uint64_t GetButtonMask(int32_t button);
uint64_t GetMainButtonsMask();
