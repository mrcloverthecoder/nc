#include <stdint.h>
#include <array>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <detours.h>
#include <helpers.h>
#include <save_data.h>
#include <diva.h>
#include <nc_log.h>
#include <db.h>
#include "customize_sel.h"

// NOTE: Some acronyms used in this file:
//		   CS - Customize Selector
//         SS - Sound Select
//		   SM - Sound Main
//

// NOTE: Here we repurpose the ButtonSnd struct for use with other (non-button sound) option lists
//
/*
static std::vector<ButtonSnd> tech_zone_styles = {
	{ 0, "Option A", "" },
	{ 1, "Option B", "" }
};
*/

static std::array<const std::vector<ButtonSnd>*, 1> option_data = {
	db::GetButtonSoundDB(SoundType_Star)
};

static FUNCTION_PTR(int32_t*, __fastcall, GetGlobalPVInfo, 0x1401D6520);

namespace customize_sel
{
	bool is_drawing_main_state = false;
	bool is_selecting_custom_data = false;
	int32_t selected_category = 0;

	static int32_t GetConfigSetID()
	{
		int32_t set = *reinterpret_cast<int32_t*>(GetDivaSaveData() + 0x169410);
		return set < 3 ? -(set + 1) : GetGlobalPVInfo()[1];
	}
}

HOOK(bool, __fastcall, SwitchCustomizeSelCtrl, 0x140687D70, void* a1)
{
	diva::InputState* is = diva::GetInputState(0);
	if (is->IsButtonTapped(93))
	{
		nc::Print("CFG: %d\n", customize_sel::GetConfigSetID());
	}

	return originalSwitchCustomizeSelCtrl(a1);
}

static FUNCTION_PTR(bool, __fastcall, sub_140697EB0, 0x140697EB0, uint64_t a1);
static FUNCTION_PTR(void, __fastcall, CSSceneSoundEnterCategory, 0x140696FD0, uint64_t a1);

HOOK(int32_t, __fastcall, GetButtonSoundCount, 0x1406992E0, int32_t a1, int32_t a2)
{
	if (!customize_sel::is_selecting_custom_data)
		return originalGetButtonSoundCount(a1, a2);

	return static_cast<int32_t>(option_data[customize_sel::selected_category]->size());
}

HOOK(bool, __fastcall, GetButtonSoundName, 0x140699370, uint64_t a1, int32_t kind, int32_t id, prj::string* name)
{
	// NOTE: Check if this is being called from StateSoundMain and return the normal values
	//       to prevent the list values from showing instead of the button sound effect names
	if (customize_sel::is_drawing_main_state || !customize_sel::is_selecting_custom_data)
		return originalGetButtonSoundName(a1, kind, id, name);

	*name = option_data[customize_sel::selected_category]->at(id).name;
	return true;
}

HOOK(void, __fastcall, CSSSSelectSound, 0x140696FF0, uint64_t a1)
{
	if (customize_sel::is_selecting_custom_data)
	{
		int32_t* info = reinterpret_cast<int32_t*>(a1 + 0x88);
		ConfigSet* set = nc::FindConfigSet(customize_sel::GetConfigSetID(), true);
		if (set != nullptr)
		{
			switch (customize_sel::selected_category)
			{
			case 0:
				set->star_se_id = *info;
				break;
			default:
				break;
			}
		}
	}
	else
		originalCSSSSelectSound(a1);
}

HOOK(void, __fastcall, CSSMUpdate, 0x140699870, uint64_t a1)
{
	originalCSSMUpdate(a1);
	uint64_t scene = *reinterpret_cast<uint64_t*>(a1 + 8);
	int32_t* state = reinterpret_cast<int32_t*>(scene + 0x10);

	if (*state == 0 && sub_140697EB0(scene))
	{
		customize_sel::is_selecting_custom_data = false;
		diva::InputState* is = diva::GetInputState(0);

		if (is->IsButtonTapped(93))
		{
			customize_sel::selected_category++;
			if (customize_sel::selected_category >= option_data.size())
				customize_sel::selected_category = 0;
		}
		else if (is->IsButtonTapped(94))
		{
			customize_sel::is_selecting_custom_data = true;

			CSSceneSoundEnterCategory(scene);
			*state = 1;
		}
	}
}

HOOK(void, __fastcall, CSSceneSoundDraw, 0x140697F70, uint64_t a1)
{
	customize_sel::is_drawing_main_state = true;

	originalCSSceneSoundDraw(a1);
	int32_t state = *reinterpret_cast<int32_t*>(a1 + 0x10);

	if (state == 0)
	{
		// TODO: Draw NC option names text
	}

	customize_sel::is_drawing_main_state = false;
}

void InstallCustomizeSelHooks()
{
	INSTALL_HOOK(SwitchCustomizeSelCtrl);
	INSTALL_HOOK(CSSMUpdate);
	INSTALL_HOOK(CSSceneSoundDraw);
	INSTALL_HOOK(GetButtonSoundCount);
	INSTALL_HOOK(GetButtonSoundName);
	INSTALL_HOOK(CSSSSelectSound);
}