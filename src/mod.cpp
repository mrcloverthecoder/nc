#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Shlwapi.h>
#include <d3d11.h>
#include <detours.h>
#include <stdio.h>
#include <stdint.h>
#include <vector>
#include <thirdparty/lazycsv.hpp>
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

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

const int32_t* life_table = reinterpret_cast<const int32_t*>(0x140BE9EA0);

static bool ParseExtraCSV(const void* data, size_t size)
{
	state.groups_ex.clear();

	if (data == nullptr || size == 0)
		return false;

	std::string_view view = std::string_view(static_cast<const char*>(data), size);
	lazycsv::parser<std::string_view> parser { view };

	auto header = parser.header();
	for (const auto row : parser)
	{
		int32_t index = -1;
		int32_t sub_index = 0;
		float length = -1.0f;
		bool is_end = false;

		int32_t cell_index = 0;
		for (const auto cell : row)
		{
			std::string_view name = header.cells(cell_index)[0].trimed();
			std::string value(cell.trimed()); // NOTE: Should usually be small enough that C++ does SSO

			if (name == "index")
				index = std::stoi(value);
			else if (name == "sub_index")
				sub_index = std::stoi(value);
			else if (name == "length")
				length = std::stof(value);
			else if (name == "end")
			{
				if (value == "true")
					is_end = true;
				else if (value == "false")
					is_end = false;
				else
				{
					int32_t i = std::stoi(value);
					is_end = i > 0;
				}
			}

			cell_index++;
		}

		if (index > -1 && sub_index > -1 && sub_index < 4)
		{
			TargetGroupEx& ex = FindOrCreateTargetGroupEx(index);
			ex.target_index = index;
			ex.targets[sub_index].length = length > 0.0f ? length / 1000.0f : -1.0f;
			ex.targets[sub_index].long_end = is_end;
			ex.target_count++;
		}
	}

	return true;
}

// NOTE: Hook TaskPvGame routines to load our own assets
//
HOOK(bool, __fastcall, TaskPvGameInit, 0x1405DA040, uint64_t a1)
{
	state.files_loaded = false;
	state.dsc_loaded = false;
	state.file_state = 0;

	prj::string str;
	prj::string_view strv;
	aet::LoadAetSet(AetSetID, &str);
	spr::LoadSprSet(SprSetID, &strv);
	aet::LoadAetSet(14010080, &str); // AET_NCGAM_TZ
	spr::LoadSprSet(14020080, &strv); // SPR_NCGAM_TZ
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
		sound::UnloadFarc("rom/sound/se_nc.farc");
		state.files_loaded = false;
	}

	return originalTaskPvGameDest(a1);
}

HOOK(void, __fastcall, PVGameReset, 0x1402436F0, void* pv_game)
{
	nc::Print("PVGameReset()\n");

	state.ResetPlayState();
	state.ui.ResetAllLayers();
	originalPVGameReset(pv_game);
}

HOOK(void, __fastcall, PVGameLoaderResetPrePlayScript, 0x140262430, PVGameData** pv_game)
{
	state.ResetPlayState();
	state.ui.ResetAllLayers();
	originalPVGameLoaderResetPrePlayScript(pv_game);
}

HOOK(void, __fastcall, PVGameArcadeReset, 0x14026AE80, PVGameArcade* game)
{
	state.ResetAetData();
	originalPVGameArcadeReset(game);
}

// NOTE: Hook LoadDscCtrl to handle loading our external CSV file
//
HOOK(bool, __fastcall, LoadDscCtrl, 0x14024E270, PVGamePvData* pv_data, prj::string* path, void* a3, bool a4)
{
	prj::string dsc_file_path = *path;
	prj::string csv_file_path = "";

	if (state.nc_chart_entry.has_value())
	{
		const std::string& script_file_name = state.nc_chart_entry.value().script_file_name;
		if (!script_file_name.empty() && script_file_name != "(NULL)")
		{
			dsc_file_path = script_file_name;
			csv_file_path = util::ChangeExtension(script_file_name, ".csv");
		}
	}

	switch (state.file_state)
	{
	case 0:
		if (csv_file_path.empty())
		{
			state.file_state = 3;
			break;
		}

		if (!FileRequestLoad(&state.file_handler, csv_file_path.c_str(), 1))
		{
			state.file_handler = nullptr;
			state.file_state = 3;
			break;
		}

		nc::Print("File (%s) exists!\n", csv_file_path.c_str());
		state.file_state = 1;
		break;
	case 1:
		if (!FileCheckNotReady(&state.file_handler))
			state.file_state = 2;
		break;
	case 2:
		ParseExtraCSV(FileGetData(&state.file_handler), FileGetSize(&state.file_handler));
		FileFree(&state.file_handler);
		state.file_handler = nullptr;
		state.file_state = 3;
		break;
	case 3:
		break;
	}

	if (!state.dsc_loaded)
		state.dsc_loaded = originalLoadDscCtrl(pv_data, &dsc_file_path, a3, a4);

	return state.dsc_loaded && state.file_state == 3;
}

HOOK(int32_t, __fastcall, ParseTargets, 0x140245C50, PVGameData* pv_game)
{
	// TODO: Rewrite this function to accomodate our custom note's scoring
	//
	int32_t ret = originalParseTargets(pv_game);

	// NOTE: Initialize target ex data
	int32_t index = 0;
	for (PvDscTargetGroup& group : pv_game->pv_data.targets)
	{
		for (int i = 0; i < group.target_count; i++)
		{
			TargetGroupEx& ex = FindOrCreateTargetGroupEx(index);
			ex.targets[i].parent = &ex;
			ex.targets[i].target_type = group.targets[i].type;
			ex.targets[i].target_pos = group.targets[i].target_pos;
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
						PvDscTargetGroup* next_group = &pv_game->pv_data.targets[next->parent->target_index];
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

	// NOTE: Patch score reference (Only in Arcade mode; We don't need to patch this in F2nd mode as
	//                              we use our own percentage calculation algorithm)
	if (state.GetScoreMode() == ScoreMode_Arcade)
	{
		int32_t life = 127;
		int32_t total_chance_bonus = 0;
		int32_t total_hold_bonus = 0;
		int32_t total_link_bonus = 0;

		for (size_t i = 0; i < pv_game->pv_data.targets.size(); i++)
		{
			for (int j = 0; j < pv_game->pv_data.targets[i].target_count; j++)
			{
				PvDscTarget& tgt = pv_game->pv_data.targets[i].targets[j];
				TargetStateEx* ex = GetTargetStateEx(i, j);

				if (ex->IsLinkNote())
					total_link_bonus += 200;

				if (state.chance_time.CheckTargetInRange(i))
				{
					total_chance_bonus += 1000;

					// NOTE: The game will apply life bonus to notes in chance time because
					//       it isn't aware of chance times, so we need to deduct those too.
					if (life == 255)
						pv_game->reference_score_with_life -= 10;
				}

				pv_game->target_reference_scores[i + 1] += total_chance_bonus + total_link_bonus;
			}

			if (!state.chance_time.CheckTargetInRange(i))
			{
				life += life_table[21 * GetPvGameplayInfo()->difficulty];
				if (life > 255)
					life = 255;
			}
		}

		pv_game->reference_score += total_chance_bonus + total_hold_bonus + total_link_bonus;
		pv_game->reference_score_with_life += total_chance_bonus + total_hold_bonus + total_link_bonus;
	}
	else
	{
		// NOTE: Calculate percentage parameters for F2nd/Franken score mode
		score::CalculateScoreReference(&state.score, pv_game);
	}

	/*
	for (TargetStateEx& ex : state.target_ex)
	{
		if (ex.next == nullptr && ex.prev == nullptr)
			continue;

		nc::Print("TARGET %03d/%03d:  %02d  %d:%.3f  <%d-%d-%d>\n", ex.target_index, ex.sub_index, ex.target_type, ex.long_end, ex.length, ex.link_start, ex.link_step, ex.link_end);
	}

	for (TechZoneState& tz : state.tech_zones)
	{
		nc::Print("TECHNICAL ZONE: %d -> %d  (%d)\n", tz.first_target_index, tz.last_target_index, tz.GetTargetCount());
	}
	*/

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
		INSTALL_HOOK(PVGameLoaderResetPrePlayScript);
		INSTALL_HOOK(PVGameArcadeReset);
		INSTALL_HOOK(ParseTargets);
		INSTALL_HOOK(LoadDscCtrl);
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