#include "types.h"

struct BSPLEAF;

bool shiftVis(byte* vis, int len, int offsetLeaf, int shift);

// decompress the given vis data into arrays of bits where each bit indicates if a leaf is visible or not
// iterationLeaves = number of leaves to decompress vis for
// visDataLeafCount = total leaves in the map (exluding the shared solid leaf 0)
// newNumLeaves = total leaves that will be in the map after merging is finished (again, excluding solid leaf 0)
void decompress_vis_lump(BSPLEAF* leafLump, byte* visLump, byte* output,
	int iterationLeaves, int visDataLeafCount, int newNumLeaves);

void DecompressVis(const byte* src, byte* const dest, const unsigned int dest_length, uint numLeaves);

int CompressVis(const byte* const src, const unsigned int src_length, byte* dest, unsigned int dest_length);

int CompressAll(BSPLEAF* leafs, byte* uncompressed, byte* output, int numLeaves, int iterLeaves, int bufferSize);

extern bool g_debug_shift;