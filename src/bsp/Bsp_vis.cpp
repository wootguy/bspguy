#include "Bsp.h"
#include "util.h"
#include "vis.h"
#include "LeafNavMesh.h"

bool Bsp::is_leaf_visible(int ileaf, vec3 pos) {
	int ipvsLeaf = get_leaf(pos, 0);
	BSPLEAF& pvsLeaf = leaves[ipvsLeaf];

	int p = pvsLeaf.nVisOffset; // pvs offset
	byte* pvs = lumps[LUMP_VISIBILITY];

	bool isVisible = false;
	int numVisible = 0;

	if (!pvs) {
		return true;
	}

	//logf("leaf %d can see:", ipvsLeaf);

	for (int lf = 1; lf < leafCount && p < header.lump[LUMP_VISIBILITY].nLength; p++)
	{
		if (pvs[p] == 0) { // prepare to skip leafs
			if (p + 1 >= header.lump[LUMP_VISIBILITY].nLength) {
				logf("Failed to read VIS data\n");
				break;
			}
			lf += 8 * pvs[++p]; // next byte holds number of leafs to skip
		}
		else
		{
			for (byte bit = 1; bit != 0; bit *= 2, lf++)
			{
				if ((pvs[p] & bit) && lf < leafCount) // leaf is flagged as visible
				{
					numVisible++;
					//logf(" %d", lf);
					if (lf == ileaf) {
						isVisible = true;
					}
				}
			}
		}
	}

	//logf("\n");

	return isVisible;
}

bool Bsp::is_face_visible(int faceIdx, vec3 pos, vec3 angles) {
	BSPFACE& face = faces[faceIdx];
	BSPPLANE& plane = planes[face.iPlane];
	vec3 normal = plane.vNormal;

	// TODO: is it in the frustrum? Is it part of an entity model? If so is the entity linked in the PVS?
	// is it facing the camera? Is it a special face?

	return true;
}

vector<int> Bsp::get_pvs(int ileaf) {
	BSPLEAF& pvsLeaf = leaves[ileaf];
	vector<int> pvsLeaves;

	int p = pvsLeaf.nVisOffset; // pvs offset
	byte* pvs = lumps[LUMP_VISIBILITY];

	bool isVisible = false;
	int numVisible = 0;

	if (!pvs) {
		return pvsLeaves;
	}

	for (int lf = 1; lf < leafCount && p < header.lump[LUMP_VISIBILITY].nLength; p++)
	{
		if (pvs[p] == 0) { // prepare to skip leafs
			if (p + 1 >= header.lump[LUMP_VISIBILITY].nLength) {
				logf("Failed to read VIS data\n");
				break;
			}
			lf += 8 * pvs[++p]; // next byte holds number of leafs to skip
		}
		else
		{
			for (byte bit = 1; bit != 0; bit *= 2, lf++)
			{
				if ((pvs[p] & bit) && lf < leafCount && lf <= models[0].nVisLeafs) // leaf is flagged as visible
				{
					pvsLeaves.push_back(lf);
				}
			}
		}
	}

	return pvsLeaves;
}

void Bsp::apply_pvs(vector<int>& targetLeaves, vector<int>& pvsLeaves, int applyMode) {
	// merge PVS
	int visLeafCount = leafCount - 1;
	uint visRowSize = ((visLeafCount + 63) & ~63) >> 3;

	int decompressedVisSize = visLeafCount * visRowSize;
	byte* decompressedVis = new byte[decompressedVisSize];
	memset(decompressedVis, 0, decompressedVisSize);
	decompress_vis_lump(leaves, lumps[LUMP_VISIBILITY], visDataLength, decompressedVis, visLeafCount);

	// create new PVS row
	byte* newPvs = new byte[visRowSize];
	memset(newPvs, 0, visRowSize);

	for (int leafIdx : pvsLeaves) {
		if (leafIdx - 1 >= leafCount) {
			logf("Invalid leaf index %d for pvs application\n", leafIdx);
			continue;
		}
		int pvsLeafIdx = leafIdx - 1;
		int leafByte = pvsLeafIdx / 8;
		int leafBit = 1 << (pvsLeafIdx % 8);
		newPvs[leafByte] |= leafBit;
	}

	// apply new PVS row to all target leaves
	for (int leafIdx : targetLeaves) {
		if (leafIdx - 1 >= visLeafCount) {
			logf("Invalid leaf index %d in copy memory\n", leafIdx);
			continue;
		}
		byte* visRow = decompressedVis + (leafIdx - 1) * visRowSize;

		if (applyMode == 0) { // replace
			memcpy(visRow, newPvs, visRowSize);
		}
		else if (applyMode == 1) { // add
			for (int k = 1; k < visRowSize; k++) {
				visRow[k] |= newPvs[k];
			}
		}
		else if (applyMode == -1) { // subtract
			for (int k = 1; k < visRowSize; k++) {
				visRow[k] &= ~newPvs[k];
			}
		}
	}

	byte* compressedVis = new byte[decompressedVisSize]; // assuming compressed will reduce size
	memset(compressedVis, 0, decompressedVisSize);
	int newVisLen = CompressAll(leaves, decompressedVis, compressedVis, visLeafCount, decompressedVisSize);

	byte* compressedVisResized = new byte[newVisLen];
	memcpy(compressedVisResized, compressedVis, newVisLen);

	replace_lump(LUMP_VISIBILITY, compressedVisResized, newVisLen);

	delete[] decompressedVis;
	delete[] compressedVis;
	delete[] newPvs;
}

byte* Bsp::decompress_vis() {
	int visLeafCount = leafCount - 1;
	uint visRowSize = ((visLeafCount + 63) & ~63) >> 3;

	int decompressedVisSize = visLeafCount * visRowSize;
	byte* decompressedVis = new byte[decompressedVisSize];
	memset(decompressedVis, 0, decompressedVisSize);
	decompress_vis_lump(leaves, lumps[LUMP_VISIBILITY], visDataLength, decompressedVis, visLeafCount);

	return decompressedVis;
}

void Bsp::compress_vis(byte* decompressedVis) {
	int visLeafCount = leafCount - 1;
	uint visRowSize = ((visLeafCount + 63) & ~63) >> 3;
	int decompressedVisSize = visLeafCount * visRowSize;

	byte* compressedVis = new byte[decompressedVisSize]; // assuming compressed will reduce size
	memset(compressedVis, 0, decompressedVisSize);
	int newVisLen = CompressAll(leaves, decompressedVis, compressedVis, leafCount - 1, decompressedVisSize);

	byte* compressedVisResized = new byte[newVisLen];
	memcpy(compressedVisResized, compressedVis, newVisLen);

	replace_lump(LUMP_VISIBILITY, compressedVisResized, newVisLen);

	delete[] decompressedVis;
	delete[] compressedVis;
}

void Bsp::write_portal_file_leaf_count(int iNode, FILE* fout) {
	if (iNode >= 0) {
		BSPNODE& node = nodes[iNode];
		write_portal_file_leaf_count(node.iChildren[0], fout);
		write_portal_file_leaf_count(node.iChildren[1], fout);
	}
	else {
		BSPLEAF& leaf = leaves[~iNode];

		if (leaf.nContents != CONTENTS_SOLID) {
			// TODO: hlbsp has logic for func_detail that is missing here, but that data is likely lost
			int count = 1;
			fprintf(fout, "%i\n", count);
		}
	}
}

void Bsp::get_portal_file_leaf_numbers(int iNode, unordered_map<uint32_t, uint32_t>& leafMap, int& leafCount) {
	if (iNode >= 0) {
		BSPNODE& node = nodes[iNode];
		get_portal_file_leaf_numbers(node.iChildren[0], leafMap, leafCount);
		get_portal_file_leaf_numbers(node.iChildren[1], leafMap, leafCount);
		return;
	}

	uint32_t leafIdx = ~iNode;
	BSPLEAF& leaf = leaves[leafIdx];

	if (leaf.nContents != CONTENTS_SOLID)
		leafMap[leafIdx] = leafCount++;
}

int Bsp::write_portal_file_portals(int iNode, LeafNavMesh* mesh, unordered_set<uint32_t>& visited,
	unordered_map<uint32_t, uint32_t>& leafMap, FILE* fout)
{
	if (iNode >= 0) {
		BSPNODE& node = nodes[iNode];
		int count = 0;
		count += write_portal_file_portals(node.iChildren[0], mesh, visited, leafMap, fout);
		count += write_portal_file_portals(node.iChildren[1], mesh, visited, leafMap, fout);
		return count;
	}

	BSPLEAF& leaf = leaves[~iNode];

	if (leaf.nContents == CONTENTS_SOLID) {
		return 0;
	}

	uint32_t leafIdx = ~iNode;
	uint32_t nodeIdx = mesh->leafMap[leafIdx];

	if (nodeIdx == NAV_INVALID_IDX) {
		if (fout)
			logf("Leaf %d has no mesh\n", (int)leafIdx);
		return 0;
	}

	LeafNode& node = mesh->nodes[nodeIdx];
	uint32_t src = leafIdx;
	int count = 0;

	for (LeafLink& link : node.links) {
		if (link.node == NAV_INVALID_IDX)
			continue;

		uint32_t dst = mesh->nodes[link.node].leafIdx;

		if (leaves[dst].nContents != leaf.nContents)
			continue;

		uint32_t ilink = ((uint32_t)src << 16) | ((uint32_t)dst);
		uint32_t ibackLink = ((uint32_t)dst << 16) | ((uint32_t)src);

		if (visited.count(ilink))
			continue;

		visited.insert(ilink);
		visited.insert(ibackLink);

		vector<vec3>& verts = link.linkArea.verts;

		if (fout) {
			// convert from leaf lump index to portal file leaf index
			int srcIdx = leafMap[src];
			int dstIdx = leafMap[dst];

			fprintf(fout, "%u %i %i ", (int)verts.size(), srcIdx, dstIdx);

			for (const vec3& v : verts) {
				fprintf(fout, "(%f %f %f) ", v.x, v.y, v.z);
			}

			fprintf(fout, "\n");
		}

		count++;
	}

	return count;
}

void Bsp::write_portal_file(LeafNavMesh* mesh, const char* fname) {
	FILE* pf = fopen(fname, "w");

	struct LeafPortal {
		vector<vec3> verts;
		int srcLeaf;
		int dstLeaf;
	};

	int leafIndexCount = 0;
	int headNode = models[0].iHeadnodes[0];
	unordered_map<uint32_t, uint32_t> leafIndexMap;
	unordered_set<uint32_t> visitedLinks;

	int portalCount = write_portal_file_portals(headNode, mesh, visitedLinks, leafIndexMap, NULL);

	visitedLinks.clear();

	get_portal_file_leaf_numbers(headNode, leafIndexMap, leafIndexCount);

	fprintf(pf, "%d\n", leafIndexCount);
	fprintf(pf, "%d\n", portalCount);
	write_portal_file_leaf_count(headNode, pf);
	write_portal_file_portals(headNode, mesh, visitedLinks, leafIndexMap, pf);
	fclose(pf);

	logf("Wrote %s\n", fname);
}

bool Bsp::validate_vis_data() {
	// exclude solid leaf
	int visLeafCount = leafCount - 1;

	uint visRowSize = ((visLeafCount + 63) & ~63) >> 3;

	int decompressedVisSize = visLeafCount * visRowSize;
	byte* decompressedVis = new byte[decompressedVisSize];
	memset(decompressedVis, 0, decompressedVisSize);
	bool ret = decompress_vis_lump(leaves, lumps[LUMP_VISIBILITY], visDataLength, decompressedVis, visLeafCount);
	delete[] decompressedVis;

	return ret;
}

int Bsp::count_visible_polys(vec3 pos, vec3 angles) {
	int ipvsLeaf = get_leaf(pos, 0);
	BSPLEAF& pvsLeaf = leaves[ipvsLeaf];

	int p = pvsLeaf.nVisOffset; // pvs offset
	byte* pvs = lumps[LUMP_VISIBILITY];

	bool isVisible = false;
	int numVisible = 0;

	if (ipvsLeaf == 0) {
		return faceCount;
	}

	memset(pvsFaces, 0, pvsFaceCount * sizeof(bool));
	int renderFaceCount = 0;

	for (int lf = 1; lf < leafCount; p++)
	{
		if (pvs[p] == 0) // prepare to skip leafs
			lf += 8 * pvs[++p]; // next byte holds number of leafs to skip
		else
		{
			for (byte bit = 1; bit != 0; bit *= 2, lf++)
			{
				if ((pvs[p] & bit) && lf < leafCount) // leaf is flagged as visible
				{
					numVisible++;
					BSPLEAF& leaf = leaves[lf];

					for (int i = 0; i < leaf.nMarkSurfaces; i++) {
						int faceIdx = marksurfs[leaf.iFirstMarkSurface + i];
						if (!pvsFaces[faceIdx]) {
							pvsFaces[faceIdx] = true;
							if (is_face_visible(faceIdx, pos, angles))
								renderFaceCount++;
						}
					}
				}
			}
		}
	}

	return renderFaceCount;
}

int Bsp::remove_unused_visdata(STRUCTREMAP* remap, BSPLEAF* oldLeaves, int oldLeafCount, int oldWorldspawnLeafCount) {
	int oldVisLength = visDataLength;

	// exclude solid leaf
	int oldVisLeafCount = oldLeafCount - 1;
	int newVisLeafCount = (header.lump[LUMP_LEAVES].nLength / sizeof(BSPLEAF)) - 1;

	if (oldVisLeafCount == newVisLeafCount) {
		return 0; // VIS data needs updating only when leaves are added/removed
	}

	uint oldVisRowSize = ((oldVisLeafCount + 63) & ~63) >> 3;
	uint newVisRowSize = ((newVisLeafCount + 63) & ~63) >> 3;

	int oldDecompressedVisSize = oldLeafCount * oldVisRowSize;
	byte* oldDecompressedVis = new byte[oldDecompressedVisSize];
	memset(oldDecompressedVis, 0, oldDecompressedVisSize);
	decompress_vis_lump(oldLeaves, lumps[LUMP_VISIBILITY], visDataLength, oldDecompressedVis, oldVisLeafCount);

	int newDecompressedVisSize = newVisLeafCount * newVisRowSize;
	byte* newDecompressedVis = new byte[oldDecompressedVisSize];
	memset(newDecompressedVis, 0, newDecompressedVisSize);

	if (newVisRowSize > oldVisRowSize) {
		errorf("ERROR: New vis row size larger than old size. VIS will likely be broken\n");
	}

	int* oldLeafs = new int[newVisLeafCount + 1];

	for (int i = 1; i <= newVisLeafCount; i++) {
		int oldLeafIdx = 0;

		for (int k = 1; k <= oldVisLeafCount; k++) {
			if (remap->leaves[k] == i) {
				oldLeafs[i] = k;
				break;
			}
		}
	}

	for (int i = 1; i <= newVisLeafCount; i++) {
		byte* oldVisRow = oldDecompressedVis + (oldLeafs[i] - 1) * oldVisRowSize;
		byte* newVisRow = newDecompressedVis + (i - 1) * newVisRowSize;

		for (int k = 1; k <= newVisLeafCount; k++) {
			int oldLeafIdx = oldLeafs[k] - 1;
			int oldByteOffset = oldLeafIdx / 8;
			int oldBitOffset = 1 << (oldLeafIdx % 8);

			if (oldVisRow[oldByteOffset] & oldBitOffset) {
				int newLeafIdx = k - 1;
				int newByteOffset = newLeafIdx / 8;
				int newBitOffset = 1 << (newLeafIdx % 8);
				newVisRow[newByteOffset] |= newBitOffset;
			}
		}
	}

	delete[] oldLeafs;
	delete[] oldDecompressedVis;

	byte* compressedVis = new byte[newDecompressedVisSize]; // assuming compressed will reduce size
	memset(compressedVis, 0, newDecompressedVisSize);
	int newVisLen = CompressAll(leaves, newDecompressedVis, compressedVis, newVisLeafCount, newDecompressedVisSize);

	byte* compressedVisResized = new byte[newVisLen];
	memcpy(compressedVisResized, compressedVis, newVisLen);

	replace_lump(LUMP_VISIBILITY, compressedVisResized, newVisLen);

	delete[] compressedVis;

	return oldVisLength - newVisLen;
}
