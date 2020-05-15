#include "vis.h"
#include "Bsp.h"

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
			printf("%d", (bits >> b) & 1);
		}
		printf(" ");
	}
	printf("\n");
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
		printf("\nSHIFT\n");
	}

	int overflow = 0;
	for (int k = 0; k < bitShifts; k++) {

		if (g_debug_shift) {
			printf("%2d = ", k);
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
			printf("%2d = ", k+1);
			printVisRow(vis, len, offsetLeaf, mask);
		}
	}
	if (overflow)
		printf("OVERFLOWED %d VIS LEAVES WHILE SHIFTING\n", overflow);


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
	int iterationLeaves, int visDataLeafCount, int newNumLeaves,
	int shiftOffsetBit, int shiftAmount)
{
	byte* dest;
	uint oldVisRowSize = ((visDataLeafCount + 63) & ~63) >> 3;
	uint newVisRowSize = ((newNumLeaves + 63) & ~63) >> 3;
	int len = 0;

	// calculate which bits of an uncompressed visibility row are used/unused
	uint64 lastChunkMask = 0;
	int lastChunkIdx = (oldVisRowSize / 8) - 1;
	int maxBitsInLastChunk = (visDataLeafCount % 64);
	for (uint64 k = 0; k < maxBitsInLastChunk; k++) {
		lastChunkMask = lastChunkMask | ((uint64)1 << k);
	}

	for (int i = 0; i < iterationLeaves; i++)
	{
		dest = output + i * newVisRowSize;

		if (leafLump[i + 1].nVisOffset < 0) {
			memset(dest, 255, visDataLeafCount / 8);
			for (int k = 0; k < visDataLeafCount % 8; k++) {
				dest[visDataLeafCount / 8] |= 1 << k;
			}
			shiftVis(dest, newVisRowSize, shiftOffsetBit, shiftAmount);
			continue;
		}

		DecompressVis((const byte*)(visLump + leafLump[i + 1].nVisOffset), dest, oldVisRowSize, visDataLeafCount);

		// Leaf visibility row lengths are multiples of 64 leaves, so there are usually some unused bits at the end.
		// Maps sometimes set those unused bits randomly (e.g. leaf index 100 is marked visible, but there are only 90 leaves...)
		// To prevent overflows when shifting the data later, the unused leaf bits will be forced to zero here.
		((uint64*)dest)[lastChunkIdx] &= lastChunkMask;

		if (shiftAmount) {
			shiftVis(dest, newVisRowSize, shiftOffsetBit, shiftAmount);
		}
	}
}

void decompress_vis_lump(BSPLEAF* leafLump, byte* visLump, byte* output, int leafCount, int iterLeafCount)
{
	byte* dest;
	uint visRowSize = ((leafCount + 63) & ~63) >> 3;
	int len = 0;

	// calculate which bits of an uncompressed visibility row are used/unused
	uint64 lastChunkMask = 0;
	int lastChunkIdx = (visRowSize / 8) - 1;
	int maxBitsInLastChunk = (iterLeafCount % 64);
	for (uint64 k = 0; k < maxBitsInLastChunk; k++) {
		lastChunkMask = lastChunkMask | ((uint64)1 << k);
	}

	for (int i = 0; i < iterLeafCount; i++)
	{
		dest = output + i * visRowSize;

		if (leafLump[i + 1].nVisOffset < 0) {
			memset(dest, 255, leafCount / 8);
			for (int k = 0; k < leafCount % 8; k++) {
				dest[leafCount / 8] |= 1 << k;
			}
			continue;
		}

		DecompressVis((const byte*)(visLump + leafLump[i + 1].nVisOffset), dest, visRowSize, leafCount);

		// Leaf visibility row lengths are multiples of 64 leaves, so there are usually some unused bits at the end.
		// Maps sometimes set those unused bits randomly (e.g. leaf index 100 is marked visible, but there are only 90 leaves...)
		// To prevent overflows when shifting the data later, the unused leaf bits will be forced to zero here.
		((uint64*)dest)[lastChunkIdx] &= lastChunkMask;
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
	int i, x = 0;
	byte* dest;
	byte* src;
	byte compressed[MAX_MAP_LEAVES / 8];
	uint g_bitbytes = ((numLeaves + 63) & ~63) >> 3;

	byte* vismap_p = output;

	for (i = 0; i < iterLeaves; i++)
	{
		memset(&compressed, 0, sizeof(compressed));

		src = uncompressed + i * g_bitbytes;

		// Compress all leafs into global compression buffer
		x = CompressVis(src, g_bitbytes, compressed, sizeof(compressed));

		dest = vismap_p;
		vismap_p += x;

		if (vismap_p > output + bufferSize)
		{
			printf("Vismap expansion overflow\n");
		}

		leafs[i + 1].nVisOffset = dest - output;            // leaf 0 is a common solid

		memcpy(dest, compressed, x);
	}
	return vismap_p - output;
}
