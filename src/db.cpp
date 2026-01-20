#include <map>
#include "diva.h"
#include "nc_log.h"
#include "nc_state.h"
#include "util.h"
#include "thirdparty/toml.h"
#include "db.h"

// REFERENCED FROM: https://github.com/BroGamer4256/scale/blob/master/src/mod.cpp
//

// TODO: Make nc_db stackable (one mod can add mixed and another can add console charts)
//       but for that I have to figure out how to properly handle the default arcade entry

constexpr size_t MaxFilePerRom = 10;
constexpr std::array<std::string_view, GameStyle_Max> styles_names_internal = { "ARCADE", "CONSOLE", "MIXED" };
constexpr std::array<std::string_view, 21> pv_lv_names_internal = {
	"PV_LV_00_0", "PV_LV_00_5", "PV_LV_01_0", "PV_LV_01_5",
	"PV_LV_02_0", "PV_LV_02_5", "PV_LV_03_0", "PV_LV_03_5",
	"PV_LV_04_0", "PV_LV_04_5", "PV_LV_05_0", "PV_LV_05_5",
	"PV_LV_06_0", "PV_LV_06_5", "PV_LV_07_0", "PV_LV_07_5",
	"PV_LV_08_0", "PV_LV_08_5", "PV_LV_09_0", "PV_LV_09_5",
	"PV_LV_10_0"
};

struct ChartDatabase
{
	std::map<int32_t, db::SongEntry> entries;
	FileHandler file_handler = nullptr;
	int32_t state = 0;
	size_t rom_dir_index = 0;
	bool ready = false;
} static nc_db;

static bool TryFindNextNCDBFile(std::string* output_path)
{
	auto* rom_dirs = GetRomDirectories();

	bool found = false;
	while (!found)
	{
		if (nc_db.rom_dir_index >= rom_dirs->size())
			return false;

		prj::string& root = rom_dirs->at(nc_db.rom_dir_index);

		// NOTE: Skip the "./" directory or else we'll load this mod's file twice
		if (root.size() >= 2 && root[0] == '.' && root[1] == '/')
		{
			nc_db.rom_dir_index++;
			continue;
		}

		prj::string file_path = util::Format("%s/rom/nc_db.toml", root.c_str()).c_str();
		prj::string fixed_path;
		if (FileCheckExists(&file_path, &fixed_path))
		{
			*output_path = file_path;
			found = true;
		}
		
		nc_db.rom_dir_index++;
	}

	return true;
}

static bool ParseDifficultyEntry(toml::array& node, db::DifficultyEntry& entry)
{
	size_t length = node.size() > MaxChartsPerDifficulty ? MaxChartsPerDifficulty : node.size();
	for (size_t i = 0; i < length; i++)
	{
		if (!node[i].is_table())
			continue;

		toml::table& table = *node[i].as_table();
		std::string style = table["style"].value_or("ARCADE");
		std::string score_mode = table["score_mode"].value_or("ARCADE");
		std::string pv_level = table["level"].value_or("PV_LV_00_0");

		db::ChartEntry& chart = entry.FindOrCreateChart(util::GetIndex(styles_names_internal, style, GameStyle_Max));
		chart.difficulty_level = util::GetIndex(pv_lv_names_internal, pv_level, 0);
		chart.script_file_name = table["script_file_name"].value_or("(NULL)");
	}

	return true;
}

static bool ParsePVEntry(toml::table& node, db::SongEntry* entry)
{
	const char* difficulty_names[MaxDifficultyCount] = { "easy", "normal", "hard", "extreme", "encore" };
	for (size_t i = 0; i < MaxDifficultyCount; i++)
	{
		for (size_t ed = 0; ed < 2; ed++)
		{
			std::string name = util::Format("%s%s", ed > 0 ? "ex_" : "", difficulty_names[i]);
			if (toml::array* diff = node[name].as_array(); diff != nullptr)
			{
				auto& difficulty_entry = entry->difficulties[i + MaxDifficultyCount * ed];
				if (difficulty_entry.has_value())
				{
					difficulty_entry.value().charts.clear();
					ParseDifficultyEntry(*diff, difficulty_entry.value());
				}
			}
		}
	}

	entry->star_se_name = node["star_se_name"].value_or(DefaultStarSound);
	entry->double_se_name = node["double_se_name"].value_or(DefaultCopySound);
	entry->long_se_name = node["long_se_name"].value_or(DefaultCopySound);
	entry->star_w_se_name = node["star_w_se_name"].value_or(DefaultStarSound);
	entry->star_long_se_name = node["star_long_se_name"].value_or(DefaultStarSound);
	entry->link_se_name = node["link_se_name"].value_or(DefaultStarSound);

	if (!node.contains("hit_effect") || !node["hit_effect"].is_string())
	{
		entry->target_hit_effect_aetset_id = node["target_hit_effect_aetset_id"].value_or(0xFFFFFFFF);
		entry->target_hit_effect_scene_id = node["target_hit_effect_scene_id"].value_or(0xFFFFFFFF);
		entry->target_hit_effect_sprset_id = node["target_hit_effect_sprset_id"].value_or(0xFFFFFFFF);
	}
	else
	{
		std::string set_name = node["hit_effect"].value_or("?");
		std::string scene_name = set_name + "_MAIN";
		AetSetInfo* set_info = GetAetSetInfoByName(nullptr, set_name);
		AetSceneInfo* scene_info = GetAetSceneInfoByName(nullptr, scene_name);

		entry->target_hit_effect_aetset_id = set_info->id;
		entry->target_hit_effect_sprset_id = set_info->spr_set_id;
		entry->target_hit_effect_scene_id  = scene_info->id;
	}

	return true;
}

static int32_t ParseNCDB(const void* data, size_t size)
{
	std::string_view view(reinterpret_cast<const char*>(data), size);
	if (auto result = toml::parse(std::string_view(reinterpret_cast<const char*>(data), size)); result.succeeded())
	{
		int32_t new_count = 0;
		toml::table& root = result.table();
		if (toml::array* songs = root["songs"].as_array(); songs != nullptr)
		{
			for (auto& node : *songs)
			{
				if (!node.is_table())
					continue;

				int32_t id = node.as_table()->at("id").value_or(-1);
				if (id > 0)
				{
					if (ParsePVEntry(*node.as_table(), &nc_db.entries[id]))
						new_count++;
				}
			}
		}

		return new_count;
	}

	return 0;
}

HOOK(bool, __fastcall, TaskPvDBParseEntry, 0x1404B1020, uint64_t a1, pv_db::PvDBEntry* entry, void* prop, int32_t id)
{
	if (originalTaskPvDBParseEntry(a1, entry, prop, id))
	{
		if (nc_db.ready)
			return true;

		db::SongEntry& nc_song = nc_db.entries[id];
		for (int32_t i = 0; i < 5; i++)
		{
			for (const auto& diff : entry->difficulties[i])
			{
				if (diff.edition != 0 && diff.edition != 1)
					continue;

				db::ChartEntry& chart = nc_song.FindOrCreateChart(diff.difficulty, diff.edition, GameStyle_Arcade);
				chart.difficulty_level = diff.level;
			}
		}

		return true;
	}

	return false;
}

HOOK(bool, __fastcall, TaskPvDBCtrl, 0x1404BB290, uint64_t a1)
{
	bool ret = originalTaskPvDBCtrl(a1);
	int32_t pv_db_state = *reinterpret_cast<int32_t*>(a1 + 0x68);

	if (pv_db_state != 0 || nc_db.ready)
		return ret;

	int32_t entry_count = 0;
	std::string path = "";
	switch (nc_db.state)
	{
	case 0:
		if (TryFindNextNCDBFile(&path))
		{
			if (!path.empty())
			{
				nc::Print("nc_db::load(%s)\n", path.c_str());
				FileRequestLoad(&nc_db.file_handler, path.c_str(), 1);
				nc_db.state = 1;
			}
		}
		else
			nc_db.state = 3;

		break;
	case 1:
		if (!FileCheckNotReady(&nc_db.file_handler))
			nc_db.state = 2;
		break;
	case 2:
		entry_count = ParseNCDB(FileGetData(&nc_db.file_handler), FileGetSize(&nc_db.file_handler));
		nc::Print("Loaded %d new entries.\n", entry_count);

		FileFree(&nc_db.file_handler);
		nc_db.state = 0;
		break;
	case 3:
		nc_db.ready = true;
		break;
	}

	return ret;
}

static FUNCTION_PTR(void*, __fastcall, GetMdataManager, 0x14043E040);
static FUNCTION_PTR(prj::list<prj::string>*, __fastcall, MdataManager_GetDBPrefixes, 0x14043F8A0, void* a1);

HOOK(void, __fastcall, InitMdataPvDbPaths, 0x1404BBE80)
{
	originalInitMdataPvDbPaths();
	prj::list<prj::string>& paths = *reinterpret_cast<prj::list<prj::string>*>(0x141753780 + 0x70);
	prj::list<prj::string>& prefixes = *MdataManager_GetDBPrefixes(GetMdataManager());

	for (const prj::string& prefix : prefixes)
	{
		prj::string pv_db_path = "rom/" + prefix + "nc_pv_db.txt";
		prj::string pv_field_path = "rom/" + prefix + "nc_pv_field.txt";

		if (FileCheckExists(&pv_db_path, nullptr))
			paths.push_back(pv_db_path);

		if (FileCheckExists(&pv_field_path, nullptr))
			paths.push_back(pv_field_path);
	}
}

db::ChartEntry& db::DifficultyEntry::FindOrCreateChart(int32_t style)
{
	for (ChartEntry& chart : charts)
		if (chart.style == style)
			return chart;

	ChartEntry& chart = charts.emplace_back();
	chart.style = style;
	return chart;
}

db::ChartEntry& db::SongEntry::FindOrCreateChart(int32_t difficulty, int32_t edition, int32_t style)
{
	auto& diff = difficulties[MaxDifficultyCount * edition + difficulty];
	if (!diff.has_value())
		diff = db::DifficultyEntry();

	return diff.value().FindOrCreateChart(style);
}

const db::ChartEntry* db::SongEntry::FindChart(int32_t difficulty, int32_t edition, int32_t style) const
{
	if (difficulty < 0 || difficulty >= MaxDifficultyCount || edition < 0 || edition >= MaxEditionCount)
		return nullptr;

	const auto& diff = difficulties[MaxDifficultyCount * edition + difficulty];
	if (!diff.has_value())
		return nullptr;

	for (const ChartEntry& chart : diff.value().charts)
		if (chart.style == style)
			return &chart;

	return nullptr;
}

extern "C" {
	const __declspec(dllexport) db::SongEntry* db::FindSongEntry(int32_t pv)
	{
		if (auto it = nc_db.entries.find(pv); it != nc_db.entries.end())
			return &it->second;
		return nullptr;
	}

	const __declspec(dllexport) db::DifficultyEntry* db::FindDifficultyEntry(int32_t pv, int32_t difficulty, int32_t edition)
	{
		if (difficulty < 0 || difficulty > MaxDifficultyCount || (edition != 0 && edition != 1))
			return nullptr;

		if (auto* song = FindSongEntry(pv); song != nullptr)
		{
			if (const auto& entry = song->difficulties[MaxDifficultyCount * edition + difficulty]; entry.has_value())
				return &entry.value();
		}

		return nullptr;
	}

	const __declspec(dllexport) db::ChartEntry* db::FindChart(int32_t pv, int32_t difficulty, int32_t edition, int32_t style)
	{
		if (style < 0 || style >= GameStyle_Max)
			return nullptr;

		if (auto* entry = FindDifficultyEntry(pv, difficulty, edition); entry != nullptr)
		{
			for (auto& chart : entry->charts)
				if (chart.style == style)
					return &chart;
		}

		return nullptr;
	}

	__declspec(dllexport) bool db::DbReady() {
		return nc_db.ready;
	}
}

void InstallDatabaseHooks()
{
	INSTALL_HOOK(TaskPvDBParseEntry);
	INSTALL_HOOK(TaskPvDBCtrl);
	INSTALL_HOOK(InitMdataPvDbPaths);
}