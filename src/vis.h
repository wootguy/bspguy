#include "util.h"

class BSPLEAF;

void DecompressVis(const byte* src, byte* const dest, const unsigned int dest_length, uint numLeaves);

int CompressVis(const byte* const src, const unsigned int src_length, byte* dest, unsigned int dest_length);

int CompressAll(BSPLEAF* leafs, byte* uncompressed, byte* output, int numLeaves, int bufferSize);