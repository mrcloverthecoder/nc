#include <stdint.h>
#include <diva.h>
#include <nc_state.h>
#include <util.h>
#include "hit_state.h"
#include "score.h"

constexpr int32_t DoubleTapScoreBonus     = 200;
constexpr int32_t RushNotePopBonus        = 30;
constexpr float   SustainBonusInterval    = 0.1;
constexpr int32_t SustainBonusScore[4]    = { 20,   10,  10,  0 };
constexpr int32_t ChanceTimeScoreBonus[4] = { 1000, 600, 200, 60 };
constexpr int32_t LinkNoteScoreBonus[4]   = { 200,  100, 0,   0 };
constexpr float TechZoneRetainedRate      = 0.03; // 3%

constexpr float WrongPercentageWeight = 0.5;
static const float JudgePercentageWeight[5][5] = {
	// COOL  FINE  SAFE  SAD   WORST
	{  1.0f, 0.7f, 0.5f, 0.3f, 0.0f  }, // EASY
	{  1.0f, 0.7f, 0.5f, 0.3f, 0.0f  }, // NORMAL
	{  1.0f, 0.7f, 0.5f, 0.3f, 0.0f  }, // HARD
	{  1.0f, 0.7f, 0.5f, 0.3f, 0.0f  }, // EXTREME / EXTRA-EXTREME
	{  0.0f, 0.0f, 0.0f, 0.0f, 0.0f  }, // ENCORE
};

constexpr float ChanceTimePercBonus  = 0.01; // 1%
constexpr float DoubleTapPercBonus   = 0.02; // 2%
constexpr float SustainHoldPercBonus = 0.01; // 1%

float score::GetTechZoneRetainedRate()
{
	return TechZoneRetainedRate;
}

int32_t score::CalculateHitScoreBonus(TargetGroupEx& group, int32_t hit_state, int32_t* disp)
{
	int32_t bonus = 0;
	int32_t ct_bonus = 0;
	int32_t bonus_disp = 0;

	hit_state = nc::GetBasicHitState(hit_state);
	if (hit_state != BasicHitState_Worst)
	{
		for (const TargetStateEx& target : group)
		{
			if (target.double_tapped)
			{
				bonus += DoubleTapScoreBonus;
				state.score.double_tap_bonus += DoubleTapScoreBonus;
			}

			if (state.chance_time.CheckTargetInRange(group.target_index))
			{
				ct_bonus = ChanceTimeScoreBonus[hit_state];
				bonus_disp += ct_bonus;
			}

			if (target.IsLinkNote())
			{
				bonus += LinkNoteScoreBonus[hit_state];
				state.score.link_bonus += LinkNoteScoreBonus[hit_state];

				for (TargetStateEx* prev = target.prev; prev != nullptr; prev = prev->prev)
					bonus_disp += prev->parent->ct_bonus_score + prev->parent->bonus_score;
			}
			else if (target.IsLongNoteEnd())
			{
				bonus += target.prev->parent->bonus_score;
				bonus_disp += target.prev->parent->ct_bonus_score;
			}
		}

		if (ct_bonus > 0)
		{
			state.score.ct_score_bonus += ct_bonus;
			group.ct_bonus_score = ct_bonus;
		}
	}

	if (disp)
		*disp = bonus_disp + bonus;

	group.bonus_score += bonus;
	return bonus;
}

int32_t score::CalculateSustainBonus(TargetStateEx* target)
{
	if (!nc::IsHitCorrect(target->hit_state))
		return 0;

	if (target->sustain_bonus_timer.Ellapsed() >= SustainBonusInterval)
	{
		int32_t bonus = SustainBonusScore[nc::GetBasicHitState(target->hit_state)];
		target->parent->bonus_score += bonus;
		target->sustain_bonus_timer.Reset();
		return bonus;
	}

	return 0;
}

int32_t score::CalculateMaxSustainBonus(TargetStateEx* target)
{
	return static_cast<int32_t>(target->length / SustainBonusInterval) * SustainBonusScore[0];
}

int32_t score::IncreaseRushPopCount(TargetStateEx* target)
{
	target->bal_hit_count += 1;
	target->bal_scale = util::Clamp(target->bal_hit_count / static_cast<float>(target->bal_max_hit_count), 0.0f, 1.0f);
	target->parent->bonus_score += RushNotePopBonus;
	state.score.rush_bonus += RushNotePopBonus;
	return RushNotePopBonus;
}

static float CalculateF2ndBasePercentage(const int32_t* judge, const float target_max_rate, int32_t note_count)
{
	return static_cast<float>(judge[HitState_Cool] + judge[HitState_Fine]) / static_cast<float>(note_count) * target_max_rate;
}

// NOTE: This function assumes it's being called from the rhythm game so the state is valid
float score::CalculatePercentage(PVGameData* pv_game)
{
	if (state.score.target_max_rate <= 0.0f || pv_game->pv_data.targets.size() < 1)
		return 0.0f;

	float percentage = 0.0f;
	if (state.GetScoreMode() == ScoreMode_F2nd)
	{
		percentage = CalculateF2ndBasePercentage(
			pv_game->judge_count_correct,
			state.score.target_max_rate,
			static_cast<int32_t>(pv_game->pv_data.targets.size())
		);
	}

	// NOTE: Add chance time star success bonus
	if (state.chance_time.IsValid() && state.chance_time.successful)
		percentage += ChanceTimeRetainedRate;

	// NOTE: Add technical zone success bonus
	for (TechZoneState& tz : state.tech_zones)
		if (tz.IsSuccessful())
			percentage += TechZoneRetainedRate;

	return percentage * 100.0;
}

void score::CalculateScoreReference(ScoreState* ref, PVGameData* pv_game)
{
	const ChanceState& ct = state.chance_time;

	ref->target_max_rate = 1.0f;
	ref->max_ct_score_bonus = 0;
	ref->max_double_tap_bonus = 0;
	ref->max_sustain_bonus = 0;
	ref->max_link_bonus = 0;

	if (ct.IsValid())
		ref->target_max_rate -= ChanceTimeRetainedRate;

	for (TechZoneState& tz : state.tech_zones)
		ref->target_max_rate -= TechZoneRetainedRate;

	const float target_step_score = (pv_game->reference_score * ref->target_max_rate) / pv_game->pv_data.targets.size();
	pv_game->target_reference_scores.clear();
	pv_game->target_reference_scores.push_back(0);

	for (size_t i = 0; i < pv_game->pv_data.targets.size(); i++)
	{
		const PvDscTargetGroup& group = pv_game->pv_data.targets[i];
		
		// NOTE: Calculate constant reference scores for percentage calculation
		if (ct.CheckTargetInRange(i))
			ref->max_ct_score_bonus += ChanceTimeScoreBonus[0];

		for (int32_t sub = 0; sub < group.target_count; sub++)
		{
			TargetStateEx& target = *GetTargetStateEx(i, sub);
			if (target.IsNormalDoubleNote())
				ref->max_double_tap_bonus += DoubleTapScoreBonus;

			if (target.IsLongNoteStart())
				ref->max_sustain_bonus += static_cast<int32_t>(target.length / SustainBonusInterval) * SustainBonusScore[0];

			if (target.IsLinkNote())
				ref->max_link_bonus += LinkNoteScoreBonus[0];
		}

		// NOTE: Patch target reference scores, to make the percentages borders
		//       move more accurately to the console gameplay flow.
		pv_game->target_reference_scores.push_back(pv_game->target_reference_scores.back() + target_step_score);

		if (ct.IsValid() && i == ct.last_target_index)
			pv_game->target_reference_scores.back() += pv_game->reference_score * ChanceTimeRetainedRate;

		for (TechZoneState& tz : state.tech_zones)
			if (i == tz.last_target_index)
				pv_game->target_reference_scores.back() += pv_game->reference_score * TechZoneRetainedRate;
	}
}