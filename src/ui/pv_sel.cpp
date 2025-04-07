#include <array>
#include <stdint.h>
#include <stdio.h>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <detours.h>
#include <Helpers.h>
#include <nc_log.h>
#include <diva.h>
#include <nc_state.h>
#include <db.h>

constexpr uint32_t AetSelSetID   = 14010050;
constexpr uint32_t AetSelSceneID = 14010051;
constexpr uint32_t SprSelSetID   = 14020050;
constexpr const char* StyleNamesInternal[4] = { "arcade", "console", "mixed", "max" };

static FUNCTION_PTR(bool, __fastcall, CheckCustomizeSelReady, 0x1401DE790);
static bool IsFutureToneMode() { return *reinterpret_cast<bool*>(0x1414AB9E3); }

static std::string GetModePrefix() { return IsFutureToneMode() ? "ps4_" : "nsw_"; }
static std::string GetLanguageSuffix()
{
	if (GetGameLocale() >= GameLocale_Max)
		return "_en";

	const char* suffixes[GameLocale_Max] = { "_jp", "_en", "_zh", "_tw", "_kr", "_fr", "_it", "_de", "_sp" };
	return suffixes[GetGameLocale()];
}

static std::string GetWindowLayerName() { return GetModePrefix() + "game_style_win"; }
static std::string GetWindowTextLayerName() { return GetModePrefix() + "game_style_txt" + GetLanguageSuffix(); }
static std::string GetStyleLayerName(int32_t style)
{
	return GetModePrefix() + "game_style_" + StyleNamesInternal[style] + GetLanguageSuffix();
}

namespace pvsel
{
	bool assets_loaded = false;
	bool ui_visible    = true;
	int32_t info_style_index  = 0;
	int32_t sel_info_style[2] = { 0, 0 };
	int32_t sel_info_win      = 0;
	int32_t sel_info_txt      = 0;

	int32_t cur_pv_id      = -1;
	int32_t cur_diff       = -1;
	int32_t cur_edition    = -1;
	int32_t prev_sel_mode  = GameStyle_Max;
	int32_t selected_index = 0;
	int32_t mode_count     = 0;
	int32_t modes[3]       = { GameStyle_Max, GameStyle_Max, GameStyle_Max };

	static bool IsModeChangeable() { return mode_count > 1; }
	static void PopulateModes(int32_t pv, int32_t diff, int32_t ed)
	{
		mode_count = 0;

		if (const auto* entry = db::FindDifficultyEntry(pv, diff, ed); entry != nullptr)
		{
			for (const db::ChartEntry& chart : entry->charts)
			{
				if (chart.style == GameStyle_Max)
					continue;

				modes[mode_count++] = chart.style;
			}
		}
		else
		{
			modes[0] = GameStyle_Arcade;
			mode_count++;
		}
	}


	static void ResetAet()
	{
		info_style_index = 0;
		aet::Stop(&sel_info_style[0]);
		aet::Stop(&sel_info_style[1]);
		aet::Stop(&sel_info_win);
		aet::Stop(&sel_info_txt);
	}

	static void SetAet()
	{
		if (mode_count < 1)
		{
			ResetAet();
			return;
		}

		if (sel_info_win == 0)
		{
			std::string win = GetWindowLayerName();
			sel_info_win = aet::PlayLayer(AetSelSceneID, 10, 0x10000, win.c_str(), nullptr, nullptr, nullptr);
		}

		if (sel_info_txt == 0)
		{
			std::string txt = GetWindowTextLayerName();
			sel_info_txt = aet::PlayLayer(AetSelSceneID, 10, 0x10000, txt.c_str(), nullptr, nullptr, nullptr);
		}
		
		if (modes[selected_index] != prev_sel_mode)
		{
			std::string layer = GetStyleLayerName(modes[selected_index]);

			if (sel_info_style[info_style_index] != 0)
			{
				std::string prev_layer = GetStyleLayerName(prev_sel_mode);
				aet::Stop(&sel_info_style[info_style_index]);
				sel_info_style[info_style_index] = aet::PlayLayer(AetSelSceneID, 10, prev_layer.c_str(), AetAction_OutOnce);

				if (++info_style_index > 1)
					info_style_index = 0;
			}

			aet::Stop(&sel_info_style[info_style_index]);
			sel_info_style[info_style_index] = aet::PlayLayer(AetSelSceneID, 10, layer.c_str(), AetAction_InLoop);
		}

		diva::vec3 offset;
		offset.x = IsFutureToneMode() && !IsModeChangeable() ? -18.0f : 0.0f;
		offset.y = 0.0f;
		offset.z = 0.0f;
		aet::SetPosition(sel_info_style[0], &offset);
		aet::SetPosition(sel_info_style[1], &offset);
	}

	static void SetAetVisibility(bool visible)
	{
		aet::SetVisible(sel_info_style[0], visible);
		aet::SetVisible(sel_info_style[1], visible);
		aet::SetVisible(sel_info_win, visible);
		aet::SetVisible(sel_info_txt, visible);
		ui_visible = visible;
	}

	static void DrawKeyIcon()
	{
		if (sel_info_win == 0 || !ui_visible || !IsModeChangeable())
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

		int32_t device = diva::GetInputState(0)->GetDevice();

		AetComposition comp;
		aet::GetComposition(&comp, pvsel::sel_info_win);

		if (auto it = comp.find("p_key_icon_c"); it != comp.end())
		{
			SprArgs args = { };
			args.id = IsFutureToneMode() ? key_sprite_ids_ps4[device] : key_sprite_ids_nsw[device];
			args.trans = it->second.position;
			args.scale.x = it->second.matrix.row0.x;
			args.scale.y = it->second.matrix.row1.y;
			args.scale.z = 1.0f;
			args.resolution_mode_sprite = 14;
			args.resolution_mode_screen = 14;
			memset(args.color, 0xFF, 4);
			args.attr = 0x400000; // SPR_ATTR_CTR_CC
			args.priority = 11;
			spr::DrawSprite(&args);
		}
	}

	static void SetStateSelectedMode()
	{
		if (modes[selected_index] == GameStyle_Arcade)
		{
			state.nc_song_entry.reset();
			state.nc_chart_entry.reset();
		}
		else
		{
			if (const db::SongEntry* song = db::FindSongEntry(cur_pv_id); song != nullptr)
			{
				state.nc_song_entry = *song;
				state.nc_chart_entry = db::FindDifficultyEntry(cur_pv_id, cur_diff, cur_edition)->charts[selected_index];
			}
		}
	}

	static void SetSelectedSong(int32_t pv, int32_t diff, int32_t ed)
	{
		if (pv != cur_pv_id || diff != cur_diff || ed != cur_edition)
		{
			prev_sel_mode = modes[selected_index];
			PopulateModes(pv, diff, ed);

			for (int i = 0; i < mode_count; i++)
			{
				if (modes[i] == prev_sel_mode)
					selected_index = i;
			}

			SetAet();
			SetStateSelectedMode();
		}

		cur_pv_id = pv;
		cur_diff = diff;
		cur_edition = ed;
	}

	static void ResetSelectedSong()
	{
		cur_pv_id = -1;
		cur_diff = -1;
		cur_edition = -1;
		prev_sel_mode = GameStyle_Max;
		selected_index = 0;
		mode_count = 0;
		modes[0] = GameStyle_Max;
		modes[1] = GameStyle_Max;
		modes[2] = GameStyle_Max;
	}

	static int32_t IterModeSelection()
	{
		if (mode_count == 0)
		{
			selected_index = -1;
			return GameStyle_Max;
		}

		prev_sel_mode = modes[selected_index];
		if (++selected_index >= mode_count)
			selected_index = 0;

		SetAet();
		SetStateSelectedMode();
		return modes[selected_index];
	}

	static void UpdateCtrl(int32_t pv_id, int32_t difficulty, int32_t edition)
	{
		if (!CheckCustomizeSelReady())
		{
			pvsel::SetAetVisibility(true);
			pvsel::SetSelectedSong(pv_id, difficulty, edition);

			diva::InputState* is = diva::GetInputState(0);
			if (is->IsButtonTapped(92) || is->IsButtonTapped(13)) // F7 or L2
			{
				pvsel::IterModeSelection();
				if (pvsel::IsModeChangeable())
					sound::PlaySoundEffect(1, "se_ft_music_selector_set_change_01", 1.0f);
			}
		}
		else
			pvsel::SetAetVisibility(false);
	}

	static void RequestAetLoad()
	{
		prj::string str;
		prj::string_view strv;
		aet::LoadAetSet(AetSelSetID, &str);
		spr::LoadSprSet(SprSelSetID, &strv);
	}

	static bool CheckAssetsLoaded()
	{
		assets_loaded = !aet::CheckAetSetLoading(AetSelSetID) && !spr::CheckSprSetLoading(SprSelSetID);
		return assets_loaded;
	}

	static void UnloadAet()
	{
		ResetAet();
		aet::UnloadAetSet(AetSelSetID);
		spr::UnloadSprSet(SprSelSetID);
	}
}

HOOK(bool, __fastcall, PVSelectorSwitchInit, 0x1406ED9D0, uint64_t a1)
{
	pvsel::RequestAetLoad();
	return originalPVSelectorSwitchInit(a1);
}

HOOK(bool, __fastcall, PVSelectorSwitchCtrl, 0x1406EDC40, uint64_t a1)
{
	int32_t sel_state  = *reinterpret_cast<int32_t*>(a1 + 108);
	int32_t pv_id      = *reinterpret_cast<int32_t*>(a1 + 0x262A4);
	int32_t difficulty = *reinterpret_cast<int32_t*>(a1 + 0x262C8);
	int32_t edition    = *reinterpret_cast<int32_t*>(a1 + 0x262CC);

	if (sel_state == 0)
	{
		if (!pvsel::CheckAssetsLoaded())
			return false;
	}

	bool ret = originalPVSelectorSwitchCtrl(a1);

	if (sel_state == 6 || sel_state == 8)
	{
		if (pv_id == -2)
		{
			int32_t** random_pv_data = *reinterpret_cast<int32_t***>(a1 + 0x261F8);
			if (random_pv_data != nullptr && *random_pv_data != nullptr)
				pv_id = **random_pv_data;
		}

		pvsel::UpdateCtrl(pv_id, difficulty, edition);
	}

	return ret;
}

HOOK(bool, __fastcall, PVSelectorSwitchDest, 0x1406EE720, uint64_t a1)
{
	pvsel::ResetSelectedSong();
	pvsel::UnloadAet();
	return originalPVSelectorSwitchDest(a1);
}

HOOK(void, __fastcall, PVSelectorSwitchDisp, 0x1406EE7B0, uint64_t a1)
{
	originalPVSelectorSwitchDisp(a1);
	pvsel::DrawKeyIcon();
}

// NOTE: FT UI hooks
HOOK(bool, __fastcall, PVselPS4Init, 0x140202D50, uint64_t a1)
{
	pvsel::RequestAetLoad();
	return originalPVselPS4Init(a1);
}

/*
bool dialog_added = false;

struct DialogInfo
{
	int32_t str_array_id;
	uint8_t gap4[4];
	prj::string std__string8;
	uint8_t gap28[56];
	void* ptr60;
	bool bool68;
};

static FUNCTION_PTR(void, __fastcall, PutDialog, 0x1406083A0, DialogInfo* info);
*/

HOOK(bool, __fastcall, PVselPS4Ctrl, 0x1402033C0, uint64_t a1)
{
	int32_t sel_state = *reinterpret_cast<int32_t*>(a1 + 108);
	int32_t pv_id = *reinterpret_cast<int32_t*>(a1 + 226268);
	int32_t difficulty = *reinterpret_cast<int32_t*>(a1 + 226296);
	int32_t edition = *reinterpret_cast<int32_t*>(a1 + 226300);

	if (sel_state == 0)
	{
		if (!pvsel::CheckAssetsLoaded())
			return false;
	}

	bool ret = originalPVselPS4Ctrl(a1);

	if (sel_state == 6 || sel_state == 8)
	{
		if (pv_id == -2)
		{
			int32_t** random_pv_data = *reinterpret_cast<int32_t***>(a1 + 0x37328);
			if (random_pv_data != nullptr && *random_pv_data != nullptr)
				pv_id = **random_pv_data;
		}

		pvsel::UpdateCtrl(pv_id, difficulty, edition);
	}

	/*
	if (!dialog_added)
	{
		dialog_added = true;
		DialogInfo dlg = { };
		dlg.str_array_id = 53580;
		dlg.bool68 = true;
		PutDialog(&dlg);
	}
	*/

	return ret;
}

HOOK(bool, __fastcall, PVselPS4Dest, 0x140204DA0, uint64_t a1)
{
	pvsel::ResetSelectedSong();
	pvsel::UnloadAet();
	return originalPVselPS4Dest(a1);
}

HOOK(void, __fastcall, PVselPS4Disp, 0x140204EB0, uint64_t a1)
{
	originalPVselPS4Disp(a1);
	pvsel::DrawKeyIcon();
}

void InstallPvSelHooks()
{
	INSTALL_HOOK(PVSelectorSwitchInit);
	INSTALL_HOOK(PVSelectorSwitchCtrl);
	INSTALL_HOOK(PVSelectorSwitchDest);
	INSTALL_HOOK(PVSelectorSwitchDisp);
	INSTALL_HOOK(PVselPS4Init);
	INSTALL_HOOK(PVselPS4Ctrl);
	INSTALL_HOOK(PVselPS4Dest);
	INSTALL_HOOK(PVselPS4Disp);
}