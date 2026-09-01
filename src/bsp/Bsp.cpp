#include "Bsp.h"
#include "util.h"
#include "Entity.h"
#include "globals.h"
#include "Editor.h"
#include "Fgd.h"

#include <fstream>
#include <algorithm>
#include <unordered_map>

typedef unordered_map< string, vec3 > mapStringToVector;

vec3 default_hull_extents[MAX_MAP_HULLS] = {
	vec3(0,  0,  0),	// hull 0
	vec3(16, 16, 36),	// hull 1
	vec3(32, 32, 64),	// hull 2
	vec3(16, 16, 18)	// hull 3
};

int g_sort_mode = SORT_CLIPNODES;

Bsp::Bsp() {
	lumps = new byte * [HEADER_LUMPS];

	header.nVersion = 30;

	for (int i = 0; i < HEADER_LUMPS; i++) {
		header.lump[i].nOffset = 0;
		if (i == LUMP_TEXTURES)
		{
			lumps[i] = new byte[4];
			header.lump[i].nLength = 4;
			memset(lumps[i], 0, header.lump[i].nLength);
		}
		else if (i == LUMP_LIGHTING)
		{
			lumps[i] = new byte[4096];
			header.lump[i].nLength = 4096;
			memset(lumps[i], 255, header.lump[i].nLength);
		}
		else 
		{
			lumps[i] = new byte[0];
			header.lump[i].nLength = 0;
		}
	}

	update_lump_pointers();
	name = "merged";
	valid = true;
}

Bsp::Bsp(const Bsp& other) {
	header = other.header;
	lumps = new byte*[HEADER_LUMPS];
	path = other.path;
	name = other.name;

	for (int i = 0; i < HEADER_LUMPS; i++) {
		lumps[i] = new byte[header.lump[i].nLength];
		memcpy(lumps[i], other.lumps[i], header.lump[i].nLength);
	}

	load_ents(lumps[LUMP_ENTITIES], header.lump[LUMP_ENTITIES].nLength, ents);
	update_lump_pointers();

	valid = true;
}

Bsp::Bsp(std::string fpath)
{
	lumps = NULL;

	if (fpath.size() < 4 || toLowerCase(fpath).rfind(".bsp") != fpath.size() - 4) {
		fpath = fpath + ".bsp";
	}
	this->path = fpath;
	this->name = stripExt(basename(fpath));
	valid = false;

	bool exists = true;
	if (!fileExists(fpath)) {
		errorf("%s not found\n", fpath.c_str());
		return;
	}

	LumpState state;
	if (!load_lumps(fpath, header, state)) {
		errorf("%s is not a valid BSP file\n", fpath.c_str());
		return;
	}

	lumps = new byte*[HEADER_LUMPS];
	for (int i = 0; i < HEADER_LUMPS; i++) {
		lumps[i] = state.lumps[i];
		header.lump[i].nLength = state.lumpLen[i];
	}

	load_ents(lumps[LUMP_ENTITIES], header.lump[LUMP_ENTITIES].nLength, ents);
	update_lump_pointers();

	valid = true;
}

Bsp::~Bsp()
{	 
	if (lumps) {
		for (int i = 0; i < HEADER_LUMPS; i++)
			if (lumps[i]) {
				delete[] lumps[i];
			}
		delete[] lumps;
		lumps = NULL;
	}

	for (int i = 0; i < ents.size(); i++) {
		delete ents[i];
		ents[i] = NULL;
	}

	if (pvsFaces) {
		delete[] pvsFaces;
		pvsFaces = NULL;
	}
}

void Bsp::get_bounding_box(vec3& mins, vec3& maxs) {
	BSPMODEL& thisWorld = models[0];

	// the model bounds are little bigger than the actual vertices bounds in the map,
	// but if you go by the vertices then there will be collision problems.

	mins = thisWorld.nMins;
	maxs = thisWorld.nMaxs;

	if (ents.size() && ents[0]->hasKey("origin")) {
		vec3 origin = ents[0]->getOrigin();
		mins += origin;
		maxs += origin;
	}
}

bool Bsp::move(vec3 offset, int modelIdx) {
	if (modelIdx < 0 || modelIdx >= modelCount) {
		logf("Invalid modelIdx moved");
		return false;
	}

	BSPMODEL& target = models[modelIdx];

	// all ents should be moved if the world is being moved
	bool movingWorld = modelIdx == 0;

	// Submodels don't use leaves like the world model does. Only the contents of a leaf matters
	// for submodels. All other data is ignored. bspguy will reuse world leaves in submodels to 
	// save space, which means moving leaves for those models would likely break something else.
	// So, don't move leaves for submodels.
	bool dontMoveLeaves = !movingWorld;

	split_shared_model_structures(modelIdx);

	bool hasLighting = lightDataLength > 0;
	LIGHTMAP* oldLightmaps = NULL;
	LIGHTMAP* newLightmaps = NULL;

	if (hasLighting) {
		g_progress.update("Calculate lightmaps", faceCount);

		oldLightmaps = new LIGHTMAP[faceCount];
		newLightmaps = new LIGHTMAP[faceCount];
		memset(oldLightmaps, 0, sizeof(LIGHTMAP) * faceCount);
		memset(newLightmaps, 0, sizeof(LIGHTMAP) * faceCount);

		for (int i = 0; i < faceCount; i++) {
			BSPFACE& face = faces[i];

			int size[2];
			GetFaceLightmapSize(this, i, size);

			int lightmapSz = size[0] * size[1];
			int lightmapCount = lightmap_count(i);
			oldLightmaps[i].layers = lightmapCount;
			lightmapSz *= lightmapCount;

			oldLightmaps[i].width = size[0];
			oldLightmaps[i].height = size[1];

			bool skipResize = i < target.iFirstFace || i >= target.iFirstFace + target.nFaces;

			if (!skipResize) {
				oldLightmaps[i].luxelFlags = new byte[size[0] * size[1]];
				qrad_get_lightmap_flags(this, i, oldLightmaps[i].luxelFlags);
			}

			g_progress.tick();
		}
	}

	g_progress.update("Moving structures", ents.size()-1);

	if (movingWorld) {
		for (int i = 1; i < ents.size(); i++) { // don't move the world entity
			g_progress.tick();

			vec3 ori;
			if (ents[i]->hasKey("origin")) {
				ori = parseVector(ents[i]->getKeyvalue("origin"));
			}
			ori += offset;

			ents[i]->setOrAddKeyvalue("origin", ori.toKeyvalueString());

			if (ents[i]->hasKey("spawnorigin")) {
				vec3 spawnori = parseVector(ents[i]->getKeyvalue("spawnorigin"));

				// entity not moved if destination is 0,0,0
				if (spawnori.x != 0 || spawnori.y != 0 || spawnori.z != 0) {
					ents[i]->setOrAddKeyvalue("spawnorigin", (spawnori + offset).toKeyvalueString());
				}
			}			
		}

		update_ent_lump();
	}
	
	target.nMins += offset;
	target.nMaxs += offset;
	if (fabs(target.nMins.x) > MAX_MAP_COORD ||
		fabs(target.nMins.y) > MAX_MAP_COORD ||
		fabs(target.nMins.z) > MAX_MAP_COORD ||
		fabs(target.nMaxs.x) > MAX_MAP_COORD ||
		fabs(target.nMaxs.y) > MAX_MAP_COORD ||
		fabs(target.nMaxs.z) > MAX_MAP_COORD) {
		logf("\nWARNING: Model moved past safe world boundary!\n");
	}

	STRUCTUSAGE shouldBeMoved(this);
	mark_model_structures(modelIdx, &shouldBeMoved, dontMoveLeaves);


	for (int i = 0; i < nodeCount; i++) {
		if (!shouldBeMoved.nodes[i]) {
			continue;
		}

		BSPNODE& node = nodes[i];

		if (fabs((float)node.nMins.x + offset.x) > MAX_MAP_COORD ||
			fabs((float)node.nMaxs.x + offset.x) > MAX_MAP_COORD ||
			fabs((float)node.nMins.y + offset.y) > MAX_MAP_COORD ||
			fabs((float)node.nMaxs.y + offset.y) > MAX_MAP_COORD ||
			fabs((float)node.nMins.z + offset.z) > MAX_MAP_COORD ||
			fabs((float)node.nMaxs.z + offset.z) > MAX_MAP_COORD) {
			logf("\nWARNING: Bounding box for node moved past safe world boundary!\n");
		}
		node.nMins.x += offset.x;
		node.nMaxs.x += offset.x;
		node.nMins.y += offset.y;
		node.nMaxs.y += offset.y;
		node.nMins.z += offset.z;
		node.nMaxs.z += offset.z;
	}

	for (int i = 1; i < leafCount; i++) { // don't move the solid leaf (always has 0 size)
		if (!shouldBeMoved.leaves[i]) {
			continue;
		}

		BSPLEAF& leaf = leaves[i];

		if (fabs((float)leaf.nMins.x + offset.x) > MAX_MAP_COORD ||
			fabs((float)leaf.nMaxs.x + offset.x) > MAX_MAP_COORD ||
			fabs((float)leaf.nMins.y + offset.y) > MAX_MAP_COORD ||
			fabs((float)leaf.nMaxs.y + offset.y) > MAX_MAP_COORD ||
			fabs((float)leaf.nMins.z + offset.z) > MAX_MAP_COORD ||
			fabs((float)leaf.nMaxs.z + offset.z) > MAX_MAP_COORD) {
			logf("\nWARNING: Bounding box for leaf moved past safe world boundary!\n");
		}
		leaf.nMins.x += offset.x;
		leaf.nMaxs.x += offset.x;
		leaf.nMins.y += offset.y;
		leaf.nMaxs.y += offset.y;
		leaf.nMins.z += offset.z;
		leaf.nMaxs.z += offset.z;
	}

	for (int i = 0; i < vertCount; i++) {
		if (!shouldBeMoved.verts[i]) {
			continue;
		}

		vec3& vert = verts[i];

		vert += offset;

		if (fabs(vert.x) > MAX_MAP_COORD ||
			fabs(vert.y) > MAX_MAP_COORD ||
			fabs(vert.z) > MAX_MAP_COORD) {
			logf("\nWARNING: Vertex moved past safe world boundary!\n");
		}
	}

	for (int i = 0; i < planeCount; i++) {
		if (!shouldBeMoved.planes[i]) {
			continue; // don't move submodels with origins
		}

		BSPPLANE& plane = planes[i];
		vec3 newPlaneOri = offset + (plane.vNormal * plane.fDist);

		if (fabs(newPlaneOri.x) > MAX_MAP_COORD || fabs(newPlaneOri.y) > MAX_MAP_COORD ||
			fabs(newPlaneOri.z) > MAX_MAP_COORD) {
			logf("\nWARNING: Plane origin moved past safe world boundary!\n");
		}

		// get distance between new plane origin and the origin-aligned plane
		plane.fDist = dotProduct(plane.vNormal, newPlaneOri) / dotProduct(plane.vNormal, plane.vNormal);
	}

	for (int i = 0; i < texinfoCount; i++) {
		if (!shouldBeMoved.texInfo[i]) {
			continue; // don't move submodels with origins
		}

		move_texinfo(i, offset);
	}

	if (hasLighting) {
		resize_lightmaps(oldLightmaps, newLightmaps);

		for (int i = 0; i < faceCount; i++) {
			if (oldLightmaps[i].luxelFlags) {
				delete[] oldLightmaps[i].luxelFlags;
			}
			if (newLightmaps[i].luxelFlags) {
				delete[] newLightmaps[i].luxelFlags;
			}
		}
		delete[] oldLightmaps;
		delete[] newLightmaps;
	}

	g_progress.clear();

	return true;
}

void Bsp::delete_oob_data(int clipFlags) {
	float oob_coord = g_settings.mapsize_max;
	BSPMODEL& worldmodel = models[0];

	// remove OOB nodes and clipnodes
	{
		vector<BSPPLANE> clipOrder;

		bool* oobMarks = new bool[nodeCount];
		
		// collect oob data, then actually remove the nodes
		int removedNodes = 0;
		do {
			removedNodes = 0;
			memset(oobMarks, 1, nodeCount * sizeof(bool)); // assume everything is oob at first
			delete_oob_nodes(worldmodel.iHeadnodes[0], NULL, clipOrder, clipFlags, oobMarks, true, removedNodes);
			delete_oob_nodes(worldmodel.iHeadnodes[0], NULL, clipOrder, clipFlags, oobMarks, false, removedNodes);
		} while (removedNodes);
		delete[] oobMarks;

		oobMarks = new bool[clipnodeCount];
		for (int i = 1; i < MAX_MAP_HULLS; i++) {
			// collect oob data, then actually remove the nodes
			int removedNodes = 0;
			do {
				removedNodes = 0;
				memset(oobMarks, 1, clipnodeCount * sizeof(bool)); // assume everything is oob at first
				delete_oob_clipnodes(worldmodel.iHeadnodes[i], NULL, clipOrder, clipFlags, oobMarks, true, removedNodes);
				delete_oob_clipnodes(worldmodel.iHeadnodes[i], NULL, clipOrder, clipFlags, oobMarks, false, removedNodes);
			} while (removedNodes);
		}
		delete[] oobMarks;
	}

	vector<Entity*> newEnts;
	newEnts.push_back(ents[0]); // never remove worldspawn

	for (int i = 1; i < ents.size(); i++) {
		vec3 v = ents[i]->getOrigin();
		int modelIdx = ents[i]->getBspModelIdx();

		if (modelIdx != -1) {
			BSPMODEL& model = models[modelIdx];

			vec3 mins, maxs;
			get_model_vertex_bounds(modelIdx, mins, maxs);
			mins += v;
			maxs += v;

			bool oobx0 = (clipFlags & OOB_CLIP_X) ? (mins.x > oob_coord) : false;
			bool oobx1 = (clipFlags & OOB_CLIP_X_NEG) ? (maxs.x < -oob_coord) : false;
			bool ooby0 = (clipFlags & OOB_CLIP_Y) ? (mins.y > oob_coord) : false;
			bool ooby1 = (clipFlags & OOB_CLIP_Y_NEG) ? (maxs.y < -oob_coord) : false;
			bool oobz0 = (clipFlags & OOB_CLIP_Z) ? (mins.z > oob_coord) : false;
			bool oobz1 = (clipFlags & OOB_CLIP_Z_NEG) ? (maxs.z < -oob_coord) : false;

			if (!oobx0 && !ooby0 && !oobz0 && !oobx1 && !ooby1 && !oobz1) {
				newEnts.push_back(ents[i]);
			}
		}
		else {
			bool oobx0 = (clipFlags & OOB_CLIP_X) ? (v.x > oob_coord) : false;
			bool oobx1 = (clipFlags & OOB_CLIP_X_NEG) ? (v.x < -oob_coord) : false;
			bool ooby0 = (clipFlags & OOB_CLIP_Y) ? (v.y > oob_coord) : false;
			bool ooby1 = (clipFlags & OOB_CLIP_Y_NEG) ? (v.y < -oob_coord) : false;
			bool oobz0 = (clipFlags & OOB_CLIP_Z) ? (v.z > oob_coord) : false;
			bool oobz1 = (clipFlags & OOB_CLIP_Z_NEG) ? (v.z < -oob_coord) : false;

			if (!oobx0 && !ooby0 && !oobz0 && !oobx1 && !ooby1 && !oobz1) {
				newEnts.push_back(ents[i]);
			}
		}

	}
	int deletedEnts = ents.size() - newEnts.size();
	if (deletedEnts)
		logf("    Deleted %d entities\n", deletedEnts);
	ents = newEnts;

	uint8_t* oobFaces = new uint8_t[faceCount];
	memset(oobFaces, 0, faceCount * sizeof(bool));
	int oobFaceCount = 0;

	for (int i = 0; i < worldmodel.nFaces; i++) {
		BSPFACE& face = faces[worldmodel.iFirstFace + i];

		bool inBounds = true;
		for (int e = 0; e < face.nEdges; e++) {
			int32_t edgeIdx = surfedges[face.iFirstEdge + e];
			BSPEDGE& edge = edges[abs(edgeIdx)];
			int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];

			vec3 v = verts[vertIdx];

			bool oobx0 = (clipFlags & OOB_CLIP_X) ? (v.x > oob_coord) : false;
			bool oobx1 = (clipFlags & OOB_CLIP_X_NEG) ? (v.x < -oob_coord) : false;
			bool ooby0 = (clipFlags & OOB_CLIP_Y) ? (v.y > oob_coord) : false;
			bool ooby1 = (clipFlags & OOB_CLIP_Y_NEG) ? (v.y < -oob_coord) : false;
			bool oobz0 = (clipFlags & OOB_CLIP_Z) ? (v.z > oob_coord) : false;
			bool oobz1 = (clipFlags & OOB_CLIP_Z_NEG) ? (v.z < -oob_coord) : false;

			if (oobx0 || ooby0 || oobz0 || oobx1 || ooby1 || oobz1) {
				inBounds = false;
				break;
			}
		}

		if (!inBounds) {
			oobFaces[worldmodel.iFirstFace + i] = 1;
			oobFaceCount++;
		}
	}
	
	BSPFACE* newFaces = new BSPFACE[faceCount - oobFaceCount];

	int outIdx = 0;
	for (int i = 0; i < faceCount; i++) {
		if (!oobFaces[i]) {
			newFaces[outIdx++] = faces[i];
		}
	}

	for (int i = 0; i < modelCount; i++) {
		BSPMODEL& model = models[i];

		int offset = 0;
		int countReduce = 0;

		for (int k = 0; k < model.iFirstFace; k++) {
			offset += oobFaces[k];
		}
		for (int k = 0; k < model.nFaces; k++) {
			countReduce += oobFaces[model.iFirstFace + k];
		}

		model.iFirstFace -= offset;
		model.nFaces -= countReduce;
	}

	for (int i = 0; i < nodeCount; i++) {
		BSPNODE& node = nodes[i];

		int offset = 0;
		int countReduce = 0;

		for (int k = 0; k < node.firstFace; k++) {
			offset += oobFaces[k];
		}
		for (int k = 0; k < node.nFaces; k++) {
			countReduce += oobFaces[node.firstFace + k];
		}

		node.firstFace -= offset;
		node.nFaces -= countReduce;
	}

	for (int i = 0; i < leafCount; i++) {
		BSPLEAF& leaf = leaves[i];

		if (!leaf.nMarkSurfaces)
			continue;

		int oobCount = 0;

		for (int k = 0; k < leaf.nMarkSurfaces; k++) {
			if (oobFaces[marksurfs[leaf.iFirstMarkSurface + k]]) {
				oobCount++;
			}
		}

		if (oobCount) {
			leaf.nMarkSurfaces = 0;
			leaf.iFirstMarkSurface = 0;

			if (oobCount != leaf.nMarkSurfaces) {
				//logf("leaf %d partially OOB\n", i);
			}
		}
		else {
			for (int k = 0; k < leaf.nMarkSurfaces; k++) {
				BSPMARKSURF faceIdx = marksurfs[leaf.iFirstMarkSurface + k];

				int offset = 0;
				for (int j = 0; j < faceIdx; j++) {
					offset += oobFaces[j];
				}

				marksurfs[leaf.iFirstMarkSurface + k] = faceIdx - offset;
			}
		}
	}

	replace_lump(LUMP_FACES, newFaces, (faceCount - oobFaceCount) * sizeof(BSPFACE));

	delete[] oobFaces;

	worldmodel = models[0];

	vec3 mins, maxs;
	get_model_vertex_bounds(0, mins, maxs);

	vec3 buffer = vec3(64, 64, 128); // leave room for largest collision hull wall thickness
	worldmodel.nMins = mins - buffer;
	worldmodel.nMaxs = maxs + buffer;

	remove_unused_model_structures().print_delete_stats(1);
}

void Bsp::delete_box_data(vec3 clipMins, vec3 clipMaxs) {
	// TODO: most of this code is duplicated in delete_oob_*

	BSPMODEL& worldmodel = models[0];

	// remove nodes and clipnodes in the clipping box
	{
		vector<BSPPLANE> clipOrder;

		bool* oobMarks = new bool[nodeCount];

		// collect oob data, then actually remove the nodes
		int removedNodes = 0;
		do {
			removedNodes = 0;
			memset(oobMarks, 1, nodeCount * sizeof(bool)); // assume everything is oob at first
			delete_box_nodes(worldmodel.iHeadnodes[0], NULL, clipOrder, clipMins, clipMaxs, oobMarks, true, removedNodes);
			delete_box_nodes(worldmodel.iHeadnodes[0], NULL, clipOrder, clipMins, clipMaxs, oobMarks, false, removedNodes);
		} while (removedNodes);
		delete[] oobMarks;

		oobMarks = new bool[clipnodeCount];
		for (int i = 1; i < MAX_MAP_HULLS; i++) {
			// collect oob data, then actually remove the nodes
			int removedNodes = 0;
			do {
				removedNodes = 0;
				memset(oobMarks, 1, clipnodeCount * sizeof(bool)); // assume everything is oob at first
				delete_box_clipnodes(worldmodel.iHeadnodes[i], NULL, clipOrder, clipMins, clipMaxs, oobMarks, true, removedNodes);
				delete_box_clipnodes(worldmodel.iHeadnodes[i], NULL, clipOrder, clipMins, clipMaxs, oobMarks, false, removedNodes);
			} while (removedNodes);
		}
		delete[] oobMarks;
	}

	vector<Entity*> newEnts;
	newEnts.push_back(ents[0]); // never remove worldspawn

	for (int i = 1; i < ents.size(); i++) {
		vec3 v = ents[i]->getOrigin();
		int modelIdx = ents[i]->getBspModelIdx();

		if (modelIdx != -1) {
			BSPMODEL& model = models[modelIdx];

			vec3 mins, maxs;
			get_model_vertex_bounds(modelIdx, mins, maxs);
			mins += v;
			maxs += v;

			if (!boxesIntersect(mins, maxs, clipMins, clipMaxs)) {
				newEnts.push_back(ents[i]);
			}
		}
		else {
			bool isCullEnt = ents[i]->hasKey("classname") && ents[i]->getClassname() == "cull";
			if (!pointInBox(v, clipMins, clipMaxs) || isCullEnt) {
				newEnts.push_back(ents[i]);
			}
		}

	}
	int deletedEnts = ents.size() - newEnts.size();
	if (deletedEnts)
		logf("    Deleted %d entities\n", deletedEnts);
	ents = newEnts;

	uint8_t* oobFaces = new uint8_t[faceCount];
	memset(oobFaces, 0, faceCount * sizeof(bool));
	int oobFaceCount = 0;

	for (int i = 0; i < worldmodel.nFaces; i++) {
		BSPFACE& face = faces[worldmodel.iFirstFace + i];

		bool isClipped = false;
		for (int e = 0; e < face.nEdges; e++) {
			int32_t edgeIdx = surfedges[face.iFirstEdge + e];
			BSPEDGE& edge = edges[abs(edgeIdx)];
			int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];

			vec3 v = verts[vertIdx];

			if (pointInBox(v, clipMins, clipMaxs)) {
				isClipped = true;
				break;
			}
		}

		if (isClipped) {
			oobFaces[worldmodel.iFirstFace + i] = 1;
			oobFaceCount++;
		}
	}

	BSPFACE* newFaces = new BSPFACE[faceCount - oobFaceCount];

	int outIdx = 0;
	for (int i = 0; i < faceCount; i++) {
		if (!oobFaces[i]) {
			newFaces[outIdx++] = faces[i];
		}
	}

	for (int i = 0; i < modelCount; i++) {
		BSPMODEL& model = models[i];

		int offset = 0;
		int countReduce = 0;

		for (int k = 0; k < model.iFirstFace; k++) {
			offset += oobFaces[k];
		}
		for (int k = 0; k < model.nFaces; k++) {
			countReduce += oobFaces[model.iFirstFace + k];
		}

		model.iFirstFace -= offset;
		model.nFaces -= countReduce;
	}

	for (int i = 0; i < nodeCount; i++) {
		BSPNODE& node = nodes[i];

		int offset = 0;
		int countReduce = 0;

		for (int k = 0; k < node.firstFace; k++) {
			offset += oobFaces[k];
		}
		for (int k = 0; k < node.nFaces; k++) {
			countReduce += oobFaces[node.firstFace + k];
		}

		node.firstFace -= offset;
		node.nFaces -= countReduce;
	}

	for (int i = 0; i < leafCount; i++) {
		BSPLEAF& leaf = leaves[i];

		if (!leaf.nMarkSurfaces)
			continue;

		int oobCount = 0;

		for (int k = 0; k < leaf.nMarkSurfaces; k++) {
			if (oobFaces[marksurfs[leaf.iFirstMarkSurface + k]]) {
				oobCount++;
			}
		}

		if (oobCount) {
			leaf.nMarkSurfaces = 0;
			leaf.iFirstMarkSurface = 0;

			if (oobCount != leaf.nMarkSurfaces) {
				//logf("leaf %d partially OOB\n", i);
			}
		}
		else {
			for (int k = 0; k < leaf.nMarkSurfaces; k++) {
				BSPMARKSURF faceIdx = marksurfs[leaf.iFirstMarkSurface + k];

				int offset = 0;
				for (int j = 0; j < faceIdx; j++) {
					offset += oobFaces[j];
				}

				marksurfs[leaf.iFirstMarkSurface + k] = faceIdx - offset;
			}
		}
	}

	replace_lump(LUMP_FACES, newFaces, (faceCount - oobFaceCount) * sizeof(BSPFACE));

	delete[] oobFaces;

	worldmodel = models[0];

	vec3 mins, maxs;
	get_model_vertex_bounds(0, mins, maxs);

	vec3 buffer = vec3(64, 64, 128); // leave room for largest collision hull wall thickness
	worldmodel.nMins = mins - buffer;
	worldmodel.nMaxs = maxs + buffer;

	remove_unused_model_structures().print_delete_stats(1);
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

	logf("%-12s  ", name.c_str());
	if (isMem) {
		logf("%8.2f / %-5.2f MB", val/meg, max/meg);
	}
	else {
		logf("%8u / %-8u", val, max);
	}
	logf("  %6.1f%%", percent);

	if (val > max) {
		logf("  (OVERFLOW!!!)");
	}

	logf("\n");

	print_color(PRINT_RED | PRINT_GREEN | PRINT_BLUE);
}

void Bsp::print_model_stat(STRUCTUSAGE* modelInfo, uint val, uint max, bool isMem)
{
	string classname = modelInfo->modelIdx == 0 ? "worldspawn" : "???";
	string targetname = modelInfo->modelIdx == 0 ? "" : "???";
	for (int k = 0; k < ents.size(); k++) {
		if (ents[k]->getBspModelIdx() == modelInfo->modelIdx) {
			targetname = ents[k]->getTargetname();
			classname = ents[k]->getClassname();
		}
	}

	const float meg = 1024 * 1024;
	float percent = (val / (float)max) * 100;

	if (isMem) {
		logf("%8.1f / %-5.1f MB", val / meg, max / meg);
	}
	else {
		logf("%-26s %-26s *%-6d %9d", classname.c_str(), targetname.c_str(), modelInfo->modelIdx, val);
	}
	if (percent >= 0.1f)
		logf("  %6.1f%%", percent);

	logf("\n");
}

bool Bsp::isValid() {
	return modelCount < g_limits.max_models
		&& planeCount < g_limits.max_planes
		&& vertCount < g_limits.max_vertexes
		&& nodeCount < g_limits.max_nodes
		&& texinfoCount < g_limits.max_texinfos
		&& faceCount < g_limits.max_faces
		&& clipnodeCount < g_limits.max_clipnodes
		&& leafCount < g_limits.max_leaves
		&& marksurfCount < g_limits.max_marksurfaces
		&& surfedgeCount < g_limits.max_surfedges
		&& edgeCount < g_limits.max_edges
		&& textureCount < g_limits.max_textures
		&& lightDataLength < g_limits.max_lightdata
		&& visDataLength < g_limits.max_visdata
		&& lightstyle_count() < g_limits.max_lightstyles
		&& ceilf(calc_allocblock_usage()) <= g_limits.max_allocblocks;
}

bool Bsp::validate() {
	bool isValid = true;

	if (planeCount > g_limits.max_planes) logf("Overflowed Planes !!!\n");
	if (texinfoCount > g_limits.max_texinfos) logf("Overflowed texinfos !!!\n");
	if (leafCount > g_limits.max_leaves) logf("Overflowed leaves !!!\n");
	if (modelCount > g_limits.max_models) logf("Overflowed models !!!\n");
	if (texinfoCount > g_limits.max_texinfos) logf("Overflowed texinfos !!!\n");
	if (nodeCount > g_limits.max_nodes) logf("Overflowed nodes !!!\n");
	if (vertCount > g_limits.max_vertexes) logf("Overflowed verts !!!\n");
	if (faceCount > g_limits.max_faces) logf("Overflowed faces !!!\n");
	if (clipnodeCount > g_limits.max_clipnodes) logf("Overflowed clipnodes !!!\n");
	if (marksurfCount > g_limits.max_marksurfaces) logf("Overflowed marksurfs !!!\n");
	if (surfedgeCount > g_limits.max_surfedges) logf("Overflowed surfedges !!!\n");
	if (edgeCount > g_limits.max_edges) logf("Overflowed edges !!!\n");
	if (textureCount > g_limits.max_textures) logf("Overflowed textures !!!\n");
	if (lightDataLength > g_limits.max_lightdata) logf("Overflowed lightdata !!!\n");
	if (visDataLength > g_limits.max_visdata) logf("Overflowed visdata !!!\n");
	if (lightstyle_count() > g_limits.max_lightstyles) logf("Overflowed lightstyles !!!\n");
	if (ceilf(calc_allocblock_usage()) > g_limits.max_allocblocks) logf("Overflowed allocblocks !!!\n");

	for (int i = 0; i < marksurfCount; i++) {
		if (marksurfs[i] >= faceCount) {
			errorf("Bad face reference in marksurf %d: %d / %d\n", i, marksurfs[i], faceCount);
			isValid = false;
		}
	}
	for (int i = 0; i < surfedgeCount; i++) {
		if (abs(surfedges[i]) >= edgeCount) {
			errorf("Bad edge reference in surfedge %d: %d / %d\n", i, surfedges[i], edgeCount);
			isValid = false;
		}
	}
	for (int i = 0; i < texinfoCount; i++) {
		if (texinfos[i].iMiptex < 0 || texinfos[i].iMiptex >= textureCount) {
			errorf("Bad texture reference in textureinfo %d: %d / %d\n", i, texinfos[i].iMiptex, textureCount);
			isValid = false;
		}
	}
	int numBadExtent = 0;
	int numBadRadTexture = 0;
	int numBadSubdivides = 0;
	for (int i = 0; i < faceCount; i++) {
		if (faces[i].iPlane < 0 || faces[i].iPlane >= planeCount) {
			errorf("Bad plane reference in face %d: %d / %d\n", i, faces[i].iPlane, planeCount);
			isValid = false;
		}
		if (faces[i].nEdges > 0 && (faces[i].iFirstEdge < 0 || faces[i].iFirstEdge >= surfedgeCount)) {
			errorf("Bad surfedge reference in face %d: %d / %d\n", i, faces[i].iFirstEdge, surfedgeCount);
			isValid = false;
		}
		if (faces[i].iTextureInfo < 0 || faces[i].iTextureInfo >= texinfoCount) {
			errorf("Bad textureinfo reference in face %d: %d / %d\n", i, faces[i].iTextureInfo, texinfoCount);
			isValid = false;
		}
		if (lightDataLength > 0 && faces[i].nStyles[0] != 255 && 
			faces[i].nLightmapOffset != (uint32_t)-1 && faces[i].nLightmapOffset >= lightDataLength) 
		{
			errorf("Bad lightmap offset in face %d: %d / %d\n", i, faces[i].nLightmapOffset, lightDataLength);
			isValid = false;
		}

		BSPTEXTUREINFO& info = texinfos[faces[i].iTextureInfo];
		int size[2];
		if (!(info.nFlags & TEX_SPECIAL) && !GetFaceLightmapSize(this, i, size)) {
			numBadExtent++;
		}

		BSPTEXTUREINFO* radinfo = get_embedded_rad_texinfo(info);
		if (radinfo) {
			BSPFACE& face = faces[i];
			BSPPLANE& plane = planes[face.iPlane];

			vec3 faceNormal = plane.vNormal * (face.nPlaneSide ? -1 : 1);
			vec3 texnormal = crossProduct(radinfo->vT, radinfo->vS).normalize();
			float distscale = dotProduct(texnormal, faceNormal);

			if (distscale == 0) {
				BSPMIPTEX* radTex = get_texture(info.iMiptex);
				if (radTex) {
					debugf("Invalid RAD texture axes in %s\n", radTex->szName);
					numBadRadTexture++;
				}
			}
		}

		// undo my fuckup subdivided faces in v5 that crash the software renderer
		bool isBspguyFuckupFace = true;
		BSPEDGE& firstEdge = edges[abs(surfedges[faces[i].iFirstEdge])];
		int lastVert0 = firstEdge.iVertex[0] - 1;
		for (int k = 0; k < faces[i].nEdges; k++) {
			int32_t edgeIdx = surfedges[faces[i].iFirstEdge + k];
			BSPEDGE& edge = edges[abs(edgeIdx)];

			if (edgeIdx >= 0) {
				// fuckups have all negative edge indices
				isBspguyFuckupFace = false;
				break;
			}

			if (edge.iVertex[0] != lastVert0 + 1) {
				// fuckup edge 1st indice is always incremented by 1
				isBspguyFuckupFace = false;
				break;
			}

			// fuckup edge 2nd index is always the 1st + 1, until the last edge, which wraps
			// to the 1st index of the 1st edge
			if (k < faces[i].nEdges - 1) {
				if (edge.iVertex[1] != edge.iVertex[0] + 1) {
					isBspguyFuckupFace = false;
					break;
				}
			}
			else if (edge.iVertex[1] != firstEdge.iVertex[0]) {
				// last edge should wrap around to the first
				isBspguyFuckupFace = false;
				break;
			}

			lastVert0 = edge.iVertex[0];
		}

		if (isBspguyFuckupFace) {
			numBadSubdivides++;

			// easy fix. Just use the 2nd indice in each edge instead of the 1st.
			for (int k = 0; k < faces[i].nEdges; k++) {
				surfedges[faces[i].iFirstEdge + k] *= -1;
			}
		}
	}
	if (numBadExtent) {
		errorf("Bad Surface Extents on %d faces\n", numBadExtent);
		isValid = false;
	}
	if (numBadRadTexture) {
		warnf("%d faces have invalid RAD textures. VHLT will complain about malformed faces.\n", numBadRadTexture);
		isValid = false;
	}
	if (numBadSubdivides) {
		logf("Bad v5 subdivides detected on %d faces. These crash the software renderer. (fixed!)\n", numBadSubdivides);
		isValid = false;
	}

	for (int i = 0; i < leafCount; i++) {
		if ((leaves[i].iFirstMarkSurface < 0 || leaves[i].iFirstMarkSurface + leaves[i].nMarkSurfaces > marksurfCount)) {
			const char* msg = cstrf("Bad marksurf reference in leaf %d: (%d + %d) / %d", 
				i, leaves[i].iFirstMarkSurface, leaves[i].nMarkSurfaces, marksurfCount);

			if (leaves[i].nMarkSurfaces == 0) {
				logf("%s (fixed!)\n", msg);
				leaves[i].iFirstMarkSurface = 0;
			}
			else {
				errorf("%s\n", msg);
			}

			isValid = false;
		}
		//logf("Leaf %d: %d %d %d\n", i, marksurfs[leaves[i].iFirstMarkSurface], leaves[i].nMarkSurfaces);
	}
	for (int i = 0; i < edgeCount; i++) {
		for (int k = 0; k < 2; k++) {
			if (edges[i].iVertex[k] >= vertCount) {
				errorf("Bad vertex reference in edge %d: %d / %d\n", i, edges[i].iVertex[k], vertCount);
				isValid = false;
			}
		}
	}
	for (int i = 0; i < nodeCount; i++) {
		if ((nodes[i].firstFace < 0 || nodes[i].firstFace + nodes[i].nFaces > faceCount)) {
			const char* msg = cstrf("Bad face reference in node %d: %d / %d", i, nodes[i].firstFace, faceCount);
			if (nodes[i].nFaces == 0) {
				nodes[i].firstFace = 0;
				logf("%s (fixed!)\n", msg);
			}
			else {
				errorf("%s\n", msg);
			}
			isValid = false;
		}
		if (nodes[i].iPlane < 0 || nodes[i].iPlane >= planeCount) {
			errorf("Bad plane reference in node %d: %d / %d\n", i, nodes[i].iPlane, planeCount);
			isValid = false;
		}
		for (int k = 0; k < 2; k++) {
			if (nodes[i].iChildren[k] >= nodeCount) {
				errorf("Bad node reference in node %d child %d: %d / %d\n", i, k, nodes[i].iChildren[k], nodeCount);
				isValid = false;
			}
			else if (nodes[i].iChildren[k] < 0 && ~nodes[i].iChildren[k] >= leafCount) {
				errorf("Bad leaf reference in node %d child %d: %d / %d\n", i, k, ~nodes[i].iChildren[k], leafCount);
				isValid = false;
			}
		}

		if (nodes[i].nMins.x > nodes[i].nMaxs.x ||
			nodes[i].nMins.y > nodes[i].nMaxs.y ||
			nodes[i].nMins.z > nodes[i].nMaxs.z) {
			warnf("Backwards mins/maxs in node %d. Mins: (%d, %d, %d) Maxs: (%d %d %d)\n", i,
				(int)nodes[i].nMins.x, (int)nodes[i].nMins.y, (int)nodes[i].nMins.z,
				(int)nodes[i].nMaxs.x, (int)nodes[i].nMaxs.y, (int)nodes[i].nMaxs.z);
			isValid = false;
		}
	}
	for (int i = 0; i < planeCount; i++) {
		BSPPLANE& plane = planes[i];
		float normLen = plane.vNormal.length();
		
		if (normLen < 0.5f) {
			const char* msg = cstrf("Bad normal for plane %d", i);
			if (normLen > 0) {
				plane.vNormal = plane.vNormal.normalize(1.0f);
				logf("%s (fixed!)\n", msg);
			}
			else {
				errorf("%s\n", msg);
			}
			
			isValid = false;
		}
	}

	for (int i = 0; i < clipnodeCount; i++) {
		if (clipnodes[i].iPlane < 0 || clipnodes[i].iPlane >= planeCount) {
			errorf("Bad plane reference in clipnode %d: %d / %d\n", i, clipnodes[i].iPlane, planeCount);
			isValid = false;
		}
		for (int k = 0; k < 2; k++) {
			if (clipnodes[i].iChildren[k] >= clipnodeCount) {
				errorf("Bad clipnode reference in clipnode %d child %d: %d / %d\n", i, k, clipnodes[i].iChildren[k], clipnodeCount);
				isValid = false;
			}
		}
	}
	for (int i = 0; i < ents.size(); i++) {
		if (ents[i]->getBspModelIdx() >= modelCount) {
			errorf("Bad model reference in entity %d: %d / %d\n", i, ents[i]->getBspModelIdx(), modelCount);
			isValid = false;
		}
	}

	bool notContig = false;
	int totalVisLeaves = 1; // solid leaf not included in model leaf counts
	int totalFaces = 0;
	vector<int> unlinkedFaces;
	for (int i = 0; i < modelCount; i++) {
		totalVisLeaves += models[i].nVisLeafs;
		totalFaces += models[i].nFaces;
		if ((models[i].iFirstFace < 0 || models[i].iFirstFace + models[i].nFaces > faceCount)) {
			const char* msg = cstrf("Bad face reference in model %d: %d / %d", i, models[i].iFirstFace, faceCount);
			if (models[i].nFaces == 0) {
				models[i].iFirstFace = 0;
				logf("%s (fixed!)\n", msg);
			}
			else {
				errorf("%s\n", msg);
			}
			isValid = false;
		}
		if (models[i].iHeadnodes[0] >= nodeCount) {
			errorf("Bad node reference in model %d hull 0: %d / %d (fixed!)\n", i, models[i].iHeadnodes[0], nodeCount);
			models[i].iHeadnodes[0] = -1;
			isValid = false;
		}
		for (int k = 1; k < MAX_MAP_HULLS; k++) {
			if (models[i].iHeadnodes[k] >= clipnodeCount) {
				errorf("Bad clipnode reference in model %d hull %d: %d / %d (fixed!)\n", i, k, models[i].iHeadnodes[k], clipnodeCount);
				models[i].iHeadnodes[k] = -1;
				isValid = false;
			}
		}
		if (models[i].nMins.x > models[i].nMaxs.x ||
			models[i].nMins.y > models[i].nMaxs.y ||
			models[i].nMins.z > models[i].nMaxs.z) {
			logf("Backwards mins/maxs in model %d. Mins: (%f, %f, %f) Maxs: (%f %f %f)\n", i,
				models[i].nMins.x, models[i].nMins.y, models[i].nMins.z,
				models[i].nMaxs.x, models[i].nMaxs.y, models[i].nMaxs.z);

			get_model_hull_bounds(i, 0, models[i].nMins, models[i].nMaxs);
			logf("    Recalculated as Mins: (%f, %f, %f) Maxs: (%f %f %f)\n", i,
				models[i].nMins.x, models[i].nMins.y, models[i].nMins.z,
				models[i].nMaxs.x, models[i].nMaxs.y, models[i].nMaxs.z);
			isValid = false;
		}

		STRUCTUSAGE usage = STRUCTUSAGE(this);
		mark_model_structures(i, &usage, i != 0);
		int faceSum = 0;
		for (int i = 0; i < faceCount; i++)
			faceSum += usage.faces[i];
		if (faceSum != models[i].nFaces) {
			// This isn't necessarily an error. A func_illusionary with a null face can set lower face count.
			// Newly created BSP models also reuse leaves from the world since leaf faces don't matter for submodels.
			warnf("Expected %d faces in model %d but found %d\n", models[i].nFaces, i, faceSum);
		}
		
		if (i == 0) {
			STRUCTUSAGE nodeUsage = STRUCTUSAGE(this);
			STRUCTUSAGE leafUsage = STRUCTUSAGE(this);

			for (int k = 0; k < nodeCount; k++) {
				if (usage.nodes[k]) {
					BSPNODE& node = nodes[k];

					for (int i = 0; i < node.nFaces; i++) {
						nodeUsage.faces[node.firstFace + i] = true;
					}
					for (int j = 0; j < 2; j++) {
						if (node.iChildren[j] < 0) {
							BSPLEAF& leaf = leaves[~node.iChildren[j]];
							
							for (int f = 0; f < leaf.nMarkSurfaces; f++) {
								leafUsage.faces[marksurfs[leaf.iFirstMarkSurface + f]] = true;
							}
						}
					}
				}
			}

			for (int k = 0; k < faceCount; k++) {
				if (nodeUsage.faces[k] && !leafUsage.faces[k]) {
					BSPTEXTUREINFO& info = texinfos[faces[k].iTextureInfo];
					if (info.nFlags & TEX_SPECIAL)
						continue; // shouldn't be visible anyway
					unlinkedFaces.push_back(k);
				}
			}
		}
		
		int lastNode = -1;
		for (int k = 0; k < nodeCount; k++) {
			if (usage.nodes[k]) {
				if (lastNode != -1 && k - lastNode > 1) {
					logf("Model %d HULL 0 nodes not contiguous\n", i, lastNode, k);
					notContig = true;
					break;
				}

				lastNode = k;
			}
		}

		for (int h = 1; h < MAX_MAP_HULLS; h++) {
			int lastClipnode = -1;
			for (int k = 0; k < clipnodeCount; k++) {
				if (usage.clipnodes[k]) {
					if (lastClipnode != -1 && k - lastClipnode > 1) {
						logf("Model %d HULL %d clipnodes not contiguous\n", i, h);
						notContig = true;
						break;
					}

					lastClipnode = k;
				}
			}
		}
	}
	if (totalVisLeaves != leafCount) {
		warnf("Bad model vis leaf sum: %d / %d\n", totalVisLeaves, leafCount);
		isValid = false;
	}
	if (totalFaces != faceCount) {
		warnf("Bad model face sum: %d / %d\n", totalFaces, faceCount);
		isValid = false;
	}
	for (int i = 0; i < unlinkedFaces.size(); i++) {
		int faceIdx = unlinkedFaces[i];
		if (add_face_to_touched_leaves(faceIdx)) {
			logf("Face %d was not linked to any leaves (fixed!)\n", faceIdx);
		}
		else {
			logf("Face %d is not linked to any leaves, and none are nearby. It will be invisible in-game.\n", faceIdx);
		}
	}

	if (notContig) {
		make_nodes_contiguous();
		logf("Rearranged nodes/clipnodes for contiguity (fixes Xash3D crash)\n");
	}

	int worldspawn_count = 0;
	for (int i = 0; i < ents.size(); i++) {
		if (ents[i]->getClassname() == "worldspawn") {
			worldspawn_count++;
		}
	}
	if (worldspawn_count != 1) {
		errorf("Found %d worldspawn entities (expected 1). This can cause crashes and svc_bad errors.\n", worldspawn_count);
		isValid = false;
	}

	if (!validate_vis_data()) {
		isValid = false;
	}

	int oobCount = 0;
	int badOriginCount = 0;
	int badModelRefCount = 0;
	int missingBspModelCount = 0;
	for (int i = 0; i < ents.size(); i++) {
		Entity* ent = ents[i];
		vec3 ori = ent->getOrigin();
		string cname = ent->getClassname();
		float oob = g_settings.mapsize_max;

		if (ori.x || ori.y || ori.z) {
			if (cname == "func_ladder" || cname == "func_water" || cname == "func_mortar_field") {
				badOriginCount++;
			}
		}

		if (fabs(ori.x) > oob || fabs(ori.y) > oob || fabs(ori.z) > oob) {
			debugf("Entity '%s' (%s) outside map boundary at (%d %d %d)\n",
				ent->hasKey("targetname") ? ent->getKeyvalue("targetname").c_str() : "",
				ent->hasKey("classname") ? ent->getKeyvalue("classname").c_str() : "",
				(int)ori.x, (int)ori.y, (int)ori.z);
			oobCount++;
		}
		if (ent->getBspModelIdx() >= modelCount) {
			badModelRefCount++;
		}

		FgdClass* fgd = g_app->mergedFgd ? g_app->mergedFgd->getFgdClass(cname) : NULL;
		if (i > 0 && fgd && fgd->classType == FGD_CLASS_SOLID && ent->getBspModelIdx() < 0) {
			debugf("Missing model key for \"%s\" (%s)\n", ent->getTargetname().c_str(), ent->getClassname().c_str());
			missingBspModelCount++;
		}
	}
	if (missingBspModelCount) {
		warnf("%d solid entities have no model key set\n", missingBspModelCount);
	}
	if (badModelRefCount) {
		errorf("%d entities have invalid BSP model references\n", badModelRefCount);
	}
	if (oobCount) {
		warnf("%d entities outside of the map boundaries\n", oobCount);
	}
	if (badOriginCount) {
		warnf("%d entities have origins that may cause problems (see \"Zero Model Origins\" tool)\n", badOriginCount);
	}

	for (int i = 0; i < textureCount; i++) {
		BSPMIPTEX* tex = get_texture(i);
		if (!tex) {
			int32_t texOffset = ((int32_t*)textures)[i + 1];
			errorf("Invalid offset %d for texture ID %d\n", texOffset, i);
			continue;
		}

		if (tex->nWidth * tex->nHeight > g_limits.max_texturepixels) {
			errorf("Texture '%s' too large (%dx%d)\n", tex->szName, tex->nWidth, tex->nHeight);
		}
	}

	int missing_textures = count_missing_textures();
	if (missing_textures) {
		warnf("%d missing textures\n", missing_textures);
	}

	return isValid;
}

void Bsp::print_info(bool perModelStats, int perModelLimit, int sortMode) {
	int entCount = ents.size();

	if (perModelStats) {
		g_sort_mode = sortMode;

		if (planeCount >= g_limits.max_planes || texinfoCount >= g_limits.max_texinfos || leafCount >= g_limits.max_leaves ||
			modelCount >= g_limits.max_models || nodeCount >= g_limits.max_nodes || vertCount >= g_limits.max_vertexes ||
			faceCount >= g_limits.max_faces || clipnodeCount >= g_limits.max_clipnodes || marksurfCount >= g_limits.max_marksurfaces ||
			surfedgeCount >= g_limits.max_surfedges || edgeCount >= g_limits.max_edges || textureCount >= g_limits.max_textures ||
			lightDataLength >= g_limits.max_lightdata || visDataLength >= g_limits.max_visdata)
		{
			logf("Unable to show model stats while BSP limits are exceeded.\n");
			return;
		}

		vector<STRUCTUSAGE*> modelStructs = get_sorted_model_infos(sortMode);

		int maxCount;
		const char* countName;

		switch (g_sort_mode) {
		case SORT_VERTS:		maxCount = vertCount; countName = "  Verts";  break;
		case SORT_NODES:		maxCount = nodeCount; countName = "  Nodes";  break;
		case SORT_CLIPNODES:	maxCount = clipnodeCount; countName = "Clipnodes";  break;
		case SORT_FACES:		maxCount = faceCount; countName = "  Faces";  break;
		}

		logf("       Classname                  Targetname          Model  %-10s  Usage\n", countName);
		logf("-------------------------  -------------------------  -----  ----------  --------\n");

		for (int i = 0; i < modelCount && i < perModelLimit; i++) {

			int val;
			switch (g_sort_mode) {
			case SORT_VERTS:		val = modelStructs[i]->sum.verts; break;
			case SORT_NODES:		val = modelStructs[i]->sum.nodes; break;
			case SORT_CLIPNODES:	val = modelStructs[i]->sum.clipnodes; break;
			case SORT_FACES:		val = modelStructs[i]->sum.faces; break;
			}

			if (val == 0)
				break;

			print_model_stat(modelStructs[i], val, maxCount, false);
		}
	}
	else {
		logf(" Data Type     Current / Max       Fullness\n");
		logf("------------  -------------------  --------\n");
		print_stat("AllocBlock", calc_allocblock_usage(), g_limits.max_allocblocks, false);
		print_stat("models", modelCount, g_limits.max_models, false);
		print_stat("planes", planeCount, g_limits.max_planes, false);
		print_stat("vertexes", vertCount, g_limits.max_vertexes, false);
		print_stat("nodes", nodeCount, g_limits.max_nodes, false);
		print_stat("texinfos", texinfoCount, g_limits.max_texinfos, false);
		print_stat("faces", faceCount, g_limits.max_faces, false);
		print_stat("clipnodes", clipnodeCount, g_limits.max_clipnodes, false);
		print_stat("leaves", leafCount, g_limits.max_leaves, false);
		print_stat("marksurfaces", marksurfCount, g_limits.max_marksurfaces, false);
		print_stat("surfedges", surfedgeCount, g_limits.max_surfedges, false);
		print_stat("edges", edgeCount, g_limits.max_edges, false);
		print_stat("textures", textureCount, g_limits.max_textures, false);
		print_stat("lightstyles", lightstyle_count(), g_limits.max_lightstyles, false);
		print_stat("lightdata", lightDataLength, g_limits.max_lightdata, true);
		print_stat("visdata", visDataLength, g_limits.max_visdata, true);
		print_stat("entities", entCount, g_limits.max_entities, false);
	}
}

int32_t Bsp::pointContents(int iNode, vec3 p, int hull, vector<int>& nodeBranch, int& leafIdx, int& childIdx) {
	if (iNode < 0) {
		leafIdx = -1;
		childIdx = -1;
		return hull == 0 ? leaves[~iNode].nContents : iNode;
	}

	if (hull == 0) {
		while (iNode >= 0 && iNode < nodeCount)
		{
			nodeBranch.push_back(iNode);
			BSPNODE& node = nodes[iNode];
			BSPPLANE& plane = planes[node.iPlane];

			float d = dotProduct(plane.vNormal, p) - plane.fDist;
			if (d < 0) {
				iNode = node.iChildren[1];
				childIdx = 1;
			}
			else {
				iNode = node.iChildren[0];
				childIdx = 0;
			}
		}

		leafIdx = ~iNode;
		return leaves[~iNode].nContents;
	}
	else {
		while (iNode >= 0 && iNode < clipnodeCount)
		{
			nodeBranch.push_back(iNode);
			BSPCLIPNODE& node = clipnodes[iNode];
			BSPPLANE& plane = planes[node.iPlane];

			float d = dotProduct(plane.vNormal, p) - plane.fDist;
			if (d < 0) {
				iNode = node.iChildren[1];
				childIdx = 1;
			}
			else {
				iNode = node.iChildren[0];
				childIdx = 0;
			}
		}

		return iNode;
	}
}

int32_t Bsp::pointContents(int iNode, vec3 p, int hull) {
	vector<int> nodeBranch;
	int leafIdx;
	int childIdx;
	return pointContents(iNode, p, hull, nodeBranch, leafIdx, childIdx);
}

bool Bsp::recursiveHullCheck(int hull, int num, float p1f, float p2f, vec3 p1, vec3 p2, TraceResult* trace)
{
	if (num < 0) {
		int contents = num;
		if (hull == 0) {
			contents = leaves[~num].nContents;
		}

		if (contents != CONTENTS_SOLID) {
			trace->fAllSolid = false;

			if (contents == CONTENTS_EMPTY)
				trace->fInOpen = true;

			else if (contents != CONTENTS_TRANSLUCENT)
				trace->fInWater = true;
		}
		else {
			trace->fStartSolid = true;
		}

		// empty
		return true;
	}

	if (hull == 0 && num >= nodeCount || hull != 0 && num >= clipnodeCount) {
		logf("%s: bad node number\n", __func__);
		return false;
	}

	// find the point distances
	BSPCLIPNODE* node = hull == 0 ? (BSPCLIPNODE*)&nodes[num] : &clipnodes[num];
	BSPPLANE* plane = &planes[node->iPlane];
	
	//float t1 = dotProduct(plane->vNormal, p1) - plane->fDist;
	//float t2 = dotProduct(plane->vNormal, p2) - plane->fDist;
	float t1, t2;

	switch (plane->nType) {
	case 0:
		t1 = p1.x - plane->fDist;
		t2 = p2.x - plane->fDist;
		break;
	case 1:
		t1 = p1.y - plane->fDist;
		t2 = p2.y - plane->fDist;
		break;
	case 2:
		t1 = p1.z - plane->fDist;
		t2 = p2.z - plane->fDist;
		break;
	default:
		t1 = dotProduct(plane->vNormal, p1) - plane->fDist;
		t2 = dotProduct(plane->vNormal, p2) - plane->fDist;
		break;
	}

	// keep descending until we find a plane that bisects the trace line
	if (t1 >= 0.0f && t2 >= 0.0f)
		return recursiveHullCheck(hull, node->iChildren[0], p1f, p2f, p1, p2, trace);
	if (t1 < 0.0f && t2 < 0.0f)
		return recursiveHullCheck(hull, node->iChildren[1], p1f, p2f, p1, p2, trace);

	int side = (t1 < 0.0f) ? 1 : 0;
	
	// put the crosspoint DIST_EPSILON pixels on the near side
	float frac;
	if (side) {
		frac = (t1 + EPSILON) / (t1 - t2);
	}
	else {
		frac = (t1 - EPSILON) / (t1 - t2);
	}
	frac = clamp(frac, 0.0f, 1.0f);

	if (frac != frac) {
		return false; // NaN
	}

	float pdif = p2f - p1f;
	float midf = p1f + pdif * frac;

	vec3 delta = p2 - p1;
	vec3 mid = p1 + (delta * frac);

	// check if trace is empty up until this plane that was just intersected
	if (!recursiveHullCheck(hull, node->iChildren[side], p1f, midf, p1, mid, trace)) {
		// hit an earlier plane that caused the trace to be fully solid here
		return false;
	}

	// check if trace can go through this plane without entering a solid area
	if (pointContents(node->iChildren[side ^ 1], mid, hull) != CONTENTS_SOLID) {
		// continue the trace from this plane
		// won't collide with it again because trace starts from a child of the intersected node
		return recursiveHullCheck(hull, node->iChildren[side ^ 1], midf, p2f, mid, p2, trace);
	}

	if (trace->fAllSolid) {
		return false; // never got out of the solid area
	}

	// the other side of the node is solid, this is the impact point
	trace->vecPlaneNormal = plane->vNormal;
	trace->flPlaneDist = side ? -plane->fDist : plane->fDist;
	trace->iNode = num;

	// backup the trace if the collision point is considered solid due to poor float precision
	// shouldn't really happen, but does occasionally
	int headnode = models[0].iHeadnodes[hull];
	while (pointContents(headnode, mid, hull) == CONTENTS_SOLID) {
		frac -= 0.1f;
		if (frac < 0.0f)
		{
			trace->flFraction = midf;
			trace->vecEndPos = mid;
			//debugf("backup past 0\n");
			return false;
		}

		midf = p1f + pdif * frac;

		vec3 point = p2 - p1;
		mid = p1 + (point * frac);
	}

	trace->flFraction = midf;
	trace->vecEndPos = mid;

	return false;
}

bool Bsp::traceHull(vec3 start, vec3 end, int hull, TraceResult* trace)
{
	if (hull < 0 || hull > 3)
		hull = 0;

	int headnode = models[0].iHeadnodes[hull];

	// fill in a default trace
	memset(trace, 0, sizeof(TraceResult));
	trace->vecEndPos = end;
	trace->flFraction = 1.0f;
	trace->fAllSolid = true;

	// trace a line through the appropriate clipping hull
	return recursiveHullCheck(hull, headnode, 0.0f, 1.0f, start, end, trace);
}

int Bsp::traceFace(vec3 start, vec3 end, int& u, int& v) {
	u = v = 0;

	TraceResult tr;
	if (traceHull(start, end, 0, &tr)) {
		return -1;
	}

	BSPNODE& node = nodes[tr.iNode];

	for (int i = 0; i < node.nFaces; i++) {
		int faceIdx = node.firstFace + i;
		BSPFACE& face = faces[faceIdx];
		BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];

		int bmins[2];
		int bmaxs[2];
		GetFaceExtents(this, faceIdx, bmins, bmaxs);

		int mins[2];
		int maxs[2];
		for (int k = 0; k < 2; k++) {
			maxs[k] = (bmaxs[k] - bmins[k]) * 16;
			mins[k] = bmins[k] * 16;
		}

		int ds = (int)(dotProduct(tr.vecEndPos, info.vS) + info.shiftS);
		int dt = (int)(dotProduct(tr.vecEndPos, info.vT) + info.shiftT);

		if (ds >= mins[0] && dt >= mins[1] && ds - mins[0] <= maxs[0] && dt - mins[1] <= maxs[1]) {
			u = ds - mins[0];
			v = dt - mins[1];
			return faceIdx;
		}
	}

	return -1;
}

BSPPLANE Bsp::get_separation_plane(vec3 minsA, vec3 maxsA, vec3 minsB, vec3 maxsB) {
	BSPPLANE separationPlane = {};

	struct AxisTest {
		int type;
		vec3 normal;
		float gap;
		float dist;
	};

	std::vector<AxisTest> candidates;

	// X axis
	if (minsB.x >= maxsA.x) {
		float gap = minsB.x - maxsA.x;
		candidates.push_back({ PLANE_X, {1, 0, 0}, gap, maxsA.x + gap * 0.5f });
	}
	else if (maxsB.x <= minsA.x) {
		float gap = minsA.x - maxsB.x;
		candidates.push_back({ PLANE_X, {-1, 0, 0}, gap, maxsB.x + gap * 0.5f });
	}

	// Y axis
	if (minsB.y >= maxsA.y) {
		float gap = minsB.y - maxsA.y;
		candidates.push_back({ PLANE_Y, {0, 1, 0}, gap, maxsA.y + gap * 0.5f });
	}
	else if (maxsB.y <= minsA.y) {
		float gap = minsA.y - maxsB.y;
		candidates.push_back({ PLANE_Y, {0, -1, 0}, gap, maxsB.y + gap * 0.5f });
	}

	// Z axis
	if (minsB.z >= maxsA.z) {
		float gap = minsB.z - maxsA.z;
		candidates.push_back({ PLANE_Z, {0, 0, 1}, gap, maxsA.z + gap * 0.5f });
	}
	else if (maxsB.z <= minsA.z) {
		float gap = minsA.z - maxsB.z;
		candidates.push_back({ PLANE_Z, {0, 0, -1}, gap, maxsB.z + gap * 0.5f });
	}

	if (candidates.empty()) {
		separationPlane.nType = -1; // No separating axis
		return separationPlane;
	}

	// Choose the axis with the largest gap
	const AxisTest* best = &candidates[0];
	for (const AxisTest& test : candidates) {
		if (test.gap > best->gap)
			best = &test;
	}

	separationPlane.nType = best->type;
	separationPlane.vNormal = best->normal;
	separationPlane.fDist = best->dist;

	return separationPlane;
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
	logf("Wrote %d planes\n", numPlanes);

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
				printf("BSP HAS %d PLANES\n", count);
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

void Bsp::write_csg_polys(int32_t nodeIdx, FILE* polyfile, int flipPlaneSkip, bool debug) {
	if (nodeIdx >= 0) {
		write_csg_polys(nodes[nodeIdx].iChildren[0], polyfile, flipPlaneSkip, debug);
		write_csg_polys(nodes[nodeIdx].iChildren[1], polyfile, flipPlaneSkip, debug);
		return;
	}

	BSPLEAF& leaf = leaves[~nodeIdx];

	int detaillevel = 0; // no way to know which faces came from a func_detail
	int32_t contents = leaf.nContents;

	for (int i = leaf.iFirstMarkSurface; i < leaf.iFirstMarkSurface + leaf.nMarkSurfaces; i++) {
		for (int z = 0; z < 2; z++) {
			if (z == 0)
				continue;
			BSPFACE& face = faces[marksurfs[i]];

			// FIXME : z always == 1
			bool flipped = (z == 1 || face.nPlaneSide) && !(z == 1 && face.nPlaneSide);

			int iPlane = !flipped ? face.iPlane : face.iPlane + flipPlaneSkip;

			// FIXME : z always == 1
			// contents in front of the face
			int faceContents = z == 1 ? leaf.nContents : CONTENTS_SOLID;

			//int texInfo = z == 1 ? face.iTextureInfo : -1;

			if (debug) {
				BSPPLANE plane = planes[iPlane];
				logf("Writing face (%2.0f %2.0f %2.0f) %4.0f  %s\n", 
					plane.vNormal.x, plane.vNormal.y, plane.vNormal.z, plane.fDist,
					(faceContents == CONTENTS_SOLID ? "SOLID" : "EMPTY"));
				if (flipped && false) {
					logf(" (flipped)");
				}
			}

			fprintf(polyfile, "%i %i %i %i %u\n", detaillevel, iPlane, face.iTextureInfo, faceContents, face.nEdges);

			if (flipped) {
				for (int e = (face.iFirstEdge + face.nEdges) - 1; e >= (int)face.iFirstEdge; e--) {
					int32_t edgeIdx = surfedges[e];
					BSPEDGE& edge = edges[abs(edgeIdx)];
					vec3 v = edgeIdx >= 0 ? verts[edge.iVertex[1]] : verts[edge.iVertex[0]];
					fprintf(polyfile, "%5.8f %5.8f %5.8f\n", v.x, v.y, v.z);
				}
			}
			else {
				for (int e = face.iFirstEdge; e < face.iFirstEdge + face.nEdges; e++) {
					int32_t edgeIdx = surfedges[e];
					BSPEDGE& edge = edges[abs(edgeIdx)];
					vec3 v = edgeIdx >= 0 ? verts[edge.iVertex[1]] : verts[edge.iVertex[0]];
					fprintf(polyfile, "%5.8f %5.8f %5.8f\n", v.x, v.y, v.z);
				}
			}

			fprintf(polyfile, "\n");
		}
		if (debug)
			logf("\n");
	}
}

int Bsp::calcMemoryUsage() {
	int bytes = sizeof(Bsp);
	if (lumps) {
		for (int i = 0; i < HEADER_LUMPS; i++) {
			bytes += header.lump[i].nLength;
		}
	}

	bytes += path.size() + name.size();
	return bytes;
}