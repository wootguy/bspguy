#include "resample.h"
#include <cstring>
#include <cmath>
#include <algorithm>

using namespace std;

#include <base_resample.h>

bool resample24(uint8_t* src, uint32_t src_width, uint32_t src_height,
	uint8_t* dst, uint32_t dst_width, uint32_t dst_height, int mode)
{
	return base::ResampleImage24(src, src_width, src_height, dst, dst_width, dst_height, (base::KernelType)mode);
}
