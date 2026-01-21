#include "colors.h"
#include <vector>
#include <unordered_map>

std::vector<COLOR3> median_cut_quantize(COLOR3* pixels, int pixel_count, int k=256);

// find the quake 1 color closest to the given color (index in global palette
int closest_q1_color(COLOR3& c);

// replace a half-life palette with colors from the global quake 1 palette
void quantize_to_q1_pal(COLOR3* pal);