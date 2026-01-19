#include <array>
#include <string>
#include <unordered_map>
#include <vector>
#include <string.h>
#include <stdio.h>
#include "hooks.h"
#include "diva.h"
#include "nc_log.h"
#include "nc_state.h"
#include "save_data.h"

#define SCORE_KEY(id, st) (id | (static_cast<uint64_t>(st) << 32))

// HEAVILY MODELED AFTER:
//   https://github.com/blueskythlikesclouds/DivaModLoader/blob/master/Source/DivaModLoader/SaveData.cpp

constexpr int32_t MaxHeaderSize = 128;
constexpr uint8_t CurrentFileVersion[2] = { 1, 3 };
static const char* SaveFileName = "NewClassics.dat";

struct DifficultyScore
{
	uint8_t _data[288];
};

static std::unordered_map<int32_t, ConfigSet> config_sets;
static std::unordered_map<uint64_t, std::array<DifficultyScore, 10>> scores;
static SharedData shared_data;

namespace nc
{
	ConfigSet* FindConfigSet(int32_t id, bool create_if_missing)
	{
		if (auto it = config_sets.find(id); it != config_sets.end())
			return &it->second;
		else if (create_if_missing)
		{
			config_sets[id] = { };
			return &config_sets[id];
		}

		return nullptr;
	}

	static FUNCTION_PTR(void, __fastcall, InitDifficultyScoreStruct, 0x1401D99A0, void* a1);
	static DifficultyScore* FindOrCreateDifficultyScore(int32_t pv, int32_t difficulty, int32_t edition, int32_t style)
	{
		if (difficulty < 0 || difficulty > 5 || pv < 0)
			return nullptr;

		if (auto it = scores.find(SCORE_KEY(pv, style)); it != scores.end())
			return &it->second[difficulty + (edition != 0 ? 5 : 0)];
		
		auto& score = scores[SCORE_KEY(pv, style)];
		for (size_t i = 0; i < 10; i++)
			InitDifficultyScoreStruct(&score[i]);

		return &score[difficulty + (edition != 0 ? 5 : 0)];
	}

	void CreateDefaultSaveData()
	{
		config_sets[-1] = { }; // NOTE: Shared Set A
		config_sets[-2] = { }; // NOTE: Shared Set B
		config_sets[-3] = { }; // NOTE: Shared Set C
	}

	SharedData& GetSharedData()
	{
		return shared_data;
	}

	int32_t GetConfigSetID()
	{
		int32_t set = *reinterpret_cast<int32_t*>(game::GetSaveData() + 0x169410);
		return set < 3 ? -(set + 1) : game::GetGlobalPvID();
	}

	ConfigSet* GetConfigSet() { return FindConfigSet(GetConfigSetID(), true); }
}

// NOTE: Handle reading of save data file
//
struct SaveDataFile
{
	struct Header
	{
		char signature[8];
		uint8_t version_major;
		uint8_t version_minor;
		uint16_t flags;
		int32_t header_size;
		size_t config_set_count;
		size_t score_ex_count;
		// NOTE: This could be a bool because there's ever only one, but I'm just doing it
		//       like this to follow the existing code pattern
		size_t shared_data_count;

		Header()
		{
			memcpy(signature, "NCSAVFIL", 8);
			version_major = CurrentFileVersion[0];
			version_minor = CurrentFileVersion[1];
			flags = 0;
			header_size = MaxHeaderSize;
			config_set_count = 0;
			score_ex_count = 0;
			shared_data_count = 0;
		}
	} header;

	struct ScoreEx
	{
		int32_t pv;
		int32_t style;
		DifficultyScore difficulties[10];
	};

	static_assert(sizeof(Header) <= MaxHeaderSize);
	char _header_padding[MaxHeaderSize - sizeof(Header)];

	SaveDataFile()
	{
		memset(_header_padding, 0, sizeof(_header_padding));
	}

	inline bool IsValid() const
	{
		return memcmp(header.signature, "NCSAVFIL", 8) == 0;
	}

	inline bool IsVersionUnsupported() const
	{
		return header.version_major > CurrentFileVersion[0] ||
			(header.version_major == CurrentFileVersion[0] && header.version_minor > CurrentFileVersion[1]);
	}

	inline const ConfigSet* GetConfigSets() const
	{
		return reinterpret_cast<const ConfigSet*>(reinterpret_cast<const uint8_t*>(this) + MaxHeaderSize);
	}

	inline const ScoreEx* GetScores() const
	{
		return reinterpret_cast<const ScoreEx*>(GetConfigSets() + header.config_set_count);
	}

	inline const SharedData* GetSharedData() const
	{
		return reinterpret_cast<const SharedData*>(GetScores() + header.score_ex_count);
	}
};

static FUNCTION_PTR(void, __fastcall, GetSaveDataPath, 0x1401D70D0, prj::string* out, const prj::string& filename);
static FUNCTION_PTR(bool, __fastcall, ReadDecryptedSaveData, 0x1401D7C90,
	const prj::string& filename, prj::unique_ptr<uint8_t[]>& dst, size_t& dst_size);
static FUNCTION_PTR(bool, __fastcall, EncryptSaveData, 0x1401D79D0,
	const prj::string& key, const void* src, size_t size, prj::unique_ptr<uint8_t[]>& out, size_t& out_size);
static FUNCTION_PTR(void, __fastcall, GetEncryptionKey, 0x1401D76F0, prj::string& key, const prj::string& filename, bool);

HOOK(void, __fastcall, LoadSaveData, 0x1401D7FB0, void* a1)
{
	originalLoadSaveData(a1);

	prj::unique_ptr<uint8_t[]> buffer;
	size_t buffer_size = 0;
	if (!ReadDecryptedSaveData(SaveFileName, buffer, buffer_size))
	{
		nc::Print("Unable to read save data file\n");
		return;
	}

	if (buffer_size == 0)
		return;

	const SaveDataFile* data = reinterpret_cast<const SaveDataFile*>(buffer.get());
	if (!data->IsValid() || data->IsVersionUnsupported())
	{
		nc::Print("Unable to load save data file. The file may be corrupted or made by a newer version of this mod.\n");
		return;
	}

	for (size_t i = 0; i < data->header.config_set_count; i++)
	{
		const ConfigSet& set = data->GetConfigSets()[i];
		config_sets[set.id] = set;
	}

	for (size_t i = 0; i < data->header.score_ex_count; i++)
	{
		const SaveDataFile::ScoreEx& score = data->GetScores()[i];
		auto& score_new = scores[SCORE_KEY(score.pv, score.style)];
		memcpy(score_new.data(), score.difficulties, sizeof(DifficultyScore) * 10);
	}

	if (data->header.shared_data_count > 0)
		shared_data = *data->GetSharedData();

	// NOTE: Remove unsupported features
	if (data->header.version_major == 1 && data->header.version_minor == 3)
	{
		if (data->GetSharedData()->sound_prio == 1)
			shared_data.sound_prio = 0;
	}
}

// NOTE: Handle writing of save data file
//
struct MemoryWriter
{
	std::vector<uint8_t> data;
	size_t pos = 0;

	void Resize(size_t size) { data.resize(size); }
	bool Write(const void* buffer, size_t size)
	{
		if (pos + size > data.size())
			return false;

		memcpy(&data[pos], buffer, size);
		pos += size;
	}

	bool WriteNulls(size_t count)
	{
		if (pos + count > data.size())
			return false;

		memset(&data[pos], 0, count);
		pos += count;
	}

	bool Dump(const char* path)
	{
		prj::unique_ptr<uint8_t[]> encrypted;
		size_t encrypted_size;

		prj::string key;
		GetEncryptionKey(key, SaveFileName, true);

		if (!EncryptSaveData(key, data.data(), data.size(), encrypted, encrypted_size))
			return false;

		if (FILE* file = fopen(path, "wb"); file != nullptr)
		{
			size_t write_count = fwrite(encrypted.get(), encrypted_size, 1, file);
			fclose(file);
			return write_count == 1;
		}

		return false;
	}
};

HOOK(void, __fastcall, SaveSaveData, 0x1401D8280, void* a1)
{
	originalSaveSaveData(a1);

	if (config_sets.empty())
		return;

	SaveDataFile::Header header;
	header.config_set_count = config_sets.size();
	header.score_ex_count = scores.size();
	header.shared_data_count = 1;

	MemoryWriter writer = { };
	writer.Resize(MaxHeaderSize +
		config_sets.size() * sizeof(ConfigSet) +
		scores.size() * sizeof(SaveDataFile::ScoreEx) +
		sizeof(SharedData)
	);
	
	// NOTE: Write header
	writer.Write(&header, sizeof(SaveDataFile::Header));
	writer.WriteNulls(MaxHeaderSize - sizeof(SaveDataFile::Header));

	// NOTE: Write config sets
	for (const auto& [id, set] : config_sets)
	{
		ConfigSet setw = set;
		setw.id = id;
		writer.Write(&setw, sizeof(ConfigSet));
	}

	// NOTE: Write scores
	for (const auto& [key, value] : scores)
	{
		writer.Write(&key, sizeof(uint64_t));
		writer.Write(value.data(), sizeof(DifficultyScore) * 10);
	}

	// NOTE: Write shared data
	writer.Write(&shared_data, sizeof(SharedData));

	// NOTE: Dump data to file
	prj::string path;
	GetSaveDataPath(&path, SaveFileName);
	if (!writer.Dump(path.c_str()))
	{
		// TODO: Add "failed to save" warning
	}
}

// NOTE: This function is implemented in `save_data_imp.asm`
HOOK(DifficultyScore*, __fastcall, GetSavedScoreDifficulty, 0x1401D9DF0, uint64_t, int32_t, int32_t, int32_t);

DifficultyScore* GetSavedScoreDifficultyImp(uint64_t a1, int32_t mode, int32_t difficulty, int32_t edition)
{
	if (mode == 0 && state.nc_chart_entry.has_value())
	{
		int32_t style = state.nc_chart_entry.value().style;
		if (style != GameStyle_Arcade)
		{
			int32_t pv = *reinterpret_cast<int32_t*>(a1);
			if (auto* patched = nc::FindOrCreateDifficultyScore(pv, difficulty, edition, style); patched != nullptr)
				return patched;
		}
	}

	return originalGetSavedScoreDifficulty(a1, mode, difficulty, edition);
}

void nc::InstallSaveDataHooks()
{
	INSTALL_HOOK(LoadSaveData);
	INSTALL_HOOK(SaveSaveData);
	INSTALL_HOOK(GetSavedScoreDifficulty);
}