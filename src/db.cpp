#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <detours.h>
#include <map>
#include "diva.h"
#include "nc_log.h"
#include "nc_state.h"
#include "util.h"
#include "thirdparty/toml.h"
#include "db.h"

// REFERENCED FROM: https://github.com/BroGamer4256/scale/blob/master/src/mod.cpp
//

constexpr size_t MaxFilePerRom = 10;
constexpr std::array<std::string_view, GameStyle_Max> styles_names_internal = { "ARCADE", "CONSOLE", "MIXED" };
constexpr std::array<std::string_view, ScoreMode_Max> score_mode_names_internal = { "ARCADE", "F2ND" };
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

static bool ParseDifficultyEntry(toml::array& node, db::DifficultyEntry* entry)
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

		db::ChartEntry& chart = entry->charts.emplace_back();
		chart.style = util::GetIndex(styles_names_internal, style, GameStyle_Max);
		chart.score_mode = util::GetIndex(score_mode_names_internal, score_mode, ScoreMode_Arcade);
		chart.difficulty_level = util::GetIndex(pv_lv_names_internal, pv_level, 0);
		chart.script_file_name = table["script_file_name"].value_or("(NULL)");
	}

	return true;
}

static int32_t ParsePVEntry(toml::table& node, db::SongEntry* entry)
{
	int32_t id = node["id"].value_or(-1);
	if (id == -1)
		return -1;

	const char* difficulty_names[MaxDifficultyCount] = { "easy", "normal", "hard", "extreme", "encore" };
	for (size_t i = 0; i < MaxDifficultyCount; i++)
	{
		for (size_t ed = 0; ed < 2; ed++)
		{
			std::string name = util::Format("%s%s", ed > 0 ? "ex_" : "", difficulty_names[i]);
			if (toml::array* diff = node[name].as_array(); diff != nullptr)
			{
				db::DifficultyEntry difficulty_entry = { };
				if (ParseDifficultyEntry(*diff, &difficulty_entry))
					entry->difficulties[i + MaxDifficultyCount * ed] = difficulty_entry;
			}
		}
	}

	entry->star_se_name = node["star_se_name"].value_or(DefaultStarSound);
	entry->double_se_name = node["double_se_name"].value_or(DefaultCopySound);
	entry->long_se_name = node["long_se_name"].value_or(DefaultCopySound);
	entry->star_w_se_name = node["star_w_se_name"].value_or(DefaultStarSound);
	entry->star_long_se_name = node["star_long_se_name"].value_or(DefaultStarSound);
	entry->link_se_name = node["link_se_name"].value_or(DefaultStarSound);
	return id;
}

static bool ParseNCDB(const void* data, size_t size)
{
	std::string_view view(reinterpret_cast<const char*>(data), size);
	auto result = toml::parse(view);
	if (!result)
		return false;

	toml::table& root = result.table();
	if (toml::array* songs = root["songs"].as_array(); songs != nullptr)
	{
		for (auto& node : *songs)
		{
			if (!node.is_table())
				continue;

			db::SongEntry entry = { };
			if (int32_t id = ParsePVEntry(*node.as_table(), &entry); id > 0)
				nc_db.entries[id] = entry;
		}
	}

	return true;
}

HOOK(bool, __fastcall, TaskPvDBCtrl, 0x1404BB290, uint64_t a1)
{
	bool ret = originalTaskPvDBCtrl(a1);
	int32_t pv_db_state = *reinterpret_cast<int32_t*>(a1 + 0x68);

	if (pv_db_state != 0 || nc_db.ready)
		return ret;

	size_t entry_count = nc_db.entries.size();
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
		ParseNCDB(FileGetData(&nc_db.file_handler), FileGetSize(&nc_db.file_handler));
		FileFree(&nc_db.file_handler);
		nc_db.state = 0;

		nc::Print("Loaded %zu new entries.\n", nc_db.entries.size() - entry_count);
		break;
	case 3:
		nc_db.ready = true;
		break;
	}

	return ret;
}

const db::SongEntry* db::FindSongEntry(int32_t pv)
{
	if (auto it = nc_db.entries.find(pv); it != nc_db.entries.end())
		return &it->second;
	return nullptr;
}

const db::DifficultyEntry* db::FindDifficultyEntry(int32_t pv, int32_t difficulty, int32_t edition)
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

//
//
//
static std::vector<ButtonSnd> star_sound_list = {
	{ 1, "Star A", "scratch1_mmv" },
	{ 2, "Star B", "scratch2_mmv" },
	{ 3, "Star C", "scratch3_mmv" },
	{ 4, "Star D", "scratch4_mmv" },
	{ 5, "Star E", "scratch5_mmv" },
	{ 6, "Star F", "scratch6_mmv" },
	{ 7, "Star G", "scratch7_mmv" },
	{ 8, "Star H", "scratch8_mmv" },
	{ 9, "Star I", "scratch9_mmv" }
};

const std::vector<ButtonSnd>* db::GetButtonSoundDB(int32_t type)
{
	if (type == SoundType_Star)
		return &star_sound_list;
}

void InstallDatabaseHooks()
{
	INSTALL_HOOK(TaskPvDBCtrl);
}