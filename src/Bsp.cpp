#include "Bsp.h"
#include "util.h"
#include <algorithm>
#include <string.h>
#include <sstream>
#include <fstream>
#include <set>
#include <iomanip>

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
	cout << "Merging " << other.path << " into " << path << endl;

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
	surfEdgeRemap.clear();
	markSurfRemap.clear();
	vertRemap.clear();
	edgeRemap.clear();
	leavesRemap.clear();
	facesRemap.clear();

	bool shouldMerge[HEADER_LUMPS] = { false };

	for (int i = 0; i < HEADER_LUMPS; i++) {
		if (!lumps[i] && !other.lumps[i]) {
			//cout << "Skipping " << g_lump_names[i] << " lump (missing from both maps)\n";
		}
		else if (!lumps[i]) {
			//cout << "Replacing " << g_lump_names[i] << " lump\n";
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

	if (shouldMerge[LUMP_LIGHTING])
		merge_lighting(other);

	// doing this last because it takes way longer than anything else, and limit overflows should fail the
	// merge as soon as possible.
	if (shouldMerge[LUMP_VISIBILITY])
		merge_vis(other);

	return true;
}

void Bsp::merge_ents(Bsp& other)
{
	printf("Merging ents... ");

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
	}

	for (int i = 0; i < other.ents.size(); i++) {
		if (other.ents[i]->keyvalues["classname"] == "worldspawn") {
			Entity* otherWorldspawn = other.ents[i];

			vector<string> otherWads = splitString(otherWorldspawn->keyvalues["wad"], ";");

			// strip paths from wad names
			for (int j = 0; j < otherWads.size(); j++) {
				otherWads[i] = basename(otherWads[i]);
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
				thisWads[i] = basename(thisWads[i]);
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
			ents.push_back(copy);
		}
	}

	update_ent_lump();

	cout << oldEntCount << " -> " << ents.size() << endl;
}

void Bsp::merge_planes(Bsp& other) {
	printf("Merging planes... ");

	BSPPLANE* thisPlanes = (BSPPLANE*)lumps[LUMP_PLANES];
	BSPPLANE* otherPlanes = (BSPPLANE*)other.lumps[LUMP_PLANES];
	int numThisPlanes = header.lump[LUMP_PLANES].nLength / sizeof(BSPPLANE);
	int numOtherPlanes = other.header.lump[LUMP_PLANES].nLength / sizeof(BSPPLANE);
	
	vector<BSPPLANE> mergedPlanes;
	mergedPlanes.reserve(numThisPlanes + numOtherPlanes);

	for (int i = 0; i < numThisPlanes; i++) {
		mergedPlanes.push_back(thisPlanes[i]);
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
	}

	int newLen = mergedPlanes.size() * sizeof(BSPPLANE);
	int duplicates = mergedPlanes.size() - (numThisPlanes + numOtherPlanes);

	if (duplicates) {
		cout << "Removed " << duplicates << " duplicate planes\n";
		cout << "ZOMG NOT READY FOR THIS\n";
		// TODO: update plane references in other BSP when duplicates are removed
	}

	delete [] this->lumps[LUMP_PLANES];
	this->lumps[LUMP_PLANES] = new byte[newLen];
	memcpy(this->lumps[LUMP_PLANES], &mergedPlanes[0], newLen);
	header.lump[LUMP_PLANES].nLength = newLen;

	// add 1 for the separation plane coming later
	cout << (numThisPlanes+1) << " -> " << mergedPlanes.size() << endl;
}

int getMipTexDataSize(int width, int height) {
	int sz = 256*3 + 4; // pallette + padding

	for (int i = 0; i < MIPLEVELS; i++) {
		sz += (width >> i) * (height >> i);
	}

	return sz;
}

void Bsp::merge_textures(Bsp& other) {
	printf("Merging textures... ");

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

	cout << thisTexCount << " -> " << newTexCount << endl;
}

void Bsp::merge_vertices(Bsp& other) {
	printf("Merging vertices... ");

	vec3* thisVerts = (vec3*)lumps[LUMP_VERTICES];
	vec3* otherVerts = (vec3*)other.lumps[LUMP_VERTICES];
	int thisVertsCount = header.lump[LUMP_VERTICES].nLength / sizeof(vec3);
	int otherVertsCount = other.header.lump[LUMP_VERTICES].nLength / sizeof(vec3);

	vector<vec3> mergedVerts;
	mergedVerts.reserve(thisVertsCount + otherVertsCount);

	for (int i = 0; i < thisVertsCount; i++) {
		mergedVerts.push_back(thisVerts[i]);
	}
	for (int i = 0; i < otherVertsCount; i++) {
		bool isUnique = true;
		for (int k = 0; k < thisVertsCount; k++) {
			if (memcmp(&otherVerts[i], &thisVerts[k], sizeof(vec3)) == 0) {
				isUnique = false;
				vertRemap.push_back(k);
				break;
			}
		}

		if (isUnique) {
			vertRemap.push_back(mergedVerts.size());
			mergedVerts.push_back(otherVerts[i]);
		}
	}

	int newLen = mergedVerts.size() * sizeof(vec3);
	int duplicates = mergedVerts.size() - (thisVertsCount + otherVertsCount);

	if (duplicates) {
		cout << "Removed " << duplicates << " duplicate verts\n";
		cout << "ZOMG NOT READY FOR THIS\n";
		// TODO: update plane references in other BSP when duplicates are removed
	}

	delete[] this->lumps[LUMP_VERTICES];
	this->lumps[LUMP_VERTICES] = new byte[newLen];
	memcpy(this->lumps[LUMP_VERTICES], &mergedVerts[0], newLen);
	header.lump[LUMP_VERTICES].nLength = newLen;

	cout << thisVertsCount << " -> " << mergedVerts.size() << endl;
}

void Bsp::merge_texinfo(Bsp& other) {
	printf("Merging texture coordinates... ");

	BSPTEXTUREINFO* thisInfo = (BSPTEXTUREINFO*)lumps[LUMP_TEXINFO];
	BSPTEXTUREINFO* otherInfo = (BSPTEXTUREINFO*)other.lumps[LUMP_TEXINFO];
	int thisInfoCount = header.lump[LUMP_TEXINFO].nLength / sizeof(BSPTEXTUREINFO);
	int otherInfoCount = other.header.lump[LUMP_TEXINFO].nLength / sizeof(BSPTEXTUREINFO);

	vector<BSPTEXTUREINFO> mergedInfo;
	mergedInfo.reserve(thisInfoCount + otherInfoCount);

	for (int i = 0; i < thisInfoCount; i++) {
		mergedInfo.push_back(thisInfo[i]);
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

	cout << thisInfoCount << " -> " << mergedInfo.size() << endl;
}

void Bsp::merge_faces(Bsp& other) {
	printf("Merging faces... ");

	BSPFACE* thisFaces = (BSPFACE*)lumps[LUMP_FACES];
	BSPFACE* otherFaces = (BSPFACE*)other.lumps[LUMP_FACES];
	thisFaceCount = header.lump[LUMP_FACES].nLength / sizeof(BSPFACE);
	int otherFaceCount = other.header.lump[LUMP_FACES].nLength / sizeof(BSPFACE);

	vector<BSPFACE> mergedFaces;
	mergedFaces.reserve(thisFaceCount + otherFaceCount);

	for (int i = 0; i < thisFaceCount; i++) {
		mergedFaces.push_back(thisFaces[i]);
	}

	for (int i = 0; i < otherFaceCount; i++) {
		BSPFACE face = otherFaces[i];
		face.iPlane = planeRemap[face.iPlane];
		face.iFirstEdge = surfEdgeRemap[face.iFirstEdge];
		face.iTextureInfo = texInfoRemap[face.iTextureInfo];

		bool isUnique = true;
		for (int k = 0; k < thisFaceCount; k++) {
			if (memcmp(&face, &thisFaces[k], sizeof(BSPFACE)) == 0) {
				isUnique = false;
				facesRemap.push_back(k);
				break;
			}
		}

		if (isUnique) {
			facesRemap.push_back(mergedFaces.size());
			mergedFaces.push_back(face);
		}
	}

	int newLen = mergedFaces.size() * sizeof(BSPFACE);
	int duplicates = mergedFaces.size() - (thisFaceCount + otherFaceCount);

	if (duplicates) {
		cout << "Removed " << duplicates << " duplicate faces\n";
		cout << "ZOMG NOT READY FOR THIS\n";
		// TODO: update plane references in other BSP when duplicates are removed
	}

	delete[] this->lumps[LUMP_FACES];
	this->lumps[LUMP_FACES] = new byte[newLen];
	memcpy(this->lumps[LUMP_FACES], &mergedFaces[0], newLen);
	header.lump[LUMP_FACES].nLength = newLen;

	cout << thisFaceCount << " -> " << mergedFaces.size() << endl;
}

void Bsp::merge_leaves(Bsp& other) {
	printf("Merging leaves... ");

	BSPLEAF* thisLeaves = (BSPLEAF*)lumps[LUMP_LEAVES];
	BSPLEAF* otherLeaves = (BSPLEAF*)other.lumps[LUMP_LEAVES];
	thisLeafCount = header.lump[LUMP_LEAVES].nLength / sizeof(BSPLEAF);
	otherLeafCount = other.header.lump[LUMP_LEAVES].nLength / sizeof(BSPLEAF);

	int thisWorldLeafCount = ((BSPMODEL*)lumps[LUMP_MODELS])->nVisLeafs+1; // include solid leaf

	vector<BSPLEAF> mergedLeaves;
	mergedLeaves.reserve(thisLeafCount + otherLeafCount);

	for (int i = 0; i < thisWorldLeafCount; i++) {
		modelLeafRemap.push_back(i);
		mergedLeaves.push_back(thisLeaves[i]);
	}

	for (int i = 0; i < otherLeafCount; i++) {
		BSPLEAF& leaf = otherLeaves[i];
		if (leaf.nMarkSurfaces) {
			leaf.iFirstMarkSurface = markSurfRemap[leaf.iFirstMarkSurface];
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
	}

	// append A's submodel leaves after B's world leaves
	// Order will be: A's world leaves -> B's world leaves -> B's submodel leaves -> A's submodel leaves
	for (int i = thisWorldLeafCount; i < thisLeafCount; i++) {
		modelLeafRemap.push_back(mergedLeaves.size());
		mergedLeaves.push_back(thisLeaves[i]);
	}

	int newLen = mergedLeaves.size() * sizeof(BSPLEAF);
	int duplicates = (thisLeafCount + otherLeafCount) - mergedLeaves.size();

	otherLeafCount -= duplicates;

	if (duplicates > 1) {
		cout << "Removed " << duplicates << " duplicate leaves\n";
		cout << "ZOMG NOT READY FOR THIS\n";
		// TODO: update plane references in other BSP when duplicates are removed
	}

	delete[] this->lumps[LUMP_LEAVES];
	this->lumps[LUMP_LEAVES] = new byte[newLen];
	memcpy(this->lumps[LUMP_LEAVES], &mergedLeaves[0], newLen);
	header.lump[LUMP_LEAVES].nLength = newLen;

	cout << thisLeafCount << " -> " << mergedLeaves.size() << endl;
}

void Bsp::merge_marksurfs(Bsp& other) {
	printf("Merging mark surfaces... ");

	uint16* thisMarks = (uint16*)lumps[LUMP_MARKSURFACES];
	uint16* otherMarks = (uint16*)other.lumps[LUMP_MARKSURFACES];
	int thisMarkCount = header.lump[LUMP_MARKSURFACES].nLength / sizeof(uint16);
	int otherMarkCount = other.header.lump[LUMP_MARKSURFACES].nLength / sizeof(uint16);

	vector<uint16> mergedMarks;
	mergedMarks.reserve(thisMarkCount + otherMarkCount);

	for (int i = 0; i < thisMarkCount; i++) {
		mergedMarks.push_back(thisMarks[i]);
	}
	for (int i = 0; i < otherMarkCount; i++) {
		uint16 mark = otherMarks[i];
		mark = facesRemap[mark];

		// TODO: don't remove all duplicates because order matters
		bool isUnique = true;
		for (int k = 0; k < thisMarkCount; k++) {
			if (memcmp(&mark, &thisMarks[k], sizeof(uint16)) == 0) {
				isUnique = false;
				markSurfRemap.push_back(k);
				break;
			}
		}

		if (isUnique) {
			markSurfRemap.push_back(mergedMarks.size());
			mergedMarks.push_back(mark);
		}
	}

	int newLen = mergedMarks.size() * sizeof(uint16);
	int duplicates = mergedMarks.size() - (thisMarkCount + otherMarkCount);

	if (duplicates) {
		cout << "Removed " << duplicates << " duplicate marksurfaces\n";
		cout << "ZOMG NOT READY FOR THIS\n";
		// TODO: update plane references in other BSP when duplicates are removed
	}

	delete[] this->lumps[LUMP_MARKSURFACES];
	this->lumps[LUMP_MARKSURFACES] = new byte[newLen];
	memcpy(this->lumps[LUMP_MARKSURFACES], &mergedMarks[0], newLen);
	header.lump[LUMP_MARKSURFACES].nLength = newLen;

	cout << thisMarkCount << " -> " << mergedMarks.size() << endl;
}

void Bsp::merge_edges(Bsp& other) {
	printf("Merging edges... ");

	BSPEDGE* thisEdges = (BSPEDGE*)lumps[LUMP_EDGES];
	BSPEDGE* otherEdges = (BSPEDGE*)other.lumps[LUMP_EDGES];
	int thisEdgeCount = header.lump[LUMP_EDGES].nLength / sizeof(BSPEDGE);
	int otherEdgeCount = other.header.lump[LUMP_EDGES].nLength / sizeof(BSPEDGE);

	vector<BSPEDGE> mergedEdges;
	mergedEdges.reserve(thisEdgeCount + otherEdgeCount);

	for (int i = 0; i < thisEdgeCount; i++) {
		mergedEdges.push_back(thisEdges[i]);
	}
	for (int i = 0; i < otherEdgeCount; i++) {
		BSPEDGE edge = otherEdges[i];
		edge.iVertex[0] = vertRemap[edge.iVertex[0]];
		edge.iVertex[1] = vertRemap[edge.iVertex[1]];

		bool isUnique = true;
		for (int k = 0; k < thisEdgeCount; k++) {
			if (memcmp(&edge, &thisEdges[k], sizeof(BSPEDGE)) == 0) {
				isUnique = false;
				edgeRemap.push_back(k);
				break;
			}
		}

		if (isUnique) {
			edgeRemap.push_back(mergedEdges.size());
			mergedEdges.push_back(edge);
		}
	}

	int newLen = mergedEdges.size() * sizeof(BSPEDGE);
	int duplicates = mergedEdges.size() - (thisEdgeCount + otherEdgeCount);

	if (duplicates) {
		cout << "Removed " << duplicates << " duplicate edges\n";
		cout << "ZOMG NOT READY FOR THIS\n";
		// TODO: update plane references in other BSP when duplicates are removed
	}

	delete[] this->lumps[LUMP_EDGES];
	this->lumps[LUMP_EDGES] = new byte[newLen];
	memcpy(this->lumps[LUMP_EDGES], &mergedEdges[0], newLen);
	header.lump[LUMP_EDGES].nLength = newLen;

	cout << thisEdgeCount << " -> " << mergedEdges.size() << endl;
}

void Bsp::merge_surfedges(Bsp& other) {
	printf("Merging surface edges... ");

	int32_t* thisSurfs = (int32_t*)lumps[LUMP_SURFEDGES];
	int32_t* otherSurfs = (int32_t*)other.lumps[LUMP_SURFEDGES];
	int thisSurfCount = header.lump[LUMP_SURFEDGES].nLength / sizeof(int32_t);
	int otherSurfCount = other.header.lump[LUMP_SURFEDGES].nLength / sizeof(int32_t);

	vector<int32_t> mergedSurfs;
	mergedSurfs.reserve(thisSurfCount + otherSurfCount);

	for (int i = 0; i < thisSurfCount; i++) {
		mergedSurfs.push_back(thisSurfs[i]);
	}
	for (int i = 0; i < otherSurfCount; i++) {
		int32_t surfEdge = otherSurfs[i];
		surfEdge = surfEdge < 0 ? -edgeRemap[-surfEdge] : edgeRemap[surfEdge];

		// TODO: don't remove all duplicates because order matters
		bool isUnique = true;
		for (int k = 0; k < thisSurfCount; k++) {
			if (memcmp(&surfEdge, &thisSurfs[k], sizeof(int32_t)) == 0) {
				isUnique = false;
				surfEdgeRemap.push_back(k);
				break;
			}
		}

		if (isUnique) {
			surfEdgeRemap.push_back(mergedSurfs.size());
			mergedSurfs.push_back(surfEdge);
		}
	}

	int newLen = mergedSurfs.size() * sizeof(int32_t);
	int duplicates = mergedSurfs.size() - (thisSurfCount + otherSurfCount);

	if (duplicates) {
		cout << "Removed " << duplicates << " duplicate surfedges\n";
		cout << "ZOMG NOT READY FOR THIS\n";
		// TODO: update plane references in other BSP when duplicates are removed
	}

	delete[] this->lumps[LUMP_SURFEDGES];
	this->lumps[LUMP_SURFEDGES] = new byte[newLen];
	memcpy(this->lumps[LUMP_SURFEDGES], &mergedSurfs[0], newLen);
	header.lump[LUMP_SURFEDGES].nLength = newLen;

	cout << thisSurfCount << " -> " << mergedSurfs.size() << endl;
}

void Bsp::merge_nodes(Bsp& other) {
	printf("Merging nodes... ");

	BSPNODE* thisNodes = (BSPNODE*)lumps[LUMP_NODES];
	BSPNODE* otherNodes = (BSPNODE*)other.lumps[LUMP_NODES];
	thisNodeCount = header.lump[LUMP_NODES].nLength / sizeof(BSPNODE);
	int otherNodeCount = other.header.lump[LUMP_NODES].nLength / sizeof(BSPNODE);

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
			node.firstFace = facesRemap[node.firstFace];
		}
		
		mergedNodes.push_back(node);
	}

	int newLen = mergedNodes.size() * sizeof(BSPNODE);

	delete[] this->lumps[LUMP_NODES];
	this->lumps[LUMP_NODES] = new byte[newLen];
	memcpy(this->lumps[LUMP_NODES], &mergedNodes[0], newLen);
	header.lump[LUMP_NODES].nLength = newLen;

	cout << thisNodeCount << " -> " << mergedNodes.size() << endl;
}

void Bsp::merge_clipnodes(Bsp& other) {
	printf("Merging clipnodes... ");

	BSPCLIPNODE* thisNodes = (BSPCLIPNODE*)lumps[LUMP_CLIPNODES];
	BSPCLIPNODE* otherNodes = (BSPCLIPNODE*)other.lumps[LUMP_CLIPNODES];
	thisClipnodeCount = header.lump[LUMP_CLIPNODES].nLength / sizeof(BSPCLIPNODE);
	int otherClipnodeCount = other.header.lump[LUMP_CLIPNODES].nLength / sizeof(BSPCLIPNODE);

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
	}

	int newLen = mergedNodes.size() * sizeof(BSPCLIPNODE);

	delete[] this->lumps[LUMP_CLIPNODES];
	this->lumps[LUMP_CLIPNODES] = new byte[newLen];
	memcpy(this->lumps[LUMP_CLIPNODES], &mergedNodes[0], newLen);
	header.lump[LUMP_CLIPNODES].nLength = newLen;

	cout << thisClipnodeCount << " -> " << mergedNodes.size() << endl;
}

void Bsp::merge_models(Bsp& other) {
	printf("Merging models... ");

	BSPMODEL* thisModels = (BSPMODEL*)lumps[LUMP_MODELS];
	BSPMODEL* otherModels = (BSPMODEL*)other.lumps[LUMP_MODELS];
	int thisModelCount = header.lump[LUMP_MODELS].nLength / sizeof(BSPMODEL);
	int otherModelCount = other.header.lump[LUMP_MODELS].nLength / sizeof(BSPMODEL);

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
		model.iFirstFace = facesRemap[model.iFirstFace];
		mergedModels.push_back(model);
	}

	// this map's submodels
	for (int i = 1; i < thisModelCount; i++) {
		BSPMODEL model = thisModels[i];
		model.iHeadnodes[0] += 1; // adjust for new head node
		for (int k = 1; k < MAX_MAP_HULLS; k++) {
			model.iHeadnodes[k] += (MAX_MAP_HULLS-1); // adjust for new head nodes
		}
		mergedModels.push_back(model);
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

	cout << thisModelCount << " -> " << mergedModels.size() << endl;
}

#define hlassume(expr, err) if (!(expr)) {printf(#err "\n");}

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

		DecompressVis((const byte*)(visLump + leafLump[i+1].nVisOffset), dest, oldVisRowSize, visDataLeafCount);
	
		if (shiftAmount) {
			shiftVis((uint64*)dest, newVisRowSize, shiftOffsetBit, shiftAmount);
		}
	}
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

int CompressAll(BSPLEAF* leafs, byte* uncompressed, byte* output, int numLeaves)
{
	int i, x = 0;
	byte* dest;
	byte* src;
	byte compressed[MAX_MAP_LEAVES / 8];
	uint g_bitbytes = ((numLeaves + 63) & ~63) >> 3;
	int len = 0;

	byte* vismap_p = output;

	for (i = 0; i < numLeaves; i++)
	{
		memset(&compressed, 0, sizeof(compressed));

		src = uncompressed + i * g_bitbytes;

		// Compress all leafs into global compression buffer
		x = CompressVis(src, g_bitbytes, compressed, sizeof(compressed));

		dest = vismap_p;
		vismap_p += x;

		/*
		if (vismap_p > vismap_end)
		{
			Error("Vismap expansion overflow");
		}
		*/

		leafs[i + 1].nVisOffset = dest - output;            // leaf 0 is a common solid

		memcpy(dest, compressed, x);
		len += x;
	}
	return len;
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

void debug_vis(byte* vis, BSPLEAF* leafs, int visLeafCount) {
	uint g_bitbytes = ((visLeafCount + 63) & ~63) >> 3;

	byte* decompressed = new byte[g_bitbytes];
	byte* compressed = new byte[g_bitbytes];

	for (int i = 0; i < visLeafCount; i++) {
		printf("LEAF %02d: ", i + 1);

		memset(decompressed, 0, g_bitbytes);
		memset(compressed, 0, g_bitbytes);

		DecompressVis((const unsigned char*)(vis + leafs[i + 1].nVisOffset), decompressed, g_bitbytes, visLeafCount);
		for (int k = 0; k < g_bitbytes; k++) {
			printf("%3d ", decompressed[k]);
		}


		if (i < visLeafCount - 1) {
			int oldCompressLen = leafs[i+2].nVisOffset - leafs[i+1].nVisOffset;
			memcpy(compressed, vis + leafs[i+1].nVisOffset, oldCompressLen);

			printf("\n         ");
			for (int k = 0; k < oldCompressLen; k++) {
				printf("%3d ", compressed[k]);
			}
		}
		
		int compressLen = CompressVis(decompressed, 11, compressed, sizeof(compressed));

		printf("\n         ");
		for (int k = 0; k < compressLen; k++) {
			printf("%3d ", compressed[k]);
		}
		printf("\n");
	}
}

void Bsp::merge_vis(Bsp& other) {
	printf("Merging VIS data... ");

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

	cout << oldLen << " -> " << newVisLen << endl;
}

void Bsp::merge_lighting(Bsp& other) {
	printf("Merging lightmaps... ");

	COLOR3* thisRad = (COLOR3*)lumps[LUMP_LIGHTING];
	COLOR3* otherRad = (COLOR3*)other.lumps[LUMP_LIGHTING];
	int thisColorCount = header.lump[LUMP_LIGHTING].nLength / sizeof(COLOR3);
	int otherColorCount = other.header.lump[LUMP_LIGHTING].nLength / sizeof(COLOR3);
	int totalColorCount = thisColorCount + otherColorCount;

	COLOR3* newRad = new COLOR3[totalColorCount];
	memcpy(newRad, thisRad, thisColorCount * sizeof(COLOR3));
	memcpy((byte*)newRad + thisColorCount * sizeof(COLOR3), otherRad, otherColorCount * sizeof(COLOR3));


	delete[] this->lumps[LUMP_LIGHTING];
	this->lumps[LUMP_LIGHTING] = (byte*)newRad;
	int oldLen = header.lump[LUMP_LIGHTING].nLength;
	header.lump[LUMP_LIGHTING].nLength = totalColorCount*sizeof(COLOR3);

	BSPFACE* faces = (BSPFACE*)lumps[LUMP_FACES];
	int totalFaceCount = header.lump[LUMP_FACES].nLength / sizeof(BSPFACE);

	for (int i = thisFaceCount; i < totalFaceCount; i++) {
		faces[i].nLightmapOffset += thisColorCount*sizeof(COLOR3);
	}

	cout << oldLen << " -> " << header.lump[LUMP_LIGHTING].nLength << endl;
}

BSPPLANE Bsp::separate(Bsp& other) {
	BSPMODEL& thisWorld = ((BSPMODEL*)lumps[LUMP_MODELS])[0];
	BSPMODEL& otherWorld = ((BSPMODEL*)other.lumps[LUMP_MODELS])[0];

	vec3 amin = thisWorld.nMins;
	vec3 amax = thisWorld.nMaxs;
	vec3 bmin = otherWorld.nMins;
	vec3 bmax = otherWorld.nMaxs;
	
	printf("Bounding boxes for each map:\n");
	printf("(%6.0f, %6.0f, %6.0f)", amin.x, amin.y, amin.z);
	printf(" - (%6.0f, %6.0f, %6.0f) %s\n", amax.x, amax.y, amax.z, this->name.c_str());

	printf("(%6.0f, %6.0f, %6.0f)", bmin.x, bmin.y, bmin.z);
	printf(" - (%6.0f, %6.0f, %6.0f) %s\n", bmax.x, bmax.y, bmax.z, other.name.c_str());

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

	printf("Separating plane: (%.0f, %.0f, %.0f) %.0f\n", separationPlane.vNormal.x, separationPlane.vNormal.y, separationPlane.vNormal.z, separationPlane.fDist);

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
			printf("HULL %d starts at %d\n", i+1, thisWorld.iHeadnodes[i+1]);
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
	printf("Moving BSP...\n");

	BSPPLANE* planes = (BSPPLANE*)lumps[LUMP_PLANES];
	BSPTEXTUREINFO* texInfo = (BSPTEXTUREINFO*)lumps[LUMP_TEXINFO];
	BSPLEAF* leaves = (BSPLEAF*)lumps[LUMP_LEAVES];
	BSPMODEL* models = (BSPMODEL*)lumps[LUMP_MODELS];
	BSPNODE* nodes = (BSPNODE*)lumps[LUMP_NODES];
	vec3* verts = (vec3*)lumps[LUMP_VERTICES];
	
	int planeCount = header.lump[LUMP_PLANES].nLength / sizeof(BSPPLANE);
	int texInfoCount = header.lump[LUMP_TEXINFO].nLength / sizeof(BSPTEXTUREINFO);
	int leafCount = header.lump[LUMP_LEAVES].nLength / sizeof(BSPLEAF);
	int modelCount = header.lump[LUMP_MODELS].nLength / sizeof(BSPMODEL);
	int nodeCount = header.lump[LUMP_NODES].nLength / sizeof(BSPNODE);
	int vertCount = header.lump[LUMP_VERTICES].nLength / sizeof(vec3);

	
	for (int i = 0; i < planeCount; i++) {
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
		BSPTEXTUREINFO& info = texInfo[i];
		
		int32_t texOffset = ((int32_t*)textures)[info.iMiptex];
		BSPMIPTEX& tex = *((BSPMIPTEX*)(textures + texOffset));

		float scaleS = info.vS.length();
		float scaleT = info.vT.length();
		vec3 nS = info.vS.normalize();
		vec3 nT = info.vT.normalize();

		vec3 newOriS = offset + (nS * info.shiftS);
		vec3 newOriT = offset + (nT * info.shiftT);

		float shiftScaleS = dotProduct(offsetDir, nS);
		float shiftScaleT = dotProduct(offsetDir, nT);

		info.shiftS -= shiftScaleS * offsetLen*scaleS;
		info.shiftT -= shiftScaleT * offsetLen*scaleT;

		/*
		while (fabs(info.shiftS) > tex.nWidth) {
			info.shiftS = (info.shiftS < 0) ? (int)tex.nWidth : -(int)(tex.nWidth);
		}
		while (fabs(info.shiftT) > tex.nHeight) {
			info.shiftT += (info.shiftT < 0) ? (int)tex.nHeight : -(int)(tex.nHeight);
		}
		*/
	}
	
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

	for (int i = 0; i < modelCount; i++) {
		BSPMODEL& model = models[i];

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

	for (int i = 0; i < vertCount; i++) {
		vec3& vert = verts[i];

		vert += offset;

		if (fabs(vert.x) > MAX_MAP_COORD ||
			fabs(vert.y) > MAX_MAP_COORD ||
			fabs(vert.z) > MAX_MAP_COORD) {
			printf("WARNING: Vertex moved past safe world boundary!");
		}
	}

	for (int i = 0; i < ents.size(); i++) {
		if (!ents[i]->hasKey("origin"))
			continue;

		Keyvalue keyvalue("origin", ents[i]->keyvalues["origin"]);
		vec3 ori = keyvalue.getVector();
		ori += offset;

		string parts[3] = { to_string(ori.x) , to_string(ori.y), to_string(ori.z) };

		// remove trailing zeros to save some space
		for (int i = 0; i < 3; i++) {
			parts[i].erase(parts[i].find_last_not_of('0') + 1, std::string::npos);

			// strip dot if there's no fractional part
			if (parts[i][parts[i].size() - 1] == '.') {
				parts[i] = parts[i].substr(0, parts[i].size() - 1);
			}
		}

		ents[i]->keyvalues["origin"] = parts[0] + " " + parts[1] + " " + parts[2];
	}

	update_ent_lump();

	return true;
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

#define FULLNESS

void Bsp::print_stat(string name, uint val, uint max, bool isMem) {
	const float meg = 1024 * 1024;
	if (isMem) {
		printf("%-12s  %8.1f / %-5.1f MB  %6.1f%%\n", name.c_str(), val/meg, max/meg, (val / (float)max) * 100);
	}
	else {
		printf("%-12s  %8u / %-8u  %6.1f%%\n", name.c_str(), val, max, (val / (float)max) * 100);
	}
	
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