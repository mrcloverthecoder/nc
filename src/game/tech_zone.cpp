#include <nc_state.h>
#include <nc_log.h>
#include <save_data.h>
#include "hit_state.h"
#include "tech_zone.h"

enum TechZoneAction : int32_t
{
	TechZoneAction_Idle    = 0,
	TechZoneAction_InWait  = 1,
	TechZoneAction_Loop    = 2,
	TechZoneAction_EndWait = 3,
};

bool TechZoneState::IsValid() const
{
	return first_target_index != -1 && last_target_index != -1;
}

bool TechZoneState::IsSuccessful() const
{
	return IsValid() && !failed && GetRemainingCount() == 0;
}

int32_t TechZoneState::GetTargetCount() const
{
	if (!IsValid())
		return 0;
	return last_target_index - first_target_index + 1;
}

int32_t TechZoneState::GetRemainingCount() const
{
	if (!IsValid())
		return 0;
	return GetTargetCount() - targets_hit;
}

bool TechZoneState::PushNewHitState(int32_t target_index, int32_t hit_state)
{
	if (!IsValid())
		return false;

	if (target_index >= first_target_index && target_index <= last_target_index)
	{
		if (nc::IsHitGreat(hit_state) && !failed)
			targets_hit++;
		else
			failed = true;
	}
}

void TechZoneState::ResetPlayState()
{
	targets_hit = 0;
	failed = false;
}

void TechZoneDispState::Reset()
{
	data = nullptr;
	scene = 0;
	layer_name.clear();
	state = 0;
	end = false;
}

void TechZoneDispState::Ctrl()
{
	if (!data)
		return;

	if (scene == 0 || layer_name.empty())
	{
		// TODO: Implement song default / match UI modes (although for the latter one we first
		//       need to actually add FT and M39 tech zone skins)
		constexpr uint32_t scene_ids[3] = { 14010081, 14010082, 14010083 };
		int32_t scene_index = nc::GetSharedData().tech_zone_style;

		switch (nc::GetSharedData().tech_zone_style)
		{
		case TechZoneStyle_F:
		case TechZoneStyle_F2nd:
		case TechZoneStyle_X:
			layer_name = "bonus_zone";
			scene = scene_ids[nc::GetSharedData().tech_zone_style];
			break;
		}
	}

	std::shared_ptr<AetElement>& tz = ::state.ui.GetLayer(LayerUI_BonusZone);
	std::shared_ptr<AetElement>& txt = ::state.ui.GetLayer(LayerUI_BonusZoneText);
	tz->SetScene(scene);
	txt->SetScene(scene);

	switch (this->state)
	{
	case TechZoneAction_Idle:
		if (data)
		{
			tz->SetLayer(layer_name, prio, 14, AetAction_InOnce);
			txt->SetVisible(true);
			txt->SetLayer("bonus_start_txt", prio, 14, AetAction_None);
			fail_in = false;
			state = TechZoneAction_InWait;
		}

		break;
	case TechZoneAction_InWait:
		if (tz->Ended())
		{
			tz->SetMarkers("st_lp", "ed_lp", true);
			state = TechZoneAction_Loop;
		}
		break;
	case TechZoneAction_Loop:
		if (data->failed)
		{
			if (!fail_in)
			{
				tz->SetMarkers("st_fail_in", "ed_fail_in", false);
				fail_in = true;
			}
			else if (fail_in && tz->Ended())
				tz->SetMarkers("st_fail_lp", "ed_fail_lp", true);
		}

		if (end) // It's ready to end, A.K.A. MODE_SELECT(X, 9) was called.
		{
			if (data->failed)
			{
				tz->SetMarkers("st_fail_out", "ed_fail_out", false);
				txt->SetVisible(true);
				txt->SetLayer("bonus_end_txt", prio, 14, AetAction_None);
			}
			else if (data->IsSuccessful())
			{
				tz->SetMarkers("st_clear", "ed_clear", false);
				txt->SetVisible(true);
				txt->SetLayer("bonus_complete_txt", prio, 14, AetAction_None);
			}
			else
				tz->SetMarkers("st_out", "ed_out", false);

			state = TechZoneAction_EndWait;
		}

		break;
	case TechZoneAction_EndWait:
		if (tz->Ended())
		{
			data = nullptr;
			state = TechZoneAction_Idle;
		}

		break;
	}

	if (txt->Ended())
		txt->SetVisible(false);
}

static int32_t GetMaxNumber(int32_t max_digits)
{
	int32_t dec = 1;
	int32_t num = 0;
	for (int i = 0; i < max_digits; i++)
	{
		num += 9 * dec;
		dec *= 10;
	}

	return num;
}

static void DrawNumberWithF2ndFont(int32_t value, int32_t max_digits, diva::vec3 pos, int32_t prio, bool fail)
{
	constexpr uint32_t sprite_id = 776507282;
	const diva::vec2 glyph_size = { 60.0f, 64.0f };
	const diva::vec2 size = { 45.0f, 48.0f };
	const int32_t color = fail ? 0xFF7F7F7F : 0xFFFFFFFF;

	if (int32_t max = GetMaxNumber(max_digits); value > max)
		value = max;

	for (int i = 0; i < max_digits; i++)
	{
		int32_t digit = value - value / 10 * 10;
		value /= 10;

		// NOTE: Discard leading zeroes
		if (digit == 0 && value == 0 && i > 0)
			break;

		SprArgs spr_args = { };
		spr_args.id = sprite_id;
		spr_args.resolution_mode_screen = 14;
		spr_args.resolution_mode_sprite = 14;
		spr_args.priority = prio;
		spr_args.attr = 0x100000; // SPR_ATTR_CTR_RT
		memcpy(spr_args.color, &color, 4);
		spr_args.trans = { pos.x - size.x * i, pos.y, 0.0f };
		spr_args.flags = 0x03;
		spr_args.texture_pos = { glyph_size.x * digit, 0.0f };
		spr_args.texture_size = { glyph_size.x, glyph_size.y };
		spr_args.sprite_size = { size.x, size.y };
		spr::DrawSprite(&spr_args);
	}
}

void TechZoneDispState::Disp() const
{
	if (!data)
		return;

	std::shared_ptr<AetElement>& tz = ::state.ui.GetLayer(LayerUI_BonusZone);
	if (auto layout = tz->GetLayout("p_notes_num_rt"); layout.has_value())
		DrawNumberWithF2ndFont(data->GetRemainingCount(), 3, layout.value().position, prio + 1, data->failed);
}