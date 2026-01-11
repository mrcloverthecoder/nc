#include <hooks.h>
#include <nc_state.h>
#include "chance_time.h"

static FUNCTION_PTR(void, __fastcall, SetNormalFrameAction, 0x1402756F0, PVGameUI* a1, bool in, float length_sec);
static FUNCTION_PTR(void, __fastcall, SetFrameAction, 0x140278AB0, PVGameUI* a1, bool visible, int32_t index, int32_t action, float length);

bool SetChanceTimeMode(PVGameUI* ui, int32_t mode)
{
	// NOTE: Chance Time Start
	if (mode == ModeSelect_ChanceStart)
	{
		SetNormalFrameAction(ui, false, 1.0f);
		SetFrameAction(ui, false, 1, 3, 60.0f);
		ui->draw_combo_counter = true;

		state.ui.SetLayer(LayerUI_ChanceFrameTop, true, "chance_frame_top", 12, 0x10000);
		state.ui.SetLayer(LayerUI_ChanceFrameBottom, true, "chance_frame_bottom", 12, 0x10000);
		state.ui.SetLayer(LayerUI_StarGauge, true, "gauge_ch00", 13, 0x10000);
		state.ui.SetLayer(LayerUI_ChanceTxt, true, "chance_start_txt", 12, 0x20000);

		state.chance_time.enabled = true;
	}
	// NOTE: Chance Time End
	else if (mode == ModeSelect_ChanceEnd)
	{
		SetNormalFrameAction(ui, true, 1.0f);
		SetFrameAction(ui, false, 1, 4, 60.0f);
		state.chance_time.enabled = false;
		state.ui.SetLayer(
			LayerUI_ChanceTxt,
			true,
			state.chance_time.successful ? "chance_result_success_txt" : "chance_end_txt",
			12,
			0x20000
		);
	}
	else
		return false;
	
	return true;
}

void SetChanceTimeStarFill(PVGameUI* ui, int32_t fill_rate)
{
	char name[32];
	sprintf_s(name, "gauge_ch%02d", state.chance_time.GetFillRate());
	state.ui.SetLayer(LayerUI_StarGauge, true, name, 13, 0x10000);
}

void SetChanceTimePosition(PVGameUI* ui)
{
	diva::vec3 top = { 0.0f, -ui->frame_top_offset[1], 0.0f };
	diva::vec3 bottom = { 0.0f, ui->frame_bottom_offset[1], 0.0f };

	aet::SetPosition(state.ui.aet_list[LayerUI_ChanceFrameTop], &top);
	aet::SetPosition(state.ui.aet_list[LayerUI_ChanceFrameBottom], &bottom);
	aet::SetPosition(state.ui.aet_list[LayerUI_StarGauge], &bottom);
}