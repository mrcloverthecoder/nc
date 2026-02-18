#pragma once

#include <string>
#include <diva.h>
#include <nc_time.h>

class SoundEffectManager
{
public:
	static constexpr int32_t QueueIndex = 3;

	void Init();

	void PlayButtonSE();
	void PlayDoubleSE();
	void PlayStarSE();
	void PlayCymbalSE();
	void PlayStarDoubleSE();
	void StartLongSE();
	void EndLongSE(bool fail);
	void StartRushBackSE();
	void EndRushBackSE(bool popped);
	void StartLinkSE();
	void EndLinkSE();
private:
	std::string button;
	std::string w_button;
	std::string l_button_on;
	std::string l_button_off;
	std::string star;
	std::string w_star;
	std::string link;
	std::string rush_on;
	std::string rush_off;
};

inline SoundEffectManager se_mgr = { };

namespace sound_effects
{
	std::string GetGameSoundEffect(int32_t kind);
}