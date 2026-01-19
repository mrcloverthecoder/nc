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

int32_t score::CalculateHitScoreBonus(TargetStateEx* target, int32_t* disp)
{
	int32_t bonus = 0;
	int32_t ct_bonus = 0;
	int32_t bonus_disp = 0;

	if (target->double_tapped)
	{
		bonus += DoubleTapScoreBonus;
		state.score.double_tap_bonus += DoubleTapScoreBonus;
	}

	if (state.chance_time.CheckTargetInRange(target->target_index))
	{
		ct_bonus = ChanceTimeScoreBonus[target->hit_state];
		bonus_disp += ct_bonus;
	}

	if (target->IsLinkNote())
	{
		bonus += LinkNoteScoreBonus[target->hit_state];
		state.score.link_bonus += LinkNoteScoreBonus[target->hit_state];

		for (TargetStateEx* prev = target->prev; prev != nullptr; prev = prev->prev)
			bonus_disp += prev->ct_score_bonus + prev->score_bonus;
	}
	else if (target->IsLongNoteEnd())
	{
		bonus += target->prev->score_bonus;
		bonus_disp += target->prev->ct_score_bonus;
	}

	if (disp)
		*disp = bonus_disp + bonus;

	if (ct_bonus > 0)
	{
		state.score.ct_score_bonus += ct_bonus;
		target->ct_score_bonus = ct_bonus;
	}

	target->score_bonus += bonus;
	return bonus;
}

int32_t score::CalculateSustainBonus(TargetStateEx* target)
{
	if (!nc::IsHitCorrect(target->hit_state))
		return 0;

	int32_t bonus = 0;
	while (target->sustain_bonus_time >= SustainBonusInterval)
	{
		bonus += SustainBonusScore[target->hit_state];
		target->sustain_bonus_time -= SustainBonusInterval;
	}

	target->score_bonus = util::Clamp(target->score_bonus + bonus, 0, CalculateMaxSustainBonus(target));
	return bonus;
}

int32_t score::CalculateMaxSustainBonus(TargetStateEx* target)
{
	return static_cast<int32_t>(target->length / SustainBonusInterval) * SustainBonusScore[0];
}

int32_t score::IncreaseRushPopCount(TargetStateEx* target)
{
	target->bal_hit_count += 1;
	target->bal_scale = util::Clamp(target->bal_hit_count / static_cast<float>(target->bal_max_hit_count), 0.0f, 1.0f);
	target->score_bonus += RushNotePopBonus;
	state.score.rush_bonus += RushNotePopBonus;
	return RushNotePopBonus;
}

// NOTE: Calculates the base percentage of just the targets (excluding events such as Chance Time).
//       Events and bonuses can be applied externally.
static float CalculateFrankenBasePercentage(
	const int32_t* judge,
	const int32_t* judge_wrong,
	const float* weights,
	const float target_max_rate,
	int32_t note_count
)
{
	float percentage = 0.0;

	// NOTE: Calculate target percentage
	for (int32_t i = 0; i < 4; i++)
	{
		float correct_inc = judge[i] / static_cast<float>(note_count) * target_max_rate;
		float wrong_inc = (judge_wrong[i] - judge[i]) / static_cast<float>(note_count) * target_max_rate;

		percentage += (correct_inc * weights[i]) + (wrong_inc * weights[i] * WrongPercentageWeight);
	}

	return percentage;
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
	if (state.GetScoreMode() == ScoreMode_Franken)
	{
		percentage = CalculateFrankenBasePercentage(
			pv_game->judge_count_correct,
			pv_game->judge_count,
			JudgePercentageWeight[GetPvGameplayInfo()->difficulty],
			state.score.target_max_rate,
			static_cast<int32_t>(pv_game->pv_data.targets.size())
		);

		if (state.chance_time.IsValid())
		{
			// NOTE: Add chance time bonus score percentage
			percentage += state.score.ct_score_bonus / static_cast<float>(state.score.max_ct_score_bonus) * ChanceTimePercBonus;
		}

		// NOTE: Add double tap bonus score percentage
		if (state.score.max_double_tap_bonus > 0)
			percentage += state.score.double_tap_bonus / static_cast<float>(state.score.max_double_tap_bonus) * DoubleTapPercBonus;

		// NOTE: Add sustain hold bonus score percentage
		if (state.score.max_sustain_bonus > 0)
			percentage += state.score.sustain_bonus / static_cast<float>(state.score.max_sustain_bonus) * SustainHoldPercBonus;
	}
	else if (state.GetScoreMode() == ScoreMode_F2nd)
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

static void CalcReferenceScoreMixed(ScoreState* ref, PVGameData* pv_game)
{
	const ChanceState& ct = state.chance_time;

	size_t  cur_target_index    = 0;
	int32_t total_double_bonus  = 0;
	int32_t total_sustain_bonus = 0;
	int32_t total_link_bonus    = 0;
	int32_t total_tech_bonus    = 0;
	int32_t total_chance_bonus  = 0;

	for (const PvDscTargetGroup& group : pv_game->pv_data.targets)
	{
		for (int32_t sub_index = 0; sub_index < group.target_count; sub_index++)
		{
			const PvDscTarget& target = group.targets[sub_index];
			TargetStateEx* ex = GetTargetStateEx(cur_target_index, sub_index);
			
			if (ex->IsNormalDoubleNote())
				total_double_bonus += 200;

			if (ex->IsLinkNote())
				total_link_bonus += 200;

			if (ex->IsLongNoteEnd())
				total_sustain_bonus += score::CalculateMaxSustainBonus(ex->prev);
		}

		if (ct.IsValid())
		{
			// NOTE: Chance time bonus score should only be given once per group (multi- or single-note)
			if (ct.CheckTargetInRange(cur_target_index))
				total_chance_bonus += 1000;
		}

		for (const TechZoneState& tech_zone : state.tech_zones)
		{
			if (tech_zone.IsValid() && tech_zone.last_target_index == cur_target_index)
				total_tech_bonus += score::GetTechZoneSuccessBonus();
		}

		pv_game->target_reference_scores[cur_target_index + 1] +=
			total_double_bonus + total_sustain_bonus + total_link_bonus + total_tech_bonus + total_chance_bonus;

		cur_target_index++;
	}

	pv_game->reference_score +=
		total_double_bonus + total_sustain_bonus + total_link_bonus + total_chance_bonus + total_tech_bonus;
	pv_game->reference_score_with_life +=
		total_double_bonus + total_sustain_bonus + total_link_bonus + total_chance_bonus + total_tech_bonus;
}

static void CalcReferenceScoreConsole(ScoreState* ref, PVGameData* pv_game)
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

void score::CalculateScoreReference(int32_t style, ScoreState* ref, PVGameData* pv_game)
{
	switch (style)
	{
	case GameStyle_Console:
		CalcReferenceScoreConsole(ref, pv_game);
		break;
	case GameStyle_Mixed:
		CalcReferenceScoreMixed(ref, pv_game);
		break;
	}
}