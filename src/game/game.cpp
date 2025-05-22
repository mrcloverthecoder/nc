#include <stdint.h>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <detours.h>
#include <hooks.h>
#include <nc_state.h>
#include <nc_log.h>
#include <save_data.h>
#include "target.h"
#include "chance_time.h"
#include "hit_state.h"
#include "score.h"

static void ProcessNoteHit(const PvGameTarget& tgt, TargetStateEx& ex, int32_t hit_state);

/*
HOOK(int32_t, __fastcall, GetHitState, 0x14026BF60,
	PVGameArcade* game,
	bool* play_default_se,
	size_t* rating_count,
	diva::vec2* rating_pos,
	int32_t* a5,
	SoundEffect* se,
	int32_t* multi_count,
	float* player_hit_time,
	int32_t* target_index,
	bool* is_success_note,
	bool* slide,
	bool* slide_chain,
	bool* slide_chain_start,
	bool* slide_chain_max,
	bool* slide_chain_continues,
	void* a16)
{
	int32_t final_hit_state = HitState_None;
	bool should_play_star_se = true;
	bool schedule_star_se = false;

	// NOTE: Update input manager
	macro_state.Update(game->ptr08, 0);
	se_mgr.UpdateSchedules();

	if (ShouldUpdateTargets())
	{
		for (TargetStateEx* tgt : state.target_references)
		{
			// NOTE: Poll input for ongoing long notes
			if (tgt->IsLongNoteStart() && tgt->holding)
			{
				bool is_in_zone = false;

				// NOTE: Check if the end target is in it's timing window
				if (tgt->next->org != nullptr)
				{
					float time = tgt->next->org->flying_time_remaining;
					is_in_zone = time >= game->sad_late_window && time <= game->sad_early_window;
				}

				score::CalculateSustainBonus(tgt);
				GetPVGameData()->ui.SetBonusText(tgt->parent->bonus_score + tgt->parent->ct_bonus_score, tgt->target_pos);

				// NOTE: Check if the start target button has been released;
				//       if it's the end note is not inside it's timing zone,
				//       automatically mark it as a fail.
				if (!nc::CheckLongNoteHolding(tgt) && !is_in_zone)
				{
					tgt->next->force_hit_state = HitState_Worst;
					tgt->StopAet();
					tgt->holding = false;
					state.PopTarget(tgt);
					se_mgr.EndLongSE(true);
					GetPVGameData()->ui.RemoveBonusText();
				}
			}
			// NOTE: Poll input for ongoing rush notes
			else if (tgt->IsRushNote() && tgt->holding)
			{
				if (nc::CheckRushNotePops(tgt))
				{
					GetPVGameData()->score += score::IncreaseRushPopCount(tgt);
					GetPVGameData()->ui.SetBonusText(tgt->parent->bonus_score, tgt->target_pos);
					state.PlayRushHitEffect(GetScaledPosition(tgt->target_pos), 0.6f * (1.0f + tgt->bal_scale), false);

					if (tgt->target_type == TargetType_StarRush)
					{
						se_mgr.PlayStarSE();
						game->mute_slide_chime = true;
						*play_default_se = false;
					}
				}
			}
		}
	}

	// NOTE: Determine which targets to be updated and what type are them (custom or vanilla)
	PvGameTarget* group[4] = { nullptr, nullptr, nullptr, nullptr };
	int32_t group_count = 0;
	bool has_custom_note = false;
	bool has_vanilla_note = false;

	for (PvGameTarget* target = game->target; target != nullptr; target = target->next)
	{
		if (group_count < 4)
		{
			group[group_count++] = target;
			has_custom_note |= target->target_type >= TargetType_Custom;
			has_vanilla_note |= target->target_type < TargetType_Custom;
		}

		if (target->multi_count == -1 || target->multi_count != game->target->multi_count)
			break;
	}

	// NOTE: Issue a warning if there's a multi mixing custom and vanilla notes,
	//       as that is not currently supported.
	if (has_custom_note && has_vanilla_note)
	{
		nc::Print("Target #%d is a multi note that mixes Vanilla and custom notes. That is not supported and might cause some issues.", group[0]->target_index);
	}

	// NOTE: Check sound prio
	int32_t snd_prio = nc::GetSharedData().sound_prio;
	if (ShouldUpdateTargets() && snd_prio != 0)
	{
		if (group_count > 0)
		{
			bool in_window = group[0]->flying_time_remaining >= game->sad_late_window && group[0]->flying_time_remaining <= game->sad_early_window;
			bool wbutton = group[0]->target_type >= TargetType_UpW && group[0]->target_type <= TargetType_LeftW;
			bool wstar = group[0]->target_type == TargetType_StarW;
			bool button_tapped = (macro_state.GetTappedBitfield() & GetMainButtonsMask()) != 0;

			if (in_window && snd_prio == 1) // Delay
			{
				if (wbutton && button_tapped)
				{
					*play_default_se = false;
					se_mgr.ScheduleButtonSound();
				}
				else if (wstar)
				{
					schedule_star_se = true;
					game->mute_slide_chime = true;
				}
			}
			else if (in_window && snd_prio == 2) // Mute
			{
				if (wbutton && game->bool1328E)
					*play_default_se = false;
				else if (wstar && macro_state.GetStarHit())
				{
					should_play_star_se = false;
					game->mute_slide_chime = true;
				}
			}
		}
	}

	if (has_vanilla_note || !ShouldUpdateTargets() || group_count < 1)
	{
		final_hit_state = originalGetHitState(
			game,
			play_default_se,
			rating_count,
			rating_pos,
			a5,
			se,
			multi_count,
			player_hit_time,
			target_index,
			is_success_note,
			slide,
			slide_chain,
			slide_chain_start,
			slide_chain_max,
			slide_chain_continues,
			a16
		);
	}
	else if (has_custom_note)
	{
		// NOTE: Set default return values
		*is_success_note = false;
		*slide = false;
		*slide_chain = false;
		*slide_chain_start = false;
		*slide_chain_max = false;
		*slide_chain_continues = false;
		bool success = false;

		// NOTE: Get extra data for the targets
		TargetStateEx* extras[4] = { nullptr, nullptr, nullptr, nullptr };
		for (int i = 0; i < group_count; i++)
			extras[i] = GetTargetStateEx(group[i]);

		// NOTE: Judge hit and set post-hit information
		int32_t hit_state = nc::JudgeNoteHit(game, group, extras, group_count, &success);
		if (hit_state != HitState_None)
		{
			final_hit_state = hit_state;
			*multi_count = group_count;
			*target_index = group[0]->target_index;
			*player_hit_time = group[0]->player_hit_time;
			
			if (success)
				GetPVGameData()->is_success_branch = true;

			for (int i = 0; i < group_count; i++)
			{
				PvGameTarget* tgt = group[i];
				TargetStateEx* ex = extras[i];

				if (ex->IsLongNoteStart())
				{
					if (ex->IsWrong())
					{
						state.PopTarget(ex);
						ex->StopAet();
						ex->next->force_hit_state = ex->hit_state;
					}
					else
						ex->SetLongNoteAet();
				}
				else if (ex->IsRushNote())
				{
					if (!ex->IsWrong())
					{
						ex->SetRushNoteAet();
						se_mgr.StartRushBackSE();
					}
					else
						state.PopTarget(ex);
				}
				else if (ex->IsLongNoteEnd())
				{
					ex->prev->StopAet();
					state.PopTarget(ex->prev);
				}
				else if (ex->IsLinkNoteEnd() && nc::IsHitGreat(hit_state))
				{
					// NOTE: Find chain start target
					TargetStateEx* chain = nullptr;
					for (chain = ex; chain != nullptr && chain->prev != nullptr; chain = chain->prev) { }

					if (chain != nullptr)
						chain->StopAet();
					ex->StopAet();
				}

				rating_pos[(*rating_count)++] = tgt->target_pos;

				game->EraseTarget(tgt);
				game->RemoveTargetAet(tgt);

				// NOTE: Play note hit effect
				//
				int32_t hit_effect_index = -1;
				switch (ex->hit_state)
				{
				case HitState_Cool:
					hit_effect_index = 1;
					break;
				case HitState_Fine:
					hit_effect_index = 2;
					break;
				case HitState_Safe:
					hit_effect_index = 3;
					break;
				case HitState_Sad:
					hit_effect_index = 4;
					break;
				case HitState_WrongCool:
				case HitState_WrongFine:
				case HitState_WrongSafe:
				case HitState_WrongSad:
					hit_effect_index = 0;
					break;
				}

				if (hit_effect_index != -1)
					game->PlayHitEffect(hit_effect_index, tgt->target_pos);

				// NOTE: Play note SE
				//
				if (nc::IsHitCorrect(hit_state))
				{
					switch (ex->target_type)
					{
					case TargetType_TriangleRush:
					case TargetType_CircleRush:
					case TargetType_CrossRush:
					case TargetType_SquareRush:
						se_mgr.PlayButtonSE();
						break;
					case TargetType_UpW:
					case TargetType_RightW:
					case TargetType_DownW:
					case TargetType_LeftW:
						se_mgr.PlayDoubleSE();
						se_mgr.ClearSchedules();
						break;
					case TargetType_TriangleLong:
					case TargetType_CircleLong:
					case TargetType_CrossLong:
					case TargetType_SquareLong:
						if (ex->IsLongNoteStart())
							se_mgr.StartLongSE();
						else
							se_mgr.EndLongSE(false);
						break;
					case TargetType_LinkStar:
						if (ex->IsLinkNoteStart()) { se_mgr.StartLinkSE(); }
						[[fallthrough]];
					case TargetType_Star:
					case TargetType_StarRush:
					case TargetType_LinkStarEnd:
						se_mgr.PlayStarSE();
						game->mute_slide_chime = true;
						break;
					case TargetType_ChanceStar:
						if (state.chance_time.GetFillRate() == 15)
							se_mgr.PlayCymbalSE();
						else
							se_mgr.PlayStarSE();
						game->mute_slide_chime = true;
						break;
					case TargetType_StarW:
						se_mgr.PlayStarDoubleSE();
						se_mgr.ClearSchedules();
						game->mute_slide_chime = true;
						break;
					}

					*play_default_se = false;
				}
				else if (nc::IsHitWrong(hit_state))
				{
					*play_default_se = true;
					se_mgr.ClearSchedules();
				}
				else if (ex->IsLongNoteEnd())
				{
					se_mgr.EndLongSE(true);
					GetPVGameData()->ui.RemoveBonusText();
				}

				if (ex->IsLinkNoteEnd())
					se_mgr.EndLinkSE();
			}
		}
	}
	
	// NOTE: Calculate bonus score
	if (nc::IsHitCorrect(final_hit_state))
	{
		for (int i = 0; i < group_count; i++)
		{
			TargetStateEx* ex = GetTargetStateEx(group[i]);
			ex->hit_state = group[i]->hit_state;

			int32_t disp_score = 0;
			GetPVGameData()->score += score::CalculateHitScoreBonus(*ex->parent, final_hit_state, &disp_score);
			GetPVGameData()->ui.SetBonusText(disp_score, ex->target_pos);

			if (ex->IsLongNoteEnd())
				state.score.sustain_bonus += ex->prev->parent->bonus_score;
		}
	}

	// NOTE: Update chance time
	if (nc::IsHitGreat(final_hit_state))
	{
		if (state.chance_time.CheckTargetInRange(*target_index))
			state.chance_time.targets_hit += 1;
	}

	// NOTE: Update technical zones
	if (final_hit_state != HitState_None)
	{
		for (TechZoneState& tz : state.tech_zones)
			tz.PushNewHitState(*target_index, final_hit_state);
	}

	if (should_play_star_se && *play_default_se && nc::GetSharedData().stick_control_se == 1 && state.GetGameStyle() != GameStyle_Arcade)
	{
		if (macro_state.GetStarHit())
		{
			if (schedule_star_se)
				se_mgr.ScheduleStarSound();
			else
				se_mgr.PlayStarSE();
		}

		game->mute_slide_chime = true;
	}

	return final_hit_state;
}*/

static bool play_default_se = false;

HOOK(int32_t, __fastcall, GetHitState, 0x14026BF60,
	PVGameArcade* game,
	bool* play_default_se,
	size_t* rating_count,
	diva::vec2* rating_pos,
	int32_t* a5,
	SoundEffect* se,
	int32_t* multi_count,
	float* player_hit_time,
	int32_t* target_index,
	bool* is_success_note,
	bool* slide,
	bool* slide_chain,
	bool* slide_chain_start,
	bool* slide_chain_max,
	bool* slide_chain_continues,
	void* a16)
{
	macro_state.Update(game->ptr08, 0);

	// NOTE: Reset state
	::play_default_se = *play_default_se;
	
	// NOTE: Figure out which notes are going to be polled by the game now
	std::pair<PvGameTarget*, TargetStateEx*> targets[4] = { };
	TargetGroupEx* group = nullptr;
	int32_t index = 0;

	for (PvGameTarget* target = game->target; target != nullptr; target = target->next)
	{
		targets[index++] = std::make_pair(target, GetTargetStateEx(target));
		if (index >= 4 || target->multi_count < 0 || target->multi_count != game->target->multi_count )
			break;
	}

	if (index > 0)
		group = targets[0].second->parent;

	// NOTE: Update in-course sustain / rush notes
	if (ShouldUpdateTargets())
	{
		for (TargetStateEx* tgt : state.target_references)
		{
			if (tgt->IsLongNoteStart() && tgt->holding)
			{
				bool is_in_zone = false;

				// NOTE: Check if the end target is in it's timing window
				if (tgt->next->org)
				{
					float time = tgt->next->org->flying_time_remaining;
					is_in_zone = time >= game->sad_late_window && time <= game->sad_early_window;
				}

				score::CalculateSustainBonus(tgt);
				if (tgt->parent)
				{
					int32_t bonus = tgt->parent->bonus_score + tgt->parent->ct_bonus_score;
					GetPVGameData()->ui.SetBonusText(bonus, tgt->parent->CalculateCenter());
				}

				// NOTE: Check if the start target button has been released;
				//       if it's the end note is not inside it's timing zone,
				//       automatically mark it as a fail.
				if (!nc::CheckLongNoteHolding(tgt) && !is_in_zone)
				{
					tgt->next->force_hit_state = HitState_Worst;
					tgt->StopAet();
					tgt->holding = false;
					se_mgr.EndLongSE(true);
					// GetPVGameData()->ui.RemoveBonusText();
				}
			}
		}
	}

	int32_t hit_state = originalGetHitState(
		game,
		play_default_se,
		rating_count,
		rating_pos,
		a5,
		se,
		multi_count,
		player_hit_time,
		target_index,
		is_success_note,
		slide,
		slide_chain,
		slide_chain_start,
		slide_chain_max,
		slide_chain_continues,
		a16
	);

	if (!::play_default_se)
	{
		*play_default_se = ::play_default_se;
		game->mute_slide_chime = true;
	}

	if (hit_state != HitState_None && group)
	{
		int32_t disp = 0;
		int32_t bonus = score::CalculateHitScoreBonus(*group, hit_state, &disp);

		GetPVGameData()->ui.SetBonusText(disp, group->CalculateCenter());
	}

	return hit_state;
}

HOOK(int32_t, __fastcall, GetHitStateInternal, 0x14026D2E0, PVGameArcade* game, PvGameTarget* target, uint16_t a3, uint16_t a4)
{
	int32_t hit_state = originalGetHitStateInternal(game, target, a3, a4);
	if (nc::IsCustomTargetType(target->target_type))
	{
		if (TargetStateEx* ex = GetTargetStateEx(target); ex != nullptr)
		{
			bool success = false;
			hit_state = nc::JudgeNoteHit(game, &target, &ex, 1, &success);

			if (hit_state != HitState_None)
				ProcessNoteHit(*target, *ex, hit_state);
		}
	}

	return hit_state;
}

HOOK(void, __fastcall, CalculatePercentage, 0x140246130, PVGameData* pv_game)
{
	switch (state.GetScoreMode())
	{
	case ScoreMode_F2nd:
	case ScoreMode_Franken:
		if (pv_game->scoring_enabled)
			pv_game->percentage = score::CalculatePercentage(pv_game);
		return;
	}

	return originalCalculatePercentage(pv_game);
}

HOOK(void, __fastcall, UpdateLife, 0x140245220, PVGameData* a1, int32_t hit_state, bool a3, bool is_challenge_time, int32_t a5, bool a6, bool a7, bool a8)
{
	originalUpdateLife(
		a1,
		hit_state,
		a3,
		is_challenge_time || state.chance_time.enabled,
		a5,
		a6,
		a7,
		a8
	);
}

HOOK(void, __fastcall, ExecuteModeSelect, 0x1503B04A0, PVGamePvData* pv_data, int32_t op)
{
	int32_t difficulty = pv_data->script_buffer[pv_data->script_pos + 1];
	int32_t mode = pv_data->script_buffer[pv_data->script_pos + 2];

	if (dsc::IsCurrentDifficulty(difficulty) && !game::IsPvMode())
	{
		switch (mode)
		{
		case ModeSelect_ChanceStart:
			SetChanceTimeMode(&pv_data->pv_game->ui, ModeSelect_ChanceStart);
			break;
		case ModeSelect_ChanceEnd:
			if (state.chance_time.successful)
				pv_data->pv_game->score += score::GetChanceTimeSuccessBonus();
			SetChanceTimeMode(&pv_data->pv_game->ui, ModeSelect_ChanceEnd);
			break;
		case ModeSelect_TechZoneStart:
			if (state.tech_zone_index < state.tech_zones.size())
			{
				state.tz_disp.data = &state.tech_zones[state.tech_zone_index];
				state.tz_disp.end = false;
			}

			break;
		case ModeSelect_TechZoneEnd:
			if (state.tech_zone_index < state.tech_zones.size())
			{
				if (TechZoneState& tz = state.tech_zones[state.tech_zone_index]; tz.IsValid())
				{
					if (tz.IsSuccessful())
						pv_data->pv_game->score += score::GetTechZoneSuccessBonus();
				}
				
				state.tz_disp.end = true;
				state.tech_zone_index++;
			}

			break;
		}
	}

	return originalExecuteModeSelect(pv_data, op);
}

HOOK(void, __fastcall, UpdateGaugeFrame, 0x14027A490, PVGameUI* ui)
{
	originalUpdateGaugeFrame(ui);
	if (state.chance_time.enabled)
		SetChanceTimeStarFill(ui, state.chance_time.GetFillRate());
	SetChanceTimePosition(ui);
	state.tz_disp.Ctrl();
}

void ProcessNoteHit(const PvGameTarget& tgt, TargetStateEx& ex, int32_t hit_state)
{
	if (ex.IsLongNoteStart())
	{
		if (nc::IsHitCorrect(hit_state))
		{
			ex.CaptureSustainAet();
			ex.sustain_bonus_timer.Start();
			se_mgr.StartLongSE();
			play_default_se = false;
		}
		else
		{
			if (ex.next)
				ex.next->force_hit_state = hit_state;
		}
	}
	else if (ex.IsLongNoteEnd())
	{
		if (ex.prev)
			ex.prev->StopAet();

		if (nc::IsHitCorrect(hit_state))
		{

		}

		se_mgr.EndLongSE(!nc::IsHitCorrect(hit_state));
		play_default_se = false;
	}
}

void InstallGameHooks()
{
	INSTALL_HOOK(GetHitState);
	INSTALL_HOOK(GetHitStateInternal);
	INSTALL_HOOK(UpdateLife);
	INSTALL_HOOK(ExecuteModeSelect);
	INSTALL_HOOK(UpdateGaugeFrame);
	INSTALL_HOOK(CalculatePercentage);

	// NOTE: Replace the branches in FinishTargetAet with a simple JMP so that the game properly
	//       removes the target aets from the screen even if the target or button aet handles are 0,
	//       which normally would make the target effects aet stay on screen when retrying a song while
	//       a link note is spawning (as we capture their aet handles)
	WRITE_MEMORY(0x14026E649, uint8_t, 0xE9, 0x7, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0);
}