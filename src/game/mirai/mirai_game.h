#pragma once

#include <diva.h>
#include <db.h>

namespace mirai_game
{
	void Init(PVGameData& pv_game, const db::SongEntry& song, const db::ChartEntry& diff);
	bool IsLoaded();
	void Update(PVGameData& pv_game, float dt);
	void Disp();
}