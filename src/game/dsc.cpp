#include <array>
#include <initializer_list>
#include <vector>
#include <hooks.h>
#include <diva.h>
#include <nc_log.h>
#include <nc_state.h>
#include <util.h>
#include "dsc.h"

enum DscState : int32_t
{
	DscState_Idle    = 0,
	DscState_Load    = 1,
	DscState_Ready   = 2,
	DscState_Missing = 3,
};

enum DscFormat : int32_t
{
	DscFormat_FT = 0,
	DscFormat_AC = 1,
	DscFormat_F  = 2,
	// NOTE: Also includes X, but since the stuff we actually read is the same between both
	//       it's pointless to differentiate
	DscFormat_F2 = 3,
	DscFormat_NC = 4,
};

enum MergeFlags : int32_t
{
	MergeFlags_None        = 0x0,
	MergeFlags_IgnoreChart = 0x1,
	MergeFlags_IgnorePV    = 0x2,
};

enum DscOp : int32_t
{
	DscOp_End    = 0,
	DscOp_Time   = 1,
	DscOp_Target = 6,
	DscOp_ChangeField = 14,
	DscOp_ModeSelect = 26,
	DscOp_BarTimeSet = 28,
	DscOp_TargetFlyingTime = 58,
	DscOp_PVBranchMode = 65,
	DscOp_TargetEffect = 91,
};

struct DscFrame
{
	struct DscCommand
	{
		int32_t length;
		int32_t opcode;
		// NOTE: The largest command in FT has 24 parameters so this should be more than enough
		int32_t data[30];

		DscCommand() = default;
		DscCommand(int32_t opcode, std::initializer_list<int32_t> params)
		{
			length = params.size();
			this->opcode = opcode;
			memcpy_s(data, sizeof(data), params.begin(), params.size() * sizeof(int32_t));
		}
	};

	std::array<std::vector<DscCommand>, 3> branch_modes;
};

static bool dsc_ready[2] = { false, false };
static int32_t* dsc_data[2] = { nullptr, nullptr };
static int32_t dsc_state = DscState_Idle;
static FileHandler file_handler = nullptr;

static bool RequestLoadDsc(const prj::string& original_path)
{
	file_handler = nullptr;

	if (!state.nc_chart_entry.has_value())
		return false;

	prj::string file_name = state.nc_chart_entry.value().script_file_name.c_str();
	if (util::Compare(file_name, "(NULL)") || util::Compare(file_name, original_path))
		return false;

	prj::string fixed;
	if (!FileCheckExists(&file_name, &fixed))
		return false;

	if (!FileRequestLoad(&file_handler, file_name.c_str(), 1))
	{
		file_handler = nullptr;
		return false;
	}

	return true;
}

static int32_t GetOpcodeLength(int32_t format, int32_t op)
{
	if (op == DscOp_Target)
	{
		if      (format == DscFormat_F)  return 11;
		else if (format == DscFormat_NC) return 11;
		else if (format == DscFormat_F2) return 12;
	}
	else if (op == DscOp_TargetEffect && format == DscFormat_F2)
		return 11;
	else if (op == DscOp_ChangeField && format == DscFormat_F2)
		return 2;
	
	if (const dsc::OpcodeInfo* info = dsc::GetOpcodeInfo(op); info != nullptr)
		return format == DscFormat_AC ? info->length_old : info->length;

	return 0;
}

static int32_t GetDscFormat(int32_t signature)
{
	if (signature < 0x10000000 || signature < 0x10101514)
		return DscFormat_AC;

	switch (signature)
	{
	case 0x43535650:
		return DscFormat_F2;
	case 0x12020220:
		return DscFormat_F;
	case 0x25061313: // 13/06/2025 13:00
		return DscFormat_NC;
	}

	return DscFormat_FT;
}

static int32_t GetDscFileFormat(int32_t index)
{
	if ((index != 0 && index != 1) || !dsc_ready[index])
		return DscFormat_FT;

	// DSC at index 0 is always the one defined in PV_DB
	if (index == 0)
	{
		auto& param = game::GetPVLoadParam()->data[game::GetPVLoadParam()->data[0].int8];
		if (void* entry = pv_db::FindPVEntry(param.pv_id); entry)
		{
			if (pv_db::PvDBDifficulty* diff = pv_db::FindDifficulty(entry, param.difficulty, param.edition); diff)
				return GetDscFormat(diff->script_format);
		}
	}

	if (dsc_data[index])
		return GetDscFormat(*dsc_data[index]);
	return DscFormat_FT;
}

static int32_t IsVanillaFormat(int32_t file_index)
{
	int32_t format = GetDscFileFormat(file_index);
	return format == DscFormat_AC || format == DscFormat_FT;
}

static void ConvertTargetParams(int32_t format, const int32_t* data, int32_t* output, float* length, bool* end, int32_t* target_hit_effect)
{
	if (format == DscFormat_F || format == DscFormat_NC || format == DscFormat_F2)
	{
		output[0] = data[0];
		*length   = data[1] / 100000.0f;
		*end      = data[2] == 1;
		output[1] = data[3]; // X
		output[2] = data[4]; // Y
		output[3] = data[5]; // Angle
		output[4] = data[7]; // Distance
		output[5] = data[8]; // Amplitude
		output[6] = data[6]; // Frequency
		if (format == DscFormat_F2)
			*target_hit_effect = data[11];
		return;
	}
	
	memcpy_s(output, sizeof(int32_t) * 7, data, sizeof(int32_t) * 7);
}

static void PushTargetExtraInfo(int32_t index, int32_t sub_index, float length, bool end, int32_t target_hit_effect)
{
	if (TargetStateEx* ex = GetTargetStateEx(index, sub_index); ex != nullptr)
	{
		ex->target_index = index;
		ex->sub_index = sub_index;
		ex->length = length;
		ex->long_end = end;
		ex->target_hit_effect_id = target_hit_effect;
		return;
	}

	TargetStateEx& ex = state.target_ex.emplace_back();
	ex.target_index = index;
	ex.sub_index = sub_index;
	ex.length = length;
	ex.long_end = end;
	ex.target_hit_effect_id = target_hit_effect;
	ex.ResetPlayState();
}

static bool ParseDsc(const int32_t* data, int32_t format, std::map<int32_t, DscFrame>& output, int32_t flags)
{
	int32_t branch_mode = 0;
	int32_t time = 0;
	int32_t flying_time = 0;
	int32_t target_index = 0;
	int32_t multi_index = 0;
	bool had_target = false;
	bool is_big_endian = false;

	auto readNext = [&]()
	{
		int32_t value = *data;
		if (is_big_endian)
			value = ((value & 0x000000FF) << 24) | ((value & 0x0000FF00) << 8) | ((value & 0x00FF0000) >> 8) | ((value & 0xFF000000) >> 24);
		data++;
		return value;
	};

	auto pushCurrentCmd = [&](int32_t opcode, std::vector<DscFrame::DscCommand>& list)
	{
		DscFrame::DscCommand& cmd = list.emplace_back();
		cmd.opcode = opcode;
		cmd.length = GetOpcodeLength(format, opcode);

		for (int32_t i = 0; i < cmd.length; i++)
			cmd.data[i] = readNext();
	};

	if (format == DscFormat_F2)
	{
		is_big_endian = (*(data + 3) & 0x08000000) != 0;
		// NOTE: Skip header
		data += *(data + 2) / sizeof(int32_t);
		// NOTE: Read information
		int32_t signature = readNext();
		branch_mode = readNext();
	}
	else
	{
		// NOTE: Skip signature
		if (*data >= 0x10000000)
			data++;
	}

	bool end = false;
	while (!end)
	{
		int32_t opcode = readNext();
		int32_t length = GetOpcodeLength(format, opcode);
		auto& list = output[time].branch_modes[branch_mode];

		switch (opcode)
		{
		case DscOp_End:
			end = true;
			break;
		case DscOp_Time:
			time = readNext();
			if (had_target)
			{
				target_index++;
				multi_index = 0;
				had_target = false;
			}

			break;
		case DscOp_ChangeField:
			if (flags & MergeFlags_IgnorePV)
			{
				data += length;
				break;
			}

			list.push_back(DscFrame::DscCommand(DscOp_ChangeField, { readNext() }));
			data += length - 1;
			break;
		case DscOp_ModeSelect:
		{
			if (flags & MergeFlags_IgnoreChart)
			{
				data += length;
				break;
			}

			int32_t diff = length > 1 ? readNext() : -1;
			int32_t mode = readNext();
			
			if (diff > -1)
				list.push_back(DscFrame::DscCommand(DscOp_ModeSelect, { diff, mode }));
			else
				list.push_back(DscFrame::DscCommand(DscOp_ModeSelect, { mode }));

			break;
		}
		case DscOp_BarTimeSet:
		{
			if (flags & MergeFlags_IgnoreChart)
			{
				data += length;
				break;
			}

			int32_t bpm = readNext();
			int32_t sig = readNext() + 1;
			flying_time = 60.0f / bpm * sig * 1000.0f;
			list.push_back(DscFrame::DscCommand(DscOp_TargetFlyingTime, { flying_time }));
			break;
		}
		case DscOp_TargetFlyingTime:
			if (flags & MergeFlags_IgnoreChart)
			{
				data += length;
				break;
			}

			flying_time = readNext();
			list.push_back(DscFrame::DscCommand(DscOp_TargetFlyingTime, { flying_time }));
			break;
		case DscOp_Target:
		{
			if (flags & MergeFlags_IgnoreChart)
			{
				data += length;
				break;
			}

			int32_t params[12] = {};
			for (int32_t i = 0; i < length; i++)
				params[i] = readNext();

			if (format == DscFormat_F || format == DscFormat_F2)
			{
				if (params[9] != flying_time)
				{
					list.push_back(DscFrame::DscCommand(DscOp_TargetFlyingTime, { params[9] }));
					flying_time = params[9];
				}
			}

			int32_t ft_params[7] = {};
			float length = -1.0f;
			bool is_end = false;
			int32_t target_hit_effect;
			ConvertTargetParams(format, params, ft_params, &length, &is_end, &target_hit_effect);
			PushTargetExtraInfo(target_index, multi_index, length, is_end, target_hit_effect);

			list.push_back(DscFrame::DscCommand(DscOp_Target, std::initializer_list<int32_t>(ft_params, ft_params + 7)));
			had_target = true;
			multi_index++;
			break;
		}
		case DscOp_PVBranchMode:
			branch_mode = readNext();
			break;
		case DscOp_TargetEffect:
		{
			// NOTE: Opcode 0x5B (91) is not unique to F 2nd; it corresponds to EDIT_MOTION_F in FT.
			if (format != DscFormat_F2)
			{
				pushCurrentCmd(opcode, list);
				break;
			}

			int32_t eff_id = readNext();
			// NOTE: The effect name string in DSC may not always be null-terminated so we need to make sure
			//       we aren't copying garbage data into our string.
			char effect_name[33] = { };
			memcpy_s(effect_name, 33, reinterpret_cast<const char*>(data + 2), 32);

			state.fail_target_effect_map[eff_id]    = branch_mode == 0 || branch_mode == 1 ? effect_name : "";
			state.success_target_effect_map[eff_id] = branch_mode == 0 || branch_mode == 2 ? effect_name : "";

			data += 10;
			break;
		}
		default:
			if (flags & MergeFlags_IgnorePV)
			{
				data += GetOpcodeLength(format, opcode);
				break;
			}

			pushCurrentCmd(opcode, list);
			break;
		}
	}

	return true;
}

static bool CompileDsc(PVGamePvData& pv_data, const std::map<int32_t, DscFrame>& data)
{
	int32_t pos = 1;
	int32_t branch_mode = -1;
	const int32_t max_size = 44999;

	auto write = [&](int32_t value)
	{
		if (pos >= max_size)
			return;
		pv_data.script_buffer[pos++] = value;
	};

	for (const auto& [time, frame] : data)
	{
		if (frame.branch_modes[0].empty() && frame.branch_modes[1].empty() && frame.branch_modes[2].empty())
			continue;

		write(DscOp_Time);
		write(time);

		for (int32_t i = 0; i < 3; i++)
		{
			if (frame.branch_modes[i].empty())
				continue;

			if (i != branch_mode)
			{
				write(DscOp_PVBranchMode);
				write(i);
				branch_mode = i;
			}

			for (const DscFrame::DscCommand& cmd : frame.branch_modes[i])
			{
				write(cmd.opcode);
				for (int32_t i = 0; i < cmd.length; i++)
					write(cmd.data[i]);
			}
		}
	}

	write(DscOp_End);
	return true;
}

static bool MergeDscs(PVGamePvData& pv_data)
{
	// NOTE: Return early if the base DSC is FT format and there's no secondary DSC
	if (dsc_data[0] && IsVanillaFormat(0) && !dsc_data[1])
		return true;

	std::map<int32_t, DscFrame> data;
	if (dsc_data[0])
		ParseDsc(dsc_data[0], GetDscFileFormat(0), data, dsc_data[1] ? MergeFlags_IgnoreChart : MergeFlags_None);

	if (dsc_data[1])
		ParseDsc(dsc_data[1], GetDscFileFormat(1), data, MergeFlags_IgnorePV);

	CompileDsc(pv_data, data);
	return true;
}

HOOK(bool, __fastcall, LoadDscCtrl, 0x14024E270, PVGamePvData* pv_data, prj::string& path, void* a3, bool a4)
{
	if (dsc_state == DscState_Idle)
	{
		dsc_state = RequestLoadDsc(path) ? DscState_Load : DscState_Missing;
		dsc_ready[0] = false;
		dsc_ready[1] = dsc_state == DscState_Missing;
		dsc_data[0] = nullptr;
		dsc_data[1] = nullptr;
	}

	if (!dsc_ready[0] && originalLoadDscCtrl(pv_data, path, a3, a4) )
	{
		dsc_ready[0] = true;
		dsc_data[0] = pv_data->script_buffer;
	}

	if (dsc_state == DscState_Load)
	{
		if (!FileCheckNotReady(&file_handler))
		{
			dsc_ready[1] = true;
			dsc_data[1] = reinterpret_cast<int32_t*>(FileGetData(&file_handler));
			dsc_state = DscState_Ready;
		}
	}

	if (dsc_ready[0] && dsc_ready[1])
	{
		MergeDscs(*pv_data);
		if (dsc_state == DscState_Ready)
			FileFree(&file_handler);

		dsc_state = DscState_Idle;
		return true;
	}

	return false;
}

void InstallDSCHooks()
{
	INSTALL_HOOK(LoadDscCtrl);
}