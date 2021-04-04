#include "util.h"

struct BSPLEAF;

bool shiftVis(BYTE* vis, int len, int offsetLeaf, int shift);

// decompress the given vis data into arrays of bits where each bit indicates if a leaf is visible or not
// iterationLeaves = number of leaves to decompress vis for
// visDataLeafCount = total leaves in the map (exluding the shared solid leaf 0)
// newNumLeaves = total leaves that will be in the map after merging is finished (again, excluding solid leaf 0)
void decompress_vis_lump(BSPLEAF* leafLump, BYTE* visLump, BYTE* output,
	int iterationLeaves, int visDataLeafCount, int newNumLeaves);

void DecompressVis(const BYTE* src, BYTE* const dest, const unsigned int dest_length, uint numLeaves);

int CompressVis(const BYTE* const src, const unsigned int src_length, BYTE* dest, unsigned int dest_length);

int CompressAll(BSPLEAF* leafs, BYTE* uncompressed, BYTE* output, int numLeaves, int iterLeaves, int bufferSize);

extern bool g_debug_shift;