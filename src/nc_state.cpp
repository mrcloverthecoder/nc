#include "nc_state.h"

void UIState::SetLayer(int32_t index, bool visible, const char* name, int32_t prio, int32_t flags)
{
	aet::Stop(&aet_list[index]);
	aet_visibility[index] = visible;
	aet_list[index] = aet::PlayLayer(
		AetSceneID,
		prio,
		flags,
		name,
		nullptr,
		0,
		nullptr,
		nullptr,
		-1.0f,
		-1.0f,
		0,
		nullptr
	);
}

std::shared_ptr<AetElement>& UIState::GetLayer(int32_t id)
{
	if (!elements[id])
		elements[id] = std::make_shared<AetElement>();
	return elements[id];
}

void UIState::ResetAllLayers()
{
	for (int i = 0; i < LayerUI_Max; i++)
	{
		aet::Stop(&aet_list[i]);
		elements[i].reset();
		aet_visibility[i] = false;
	}
}

void StateEx::ResetPlayState()
{
	target_references.clear();
	for (TargetGroupEx& ex : groups_ex)
		ex.ResetPlayState();
	chance_time.ResetPlayState();
	score.ct_score_bonus = 0;
	score.double_tap_bonus = 0;
	score.sustain_bonus = 0;
	score.link_bonus = 0;
	score.rush_bonus = 0;
	for (TechZoneState& tz : tech_zones)
		tz.ResetPlayState();
	tz_disp.Reset();
	tech_zone_index = 0;
	ResetAetData();
}

void StateEx::ResetAetData()
{
	for (int i = 0; i < MaxHitEffectCount; i++)
		aet::Stop(&effect_buffer[i]);
	effect_index = 0;
	ui.ResetAllLayers();
}

void StateEx::Reset()
{
	target_references.clear();
	groups_ex.clear();
	tech_zones.clear();
	tech_zone_index = 0;
	chance_time.first_target_index = -1;
	chance_time.last_target_index = -1;
	chance_time.targets_hit = 0;
}

bool StateEx::PushTarget(TargetStateEx* ex)
{
	for (TargetStateEx* p : target_references)
		if (p == ex)
			return false;

	target_references.push_back(ex);
	return true;
}

bool StateEx::PopTarget(TargetStateEx* ex)
{
	for (auto it = target_references.begin(); it != target_references.end(); it++)
	{
		if (*it == ex)
		{
			target_references.erase(it);
			return true;
		}
	}

	return false;
}

void StateEx::PlayRushHitEffect(const diva::vec2& pos, float scale, bool pop)
{
	if (effect_index >= MaxHitEffectCount)
		effect_index = 0;

	uint32_t scene_id = pop ? AetSceneID : 3;
	const char* layer_name = pop ? "bal_hit_eff" : "hit_eff00";
	int32_t max_keep = pop ? 4 : 2;

	aet::Stop(&effect_buffer[effect_index]);
	effect_buffer[effect_index] = aet::PlayLayer(
		scene_id,
		8,
		0x20000,
		layer_name,
		&pos,
		0,
		nullptr,
		nullptr,
		-1.0f,
		-1.0f,
		0,
		nullptr
	);

	diva::vec3 scl = { scale, scale, 1.0f };
	aet::SetScale(effect_buffer[effect_index], &scl);

	int32_t count = 0;
	int32_t start = effect_index > 0 ? effect_index - 1 : MaxHitEffectCount - 1;
	for (int i = start; i > 0; i--)
	{
		// NOTE: Break early if we're back to the initial point. Should be when we
		//       loop back around to it.
		if (i == effect_index)
			break;

		// NOTE: Remove hit effect aet if it exceeds the max count
		if (count + 1 > max_keep)
			aet::Stop(&effect_buffer[i]);

		count++;

		// NOTE: Loop around to the back of the array
		if (i == 0)
			i = MaxHitEffectCount - 1;
	}

	effect_index++;
}

int32_t StateEx::GetScoreMode() const
{
	if (GetGameStyle() == GameStyle_Console)
		return ScoreMode_F2nd;
	return ScoreMode_Arcade;
}

int32_t StateEx::GetGameStyle() const
{
	if (nc_chart_entry.has_value())
		return nc_chart_entry.value().style;
	return GameStyle_Arcade;
}

int32_t StateEx::CalculateTotalBonusScore() const
{
	return score.ct_score_bonus + score.double_tap_bonus + score.sustain_bonus + score.link_bonus + score.rush_bonus;
}

TargetGroupEx* FindTargetGroupEx(int32_t index)
{
	for (TargetGroupEx& group : state.groups_ex)
		if (group.target_index == index)
			return &group;
	return nullptr;
}

TargetGroupEx& FindOrCreateTargetGroupEx(int32_t index)
{
	if (TargetGroupEx* group = FindTargetGroupEx(index); group != nullptr)
		return *group;

	TargetGroupEx& group = state.groups_ex.emplace_back();
	group.target_index = index;
	return group;
}

TargetStateEx* GetTargetStateEx(int32_t index, int32_t sub_index)
{
	if (TargetGroupEx* group = FindTargetGroupEx(index); group != nullptr)
		if (sub_index > -1 && sub_index < group->target_count)
			return &group->targets[sub_index];
	return nullptr;
}

TargetStateEx* GetTargetStateEx(const PvGameTarget* org)
{
	int32_t sub_index = 0;
	for (PvGameTarget* prev = org->prev; prev != nullptr; prev = prev->prev)
	{
		if (prev->multi_count != org->multi_count || org->multi_count == -1)
			break;

		sub_index++;
	}

	return GetTargetStateEx(org->target_index, sub_index);
}