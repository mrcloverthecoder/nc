#pragma once

#include <diva.h>

namespace utils
{
	inline diva::vec2 GetResolutionScaleWithSrcAspectRatio(const diva::vec2& src, const diva::vec2& dst)
	{
		float src_ratio = src.x / src.y;
		float dst_ratio = dst.x / dst.y;
		diva::vec2 res = {};
		if (dst_ratio > src_ratio)
			res = diva::vec2(src.x * dst.y / src.y, dst.y);
		else
			res = diva::vec2(dst.x, src.y * dst.x / src.x);
		return res / src;
	}
}