#include <array>
#include <stdint.h>
#include <stdio.h>
#include <hooks.h>
#include <nc_log.h>
#include <diva.h>
#include <nc_state.h>
#include <db.h>
#include <util.h>
#include "pv_sel.h"

constexpr uint32_t AetSelSetID   = 14010050;
constexpr uint32_t AetSelSceneID = 14010051;
constexpr uint32_t SprSelSetID   = 14020050;
constexpr const char* StyleNamesInternal[4] = { "arcade", "console", "mixed", "max" };

namespace pvsel
{
	std::string GSWindow::GetModePrefix() { return game::IsFutureToneMode() ? "ps4_" : "nsw_"; }
	std::string GSWindow::GetLanguageSuffix()
	{
		if (GetGameLocale() >= GameLocale_Max)
			return "_en";

		const char* suffixes[GameLocale_Max] = { "_jp", "_en", "_zh", "_tw", "_kr", "_fr", "_it", "_de", "_sp" };
		return suffixes[GetGameLocale()];
	}

	std::string GSWindow::GetBaseLayerName() { return GetModePrefix() + "game_style_win"; }
	std::string GSWindow::GetBaseTextLayerName() { return GetModePrefix() + "game_style_txt" + GetLanguageSuffix(); }
	std::string GSWindow::GetStyleTextLayerName(int32_t style)
	{
		return GetModePrefix() + "game_style_" + StyleNamesInternal[style] + GetLanguageSuffix();
	}

	bool GSWindow::SetAvailableOptions(std::array<int32_t, GameStyle_Max> song_counts)
	{
		int32_t prev_option = options[option_count];
		option_count = 0;
		selected_index = 0;

		for (int32_t i = 0; i < GameStyle_Max; i++)
		{
			if (song_counts[i] > 0)
			{
				if (i == preferred_style)
					selected_index = option_count;

				options[option_count++] = i;
			}
		}

		return options[selected_index] != prev_option;
	}

	void GSWindow::UpdateAet()
	{
		if (!base_win.IsPlaying())
		{
			base_win.SetScene(AetSelSceneID);
			base_win.SetLayer(GetBaseLayerName(), 0x10000, 10, 14, "", "", nullptr);
		}

		if (!base_txt.IsPlaying())
		{
			base_txt.SetScene(AetSelSceneID);
			base_txt.SetLayer(GetBaseTextLayerName(), 0x10000, 10, 14, "", "", nullptr);
		}

		if (options[selected_index] != previous_option)
		{
			if (previous_option != GameStyle_Max)
			{
				prev_style_txt.SetScene(AetSelSceneID);
				prev_style_txt.SetLayer(GetStyleTextLayerName(previous_option), 10, 14, AetAction_OutOnce);
			}

			cur_style_txt.SetScene(AetSelSceneID);
			cur_style_txt.SetLayer(GetStyleTextLayerName(options[selected_index]), 10, 14, AetAction_InLoop);
		}

		if (IsToggleable())
		{
			base_win.SetMarkers("st_on", "ed_on", true);
			base_txt.SetMarkers("st_on", "ed_on", true);
		}
		else
		{
			base_win.SetMarkers("st_off", "ed_off", true);
			base_txt.SetMarkers("st_off", "ed_off", true);
		}

		diva::vec3 offset;
		offset.x = game::IsFutureToneMode() && !IsToggleable() ? -18.0f : 0.0f;
		offset.y = 0.0f;
		offset.z = 0.0f;
		prev_style_txt.SetPosition(offset);
		cur_style_txt.SetPosition(offset);
	}

	void GSWindow::SetVisible(bool visible)
	{
		hidden = !visible;
		base_win.SetVisible(visible);
		base_txt.SetVisible(visible);
		prev_style_txt.SetVisible(visible);
		cur_style_txt.SetVisible(visible);
	}

	bool GSWindow::Ctrl()
	{
		if (game::IsCustomizeSelTaskReady())
		{
			SetVisible(false);
			return false;
		}

		diva::InputState* is = diva::GetInputState(0);
		if (IsToggleable() && (is->IsButtonTapped(92) || is->IsButtonTapped(13)))
		{
			selected_index = util::Wrap(selected_index + 1, 0, option_count - 1);
			preferred_style = options[selected_index];
			dirty = true;
			sound::PlaySoundEffect(1, "se_ft_music_selector_sortfilter_change_01", 1.0f);
		}

		SetVisible(true);

		if (options[selected_index] != previous_option || aet_dirty)
		{
			UpdateAet();
			previous_option = options[selected_index];
			aet_dirty = false;
		}

		// NOTE: Reset dirty flag and return it's value
		bool dirty = this->dirty;
		this->dirty = false;
		return dirty;
	}

	void GSWindow::Disp() const
	{
		if (!base_win.IsPlaying() || hidden || !IsToggleable())
			return;

		const uint32_t key_sprite_ids_nsw[6] = {
			3203571017, // Xbox
			2779278485, // DualShock 4
			2152960672, // JoyCon
			312905805,  // Steam Deck
			257587290,  // Keyboard
			0
		};

		const uint32_t key_sprite_ids_ps4[6] = {
			585831999,  // Xbox
			1474623112, // DualShock 4
			3007666668, // JoyCon
			1166812452, // Steam Deck
			2835797874, // Keyboard
			0
		};

		const uint32_t* sprites = game::IsFutureToneMode() ? key_sprite_ids_ps4 : key_sprite_ids_nsw;
		base_win.DrawSpriteAt("p_key_icon_c", sprites[diva::GetInputState(0)->GetDevice()]);
	}
}

namespace pvsel
{
	static bool assets_loaded = false;

	void RequestAssetsLoad()
	{
		prj::string str;
		prj::string_view strv;
		aet::LoadAetSet(AetSelSetID, &str);
		spr::LoadSprSet(SprSelSetID, &strv);
	}

	bool CheckAssetsLoaded()
	{
		assets_loaded = !aet::CheckAetSetLoading(AetSelSetID) && !spr::CheckSprSetLoading(SprSelSetID);
		return assets_loaded;
	}

	void UnloadAssets()
	{
		aet::UnloadAetSet(AetSelSetID);
		spr::UnloadSprSet(SprSelSetID);
	}

	int32_t GetSelectedStyleOrDefault()
	{
		if (gs_win)
		{
			if (int32_t style = gs_win->GetSelectedStyle(); style != GameStyle_Max)
				return style;
		}

		return GameStyle_Arcade;
	}

	int32_t GetPreferredStyleOrDefault()
	{
		if (gs_win)
		{
			if (int32_t style = gs_win->GetPreferredStyle(); style != GameStyle_Max)
				return style;
		}

		return GameStyle_Arcade;
	}

	extern "C" {
		__declspec(dllexport) bool CheckSongHasStyleAvailable(int32_t pv, int32_t difficulty, int32_t edition, int32_t style)
		{
			const db::SongEntry* song = db::FindSongEntry(pv);
			if (!song)
			{
				// NOTE: Assume the song only has ARCADE style if it doesn't have an nc_db entry
				return style == GameStyle_Arcade;
			}

			return song->FindChart(difficulty, edition, style) != nullptr;
		}
	}

	int32_t CalculateSongStyleCount(int32_t pv, int32_t difficulty, int32_t edition)
	{
		if (const db::SongEntry* song = db::FindSongEntry(pv); song != nullptr)
		{
			int32_t count = 0;
			for (int32_t i = 0; i < GameStyle_Max; i++)
				if (song->FindChart(difficulty, edition, i))
					count++;

			return count;
		}

		return 0;
	}

	prj::vector<pvsel::PvData*> SortWithStyle(const SelPvList& list, int32_t difficulty, int32_t edition, int32_t style)
	{
		prj::vector<pvsel::PvData*> songs;
		if (!list.pv_data)
			return songs;

		for (pvsel::PvData* pv : *list.pv_data)
		{
			if (!pv->data2 || pvsel::CheckSongHasStyleAvailable(*pv->data2->pv_db_entry, difficulty, edition, style))
				songs.push_back(pv);
		}

		return songs;
	}
}

void InstallPvSelSwitchHooks(); // NOTE: Defined in pv_sel_switch.cpp
void InstallPvSelPS4Hooks();    // NOTE: Defined in pv_sel_ps4.cpp

// NOTE: Implementation in `pv_sel_imp.asm`
HOOK(pv_db::PvDBDifficulty*, __fastcall, FindDifficultyEntry, 0x1404BCA70, pv_db::PvDBEntry* pv, int32_t difficulty, int32_t edition);

pv_db::PvDBDifficulty* FindDifficultyEntryImp(pv_db::PvDBEntry* pv, int32_t difficulty, int32_t edition)
{
	pv_db::PvDBDifficulty* entry = originalFindDifficultyEntry(pv, difficulty, edition);
	if (pvsel::CheckSongHasStyleAvailable(pv->pv_id, difficulty, edition, GetState()->GetGameStyle()))
		return entry;
	return nullptr;
}

void InstallPvSelHooks()
{
	if (game::IsFutureToneMode())
		InstallPvSelPS4Hooks();
	else
		InstallPvSelSwitchHooks();

	INSTALL_HOOK(FindDifficultyEntry);
}