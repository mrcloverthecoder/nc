#include <stdint.h>
#include <hooks.h>
#include <nc_state.h>
#include <nc_log.h>
#include <save_data.h>
#include "target.h"
#include "chance_time.h"
#include "hit_state.h"
#include "score.h"
#include "sound_effects.h"

struct NCSharedGameState
{
	std::vector<PvGameTarget*> active_group;
	std::vector<std::pair<PvGameTarget*, TargetStateEx*>> group;
	bool mute_slide_chime;

	NCSharedGameState()
	{
		active_group.reserve(4);
		group.reserve(4);
		mute_slide_chime = false;
	}

	void Reset()
	{
		active_group.clear();
		group.clear();
		mute_slide_chime = false;
	}

	void PushActiveTarget(PvGameTarget* target)
	{
		active_group.push_back(target);
		if (!group.empty() || target->multi_count < 0)
			return;

		group.emplace_back(target, GetTargetStateEx(target));

		for (PvGameTarget* prev = target->prev; prev && prev->multi_count == target->multi_count; prev = prev->prev)
			group.emplace_back(prev, GetTargetStateEx(prev));

		for (PvGameTarget* next = target->next; next && next->multi_count == target->multi_count; next = next->next)
			group.emplace_back(next, GetTargetStateEx(next));
	}

} static game_state;

static bool PlayNoteSoundEffectOnHit(PvGameTarget* target, TargetStateEx* ex);
static bool CheckContinuousNoteSoundEffects(PvGameTarget* target, TargetStateEx* ex);


float ChallengeTimeHeight = 0.0f;
constexpr float ChanceTimeHeight = 149.0f;
constexpr uintptr_t FrmBtmHeightAddrs[] = { 0x140274E47, 0x140274E5B };

void PatchFrmBtmHeight(float height) 
{
	uint32_t bits;
	std::memcpy(&bits, &height, sizeof(float));

	for (uintptr_t addr : FrmBtmHeightAddrs)
		WRITE_MEMORY(addr, uint32_t, bits);
}

void SaveAndPatchCTHeight()
{
	float currentBtm = *reinterpret_cast<float*>(FrmBtmHeightAddrs[0]);

	if (currentBtm != ChanceTimeHeight)
		ChallengeTimeHeight = currentBtm;

	PatchFrmBtmHeight(ChanceTimeHeight);
}


HOOK(int32_t, __fastcall, GetHitStateInternal, 0x14026D2E0,
	PVGameArcade* game,
	PvGameTarget* target,
	uint16_t a3,
	uint16_t a4)
{
	game_state.PushActiveTarget(target);

	if (target->target_type < TargetType_Custom || target->target_type >= TargetType_Max)
		return originalGetHitStateInternal(game, target, a3, a4);

	TargetStateEx* ex = GetTargetStateEx(target);
	if (!ex)
		return HitState_Worst;

	bool success = false;
	int32_t hit_state = nc::JudgeNoteHit(game, &target, &ex, 1, &success);

	if (success)
		GetPVGameData()->is_success_branch = true;

	if (hit_state != HitState_None)
	{
		if (ex->IsLongNoteStart())
		{
			if (ex->IsWrong())
			{
				state.PopTarget(ex);
				ex->StopAet();
				ex->next->shared_data->force_hit_state = ex->hit_state;
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
			for (chain = ex; chain != nullptr && chain->prev != nullptr; chain = chain->prev) {}

			if (chain != nullptr)
				chain->StopAet();
			ex->StopAet();
		}
	}

	return hit_state;
}

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
	game_state.Reset();
	macro_state.Update(game->ptr08, 0);

	if (ShouldUpdateTargets())
	{
		for (auto it = state.target_references.begin(); it != state.target_references.end();)
		{
			TargetStateEx* tgt = *it;

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
				GetPVGameData()->ui.SetBonusText(tgt->score_bonus + tgt->ct_score_bonus, tgt->target_pos);

				// NOTE: Check if the start target button has been released;
				//       if it's the end note is not inside it's timing zone,
				//       automatically mark it as a fail.
				if (!nc::CheckLongNoteHolding(tgt) && !is_in_zone)
				{
					tgt->next->shared_data->force_hit_state = HitState_Worst;
					tgt->StopAet();
					tgt->holding = false;
					it = state.target_references.erase(it);
					se_mgr.EndLongSE(true);
					GetPVGameData()->ui.RemoveBonusText();
					continue;
				}
			}
			// NOTE: Poll input for ongoing rush notes
			else if (tgt->IsRushNote() && tgt->holding)
			{
				if (nc::CheckRushNotePops(tgt))
				{
					GetPVGameData()->score += score::IncreaseRushPopCount(tgt);
					GetPVGameData()->ui.SetBonusText(tgt->score_bonus, tgt->target_pos);
					state.PlayRushHitEffect(GetScaledPosition(tgt->target_pos), 0.6f * (1.0f + tgt->bal_scale), false);

					if (tgt->target_type == TargetType_StarRush)
					{
						se_mgr.PlayStarSE();
						game->mute_slide_chime = true;
						*play_default_se = false;
					}
				}
			}

			it++;
		}
	}

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

	if (!ShouldUpdateTargets())
		return final_hit_state;

	// NOTE: Calculate bonus score and play hit effect
	if (final_hit_state != HitState_None && game_state.group.size() > 0)
	{
		int32_t total_disp_score = 0;
		diva::vec2 calc_target_pos = {};

		if (nc::IsHitCorrect(final_hit_state))
		{
			if (state.chance_time.CheckTargetInRange(game_state.group[0].first->target_index))
			{
				int32_t bonus = score::GetChanceTimeScoreBonus(nc::GetHitStateBase(final_hit_state));
				GetPVGameData()->score += bonus;
				state.score.ct_score_bonus += bonus;
				total_disp_score += bonus;
			}
		}

		for (auto& [target, ex] : game_state.group)
		{
			ex->hit_state = target->hit_state;

			if (nc::IsHitCorrect(ex->hit_state))
			{
				int32_t disp_score = 0;
				GetPVGameData()->score += score::CalculateHitScoreBonus(ex, &disp_score);

				total_disp_score += disp_score;
				calc_target_pos = calc_target_pos + target->target_pos;

				if (ex->IsLongNoteEnd())
					state.score.sustain_bonus += ex->prev->score_bonus;

				if (ex->target_hit_effect_id >= 0)
				{
					std::string effect_name =
						GetPVGameData()->is_success_branch
						? state.success_target_effect_map[ex->target_hit_effect_id]
						: state.fail_target_effect_map[ex->target_hit_effect_id];

					if (!effect_name.empty())
					{
						std::shared_ptr<AetElement> eff = state.ui.PushHitEffect();
						if (eff)
						{
							eff->SetLayer(effect_name, 0x20000, 7, 13, "", "", nullptr);
							eff->SetPosition(diva::vec3(GetScaledPosition(ex->target_pos), 0.0f));
						}
					}
				}
			}

			PlayNoteSoundEffectOnHit(target, ex);
			CheckContinuousNoteSoundEffects(target, ex);
		}

		game->mute_slide_chime |= game_state.mute_slide_chime;
		GetPVGameData()->ui.SetBonusText(total_disp_score, calc_target_pos / game_state.group.size());
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

	// NOTE: Check sound priority
	int32_t snd_prio = nc::GetSharedData().sound_prio;
	bool should_play_star_se = true;

	if (snd_prio == 2 && game_state.group.size() > 0)
	{
		for (auto& [target, ex] : game_state.group)
		{
			if (target->flying_time_remaining >= game->sad_late_window &&
				target->flying_time_remaining <= game->sad_early_window)
			{
				if (target->target_type >= TargetType_UpW && target->target_type <= TargetType_LeftW)
					*play_default_se = false;
				else if (target->target_type == TargetType_StarW)
				{
					should_play_star_se = false;
					game->mute_slide_chime = true;
				}
			}
		}
	}

	// NOTE: Play default star sound when no notes are currently being polled.
	//       (Only in CONSOLE and MIXED modes)
	if (should_play_star_se && *play_default_se && nc::GetSharedData().stick_control_se == 1 && state.GetGameStyle() != GameStyle_Arcade)
	{
		if (macro_state.GetStarHit())
		{
			se_mgr.PlayStarSE();
			game->mute_slide_chime = true;
		}
	}

	return final_hit_state;
}

MIDASM_HOOK(GetHitStatePlaySE, 0x14026C7E7)
{
	int32_t hit_type = *reinterpret_cast<int32_t*>(&ctx.rsi);
	int32_t target_type = *reinterpret_cast<int32_t*>(&ctx.rax) + 14;

	// NOTE: Check if this is an NC note and nullify the name so that
	//       no sound plays. For some reason setting xmm2 (volume) to
	//       0.0 makes the next sound also play silently.
	if (hit_type >= 0 && target_type >= TargetType_Custom && target_type <= TargetType_Max)
		*reinterpret_cast<const char**>(&ctx.rdx) = "";
}

static bool PlayNoteSoundEffectOnHit(PvGameTarget* target, TargetStateEx* ex)
{
	if (ex->hit_state == HitState_Worst || ex->hit_state == HitState_None)
		return false;

	if (nc::IsHitCorrect(ex->hit_state))
	{
		if (ex->IsNormalDoubleNote())
		{
			se_mgr.PlayDoubleSE();
			return true;
		}
		else if (ex->target_type == TargetType_StarW)
		{
			se_mgr.PlayStarDoubleSE();
			game_state.mute_slide_chime = true;
			return true;
		}
		else if (ex->IsRushNote())
		{
			if (!ex->IsStarLikeNote())
				se_mgr.PlayButtonSE();
			se_mgr.StartRushBackSE();
		}
		else if (ex->IsLinkNoteStart())
			se_mgr.StartLinkSE();
		else if (ex->IsLongNoteStart())
			se_mgr.StartLongSE();

		if (ex->IsStarLikeNote())
		{
			if (ex->target_type == TargetType_ChanceStar && state.chance_time.GetFillRate() == 15)
				se_mgr.PlayCymbalSE();
			else
				se_mgr.PlayStarSE();

			game_state.mute_slide_chime = true;
		}
	}
	else if (nc::IsHitWrong(ex->hit_state))
	{
		if (ex->IsNormalDoubleNote() || ex->IsRushNote() && !ex->IsStarLikeNote())
			se_mgr.PlayButtonSE();
	}

	return true;
}

static bool CheckContinuousNoteSoundEffects(PvGameTarget* target, TargetStateEx* ex)
{
	if (ex->hit_state == HitState_None)
		return false;

	if (ex->IsLinkNoteEnd())
		se_mgr.EndLinkSE();
	else if (ex->IsLongNoteEnd())
		se_mgr.EndLongSE(!nc::IsHitCorrect(ex->hit_state));
	else if (ex->IsRushNote() && ex->length_remaining <= 0.0f)
		se_mgr.EndRushBackSE(ex->bal_hit_count >= ex->bal_max_hit_count);

	return true;
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
		case ModeSelect_ChallengeStart:
			PatchFrmBtmHeight(ChallengeTimeHeight);
			break;
		case ModeSelect_ChanceStart:
			SaveAndPatchCTHeight();
			SetChanceTimeMode(&pv_data->pv_game->ui, ModeSelect_ChanceStart);
			break;
		case ModeSelect_ChanceEnd:
			if (state.chance_time.successful && state.GetGameStyle() == GameStyle_Console)
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
					if (tz.IsSuccessful() && state.GetGameStyle() != GameStyle_Arcade)
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

void InstallGameHooks()
{
	INSTALL_HOOK(GetHitStateInternal);
	INSTALL_HOOK(GetHitState);
	INSTALL_HOOK(UpdateLife);
	INSTALL_HOOK(ExecuteModeSelect);
	INSTALL_HOOK(UpdateGaugeFrame);
	INSTALL_HOOK(CalculatePercentage);
	INSTALL_MIDASM_HOOK(GetHitStatePlaySE);

	// NOTE: Replace the branches in FinishTargetAet with a simple JMP so that the game properly
	//       removes the target aets from the screen even if the target or button aet handles are 0,
	//       which normally would make the target effects aet stay on screen when retrying a song while
	//       a link note is spawning (as we capture their aet handles)
	WRITE_MEMORY(0x14026E649, uint8_t, 0xE9, 0x7, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0);
	
	ChallengeTimeHeight = *reinterpret_cast<float*>(FrmBtmHeightAddrs[0]);
}
