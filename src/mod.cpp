#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Shlwapi.h>
#include <d3d11.h>
#include <stdio.h>
#include <stdint.h>
#include <vector>
#include "hooks.h"
#include "game/hit_state.h"
#include "game/target.h"
#include "game/chance_time.h"
#include "nc_log.h"
#include "game/game.h"
#include "ui/pv_sel.h"
#include "ui/customize_sel.h"
#include "ui/result.h"
#include "db.h"
#include "save_data.h"
#include "util.h"
#include "game/dsc.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

const int32_t* life_table = reinterpret_cast<const int32_t*>(0x140BE9EA0);

// NOTE: Hook TaskPvGame routines to load our own assets
//
HOOK(bool, __fastcall, TaskPvGameInit, 0x1405DA040, uint64_t a1)
{
	state.files_loaded = false;
	state.dsc_loaded = false;
	state.file_state = 0;
	
	int32_t pv_id = game::GetPVLoadParam()->data[game::GetPVLoadParam()->data[0].int8].pv_id;
	if (pv_id == 700)
	{
		state.nc_song_entry.reset();
		state.nc_chart_entry.reset();
	}

	prj::string str;
	prj::string_view strv;
	aet::LoadAetSet(AetSetID, &str);
	spr::LoadSprSet(SprSetID, &strv);
	aet::LoadAetSet(14010080, &str); // AET_NCGAM_TZ
	spr::LoadSprSet(14020080, &strv); // SPR_NCGAM_TZ
	
	if (state.nc_song_entry.has_value() && state.nc_song_entry->IsHitEffectsValid())
	{
		aet::LoadAetSet(state.nc_song_entry->target_hit_effect_aetset_id, &str);
		spr::LoadSprSet(state.nc_song_entry->target_hit_effect_sprset_id, &strv);
	}

	if (!sound::RequestFarcLoad("rom/sound/se_nc.farc"))
		nc::Print("Failed to load se_nc.farc\n");

	state.Reset();
	se_mgr.Init();

	macro_state.sensivity = 1.0f - nc::GetSharedData().stick_sensitivity / 100.0f;
	return originalTaskPvGameInit(a1);
}

HOOK(bool, __fastcall, TaskPvGameCtrl, 0x1405DA060, uint64_t a1)
{
	if (!state.files_loaded)
	{
		state.files_loaded = !aet::CheckAetSetLoading(AetSetID) &&
			!spr::CheckSprSetLoading(SprSetID) &&
			!aet::CheckAetSetLoading(14010080) &&
			!spr::CheckSprSetLoading(14020080) &&
			!sound::IsFarcLoading("rom/sound/se_nc.farc");

		if (state.nc_song_entry.has_value() && state.nc_song_entry->IsHitEffectsValid())
		{
			state.files_loaded = state.files_loaded &&
				!aet::CheckAetSetLoading(state.nc_song_entry->target_hit_effect_aetset_id) &&
				!spr::CheckSprSetLoading(state.nc_song_entry->target_hit_effect_sprset_id);
		}
	}

	return originalTaskPvGameCtrl(a1);
}

HOOK(bool, __fastcall, TaskPvGameDest, 0x1405DA0A0, uint64_t a1)
{
	state.ResetPlayState();
	state.ui.ResetAllLayers();

	if (state.files_loaded)
	{
		aet::UnloadAetSet(AetSetID);
		spr::UnloadSprSet(SprSetID);
		aet::UnloadAetSet(14010080);
		spr::UnloadSprSet(14020080);
		if (state.nc_song_entry.has_value() && state.nc_song_entry->IsHitEffectsValid())
		{
			aet::UnloadAetSet(state.nc_song_entry->target_hit_effect_aetset_id);
			spr::UnloadSprSet(state.nc_song_entry->target_hit_effect_sprset_id);
			state.fail_target_effect_map.clear();
			state.success_target_effect_map.clear();
		}
		sound::UnloadFarc("rom/sound/se_nc.farc");
		state.files_loaded = false;
	}

	return originalTaskPvGameDest(a1);
}

// NOTE: Resets gameplay state.
HOOK(void, __fastcall, PVGameReset, 0x1402436F0, PVGameData* pv_game)
{
	state.ResetPlayState();
	originalPVGameReset(pv_game);
}

// NOTE: Last step of the PVGameLoader loop. Reset the game state to prevent dangling
//       note AETs when the game starts.
HOOK(bool, __fastcall, PVGameLoaderFinishUp, 0x140262730, void* a1)
{
	state.ResetPlayState();
	return originalPVGameLoaderFinishUp(a1);
}

// NOTE: Specifically stops the gameplay AETs from playing; Doesn't reset gameplay state.
HOOK(void, __fastcall, PVGameArcadeReset, 0x14026AE80, PVGameArcade* game)
{
	state.ResetAetData();
	originalPVGameArcadeReset(game);
}

HOOK(int32_t, __fastcall, ParseTargets, 0x140245C50, PVGameData* pv_game)
{
	// TODO: Rewrite this function to accomodate our custom note's scoring
	//
	int32_t ret = originalParseTargets(pv_game);

	// NOTE: Initialize extra data for targets
	int32_t index = 0;
	for (PvDscTargetGroup& group : pv_game->pv_data.targets)
	{
		auto shared_data = std::make_shared<TargetStateExShared>();
		shared_data->Reset();

		for (int i = 0; i < group.target_count; i++)
		{
			// NOTE: Patch existing extra data with new information
			//
			if (TargetStateEx* ex = GetTargetStateEx(index, i); ex != nullptr)
			{
				ex->target_type = group.targets[i].type;
				ex->shared_data = shared_data;
				continue;
			}

			// NOTE: Append new extra data to the list
			//
			TargetStateEx ex = { };
			ex.target_index = index;
			ex.sub_index = i;
			ex.target_type = group.targets[i].type;
			ex.shared_data = shared_data;
			ex.ResetPlayState();
			state.target_ex.push_back(ex);
		}

		index++;
	}

	// NOTE: Resolve target relations
	//
	auto findNextTarget = [&pv_game](size_t start_index, int32_t start_sub, int32_t type, int32_t type2, bool end)
	{
		for (size_t i = start_index; i < pv_game->pv_data.targets.size(); i++)
		{
			PvDscTargetGroup* group = &pv_game->pv_data.targets[i];
			for (int sub = start_sub; sub < group->target_count; sub++)
			{
				TargetStateEx* ex = GetTargetStateEx(i, sub);
				if (group->targets[sub].type == type || group->targets[sub].type == type2 || type == -1)
				{
					if (ex->long_end == end)
						return std::pair(&group->targets[sub], ex);
				}
			}
		}

		return std::pair<PvDscTarget*, TargetStateEx*>(nullptr, nullptr);
	};

	TargetStateEx* previous = nullptr;
	for (size_t i = 0; i < pv_game->pv_data.targets.size(); i++)
	{
		PvDscTargetGroup* group = &pv_game->pv_data.targets[i];
		for (int sub = 0; sub < group->target_count; sub++)
		{
			PvDscTarget* tgt = &group->targets[sub];
			TargetStateEx* ex = GetTargetStateEx(i, sub);

			// NOTE: Link long note pieces together
			//
			if (ex->IsLongNoteStart())
			{
				TargetStateEx* next = findNextTarget(i + 1, sub, tgt->type, -1, true).second;
				if (next != nullptr)
				{
					ex->next = next;
					next->prev = ex;

					// NOTE: Auto-calculate long note length if necessary
					if (ex->length < 0.0f)
					{
						PvDscTargetGroup* next_group = &pv_game->pv_data.targets[next->target_index];
						ex->length = static_cast<float>(next_group->hit_time - group->hit_time) / 1000000000.0f;
					}
				}
			}
			// NOTE: Resolve LinkStars
			else if (tgt->type == TargetType_LinkStar || tgt->type == TargetType_LinkStarEnd)
			{
				if (tgt->type == TargetType_LinkStar)
				{
					if (ex->prev == nullptr)
						ex->link_start = true;

					TargetStateEx* next = findNextTarget(
						i + 1,
						sub,
						TargetType_LinkStar,
						TargetType_LinkStarEnd,
						false
					).second;

					if (next != nullptr)
					{
						ex->next = next;
						next->prev = ex;
					}

					ex->link_step = true;
				}
				else if (tgt->type == TargetType_LinkStarEnd)
				{
					ex->link_step = true;
					ex->link_end = true;
				}
			}
			// NOTE: Resolve rush note info
			else if (ex->IsRushNote())
				ex->bal_max_hit_count = ex->length * 4.5f;
		}
	}

	// NOTE: Find chance time
	//
	int32_t pos = 1;
	int64_t cur_time = -1;
	int64_t last_time = -1;
	int64_t chance_start_time = -1;
	int64_t chance_end_time = -1;
	std::vector<std::pair<int64_t, int64_t>> tech_zone_times;
	while (true)
	{
		int32_t branch = 0;
		int32_t time = 0;
		pos = FindNextCommand(&pv_game->pv_data, 26, &time, &branch, pos);
		cur_time = static_cast<int64_t>(time) * 10000;

		if (pos != -1)
		{
			if (time != -1)
				last_time = cur_time;

			int32_t difficulty = pv_game->pv_data.script_buffer[pos + 1];
			int32_t mode = pv_game->pv_data.script_buffer[pos + 2];

			if (dsc::IsCurrentDifficulty(difficulty))
			{
				switch (mode)
				{
				case ModeSelect_ChanceStart:
					chance_start_time = last_time;
					break;
				case ModeSelect_ChanceEnd:
					chance_end_time = last_time;
					break;
				case ModeSelect_TechZoneStart:
					tech_zone_times.emplace_back(last_time, -1);
					break;
				case ModeSelect_TechZoneEnd:
					if (tech_zone_times.size() > 0)
						tech_zone_times.back().second = last_time;
					break;
				}
			}

			pos += dsc::GetOpcodeInfo(26)->length + 1;
			continue;
		}

		break;
	}

	// NOTE: Figure out which notes are part of the chance time
	//
	state.chance_time.first_target_index = -1;
	state.chance_time.last_target_index = -1;
	if (chance_start_time != -1 && chance_end_time != -1)
	{
		for (size_t i = 0; i < pv_game->pv_data.targets.size(); i++)
		{
			PvDscTargetGroup* tgt = &pv_game->pv_data.targets[i];
			if (tgt->hit_time >= chance_start_time && tgt->hit_time <= chance_end_time)
			{
				if (state.chance_time.first_target_index == -1)
					state.chance_time.first_target_index = i;
				state.chance_time.last_target_index = i;
			}
		}
	}

	// NOTE: Find valid technical zones.
	//
	for (auto& [start_time, end_time] : tech_zone_times)
	{
		if (start_time == -1 || end_time == -1)
			continue;

		TechZoneState tz = { };
		for (size_t i = 0; i < pv_game->pv_data.targets.size(); i++)
		{
			PvDscTargetGroup& tgt = pv_game->pv_data.targets[i];
			if (tgt.hit_time >= start_time && tgt.hit_time <= end_time)
			{
				if (tz.first_target_index == -1)
					tz.first_target_index = i;
				tz.last_target_index = i;
			}
		}

		if (tz.IsValid())
			state.tech_zones.push_back(tz);
	}

	score::CalculateScoreReference(state.GetGameStyle(), &state.score, pv_game);
	return pv_game->reference_score;
}

extern "C"
{
	void __declspec(dllexport) Init()
	{
		freopen("CONOUT$", "w", stdout);

		// NOTE: Patch target type check in PVGameTarget::CreateAet (0x150D54750)
		WRITE_MEMORY(0x150D54766, uint8_t, TargetType_Max - 12);

		// NOTE: Install hooks
		INSTALL_HOOK(TaskPvGameInit);
		INSTALL_HOOK(TaskPvGameCtrl);
		INSTALL_HOOK(TaskPvGameDest);
		INSTALL_HOOK(PVGameReset);
		INSTALL_HOOK(PVGameLoaderFinishUp);
		INSTALL_HOOK(PVGameArcadeReset);
		INSTALL_HOOK(ParseTargets);
		// INSTALL_HOOK(LoadDscCtrl);
		InstallDSCHooks();
		InstallGameHooks();
		InstallTargetHooks();
		InstallDatabaseHooks();
		nc::InstallResultsHook();
		nc::CreateDefaultSaveData();
		nc::InstallSaveDataHooks();
		nc::InstallInputHooks();
	}

	void __declspec(dllexport) PostInit()
	{
		InstallPvSelHooks();
		InstallCustomizeSelHooks();
	}
};