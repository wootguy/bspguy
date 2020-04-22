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

void Bsp::merge(Bsp& other) {
	cout << "Merging " << other.path << " into " << path << endl;

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
			cout << "Merging " << g_lump_names[i] << " lump\n";

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

	if (shouldMerge[LUMP_VISIBILITY])
		merge_vis(other);

	if (shouldMerge[LUMP_NODES]) {
		separate(other);
		merge_nodes(other);
		merge_clipnodes(other);
	}		

	if (shouldMerge[LUMP_MODELS])
		merge_models(other);

	if (shouldMerge[LUMP_LIGHTING])
		merge_lighting(other);
}

void Bsp::merge_ents(Bsp& other)
{
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
					cout << "Adding " << otherWads[j] << "\n";
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

	stringstream ent_data;

	for (int i = 0; i < ents.size(); i++) {
		ent_data << "{\n";

		for (auto it = ents[i]->keyvalues.begin(); it != ents[i]->keyvalues.end(); it++) {
			ent_data << "\"" << it->first << "\" \"" << it->second << "\"\n";
		}

		ent_data << "}\n";
	}

	string str_data = ent_data.str();

	delete [] lumps[LUMP_ENTITIES];
	header.lump[LUMP_ENTITIES].nLength = str_data.size();
	lumps[LUMP_ENTITIES] = new byte[str_data.size()];
	memcpy((char*)lumps[LUMP_ENTITIES], str_data.c_str(), str_data.size());
}

void Bsp::merge_planes(Bsp& other) {
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

	cout << "Removed " << duplicates << " duplicate planes\n";

	if (duplicates) {
		cout << "ZOMG NOT READY FOR THIS\n";
		// TODO: update plane references in other BSP when duplicates are removed
	}

	delete [] this->lumps[LUMP_PLANES];
	this->lumps[LUMP_PLANES] = new byte[newLen];
	memcpy(this->lumps[LUMP_PLANES], &mergedPlanes[0], newLen);
	header.lump[LUMP_PLANES].nLength = newLen;

	cout << "planes: " << numThisPlanes << " -> " << mergedPlanes.size() << endl;
}

void Bsp::merge_textures(Bsp& other) {
	uint32_t thisTexCount =  (uint32_t)(lumps[LUMP_TEXTURES])[0];
	uint32_t otherTexCount = (uint32_t)(other.lumps[LUMP_TEXTURES])[0];
	BSPMIPTEX* thisTex = (BSPMIPTEX*)(lumps[LUMP_TEXTURES] + sizeof(uint32_t)*(thisTexCount+1));
	BSPMIPTEX* otherTex = (BSPMIPTEX*)(other.lumps[LUMP_TEXTURES] + sizeof(uint32_t)*(otherTexCount+1));

	uint32_t newTexCount = thisTexCount + otherTexCount;

	vector<BSPMIPTEX> mergedTex;

	for (int i = 0; i < thisTexCount; i++) {
		if (thisTex[i].nOffsets[0] != 0) {
			cout << "0ZOMG " << thisTex[i].szName << " STORED IN BSP ZOMG " << thisTex[i].nOffsets[0] << endl;
		}
		
		mergedTex.push_back(thisTex[i]);
	}

	for (int i = 0; i < otherTexCount; i++) {
		if (otherTex[i].nOffsets[0] != 0) {
			cout << "1ZOMG " << otherTex[i].szName << " STORED IN BSP ZOMG " << otherTex[i].nOffsets[0] << endl;
		}

		bool isUnique = true;
		for (int k = 0; k < thisTexCount; k++) {
			if (memcmp(&otherTex[i], &thisTex[k], sizeof(BSPMIPTEX)) == 0) {
				isUnique = false;
				texRemap.push_back(k);
				break;
			}
		}
		
		if (isUnique) {
			texRemap.push_back(mergedTex.size());
			mergedTex.push_back(otherTex[i]);
		}
	}

	uint32_t texHeaderSize = sizeof(uint32_t) * (newTexCount + 1);

	int newLen = mergedTex.size() * sizeof(BSPMIPTEX) + texHeaderSize;
	int duplicates = mergedTex.size() - (thisTexCount + otherTexCount);

	cout << "Removed " << duplicates << " duplicate textures\n";

	if (duplicates) {
		cout << "ZOMG NOT READY FOR THIS\n";
		// TODO: update plane references in other BSP when duplicates are removed
	}

	delete[] this->lumps[LUMP_TEXTURES];
	this->lumps[LUMP_TEXTURES] = new byte[newLen];

	// write texture lump header
	uint32_t* texHeader = (uint32_t*)(this->lumps[LUMP_TEXTURES]);
	texHeader[0] = newTexCount;
	for (int i = 0; i < newTexCount; i++) {
		texHeader[i+1] = texHeaderSize + sizeof(BSPMIPTEX) * i;
	}

	memcpy(this->lumps[LUMP_TEXTURES] + texHeaderSize, &mergedTex[0], sizeof(BSPMIPTEX)*mergedTex.size());
	header.lump[LUMP_TEXTURES].nLength = newLen;

	cout << "textures: " << thisTexCount << " -> " << newTexCount << endl;
}

void Bsp::merge_vertices(Bsp& other) {
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

	cout << "Removed " << duplicates << " duplicate verts\n";

	if (duplicates) {
		cout << "ZOMG NOT READY FOR THIS\n";
		// TODO: update plane references in other BSP when duplicates are removed
	}

	delete[] this->lumps[LUMP_VERTICES];
	this->lumps[LUMP_VERTICES] = new byte[newLen];
	memcpy(this->lumps[LUMP_VERTICES], &mergedVerts[0], newLen);
	header.lump[LUMP_VERTICES].nLength = newLen;

	cout << "vertices: " << thisVertsCount << " -> " << mergedVerts.size() << endl;
}

void Bsp::merge_texinfo(Bsp& other) {
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

	cout << "Removed " << duplicates << " duplicate texinfos\n";

	if (duplicates) {
		cout << "ZOMG NOT READY FOR THIS\n";
		// TODO: update plane references in other BSP when duplicates are removed
	}

	delete[] this->lumps[LUMP_TEXINFO];
	this->lumps[LUMP_TEXINFO] = new byte[newLen];
	memcpy(this->lumps[LUMP_TEXINFO], &mergedInfo[0], newLen);
	header.lump[LUMP_TEXINFO].nLength = newLen;

	cout << "texinfo: " << thisInfoCount << " -> " << mergedInfo.size() << endl;
}

void Bsp::merge_faces(Bsp& other) {
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
		//face.nLightmapOffset = 0; // TODO: lightmap remap
		

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

	cout << "Removed " << duplicates << " duplicate faces\n";

	if (duplicates) {
		cout << "ZOMG NOT READY FOR THIS\n";
		// TODO: update plane references in other BSP when duplicates are removed
	}

	delete[] this->lumps[LUMP_FACES];
	this->lumps[LUMP_FACES] = new byte[newLen];
	memcpy(this->lumps[LUMP_FACES], &mergedFaces[0], newLen);
	header.lump[LUMP_FACES].nLength = newLen;

	cout << "faces: " << thisFaceCount << " -> " << mergedFaces.size() << endl;
}

void Bsp::merge_leaves(Bsp& other) {
	BSPLEAF* thisLeaves = (BSPLEAF*)lumps[LUMP_LEAVES];
	BSPLEAF* otherLeaves = (BSPLEAF*)other.lumps[LUMP_LEAVES];
	thisLeafCount = header.lump[LUMP_LEAVES].nLength / sizeof(BSPLEAF);
	otherLeafCount = other.header.lump[LUMP_LEAVES].nLength / sizeof(BSPLEAF);

	vector<BSPLEAF> mergedLeaves;
	mergedLeaves.reserve(thisLeafCount + otherLeafCount);

	for (int i = 0; i < thisLeafCount; i++) {
		mergedLeaves.push_back(thisLeaves[i]);
	}

	for (int i = 0; i < otherLeafCount; i++) {
		BSPLEAF& leaf = otherLeaves[i];
		if (leaf.nMarkSurfaces) {
			leaf.iFirstMarkSurface = markSurfRemap[leaf.iFirstMarkSurface];
		}

		bool isUnique = true;
		for (int k = 0; k < thisLeafCount; k++) {
			if (memcmp(&leaf, &thisLeaves[k], sizeof(BSPLEAF)) == 0) {
				isUnique = false;
				leavesRemap.push_back(k);
				break;
			}
		}

		if (isUnique) {
			leavesRemap.push_back(mergedLeaves.size());
			mergedLeaves.push_back(leaf);
		}
	}

	int newLen = mergedLeaves.size() * sizeof(BSPLEAF);
	int duplicates = (thisLeafCount + otherLeafCount) - mergedLeaves.size();

	otherLeafCount -= duplicates;

	cout << "Removed " << duplicates << " duplicate leaves\n";

	if (duplicates) {
		cout << "ZOMG NOT READY FOR THIS\n";
		// TODO: update plane references in other BSP when duplicates are removed
	}

	delete[] this->lumps[LUMP_LEAVES];
	this->lumps[LUMP_LEAVES] = new byte[newLen];
	memcpy(this->lumps[LUMP_LEAVES], &mergedLeaves[0], newLen);
	header.lump[LUMP_LEAVES].nLength = newLen;

	cout << "leaves: " << thisLeafCount << " -> " << mergedLeaves.size() << endl;
}

void Bsp::merge_marksurfs(Bsp& other) {
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

	cout << "Removed " << duplicates << " duplicate marksurfaces\n";

	if (duplicates) {
		cout << "ZOMG NOT READY FOR THIS\n";
		// TODO: update plane references in other BSP when duplicates are removed
	}

	delete[] this->lumps[LUMP_MARKSURFACES];
	this->lumps[LUMP_MARKSURFACES] = new byte[newLen];
	memcpy(this->lumps[LUMP_MARKSURFACES], &mergedMarks[0], newLen);
	header.lump[LUMP_MARKSURFACES].nLength = newLen;

	cout << "marksurfaces: " << thisMarkCount << " -> " << mergedMarks.size() << endl;
}

void Bsp::merge_edges(Bsp& other) {
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

	cout << "Removed " << duplicates << " duplicate edges\n";

	if (duplicates) {
		cout << "ZOMG NOT READY FOR THIS\n";
		// TODO: update plane references in other BSP when duplicates are removed
	}

	delete[] this->lumps[LUMP_EDGES];
	this->lumps[LUMP_EDGES] = new byte[newLen];
	memcpy(this->lumps[LUMP_EDGES], &mergedEdges[0], newLen);
	header.lump[LUMP_EDGES].nLength = newLen;

	cout << "edges: " << thisEdgeCount << " -> " << mergedEdges.size() << endl;
}

void Bsp::merge_surfedges(Bsp& other) {
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

	cout << "Removed " << duplicates << " duplicate surfedges\n";

	if (duplicates) {
		cout << "ZOMG NOT READY FOR THIS\n";
		// TODO: update plane references in other BSP when duplicates are removed
	}

	delete[] this->lumps[LUMP_SURFEDGES];
	this->lumps[LUMP_SURFEDGES] = new byte[newLen];
	memcpy(this->lumps[LUMP_SURFEDGES], &mergedSurfs[0], newLen);
	header.lump[LUMP_SURFEDGES].nLength = newLen;

	cout << "surfedges: " << thisSurfCount << " -> " << mergedSurfs.size() << endl;
}

void Bsp::merge_nodes(Bsp& other) {
	BSPNODE* thisNodes = (BSPNODE*)lumps[LUMP_NODES];
	BSPNODE* otherNodes = (BSPNODE*)other.lumps[LUMP_NODES];
	int thisNodeCount = header.lump[LUMP_NODES].nLength / sizeof(BSPNODE);
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
		node.firstFace = facesRemap[node.firstFace];

		mergedNodes.push_back(node);
	}

	int newLen = mergedNodes.size() * sizeof(BSPNODE);

	delete[] this->lumps[LUMP_NODES];
	this->lumps[LUMP_NODES] = new byte[newLen];
	memcpy(this->lumps[LUMP_NODES], &mergedNodes[0], newLen);
	header.lump[LUMP_NODES].nLength = newLen;

	cout << "nodes: " << thisNodeCount << " -> " << mergedNodes.size() << endl;
}

void Bsp::merge_clipnodes(Bsp& other) {
	BSPCLIPNODE* thisNodes = (BSPCLIPNODE*)lumps[LUMP_CLIPNODES];
	BSPCLIPNODE* otherNodes = (BSPCLIPNODE*)other.lumps[LUMP_CLIPNODES];
	int thisNodeCount = header.lump[LUMP_CLIPNODES].nLength / sizeof(BSPCLIPNODE);
	int otherNodeCount = other.header.lump[LUMP_CLIPNODES].nLength / sizeof(BSPCLIPNODE);

	vector<BSPCLIPNODE> mergedNodes;
	mergedNodes.reserve(thisNodeCount + otherNodeCount);

	for (int i = 0; i < thisNodeCount; i++) {
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

	for (int i = 0; i < otherNodeCount; i++) {
		BSPCLIPNODE node = otherNodes[i];
		node.iPlane = planeRemap[node.iPlane];

		for (int k = 0; k < 2; k++) {
			if (node.iChildren[k] >= 0) {
				node.iChildren[k] += thisNodeCount;
			}
		}
		mergedNodes.push_back(node);
	}

	int newLen = mergedNodes.size() * sizeof(BSPCLIPNODE);

	delete[] this->lumps[LUMP_CLIPNODES];
	this->lumps[LUMP_CLIPNODES] = new byte[newLen];
	memcpy(this->lumps[LUMP_CLIPNODES], &mergedNodes[0], newLen);
	header.lump[LUMP_CLIPNODES].nLength = newLen;

	cout << "clipnodes: " << thisNodeCount << " -> " << mergedNodes.size() << endl;
}

void Bsp::merge_models(Bsp& other) {
	BSPMODEL* thisModels = (BSPMODEL*)lumps[LUMP_MODELS];
	BSPMODEL* otherModels = (BSPMODEL*)other.lumps[LUMP_MODELS];
	int thisModelCount = header.lump[LUMP_MODELS].nLength / sizeof(BSPMODEL);
	int otherModelCount = other.header.lump[LUMP_MODELS].nLength / sizeof(BSPMODEL);

	vector<BSPMODEL> mergedModels;
	mergedModels.reserve(thisModelCount + otherModelCount);

	for (int i = 0; i < thisModelCount; i++) {
		mergedModels.push_back(thisModels[i]);
	}

	// skip first model because there can only be one "world" model
	for (int i = 1; i < otherModelCount; i++) {
		BSPMODEL node = otherModels[i];
		mergedModels.push_back(node);
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

	cout << "models: " << thisModelCount << " -> " << mergedModels.size() << endl;
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

void shiftVis(byte* vis, int len, int shift) {

	for (int k = 0; k < shift; k++) {
		bool carry = 0;
		for (int i = 0; i < len; i++) {
			byte oldCarry = carry;
			carry = (vis[i] & 0x80) != 0;
			vis[i] = (vis[i] << 1) + oldCarry;
		}

		if (carry) {
			printf("ZOMG OVERFLOW");
		}
	}
}

int DecompressAll(BSPLEAF* leafLump, byte* visLump, byte* output, int numLeaves, int newNumLeaves, bool prependNewLeaves)
{
	byte* dest;
	uint oldBitbytes = ((numLeaves + 63) & ~63) >> 3;
	uint newBitbytes = ((newNumLeaves + 63) & ~63) >> 3;
	int len = 0;

	for (int i = 0; i < numLeaves; i++)
	{
		dest = output + i * newBitbytes;

		if (prependNewLeaves) {

		}

		DecompressVis((const unsigned char*)(visLump + (byte)leafLump[i+1].nVisOffset), dest, oldBitbytes, numLeaves);
	
		if (prependNewLeaves) {
			shiftVis(dest, newBitbytes, newNumLeaves - numLeaves);
		}
	}

	return numLeaves* newBitbytes;
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
	byte compressed[MAX_MAP_LEAFS / 8];
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

void print_vis(byte* vis, int visLeafCount) {
	uint g_bitbytes = ((visLeafCount + 63) & ~63) >> 3;
	for (int k = 0; k < visLeafCount; k++) {
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
	int totalLeaves = thisLeafCount + otherLeafCount;

	byte* decompressedVis = new byte[65535];
	memset(decompressedVis, 0, 65535);

	// decompress this map's vis data (skip empty first leaf)
	int thisDecompLen = DecompressAll(allLeaves, thisVis, decompressedVis, thisLeafCount-1, totalLeaves-1, false);
	//cout << "Decompressed this vis:\n";
	//print_vis(decompressedVis, thisLeafCount-1);

	// decompress other map's vis data (skip empty first leaf, which now only the first map should have)
	byte* decompressedOtherVis = decompressedVis + thisDecompLen;
	int otherDecompLen = DecompressAll(allLeaves + (thisLeafCount - 1), otherVis, decompressedOtherVis, otherLeafCount, totalLeaves-1, true);
	//cout << "Decompressed other vis:\n";
	//print_vis(decompressedOtherVis, otherLeafCount);

	// recompress the combined vis data
	byte* compressedVis = new byte[65535];
	memset(compressedVis, 0, 65535);
	int newVisLen = CompressAll(allLeaves, decompressedVis, compressedVis, totalLeaves-1);

	delete[] this->lumps[LUMP_VISIBILITY];
	this->lumps[LUMP_VISIBILITY] = new byte[newVisLen];
	memcpy(this->lumps[LUMP_VISIBILITY], compressedVis, newVisLen);
	header.lump[LUMP_VISIBILITY].nLength = newVisLen;
}

void Bsp::merge_lighting(Bsp& other) {
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
	header.lump[LUMP_LIGHTING].nLength = totalColorCount*sizeof(COLOR3);

	BSPFACE* faces = (BSPFACE*)lumps[LUMP_FACES];
	int totalFaceCount = header.lump[LUMP_FACES].nLength / sizeof(BSPFACE);

	for (int i = thisFaceCount; i < totalFaceCount; i++) {
		faces[i].nLightmapOffset += thisColorCount*sizeof(COLOR3);
	}
}

bool Bsp::separate(Bsp& other) {
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

	// planes with negative normals mess up VIS and lighting stuff, so swap children instead
	bool swapNodeChildren = false;

	// separating plane points toward the other map (b)
	if (bmin.x >= amax.x) {
		separationPlane.nType = PLANE_X;
		separationPlane.vNormal = {1, 0, 0};
		separationPlane.fDist = amax.x + (bmin.x - amax.x)*0.5f;
	}
	else if (bmax.x <= amin.x) {
		separationPlane.nType = PLANE_X;
		separationPlane.vNormal = { 1, 0, 0 };
		separationPlane.fDist = bmax.x + (amin.x - bmax.x) * 0.5f;
		swapNodeChildren = true;
	}
	else if (bmin.y >= amax.y) {
		separationPlane.nType = PLANE_Y;
		separationPlane.vNormal = { 0, 1, 0 };
		separationPlane.fDist = bmin.y;
	}
	else if (bmax.y <= amin.y) {
		separationPlane.nType = PLANE_Y;
		separationPlane.vNormal = { 0, 1, 0 };
		separationPlane.fDist = bmax.y;
		swapNodeChildren = true;
	}
	else if (bmin.z >= amax.z) {
		separationPlane.nType = PLANE_Z;
		separationPlane.vNormal = { 0, 0, 1 };
		separationPlane.fDist = bmin.z;
	}
	else if (bmax.z <= amin.z) {
		separationPlane.nType = PLANE_Z;
		separationPlane.vNormal = { 0, 0, 1 };
		separationPlane.fDist = bmax.z;
		swapNodeChildren = true;
	}
	else {
		separationPlane.nType = -1; // no simple separating axis
	}

	if (separationPlane.nType == -1) {
		printf("No separating axis found. The maps overlap and can't be merged.\n");
		return false;
	}

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
					(int16_t)(thisWorld.iHeadnodes[i + 1] + NEW_NODE_COUNT),
					(int16_t)(otherWorld.iHeadnodes[i + 1] + numThisNodes + NEW_NODE_COUNT)
				},
			};
		}

		BSPCLIPNODE* newThisNodes = new BSPCLIPNODE[numThisNodes + NEW_NODE_COUNT];
		memcpy(newThisNodes, newHeadNodes, NEW_NODE_COUNT * sizeof(BSPCLIPNODE));
		memcpy(newThisNodes + NEW_NODE_COUNT, thisNodes, numThisNodes * sizeof(BSPCLIPNODE));

		delete[] this->lumps[LUMP_CLIPNODES];
		this->lumps[LUMP_CLIPNODES] = (byte*)newThisNodes;
		header.lump[LUMP_CLIPNODES].nLength = (numThisNodes + NEW_NODE_COUNT) * sizeof(BSPCLIPNODE);
	}

	return true;
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

void Bsp::print_bsp() {
	BSPMODEL* models = (BSPMODEL*)lumps[LUMP_MODELS];
	BSPMODEL world = models[0];

	
	int node = world.iHeadnodes[0];
	cout << "Head node: " << node << endl;

	recurse_node(node, 0);
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
/*
int Bsp::PM_HullPointContents(hull_t* hull, int num, const vec3_t p)
{
	mplane_t* plane;

	if (!hull || !hull->planes)	// fantom bmodels?
		return CONTENTS_NONE;

	while (num >= 0)
	{
		plane = &hull->planes[hull->clipnodes[num].planenum];
		num = hull->clipnodes[num].children[PlaneDiff(p, plane) < 0];
	}
	return num;
}
*/

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