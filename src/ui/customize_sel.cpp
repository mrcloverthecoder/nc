#include <stdint.h>
#include <vector>
#include <diva.h>
#include <hooks.h>
#include <nc_log.h>
#include <sound_db.h>
#include <save_data.h>
#include <input.h>
#include <util.h>
#include <game/sound_effects.h>
#include <game/tech_zone.h>
#include "common.h"
#include "customize_sel.h"

constexpr int32_t PreviewQueueIndex = 3;
constexpr uint32_t SceneID = 14010150;
constexpr int32_t WindowPrio = 20;

int32_t NCKeyInputFooter = 0;
static InputType previousInputType = InputType::UNKNOWN;

struct CSStateConfigNC
{
	bool window_open = false;
	bool assets_loaded = false;
} static cs_state;

struct SoundOptionInfo
{
	int32_t id = 0;
	std::string preview_name;
};

struct SelectorExtraData
{
	int32_t same_index = -1;
	int32_t song_default_index = -1;
	int32_t base_index = 0;
	int32_t same_id = 0;
	std::vector<SoundOptionInfo> sounds;
};

struct PlayCustomizeSelFooterArgs {
	std::string footerName;
	int32_t screen;
};

static void PlayPreviewSoundEffect(HorizontalSelector* sel_base, const void* extra)
{
	HorizontalSelectorMulti* sel = dynamic_cast<HorizontalSelectorMulti*>(sel_base);
	const auto* ex_data = reinterpret_cast<const SelectorExtraData*>(extra);

	std::string se_name;

	if (sel->selected_index == ex_data->same_index)
	{
		if (ex_data->same_id == 1)
		{
			se_name = sound_effects::GetGameSoundEffect(0);
		}
		else if (ex_data->same_id == 2)
		{
			const auto* snd = util::FindWithID(*sound_db::GetStarSoundDB(), nc::GetConfigSet()->star_se_id);
			if (snd != nullptr)
				se_name = snd->se_name;
		}
	}
	else
		se_name = ex_data->sounds[sel->selected_index].preview_name;
	
	if (!se_name.empty())
	{
		sound::ReleaseAllCues(PreviewQueueIndex);
		sound::PlaySoundEffect(PreviewQueueIndex, se_name.c_str(), 1.0f);
	}
}

static void PlayControlSEPreview(HorizontalSelector* sel_base, const void*)
{
	HorizontalSelectorMulti* sel = dynamic_cast<HorizontalSelectorMulti*>(sel_base);
	std::string se_name;

	switch (sel->selected_index)
	{
	case 0:
		se_name = sound_effects::GetGameSoundEffect(3);
		break;
	case 1:
		if (const auto* snd = util::FindWithID(*sound_db::GetStarSoundDB(), nc::GetConfigSet()->star_se_id); snd != nullptr)
			se_name = snd->se_name;
		break;
	}

	if (!se_name.empty())
	{
		sound::ReleaseAllCues(PreviewQueueIndex);
		sound::PlaySoundEffect(PreviewQueueIndex, se_name.c_str(), 1.0f);
	}
}

static void StoreSoundEffectConfig(int32_t index, const SelectorExtraData& ex_data, int8_t* output)
{
	int32_t id = ex_data.sounds[index].id;
	if (index == ex_data.song_default_index)
		id = -2;
	else if (index == ex_data.same_index)
		id = -1;

	*output = id;
}

class NCConfigWindow : public AetControl
{
protected:
	bool finishing = false;
	bool exit = false;
	int32_t prev_selected_tab = 0;
	int32_t selected_tab = 0;
	int32_t selected_option = 0;
	AetElement fade_base;
	AetElement sub_menu_base;
	AetElement help_loc;
	AetElement subhelp_loc;
	std::vector<std::unique_ptr<HorizontalSelector>> selectors;
	std::vector<SelectorExtraData> user_data;
	float win_opacity = 1.0f;
	ConfigSet* config_set;

	static constexpr int32_t MaxTabCount = 2;
	static constexpr uint32_t NumberSprites[2][3] = {
		{ 3084111403, 965335902,  268427239 }, // FT UI
		{ 880817216,  1732835926, 3315147794 } // MM+ UI
	};
	
	uint32_t TabInfoSpritesFT[2];
	uint32_t TabInfoSpritesMM[2];
	
	uint32_t OptionInfoSpritesMM[MaxTabCount][5];
	uint32_t OptionInfoSpritesPS4[MaxTabCount][5];
						  
	uint32_t SoundPrioSubhelpMM[3];
	uint32_t SoundPrioSubhelpPS4[3];

	uint32_t PS4WinTitleSpriteID;


	void LoadAllSpriteSet()
	{
		std::string suffix = GetLanguageSuffix();
		for (auto& c : suffix)
			c = std::toupper(c);

		const int OptionInfoIndices[] = { 6, 7, 8, 9 };

		if (game::IsFutureToneMode())
		{

			LoadSpriteSetArray(
				"SPR_NSWGAM_NCWIN_NC_CUSTOM_FT_OPTIONS%02d%s",
				nullptr,
				2,
				TabInfoSpritesFT,
				suffix.c_str()
			);

			LoadSpriteSetArray(
				"SPR_NSWGAM_NCWIN_HELP_SUBTXT_NC_FT_%02d%s",
				nullptr,
				3,
				SoundPrioSubhelpPS4,
				suffix.c_str()
			);

			LoadSpriteSetArray(
				"SPR_NSWGAM_NCWIN_HELP_TXT_NC_FT_%02d%s",
				OptionInfoIndices,
				4,
				OptionInfoSpritesPS4[1],
				suffix.c_str()
			);

			LoadSpriteSet(
				"SPR_NSWGAM_NCWIN_NC_CUSTOM_FT_TITLE%s",
				suffix.c_str(),
				&PS4WinTitleSpriteID
			);
		}
		else
		{
			LoadSpriteSetArray(
				"SPR_NSWGAM_NCWIN_NC_CUSTOM_OPTIONS%02d%s",
				nullptr,
				2,
				TabInfoSpritesMM,
				suffix.c_str()
			);

			LoadSpriteSetArray(
				"SPR_NSWGAM_NCWIN_HELP_SUBTXT_NC_%02d%s",
				nullptr,
				3,
				SoundPrioSubhelpMM,
				suffix.c_str()
			);

			LoadSpriteSetArray(
				"SPR_NSWGAM_NCWIN_HELP_TXT_NC_%02d%s",
				OptionInfoIndices,
				4,
				OptionInfoSpritesMM[1],
				suffix.c_str()
			);
		}
	}

	void CreateWindowBase()
	{
		if (game::IsFutureToneMode())
		{
			fade_base.SetScene(SceneID);
			fade_base.SetLayer("ps4_help_win_bg", WindowPrio - 1, 14, AetAction_InLoop);
			help_loc.SetScene(SceneID);
			help_loc.SetLayer("ps4_nc_help_loc", WindowPrio + 1, 14, AetAction_InLoop);
			subhelp_loc.SetScene(SceneID);
			subhelp_loc.SetLayer("ps4_nc_subhelp_loc", WindowPrio + 1, 14, AetAction_InLoop);
			SetLayer("ps4_help_win_l_back_t", WindowPrio, 14, AetAction_InLoop);
		}
		else
		{
			help_loc.SetScene(SceneID);
			help_loc.SetLayer("nsw_nc_help_loc", WindowPrio + 1, 14, AetAction_InLoop);
			subhelp_loc.SetScene(SceneID);
			subhelp_loc.SetLayer("nsw_nc_subhelp_loc", WindowPrio + 1, 14, AetAction_InLoop);
			SetLayer("nsw_cmn_win_nc_options_g_inout", WindowPrio, 14, AetAction_InLoop);
		}
	}

	void CreateSubmenuBase(int32_t page_num)
	{
		std::string layer_name;
		int32_t action;

		if (game::IsFutureToneMode())
		{
			layer_name = util::Format("ps4_base_nc_anm_%02d", page_num);
			action = AetAction_InLoop;
		}
		else
		{
			layer_name = util::Format("nsw_submenu_nc_anm_%02d", page_num);
			action = AetAction_None;
		}

		if (!layer_name.empty())
		{
			sub_menu_base.SetScene(SceneID);
			sub_menu_base.SetLayer(layer_name, WindowPrio, 14, action);
		}
	}

public:
	NCConfigWindow()
	{
		AllowInputsWhenBlocked(true);
		config_set = nc::GetConfigSet();

		SetScene(SceneID);
		CreateWindowBase();
		ChangeTab(0);
		LoadAllSpriteSet();
	}

	bool ShouldExit() const { return exit; }

	void Ctrl() override
	{
		AetControl::Ctrl();

		if (finishing && Ended())
		{
			exit = true;
			return;
		}

		if (game::IsFutureToneMode())
		{
			if (auto layout = GetLayout("ps4_cmn_t_win_l_side.pic"); layout.has_value())
				win_opacity = layout.value().opacity;
		}
		else
		{
			if (auto layout = GetLayout("nswgam_cmn_win_base.pic"); layout.has_value())
				win_opacity = layout.value().opacity;
		}

		help_loc.SetOpacity(win_opacity);
		subhelp_loc.SetOpacity(win_opacity);

		for (auto& selector : selectors)
		{
			selector->SetOpacity(win_opacity);
			selector->text_opacity = win_opacity;
			selector->Ctrl();
		}
	}

	void Disp() override
	{
		for (auto& selector : selectors)
			selector->Disp();

		if (game::IsFutureToneMode())
		{
			DrawSpriteAt("p_num_n_c", NumberSprites[0][selected_tab + 1]);
			DrawSpriteAt("p_num_d_c", NumberSprites[0][MaxTabCount]);
			DrawSpriteAt("p_win_img_c", TabInfoSpritesFT[selected_tab]);
			DrawSpriteAt("p_win_tit_lt", PS4WinTitleSpriteID);
			help_loc.DrawSpriteAt("p_help_loc_c", OptionInfoSpritesPS4[selected_tab][selected_option]);

			if (selected_tab == 1 && selected_option == 3)
				subhelp_loc.DrawSpriteAt("p_subhelp_loc_c", SoundPrioSubhelpPS4[nc::GetSharedData().sound_prio]);
		}
		else
		{
			DrawSpriteAt("p_nc_page_num_10_c", NumberSprites[1][selected_tab + 1]);
			DrawSpriteAt("p_nc_page_num_01_c", NumberSprites[1][MaxTabCount]);
			DrawSpriteAt("p_nc_img_02_c", TabInfoSpritesMM[prev_selected_tab]);
			DrawSpriteAt("p_nc_img_01_c", TabInfoSpritesMM[selected_tab]);
			help_loc.DrawSpriteAt("p_help_loc_c", OptionInfoSpritesMM[selected_tab][selected_option]);

			if (selected_tab == 1 && selected_option == 3)
				subhelp_loc.DrawSpriteAt("p_subhelp_loc_c", SoundPrioSubhelpMM[nc::GetSharedData().sound_prio]);
		}
	}

	void OnActionPressed(int32_t action) override
	{
		if (finishing)
			return;

		switch (action)
		{
		case KeyAction_SwapLeft:
			ChangeTab(-1);
			break;
		case KeyAction_SwapRight:
			ChangeTab(1);
			break;
		case KeyAction_Cancel:
			SetMarkers("st_out", "ed_out", false);
			fade_base.SetMarkers("st_out", "ed_out", false);
			sound::ReleaseAllCues(PreviewQueueIndex);
			sound::PlaySoundEffect(1, "se_ft_sys_dialog_close", 1.0f);
			finishing = true;
			break;
		}
	}

	void OnActionPressedOrRepeat(int32_t action) override
	{
		if (finishing)
			return;

		switch (action)
		{
		case KeyAction_MoveUp:
			if (SetSelectorIndex(-1, true))
				sound::PlaySelectSE();
			break;
		case KeyAction_MoveDown:
			if (SetSelectorIndex(1, true))
				sound::PlaySelectSE();
			break;
		}
	}

	template <typename T, typename F>
	T* CreateOptionElement(int32_t id, int32_t loc_id, F func = nullptr)
	{
		diva::vec3 pos = { 0.0f, 0.0f, 0.0f };

		if (auto layout = sub_menu_base.GetLayout(util::Format("p_nc_submenu_%02d_c", loc_id)); layout.has_value())
			pos = layout.value().position;

		std::unique_ptr<T> opt;
		if (game::IsFutureToneMode())
		{
			opt = std::make_unique<T>(
				SceneID,
				util::Format("ps4_options_base_nc_%02d_ft%s", id, GetLanguageSuffix().c_str()),
				WindowPrio,
				14
			);

			opt->SetArrows("ps4_sel_arrow_l", "ps4_sel_arrow_r");
		}
		else
		{
			opt = std::make_unique<T>(
				SceneID,
				util::Format("nsw_option_submenu_nc_%02d__f%s", id, GetLanguageSuffix().c_str()),
				WindowPrio,
				14
			);
		}

		opt->AllowInputsWhenBlocked(true);
		opt->SetPosition(pos);

		if (func)
			opt->SetOnChangeNotifier(func);

		selectors.push_back(std::move(opt));
		return dynamic_cast<T*>(selectors.back().get());
	}

	void ChangeTab(int32_t dir)
	{
		prev_selected_tab = selected_tab;
		selected_tab = util::Wrap(selected_tab + dir, 0, MaxTabCount - 1);
		CreateSubmenuBase(selected_tab + 1);

		auto putSoundEffectList = [&](int32_t id, int32_t loc_id, const std::vector<SoundInfo>& sounds, int32_t same_id, int8_t* selected_id)
		{
			size_t index = selectors.size();

			auto& ex_data = user_data.emplace_back();
			ex_data.same_id = same_id;

			auto* opt = CreateOptionElement<HorizontalSelectorMulti, HorizontalSelectorMulti::Notifier>(
				id,
				loc_id,
				[this, index, selected_id](int32_t i) { StoreSoundEffectConfig(i, user_data[index], selected_id); }
			);

			if (same_id > 0)
			{
				if (*selected_id == -1)
					opt->selected_index = static_cast<int32_t>(opt->values.size());

				ex_data.same_index = static_cast<int32_t>(opt->values.size());
				ex_data.sounds.emplace_back().id = -1;
				ex_data.base_index++;

				opt->values.emplace_back(loc::GetString(6250 + same_id));
			}

			for (const auto& snd : sounds)
			{
				if (snd.id == *selected_id)
					opt->selected_index = static_cast<int32_t>(opt->values.size());

				auto& info = ex_data.sounds.emplace_back();
				info.id = snd.id;
				info.preview_name = !snd.se_preview_name.empty() ? snd.se_preview_name : snd.se_name;
				
				opt->values.push_back(snd.name);
			}

			opt->SetExtraData(&user_data[index]);
			opt->SetPreviewNotifier(PlayPreviewSoundEffect);
		};

		selectors.clear();
		user_data.clear();
		user_data.reserve(20);
		if (selected_tab == 0)
		{
			// TODO: Change the sound effect names (maybe?) and the "Same as" text to STR ARRAY
			//       string to allow localization.
			putSoundEffectList(1, 1, *sound_db::GetButtonLongOnSoundDB(), 1, &config_set->button_l_se_id);
			putSoundEffectList(2, 2, *sound_db::GetButtonWSoundDB(),      1, &config_set->button_w_se_id);
			putSoundEffectList(3, 3, *sound_db::GetStarSoundDB(),         0, &config_set->star_se_id);
			putSoundEffectList(4, 4, *sound_db::GetLinkSoundDB(),         2, &config_set->link_se_id); 
			putSoundEffectList(5, 5, *sound_db::GetStarWSoundDB(),        2, &config_set->star_w_se_id);
		}
		else if (selected_tab == 1)
		{
			auto* sens = CreateOptionElement<HorizontalSelectorNumber, HorizontalSelectorNumber::Notifier>(6, 1);
			sens->value_min = 20.0f;
			sens->value_max = 80.0f;
			sens->SetValue(nc::GetSharedData().stick_sensitivity);
			sens->format_string = "%.0f%%";
			sens->SetOnChangeNotifier([](float v) { nc::GetSharedData().stick_sensitivity = v; });

			auto* ctrl_se = CreateOptionElement<HorizontalSelectorMulti, HorizontalSelectorMulti::Notifier>(7, 2);
			ctrl_se->values.push_back("Slide");
			ctrl_se->values.push_back("Star");
			ctrl_se->selected_index = nc::GetSharedData().stick_control_se;
			ctrl_se->SetOnChangeNotifier([](int32_t index) { nc::GetSharedData().stick_control_se = index; });
			ctrl_se->SetPreviewNotifier(PlayControlSEPreview);

			auto* tz = CreateOptionElement<HorizontalSelectorMulti, HorizontalSelectorMulti::Notifier>(8, 3);
			tz->values.push_back("F");
			tz->values.push_back("F 2nd");
			tz->values.push_back("X");
			tz->values.push_back("Future Tone");
			tz->values.push_back("Mega Mix+");
			tz->values.push_back("Match UI");

			int32_t tz_style = nc::GetSharedData().tech_zone_style;
			switch (tz_style)
			{
			case TechZoneStyle_F:
			case TechZoneStyle_F2nd:
			case TechZoneStyle_X:
				tz->selected_index = tz_style;
				break;
			case TechZoneStyle_FT:
				tz->selected_index = 3;
				break;
			case TechZoneStyle_M39:
				tz->selected_index = 4;
				break;
			case TechZoneStyle_Match:
				tz->selected_index = 5;
				break;
			}

			tz->SetOnChangeNotifier([this](int32_t index)
			{
				int32_t& tz_style = nc::GetSharedData().tech_zone_style;
				switch (index)
				{
				case 0:
				case 1:
				case 2:
					tz_style = index;
					break;
				case 3:
					tz_style = TechZoneStyle_FT;
					break;
				case 4:
					tz_style = TechZoneStyle_M39;
					break;
				case 5:
					tz_style = TechZoneStyle_Match;
					break;
				}
			});

			auto* snd = CreateOptionElement<HorizontalSelectorMulti, HorizontalSelectorMulti::Notifier>(9, 4);
			snd->values.push_back("Disabled");
			snd->values.push_back("Mute");
			snd->selected_index = nc::GetSharedData().sound_prio > 0 ? 1 : 0;
			snd->SetOnChangeNotifier([](int32_t index) { nc::GetSharedData().sound_prio = (index == 1 ? 2 : 0); });
		}

		SetSelectorIndex(0);
	}

	bool SetSelectorIndex(int32_t index, bool relative = false)
	{
		int32_t prev_index = selected_option;
		selected_option = util::Wrap<int32_t>(relative ? selected_option + index : index, 0, selectors.size() - 1);

		for (size_t i = 0; i < selectors.size(); i++)
			selectors[i]->SetFocus(i == selected_option);

		return selected_option != prev_index;
	}
};

namespace customize_sel
{
	std::unique_ptr<NCConfigWindow> window;

	static bool CtrlWindow()
	{
		if (!cs_state.window_open)
		{
			diva::InputState* is = diva::GetInputState(0);
			if (is->IsButtonTapped(92) || is->IsButtonTapped(13))
			{
				window = std::make_unique<NCConfigWindow>();
				cs_state.window_open = true;
				nc::BlockInputs();
				sound::PlaySoundEffect(1, "se_ft_sys_dialog_open", 1.0f);
			}
		}

		if (cs_state.window_open)
		{
			window->Ctrl();
			if (window->ShouldExit())
			{
				window.reset();
				cs_state.window_open = false;
				nc::UnblockInputs();
			}
		}

		return cs_state.window_open;
	}
}

HOOK(bool, __fastcall, CustomizeSelTaskInit, 0x140687D10, uint64_t a1)
{
	sound::RequestFarcLoad("rom/sound/se_nc.farc");
	sound::RequestFarcLoad("rom/sound/se_nc_option.farc");

	prj::string out;
	prj::string_view out2;
	aet::LoadAetSet(14010060, &out);
	spr::LoadSprSet(14020060, &out2);

	cs_state.assets_loaded = false;
	return originalCustomizeSelTaskInit(a1);
}


void AddNCInputOptions()
{
	aet::Stop(&NCKeyInputFooter);

	std::string layerName =
		(game::IsFutureToneMode() ? "ps4_" : "nsw_")
		+ std::string("key_nc")
		+ GetPlatformSuffix(GetInputType())
		+ GetLanguageSuffix();

	AetArgs args;
	int prio = game::IsFutureToneMode() ? WindowPrio+2 : WindowPrio;
	aet::CreateAetArgs(&args, SceneID, layerName.c_str(), prio);
	args.res_mode = 14;
	args.flags = 0x20000;
	NCKeyInputFooter = aet::Play(&args, NCKeyInputFooter);
}

HOOK(bool, __fastcall, CustomizeSelTaskCtrl, 0x140687D70, uint64_t a1)
{
	if (!cs_state.assets_loaded)
	{
		cs_state.assets_loaded = !sound::IsFarcLoading("rom/sound/se_nc.farc")
			&& !sound::IsFarcLoading("rom/sound/se_nc_option.farc")
			&& !aet::CheckAetSetLoading(14010060)
			&& !spr::CheckSprSetLoading(14020060);
	}

	InputType current = GetInputType();
	if (current != previousInputType)
	{
		previousInputType = current;
		AddNCInputOptions();
	}
	return originalCustomizeSelTaskCtrl(a1);
}

HOOK(bool, __fastcall, CustomizeSelTaskDest, 0x140687D80, uint64_t a1)
{
	customize_sel::window.reset();
	aet::Stop(&NCKeyInputFooter);
	sound::UnloadFarc("rom/sound/se_nc.farc");
	sound::UnloadFarc("rom/sound/se_nc_option.farc");
	aet::UnloadAetSet(14010060);
	spr::UnloadSprSet(14020060);
	cs_state.assets_loaded = false;
	return originalCustomizeSelTaskDest(a1);
}

HOOK(void, __fastcall, CustomizeSelTaskDisp, 0x140687DE0, uint64_t a1)
{
	originalCustomizeSelTaskDisp(a1);
	if (customize_sel::window)
		customize_sel::window->Disp();
}

HOOK(void, __fastcall, CSTopMenuMainCtrl, 0x14069B610, uint64_t a1)
{
	customize_sel::CtrlWindow();
	originalCSTopMenuMainCtrl(a1);
}

HOOK(void, __fastcall, PlayCustomizeSelFooter, 0x15F9811D0, void* a1, PlayCustomizeSelFooterArgs* args)
{
	if (args->footerName == "fotter01" && args->screen == 5) AddNCInputOptions();
	else aet::Stop(&NCKeyInputFooter);
	originalPlayCustomizeSelFooter(a1, args);
}

HOOK(void, __fastcall, StopCustomizeSelFooter, 0x140684A00, void* a1) 
{
	aet::Stop(&NCKeyInputFooter);
	originalStopCustomizeSelFooter(a1);
}


void InstallCustomizeSelHooks()
{
	INSTALL_HOOK(CustomizeSelTaskInit);
	INSTALL_HOOK(CustomizeSelTaskCtrl);
	INSTALL_HOOK(CustomizeSelTaskDest);
	INSTALL_HOOK(CustomizeSelTaskDisp);
	INSTALL_HOOK(CSTopMenuMainCtrl);
	INSTALL_HOOK(PlayCustomizeSelFooter);
	INSTALL_HOOK(StopCustomizeSelFooter);				  
}