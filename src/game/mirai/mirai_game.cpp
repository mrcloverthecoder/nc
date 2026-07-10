#include <util.h>
#include <nc_log.h>
#include <diva.h>
#include "mirai_game.h"
#include "mirai_utils.h"
#include <hooks.h>

constexpr const char* AET_SCENE_SUFFIXES[10] = {
	"_EASY", "_NORMAL", "_HARD", "_EX", "_ENC",
	"_EXEASY", "_EXNORMAL", "_EXHARD", "_EXEX", "_EXENC"
};

constexpr int32_t MaxLineShapes = 99;

struct LineSegment
{
	float start_time_sec = 0.0f;
	float duration_sec = 0.0f;
	float end_time_sec = 0.0f;
	diva::vec2 start_pos;
	diva::vec2 end_pos;
	diva::vec2 delta_pos;
	diva::vec2 normal;
	float distance = 0.0f;

	void Calculate(const diva::vec2& p0, const diva::vec2& p1)
	{
		start_pos = p0;
		end_pos = p1;
		delta_pos = p1 - p0;
		normal = (delta_pos / delta_pos.length()).rotated(math::ToRadians(90.0f));
		distance = delta_pos.length();
	}
};

struct LineShape
{
	float start_time_sec = 0.0f;
	float end_time_sec = 0.0f;
	float duration_sec = 0.0f;
	std::vector<LineSegment> segments;
};

struct AetData
{
	bool ready = false;
	uint32_t set_id = 0xFFFFFFFF;
	uint32_t scene_id = 0xFFFFFFFF;
	AetFScene* scene = nullptr;
	AetFComposition* comp = nullptr;
	int32_t handle = 0;

	bool RetrieveInfoAndCreateHandle()
	{
		if (set_id == 0xFFFFFFFF || scene_id == 0xFFFFFFFF)
			return false;

		if (!ready)
			return false;

		scene = aet::GetScene(scene_id);
		if (!scene)
			return false;

		comp = scene->GetMainComposition();
		if (!comp)
			return false;

		AetArgs args = {};
		args.scene_id = scene_id;
		args.flags = AetFlags_Hidden | AetFlags_Paused | AetFlags_Loop;
		args.layer_name = nullptr;
		handle = aet::Play(&args, 0);

		return handle != 0;
	}

	std::optional<AetLayout> GetLayout(std::string_view layer_name, float frame = -1.0f) const
	{
		if (frame >= 0.0f)
			aet::SetFrame(handle, frame);

		AetComposition fcomp;
		aet::GetComposition(&fcomp, handle);

		if (auto it = fcomp.find(prj::string(layer_name)); it != fcomp.end())
			return it->second;
		return std::nullopt;
	}
};

struct PVGameMirai
{
	static constexpr int32_t SegmentSubdivisions = 10;

	const db::SongEntry* song = nullptr;
	const db::ChartEntry* difficulty = nullptr;
	AetData aet_data = {};
	bool paused = false;
	float cur_time = 0.0f;
	float bpm = 130.0f;
	float beat_per_second = 130.0f / 60.0f;
	float dot_per_beat = 50.0f;
	float bar_length = 60.0f / 130.0f * 4.0f;
	std::vector<LineShape> shapes;
	std::array<SpriteVertex, 1000> vertices;
	int32_t vertex_draw_count = 0;
	diva::vec2 master_line_pos = {};
	diva::vec2 res_scale = {};

	bool ImportLinesFromAetComposition()
	{
		if (!aet_data.RetrieveInfoAndCreateHandle())
			return false;

		res_scale = utils::GetResolutionScaleWithSrcAspectRatio(diva::vec2(400.0f, 240.0f), diva::vec2(1920.0f, 1080.0f));

		const float max_segment_length = 3.0f * (aet_data.scene->fps / 30.0f);

		for (int32_t i = 0; i < MaxLineShapes; i++)
		{
			std::string layer_name = util::Format("LINE%02d", i + 1);
			AetFLayer* layer = std::find_if(
				aet_data.comp->layers,
				aet_data.comp->layers + aet_data.comp->layer_count,
				[&layer_name](const AetFLayer& layer) { return util::Compare(layer.name, layer_name); }
			);

			if (layer == aet_data.comp->layers + aet_data.comp->layer_count)
				break;

			if (!layer->video->trans_x.key_count && !layer->video->trans_y.key_count)
				continue;

			LineShape& shape = shapes.emplace_back();
			shape.start_time_sec = layer->start_time / aet_data.scene->fps;
			shape.segments.reserve(static_cast<size_t>((layer->end_time - layer->start_time) / max_segment_length) + 1);

			diva::vec2 prev_pos = { 0.0f, 0.0f };
			float frame = layer->start_time;
			int32_t index = 0;
			bool last = false;
			float start_time = layer->start_time / aet_data.scene->fps;
			float total_duration = 0.0f;
			while (true)
			{
				diva::vec2 pos = { 0.0f, 0.0f };
				if (auto layout = aet_data.GetLayout(layer_name, frame); layout.has_value())
					pos = layout->position.xy();

				if (index > 0)
				{
					LineSegment& seg = shape.segments.emplace_back();
					seg.Calculate(prev_pos, pos);
					seg.start_time_sec = start_time + total_duration;
					seg.duration_sec = seg.distance / dot_per_beat / beat_per_second;
					seg.end_time_sec = seg.start_time_sec + seg.duration_sec;
					total_duration += seg.duration_sec;
					printf("LINE%02d<%d>: %.3f (%.3f)  [%.2f, %.2f] -> [%.2f, %.2f] (%.3f)  (%.2f %.2f)\n", i + 1, index, seg.start_time_sec, seg.duration_sec, seg.start_pos.x, seg.start_pos.y, seg.end_pos.x, seg.end_pos.y, seg.distance, layer->start_time, layer->end_time);
				}

				index++;
				prev_pos = pos;
				frame += max_segment_length;

				if (last)
					break;

				if (frame >= layer->end_time)
				{
					frame = layer->end_time - 0.01f;
					last = true;
				}
			}

			shape.end_time_sec = start_time + total_duration;
			shape.duration_sec = total_duration;
		}

		return true;
	}

	void Update(float dt)
	{
		if (paused || !aet_data.ready)
			return;
		
		if (auto layout = aet_data.GetLayout("MASTER_LINE", cur_time * aet_data.scene->fps); layout.has_value())
			master_line_pos = layout->position.xy();
		
		for (LineShape& shape : shapes)
		{
			int32_t vertex_count = 0;

			if (cur_time + bar_length * 1.5f < shape.start_time_sec || cur_time - bar_length * 0.5f > shape.end_time_sec)
				continue;

			for (LineSegment& seg : shape.segments)
			{
				float sub_duration = seg.duration_sec / SegmentSubdivisions;
				diva::vec2 sub_delta_pos = seg.delta_pos / SegmentSubdivisions;

				for (int32_t i = 0; i < SegmentSubdivisions; i++)
				{
					float start_time = seg.start_time_sec + sub_duration * i;
					float end_time = seg.start_time_sec + sub_duration * (i + 1);
					diva::vec2 start_pos_abs = seg.start_pos + sub_delta_pos * i;
					float opacity = 1.0f;

					if (start_time > cur_time + bar_length * 1.5f)
						break;

					if (end_time < cur_time - bar_length * 0.5f)
						continue;

					if (cur_time - end_time >= 0.0f)
						opacity = 1.0f - util::Clamp((cur_time - end_time) / (bar_length * 0.5f), 0.0f, 1.0f);

					int32_t opacity_rgba = static_cast<int32_t>(util::Clamp(opacity * 255, 0.0f, 255.0f)) << 24;

					float progress = util::Clamp((cur_time - start_time) / sub_duration, 0.0f, 1.0f);
					diva::vec2 start_pos = (start_pos_abs - master_line_pos) * res_scale + diva::vec2(1920.0f / 2, 1080.0f / 2);
					diva::vec2 delta_pos = sub_delta_pos * progress * res_scale;
					float dist_progress = seg.distance * progress;

					if (vertex_count < 1)
					{
						SpriteVertex& v2 = vertices[vertex_count++];
						SpriteVertex& v3 = vertices[vertex_count++];
						v2.pos = diva::vec3(start_pos + seg.normal * 1.5f, 0.0f);
						v2.uv = { 0.0f, 16.0f };
						v2.color = 0x00FFFFFF | opacity_rgba;
						v3.pos = diva::vec3(start_pos - seg.normal * 1.5f, 0.0f);
						v3.uv = { 0.0f, 0.0f };
						v3.color = 0x00FFFFFF | opacity_rgba;
					}

					SpriteVertex& v4 = vertices[vertex_count++];
					SpriteVertex& v5 = vertices[vertex_count++];

					v4.pos = diva::vec3(start_pos + delta_pos + seg.normal * 15.0f, 0.0f);
					v4.uv = { dist_progress * 16.0f, 16.0f };
					v4.color = 0x00FFFFFF | opacity_rgba;
					v5.pos = diva::vec3(start_pos + delta_pos - seg.normal * 15.0f, 0.0f);
					v5.uv = { dist_progress * 16.0f, 0.0f };
					v5.color = 0x00FFFFFF | opacity_rgba;
				}
			}

			vertex_draw_count = vertex_count;
			break;
		}
	}

	void Disp()
	{
		// 2988 COL CHIP RED
		// 1576 COL CHIP WHITE

		char buffer[0x100] = { 0 };
		sprintf_s(buffer, "TIME: %.3f\nDISP: %d\nMASTER: (%.2f, %.2f)\nRES: (%.3f, %.3f)\n", cur_time, vertex_draw_count, master_line_pos.x, master_line_pos.y, res_scale.x, res_scale.y);
		spr::DrawSimpleText(75.0f, 200.0f, 14, 9, buffer, false, 0xFFFFFFFF, nullptr);

		diva::Rect rect = {};
		rect.x = master_line_pos.x - 10.0f;
		rect.y = master_line_pos.y - 10.0f;
		rect.width = 20.0f;
		rect.height = 20.0f;
		spr::DrawRect(&rect, 14, 11, 0xFFFF0000, 0);
		spr::DrawSimpleText(master_line_pos.x + 13.0f, master_line_pos.y - 13.0f, 14, 11, "MASTER", false, 0xFFFF0000, nullptr);

		if (vertex_draw_count > 2)
			DrawTriangles(vertices.data(), vertex_draw_count, 14, 10, 33648);
	}

} static pv_game_mirai;

void mirai_game::Init(PVGameData& pv_game, const db::SongEntry& song, const db::ChartEntry& diff)
{
	std::string aet_set_name = util::Format("AET_LINE_PV%03d", song.pv_id);
	AetSetInfo& info = *GetAetSetInfoByName(nullptr, aet_set_name);

	if (info.id != 0xFFFFFFFF)
	{
		int32_t difficulty = GetPvGameplayInfo()->difficulty;
		int32_t edition = GetPvGameplayInfo()->edition;

		if (difficulty >= 0 && difficulty < 5 && (edition == 0 || edition == 1))
		{
			std::string aet_scene_name = util::Format(
				"AET_LINE_PV%03d%s",
				song.pv_id,
				AET_SCENE_SUFFIXES[difficulty * (edition + 1)]
			);

			AetSceneInfo& scene = *GetAetSceneInfoByName(nullptr, aet_scene_name);
			if (scene.id != 0xFFFFFFFF)
			{
				pv_game_mirai.aet_data.scene_id = scene.id;
				nc::Print("Using the difficulty-specific AET scene for the line: %s\n", aet_scene_name.c_str());
			}
			else
				nc::Print("The difficulty-specific AET scene for the line (%s) doesn't exist.\n", aet_scene_name.c_str());
		}

		if (pv_game_mirai.aet_data.scene_id == 0xFFFFFFFF)
		{
			std::string aet_scene_name = util::Format("AET_LINE_PV%03d_MAIN", song.pv_id);
			AetSceneInfo& scene = *GetAetSceneInfoByName(nullptr, aet_scene_name);

			if (scene.id != 0xFFFFFFFF)
			{
				pv_game_mirai.aet_data.scene_id = scene.id;
				nc::Print("Using the main aet scene for the line: %s\n", aet_scene_name.c_str());
			}
			else
				nc::Print("There are no AET scenes for the line for this PV. This behaviour is undefined.\n");
		}

		pv_game_mirai.aet_data.set_id = info.id;
	}

	prj::string str;
	aet::LoadAetSet(pv_game_mirai.aet_data.set_id, &str);

	pv_game_mirai.song = &song;
	pv_game_mirai.difficulty = &diff;
}

bool mirai_game::IsLoaded()
{
	if (!pv_game_mirai.aet_data.ready && !aet::CheckAetSetLoading(pv_game_mirai.aet_data.set_id))
	{
		pv_game_mirai.aet_data.ready = true;
		pv_game_mirai.ImportLinesFromAetComposition();
	}

	return pv_game_mirai.aet_data.ready;
}

void mirai_game::Update(PVGameData& pv_game, float dt)
{
	pv_game_mirai.paused = pv_game.paused;
	pv_game_mirai.cur_time = pv_game.pv_time_sec;
	pv_game_mirai.Update(dt);
}

void mirai_game::Disp()
{
	pv_game_mirai.Disp();
}