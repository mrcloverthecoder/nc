#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <memory>
#include <stdint.h>
#include "hooks.h"

// TODO: Re-organize this file completely. I think some stuff here could be moved into
//       other files and maybe centralize all the addresses on a single place so that
//       *if* the game ever updates it's easier to patch them up (actually the best thing 
//       to do in this case would be adding sigscan -- Too lazy for that though lmao)

namespace prj
{
	inline FUNCTION_PTR(void*, __fastcall, operatorNew, 0x1409777D0, size_t);
	inline FUNCTION_PTR(void*, __fastcall, operatorDelete, 0x1409B1E90, void*);

	// FROM: https://github.com/blueskythlikesclouds/DivaModLoader/blob/master/Source/DivaModLoader/Allocator.h
	//       https://github.com/blueskythlikesclouds/DivaModLoader/blob/master/Source/DivaModLoader/Types.h
	//
	template <class T>
	class Allocator
	{
	public:
		using value_type = T;

		Allocator() noexcept {}
		template <class U> Allocator(Allocator<U> const&) noexcept {}

		value_type* allocate(size_t n)
		{
			return reinterpret_cast<value_type*>(operatorNew(n * sizeof(value_type)));
		}

		void deallocate(value_type* p, size_t) noexcept
		{
			operatorDelete(reinterpret_cast<void*>(p));
		}
	};

	struct string_range
	{
		const char* start;
		const char* end;

		string_range() : start(nullptr), end(nullptr) {}
		string_range(const std::string& s) : start(s.c_str()), end(s.c_str() + s.size()) {}
	};

	template <class T>
	struct default_delete
	{
		void operator()(T* ptr) const noexcept
		{
			operatorDelete(ptr);
		}
	};

	template <class T>
	struct default_delete<T[]>
	{
		void operator()(T* ptr) const noexcept
		{
			static_assert(std::is_trivially_destructible_v<T>);
			operatorDelete(ptr);
		}
	};

	template <class T, class U>
	bool operator==(Allocator<T> const&, Allocator<U> const&) noexcept
	{
		return true;
	}

	template <class T, class U>
	bool operator!=(Allocator<T> const& x, Allocator<U> const& y) noexcept
	{
		return !(x == y);
	}

	using string = std::basic_string<char, std::char_traits<char>, Allocator<char>>;
	using string_view = std::basic_string_view<char, std::char_traits<char>>;

	template <typename T>
	using vector = std::vector<T, Allocator<T>>;

	template <typename _K, typename _V>
	using map = std::map<_K, _V, std::less<_K>, Allocator<std::pair<const _K, _V>>>;

	template<typename T>
	using unique_ptr = std::unique_ptr<T, default_delete<T>>;
}

enum HitState : int32_t
{
	HitState_Cool       = 0,
	HitState_Fine       = 1,
	HitState_Safe       = 2,
	HitState_Sad        = 3,
	HitState_WrongCool  = 4,
	HitState_WrongFine  = 5,
	HitState_WrongSafe  = 6,
	HitState_WrongSad   = 7,
	HitState_Worst      = 8,
	HitState_CoolDouble = 9,
	HitState_FineDouble = 10,
	HitState_SafeDouble = 11,
	HitState_SadDouble  = 12,
	HitState_CoolTriple = 13,
	HitState_FineTriple = 14,
	HitState_SafeTriple = 15,
	HitState_SadTriple  = 16,
	HitState_CoolQuad   = 17,
	HitState_FineQuad   = 18,
	HitState_SafeQuad   = 19,
	HitState_SadQuad    = 20,
	HitState_None       = 21
};

enum BasicHitState : int32_t
{
	BasicHitState_Cool  = 0,
	BasicHitState_Fine  = 1,
	BasicHitState_Safe  = 2,
	BasicHitState_Sad   = 3,
	BasicHitState_Worst = 4
};

enum InputDevice : int32_t
{
	InputDevice_Xbox      = 0,
	InputDevice_DualShock = 1,
	InputDevice_Switch    = 2,
	InputDevice_Steam     = 3,
	InputDevice_Keyboard  = 4,
	InputDevice_Unknown   = 5
};

enum GameFont : int32_t
{
	GameFont_Cmn10x16          = 0,
	GameFont_Num12x16          = 1,
	GameFont_Num14x18          = 2,
	GameFont_Num14x20          = 3,
	GameFont_Num20x26          = 4,
	GameFont_Num20x22          = 5,
	GameFont_Num22x22          = 6,
	GameFont_Num22x24          = 7,
	GameFont_Num24x30          = 8,
	GameFont_Num26x24          = 9,
	GameFont_Num28x40          = 10,
	GameFont_Num28x40_GOLD     = 11,
	GameFont_Num34x32          = 12,
	GameFont_Num40x52          = 13,
	GameFont_Num56x46          = 14,
	GameFont_Asc12x18          = 15,
	GameFont_Diva36x38x24      = 16,
	GameFont_Bold36x38x24      = 17,
	GameFont_Diva36x38         = 18,
	GameFont_Bold36x38         = 19,
	GameFont_CN36x38           = 20,
	GameFont_BoldCN36x38       = 21,
	GameFont_CN36x38_2         = 22,
	GameFont_BoldCN36x38_2     = 23,
	GameFont_KR36x38           = 24,
	GameFont_BoldKR36x38       = 25,
	GameFont_Latin36x38x24     = 26,
	GameFont_LatinBold36x38x24 = 27,
	GameFont_Latin36x38        = 28,
	GameFont_LatinBold36x38    = 29
};

// NOTE: Math types
//
namespace diva
{
	struct vec2
	{
		float x;
		float y;

		vec2() { x = 0.0f; y = 0.0f; }
		vec2(float x, float y) { this->x = x; this->y = y; }

		inline float length() const { return sqrtf(x * x + y * y); }
		inline vec2 abs() const { return vec2(fabsf(x), fabsf(y)); }

		inline vec2 operator+(const vec2& right) const
		{
			return { this->x + right.x, this->y + right.y };
		}

		inline vec2 operator-() const
		{
			return { -this->x, -this->y };
		}

		inline vec2 operator-(const vec2& right) const
		{
			return { this->x - right.x, this->y - right.y };
		}

		inline vec2 operator*(float scalar) const
		{
			return { this->x * scalar, this->y * scalar };
		}

		inline vec2 operator*(const vec2& right) const
		{
			return { this->x * right.x, this->y * right.y };
		}

		inline vec2 operator/(float scalar) const
		{
			return { this->x / scalar, this->y / scalar };
		}

		inline vec2 operator/(const vec2& right) const
		{
			return { this->x / right.x, this->y / right.y };
		}

		inline bool operator>(const vec2& right) const
		{
			return x > right.x && y > right.y;
		}

		inline bool operator>=(const vec2& right) const
		{
			return x >= right.x && y >= right.y;
		}

		inline bool operator<(const vec2& right) const
		{
			return x < right.x && y < right.y;
		}

		inline bool operator<=(const vec2& right) const
		{
			return x <= right.x && y <= right.y;
		}

		inline vec2 rotated(float angle)
		{
			float c = cosf(angle);
			float s = sinf(angle);
			return {
				this->x * c - this->y * s,
				this->x * s - this->y * c
			};
		}
	};

	struct vec3
	{
		float x;
		float y;
		float z;

		vec3()
		{
			x = 0.0f;
			y = 0.0f;
			z = 0.0f;
		}

		vec3(const vec2& xy, float z)
		{
			x = xy.x;
			y = xy.y;
			this->z = z;
		}

		vec3(float x, float y, float z)
		{
			this->x = x;
			this->y = y;
			this->z = z;
		}

		inline vec2 xy() const { return vec2(x, y); }

		inline vec3 operator+(const vec3& right) const
		{
			return { this->x + right.x, this->y + right.y, this->z + right.z };
		}

		inline vec3 operator-(const vec3& right) const
		{
			return { this->x - right.x, this->y - right.y, this->z - right.z };
		}
	};

	struct vec4
	{
		float x;
		float y;
		float z;
		float w;
	};

	struct mat4
	{
		vec4 row0;
		vec4 row1;
		vec4 row2;
		vec4 row3;

		inline diva::vec3 GetScale() const
		{
			// NOTE: Only works for 2D transformations
			float v = sqrtf(powf(row0.x, 2.0f) + powf(row1.x, 2.0f));
			float t = acosf(row0.x / v);
			return { t, t, 1.0f };
		}
	};

	struct Rect
	{
		float x;
		float y;
		float width;
		float height;
	};
}

struct SpriteVertex
{
	diva::vec3 pos;
	diva::vec2 uv;
	uint32_t color;
};

struct SprArgs
{
	uint32_t kind;
	uint32_t id;
	uint8_t color[4];
	int32_t attr;
	int32_t blend;
	int32_t index;
	int32_t layer;
	int32_t priority;
	int32_t resolution_mode_screen;
	int32_t resolution_mode_sprite;
	diva::vec3 center;
	diva::vec3 trans;
	diva::vec3 scale;
	diva::vec3 rot;
	diva::vec2 skew_angle;
	diva::mat4 mat;
	void* texture;
	int32_t shader;
	int32_t field_AC;
	diva::mat4 transform;
	bool field_F0;
	SpriteVertex* vertex_array;
	size_t num_vertex;
	int32_t field_108;
	void* field_110;
	bool set_viewport;
	diva::vec4 viewport;
	uint32_t flags;
	diva::vec2 sprite_size;
	int32_t field_138;
	diva::vec2 texture_pos;
	diva::vec2 texture_size;
	SprArgs* next;
	void* tex;

	SprArgs();
};

struct FontInfo
{
	int32_t font_id = -1;
	void* raw_font = nullptr;
	int32_t spr_id = -1;
	int32_t unk14 = 0;
	float unk18 = 0.0f;
	float unk1C = 0.0f;
	diva::vec2 font_size;
	diva::vec2 font_box_size;
	diva::vec2 size;
	diva::vec2 scale;
	diva::vec2 spacing;

	FontInfo();
	~FontInfo() = default;

	static FontInfo CreateSpriteFont(int32_t sprite_id, int32_t width, int32_t height);

	void SetSize(float width, float height);
};

enum TextFlags : int32_t
{
	TextFlags_AlignLeft = 0x1,
	TextFlags_AlignRight = 0x2,
	TextFlags_AlignHCenter = 0x4,
	TextFlags_AutoAlignHCenter = 0x8,
	TextFlags_AlignVCenter = 0x10,
	TextFlags_AutoAlignVCenter = 0x20,
	TextFlags_Clip = 0x200,
	TextFlags_Outline = 0x10000,

	TextFlags_AutoAlign = TextFlags_AutoAlignHCenter | TextFlags_AutoAlignVCenter
};

#pragma pack(push, 1)
struct PrintWork
{
	int32_t color;
	int32_t fill_color;
	bool clip;
	int8_t gap9[3];
	diva::vec4 clip_data;
	int32_t prio;
	int32_t layer;
	int32_t res_mode;
	uint32_t unk28;
	diva::vec2 text_current;
	diva::vec2 line_origin;
	size_t line_length;
	FontInfo* font;
	uint16_t empty_char;
	int8_t gap4E[2];

	inline void SetColor(uint32_t color, uint32_t fill)
	{
		color = color;
		fill_color = fill;
	}

	inline void SetOpacity(float opacity)
	{
		int32_t a = (opacity < 0.0f ? 0.0f : opacity > 1.0f ? 1.0f : opacity) * 255;
		color = (color & 0xFFFFFF) | (a << 24);
		fill_color = (fill_color & 0xFFFFFF) | (a << 24);
	}
};

// TODO: Clean-up this struct. I think the struct might be actually smaller than this.
struct TextArgs
{
	float max_width; // NOTE: Text will get squished to fit once it goes past this limit
	PrintWork print_work;
	uint32_t unk60;
	uint32_t unk64;
	uint32_t unk68;
	uint32_t unk6C;
	uint32_t unk70;
	uint32_t unk74;
	uint32_t unk78;
	uint32_t unk7C;

	TextArgs();
};
#pragma pack(pop)

enum AetAction : int32_t
{
	AetAction_None = 0,
	AetAction_InOnce = 1,
	AetAction_InLoop = 2,
	AetAction_Loop = 3,
	AetAction_OutOnce = 4,
	AetAction_SpecialOnce = 5,
	AetAction_SpecialLoop = 6
};

struct AetArgs
{
	uint32_t scene_id = 0;
	const char* layer_name = nullptr;
	prj::string start_marker;
	prj::string end_marker;
	prj::string loop_marker;
	float start_time = -1.0f;
	float end_time = -1.0f;
	int32_t flags = 0x0;
	int32_t index = 0;
	int32_t layer = 0;
	int32_t prio = 0;
	int32_t res_mode = 13;
	diva::vec3 pos = { 0.0f, 0.0f, 0.0f };
	diva::vec3 rot = { 0.0f, 0.0f, 0.0f };
	diva::vec3 scale = { 1.0f, 1.0f, 1.0f };
	diva::vec3 anchor = { 0.0f, 0.0f, 0.0f };
	float frame_speed = 1.0f;
	diva::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
	prj::map<prj::string, int32_t> layer_sprites;
	prj::string sound_path;
	prj::map<prj::string, prj::string> sound_replace;
	int32_t sound_queue_index = 0;
	prj::map<uint32_t, uint32_t> sprite_replace;
	prj::map<uint32_t, void*> sprite_texture;
	prj::map<uint32_t, uint32_t> sprite_discard;
	void* frame_rate_control = nullptr;
	bool sound_voice = false;
	int32_t dword154 = 0;
	int32_t dword158 = 0;
	int32_t dword15C = 0;
	int32_t id = 0;
	int32_t dword164 = 0;
	diva::vec3 pos_2 = { 0.0f, 0.0f, 0.0f };

	AetArgs() = default;
	AetArgs(uint32_t scene, const char* layer, int32_t prio, int32_t marker_mode);
	~AetArgs() = default;
};

struct AetLayout
{
	diva::mat4 matrix;
	diva::vec3 position;
	diva::vec3 anchor;
	float width;
	float height;
	float opacity;
	uint32_t color;
	int32_t resolution_mode;
	uint32_t unk6C;
	int32_t unk70;
	uint8_t blendMode;
	uint8_t transferFlags;
	uint8_t trackMatte;
	int32_t unk78;
	int32_t unk7C;
};

using AetComposition = prj::map<prj::string, AetLayout>;
using AetHandle = int32_t;

// NOTE: Koren named this struct "struc_14" so I have no idea what it's real name is. But PvGameplayInfo sounds fitting.
struct PvGameplayInfo
{
	int32_t type;
	int32_t difficulty;
};

struct PvDscTarget
{
	int32_t type;
	diva::vec2 target_pos;
	diva::vec2 start_pos;
	float amplitude;
	int32_t frequency;
	bool slide_chain_start;
	bool slide_chain_end;
	bool slide_chain_left;
	bool slide_chain_right;
	bool slide_chain;
};

struct PvDscTargetGroup
{
	int32_t target_count;
	PvDscTarget targets[4];
	int32_t field_94;
	int64_t spawn_time;
	int64_t hit_time;
	bool slide_chain;
};

struct PVGameData;
struct PVGameArcade;

// NOTE: Some information were taken from ReDIVA. Thank you, Koren!
//		 https://github.com/korenkonder/ReDIVA/blob/master/src/ReDIVA/pv_game/pv_game_pv_data.hpp
//
struct PVGamePvData
{
	bool field_0;
	int32_t dsc_state;
	bool play;
	int script_buffer[45000];
	int script_pos;
	uint8_t gap2BF30[24];
	float float2BF48;
	uint8_t gap2BF4C[36];
	int int2BF70;
	int int2BF74;
	PVGameData* pv_game;
	uint8_t gap2BF80[24];
	float float2BF98;
	float float2BF9C;
	int32_t dword2BFA0;
	int32_t dword2BFA4;
	uint64_t script_time;
	uint8_t gap2BFA8[16];
	uint8_t byte2BFC0;
	uint8_t byte2BFC1;
	uint8_t gap2BFC8[8];
	int32_t dword2BFCC;
	int32_t dword2BFD0;
	uint8_t byte2BFD4;
	uint8_t byte2BFD5;
	uint8_t byte2BFD6;
	uint8_t gap2BFD7[961];
	uint8_t byte2C398;
	int32_t dword2C39C;
	uint8_t gap2C3A0[8];
	uint8_t byte2C3A8;
	int32_t dword2C3AC;
	uint8_t gap2C3B0[272];
	uint8_t byte2C4C0;
	int32_t dword2C4C4;
	float float2C4C8;
	float float2C4CC;
	float float2C4D0;
	float float2C4D4;
	float float2C4D8;
	float float2C4DC;
	bool has_dsc_signature;
	bool is_dsc_ac200_format;
	uint8_t pad2C4E2[2];
	int targets_remaining;
	prj::vector<PvDscTargetGroup> targets;
	size_t target_index;
	float float2C508;
	char char2C50C;
	uint8_t gap2C50D[63];
	uint8_t gap2C54C[8];
	int32_t dsc_branch_mode;
	int32_t first_challenge_target_index;
	int32_t last_challenge_target_index;
	uint8_t gap2C558[704];
	uint8_t byte2C820;
	int32_t dword2C824;
	int32_t dword2C828;
	float float2C82C;
};

struct PvGameTarget
{
	PvGameTarget* prev;
	PvGameTarget* next;
	int32_t dword10;
	int32_t target_type;
	float flying_time_remaining;
	float player_hit_time;
	float flying_time;
	float amplitude;
	float freq;
	float cur_freq;
	uint8_t gap30[4];
	int32_t dword34;
	uint8_t gap38[4];
	float dword3C;
	uint8_t gap40;
	bool slide_chain_start;
	bool slide_chain_end;
	bool slide_left;
	bool slide_right;
	bool slide_chain;
	uint8_t gap46[2];
	int64_t hit_time;
	uint8_t gap50[32];
	int32_t target_aet;
	int32_t button_aet;
	int32_t dword78;
	int32_t target_eff_aet;
	diva::vec2 target_pos;
	diva::vec2 button_pos;
	diva::vec2 delta_pos;
	diva::vec2 delta_pos_sq;
	diva::vec2 vecA0;
	SpriteVertex kiseki[40];
	bool note_active;
	uint8_t gap469[3];
	float out_start_time;
	uint8_t gap470[4];
	int32_t hit_state;
	int32_t multi_count;
	int32_t dword47C;
	float float480;
	uint8_t gap484[12];
	uint8_t byte490;
	bool b491;
	bool b492;
	bool b493;
	float button_opacity;
	float target_opacity;
	float sudden_appear_frame;
	float scaling_end_time;
	uint8_t gap4A4[4];
	int32_t sprite_index;
	uint8_t gap4AC[4];
	int64_t appear_time;
	int16_t word4B8;
	uint8_t gap4BA[2];
	float kiseki_width;
	int32_t target_index;
	int32_t dword4C4;
};

struct PVGameArcade
{
	bool play;
	uint8_t gap1[7];
	void* ptr08;
	uint8_t gap[16];
	PvGameTarget* target;
	PvGameTarget target_buffer[64];
	uint8_t gap13228[64];
	float target_time;
	float cool_early_window;
	float cool_late_window;
	float fine_early_window;
	float fine_late_window;
	float safe_early_window;
	float safe_late_window;
	float sad_early_window;
	float sad_late_window;
	uint8_t byte1328C;
	uint8_t byte1328D;
	bool bool1328E;
	uint8_t byte1328F;
	float current_time;
	int32_t int13294;
	int32_t hit_effects[64];
	int32_t hit_effect_index;
	uint8_t gap1339C[96];
	int32_t dword133FC;
	int32_t dword13400[3];
	int32_t slide_hit_effects[64];
	int32_t slide_hit_effect_index;
	uint8_t gap13510[8];
	float fl13518;
	float fl1351C;
	float fl13520;
	float fl13524;
	float fl13528;
	float fl1352C;
	float fl13530;
	bool mute_slide_chime;
	uint8_t gap13535[7];
	bool bool1353C[4];
	uint8_t gap13540[14];
	int8_t char1354E[4];
	float fl13554[2];

	void EraseTarget(PvGameTarget* target);
	void RemoveTargetAet(PvGameTarget* target);
	void PlayHitEffect(int32_t index, const diva::vec2& pos);
};

struct PVGameUI
{
	int32_t int00;
	int32_t aet_list[95];
	bool visibility[95];
	float frame_bottom_offset[2];
	float frame_bottom_dt[2];
	float frame_top_offset[2];
	float frame_top_dt[2];
	float top_offset[2];
	float bottom_offset[2];
	bool frame_visibility[2];
	int32_t frame_action[2];
	uint8_t gap380[368];
	int32_t life;
	bool draw_combo_counter;
	int32_t chance_txt_state;
	bool show_chance_txt;
	int32_t int39C;
	uint8_t gap39C[8];
	int32_t combo_counter_state;
	float combo_counter_time;
	uint8_t gap3B0[12];
	int32_t combo_num;
	int32_t int3C0;
	diva::vec2 combo_counter_pos;

	void SetBonusText(int32_t value, const diva::vec2& pos);
	void RemoveBonusText();
};

struct PVGameData
{
	bool loaded;
	bool paused;
	uint8_t byte2;
	uint8_t byte3;
	uint8_t gap4[196];
	PVGamePvData pv_data;
	uint8_t gap2C8F8[8];
	PVGameUI ui;
	uint8_t gap2CCCC[1520 - sizeof(PVGameUI)];
	uint8_t byte2CEF0;
	uint8_t gap2CEF1[39];
	const char char2CF18;
	uint8_t gap2CF19[23];
	int64_t qword2CF30;
	const char char2CF38;
	uint8_t gap2CF39[23];
	int64_t qword2CF50;
	const char char2CF58;
	uint8_t gap2CF59[23];
	int64_t qword2CF70;
	const char char2CF78;
	uint8_t gap2CF79[23];
	int64_t qword2CF90;
	const char char2CF98;
	uint8_t gap2CF99[23];
	int64_t qword2CFB0;
	const char char2CFB8;
	uint8_t gap2CFB9[23];
	int64_t qword2CFD0;
	const char char2CFD8;
	uint8_t gap2CFD9[23];
	int64_t qword2CFF0;
	const char char2CFF8;
	uint8_t gap2CFF9[23];
	int64_t qword2D010;
	uint8_t gap2D018[357];
	uint8_t byte2D17D;
	uint8_t gap2D17E[174];
	uint8_t gap2D22C[8];
	int32_t life;
	int32_t score;
	int32_t dword2D23C;
	bool scoring_enabled;
	uint8_t gap2D241[3];
	int32_t total_challenge_bonus;
	int32_t combo;
	int32_t total_surv_challenge_bonus;
	uint8_t gap2D250[8];
	int32_t challenge_combo;
	int32_t max_combo;
	int32_t judge_count[5];         // NOTE: Includes WRONG
	int32_t judge_count_correct[5]; // NOTE: Does not include WRONG
	uint8_t gap2D288[16];
	float float2D298;
	uint8_t gap2D29C[12];
	int int2D2A8;
	uint8_t gap2D2AC;
	uint8_t byte2D2AD;
	float float2D2B0;
	int16_t word2D2B4;
	uint8_t gap2D2B6[6];
	int32_t challenge_bonus;
	int32_t dword2D2C0;
	uint8_t gap2D2C4[8];
	int32_t reference_score;
	int32_t reference_score_with_life;
	int32_t dword2D2D4;
	prj::vector<int32_t> target_reference_scores;
	uint8_t gap2D2F0[8];
	int64_t qword2D2F8;
	uint8_t gap2D300[4];
	float percentage;
	float float2D308;
	float float2D30C;
	uint8_t gap2D310[13];
	uint8_t byte2D31D;
	uint8_t gap2D31E;
	uint8_t byte2D31F;
	int64_t challenge_time_start;
	int64_t challenge_time_end;
	int64_t pv_end_time;
	float pv_end_time_sec;
	float float2D33C;
	uint8_t gap2D340[4];
	float float2D344;
	int64_t qword2D348;
	float float2D350;
	float float2D354;
	int32_t dword2D358;
	int32_t dword2D35C;
	uint8_t gap2D360[5];
	bool has_success_note;
	uint8_t gap2D366[10];
	int32_t dword2D370;
	uint8_t byte2D374;
	uint8_t byte2D375;
	uint8_t gap2D376[30];
	int32_t current_frame;
	int32_t dword2D398;
	int32_t total_life_bonus;
	int32_t dword2D3A0;
	uint8_t gap2D3A4[4];
	uint8_t byte2D3A8;
	uint8_t byte2D3A9;
	uint8_t gap2D3AA;
	uint8_t byte2D3AB;
	uint8_t gap2D3AC[2];
	uint8_t byte2D3AE;
	bool is_success_branch;
	uint8_t gap2D3B0[16];
	int32_t dword2D3C0;
	uint8_t gap2D3C4[16];
	uint8_t byte2D3D4;
	uint8_t gap2D3D5[19];
	int32_t dword2D3E8;
	uint8_t gap2D3EC[2020];
	int64_t qword2DBD0;
	int64_t qword2DBD8;
	float float2DBE0;
	int32_t dword2DBE4;
	uint8_t byte2DBE8;
	uint8_t byte2DBE9;
	uint8_t byte2DBEA;
	float float2DBEC;
	uint8_t byte2DBF0;
	uint8_t gap2DBF1[11];
	float float2DBFC;
	uint8_t gap2DC00[684];
	int64_t int642DEAC;
};

struct SoundEffect
{
	prj::string button;
	prj::string slide;
	prj::string slidechain;
	prj::string slide_button;
	prj::string slide_ok;
	prj::string slide_ng;
	prj::string chime;
};

struct AetSetInfo
{
	uint32_t id;
	const char* name;
	const char* ptr10;
	const char* file_name;
	const char* ptr20;
	int32_t index;
	uint32_t spr_set_id;
};

struct AetSceneInfo
{
	uint32_t id;
	const char* name;
	const char* str10;
	int32_t dword18;
	int32_t dword1C;
};

enum GameLocale : int32_t
{
	GameLocale_JP = 0,
	GameLocale_EN = 1,
	GameLocale_ZH = 2,
	GameLocale_TW = 3,
	GameLocale_KR = 4,
	GameLocale_FR = 5,
	GameLocale_IT = 6,
	GameLocale_DE = 7,
	GameLocale_SP = 8,
	GameLocale_Max
};

namespace diva
{
	struct InputState
	{
		uint8_t _data[6720];

		// 0x08 - Cursor position X (1920x1080)
		// 0x09 - Cursor position Y (1920x1080)
		// 0x10 - Cursor position X (1280x720)
		// 0x11 - Cursor position Y (1280x720)
		// 0x12 - Cursor pos delta X
		// 0x13 - Cursor pos delta Y
		// 0x14 - LStick X axis
		// 0x15 - LStick Y axis
		// 0x16 - RStick X axis
		// 0x17 - RStick Y axis
		float GetPosition(int32_t index);
		int32_t GetDevice();
		bool IsButtonDown(int32_t key);
		bool IsButtonTapped(int32_t key);
		bool IsButtonTappedAbs(int32_t key);
		bool IsButtonTappedOrRepeat(int32_t key);
		inline bool IsInputBlocked() { return *reinterpret_cast<bool*>(&_data[0x1A28]); }
	};

	inline FUNCTION_PTR(InputState*, __fastcall, GetInputState, 0x1402AC970, int32_t index);
	inline FUNCTION_PTR(bool, __fastcall, CheckButtonDelegate, 0x1402AB1A0,
		InputState* state, int32_t key, bool(__fastcall* func)(InputState*, int32_t));
}

namespace aet
{
	// NOTE: Queues AetSet file to be loaded
	inline FUNCTION_PTR(void, __fastcall, LoadAetSet, 0x1402C9FA0, uint32_t id, prj::string* out);
	// NOTE: Returns true if AetSet file is still loading, false otherwise
	inline FUNCTION_PTR(bool, __fastcall, CheckAetSetLoading, 0x1402CA020, uint32_t id);
	// NOTE: Free AetSet file from memory
	inline FUNCTION_PTR(void, __fastcall, UnloadAetSet, 0x1402CA040, uint32_t id);
	// NOTE: Plays an Aet layer described by the `args` variable passed in.
	inline FUNCTION_PTR(int32_t, __fastcall, Play, 0x1402CA220, AetArgs* args, int32_t id);
	// NOTE: Returns the current from on an Aet object.
	inline FUNCTION_PTR(float, __fastcall, GetFrame, 0x1402CA500, int32_t id);
	// NOTE: Returns whether an Aet object has ended playback. Will always return false if the loop flag is set.
	inline FUNCTION_PTR(bool, __fastcall, GetEnded, 0x1402CA510, int32_t id);
	// NOTE: Retrieves layout data for specific Aet object ID.
	inline FUNCTION_PTR(void, __fastcall, GetComposition, 0x1402CA670, AetComposition* comp, int32_t id);
	// NOTE: Returns the time where the specified marker is placed on a layer
	inline FUNCTION_PTR(float, __fastcall, GetMarkerTime, 0x1402CA170, uint32_t id, const char* layer, const char* marker);

	// NOTE: Plays a layer from AET_GAM_CMN. Resolution mode is HDTV720
	inline FUNCTION_PTR(int32_t, __fastcall, PlayGamCmnLayer, 0x14027B2C0, int32_t prio, void* a2, const char* layer, const diva::vec2* pos);

	// NOTE: Sets the position of the layer object
	inline FUNCTION_PTR(void, __fastcall, SetPosition, 0x1402CA3F0, int32_t id, const diva::vec3* pos);
	// NOTE: Sets the scale of the layer object
	inline FUNCTION_PTR(void, __fastcall, SetScale, 0x1402CA400, int32_t id, const diva::vec3* scale);
	// NOTE: Sets the current frame of the layer objects
	inline FUNCTION_PTR(void, __fastcall, SetFrame, 0x1402CA4B0, int32_t id, float frame);
	// NOTE: Sets if the layer object should play (1) or pause (0)
	inline FUNCTION_PTR(void, __fastcall, SetPlay, 0x1402CA380, int32_t id, bool play);
	inline FUNCTION_PTR(void, __fastcall, SetVisible, 0x1402CA390, int32_t id, bool visible);
	inline FUNCTION_PTR(void, __fastcall, SetOpacity, 0x1402CA420, int32_t id, float opacity);

	void CreateAetArgs(AetArgs* args, uint32_t scene_id, const char* layer_name, int32_t prio);
	void CreateAetArgs(AetArgs* args, uint32_t scene_id, const char* layer_name, int32_t flags, int32_t layer, int32_t prio, const char* start_marker, const char* end_marker);

	// NOTE: Stops (removes, not pause!) an Aet layer object created by `diva::aet::Play`
	void Stop(int32_t id);
	// NOTE: Same as the normal one, but sets id to 0 afterwards
	void Stop(int32_t* id);
	// NOTE: Stops if GetEnded() returns true and returns the value of GetEnded().
	bool StopOnEnded(int32_t* id); 

	int32_t PlayLayer(uint32_t scene_id, int32_t prio, int32_t flags, const char* layer, const diva::vec2* pos, int32_t index, const char* start_marker, const char* end_marker, float start_time, float end_time, int32_t a11, void* frmctl);
	int32_t PlayLayer(uint32_t scene, int32_t prio, int32_t flags, const char* layer, const diva::vec2* pos, const char* start_marker, const char* end_marker);

	int32_t PlayLayer(uint32_t scene_id, int32_t prio, const char* layer, int32_t action);
}

namespace spr
{
	// NOTE: Queues SprSet file to be loaded
	inline FUNCTION_PTR(void, __fastcall, LoadSprSet, 0x1405B4770, uint32_t id, prj::string_view* out);
	// NOTE: Returns true if SprSet file is still loading, false otherwise
	inline FUNCTION_PTR(bool, __fastcall, CheckSprSetLoading, 0x1405B4810, uint32_t id);
	// NOTE: Free SprSet file from memory
	inline FUNCTION_PTR(void, __fastcall, UnloadSprSet, 0x1405B48B0, uint32_t id);

	// NOTE: Draw sprite to screen
	inline FUNCTION_PTR(SprArgs*, __fastcall, DrawSprite, 0x1405B49C0, SprArgs* args);

	// NOTE: Retrieves a font from the font list
	inline FUNCTION_PTR(FontInfo*, __fastcall, GetFont, 0x1402C4DC0, FontInfo* font, int32_t font_id);
	inline FUNCTION_PTR(FontInfo*, __fastcall, GetLocaleFont, 0x1402C4E10, FontInfo* font, int32_t id, bool a3);
	inline FUNCTION_PTR(void, __fastcall, SetFontSize, 0x1402C5240, FontInfo* font, float w, float h);
	// NOTE: Draw text to the screen (UTF-8, char*)
	inline FUNCTION_PTR(void, __fastcall, DrawTextA, 0x1402C57B0, TextArgs* params, uint32_t flags, const char* text);
	inline FUNCTION_PTR(void**, __fastcall, DrawSimpleText, 0x14027B160, float x, float y, int32_t res, int32_t prio, const char* text, bool center, uint32_t color, const diva::vec4* clip);
}

namespace sound
{
	// NOTE: Plays a sound effect.
	//       `queue_index` can be a value between 1~5 (?)
	inline FUNCTION_PTR(int32_t, __fastcall, PlaySoundEffect, 0x1405AA550, int32_t queue_index, const char* name, float volume);

	// NOTE: Releases a sound effect from the queue.
	//
	inline FUNCTION_PTR(void, __fastcall, ReleaseCue, 0x1405AAD00, int32_t queue_index, const char* name, bool force_release);

	// NOTE: Releases every cue in a sound queue.
	//
	inline FUNCTION_PTR(void, __fastcall, ReleaseAllCues, 0x1405AADD0, int32_t queue_index);

	// NOTE: Requests SoundWork to load a sound farc. Returns false if the queue is full.
	//
	inline FUNCTION_PTR(bool, __fastcall, RequestFarcLoad, 0x1405AA250, const char* path);

	// NOTE: Returns true if SoundWork is still loading the farc, false otherwise.
	//
	inline FUNCTION_PTR(bool, __fastcall, IsFarcLoading, 0x1405AA420, const char* path);

	// NOTE: Unloads a sound farc from memory
	//
	inline FUNCTION_PTR(bool, __fastcall, UnloadFarc, 0x1405AA480, const char* path);

	inline FUNCTION_PTR(void, __fastcall, PlayEnterSE, 0x1401A7F00);
	inline FUNCTION_PTR(void, __fastcall, PlayCancelSE, 0x1401A7F20);
	inline FUNCTION_PTR(void, __fastcall, PlaySelectSE, 0x1401A7F60);
}

namespace loc
{
	inline FUNCTION_PTR(const char*, __fastcall, GetStrArrayString, 0x140239BC0, int32_t id);
	inline std::string_view GetString(int32_t id)
	{
		if (const char* data = GetStrArrayString(id); data != nullptr)
			return std::string_view(data, strlen(data));
		return std::string_view();
	}
}

inline FUNCTION_PTR(PvGameplayInfo*, __fastcall, GetPvGameplayInfo, 0x14027DD90);
inline FUNCTION_PTR(bool, __fastcall, IsPracticeMode, 0x1401E7B90);
inline FUNCTION_PTR(int32_t, __fastcall, FindNextCommand, 0x140257D50, PVGamePvData* pv_data, int32_t op, int32_t* time, int32_t* branch, int32_t head);
inline FUNCTION_PTR(PVGameData*, __fastcall, GetPVGameData, 0x140266720);
inline FUNCTION_PTR(bool, __fastcall, IsInSongResults, 0x1401E8090);
inline FUNCTION_PTR(int64_t, __fastcall, DrawTriangles, 0x1405B4C50, SpriteVertex* vertices, size_t vertex_count, int32_t res_mode, int32_t prio, uint32_t sprite_id);
inline FUNCTION_PTR(int32_t, __fastcall, GetGameLocale, 0x1402C8D20);

inline FUNCTION_PTR(bool, __fastcall, IsSuddenEquipped, 0x14024B720, PVGameData* pv_game);

inline FUNCTION_PTR(AetSetInfo*, __fastcall, GetAetSetInfoByName, 0x140294490, void* a1, const prj::string_range& name);
inline FUNCTION_PTR(AetSceneInfo*, __fastcall, GetAetSceneInfoByName, 0x140294610, void* a1, const prj::string_range& name);

diva::vec2 GetScaledPosition(const diva::vec2& v);

inline prj::vector<prj::string>* GetRomDirectories()
{
	return reinterpret_cast<prj::vector<prj::string>*>(0x1414AB8A0);
}

namespace dsc
{
	struct OpcodeInfo
	{
		int32_t id;
		int32_t length_old;
		int32_t length;
		int32_t unk0C;
		const char* name;
	};

	// NOTE: Returns a pointer to a struct that describes DSC opcode <id>
	inline FUNCTION_PTR(const OpcodeInfo*, __fastcall, GetOpcodeInfo, 0x14024DCA0, int32_t id);

	inline bool IsCurrentDifficulty(int32_t bit) { return (bit & (1 << GetPvGameplayInfo()->difficulty)) != 0; }
}

namespace game
{
	struct GlobalPVInfo
	{
		int32_t difficulty; // NOTE: Extra-extreme is set here as 4, and not 3 with edition 1!
		int32_t pv_id;
	};

	struct PVLoadParam
	{
		struct Data
		{
			int32_t difficulty;
			int32_t edition;
			int32_t int8;
			uint8_t gap0[0x50];
			int32_t pv_id;
			uint8_t gap60[0x648];
		};

		Data data[4];
	};

	struct NoteSE
	{
		int32_t id;
		prj::string name;
		prj::string se_name;
	};

	inline bool IsFutureToneMode() { return *reinterpret_cast<bool*>(0x1414AB9E3); }
	inline bool IsPvMode() { return GetPvGameplayInfo()->type == 3 || GetPvGameplayInfo()->type == 6; }
	inline int32_t GetFramerate() { return *reinterpret_cast<int32_t*>(0x1414ABBB8); }

	// NOTE: The CustomizeSel doesn't have duplicated code for the Switch version
	//       like other UI screens, so this is valid for both Switch and PS4 mode.
	inline FUNCTION_PTR(bool, __fastcall, IsCustomizeSelTaskReady, 0x1401DE790);

	inline FUNCTION_PTR(GlobalPVInfo*, __fastcall, GetGlobalPVInfo, 0x1401D6520);
	inline FUNCTION_PTR(PVLoadParam*, __fastcall, GetPVLoadParam, 0x14040B2A0);

	inline FUNCTION_PTR(char*, __fastcall, GetSaveData, 0x1401D6510);
	inline FUNCTION_PTR(void*, __fastcall, GetConfigSet, 0x1401D5F40, void* save_data, int32_t pv, bool a3);

	inline FUNCTION_PTR(NoteSE*, __fastcall, GetButtonSE, 0x1403F7540, int32_t id);

	int32_t GetGlobalPvID();
}

namespace pv_db
{
	struct PvDBDifficulty
	{
		int32_t difficulty;
		int32_t edition;
		int32_t extra;
		int32_t dwordC;
		int64_t attributes;
		int32_t gap18[2];
		std::string script_file_name;
		int32_t level;
		int32_t level_sort_index;
		std::string button_se;
		std::string success_se;
		std::string slide_se;
		std::string slidechain_start_se;
		std::string slidechain_se;
		std::string slidechain_success_se;
		std::string slidechain_failure_se;
		std::string slide_touch_se;
		uint8_t gap148[932];
		int32_t script_format;
		uint8_t gap4F0[64];
	};

	struct PvDBEntry
	{
		int32_t pv_id;
		int32_t date;
		std::string song_name;
		std::string song_name_reading;
		int32_t unk48;
		int32_t bpm;
		std::string song_file_name;
		uint8_t gap70[24];
		float sabi_start_time;
		float sabi_play_time;
		uint8_t gap90[32];
		prj::vector<PvDBDifficulty> difficulties[5];
		uint8_t gap128[664];
	};

	inline FUNCTION_PTR(void*, __fastcall, FindPVEntry, 0x1404BC500, int32_t pv_id);
	inline FUNCTION_PTR(PvDBDifficulty*, __fastcall, FindDifficulty, 0x1404BCA70, void* entry, int32_t difficulty, int32_t edition);
}

// NOTE: File IO functions
typedef void* FileHandler;

inline FUNCTION_PTR(bool, __fastcall, FileRequestLoad, 0x1402A4710, FileHandler* file, const char* path, int32_t a3);
inline FUNCTION_PTR(bool, __fastcall, FileCheckExists, 0x1402A5330, prj::string* path, prj::string* fixed);
inline FUNCTION_PTR(bool, __fastcall, FileCheckNotReady, 0x151C03830, FileHandler* file);
inline FUNCTION_PTR(void*, __fastcall, FileGetData, 0x151C0EF70, FileHandler* file);
inline FUNCTION_PTR(size_t, __fastcall, FileGetSize, 0x151C7ADA0, FileHandler* file);
inline FUNCTION_PTR(void, __fastcall, FileFree, 0x1402A4E90, FileHandler* file);

inline bool ShouldUpdateTargets()
{
	PVGameData* pv_game = GetPVGameData();
	if (pv_game != nullptr)
		return !IsInSongResults() && !pv_game->paused && !pv_game->byte2;

	return false;
}