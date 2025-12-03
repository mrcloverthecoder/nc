#include <stdint.h>
#include <hooks.h>
#include <save_data.h>
#include "pv_sel.h"

struct PVSelectorSwitch
{
	INSERT_PADDING(0x6C);
	int32_t state;
	INSERT_PADDING(0x258BB);
	bool is_ranking_mode;
	int32_t dword2592C;
	pvsel::SelPvList sel_pv_list;
	INSERT_PADDING(0x498);
	prj::vector<pvsel::PvData> sorted_pv_lists[32];
	prj::vector<pvsel::PvData>* cur_pv_list;
	INSERT_PADDING(0x8);
	pvsel::PvData random_pv;
	int32_t* cur_sort_index;
	int32_t song_counts[21];
	int32_t all_sort_index;
	int32_t dword262A0;
	int32_t pv_id;
	int32_t dword262A8;
	int32_t sort_indices[4];
	int32_t sort_change_dir;
	int32_t sort_mode;
	uint8_t difficulty_max;
	uint8_t difficulty_min;
	INSERT_PADDING(0x2);
	int32_t difficulty;
	int32_t edition;
};

static bool style_dirty = false;

static void SetGlobalStateSelectedData(const PVSelectorSwitch* sel)
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
			if (auto* entry = db::FindSongEntry(cur_pv); entry != nullptr)
			{
				state.nc_song_entry = *entry;
				if (auto* chart = entry->FindChart(sel->difficulty, sel->edition, style); chart != nullptr)
					state.nc_chart_entry = *chart;
			}
		}

		pv_id = cur_pv;
		style_dirty = false;
	}
}

static FUNCTION_PTR(bool, __fastcall, IsPlaylistMode, 0x1406D8D50);
static FUNCTION_PTR(void, __fastcall, PVListSetSelectedIndex, 0x140217500, pvsel::SelPvList* a1, int32_t a2, int32_t a3);
static FUNCTION_PTR(void, __fastcall, PVSelectorSwitchChangeSortFilter, 0x1406F2F40, PVSelectorSwitch* a1, int32_t a2);
static FUNCTION_PTR(bool, __fastcall, CheckSongPertains, 0x1406F3D40, PVSelectorSwitch* sel, const pvsel::PvData* pv, int32_t, int32_t);
static FUNCTION_PTR(void, __fastcall, ImpUpdateDifficultyBeltAndSabi, 0x1406E0D90, void* a1, int32_t difficulty, int32_t edition, pvsel::SelPvList* pv_list, void* sabi, bool a6);

static int32_t GetSelectedIndex(PVSelectorSwitch* sel)
{
	if (sel->pv_id == -2 && !sel->is_ranking_mode)
		return sel->sel_pv_list.GetSongCount() - 1;

	return pvsel::GetSelectedPVIndex(sel);
}

static void UpdateDifficultyBeltAndSabi(PVSelectorSwitch* sel)
{
	void* a1 = reinterpret_cast<char*>(sel) + 0x1A50 + 0x163E0;
	void* sabi = reinterpret_cast<char*>(sel) + 0x258D8;
	ImpUpdateDifficultyBeltAndSabi(a1, sel->difficulty, sel->edition, &sel->sel_pv_list, sabi, false);
}

HOOK(bool, __fastcall, PVSelectorSwitchCreateSortedPVList, 0x1406F3100, PVSelectorSwitch* sel)
{
	if (bool ret = originalPVSelectorSwitchCreateSortedPVList(sel); !ret || IsPlaylistMode())
		return ret;

	auto song_counts = pvsel::GetSongCountPerStyle(sel);
	if (pvsel::gs_win && pvsel::gs_win->SetAvailableOptions(song_counts))
		style_dirty = true;

	int32_t count = 0;
	for (int32_t style = pvsel::GetSelectedStyleOrDefault(); count < 3; style++)
	{
		if (song_counts[style % 3] > 0)
		{
			auto songs = pvsel::SortWithStyle(sel->sel_pv_list, sel->difficulty, sel->edition, style % 3);
			*sel->sel_pv_list.pv_data = songs;
			PVListSetSelectedIndex(&sel->sel_pv_list, GetSelectedIndex(sel), 0);
			pvsel::CalculateAllSongCount(sel, CheckSongPertains);
			return true;
		}

		count++;
	}

	return false;
}

HOOK(bool, __fastcall, PVSelectorSwitchInit, 0x1406ED9D0, uint64_t a1)
{
	pvsel::RequestAssetsLoad();
	state.nc_song_entry.reset();
	state.nc_chart_entry.reset();

	if (!pvsel::gs_win && !IsPlaylistMode())
	{
		pvsel::gs_win = std::make_unique<pvsel::GSWindow>();
		pvsel::gs_win->SetPreferredStyle(nc::GetSharedData().pv_sel_selected_style);
	}

	return originalPVSelectorSwitchInit(a1);
}

HOOK(bool, __fastcall, PVSelectorSwitchCtrl, 0x1406EDC40, PVSelectorSwitch* sel)
{
	if (sel->state == 0)
	{
		if (!pvsel::CheckAssetsLoaded())
			return false;
	}

	bool ret = originalPVSelectorSwitchCtrl(sel);

	if (sel->state == 6 && pvsel::gs_win)
	{
		if (pvsel::gs_win->Ctrl())
		{
			PVSelectorSwitchChangeSortFilter(sel, 0);
			style_dirty = true;
			SetGlobalStateSelectedData(sel);
			UpdateDifficultyBeltAndSabi(sel);
		}

		pvsel::SetSongToggleable(sel);
	}
	
	SetGlobalStateSelectedData(sel);
	nc::GetSharedData().pv_sel_selected_style = pvsel::GetPreferredStyleOrDefault();
	return ret;
}

HOOK(bool, __fastcall, PVSelectorSwitchDest, 0x1406EE720, uint64_t a1)
{
	pvsel::gs_win.reset();
	pvsel::UnloadAssets();
	return originalPVSelectorSwitchDest(a1);
}

HOOK(void, __fastcall, PVSelectorSwitchDisp, 0x1406EE7B0, uint64_t a1)
{
	originalPVSelectorSwitchDisp(a1);
	if (pvsel::gs_win)
		pvsel::gs_win->Disp();
}

void InstallPvSelSwitchHooks()
{
	INSTALL_HOOK(PVSelectorSwitchCreateSortedPVList);
	INSTALL_HOOK(PVSelectorSwitchInit);
	INSTALL_HOOK(PVSelectorSwitchCtrl);
	INSTALL_HOOK(PVSelectorSwitchDest);
	INSTALL_HOOK(PVSelectorSwitchDisp);
}