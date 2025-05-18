#pragma once

#include <stdint.h>
#include <array>
#include <iterator>
#include <nc_time.h>

enum TargetType : int32_t
{
	// FT
	TargetType_Triangle       = 0,
	TargetType_Circle         = 1,
	TargetType_Cross          = 2,
	TargetType_Square         = 3,
	TargetType_TriangleHold   = 4,
	TargetType_CircleHold     = 5,
	TargetType_CrossHold      = 6,
	TargetType_SquareHold     = 7,
	TargetType_Random         = 8,
	TargetType_RandomHold     = 9,
	TargetType_Previous       = 10,
	TargetType_0B             = 11,
	TargetType_SlideL         = 12,
	TargetType_SlideR         = 13,
	TargetType_0E             = 14,
	TargetType_ChainslideL    = 15,
	TargetType_ChainslideR    = 16,
	TargetType_11             = 17,
	TargetType_ChanceTriangle = 18,
	TargetType_ChanceCircle   = 19,
	TargetType_ChanceCross    = 20,
	TargetType_ChanceSquare   = 21,
	TargetType_16             = 22,
	TargetType_ChanceSlideL   = 23,
	TargetType_ChanceSlideR   = 24,

	// X (these are their actual IDs)
	TargetType_TriangleRush   = 25,
	TargetType_CircleRush     = 26,
	TargetType_CrossRush      = 27,
	TargetType_SquareRush     = 28,

	// PSP / F / F 2nd (Changed IDs so they don't overlap base game notes)
	TargetType_UpW            = 29,
	TargetType_RightW         = 30,
	TargetType_DownW          = 31,
	TargetType_LeftW          = 32,
	TargetType_TriangleLong   = 33,
	TargetType_CircleLong     = 34,
	TargetType_CrossLong      = 35,
	TargetType_SquareLong     = 36,
	TargetType_Star           = 37, 
	TargetType_StarLong       = 38, // NOTE: Unused F mechanic, should I implement this?
	TargetType_StarW          = 39,
	TargetType_ChanceStar     = 40,
	TargetType_LinkStar       = 41,
	TargetType_LinkStarEnd    = 42,
	TargetType_StarRush       = 43,

	TargetType_Max,
	TargetType_Custom = 25
};

struct TargetGroupEx;
struct TargetStateEx
{
	// NOTE: Static data; Information about the target.
	TargetStateEx* prev = nullptr;
	TargetStateEx* next = nullptr;
	TargetGroupEx* parent = nullptr;
	int32_t target_type = -1;
	// int32_t target_index = 0;
	// int32_t sub_index = 0;
	float length = 0.0f;
	bool long_end = false;
	bool link_start = false;
	bool link_step = false;
	bool link_end = false;
	int32_t bal_max_hit_count = 0;

	// NOTE: Gameplay state
	ButtonState* hold_button = nullptr;
	PvGameTarget* org = nullptr;
	int32_t force_hit_state = HitState_None;
	int32_t hit_state = HitState_None;
	float hit_time = 0.0f;
	float flying_time_max = 0.0f;
	float flying_time_remaining = 0.0f;
	float delta_time_max = 0.0f;
	float delta_time = 0.0f;
	float length_remaining = 0.0f;
	float kiseki_time = 0.0f;
	float alpha = 0.0f;
	bool holding = false;
	bool success = false; // NOTE: If this note is a chance star, this determines if it's successful or not
	bool current_step = false; // NOTE: If this target is the current step of the link star chain
	int32_t step_state = 0;
	bool link_ending = false;

	// float sustain_bonus_time = 0.0f;
	// int32_t score_bonus = 0;
	// int32_t ct_score_bonus = 0;
	nc::Timer sustain_bonus_timer;
	bool double_tapped = false;
	int32_t bal_hit_count = 0;

	// NOTE: Visual info for long notes. This is kind of a workaround as to not mess too much
	//       with the vanilla game structs.
	int32_t target_aet = 0;
	int32_t button_aet = 0;
	int32_t bal_effect_aet = 0;
	diva::vec2 target_pos = { };
	diva::vec2 kiseki_pos = { }; // NOTE: Position where the kiseki will be updated from
	diva::vec2 kiseki_dir = { }; // NOTE: Direction of the note
	diva::vec2 kiseki_dir_norot = { };
	std::vector<SpriteVertex> kiseki;
	size_t vertex_count_max = 0;
	bool fix_long_kiseki = false;
	float bal_time = -1.0f;
	float bal_start_time = -1.0f;
	float bal_end_time = -1.0f;
	float bal_scale = 0.0f;

	void ResetPlayState();
	void ResetAetData();
	bool IsChainSucessful();
	void StopAet(bool button = true, bool target = true, bool kiseki = true);
	bool SetLongNoteAet();
	bool SetLinkNoteAet();
	bool SetRushNoteAet();

	inline bool IsWrong() const
	{
		return (hit_state >= HitState_WrongCool && hit_state <= HitState_WrongSad) || hit_state == HitState_Worst;
	}

	inline bool IsLongNote() const
	{
		return target_type == TargetType_TriangleLong ||
			target_type == TargetType_CircleLong ||
			target_type == TargetType_CrossLong ||
			target_type == TargetType_SquareLong ||
			target_type == TargetType_StarLong;
	}

	inline bool IsStarLikeNote() const
	{
		return target_type == TargetType_Star ||
			target_type == TargetType_ChanceStar ||
			target_type == TargetType_LinkStar ||
			target_type == TargetType_LinkStarEnd ||
			target_type == TargetType_StarRush;
	}

	inline bool IsRushNote() const
	{
		return target_type == TargetType_TriangleRush ||
			target_type == TargetType_CircleRush ||
			target_type == TargetType_CrossRush ||
			target_type == TargetType_SquareRush ||
			target_type == TargetType_StarRush;
	}

	inline bool IsNormalDoubleNote() const
	{
		return target_type == TargetType_UpW ||
			target_type == TargetType_RightW ||
			target_type == TargetType_DownW ||
			target_type == TargetType_LeftW;
	}

	inline bool IsLongNoteStart() const { return IsLongNote() && !long_end; }
	inline bool IsLongNoteEnd()   const { return IsLongNote() && long_end; }
	inline bool IsLinkNote()      const { return link_step; }
	inline bool IsLinkNoteStart() const { return link_step && link_start; }
	inline bool IsLinkNoteEnd()   const { return link_step && link_end; }
};

struct TargetGroupEx
{
	using it = std::array<TargetStateEx, 4>::iterator;
	using cit = std::array<TargetStateEx, 4>::const_iterator;

	int32_t bonus_score;
	int32_t ct_bonus_score;
	int32_t target_index;
	int32_t target_count;
	std::array<TargetStateEx, 4> targets;

	TargetGroupEx()
	{
		bonus_score = 0;
		ct_bonus_score = 0;
		target_index = 0;
		target_count = 0;
	}

	void ResetPlayState();

	it begin() { return targets.begin(); }
	cit cbegin() const { return targets.cbegin(); }
	it end() { return targets.end() + target_count; }
	cit cend() const { return targets.cend() + target_count; }
};

void InstallTargetHooks();