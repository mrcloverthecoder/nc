#include <chrono>
#include <unordered_map>
#include <bitset>
#include <array>
#include <string.h>
#include "hooks.h"
#include "diva.h"
#include "input.h"
#include "nc_log.h"

constexpr int32_t NearFramesBaseCount = 3;
constexpr float NearFramesBaseRate = 60.0f;
constexpr size_t MaxKeyCount = 0xA2;

struct RepeatTapMemory
{
	float float00;
	float float04;
	int32_t dword08;
	int32_t dword0C;

	RepeatTapMemory()
	{
		float00 = 3.0f;
		float04 = 40.0f;
		dword08 = 0;
		dword0C = 0;
	}
};

struct InputStateExData
{
	std::bitset<MaxKeyCount> down;
	std::bitset<MaxKeyCount> repeat_tap;
	std::array<RepeatTapMemory, MaxKeyCount> repeat_tap_memory;
	bool blocked;

	InputStateExData()
	{
		repeat_tap_memory.fill(RepeatTapMemory());
		blocked = false;
	}

	void ResetMultiFrameInputs()
	{
		for (int i = 0; i < MaxKeyCount; i++)
		{
			repeat_tap_memory[i].dword08 = 0;
			repeat_tap_memory[i].dword0C = 0;
		}
	}
};

static_assert(sizeof(std::bitset<MaxKeyCount>) == 0x18);

// NOTE: Using a pointer as key is kinda hacky I guess, but the input state buffer
//       is allocated on the heap at startup and only freed on exit, so if the pointer
//       becomes invalid in this period that isn't a issue for us to deal with!
static std::unordered_map<diva::InputState*, InputStateExData> input_ex_data;

enum ButtonIndex : int32_t
{
	ButtonIndex_Circle = 0,
	ButtonIndex_Cross = 1,
	ButtonIndex_Triangle = 2,
	ButtonIndex_Square = 3,
	ButtonIndex_L = 4,
	ButtonIndex_05 = 5,
	ButtonIndex_06 = 6,
	ButtonIndex_R = 7
};

enum GameButton : int32_t
{
	GameButton_Triangle = 1,
	GameButton_Up = 2,
	GameButton_Square = 4,
	GameButton_Left = 8,
	GameButton_Cross = 16,
	GameButton_Down = 32,
	GameButton_Circle = 64,
	GameButton_Right = 128,
	GameButton_L1 = 256,   // Q
	GameButton_L2 = 512,   // U
	GameButton_L3 = 1024,  // Left Arrow
	GameButton_L4 = 2048,  // R
	GameButton_R1 = 4096,  // E
	GameButton_R2 = 8192,  // O
	GameButton_R3 = 16384, // Right Arrow
	GameButton_R4 = 32768, // Y
};

static FUNCTION_PTR(int64_t, __fastcall, GetPvKeyStateDown, 0x140274930, void* handler, int32_t key);
static FUNCTION_PTR(int64_t, __fastcall, GetPvKeyStateTapped, 0x140274960, void* handler, int32_t key);
static FUNCTION_PTR(bool, __fastcall, PollRepeatTapInput, 0x1402AA650, RepeatTapMemory* a1, float a2, bool is_down);

bool ButtonState::IsTappedInNearFrames() const
{
	// 3 frames, so 3 times 16.66ms ??? Let's make it 50ms
	bool mask = false;

	for (auto& state : data)
	{
		if (std::chrono::duration_cast<std::chrono::milliseconds>(data[0].time - state.time).count() > 50)
			break;

		mask |= state.tapped;
	}

	return mask;
}

static void UpdateButtonState(ButtonState* state, const int64_t* buffer, int32_t index, int32_t mask)
{
	state->data[0].down = (buffer[index] & mask) != 0;
	state->data[0].up = !state->data[0].down;
}

bool MacroState::Update(void* internal_handler, int32_t player_index)
{
	if (internal_handler == nullptr)
		return false;

	diva::InputState* diva_input = diva::GetInputState(player_index);
	device = diva_input->GetDevice();

	auto time = std::chrono::steady_clock::now();

	// NOTE: Update buttons
	for (int32_t i = 0; i < Button_Max; i++)
		buttons[i].Push(time);

	int64_t key_states[8];
	for (int i = 0; i < 8; i++)
		key_states[i] = GetPvKeyStateDown(internal_handler, i);

	UpdateButtonState(&buttons[Button_Triangle], key_states, ButtonIndex_Triangle, GameButton_Triangle);
	UpdateButtonState(&buttons[Button_Circle], key_states, ButtonIndex_Circle, GameButton_Circle);
	UpdateButtonState(&buttons[Button_Cross], key_states, ButtonIndex_Cross, GameButton_Cross);
	UpdateButtonState(&buttons[Button_Square], key_states, ButtonIndex_Square, GameButton_Square);
	UpdateButtonState(&buttons[Button_Up], key_states, ButtonIndex_Triangle, GameButton_Up);
	UpdateButtonState(&buttons[Button_Right], key_states, ButtonIndex_Circle, GameButton_Right);
	UpdateButtonState(&buttons[Button_Down], key_states, ButtonIndex_Cross, GameButton_Down);
	UpdateButtonState(&buttons[Button_Left], key_states, ButtonIndex_Square, GameButton_Left);
	UpdateButtonState(&buttons[Button_L1], key_states, ButtonIndex_L, GameButton_L1);
	UpdateButtonState(&buttons[Button_L2], key_states, ButtonIndex_L, GameButton_L2);
	UpdateButtonState(&buttons[Button_L3], key_states, ButtonIndex_L, GameButton_L3);
	UpdateButtonState(&buttons[Button_L4], key_states, ButtonIndex_L, GameButton_L4);
	UpdateButtonState(&buttons[Button_R1], key_states, ButtonIndex_R, GameButton_R1);
	UpdateButtonState(&buttons[Button_R2], key_states, ButtonIndex_R, GameButton_R2);
	UpdateButtonState(&buttons[Button_R3], key_states, ButtonIndex_R, GameButton_R3);
	UpdateButtonState(&buttons[Button_R4], key_states, ButtonIndex_R, GameButton_R4);

	for (int i = 0; i < Button_Max; i++)
	{
		buttons[i].data[0].tapped = buttons[i].data[0].down && buttons[i].data[1].up;
		buttons[i].data[0].released = buttons[i].data[0].up && buttons[i].data[1].down;
	}

	UpdateSticks(diva_input, time);
	return true;
}

void MacroState::UpdateSticks(diva::InputState* input_state, const std::chrono::steady_clock::time_point& time)
{
	auto checkDeadzoned = [&](const diva::vec2& pos)
	{
		return pos.x <= stick_deadzone.x && pos.y <= stick_deadzone.y
			&& pos.x >= -stick_deadzone.x && pos.y >= -stick_deadzone.y;
	};

	auto updateStick = [&](int32_t index)
	{
		StickState* state = &sticks[index];
		diva::vec2 pos = {
			input_state->GetPosition(0x14 + index * 2 + 0),
			input_state->GetPosition(0x14 + index * 2 + 1)
		};

		state->prev_distance = state->distance;
		state->distance = checkDeadzoned(pos) ? 0.0f : pos.length();
		state->returning = state->distance < state->prev_distance;
		state->flicked = false;

		if (!state->returning)
		{
			state->flicked = state->distance >= sensivity && !state->flick_block;
			if (state->flicked)
				state->flick_block = true;
		}
		else
		{
			if (state->distance < sensivity)
				state->flick_block = false;
		}
	};

	auto updateStickButtonState = [&](int32_t index)
	{
		StickState* state = &sticks[index];
		auto& button = buttons[Button_LStick + index].data[0];

		button.down = state->distance >= sensivity;
		button.up = !button.down;
		button.tapped = state->flicked;
		// button.released = !button.tapped;
		button.time = time;
	};

	updateStick(Stick_L);
	updateStick(Stick_R);
	updateStickButtonState(Stick_L);
	updateStickButtonState(Stick_R);
}

bool MacroState::GetStarHit() const
{
	if (diva::GetInputState(0)->GetDevice() != InputDevice_Keyboard)
		return buttons[Button_LStick].IsTapped() || buttons[Button_RStick].IsTapped();

	return buttons[Button_L1].IsTapped() ||
		buttons[Button_L2].IsTapped() ||
		buttons[Button_L3].IsTapped() ||
		buttons[Button_L4].IsTapped() ||
		buttons[Button_R1].IsTapped() ||
		buttons[Button_R2].IsTapped() ||
		buttons[Button_R3].IsTapped() ||
		buttons[Button_R4].IsTapped();
}

bool MacroState::GetDoubleStarHit() const
{
	if (diva::GetInputState(0)->GetDevice() != InputDevice_Keyboard)
	{
		return (buttons[Button_LStick].IsTapped() && buttons[Button_RStick].IsTappedInNearFrames()) ||
			(buttons[Button_RStick].IsTapped() && buttons[Button_LStick].IsTappedInNearFrames());
	}

	const int32_t buttons_left[] = {
		Button_L1, Button_L2, Button_L3, Button_L4
	};

	const int32_t buttons_right[] = {
		Button_R1, Button_R2, Button_R3, Button_R4
	};

	const ButtonState* l = nullptr;
	const ButtonState* r = nullptr;

	for (int32_t i = 0; i < 4; i++)
		if (buttons[buttons_left[i]].IsDown())
			l = &buttons[buttons_left[i]];

	for (int32_t i = 0; i < 4; i++)
		if (buttons[buttons_right[i]].IsDown())
			r = &buttons[buttons_right[i]];

	if (l != nullptr && r != nullptr)
	{
		return (l->IsTapped() && r->IsTappedInNearFrames()) ||
			(r->IsTapped() && l->IsTappedInNearFrames());
	}

	return false;
}

HOOK(void, __fastcall, SetInputNotDispatchable, 0x1402AC250, diva::InputState* input_state, bool blocked)
{
	originalSetInputNotDispatchable(input_state, blocked || input_ex_data[input_state].blocked);
}

// NOTE: Blocking inputs break repeat and other multi-frame inputs, so we have to handle that externally
HOOK(void, __fastcall, PollInputRepeatAndDouble, 0x1402ABD90, diva::InputState* input_state, float a2)
{
	originalPollInputRepeatAndDouble(input_state, a2);

	auto& ex = input_ex_data[input_state];
	memcpy(&ex.down, &input_state->_data[0x30], 0x18);
	memcpy(&ex.repeat_tap, &input_state->_data[0x90], 0x18);

	if (ex.blocked)
	{
		memset(&ex.repeat_tap, 0, sizeof(ex.repeat_tap));
		for (int32_t i = 0; i < MaxKeyCount; i++)
		{
			if (PollRepeatTapInput(&ex.repeat_tap_memory[i], a2, ex.down[i]))
				ex.repeat_tap[i] = true;
		}
	}
	else
		ex.ResetMultiFrameInputs();
}

void nc::BlockInputs()
{
	for (auto& [ptr, state] : input_ex_data)
		state.blocked = true;
}

void nc::UnblockInputs()
{
	for (auto& [ptr, state] : input_ex_data)
		state.blocked = false;
}

static bool IsButtonTappedOrRepeatImp(diva::InputState* input_state, int32_t key)
{
	if (key >= 0xA2)
		return false;
	return input_ex_data[input_state].repeat_tap[key];
}

bool nc::IsButtonTappedOrRepeat(diva::InputState* input_state, int32_t key)
{
	if (!input_ex_data[input_state].blocked)
		return input_state->IsButtonTappedOrRepeat(key);
	return diva::CheckButtonDelegate(input_state, key, IsButtonTappedOrRepeatImp);
}

void nc::InstallInputHooks()
{
	INSTALL_HOOK(SetInputNotDispatchable);
	INSTALL_HOOK(PollInputRepeatAndDouble);
}

bool MacroState::GetStarHitCancel() const
{
	uint64_t left = GetButtonMask(Button_L1) |
		GetButtonMask(Button_L2) |
		GetButtonMask(Button_L3) |
		GetButtonMask(Button_L4) |
		GetButtonMask(Button_LStick);

	uint64_t right = GetButtonMask(Button_R1) |
		GetButtonMask(Button_R2) |
		GetButtonMask(Button_R3) |
		GetButtonMask(Button_R4) |
		GetButtonMask(Button_RStick);

	return (GetDownBitfield() & left) != 0 && (GetDownBitfield() & right) != 0;
}

uint64_t MacroState::GetDownBitfield() const
{
	uint64_t mask = 0;
	for (size_t i = 0; i < Button_Max; i++)
		mask |= static_cast<uint64_t>(buttons[i].IsDown()) << i;
	return mask;
}

uint64_t MacroState::GetTappedBitfield() const
{
	uint64_t mask = 0;
	for (size_t i = 0; i < Button_Max; i++)
		mask |= static_cast<uint64_t>(buttons[i].IsTapped()) << i;
	return mask;
}

uint64_t MacroState::GetReleasedBitfield() const
{
	uint64_t mask = 0;
	for (size_t i = 0; i < Button_Max; i++)
		mask |= static_cast<uint64_t>(buttons[i].IsReleased()) << i;
	return mask;
}

uint64_t MacroState::GetTappedInNearFramesBitfield() const
{
	uint64_t mask = 0;
	for (size_t i = 0; i < Button_Max; i++)
		mask |= static_cast<uint64_t>(buttons[i].IsTappedInNearFrames()) << i;
	return mask;
}

uint64_t GetButtonMask(int32_t button)
{
	if (button < 0 || button > Button_Max)
		return 0;
	else if (button == Button_Max)
	{
		uint64_t mask = 0;
		for (size_t i = 0; i < Button_Max; i++)
			mask |= static_cast<uint64_t>(1) << i;
		return mask;
	}

	return static_cast<uint64_t>(1) << static_cast<uint64_t>(button);
}

uint64_t GetMainButtonsMask()
{
	uint64_t mask = 0;
	for (int i = 0; i < 8; i++)
		mask |= GetButtonMask(i);
	return mask;
}