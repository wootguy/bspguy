#include "Bsp.h"
#include "util.h"
#include <algorithm>
#include <string.h>
#include <sstream>
#include <fstream>
#include <set>
#include <iomanip>
#include "lodepng.h"
#include "rad.h"
#include "vis.h"

Bsp::Bsp() {
	lumps = new byte * [HEADER_LUMPS];

	header.nVersion = 30;

	for (int i = 0; i < HEADER_LUMPS; i++) {
		header.lump[i].nLength = 0;
		header.lump[i].nOffset = 0;
		lumps[i] = NULL;
	}

	name = "merged";
}

Bsp::Bsp(std::string fpath)
{
	this->path = fpath;
	this->name = stripExt(basename(fpath));

	bool exists = true;
	if (!fileExists(fpath)) {
		cout << "ERROR: " + fpath + " not found\n";
		return;
	}

	if (!load_lumps(fpath)) {
		cout << fpath + " is not a valid BSP file\n";
		return;
	}

	load_ents();
}

Bsp::~Bsp()
{	 
	for (int i = 0; i < HEADER_LUMPS; i++)
		if (lumps[i])
			delete [] lumps[i];
	delete [] lumps;

	for (int i = 0; i < ents.size(); i++)
		delete ents[i];
}

bool Bsp::merge(Bsp& other) {	
	last_progress = std::chrono::system_clock::now();

	BSPPLANE separationPlane = separate(other);
	if (separationPlane.nType == -1) {
		printf("No separating axis found. The maps overlap and can't be merged.\n");
		return false;
	}

	thisWorldLeafCount = ((BSPMODEL*)lumps[LUMP_MODELS])->nVisLeafs; // excludes solid leaf 0
	otherWorldLeafCount = ((BSPMODEL*)other.lumps[LUMP_MODELS])->nVisLeafs; // excluding solid leaf 0

	texRemap.clear();
	texInfoRemap.clear();
	planeRemap.clear();
	leavesRemap.clear();
	modelLeafRemap.clear();

	bool shouldMerge[HEADER_LUMPS] = { false };

	for (int i = 0; i < HEADER_LUMPS; i++) {

		if (i == LUMP_VISIBILITY || i == LUMP_LIGHTING) {
			continue; // always merge
		}

		if (!lumps[i] && !other.lumps[i]) {
			//cout << "Skipping " << g_lump_names[i] << " lump (missing from both maps)\n";
		}
		else if (!lumps[i]) {
			cout << "Replacing " << g_lump_names[i] << " lump\n";
			header.lump[i].nLength = other.header.lump[i].nLength;
			lumps[i] = new byte[other.header.lump[i].nLength];
			memcpy(lumps[i], other.lumps[i], other.header.lump[i].nLength);

			// process the lump here (TODO: faster to just copy wtv needs copying)
			switch (i) {
				case LUMP_ENTITIES:
					load_ents(); break;
			}
		}
		else if (!other.lumps[i]) {
			cout << "Keeping " << g_lump_names[i] << " lump\n";
		}
		else {
			//cout << "Merging " << g_lump_names[i] << " lump\n";

			shouldMerge[i] = true;
		}
	}

	// base structures (they don't reference any other structures)
	if (shouldMerge[LUMP_ENTITIES])
		merge_ents(other);
	if (shouldMerge[LUMP_PLANES])
		merge_planes(other);
	if (shouldMerge[LUMP_TEXTURES])
		merge_textures(other);
	if (shouldMerge[LUMP_VERTICES])
		merge_vertices(other);

	if (shouldMerge[LUMP_EDGES])
		merge_edges(other); // references verts

	if (shouldMerge[LUMP_SURFEDGES])
		merge_surfedges(other); // references edges

	if (shouldMerge[LUMP_TEXINFO])
		merge_texinfo(other); // references textures

	if (shouldMerge[LUMP_FACES])
		merge_faces(other); // references planes, surfedges, and texinfo

	if (shouldMerge[LUMP_MARKSURFACES])
		merge_marksurfs(other); // references faces

	if (shouldMerge[LUMP_LEAVES])
		merge_leaves(other); // references vis data, and marksurfs

	if (shouldMerge[LUMP_NODES]) {
		create_merge_headnodes(other, separationPlane);
		merge_nodes(other);
		merge_clipnodes(other);
	}

	if (shouldMerge[LUMP_MODELS])
		merge_models(other);

	merge_lighting(other);

	// doing this last because it takes way longer than anything else, and limit overflows should fail the
	// merge as soon as possible.
	merge_vis(other);

	printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b");
	printf("                               ");
	printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b");

	return true;
}

void Bsp::merge_ents(Bsp& other)
{
	progress_title = "entities";
	progress = 0;
	progress_total = ents.size() + other.ents.size();

	int oldEntCount = ents.size();

	// update model indexes since this map's models will be appended after the other map's models
	int otherModelCount = (other.header.lump[LUMP_MODELS].nLength / sizeof(BSPMODEL)) - 1;
	for (int i = 0; i < ents.size(); i++) {
		if (!ents[i]->hasKey("model") || ents[i]->keyvalues["model"][0] != '*') {
			continue;
		}
		string modelIdxStr = ents[i]->keyvalues["model"].substr(1);

		if (!isNumeric(modelIdxStr)) {
			continue;
		}

		int newModelIdx = atoi(modelIdxStr.c_str()) + otherModelCount;
		ents[i]->keyvalues["model"] = "*" + to_string(newModelIdx);

		print_merge_progress();
	}

	for (int i = 0; i < other.ents.size(); i++) {
		if (other.ents[i]->keyvalues["classname"] == "worldspawn") {
			Entity* otherWorldspawn = other.ents[i];

			vector<string> otherWads = splitString(otherWorldspawn->keyvalues["wad"], ";");

			// strip paths from wad names
			for (int j = 0; j < otherWads.size(); j++) {
				otherWads[j] = basename(otherWads[j]);
			}

			Entity* worldspawn = NULL;
			for (int k = 0; k < ents.size(); k++) {
				if (ents[k]->keyvalues["classname"] == "worldspawn") {
					worldspawn = ents[k];
					break;
				}
			}

			// merge wad list
			vector<string> thisWads = splitString(worldspawn->keyvalues["wad"], ";");

			// strip paths from wad names
			for (int j = 0; j < thisWads.size(); j++) {
				thisWads[j] = basename(thisWads[j]);
			}

			// add unique wads to this map
			for (int j = 0; j < otherWads.size(); j++) {
				if (std::find(thisWads.begin(), thisWads.end(), otherWads[j]) == thisWads.end()) {
					thisWads.push_back(otherWads[j]);
				}
			}

			worldspawn->keyvalues["wad"] = "";
			for (int j = 0; j < thisWads.size(); j++) {
				worldspawn->keyvalues["wad"] += thisWads[j] + ";";
			}

			// include prefixed version of the other maps keyvalues
			for (auto it = otherWorldspawn->keyvalues.begin(); it != otherWorldspawn->keyvalues.end(); it++) {
				if (it->first == "classname" || it->first == "wad") {
					continue;
				}
				// TODO: unknown keyvalues crash the game? Try something else.
				//worldspawn->addKeyvalue(Keyvalue(other.name + "_" + it->first, it->second));
			}
		}
		else {
			Entity* copy = new Entity();
			copy->keyvalues = other.ents[i]->keyvalues;
			copy->keyOrder = other.ents[i]->keyOrder;
			ents.push_back(copy);
		}

		print_merge_progress();
	}

	update_ent_lump();

	//cout << oldEntCount << " -> " << ents.size() << endl;
}

void Bsp::merge_planes(Bsp& other) {
	BSPPLANE* thisPlanes = (BSPPLANE*)lumps[LUMP_PLANES];
	BSPPLANE* otherPlanes = (BSPPLANE*)other.lumps[LUMP_PLANES];
	int numThisPlanes = header.lump[LUMP_PLANES].nLength / sizeof(BSPPLANE);
	int numOtherPlanes = other.header.lump[LUMP_PLANES].nLength / sizeof(BSPPLANE);
	
	progress_title = "planes";
	progress = 0;
	progress_total = numThisPlanes + numOtherPlanes;

	vector<BSPPLANE> mergedPlanes;
	mergedPlanes.reserve(numThisPlanes + numOtherPlanes);

	for (int i = 0; i < numThisPlanes; i++) {
		mergedPlanes.push_back(thisPlanes[i]);
		print_merge_progress();
	}
	for (int i = 0; i < numOtherPlanes; i++) {
		bool isUnique = true;
		for (int k = 0; k < numThisPlanes; k++) {
			if (memcmp(&otherPlanes[i], &thisPlanes[k], sizeof(BSPPLANE)) == 0) {
				isUnique = false;
				planeRemap.push_back(k);
				break;
			}
		}
		if (isUnique) {
			planeRemap.push_back(mergedPlanes.size());
			mergedPlanes.push_back(otherPlanes[i]);
		}

		print_merge_progress();
	}

	int newLen = mergedPlanes.size() * sizeof(BSPPLANE);
	int duplicates = (numThisPlanes + numOtherPlanes) - mergedPlanes.size();

	delete [] this->lumps[LUMP_PLANES];
	this->lumps[LUMP_PLANES] = new byte[newLen];
	memcpy(this->lumps[LUMP_PLANES], &mergedPlanes[0], newLen);
	header.lump[LUMP_PLANES].nLength = newLen;

	// add 1 for the separation plane coming later
	//cout << (numThisPlanes + 1) << " -> " << mergedPlanes.size();
	//if (duplicates) cout << " (" << duplicates << " deduped)";
	//cout << endl;
}

int getMipTexDataSize(int width, int height) {
	int sz = 256*3 + 4; // pallette + padding

	for (int i = 0; i < MIPLEVELS; i++) {
		sz += (width >> i) * (height >> i);
	}

	return sz;
}

void Bsp::merge_textures(Bsp& other) {
	int32_t thisTexCount =  *((int32_t*)(lumps[LUMP_TEXTURES]));
	int32_t otherTexCount = *((int32_t*)(other.lumps[LUMP_TEXTURES]));
	byte* thisTex = lumps[LUMP_TEXTURES];
	byte* otherTex = other.lumps[LUMP_TEXTURES];

	uint32_t newTexCount = 0;

	// temporary buffer for holding miptex + embedded textures (too big but doesn't matter)
	uint maxMipTexDataSize = header.lump[LUMP_TEXTURES].nLength + other.header.lump[LUMP_TEXTURES].nLength;
	byte* newMipTexData = new byte[maxMipTexDataSize];

	byte* mipTexWritePtr = newMipTexData;

	// offsets relative to the start of the mipmap data, not the lump
	uint32_t* mipTexOffsets = new uint32_t[thisTexCount + otherTexCount];

	progress_title = "planes";
	progress = 0;
	progress_total = thisTexCount + otherTexCount;

	uint thisMergeSz = (thisTexCount + 1) * 4;
	for (int i = 0; i < thisTexCount; i++) {
		int32_t offset = ((int32_t*)thisTex)[i + 1];
		BSPMIPTEX* tex = (BSPMIPTEX*)(thisTex + offset);

		int sz = sizeof(BSPMIPTEX);
		if (tex->nOffsets[0] != 0) {
			sz += getMipTexDataSize(tex->nWidth, tex->nHeight);
		}
		//memset(tex->nOffsets, 0, sizeof(uint32) * 4);
		
		mipTexOffsets[newTexCount] = (mipTexWritePtr - newMipTexData);
		memcpy(mipTexWritePtr, tex, sz);
		mipTexWritePtr += sz;
		newTexCount++;
		thisMergeSz += sz;

		print_merge_progress();
	}

	uint otherMergeSz = (otherTexCount+1)*4;
	for (int i = 0; i < otherTexCount; i++) {
		int32_t offset = ((int32_t*)otherTex)[i + 1];
		BSPMIPTEX* tex = (BSPMIPTEX*)(otherTex + offset);

		int sz = sizeof(BSPMIPTEX);
		if (tex->nOffsets[0] != 0) {
			sz += getMipTexDataSize(tex->nWidth, tex->nHeight);
		}

		bool isUnique = true;
		for (int k = 0; k < thisTexCount; k++) {
			BSPMIPTEX* thisTex = (BSPMIPTEX*)(newMipTexData + mipTexOffsets[k]);
			if (memcmp(tex, thisTex, sz) == 0 && false) {
				isUnique = false;
				texRemap.push_back(k);
				break;
			}
		}
		
		if (isUnique) {
			mipTexOffsets[newTexCount] = (mipTexWritePtr - newMipTexData);
			if (mipTexOffsets[newTexCount] > maxMipTexDataSize) {
				printf("ZOMG OVERFLOW\n");
			}

			texRemap.push_back(newTexCount);
			memcpy(mipTexWritePtr, tex, sz);
			mipTexWritePtr += sz;
			newTexCount++;
			otherMergeSz += sz;
		}

		print_merge_progress();
	}

	int duplicates = newTexCount - (thisTexCount + otherTexCount);

	if (duplicates) {
		cout << "Removed " << duplicates << " duplicate textures\n";
		cout << "ZOMG NOT READY FOR THIS\n";
		// TODO: update plane references in other BSP when duplicates are removed
	}

	uint texHeaderSize = (newTexCount + 1) * sizeof(int32_t);
	uint newLen = (mipTexWritePtr - newMipTexData) + texHeaderSize;
	delete[] this->lumps[LUMP_TEXTURES];
	this->lumps[LUMP_TEXTURES] = new byte[newLen];

	// write texture lump header
	uint32_t* texHeader = (uint32_t*)(this->lumps[LUMP_TEXTURES]);
	texHeader[0] = newTexCount;
	for (int i = 0; i < newTexCount; i++) {
		texHeader[i+1] = mipTexOffsets[i] + texHeaderSize;
	}

	memcpy(this->lumps[LUMP_TEXTURES] + texHeaderSize, newMipTexData, mipTexWritePtr - newMipTexData);
	header.lump[LUMP_TEXTURES].nLength = newLen;

	//cout << thisTexCount << " -> " << newTexCount << endl;
}

void Bsp::merge_vertices(Bsp& other) {
	vec3* thisVerts = (vec3*)lumps[LUMP_VERTICES];
	vec3* otherVerts = (vec3*)other.lumps[LUMP_VERTICES];
	thisVertCount = header.lump[LUMP_VERTICES].nLength / sizeof(vec3);
	int otherVertCount = other.header.lump[LUMP_VERTICES].nLength / sizeof(vec3);
	int totalVertCount = thisVertCount + otherVertCount;

	progress_title = "verticies";
	progress = 0;
	progress_total = 3;
	print_merge_progress();

	vec3* newVerts = new vec3[totalVertCount];
	memcpy(newVerts, thisVerts, thisVertCount * sizeof(vec3));
	print_merge_progress();
	memcpy(newVerts + thisVertCount, otherVerts, otherVertCount * sizeof(vec3));
	print_merge_progress();

	delete[] this->lumps[LUMP_VERTICES];
	this->lumps[LUMP_VERTICES] = (byte*)newVerts;
	header.lump[LUMP_VERTICES].nLength = totalVertCount*sizeof(vec3);
}

void Bsp::merge_texinfo(Bsp& other) {
	BSPTEXTUREINFO* thisInfo = (BSPTEXTUREINFO*)lumps[LUMP_TEXINFO];
	BSPTEXTUREINFO* otherInfo = (BSPTEXTUREINFO*)other.lumps[LUMP_TEXINFO];
	int thisInfoCount = header.lump[LUMP_TEXINFO].nLength / sizeof(BSPTEXTUREINFO);
	int otherInfoCount = other.header.lump[LUMP_TEXINFO].nLength / sizeof(BSPTEXTUREINFO);

	progress_title = "texture info";
	progress = 0;
	progress_total = thisInfoCount + otherInfoCount;

	vector<BSPTEXTUREINFO> mergedInfo;
	mergedInfo.reserve(thisInfoCount + otherInfoCount);

	for (int i = 0; i < thisInfoCount; i++) {
		mergedInfo.push_back(thisInfo[i]);
		print_merge_progress();
	}

	for (int i = 0; i < otherInfoCount; i++) {
		BSPTEXTUREINFO info = otherInfo[i];
		info.iMiptex = texRemap[info.iMiptex];

		bool isUnique = true;
		for (int k = 0; k < thisInfoCount; k++) {
			if (memcmp(&info, &thisInfo[k], sizeof(BSPTEXTUREINFO)) == 0) {
				texInfoRemap.push_back(k);
				isUnique = false;
				break;
			}
		}

		if (isUnique) {
			texInfoRemap.push_back(mergedInfo.size());
			mergedInfo.push_back(info);
		}
		print_merge_progress();
	}

	int newLen = mergedInfo.size() * sizeof(BSPTEXTUREINFO);
	int duplicates = mergedInfo.size() - (thisInfoCount + otherInfoCount);

	if (duplicates) {
		cout << "Removed " << duplicates << " duplicate texinfos\n";
		cout << "ZOMG NOT READY FOR THIS\n";
		// TODO: update plane references in other BSP when duplicates are removed
	}

	delete[] this->lumps[LUMP_TEXINFO];
	this->lumps[LUMP_TEXINFO] = new byte[newLen];
	memcpy(this->lumps[LUMP_TEXINFO], &mergedInfo[0], newLen);
	header.lump[LUMP_TEXINFO].nLength = newLen;

	//cout << thisInfoCount << " -> " << mergedInfo.size() << endl;
}

void Bsp::merge_faces(Bsp& other) {
	BSPFACE* thisFaces = (BSPFACE*)lumps[LUMP_FACES];
	BSPFACE* otherFaces = (BSPFACE*)other.lumps[LUMP_FACES];
	thisFaceCount = header.lump[LUMP_FACES].nLength / sizeof(BSPFACE);
	int otherFaceCount = other.header.lump[LUMP_FACES].nLength / sizeof(BSPFACE);
	int totalFaceCount = thisFaceCount + otherFaceCount;

	progress_title = "faces";
	progress = 0;
	progress_total = totalFaceCount + 1;
	print_merge_progress();

	BSPFACE* newFaces = new BSPFACE[totalFaceCount];
	memcpy(newFaces, thisFaces, thisFaceCount * sizeof(BSPFACE));
	memcpy(newFaces + thisFaceCount, otherFaces, otherFaceCount * sizeof(BSPFACE));

	for (int i = thisFaceCount; i < totalFaceCount; i++) {
		BSPFACE& face = newFaces[i];
		face.iPlane = planeRemap[face.iPlane];
		face.iFirstEdge = face.iFirstEdge + thisSurfEdgeCount;
		face.iTextureInfo = texInfoRemap[face.iTextureInfo];
		print_merge_progress();
	}

	delete[] this->lumps[LUMP_FACES];
	this->lumps[LUMP_FACES] = (byte*)newFaces;
	header.lump[LUMP_FACES].nLength = totalFaceCount*sizeof(BSPFACE);
}

void Bsp::merge_leaves(Bsp& other) {
	BSPLEAF* thisLeaves = (BSPLEAF*)lumps[LUMP_LEAVES];
	BSPLEAF* otherLeaves = (BSPLEAF*)other.lumps[LUMP_LEAVES];
	thisLeafCount = header.lump[LUMP_LEAVES].nLength / sizeof(BSPLEAF);
	otherLeafCount = other.header.lump[LUMP_LEAVES].nLength / sizeof(BSPLEAF);

	int thisWorldLeafCount = ((BSPMODEL*)lumps[LUMP_MODELS])->nVisLeafs+1; // include solid leaf

	progress_title = "leaves";
	progress = 0;
	progress_total = thisLeafCount + otherLeafCount;

	vector<BSPLEAF> mergedLeaves;
	mergedLeaves.reserve(thisWorldLeafCount + otherLeafCount);
	modelLeafRemap.reserve(thisWorldLeafCount + otherLeafCount);

	for (int i = 0; i < thisWorldLeafCount; i++) {
		modelLeafRemap.push_back(i);
		mergedLeaves.push_back(thisLeaves[i]);
		print_merge_progress();
	}

	for (int i = 0; i < otherLeafCount; i++) {
		BSPLEAF& leaf = otherLeaves[i];
		if (leaf.nMarkSurfaces) {
			leaf.iFirstMarkSurface = leaf.iFirstMarkSurface + thisMarkSurfCount;
		}

		bool isSharedSolidLeaf = i == 0;
		if (!isSharedSolidLeaf) {
			leavesRemap.push_back(mergedLeaves.size());
			mergedLeaves.push_back(leaf);
		}
		else {
			// always exclude the first solid leaf since there can only be one per map, at index 0
			leavesRemap.push_back(0);
		}
		print_merge_progress();
	}

	// append A's submodel leaves after B's world leaves
	// Order will be: A's world leaves -> B's world leaves -> B's submodel leaves -> A's submodel leaves
	for (int i = thisWorldLeafCount; i < thisLeafCount; i++) {
		modelLeafRemap.push_back(mergedLeaves.size());
		mergedLeaves.push_back(thisLeaves[i]);
	}

	otherLeafCount -= 1; // solid leaf removed

	int newLen = mergedLeaves.size() * sizeof(BSPLEAF);

	delete[] this->lumps[LUMP_LEAVES];
	this->lumps[LUMP_LEAVES] = new byte[newLen];
	memcpy(this->lumps[LUMP_LEAVES], &mergedLeaves[0], newLen);
	header.lump[LUMP_LEAVES].nLength = newLen;

	//cout << thisLeafCount << " -> " << mergedLeaves.size() << endl;
}

void Bsp::merge_marksurfs(Bsp& other) {
	uint16* thisMarks = (uint16*)lumps[LUMP_MARKSURFACES];
	uint16* otherMarks = (uint16*)other.lumps[LUMP_MARKSURFACES];
	thisMarkSurfCount = header.lump[LUMP_MARKSURFACES].nLength / sizeof(uint16);
	int otherMarkCount = other.header.lump[LUMP_MARKSURFACES].nLength / sizeof(uint16);
	int totalSurfCount = thisMarkSurfCount + otherMarkCount;

	progress_title = "mark surfaces";
	progress = 0;
	progress_total = otherMarkCount + 1;
	print_merge_progress();

	uint16* newSurfs = new uint16[totalSurfCount];
	memcpy(newSurfs, thisMarks, thisMarkSurfCount * sizeof(uint16));
	memcpy(newSurfs + thisMarkSurfCount, otherMarks, otherMarkCount * sizeof(uint16));

	for (int i = thisMarkSurfCount; i < totalSurfCount; i++) {
		uint16& mark = newSurfs[i];
		mark = mark + thisFaceCount;
		print_merge_progress();
	}

	delete[] this->lumps[LUMP_MARKSURFACES];
	this->lumps[LUMP_MARKSURFACES] = (byte*)newSurfs;
	header.lump[LUMP_MARKSURFACES].nLength = totalSurfCount*sizeof(uint16);
}

void Bsp::merge_edges(Bsp& other) {
	BSPEDGE* thisEdges = (BSPEDGE*)lumps[LUMP_EDGES];
	BSPEDGE* otherEdges = (BSPEDGE*)other.lumps[LUMP_EDGES];
	thisEdgeCount = header.lump[LUMP_EDGES].nLength / sizeof(BSPEDGE);
	int otherEdgeCount = other.header.lump[LUMP_EDGES].nLength / sizeof(BSPEDGE);
	int totalEdgeCount = thisEdgeCount + otherEdgeCount;

	progress_title = "edges";
	progress = 0;
	progress_total = otherEdgeCount + 1;
	print_merge_progress();

	BSPEDGE* newEdges = new BSPEDGE[totalEdgeCount];
	memcpy(newEdges, thisEdges, thisEdgeCount * sizeof(BSPEDGE));
	memcpy(newEdges + thisEdgeCount, otherEdges, otherEdgeCount * sizeof(BSPEDGE));

	for (int i = thisEdgeCount; i < totalEdgeCount; i++) {
		BSPEDGE& edge = newEdges[i];
		edge.iVertex[0] = edge.iVertex[0] + thisVertCount;
		edge.iVertex[1] = edge.iVertex[1] + thisVertCount;
		print_merge_progress();
	}

	delete[] this->lumps[LUMP_EDGES];
	this->lumps[LUMP_EDGES] = (byte*)newEdges;
	header.lump[LUMP_EDGES].nLength = totalEdgeCount * sizeof(BSPEDGE);
}

void Bsp::merge_surfedges(Bsp& other) {
	int32_t* thisSurfs = (int32_t*)lumps[LUMP_SURFEDGES];
	int32_t* otherSurfs = (int32_t*)other.lumps[LUMP_SURFEDGES];
	thisSurfEdgeCount = header.lump[LUMP_SURFEDGES].nLength / sizeof(int32_t);
	int otherSurfCount = other.header.lump[LUMP_SURFEDGES].nLength / sizeof(int32_t);
	int totalSurfCount = thisSurfEdgeCount + otherSurfCount;

	progress_title = "surface edges";
	progress = 0;
	progress_total = otherSurfCount + 1;
	print_merge_progress();

	int32_t* newSurfs = new int32_t[totalSurfCount];
	memcpy(newSurfs, thisSurfs, thisSurfEdgeCount * sizeof(int32_t));
	memcpy(newSurfs + thisSurfEdgeCount, otherSurfs, otherSurfCount * sizeof(int32_t));

	for (int i = thisSurfEdgeCount; i < totalSurfCount; i++) {
		int32_t& surfEdge = newSurfs[i];
		surfEdge = surfEdge < 0 ? surfEdge - thisEdgeCount : surfEdge + thisEdgeCount;
		print_merge_progress();
	}

	delete[] this->lumps[LUMP_SURFEDGES];
	this->lumps[LUMP_SURFEDGES] = (byte*)newSurfs;
	header.lump[LUMP_SURFEDGES].nLength = totalSurfCount*sizeof(int32_t);
}

void Bsp::merge_nodes(Bsp& other) {
	BSPNODE* thisNodes = (BSPNODE*)lumps[LUMP_NODES];
	BSPNODE* otherNodes = (BSPNODE*)other.lumps[LUMP_NODES];
	thisNodeCount = header.lump[LUMP_NODES].nLength / sizeof(BSPNODE);
	int otherNodeCount = other.header.lump[LUMP_NODES].nLength / sizeof(BSPNODE);

	progress_title = "nodes";
	progress = 0;
	progress_total = thisNodeCount + otherNodeCount;

	vector<BSPNODE> mergedNodes;
	mergedNodes.reserve(thisNodeCount + otherNodeCount);

	for (int i = 0; i < thisNodeCount; i++) {
		BSPNODE node = thisNodes[i];

		if (i > 0) { // new headnode should already be correct
			for (int k = 0; k < 2; k++) {
				if (node.iChildren[k] >= 0) {
					node.iChildren[k] += 1; // shifted from new head node
				}
				else {
					node.iChildren[k] = ~((int16_t)modelLeafRemap[~node.iChildren[k]]);
				}
			}
		}

		mergedNodes.push_back(node);
		print_merge_progress();
	}

	for (int i = 0; i < otherNodeCount; i++) {
		BSPNODE node = otherNodes[i];

		for (int k = 0; k < 2; k++) {
			if (node.iChildren[k] >= 0) {
				node.iChildren[k] += thisNodeCount;
			}
			else {
				node.iChildren[k] = ~((int16_t)leavesRemap[~node.iChildren[k]]);
			}
		}
		node.iPlane = planeRemap[node.iPlane];
		if (node.nFaces) {
			node.firstFace = node.firstFace + thisFaceCount;
		}
		
		mergedNodes.push_back(node);
		print_merge_progress();
	}

	int newLen = mergedNodes.size() * sizeof(BSPNODE);

	delete[] this->lumps[LUMP_NODES];
	this->lumps[LUMP_NODES] = new byte[newLen];
	memcpy(this->lumps[LUMP_NODES], &mergedNodes[0], newLen);
	header.lump[LUMP_NODES].nLength = newLen;

	//cout << thisNodeCount << " -> " << mergedNodes.size() << endl;
}

void Bsp::merge_clipnodes(Bsp& other) {
	BSPCLIPNODE* thisNodes = (BSPCLIPNODE*)lumps[LUMP_CLIPNODES];
	BSPCLIPNODE* otherNodes = (BSPCLIPNODE*)other.lumps[LUMP_CLIPNODES];
	thisClipnodeCount = header.lump[LUMP_CLIPNODES].nLength / sizeof(BSPCLIPNODE);
	int otherClipnodeCount = other.header.lump[LUMP_CLIPNODES].nLength / sizeof(BSPCLIPNODE);

	progress_title = "clipnodes";
	progress = 0;
	progress_total = thisClipnodeCount + otherClipnodeCount;

	vector<BSPCLIPNODE> mergedNodes;
	mergedNodes.reserve(thisClipnodeCount + otherClipnodeCount);

	for (int i = 0; i < thisClipnodeCount; i++) {
		BSPCLIPNODE node = thisNodes[i];
		if (i > 2) { // new headnodes should already be correct
			for (int k = 0; k < 2; k++) {
				if (node.iChildren[k] >= 0) {
					node.iChildren[k] += MAX_MAP_HULLS-1; // offset from new headnodes being added
				}
			}
		}
		mergedNodes.push_back(node);
		print_merge_progress();
	}

	for (int i = 0; i < otherClipnodeCount; i++) {
		BSPCLIPNODE node = otherNodes[i];
		node.iPlane = planeRemap[node.iPlane];

		for (int k = 0; k < 2; k++) {
			if (node.iChildren[k] >= 0) {
				node.iChildren[k] += thisClipnodeCount;
			}
		}
		mergedNodes.push_back(node);
		print_merge_progress();
	}

	int newLen = mergedNodes.size() * sizeof(BSPCLIPNODE);

	delete[] this->lumps[LUMP_CLIPNODES];
	this->lumps[LUMP_CLIPNODES] = new byte[newLen];
	memcpy(this->lumps[LUMP_CLIPNODES], &mergedNodes[0], newLen);
	header.lump[LUMP_CLIPNODES].nLength = newLen;

	//cout << thisClipnodeCount << " -> " << mergedNodes.size() << endl;
}

void Bsp::merge_models(Bsp& other) {
	BSPMODEL* thisModels = (BSPMODEL*)lumps[LUMP_MODELS];
	BSPMODEL* otherModels = (BSPMODEL*)other.lumps[LUMP_MODELS];
	int thisModelCount = header.lump[LUMP_MODELS].nLength / sizeof(BSPMODEL);
	int otherModelCount = other.header.lump[LUMP_MODELS].nLength / sizeof(BSPMODEL);

	progress_title = "models";
	progress = 0;
	progress_total = thisModelCount + otherModelCount;

	vector<BSPMODEL> mergedModels;
	mergedModels.reserve(thisModelCount + otherModelCount);

	// merged world model
	mergedModels.push_back(thisModels[0]);

	// other map's submodels
	for (int i = 1; i < otherModelCount; i++) {
		BSPMODEL model = otherModels[i];
		model.iHeadnodes[0] += thisNodeCount + 1;
		for (int k = 1; k < MAX_MAP_HULLS; k++) {
			model.iHeadnodes[k] += thisClipnodeCount;
		}
		model.iFirstFace = model.iFirstFace + thisFaceCount;
		mergedModels.push_back(model);
		print_merge_progress();
	}

	// this map's submodels
	for (int i = 1; i < thisModelCount; i++) {
		BSPMODEL model = thisModels[i];
		model.iHeadnodes[0] += 1; // adjust for new head node
		for (int k = 1; k < MAX_MAP_HULLS; k++) {
			model.iHeadnodes[k] += (MAX_MAP_HULLS-1); // adjust for new head nodes
		}
		mergedModels.push_back(model);
		print_merge_progress();
	}

	// update world head nodes
	mergedModels[0].iHeadnodes[0] = 0;
	mergedModels[0].iHeadnodes[1] = 0;
	mergedModels[0].iHeadnodes[2] = 1;
	mergedModels[0].iHeadnodes[3] = 2;
	mergedModels[0].nVisLeafs = thisModels[0].nVisLeafs + otherModels[0].nVisLeafs;
	mergedModels[0].nFaces = thisModels[0].nFaces + otherModels[0].nFaces;

	vec3 amin = thisModels[0].nMins;
	vec3 bmin = otherModels[0].nMins;
	vec3 amax = thisModels[0].nMaxs;
	vec3 bmax = otherModels[0].nMaxs;
	mergedModels[0].nMins = { min(amin.x, bmin.x), min(amin.y, bmin.y), min(amin.z, bmin.z) };
	mergedModels[0].nMaxs = { max(amax.x, bmax.x), max(amax.y, bmax.y), max(amax.z, bmax.z) };

	int newLen = mergedModels.size() * sizeof(BSPMODEL);

	delete[] this->lumps[LUMP_MODELS];
	this->lumps[LUMP_MODELS] = new byte[newLen];
	memcpy(this->lumps[LUMP_MODELS], &mergedModels[0], newLen);
	header.lump[LUMP_MODELS].nLength = newLen;

	//cout << thisModelCount << " -> " << mergedModels.size() << endl;
}

void Bsp::shiftVis(uint64* vis, int len, int offsetLeaf, int shift) {
	byte bitsPerStep = 64;
	byte offsetBit = offsetLeaf % bitsPerStep;
	uint64 mask = 0; // part of the byte that shouldn't be shifted
	for (int i = 0; i < offsetBit; i++) {
		mask |= 1 << i;
	}

	len /= 8; // byte -> uint64 (vis rows are always divisible by 8)

	for (int k = 0; k < shift; k++) {

		bool carry = 0;
		for (int i = 0; i < len; i++) {
			uint64 oldCarry = carry;
			carry = (vis[i] & 0x8000000000000000L) != 0;

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
			printf("ZOMG OVERFLOW VIS\n");
		}
	}
}

// decompress this map's vis data into arrays of bits where each bit indicates if a leaf is visible or not
// iterationLeaves = number of leaves to decompress vis for
// visDataLeafCount = total leaves in this map (exluding the shared solid leaf 0)
// newNumLeaves = total leaves that will be in the map after merging is finished (again, excluding solid leaf 0)
void Bsp::decompress_vis_lump(BSPLEAF* leafLump, byte* visLump, byte* output, 
	int iterationLeaves, int visDataLeafCount, int newNumLeaves, 
	int shiftOffsetBit, int shiftAmount)
{
	byte* dest;
	uint oldVisRowSize = ((visDataLeafCount + 63) & ~63) >> 3;
	uint newVisRowSize = ((newNumLeaves + 63) & ~63) >> 3;
	int len = 0;

	for (int i = 0; i < iterationLeaves; i++)
	{
		dest = output + i * newVisRowSize;

		if (leafLump[i + 1].nVisOffset == -1) {
			memset(dest, 255, visDataLeafCount/8);
			for (int k = 0; k < visDataLeafCount % 8; k++) {
				dest[visDataLeafCount / 8] |= 1 << k;
			}
			shiftVis((uint64*)dest, newVisRowSize, shiftOffsetBit, shiftAmount);
			continue;
		}

		DecompressVis((const byte*)(visLump + leafLump[i+1].nVisOffset), dest, oldVisRowSize, visDataLeafCount);
	
		if (shiftAmount) {
			shiftVis((uint64*)dest, newVisRowSize, shiftOffsetBit, shiftAmount);
		}

		print_merge_progress();
	}
}

void print_vis(byte* vis, int visLeafCount, int g_bitbytes) {
	for (int k = 0; k < visLeafCount; k++) {
		printf("LEAF %02d: ", k+1);
		for (int i = 0; i < g_bitbytes; i++) {
			printf("%3d ", vis[k * g_bitbytes + i]);
		}
		cout << endl;
	}
}

void Bsp::merge_vis(Bsp& other) {
	byte* thisVis = (byte*)lumps[LUMP_VISIBILITY];
	byte* otherVis = (byte*)other.lumps[LUMP_VISIBILITY];

	BSPLEAF* allLeaves = (BSPLEAF*)lumps[LUMP_LEAVES];

	int thisVisLeaves = thisLeafCount - 1; // VIS ignores the shared solid leaf 0
	int otherVisLeaves = otherLeafCount; // already does not include the solid leaf (see merge_leaves)
	int totalVisLeaves = thisVisLeaves + otherVisLeaves;

	int thisModelLeafCount = thisVisLeaves - thisWorldLeafCount;
	int otherModelLeafCount = otherVisLeaves - otherWorldLeafCount;

	uint newVisRowSize = ((totalVisLeaves + 63) & ~63) >> 3;
	int decompressedVisSize = totalVisLeaves * newVisRowSize;

	// submodel leaves should come after world leaves and need to be moved after the incoming world leaves from the other map
	int shiftOffsetBit = 0; // where to start making room for the submodel leaves
	int shiftAmount = otherLeafCount;
	for (int k = 0; k < modelLeafRemap.size(); k++) {
		if (k != modelLeafRemap[k]) {
			shiftOffsetBit = k - 1; // skip solid leaf
			break;
		}
	}

	progress_title = "visibility";
	progress = 0;
	progress_total = thisWorldLeafCount + thisModelLeafCount + otherLeafCount;

	//debug_vis(thisVis, allLeaves, totalLeaves - 1);
	//return;

	byte* decompressedVis = new byte[decompressedVisSize];
	memset(decompressedVis, 0, decompressedVisSize);

	// decompress this map's world leaves
	decompress_vis_lump(allLeaves, thisVis, decompressedVis, 
		thisWorldLeafCount, thisVisLeaves, totalVisLeaves,
		shiftOffsetBit, shiftAmount);

	// decompress this map's model leaves (also making room for the other map's world leaves)
	BSPLEAF* thisModelLeaves = allLeaves + thisWorldLeafCount + otherLeafCount;
	byte* modelLeafVisDest = decompressedVis + (thisWorldLeafCount + otherLeafCount) * newVisRowSize;
	decompress_vis_lump(thisModelLeaves, thisVis, modelLeafVisDest,
		thisModelLeafCount, thisVisLeaves, totalVisLeaves,
		shiftOffsetBit, shiftAmount);

	//cout << "Decompressed this vis:\n";
	//print_vis(decompressedVis, thisVisLeaves + otherLeafCount, newVisRowSize);

	// all of other map's leaves come after this map's world leaves
	shiftOffsetBit = 0;
	shiftAmount = thisWorldLeafCount; // world leaf count (exluding solid leaf)

	// decompress other map's vis data (skip empty first leaf, which now only the first map should have)
	byte* decompressedOtherVis = decompressedVis + thisWorldLeafCount*newVisRowSize;
	decompress_vis_lump(allLeaves + thisWorldLeafCount, otherVis, decompressedOtherVis,
		otherLeafCount, otherLeafCount, totalVisLeaves,
		shiftOffsetBit, shiftAmount);
	
	//cout << "Decompressed other vis:\n";
	//print_vis(decompressedOtherVis, otherLeafCount, newVisRowSize);

	//memset(decompressedVis + 9 * newBitbytes, 0xff, otherMapVisSize);

	//cout << "Decompressed combined vis:\n";
	//print_vis(decompressedVis, totalVisLeaves, newVisRowSize);

	// recompress the combined vis data
	byte* compressedVis = new byte[decompressedVisSize];
	memset(compressedVis, 0, decompressedVisSize);
	int newVisLen = CompressAll(allLeaves, decompressedVis, compressedVis, totalVisLeaves);
	int oldLen = header.lump[LUMP_VISIBILITY].nLength;

	delete[] this->lumps[LUMP_VISIBILITY];
	this->lumps[LUMP_VISIBILITY] = new byte[newVisLen];
	memcpy(this->lumps[LUMP_VISIBILITY], compressedVis, newVisLen);
	header.lump[LUMP_VISIBILITY].nLength = newVisLen;

	//cout << oldLen << " -> " << newVisLen << endl;
}

void Bsp::merge_lighting(Bsp& other) {
	BSPFACE* faces = (BSPFACE*)lumps[LUMP_FACES];
	COLOR3* thisRad = (COLOR3*)lumps[LUMP_LIGHTING];
	COLOR3* otherRad = (COLOR3*)other.lumps[LUMP_LIGHTING];
	int thisColorCount = header.lump[LUMP_LIGHTING].nLength / sizeof(COLOR3);
	int otherColorCount = other.header.lump[LUMP_LIGHTING].nLength / sizeof(COLOR3);
	int totalColorCount = thisColorCount + otherColorCount;
	int totalFaceCount = header.lump[LUMP_FACES].nLength / sizeof(BSPFACE);

	progress_title = "lightmaps";
	progress = 0;
	progress_total = 4 + totalFaceCount;

	// create a single full-bright lightmap to use for all faces, if one map has lighting but the other doesn't
	if (thisColorCount == 0 && otherColorCount != 0) {
		thisColorCount = MAX_SURFACE_EXTENT * MAX_SURFACE_EXTENT;
		totalColorCount += thisColorCount;
		int sz = thisColorCount * sizeof(COLOR3);
		lumps[LUMP_LIGHTING] = new byte[sz];
		header.lump[LUMP_LIGHTING].nLength = sz;
		thisRad = (COLOR3*)lumps[LUMP_LIGHTING];

		memset(thisRad, 255, sz);
		
		for (int i = 0; i < thisFaceCount; i++) {
			faces[i].nLightmapOffset = 0;
		}
	} else if (thisColorCount != 0 && otherColorCount == 0) {
		otherColorCount = MAX_SURFACE_EXTENT * MAX_SURFACE_EXTENT;
		totalColorCount += otherColorCount;
		otherRad = new COLOR3[otherColorCount];

		memset(otherRad, 255, otherColorCount*sizeof(COLOR3));

		for (int i = thisFaceCount; i < totalFaceCount; i++) {
			faces[i].nLightmapOffset = 0;
		}
	}

	COLOR3* newRad = new COLOR3[totalColorCount];
	print_merge_progress();

	memcpy(newRad, thisRad, thisColorCount * sizeof(COLOR3));
	print_merge_progress();

	memcpy((byte*)newRad + thisColorCount * sizeof(COLOR3), otherRad, otherColorCount * sizeof(COLOR3));
	print_merge_progress();


	delete[] this->lumps[LUMP_LIGHTING];
	this->lumps[LUMP_LIGHTING] = (byte*)newRad;
	int oldLen = header.lump[LUMP_LIGHTING].nLength;
	header.lump[LUMP_LIGHTING].nLength = totalColorCount*sizeof(COLOR3);
	print_merge_progress();	

	for (int i = thisFaceCount; i < totalFaceCount; i++) {
		faces[i].nLightmapOffset += thisColorCount*sizeof(COLOR3);
		print_merge_progress();
	}

	//cout << oldLen << " -> " << header.lump[LUMP_LIGHTING].nLength << endl;
}

void Bsp::get_bounding_box(vec3& mins, vec3& maxs) {
	BSPMODEL& thisWorld = ((BSPMODEL*)lumps[LUMP_MODELS])[0];

	// the model bounds are little bigger than the actual vertices bounds in the map,
	// but if you go by the vertices then there will be collision problems.

	mins = thisWorld.nMins;
	maxs = thisWorld.nMaxs;
}

BSPPLANE Bsp::separate(Bsp& other) {
	BSPMODEL& thisWorld = ((BSPMODEL*)lumps[LUMP_MODELS])[0];
	BSPMODEL& otherWorld = ((BSPMODEL*)other.lumps[LUMP_MODELS])[0];

	vec3 amin = thisWorld.nMins;
	vec3 amax = thisWorld.nMaxs;
	vec3 bmin = otherWorld.nMins;
	vec3 bmax = otherWorld.nMaxs;

	BSPPLANE separationPlane;
	memset(&separationPlane, 0, sizeof(BSPPLANE));

	// separating plane points toward the other map (b)
	if (bmin.x >= amax.x) {
		separationPlane.nType = PLANE_X;
		separationPlane.vNormal = {1, 0, 0};
		separationPlane.fDist = amax.x + (bmin.x - amax.x)*0.5f;
	}
	else if (bmax.x <= amin.x) {
		separationPlane.nType = PLANE_X;
		separationPlane.vNormal = { -1, 0, 0 };
		separationPlane.fDist = bmax.x + (amin.x - bmax.x) * 0.5f;
	}
	else if (bmin.y >= amax.y) {
		separationPlane.nType = PLANE_Y;
		separationPlane.vNormal = { 0, 1, 0 };
		separationPlane.fDist = bmin.y;
	}
	else if (bmax.y <= amin.y) {
		separationPlane.nType = PLANE_Y;
		separationPlane.vNormal = { 0, -1, 0 };
		separationPlane.fDist = bmax.y;
	}
	else if (bmin.z >= amax.z) {
		separationPlane.nType = PLANE_Z;
		separationPlane.vNormal = { 0, 0, 1 };
		separationPlane.fDist = bmin.z;
	}
	else if (bmax.z <= amin.z) {
		separationPlane.nType = PLANE_Z;
		separationPlane.vNormal = { 0, 0, -1 };
		separationPlane.fDist = bmax.z;
	}
	else {
		separationPlane.nType = -1; // no simple separating axis

		printf("Bounding boxes for each map:\n");
		printf("(%6.0f, %6.0f, %6.0f)", amin.x, amin.y, amin.z);
		printf(" - (%6.0f, %6.0f, %6.0f) %s\n", amax.x, amax.y, amax.z, this->name.c_str());

		printf("(%6.0f, %6.0f, %6.0f)", bmin.x, bmin.y, bmin.z);
		printf(" - (%6.0f, %6.0f, %6.0f) %s\n", bmax.x, bmax.y, bmax.z, other.name.c_str());
	}

	return separationPlane;	
}

void Bsp::create_merge_headnodes(Bsp& other, BSPPLANE separationPlane) {
	BSPMODEL& thisWorld = ((BSPMODEL*)lumps[LUMP_MODELS])[0];
	BSPMODEL& otherWorld = ((BSPMODEL*)other.lumps[LUMP_MODELS])[0];

	vec3 amin = thisWorld.nMins;
	vec3 amax = thisWorld.nMaxs;
	vec3 bmin = otherWorld.nMins;
	vec3 bmax = otherWorld.nMaxs;

	// planes with negative normals mess up VIS and lighting stuff, so swap children instead
	bool swapNodeChildren = separationPlane.vNormal.x < 0 || separationPlane.vNormal.y < 0 || separationPlane.vNormal.z < 0;
	if (swapNodeChildren)
		separationPlane.vNormal = separationPlane.vNormal.invert();

	//printf("Separating plane: (%.0f, %.0f, %.0f) %.0f\n", separationPlane.vNormal.x, separationPlane.vNormal.y, separationPlane.vNormal.z, separationPlane.fDist);

	// write separating plane
	BSPPLANE* thisPlanes = (BSPPLANE*)lumps[LUMP_PLANES];
	int numThisPlanes = header.lump[LUMP_PLANES].nLength / sizeof(BSPPLANE);

	BSPPLANE* newThisPlanes = new BSPPLANE[numThisPlanes + 1];
	memcpy(newThisPlanes, thisPlanes, numThisPlanes * sizeof(BSPPLANE));
	newThisPlanes[numThisPlanes] = separationPlane;

	delete[] this->lumps[LUMP_PLANES];
	this->lumps[LUMP_PLANES] = (byte*)newThisPlanes;
	header.lump[LUMP_PLANES].nLength = (numThisPlanes + 1) * sizeof(BSPPLANE);

	int separationPlaneIdx = numThisPlanes;


	// write new head node (visible BSP)
	{
		BSPNODE* thisNodes = (BSPNODE*)lumps[LUMP_NODES];
		int numThisNodes = header.lump[LUMP_NODES].nLength / sizeof(BSPNODE);

		BSPNODE headNode = {
			separationPlaneIdx,			// plane idx
			{numThisNodes+1, 1},		// child nodes
			{ min(amin.x, bmin.x), min(amin.y, bmin.y), min(amin.z, bmin.z) },	// mins
			{ max(amax.x, bmax.x), max(amax.y, bmax.y), max(amax.z, bmax.z) },	// maxs
			0, // first face
			0  // n faces (none since this plane is in the void)
		};

		if (swapNodeChildren) {
			int16_t temp = headNode.iChildren[0];
			headNode.iChildren[0] = headNode.iChildren[1];
			headNode.iChildren[1] = temp;
		}

		BSPNODE* newThisNodes = new BSPNODE[numThisNodes + 1];
		memcpy(newThisNodes + 1, thisNodes, numThisNodes * sizeof(BSPNODE));
		newThisNodes[0] = headNode;

		delete[] this->lumps[LUMP_NODES];
		this->lumps[LUMP_NODES] = (byte*)newThisNodes;
		header.lump[LUMP_NODES].nLength = (numThisNodes + 1) * sizeof(BSPNODE);
	}


	// write new head node (clipnode BSP)
	{
		BSPCLIPNODE* thisNodes = (BSPCLIPNODE*)lumps[LUMP_CLIPNODES];
		int numThisNodes = header.lump[LUMP_CLIPNODES].nLength / sizeof(BSPCLIPNODE);
		const int NEW_NODE_COUNT = MAX_MAP_HULLS - 1;

		BSPCLIPNODE newHeadNodes[NEW_NODE_COUNT];
		for (int i = 0; i < NEW_NODE_COUNT; i++) {
			//printf("HULL %d starts at %d\n", i+1, thisWorld.iHeadnodes[i+1]);
			newHeadNodes[i] = {
				separationPlaneIdx,	// plane idx
				{	// child nodes
					(int16_t)(otherWorld.iHeadnodes[i + 1] + numThisNodes + NEW_NODE_COUNT),
					(int16_t)(thisWorld.iHeadnodes[i + 1] + NEW_NODE_COUNT)
				},
			};

			if (swapNodeChildren) {
				int16_t temp = newHeadNodes[i].iChildren[0];
				newHeadNodes[i].iChildren[0] = newHeadNodes[i].iChildren[1];
				newHeadNodes[i].iChildren[1] = temp;
			}
		}

		BSPCLIPNODE* newThisNodes = new BSPCLIPNODE[numThisNodes + NEW_NODE_COUNT];
		memcpy(newThisNodes, newHeadNodes, NEW_NODE_COUNT * sizeof(BSPCLIPNODE));
		memcpy(newThisNodes + NEW_NODE_COUNT, thisNodes, numThisNodes * sizeof(BSPCLIPNODE));

		delete[] this->lumps[LUMP_CLIPNODES];
		this->lumps[LUMP_CLIPNODES] = (byte*)newThisNodes;
		header.lump[LUMP_CLIPNODES].nLength = (numThisNodes + NEW_NODE_COUNT) * sizeof(BSPCLIPNODE);
	}
}

bool Bsp::move(vec3 offset) {
	BSPPLANE* planes = (BSPPLANE*)lumps[LUMP_PLANES];
	BSPTEXTUREINFO* texInfo = (BSPTEXTUREINFO*)lumps[LUMP_TEXINFO];
	BSPLEAF* leaves = (BSPLEAF*)lumps[LUMP_LEAVES];
	BSPMODEL* models = (BSPMODEL*)lumps[LUMP_MODELS];
	BSPNODE* nodes = (BSPNODE*)lumps[LUMP_NODES];
	BSPFACE* faces = (BSPFACE*)lumps[LUMP_FACES];
	vec3* verts = (vec3*)lumps[LUMP_VERTICES];
	COLOR3* lightdata = (COLOR3*)lumps[LUMP_LIGHTING];

	int planeCount = header.lump[LUMP_PLANES].nLength / sizeof(BSPPLANE);
	int texInfoCount = header.lump[LUMP_TEXINFO].nLength / sizeof(BSPTEXTUREINFO);
	int leafCount = header.lump[LUMP_LEAVES].nLength / sizeof(BSPLEAF);
	int modelCount = header.lump[LUMP_MODELS].nLength / sizeof(BSPMODEL);
	int nodeCount = header.lump[LUMP_NODES].nLength / sizeof(BSPNODE);
	int vertCount = header.lump[LUMP_VERTICES].nLength / sizeof(vec3);
	int faceCount = header.lump[LUMP_FACES].nLength / sizeof(BSPFACE);
	int thisColorCount = header.lump[LUMP_LIGHTING].nLength / sizeof(COLOR3);

	bool hasLighting = thisColorCount > 0;

	LIGHTMAP* oldLightmaps = NULL;
	LIGHTMAP* newLightmaps = NULL;

	if (hasLighting) {
		oldLightmaps = new LIGHTMAP[faceCount];
		newLightmaps = new LIGHTMAP[faceCount];
		memset(oldLightmaps, 0, sizeof(LIGHTMAP) * faceCount);
		memset(newLightmaps, 0, sizeof(LIGHTMAP) * faceCount);

		qrad_init_globals(this);

		for (int i = 0; i < faceCount; i++) {
			BSPFACE& face = faces[i];

			int size[2];
			GetFaceLightmapSize(i, size);

			int lightmapSz = size[0] * size[1];

			int lightmapCount = 0;
			for (int k = 0; k < 4; k++) {
				lightmapCount += face.nStyles[k] != 255;
			}
			lightmapSz *= lightmapCount;

			oldLightmaps[i].width = size[0];
			oldLightmaps[i].height = size[1];
			oldLightmaps[i].layers = lightmapCount;

			qrad_get_lightmap_flags(this, i, oldLightmaps[i].luxelFlags);

			if (i % (faceCount / 3) == 0) {
				printf(".");
			}
		}
	}
	
	bool* modelHasOrigin = new bool[modelCount];
	memset(modelHasOrigin, 0, modelCount * sizeof(bool));

	for (int i = 0; i < ents.size(); i++) {
		if (!ents[i]->hasKey("origin")) {
			continue;
		}
		if (ents[i]->isBspModel()) {
			modelHasOrigin[ents[i]->getBspModelIdx()] = true;
		}

		if (ents[i]->keyvalues["classname"] == "info_node") {
			ents[i]->keyvalues["classname"] = "info_bode";
		}

		Keyvalue keyvalue("origin", ents[i]->keyvalues["origin"]);
		vec3 ori = keyvalue.getVector() + offset;

		ents[i]->keyvalues["origin"] = ori.toKeyvalueString();
	}

	update_ent_lump();

	for (int i = 0; i < leafCount; i++) {
		BSPLEAF& leaf = leaves[i];

		if (fabs((float)leaf.nMins[0] + offset.x) > MAX_MAP_COORD ||
			fabs((float)leaf.nMaxs[0] + offset.x) > MAX_MAP_COORD ||
			fabs((float)leaf.nMins[1] + offset.y) > MAX_MAP_COORD ||
			fabs((float)leaf.nMaxs[1] + offset.y) > MAX_MAP_COORD ||
			fabs((float)leaf.nMins[2] + offset.z) > MAX_MAP_COORD ||
			fabs((float)leaf.nMaxs[2] + offset.z) > MAX_MAP_COORD) {
			printf("WARNING: Bounding box for leaf moved past safe world boundary!");
		}
		leaf.nMins[0] += offset.x;
		leaf.nMaxs[0] += offset.x;
		leaf.nMins[1] += offset.y;
		leaf.nMaxs[1] += offset.y;
		leaf.nMins[2] += offset.z;
		leaf.nMaxs[2] += offset.z;
	}

	for (int i = 0; i < nodeCount; i++) {
		BSPNODE& node = nodes[i];

		if (fabs((float)node.nMins[0] + offset.x) > MAX_MAP_COORD ||
			fabs((float)node.nMaxs[0] + offset.x) > MAX_MAP_COORD ||
			fabs((float)node.nMins[1] + offset.y) > MAX_MAP_COORD ||
			fabs((float)node.nMaxs[1] + offset.y) > MAX_MAP_COORD ||
			fabs((float)node.nMins[2] + offset.z) > MAX_MAP_COORD ||
			fabs((float)node.nMaxs[2] + offset.z) > MAX_MAP_COORD) {
			printf("WARNING: Bounding box for leaf moved past safe world boundary!");
		}
		node.nMins[0] += offset.x;
		node.nMaxs[0] += offset.x;
		node.nMins[1] += offset.y;
		node.nMaxs[1] += offset.y;
		node.nMins[2] += offset.z;
		node.nMaxs[2] += offset.z;
	}

	// map a verts/plane indexes to a model
	int* vertexToModel = new int[vertCount];
	int* planeToModel = new int[planeCount];
	int* texInfoToModel = new int[texInfoCount];
	memset(vertexToModel, -1, vertCount*sizeof(int));
	memset(planeToModel, -1, planeCount *sizeof(int));
	memset(texInfoToModel, -1, texInfoCount *sizeof(int));

	int32_t* surfEdges = (int32_t*)lumps[LUMP_SURFEDGES];
	BSPEDGE* edges = (BSPEDGE*)lumps[LUMP_EDGES];

	for (int i = 0; i < modelCount; i++) {
		BSPMODEL& model = models[i];

		if (!modelHasOrigin[i]) {
			model.nMins += offset;
			model.nMaxs += offset;
			//model.vOrigin += offset; (wouldn't work once maps are merged into one model)

			if (fabs(model.nMins.x) > MAX_MAP_COORD ||
				fabs(model.nMins.y) > MAX_MAP_COORD ||
				fabs(model.nMins.z) > MAX_MAP_COORD ||
				fabs(model.nMaxs.z) > MAX_MAP_COORD ||
				fabs(model.nMaxs.z) > MAX_MAP_COORD ||
				fabs(model.nMaxs.z) > MAX_MAP_COORD) {
				printf("WARNING: Model moved past safe world boundary!");
			}
		}

		for (int j = 0; j < model.nFaces; j++)
		{
			BSPFACE& face = faces[model.iFirstFace + j];

			for (int e = 0; e < face.nEdges; e++) {
				int32_t edgeIdx = surfEdges[face.iFirstEdge + e];
				BSPEDGE& edge = edges[abs(edgeIdx)];
				int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];

				int oldVertIdx = vertexToModel[vertIdx];
				int oldPlaneModelIdx = planeToModel[face.iPlane];
				int oldTexInfoIdx = texInfoToModel[face.iTextureInfo];

				// TODO: yust duplicate the structure if there's a conflict
				if (oldVertIdx >= 0 && oldVertIdx != i && modelHasOrigin[oldVertIdx] != modelHasOrigin[i]) {
					printf("ERROR: Model %d shares a vertex with %d, and only one an origin. Something will be messed up.\n", i, vertexToModel[vertIdx]);
				}
				if (oldPlaneModelIdx >= 0 && oldPlaneModelIdx != i && modelHasOrigin[oldPlaneModelIdx] != modelHasOrigin[i]) {
					printf("ERROR: Model %d shares a plane with %d, and only one an origin. Something will be messed up.\n", i, planeToModel[face.iPlane]);
				}
				if (oldTexInfoIdx >= 0 && oldTexInfoIdx != i && modelHasOrigin[oldTexInfoIdx] != modelHasOrigin[i]) {
					printf("ERROR: Model %d shares a texinfo with %d, and only one an origin. Something will be messed up.\n", i, vertexToModel[vertIdx]);
				}

				vertexToModel[vertIdx] = i;
				planeToModel[face.iPlane] = i;
				texInfoToModel[face.iTextureInfo] = i;
			}
		}
	}

	for (int i = 0; i < vertCount; i++) {
		if (vertexToModel[i] != -1 && modelHasOrigin[vertexToModel[i]]) {
			continue; // don't move submodels with origins
		}

		vec3& vert = verts[i];

		vert += offset;

		if (fabs(vert.x) > MAX_MAP_COORD ||
			fabs(vert.y) > MAX_MAP_COORD ||
			fabs(vert.z) > MAX_MAP_COORD) {
			printf("WARNING: Vertex moved past safe world boundary!");
		}
	}

	for (int i = 0; i < planeCount; i++) {
		if (planeToModel[i] != -1 && modelHasOrigin[planeToModel[i]]) {
			continue; // don't move submodels with origins
		}

		BSPPLANE& plane = planes[i];
		vec3 newPlaneOri = offset + (plane.vNormal * plane.fDist);

		if (fabs(newPlaneOri.x) > MAX_MAP_COORD || fabs(newPlaneOri.y) > MAX_MAP_COORD ||
			fabs(newPlaneOri.z) > MAX_MAP_COORD) {
			printf("WARNING: Plane origin moved past safe world boundary!");
		}

		// get distance between new plane origin and the origin-aligned plane
		plane.fDist = dotProduct(plane.vNormal, newPlaneOri) / dotProduct(plane.vNormal, plane.vNormal);
	}

	uint32_t texCount = (uint32_t)(lumps[LUMP_TEXTURES])[0];
	byte* textures = lumps[LUMP_TEXTURES];
	vec3 offsetDir = offset.normalize();
	float offsetLen = offset.length();

	for (int i = 0; i < texInfoCount; i++) {
		if (texInfoToModel[i] != -1 && modelHasOrigin[texInfoToModel[i]]) {
			continue; // don't move submodels with origins
		}

		BSPTEXTUREINFO& info = texInfo[i];

		int32_t texOffset = ((int32_t*)textures)[info.iMiptex + 1];
		BSPMIPTEX& tex = *((BSPMIPTEX*)(textures + texOffset));

		float scaleS = info.vS.length();
		float scaleT = info.vT.length();
		vec3 nS = info.vS.normalize();
		vec3 nT = info.vT.normalize();

		vec3 newOriS = offset + (nS * info.shiftS);
		vec3 newOriT = offset + (nT * info.shiftT);

		float shiftScaleS = dotProduct(offsetDir, nS);
		float shiftScaleT = dotProduct(offsetDir, nT);

		int olds = info.shiftS;
		int oldt = info.shiftT;

		float shiftAmountS = shiftScaleS * offsetLen * scaleS;
		float shiftAmountT = shiftScaleT * offsetLen * scaleT;

		info.shiftS -= shiftAmountS;
		info.shiftT -= shiftAmountT;

		// minimize shift values (just to be safe. floats can be p wacky and zany)
		while (fabs(info.shiftS) > tex.nWidth) {
			info.shiftS += (info.shiftS < 0) ? (int)tex.nWidth : -(int)(tex.nWidth);
		}
		while (fabs(info.shiftT) > tex.nHeight) {
			info.shiftT += (info.shiftT < 0) ? (int)tex.nHeight : -(int)(tex.nHeight);
		}
	}

	delete[] vertexToModel;
	delete[] modelHasOrigin;
	delete[] texInfoToModel;

	if (hasLighting) {
		// calculate new lightmap sizes
		qrad_init_globals(this);
		int newLightDataSz = 0;
		int totalLightmaps = 0;
		int lightmapsResizeCount = 0;
		for (int i = 0; i < faceCount; i++) {
			BSPFACE& face = faces[i];

			if (face.nStyles[0] == 255 || texInfo[face.iTextureInfo].nFlags & TEX_SPECIAL)
				continue;

			BSPTEXTUREINFO& info = texInfo[face.iTextureInfo];
			int32_t texOffset = ((int32_t*)textures)[info.iMiptex + 1];
			BSPMIPTEX& tex = *((BSPMIPTEX*)(textures + texOffset));

			int size[2];
			GetFaceLightmapSize(i, size);

			int lightmapSz = size[0] * size[1];

			newLightmaps[i].width = size[0];
			newLightmaps[i].height = size[1];
			newLightmaps[i].layers = oldLightmaps[i].layers;

			newLightDataSz += (lightmapSz * newLightmaps[i].layers) * sizeof(COLOR3);

			totalLightmaps += newLightmaps[i].layers;
			if (oldLightmaps[i].width != newLightmaps[i].width || oldLightmaps[i].height != newLightmaps[i].height) {
				lightmapsResizeCount += newLightmaps[i].layers;
			}
		}

		if (lightmapsResizeCount > 0) {
			printf(" %d lightmap(s) to resize", lightmapsResizeCount, totalLightmaps);

			int newColorCount = newLightDataSz / sizeof(COLOR3);
			COLOR3* newLightData = new COLOR3[newColorCount];
			memset(newLightData, 0, newColorCount * sizeof(COLOR3));
			int lightmapOffset = 0;


			for (int i = 0; i < faceCount; i++) {
				BSPFACE& face = faces[i];

				if (i % (faceCount / 3) == 0) {
					printf(".");
				}

				if (face.nStyles[0] == 255 || texInfo[face.iTextureInfo].nFlags & TEX_SPECIAL) // no lighting
					continue;

				LIGHTMAP& oldLight = oldLightmaps[i];
				LIGHTMAP& newLight = newLightmaps[i];
				int oldLayerSz = (oldLight.width * oldLight.height) * sizeof(COLOR3);
				int newLayerSz = (newLight.width * newLight.height) * sizeof(COLOR3);
				int oldSz = oldLayerSz * oldLight.layers;
				int newSz = newLayerSz * newLight.layers;

				totalLightmaps++;

				if (oldLight.width == newLight.width && oldLight.height == newLight.height) {
					memcpy((byte*)newLightData + lightmapOffset, (byte*)lightdata + face.nLightmapOffset, oldSz);
				}
				else {
					qrad_get_lightmap_flags(this, i, newLight.luxelFlags);

					int minWidth = min(newLight.width, oldLight.width);
					int minHeight = min(newLight.height, oldLight.height);

					int srcOffsetX, srcOffsetY;
					get_lightmap_shift(oldLight, newLight, srcOffsetX, srcOffsetY);

					for (int layer = 0; layer < newLight.layers; layer++) {
						int srcOffset = (face.nLightmapOffset + oldLayerSz * layer) / sizeof(COLOR3);
						int dstOffset = (lightmapOffset + newLayerSz * layer) / sizeof(COLOR3);

						for (int y = 0; y < minHeight; y++) {
							for (int x = 0; x < minWidth; x++) {
								int offsetX = x + srcOffsetX;
								int offsetY = y + srcOffsetY;

								int srcX = oldLight.width > newLight.width ? offsetX : x;
								int srcY = oldLight.height > newLight.height ? offsetY : y;
								int dstX = newLight.width > oldLight.width ? offsetX : x;
								int dstY = newLight.height > oldLight.height ? offsetY : y;

								srcX = max(0, min(oldLight.width - 1, srcX));
								srcY = max(0, min(oldLight.height - 1, srcY));
								dstX = max(0, min(newLight.width - 1, dstX));
								dstY = max(0, min(newLight.height - 1, dstY));

								COLOR3& src = lightdata[srcOffset + srcY * oldLight.width + srcX];
								COLOR3& dst = newLightData[dstOffset + dstY * newLight.width + dstX];

								dst = src;
							}
						}
					}
				}

				face.nLightmapOffset = lightmapOffset;
				lightmapOffset += newSz;
			}

			delete[] this->lumps[LUMP_LIGHTING];
			this->lumps[LUMP_LIGHTING] = (byte*)newLightData;
			header.lump[LUMP_LIGHTING].nLength = lightmapOffset;
		}

		delete[] oldLightmaps;
		delete[] newLightmaps;
	}

	printf("\n");

	return true;
}

void Bsp::get_lightmap_shift(const LIGHTMAP& oldLightmap, const LIGHTMAP& newLightmap, int& srcOffsetX, int& srcOffsetY) {
	int minWidth = min(newLightmap.width, oldLightmap.width);
	int minHeight = min(newLightmap.height, oldLightmap.height);

	int bestMatch = 0;
	int bestShiftCombo = 0;
	
	// Try different combinations of shifts to find the best alignment of the lightmaps.
	// Example (2 = unlit, 3 = lit)
	//  old         new
	// 3 3 3      2 3 3 3
	// 3 3 3  ->  2 3 3 3  =  old lightmap matches more luxels when it's shifted right 1 pixel in the new lightmap
	// 3 3 3      2 3 3 3
	// Only works for lightmap resizes caused by precision errors. Faces that are actually different sizes will
	// likely have more than 1 pixel of difference in either dimension.
	for (int t = 0; t < 4; t++) {
		int numMatch = 0;
		for (int y = 0; y < minHeight; y++) {
			for (int x = 0; x < minWidth; x++) {
				int offsetX = x;
				int offsetY = y;

				if (t == 1) {
					offsetX = x + 1;
				}
				if (t == 2) {
					offsetY = y + 1;
				}
				if (t == 3) {
					offsetX = x + 1;
					offsetY = y + 1;
				}

				int srcX = oldLightmap.width > newLightmap.width ? offsetX : x;
				int srcY = oldLightmap.height > newLightmap.height ? offsetY : y;
				int dstX = newLightmap.width > oldLightmap.width ? offsetX : x;
				int dstY = newLightmap.height > oldLightmap.height ? offsetY : y;

				srcX = max(0, min(oldLightmap.width - 1, srcX));
				srcY = max(0, min(oldLightmap.height - 1, srcY));
				dstX = max(0, min(newLightmap.width - 1, dstX));
				dstY = max(0, min(newLightmap.height - 1, dstY));

				int oldLuxelFlag = oldLightmap.luxelFlags[srcY * oldLightmap.width + srcX];
				int newLuxelFlag = newLightmap.luxelFlags[dstY * newLightmap.width + dstX];

				if (oldLuxelFlag == newLuxelFlag) {
					numMatch += 1;
				}
			}
		}

		if (numMatch > bestMatch) {
			bestMatch = numMatch;
			bestShiftCombo = t;
		}
	}

	int shouldShiftLeft = bestShiftCombo == 1 || bestShiftCombo == 3;
	int shouldShiftTop = bestShiftCombo == 2 || bestShiftCombo == 3;

	srcOffsetX = newLightmap.width != oldLightmap.width ? shouldShiftLeft : 0;
	srcOffsetY = newLightmap.height != oldLightmap.height ? shouldShiftTop : 0;
}

void Bsp::update_ent_lump() {
	stringstream ent_data;

	for (int i = 0; i < ents.size(); i++) {
		ent_data << "{\n";

		for (int k = 0; k < ents[i]->keyOrder.size(); k++) {
			string key = ents[i]->keyOrder[k];
			ent_data << "\"" << key << "\" \"" << ents[i]->keyvalues[key] << "\"\n";
		}

		ent_data << "}";
		if (i < ents.size() - 1) {
			ent_data << "\n"; // trailing newline crashes sven, and only sven, and only sometimes
		}
	}

	string str_data = ent_data.str();

	delete[] lumps[LUMP_ENTITIES];
	header.lump[LUMP_ENTITIES].nLength = str_data.size();
	lumps[LUMP_ENTITIES] = new byte[str_data.size()];
	memcpy((char*)lumps[LUMP_ENTITIES], str_data.c_str(), str_data.size());
}

void Bsp::write(string path) {
	cout << "Writing " << path << endl;

	// calculate lump offsets
	int offset = sizeof(BSPHEADER);
	for (int i = 0; i < HEADER_LUMPS; i++) {
		header.lump[i].nOffset = offset;
		offset += header.lump[i].nLength;
	}

	ofstream file(path, ios::out | ios::binary | ios::trunc);


	file.write((char*)&header, sizeof(BSPHEADER));

	// write the lumps
	for (int i = 0; i < HEADER_LUMPS; i++) {
		file.write((char*)lumps[i], header.lump[i].nLength);
	}
}

bool Bsp::load_lumps(string fpath)
{
	bool valid = true;

	// Read all BSP Data
	ifstream fin(fpath, ios::binary | ios::ate);
	int size = fin.tellg();
	fin.seekg(0, fin.beg);

	if (size < sizeof(BSPHEADER) + sizeof(BSPLUMP)*HEADER_LUMPS)
		return false;

	fin.read((char*)&header.nVersion, sizeof(int));
	
	for (int i = 0; i < HEADER_LUMPS; i++)
		fin.read((char*)&header.lump[i], sizeof(BSPLUMP));

	lumps = new byte*[HEADER_LUMPS];
	memset(lumps, 0, sizeof(byte*)*HEADER_LUMPS);
	
	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		if (header.lump[i].nLength == 0) {
			lumps[i] = NULL;
			continue;
		}

		fin.seekg(header.lump[i].nOffset);
		if (fin.eof()) {
			cout << "FAILED TO READ BSP LUMP " + to_string(i) + "\n";
			valid = false;
		}
		else
		{
			lumps[i] = new byte[header.lump[i].nLength];
			fin.read((char*)lumps[i], header.lump[i].nLength);
		}
	}	
	
	fin.close();

	return valid;
}

void Bsp::load_ents()
{
	ents.clear();
	bool verbose = true;
	membuf sbuf((char*)lumps[LUMP_ENTITIES], header.lump[LUMP_ENTITIES].nLength);
	istream in(&sbuf);

	int lineNum = 0;
	int lastBracket = -1;
	Entity* ent = NULL;

	string line = "";
	while (getline(in, line))
	{
		lineNum++;
		if (line.length() < 1 || line[0] == '\n')
			continue;

		if (line[0] == '{')
		{
			if (lastBracket == 0)
			{
				cout << path + ".bsp ent data (line " + to_string(lineNum) + "): Unexpected '{'\n";
				continue;
			}
			lastBracket = 0;

			if (ent != NULL)
				delete ent;
			ent = new Entity();
		}
		else if (line[0] == '}')
		{
			if (lastBracket == 1)
				cout << path + ".bsp ent data (line " + to_string(lineNum) + "): Unexpected '}'\n";
			lastBracket = 1;

			if (ent == NULL)
				continue;

			ents.push_back(ent);
			ent = NULL;

			// you can end/start an ent on the same line, you know
			if (line.find("{") != string::npos)
			{
				ent = new Entity();
				lastBracket = 0;
			}
		}
		else if (lastBracket == 0 && ent != NULL) // currently defining an entity
		{
			Keyvalue k(line);
			if (k.key.length() && k.value.length())
				ent->addKeyvalue(k);
		}
	}	
	//cout << "got " << ents.size() <<  " entities\n";

	if (ent != NULL)
		delete ent;
}

void Bsp::print_merge_progress() {
	if (progress++ > 0) {
		auto now = std::chrono::system_clock::now();
		std::chrono::duration<double> delta = now - last_progress;
		if (delta.count() < 0.016) {
			return;
		}
		last_progress = now;
	}

	int percent = (progress / (float)progress_total)*100;

	printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b");
	printf("    Merging %-13s %2d%%", progress_title, percent);
}

void Bsp::print_stat(string name, uint val, uint max, bool isMem) {
	const float meg = 1024 * 1024;
	float percent = (val / (float)max) * 100;

	if (val > max) {
		print_color(PRINT_RED | PRINT_BRIGHT);
	}
	else if (percent >= 90) {
		print_color(PRINT_RED | PRINT_GREEN | PRINT_BRIGHT);
	}
	else if (percent >= 75) {
		print_color(PRINT_RED | PRINT_GREEN | PRINT_BLUE | PRINT_BRIGHT);
	}
	else {
		print_color(PRINT_RED | PRINT_GREEN | PRINT_BLUE);
	}

	printf("%-12s  ", name.c_str());
	if (isMem) {
		printf("%8.1f / %-5.1f MB", val/meg, max/meg);
	}
	else {
		printf("%8u / %-8u", val, max);
	}
	printf("  %6.1f%%", percent);

	if (val > max) {
		printf("  (OVERFLOW!!!)");
	}

	printf("\n");

	print_color(PRINT_RED | PRINT_GREEN | PRINT_BLUE);
}

void Bsp::print_info() {
	printf(" Data Type       Current / Max     Fullness\n");
	printf("------------  -------------------  --------\n");

	int planeCount = header.lump[LUMP_PLANES].nLength / sizeof(BSPPLANE);
	int texInfoCount = header.lump[LUMP_TEXINFO].nLength / sizeof(BSPTEXTUREINFO);
	int leafCount = header.lump[LUMP_LEAVES].nLength / sizeof(BSPLEAF);
	int modelCount = header.lump[LUMP_MODELS].nLength / sizeof(BSPMODEL);
	int nodeCount = header.lump[LUMP_NODES].nLength / sizeof(BSPNODE);
	int vertCount = header.lump[LUMP_VERTICES].nLength / sizeof(vec3);
	int faceCount = header.lump[LUMP_FACES].nLength / sizeof(BSPFACE);
	int clipnodeCount = header.lump[LUMP_CLIPNODES].nLength / sizeof(BSPCLIPNODE);
	int marksurfacesCount = header.lump[LUMP_MARKSURFACES].nLength / sizeof(uint16_t);
	int surfedgeCount = header.lump[LUMP_SURFEDGES].nLength / sizeof(int32_t);
	int edgeCount = header.lump[LUMP_EDGES].nLength / sizeof(BSPEDGE);
	int textureCount = *((int32_t*)(lumps[LUMP_TEXTURES]));
	int lightDataLength = header.lump[LUMP_LIGHTING].nLength;
	int visDataLength = header.lump[LUMP_VISIBILITY].nLength;
	int entCount = ents.size();

	print_stat("models", modelCount, MAX_MAP_MODELS, false);
	print_stat("planes", planeCount, MAX_MAP_PLANES, false);
	print_stat("vertexes", vertCount, MAX_MAP_VERTS, false);
	print_stat("nodes", nodeCount, MAX_MAP_NODES, false);
	print_stat("texinfos", texInfoCount, MAX_MAP_TEXINFOS, false);
	print_stat("faces", faceCount, MAX_MAP_FACES, false);
	print_stat("clipnodes", clipnodeCount, MAX_MAP_CLIPNODES, false);
	print_stat("leaves", leafCount, MAX_MAP_LEAVES, false);
	print_stat("marksurfaces", marksurfacesCount, MAX_MAP_MARKSURFS, false);
	print_stat("surfedges", surfedgeCount, MAX_MAP_SURFEDGES, false);
	print_stat("edges", edgeCount, MAX_MAP_SURFEDGES, false);
	print_stat("textures", textureCount, MAX_MAP_TEXTURES, false);
	print_stat("lightdata", lightDataLength, MAX_MAP_LIGHTDATA, true);
	print_stat("visdata", visDataLength, MAX_MAP_VISDATA, true);
	print_stat("entities", entCount, MAX_MAP_ENTS, false);
}

void Bsp::print_bsp() {
	BSPMODEL* models = (BSPMODEL*)lumps[LUMP_MODELS];		

	for (int i = 0; i < header.lump[LUMP_MODELS].nLength / sizeof(BSPMODEL); i++) {
		int node = models[i].iHeadnodes[0];
		printf("\nModel %02d\n", i);
		recurse_node(node, 0);
	}
}

void Bsp::recurse_node(int16_t nodeIdx, int depth) {

	BSPNODE* nodes = (BSPNODE*)lumps[LUMP_NODES];

	for (int i = 0; i < depth; i++) {
		cout << "    ";
	}

	if (nodeIdx < 0) {
		BSPLEAF* leaves = (BSPLEAF * )lumps[LUMP_LEAVES];
		BSPLEAF leaf = leaves[~nodeIdx];
		print_leaf(leaf);
		cout << " (LEAF " << ~nodeIdx << ")" << endl;
		return;
	}
	else {
		print_node(nodes[nodeIdx]);
		cout << endl;
	}
	
	recurse_node(nodes[nodeIdx].iChildren[0], depth+1);
	recurse_node(nodes[nodeIdx].iChildren[1], depth+1);
}

void Bsp::print_node(BSPNODE node) {
	BSPPLANE* planes = (BSPPLANE * )lumps[LUMP_PLANES];
	BSPPLANE plane = planes[node.iPlane];

	cout << "Plane (" << plane.vNormal.x << " " << plane.vNormal.y << " " << plane.vNormal.z << ") d: " << plane.fDist;
}

int Bsp::pointContents(int iNode, vec3 p)
{
	BSPNODE* nodes = (BSPNODE*)lumps[LUMP_NODES];
	BSPPLANE* planes = (BSPPLANE*)lumps[LUMP_PLANES];
	BSPLEAF* leaves = (BSPLEAF*)lumps[LUMP_LEAVES];

	float       d;
	BSPNODE* node;
	BSPPLANE* plane;

	while (iNode >= 0)
	{
		node = &nodes[iNode];
		plane = &planes[node->iPlane];

		d = dotProduct(plane->vNormal, p) - plane->fDist;
		if (d < 0)
			iNode = node->iChildren[1];
		else
			iNode = node->iChildren[0];
	}

	cout << "Contents at " << p.x << " " << p.y << " " << p.z << " is ";
	print_leaf(leaves[~iNode]);
	cout << " (LEAF " << ~iNode << ")\n";

	return leaves[~iNode].nContents;
}

void Bsp::dump_lightmap(int faceIdx, string outputPath)
{
	BSPFACE* faces = (BSPFACE*)lumps[LUMP_FACES];
	int faceCount = header.lump[LUMP_FACES].nLength / sizeof(BSPFACE);
	byte* lightdata = (byte*)lumps[LUMP_LIGHTING];

	BSPFACE& face = faces[faceIdx];

	qrad_init_globals(this);

	int mins[2];
	int extents[2];
	GetFaceExtents(faceIdx, mins, extents);

	int lightmapSz = extents[0] * extents[1];

	lodepng_encode24_file(outputPath.c_str(), lightdata + face.nLightmapOffset, extents[0], extents[1]);
}

void Bsp::dump_lightmap_atlas(string outputPath) {
	BSPFACE* faces = (BSPFACE*)lumps[LUMP_FACES];
	int faceCount = header.lump[LUMP_FACES].nLength / sizeof(BSPFACE);
	byte* lightdata = (byte*)lumps[LUMP_LIGHTING];

	int lightmapWidth = MAX_SURFACE_EXTENT;

	int lightmapsPerDim = ceil(sqrt(faceCount));
	int atlasDim = lightmapsPerDim * lightmapWidth;
	int sz = atlasDim * atlasDim;
	printf("ATLAS SIZE %d x %d (%.2f KB)", lightmapsPerDim, lightmapsPerDim, (sz * sizeof(COLOR3))/1024.0f);

	COLOR3* pngData = new COLOR3[sz];

	memset(pngData, 0, sz * sizeof(COLOR3));

	qrad_init_globals(this);

	for (int i = 0; i < faceCount; i++) {
		BSPFACE& face = faces[i];

		if (face.nStyles[0] == 255)
			continue; // no lighting info

		int atlasX = (i % lightmapsPerDim)*lightmapWidth;
		int atlasY = (i / lightmapsPerDim)*lightmapWidth;

		int size[2];
		GetFaceLightmapSize(i, size);

		int lightmapWidth = size[0];
		int lightmapHeight = size[1];

		for (int y = 0; y < lightmapHeight; y++) {
			for (int x = 0; x < lightmapWidth; x++) {
				int dstX = atlasX + x;
				int dstY = atlasY + y;

				int lightmapOffset = (y * lightmapWidth + x)*sizeof(COLOR3);

				COLOR3* src = (COLOR3*)(lightdata + face.nLightmapOffset + lightmapOffset);

				pngData[dstY * atlasDim + dstX] = *src;
			}
		}
	}

	lodepng_encode24_file(outputPath.c_str(), (byte*)pngData, atlasDim, atlasDim);
}

void Bsp::write_csg_outputs(string path) {
	BSPPLANE* thisPlanes = (BSPPLANE*)lumps[LUMP_PLANES];
	int numPlanes = header.lump[LUMP_PLANES].nLength / sizeof(BSPPLANE);

	// add flipped version of planes since face output files can't specify plane side
	BSPPLANE* newPlanes = new BSPPLANE[numPlanes*2];
	memcpy(newPlanes, thisPlanes, numPlanes * sizeof(BSPPLANE));
	for (int i = 0; i < numPlanes; i++) {
		BSPPLANE flipped = thisPlanes[i];
		flipped.vNormal = { flipped.vNormal.x > 0 ? -flipped.vNormal.x : flipped.vNormal.x,
							flipped.vNormal.y > 0 ? -flipped.vNormal.y : flipped.vNormal.y,
							flipped.vNormal.z > 0 ? -flipped.vNormal.z : flipped.vNormal.z, };
		flipped.fDist = -flipped.fDist;
		newPlanes[numPlanes + i] = flipped;
	}
	delete [] lumps[LUMP_PLANES];
	lumps[LUMP_PLANES] = (byte*)newPlanes;
	numPlanes *= 2;
	header.lump[LUMP_PLANES].nLength = numPlanes * sizeof(BSPPLANE);
	thisPlanes = newPlanes;

	ofstream pln_file(path + name + ".pln", ios::out | ios::binary | ios::trunc);
	for (int i = 0; i < numPlanes; i++) {
		BSPPLANE& p = thisPlanes[i];
		CSGPLANE csgplane = {
			{p.vNormal.x, p.vNormal.y, p.vNormal.z},
			{0,0,0},
			p.fDist,
			p.nType
		};
		pln_file.write((char*)&csgplane, sizeof(CSGPLANE));
	}
	cout << "Wrote " << numPlanes << " planes\n";

	BSPFACE* thisFaces = (BSPFACE*)lumps[LUMP_FACES];
	int thisFaceCount = header.lump[LUMP_FACES].nLength / sizeof(BSPFACE);

	BSPMODEL* models = (BSPMODEL*)lumps[LUMP_MODELS];
	BSPMODEL world = models[0];

	for (int i = 0; i < 4; i++) {
		FILE* polyfile = fopen((path + name + ".p" + to_string(i)).c_str(), "wb");
		write_csg_polys(world.iHeadnodes[i], polyfile, numPlanes/2, i == 0);
		fprintf(polyfile, "-1 -1 -1 -1 -1\n"); // end of file marker (parsing fails without this)
		fclose(polyfile);

		FILE* detailfile = fopen((path + name + ".b" + to_string(i)).c_str(), "wb");
		fprintf(detailfile, "-1\n");
		fclose(detailfile);
	}

	ofstream hsz_file(path + name + ".hsz", ios::out | ios::binary | ios::trunc);
	const char* hullSizes = "0 0 0 0 0 0\n"
							"-16 -16 -36 16 16 36\n"
							"-32 -32 -32 32 32 32\n"
							"-16 -16 -18 16 16 18\n";
	hsz_file.write(hullSizes, strlen(hullSizes));

	ofstream bsp_file(path + name + "_new.bsp", ios::out | ios::binary | ios::trunc);
	// calculate lump offsets
	int offset = sizeof(BSPHEADER);
	for (int i = 0; i < HEADER_LUMPS; i++) {
		header.lump[i].nOffset = offset;
		if (i == LUMP_ENTITIES || i == LUMP_PLANES || i == LUMP_TEXTURES || i == LUMP_TEXINFO) {
			offset += header.lump[i].nLength;
			if (i == LUMP_PLANES) {
				int count = header.lump[i].nLength / sizeof(BSPPLANE);
				cout << "BSP HAS " << count << " PLANES\n";
			}
		}
		else {
			header.lump[i].nLength = 0;
		}
	}
	bsp_file.write((char*)&header, sizeof(BSPHEADER));
	// write the lumps
	for (int i = 0; i < HEADER_LUMPS; i++) {
		bsp_file.write((char*)lumps[i], header.lump[i].nLength);
	}
}

void Bsp::write_csg_polys(int16_t nodeIdx, FILE* polyfile, int flipPlaneSkip, bool debug) {
	BSPNODE* nodes = (BSPNODE*)lumps[LUMP_NODES];

	if (nodeIdx >= 0) {
		write_csg_polys(nodes[nodeIdx].iChildren[0], polyfile, flipPlaneSkip, debug);
		write_csg_polys(nodes[nodeIdx].iChildren[1], polyfile, flipPlaneSkip, debug);
		return;
	}

	BSPLEAF* leaves = (BSPLEAF*)lumps[LUMP_LEAVES];
	BSPLEAF leaf = leaves[~nodeIdx];

	int detaillevel = 0; // no way to know which faces came from a func_detail
	int32_t contents = leaf.nContents;

	uint16* marksurfs = (uint16*)lumps[LUMP_MARKSURFACES];
	int32_t* surfEdges = (int32_t*)lumps[LUMP_SURFEDGES];
	vec3* verts = (vec3*)lumps[LUMP_VERTICES];
	int numVerts = header.lump[LUMP_VERTICES].nLength / sizeof(vec3);
	BSPEDGE* edges = (BSPEDGE*)lumps[LUMP_EDGES];
	BSPFACE* faces = (BSPFACE*)lumps[LUMP_FACES];

	BSPPLANE* planes = (BSPPLANE*)lumps[LUMP_PLANES];

	
	for (int i = leaf.iFirstMarkSurface; i < leaf.iFirstMarkSurface + leaf.nMarkSurfaces; i++) {
		for (int z = 0; z < 2; z++) {
			if (z == 0)
				continue;
			BSPFACE& face = faces[marksurfs[i]];

			bool flipped = (z == 1 || face.nPlaneSide) && !(z == 1 && face.nPlaneSide);

			int iPlane = !flipped ? face.iPlane : face.iPlane + flipPlaneSkip;

			// contents in front of the face
			int faceContents = z == 1 ? leaf.nContents : CONTENTS_SOLID;

			//int texInfo = z == 1 ? face.iTextureInfo : -1;

			if (debug) {
				BSPPLANE plane = planes[iPlane];
				printf("Writing face (%2.0f %2.0f %2.0f) %4.0f  %s\n", 
					plane.vNormal.x, plane.vNormal.y, plane.vNormal.z, plane.fDist,
					(faceContents == CONTENTS_SOLID ? "SOLID" : "EMPTY"));
				if (flipped && false) {
					cout << " (flipped)";
				}
			}

			fprintf(polyfile, "%i %i %i %i %u\n", detaillevel, iPlane, face.iTextureInfo, faceContents, face.nEdges);

			if (flipped) {
				for (int e = (face.iFirstEdge + face.nEdges) - 1; e >= (int)face.iFirstEdge; e--) {
					int32_t edgeIdx = surfEdges[e];
					BSPEDGE& edge = edges[abs(edgeIdx)];
					vec3 v = edgeIdx >= 0 ? verts[edge.iVertex[1]] : verts[edge.iVertex[0]];
					fprintf(polyfile, "%5.8f %5.8f %5.8f\n", v.x, v.y, v.z);
				}
			}
			else {
				for (int e = face.iFirstEdge; e < face.iFirstEdge + face.nEdges; e++) {
					int32_t edgeIdx = surfEdges[e];
					BSPEDGE& edge = edges[abs(edgeIdx)];
					vec3 v = edgeIdx >= 0 ? verts[edge.iVertex[1]] : verts[edge.iVertex[0]];
					fprintf(polyfile, "%5.8f %5.8f %5.8f\n", v.x, v.y, v.z);
				}
			}

			fprintf(polyfile, "\n");
		}
		if (debug)
			printf("\n");
	}
}

void Bsp::print_leaf(BSPLEAF leaf) {
	switch (leaf.nContents) {
		case CONTENTS_EMPTY:
			cout << "EMPTY"; break;
		case CONTENTS_SOLID:
			cout << "SOLID"; break;
		case CONTENTS_WATER:
			cout << "WATER"; break;
		case CONTENTS_SLIME:
			cout << "SLIME"; break;
		case CONTENTS_LAVA:
			cout << "LAVA"; break;
		case CONTENTS_SKY:
			cout << "SKY"; break;
		case CONTENTS_ORIGIN:
			cout << "ORIGIN"; break;
		case CONTENTS_CURRENT_0:
			cout << "CURRENT_0"; break;
		case CONTENTS_CURRENT_90:
			cout << "CURRENT_90"; break;
		case CONTENTS_CURRENT_180:
			cout << "CURRENT_180"; break;
		case CONTENTS_CURRENT_270:
			cout << "CURRENT_270"; break;
		case CONTENTS_CURRENT_UP:
			cout << "CURRENT_UP"; break;
		case CONTENTS_CURRENT_DOWN:
			cout << "CURRENT_DOWN"; break;
		case CONTENTS_TRANSLUCENT:
			cout << "TRANSLUCENT"; break;
		default:
			cout << "UNKNOWN"; break;
	}

	cout << " " << leaf.nMarkSurfaces << " surfs";
}