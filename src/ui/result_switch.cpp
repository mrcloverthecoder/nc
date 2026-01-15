#include <diva.h>
#include <hooks.h>
#include <nc_log.h>
#include <nc_state.h>
#include <util.h>
#include "common.h"
#include "result.h"

// GETSCENEID  14064BC50 (VTABLE 140CC1A00)
// DETAIL_DISP 140647840 

struct CAetController
{
	void** vftable;
	AetArgs args;
};

struct CStageResultAetController_InLoopOut
{
	void** vftable;
	uint8_t gap0[0x1F8];
	prj::string in_name;
	prj::string lp_name;
	prj::string out_name;
};

static prj::string GetModeLayerName(int32_t kind, bool no_fail)
{
	static const char* styles[4] = { "", "console", "mixed", "max" };
	static const char* kinds[3] = { "in", "loop", "out" };

	return prj::string("nsw_mode_tit_")
		+ styles[state.GetGameStyle()]
		+ "_"
		+ (no_fail ? "nofail_" : "")
		+ kinds[kind]
		+ GetLanguageSuffix().c_str();
}

static prj::string GetWindowLayerName(int32_t kind)
{
	static const char* kinds[4] = { "slide in", "slide loop", "slide_out", "count" };

	return prj::string("nsw_win_arcard_")
		+ kinds[kind]
		+ (GetGameLocale() == GameLocale_JP ? "_jp" : "_en");
}

static bool CheckLayerNameMatchesNC(std::string_view name)
{
	static const char* checks[2] = { "nsw_win_arcard_slide", "nsw_mode_tit" };
	for (int32_t i = 0; i < 2; i++)
	{
		if (util::Contains(name, checks[i]))
			return true;
	}

	return false;
}

static bool patch_scene = false;
HOOK(uint32_t, __fastcall, CStageResultAetControllerInLoopOutGetSceneID, 0x14064BC50, void* a1)
{
	uint32_t id = originalCStageResultAetControllerInLoopOutGetSceneID(a1);
	return patch_scene ? results::AetSceneID : id;
}

HOOK(void, __fastcall, CAetControllerInLoopOutSetLayer, 0x14065E7D0, void* a1, const prj::string* in, const prj::string* loop, const prj::string* out, int32_t prio)
{
	// NOTE: This is sort of hacky, I guess, but it works™
	prj::string patched_in = *in;
	prj::string patched_lp = *loop;
	prj::string patched_out = *out;

	if (util::StartsWith(*in, "mode_tit_arcade") && util::StartsWith(*loop, "mode_tit_arcade"))
	{
		if (state.GetGameStyle() != GameStyle_Arcade)
		{
			bool no_fail = util::Contains(*in, "comp") || util::Contains(*loop, "comp");
			patched_in = GetModeLayerName(0, no_fail);
			patched_lp = GetModeLayerName(1, no_fail);
		}
	}
	else if (util::StartsWith(*in, "win_arcard") && util::StartsWith(*loop, "win_arcard"))
	{
		if (nc::ShouldUseConsoleStyleWin())
		{
			patched_in = GetWindowLayerName(0);
			patched_lp = GetWindowLayerName(1);
			patched_out = GetWindowLayerName(2);
		}
	}
	
	originalCAetControllerInLoopOutSetLayer(a1, patched_in, patched_lp, patched_out, prio);
}

HOOK(void, __fastcall, CStageResultAetControllerSetLayer, 0x14065DBA0, void* a1, prj::string* name, int32_t prio, int32_t action)
{
	patch_scene = CheckLayerNameMatchesNC(*name);
	originalCStageResultAetControllerSetLayer(a1, name, prio, action);
	patch_scene = false;
}

HOOK(void, __fastcall, CAetControllerGetLayout, 0x14065E200, CAetController* a1, void* a2, void* a3)
{
	patch_scene = CheckLayerNameMatchesNC(a1->args.layer_name);
	originalCAetControllerGetLayout(a1, a2, a3);
	patch_scene = false;
}

HOOK(bool, __fastcall, StageResultSwitchInit, 0x14064C0E0, void* a1)
{
	prj::string out;
	prj::string_view strv;
	aet::LoadAetSet(results::AetSetID, &out);
	spr::LoadSprSet(results::SprSetID, &strv);
	return originalStageResultSwitchInit(a1);
}

HOOK(void, __fastcall, StageResultSwitchWaitLoad, 0x14064C4B0, void* a1)
{
	if (aet::CheckAetSetLoading(results::AetSetID) || spr::CheckSprSetLoading(results::SprSetID))
		return;

	return originalStageResultSwitchWaitLoad(a1);
}

HOOK(bool, __fastcall, StageResultSwitchDest, 0x14064C300, void* a1)
{
	aet::UnloadAetSet(results::AetSetID);
	spr::UnloadSprSet(results::SprSetID);
	return originalStageResultSwitchDest(a1);
}

HOOK(void, __fastcall, StageResultSwitchDetailInit, 0x1406463B0, char* a1)
{
	originalStageResultSwitchDetailInit(a1);
	nc::InitResultsData(*reinterpret_cast<ScoreDetail**>(a1 + 0x18));
}

HOOK(void, __fastcall, StageResultSwitchDetailDisp, 0x140647840, char* a1)
{
	nc::DrawResultsWindowText(
		*reinterpret_cast<const int32_t*>(a1 + 0x28 + 0x8 + 0x15C),
		*reinterpret_cast<const int32_t*>(a1 + 0xC)
	);
	originalStageResultSwitchDetailDisp(a1);
}

void InstallResultSwitchHooks()
{
	INSTALL_HOOK(CStageResultAetControllerInLoopOutGetSceneID);
	INSTALL_HOOK(CAetControllerInLoopOutSetLayer);
	INSTALL_HOOK(CStageResultAetControllerSetLayer);
	INSTALL_HOOK(CAetControllerGetLayout);
	INSTALL_HOOK(StageResultSwitchInit);
	INSTALL_HOOK(StageResultSwitchWaitLoad);
	INSTALL_HOOK(StageResultSwitchDest);
	INSTALL_HOOK(StageResultSwitchDetailInit);
	INSTALL_HOOK(StageResultSwitchDetailDisp);
}