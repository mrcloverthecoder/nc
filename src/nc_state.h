#pragma once

#include <optional>
#include <list>
#include <memory>
#include "diva.h"
#include "input.h"
#include "db.h"
#include "game/score.h"
#include "game/tech_zone.h"
#include "game/sound_effects.h"
#include "game/target.h"
#include "ui/common.h"

constexpr float ChanceTimeRetainedRate = 0.05; // 5%

enum LinkStepState : int32_t
{
	LinkStepState_None      = 0,
	LinkStepState_FadeIn    = 1,
	LinkStepState_Normal    = 2,
	LinkStepState_GlowStart = 3,
	LinkStepState_Glow      = 4,
	LinkStepState_GlowEnd   = 5,
	LinkStepState_Wait      = 6,
	LinkStepState_Idle      = 7
};



struct ChanceState
{
	int32_t first_target_index = -1;
	int32_t last_target_index = -1;
	int32_t targets_hit = 0;
	bool enabled = false;
	bool successful = false;

	inline bool IsValid() const { return first_target_index != -1 && last_target_index != -1; }

	inline void ResetPlayState()
	{
		targets_hit = 0;
		enabled = false;
		successful = false;
	}

	inline int32_t GetTargetCount() const
	{
		if (first_target_index != -1 && last_target_index != -1)
			return last_target_index - first_target_index + 1;
		return 0;
	}

	inline int32_t GetFillRate() const
	{
		const float max_threshold = 0.875f;
		const float target_count = GetTargetCount();
		if (target_count > 0.0f)
		{
			float percent = fminf(targets_hit / target_count / max_threshold, 1.0f);
			return static_cast<int32_t>(percent * 15);
		}

		return 0;
	}

	inline bool CheckTargetInRange(int32_t index) const
	{
		return IsValid() && (index >= first_target_index && index <= last_target_index);
	}
};

enum LayerUI : int32_t
{
	LayerUI_ChanceFrameTop = 0,
	LayerUI_ChanceFrameBottom,
	LayerUI_StarGaugeBase,
	LayerUI_StarGauge,
	LayerUI_ChanceTxt,
	LayerUI_BonusZone,
	LayerUI_BonusZoneText,
	LayerUI_Max
};

struct UIState
{
	int32_t aet_list[LayerUI_Max];
	// TODO: Change all the layers to use AetElement
	std::shared_ptr<AetElement> elements[LayerUI_Max];
	bool aet_visibility[LayerUI_Max];

	UIState()
	{
		memset(aet_list, 0, sizeof(aet_list));
		memset(aet_visibility, 0, sizeof(aet_visibility));
	}

	void SetLayer(int32_t index, bool visible, const char* name, int32_t prio, int32_t flags);
	std::shared_ptr<AetElement>& GetLayer(int32_t id);
	void ResetAllLayers();
};

struct StateEx
{
	static constexpr int32_t MaxHitEffectCount = 4;
	
	FileHandler file_handler;
	int32_t file_state;
	bool dsc_loaded;
	bool files_loaded;
	std::list<TargetStateEx*> target_references;
	std::list<TargetGroupEx> groups_ex;
	ChanceState chance_time;
	std::vector<TechZoneState> tech_zones;
	TechZoneDispState tz_disp;
	size_t tech_zone_index;
	UIState ui;
	int32_t effect_buffer[MaxHitEffectCount] = { };
	int32_t effect_index = 0;
	std::optional<db::SongEntry> nc_song_entry;
	std::optional<db::ChartEntry> nc_chart_entry;
	ScoreState score;

	void ResetPlayState();
	void ResetAetData();
	void Reset();
	bool PushTarget(TargetStateEx* ex);
	bool PopTarget(TargetStateEx* ex);
	void PlayRushHitEffect(const diva::vec2& pos, float scale, bool pop);

	int32_t GetScoreMode() const;
	int32_t GetGameStyle() const;
	int32_t CalculateTotalBonusScore() const;
};

// NOTE: Constants
constexpr uint32_t AetSetID = 16014000;
constexpr uint32_t AetSceneID = 16014001;
constexpr uint32_t SprSetID = 3068121241;

constexpr float KisekiRate = 60.0f; // NOTE: Framerate where the long note kiseki will be updated
constexpr float KisekiInterval = 1.0f / KisekiRate;

// NOTE: Global state
inline MacroState macro_state = { };
inline StateEx state = { };
inline SoundEffectManager se_mgr = { };

// NOTE: Helper functions
TargetGroupEx* FindTargetGroupEx(int32_t index);
TargetGroupEx& FindOrCreateTargetGroupEx(int32_t index);
TargetStateEx* GetTargetStateEx(int32_t index, int32_t sub_index);
TargetStateEx* GetTargetStateEx(const PvGameTarget* org);