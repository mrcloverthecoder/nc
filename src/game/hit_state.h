#pragma once

#include <nc_state.h>

namespace nc
{
	// NOTE: Returns true if COOL, FINE, SAFE or SAD
	inline bool IsHitCorrect(int32_t hit)
	{
		return (hit >= HitState_Cool && hit <= HitState_Sad) ||
			(hit >= HitState_CoolDouble && hit <= HitState_SadDouble) ||
			(hit >= HitState_CoolTriple && hit <= HitState_SadTriple) ||
			(hit >= HitState_CoolQuad && hit <= HitState_SadQuad);
	}

	// NOTE: Returns true if COOL or FINE
	inline bool IsHitGreat(int32_t hit)
	{
		return hit == HitState_Cool || hit == HitState_Fine ||
			hit == HitState_CoolDouble || hit == HitState_FineDouble ||
			hit == HitState_CoolTriple || hit == HitState_FineTriple ||
			hit == HitState_CoolQuad || hit == HitState_FineQuad;
	}

	// NOTE: Returns true if the hit state is WRONG
	inline bool IsHitWrong(int32_t hit)
	{
		return hit == HitState_WrongCool ||
			hit == HitState_WrongFine ||
			hit == HitState_WrongSafe ||
			hit == HitState_WrongSad;
	}

	// NOTE: Returns true if the hit state is any WRONG or WORST
	inline bool IsHitMiss(int32_t hit)
	{
		return IsHitWrong(hit) || hit == HitState_Worst;
	}

	int32_t GetBasicHitState(int32_t hit_state);
	int32_t JudgeNoteHit(PVGameArcade* game, PvGameTarget** group, TargetStateEx** extras, int32_t group_count, bool* success);
	bool CheckLongNoteHolding(TargetStateEx* ex);
	bool CheckRushNotePops(TargetStateEx* ex);
}