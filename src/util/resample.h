#pragma once
#include <stdint.h>

// separate header to avoid conflicts with others

// maps directly to resample lib modes
enum resample_modes {
	KernelTypeUnknown,
	KernelTypeNearest,
	KernelTypeAverage,
	KernelTypeBilinear,
	KernelTypeBicubic,
	KernelTypeMitchell,
	KernelTypeCardinal,
	KernelTypeBSpline,
	KernelTypeLanczos,
	KernelTypeLanczos2,
	KernelTypeLanczos3,
	KernelTypeLanczos4,
	KernelTypeLanczos5,
	KernelTypeCatmull,
	KernelTypeGaussian,
};

bool resample24(uint8_t* src, uint32_t src_width, uint32_t src_height,
	uint8_t* dst, uint32_t dst_width, uint32_t dst_height, int mode);