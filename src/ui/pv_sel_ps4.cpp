#include <stdint.h>
#include <hooks.h>
#include <save_data.h>
#include "pv_sel.h"

struct CommonMenu
{
	int32_t selected_index;
	uint8_t gap4[8];
	int32_t max_count;
};

struct PvDataPS4
{
	pvsel::PvData2* data2;
	uint8_t gap8[72];
};

struct PVselPS4
{
	INSERT_PADDING(0x6C);
	int32_t state;
	INSERT_PADDING(0x5578);
	CommonMenu cmn_menu;
	INSERT_PADDING(0x318F0);
	pvsel::SelPvList sel_pv_list;
	prj::vector<PvDataPS4> sorted_pv_lists[32];
	prj::vector<PvDataPS4>* cur_pv_list;
	uint8_t gap37310[24];
	PvDataPS4 random_pv;
	int32_t song_counts[21];
	int32_t all_sort_index;
	int32_t* cur_sort_index;
	int32_t dword373D8;
	int32_t pv_id;
	int32_t sort_mode;
	INSERT_PADDING(0x14);
	int32_t difficulty;
	int32_t edition;
};

static bool style_dirty = false;

static FUNCTION_PTR(bool, __fastcall, IsPlaylistMode, 0x14022A7F0);
static FUNCTION_PTR(bool, __fastcall, IsSurvivalMode, 0x14023B6A0);
static FUNCTION_PTR(void, __fastcall, PVListSetSelectedIndex, 0x140217500, pvsel::SelPvList* a1, int32_t a2, int32_t a3);
static FUNCTION_PTR(void, __fastcall, PVselPS4ChangeSortFilter, 0x1402078D0, PVselPS4* sel, int32_t dir);
static FUNCTION_PTR(void, __fastcall, InitCommonMenuPVList, 0x140211D00, CommonMenu* cmn, pvsel::SelPvList* pv_list);
static FUNCTION_PTR(bool, __fastcall, CheckSongPertains, 0x1402079E0, PVselPS4* sel, const PvDataPS4* pv, int32_t, int32_t);

static void SetGlobalStateSelectedData(const PVselPS4* sel)
{
	static int32_t pv_id = -1;

	int32_t cur_pv = sel->pv_id;
	if (cur_pv == -2 && sel->random_pv.data2 != nullptr)
		cur_pv = *sel->random_pv.data2->pv_db_entry;

	if (cur_pv != pv_id || style_dirty)
	{
		if (cur_pv != -2)
		{
			state.nc_song_entry.reset();
			state.nc_chart_entry.reset();
		}

		if (int32_t style = pvsel::GetSelectedStyleOrDefault(); style != GameStyle_Arcade)
		{
			if (const auto* entry = db::FindSongEntry(cur_pv); entry != nullptr)
			{
				state.nc_song_entry = *entry;
				if (const auto* chart = entry->FindChart(sel->difficulty, sel->edition, style); chart != nullptr)
					state.nc_chart_entry = *chart;
			}
		}

		pv_id = cur_pv;
		style_dirty = false;
	}
}

static int32_t GetSelectedIndex(PVselPS4* sel)
{
	if (sel->pv_id == -2)
		return sel->sel_pv_list.GetSongCount() - 1;
	return pvsel::GetSelectedPVIndex(sel);
}

HOOK(void, __fastcall, PVselPS4CreateSortedPVList, 0x140206C30, PVselPS4* sel)
{
	originalPVselPS4CreateSortedPVList(sel);
	if (IsSurvivalMode() || IsPlaylistMode())
		return;

	auto song_counts = pvsel::GetSongCountPerStyle(sel);

	if (pvsel::gs_win && pvsel::gs_win->SetAvailableOptions(song_counts))
		style_dirty = true;

	int32_t count = 0;
	for (int32_t style = pvsel::GetSelectedStyleOrDefault(); count < GameStyle_Max; style++)
	{
		if (song_counts[style % GameStyle_Max] > 0)
		{
			auto songs = pvsel::SortWithStyle(sel->sel_pv_list, sel->difficulty, sel->edition, style % GameStyle_Max);
			*sel->sel_pv_list.pv_data = songs;
			sel->song_counts[*sel->cur_sort_index] = song_counts[style % GameStyle_Max];
			pvsel::CalculateAllSongCount(sel, CheckSongPertains);
			PVListSetSelectedIndex(&sel->sel_pv_list, GetSelectedIndex(sel), 0);
			InitCommonMenuPVList(&sel->cmn_menu, &sel->sel_pv_list);
			return;
		}

		count++;
	}
}

HOOK(bool, __fastcall, PVselPS4Init, 0x140202D50, PVselPS4* sel)
{
	pvsel::RequestAssetsLoad();
	if (!pvsel::gs_win && !IsPlaylistMode())
	{
		pvsel::gs_win = std::make_unique<pvsel::GSWindow>();
		pvsel::gs_win->SetPreferredStyle(nc::GetSharedData().pv_sel_selected_style);
	}

	state.nc_song_entry.reset();
	state.nc_chart_entry.reset();

	return originalPVselPS4Init(sel);
}

HOOK(bool, __fastcall, PVselPS4Ctrl, 0x1402033C0, PVselPS4* sel)
{
	if (sel->state == 0)
	{
		if (!pvsel::CheckAssetsLoaded())
			return false;
	}

	bool ret = originalPVselPS4Ctrl(sel);

	if (!IsSurvivalMode() && !IsPlaylistMode())
	{
		if (sel->state == 6)
		{
			pvsel::gs_win->SetVisible(true);
			if (pvsel::gs_win->Ctrl())
			{
				PVselPS4ChangeSortFilter(sel, 0);
				style_dirty = true;
			}

			pvsel::SetSongToggleable(sel);
		}

		SetGlobalStateSelectedData(sel);
		nc::GetSharedData().pv_sel_selected_style = pvsel::GetSelectedStyleOrDefault();
	}
	else
	{
		if (pvsel::gs_win)
			pvsel::gs_win->SetVisible(false);
	}

	return ret;
}

HOOK(bool, __fastcall, PVselPS4Dest, 0x140204DA0, uint64_t a1)
{
	pvsel::gs_win.reset();
	pvsel::UnloadAssets();
	return originalPVselPS4Dest(a1);
}

HOOK(void, __fastcall, PVselPS4Disp, 0x140204EB0, uint64_t a1)
{
	originalPVselPS4Disp(a1);
	if (pvsel::gs_win)
		pvsel::gs_win->Disp();
}

void InstallPvSelPS4Hooks()
{
	INSTALL_HOOK(PVselPS4CreateSortedPVList);
	INSTALL_HOOK(PVselPS4Init);
	INSTALL_HOOK(PVselPS4Ctrl);
	INSTALL_HOOK(PVselPS4Dest);
	INSTALL_HOOK(PVselPS4Disp);
}