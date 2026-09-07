#include "build_info.h"
#include "nc_state.h"
#include <save_data.h>

extern "C"
{
	__declspec(dllexport) int32_t GetModVersion()
	{
		return nc::VersionSuper * 100 + nc::VersionMajor * 10 + nc::VersionMinor;
	}

	__declspec(dllexport) int32_t GetStateGameStyle()
	{
		return GetState()->GetGameStyle();
	}

	__declspec(dllexport) bool SetStateSong(int32_t pv, int32_t difficulty, int32_t edition, int32_t style)
	{
		if (const auto* entry = db::FindSongEntry(pv); entry != nullptr)
		{
			GetState()->nc_song_entry = *entry;
			if (const auto* chart = entry->FindChart(difficulty, edition, style); chart != nullptr)
			{
				GetState()->nc_chart_entry = *chart;
				return true;
			}
		}
		return false;
	}

	__declspec(dllexport) void ResetStateSong()
	{
		GetState()->nc_song_entry.reset();
		GetState()->nc_chart_entry.reset();
	}

	__declspec(dllexport) int32_t GetSharedTechZoneStyle()
	{
		return nc::GetSharedData().tech_zone_style;
	}
}