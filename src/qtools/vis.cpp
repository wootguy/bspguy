#include "vis.h"
#include "rad.h"
#include "util.h"
#include "globals.h"

bool g_debug_shift = false;

void printVisRow(byte* vis, int len, int offsetLeaf, int mask) {
	for (int i = 0; i < len; i++) {
		byte bits = vis[i];
		for (int b = 0; b < 8; b++) {
			int leafIdx = i * 8 + b;
			if (leafIdx == offsetLeaf) {
				print_color(PRINT_GREEN | PRINT_BRIGHT);
			}
			else {
				if (i * 8 < offsetLeaf && i * 8 + 8 > offsetLeaf && (1 << b) & mask) {
					print_color(PRINT_RED | PRINT_GREEN);
				} 
				else
					print_color(PRINT_RED | PRINT_GREEN | PRINT_BLUE);
			}
			logf("%d", (bits >> b) & 1);
		}
		logf(" ");
	}
	logf("\n");
}

bool shiftVis(byte* vis, int len, int offsetLeaf, int shift) {
	if (shift == 0)
		return false;

	byte bitsPerStep = 8;
	byte offsetBit = offsetLeaf % bitsPerStep;
	byte mask = 0; // part of the byte that shouldn't be shifted
	for (int i = 0; i < offsetBit; i++) {
		mask |= 1 << i;
	}

	int byteShifts = abs(shift) / 8;
	int bitShifts = abs(shift) % 8;

	// shift until offsetLeaf isn't sharing a byte with the leaves that come before it
	// then we can do a much faster memcpy on the section that needs to be shifted
	if ((offsetLeaf % 8) + bitShifts < 8 && byteShifts > 0) {
		byteShifts -= 1;
		bitShifts += 8;
	}

	if (shift < 0) {
		// TODO: memcpy for negative shifts
		bitShifts += byteShifts * 8;
		byteShifts = 0;
	}

	if (g_debug_shift) {
		logf("\nSHIFT\n");
	}

	int overflow = 0;
	for (int k = 0; k < bitShifts; k++) {

		if (g_debug_shift) {
			logf("%2d = ", k);
			printVisRow(vis, len, offsetLeaf, mask);
		}

		if (shift > 0) {
			bool carry = 0;
			for (int i = 0; i < len; i++) {
				uint oldCarry = carry;
				carry = (vis[i] & 0x80) != 0;

				if (offsetBit != 0 && i * bitsPerStep < offsetLeaf && i * bitsPerStep + bitsPerStep > offsetLeaf) {
					vis[i] = (vis[i] & mask) | ((vis[i] & ~mask) << 1);
				}
				else if (i >= offsetLeaf / bitsPerStep) {
					vis[i] = (vis[i] << 1) + oldCarry;
				}
				else {
					carry = 0;
				}
			}

			if (carry) {
				overflow++;
			}
		}
		else {
			bool carry = 0;
			for (int i = len-1; i >= 0; i--) {
				uint oldCarry = carry;
				carry = (vis[i] & 0x01) != 0;

				if (offsetBit != 0 && i * bitsPerStep < offsetLeaf && i * bitsPerStep + bitsPerStep > offsetLeaf) {
					vis[i] = (vis[i] & mask) | ((vis[i] >> 1) & ~mask) | (oldCarry << 7);
				}
				else if (i >= offsetLeaf / bitsPerStep) {
					vis[i] = (vis[i] >> 1) + (oldCarry << 7);
				}
				else {
					carry = 0;
				}
			}

			if (carry) {
				overflow++;
			}
		}

		if (g_debug_shift && k == bitShifts-1) {
			logf("%2d = ", k+1);
			printVisRow(vis, len, offsetLeaf, mask);
		}
	}
	if (overflow)
		logf("OVERFLOWED %d VIS LEAVES WHILE SHIFTING FROM LEAF %d\n", overflow, offsetLeaf);


	if (byteShifts > 0) {
		// TODO: detect overflows here too
		static byte temp[MAX_MAP_LEAVES / 8];

		if (shift > 0) {
			int startByte = (offsetLeaf + bitShifts) / 8;
			int moveSize = len - (startByte + byteShifts);

			memcpy(temp, (byte*)vis + startByte, moveSize);
			memset((byte*)vis + startByte, 0, byteShifts);
			memcpy((byte*)vis + startByte + byteShifts, temp, moveSize);
		}
		else {
			// TODO LOL
		}
		
	}

	return overflow;
}

// decompress this map's vis data into arrays of bits where each bit indicates if a leaf is visible or not
// iterationLeaves = number of leaves to decompress vis for
// visDataLeafCount = total leaves in this map (exluding the shared solid leaf 0)
// newNumLeaves = total leaves that will be in the map after merging is finished (again, excluding solid leaf 0)
void decompress_vis_lump(BSPLEAF* leafLump, byte* visLump, byte* output,
	int iterationLeaves, int visDataLeafCount, int newNumLeaves)
{
	byte* dest;
	uint oldVisRowSize = ((visDataLeafCount + 63) & ~63) >> 3;
	uint newVisRowSize = ((newNumLeaves + 63) & ~63) >> 3;
	int len = 0;

	// calculate which bits of an uncompressed visibility row are used/unused
	byte lastChunkMask = 0;
	int lastUsedIdx = (iterationLeaves / 8);
	for (byte k = 0; k < iterationLeaves % 8; k++) {
		lastChunkMask = lastChunkMask | (1 << k);
	}

	for (int i = 0; i < iterationLeaves; i++)
	{
		g_progress.tick();
		dest = output + i * newVisRowSize;
		if (lastUsedIdx >= 0)
		{
			if (leafLump[i + 1].nVisOffset < 0) {
				memset(dest, 255, lastUsedIdx);
				dest[lastUsedIdx] |= lastChunkMask;
				continue;
			}

			DecompressVis((const byte*)(visLump + leafLump[i + 1].nVisOffset), dest, oldVisRowSize, visDataLeafCount);

			// Leaf visibility row lengths are multiples of 64 leaves, so there are usually some unused bits at the end.
			// Maps sometimes set those unused bits randomly (e.g. leaf index 100 is marked visible, but there are only 90 leaves...)
			// Leaves for submodels also don't matter and can be set to 0 to save space during recompression.
			if (lastUsedIdx < newVisRowSize) {
				dest[lastUsedIdx] &= lastChunkMask;
				int sz = newVisRowSize - (lastUsedIdx + 1);
				int last = lastUsedIdx + 1 + sz;
				memset(dest + lastUsedIdx + 1, 0, sz);
			}
		}
		else {
			logf("Overflow decompressing VIS lump!");
			return;
		}
	}
}

//
// BEGIN COPIED QVIS CODE
//

void DecompressVis(const byte* src, byte* const dest, const unsigned int dest_length, uint numLeaves)
{
	unsigned int    current_length = 0;
	int             c;
	byte* out;
	int             row;

	row = (numLeaves + 7) >> 3; // same as the length used by VIS program in CompressVis
	// The wrong size will cause DecompressVis to spend extremely long time once the source pointer runs into the invalid area in g_dvisdata (for example, in BuildFaceLights, some faces could hang for a few seconds), and sometimes to crash.
	out = dest;

	do
	{
		//hlassume(src - g_dvisdata < g_visdatasize, assume_DECOMPRESSVIS_OVERFLOW);
		if (*src)
		{
			current_length++;
			hlassume(current_length <= dest_length, assume_DECOMPRESSVIS_OVERFLOW);

			*out = *src;
			out++;
			src++;
			continue;
		}

		//hlassume(&src[1] - g_dvisdata < g_visdatasize, assume_DECOMPRESSVIS_OVERFLOW);
		c = src[1];
		src += 2;
		while (c)
		{
			current_length++;
			hlassume(current_length <= dest_length, assume_DECOMPRESSVIS_OVERFLOW);

			*out = 0;
			out++;
			c--;

			if (out - dest >= row)
			{
				return;
			}
		}
	} while (out - dest < row);
}

int CompressVis(const byte* const src, const unsigned int src_length, byte* dest, unsigned int dest_length)
{
	unsigned int    j;
	byte* dest_p = dest;
	unsigned int    current_length = 0;

	for (j = 0; j < src_length; j++)
	{
		current_length++;
		hlassume(current_length <= dest_length, assume_COMPRESSVIS_OVERFLOW);

		*dest_p = src[j];
		dest_p++;

		if (src[j])
		{
			continue;
		}

		unsigned char   rep = 1;

		for (j++; j < src_length; j++)
		{
			if (src[j] || rep == 255)
			{
				break;
			}
			else
			{
				rep++;
			}
		}
		current_length++;
		hlassume(current_length <= dest_length, assume_COMPRESSVIS_OVERFLOW);

		*dest_p = rep;
		dest_p++;
		j--;
	}

	return dest_p - dest;
}

int CompressAll(BSPLEAF* leafs, byte* uncompressed, byte* output, int numLeaves, int iterLeaves, int bufferSize)
{
	int x = 0;
	byte* dest;
	byte* src;
	byte compressed[MAX_MAP_LEAVES / 8];
	uint g_bitbytes = ((numLeaves + 63) & ~63) >> 3;

	byte* vismap_p = output;

	int* sharedRows = new int[iterLeaves];
	for (int i = 0; i < iterLeaves; i++) {
		byte* src = uncompressed + i * g_bitbytes;

		sharedRows[i] = i;
		for (int k = 0; k < i; k++) {
			if (sharedRows[k] != k) {
				continue; // already compared in an earlier row
			}
			byte* previous = uncompressed + k * g_bitbytes;
			if (memcmp(src, previous, g_bitbytes) == 0) {
				sharedRows[i] = k;
				break;
			}
		}
		g_progress.tick();
	}

	for (int i = 0; i < iterLeaves; i++)
	{
		if (sharedRows[i] != i) {
			leafs[i + 1].nVisOffset = leafs[sharedRows[i] + 1].nVisOffset;
			continue;
		}

		memset(&compressed, 0, sizeof(compressed));

		src = uncompressed + i * g_bitbytes;

		// Compress all leafs into global compression buffer
		x = CompressVis(src, g_bitbytes, compressed, sizeof(compressed));

		dest = vismap_p;
		vismap_p += x;

		if (vismap_p > output + bufferSize)
		{
			logf("Vismap expansion overflow\n");
		}

		leafs[i + 1].nVisOffset = dest - output;            // leaf 0 is a common solid

		memcpy(dest, compressed, x);
	}

	delete[] sharedRows;

	return vismap_p - output;
}
