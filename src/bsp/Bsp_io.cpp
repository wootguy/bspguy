#include "Bsp.h"
#include "util.h"
#include "mstream.h"
#include <fstream>

const char* g_bsp_format_names[BSP_FORMAT_TYPES]{
	"BSP29",
	"BSP2",
	"2PSB",
	"BSP30",
	"BSP30_BS",
	"BSPGUY",
	"BSP_UNKOWN",
};

int Bsp::formatForFileVersion(int bspVersion) {
	switch (bspVersion < 0 ? header.nVersion : bspVersion) {
	default:			return BSP_UNKNOWN;
	case 30:			return BSP_HALFLIFE;
	case 29:			return BSP_QUAKE1;
	case 0x32505342:	return BSP_QUAKE1_BSP2;
	case 0x42535032:	return BSP_QUAKE1_2PSB;
	}
}

int Bsp::formatForGameEngine(int engine) {
	switch (engine) {
	default: return BSP_HALFLIFE;
	case ENGINE_HALF_LIFE: return BSP_HALFLIFE;
	case ENGINE_BLUE_SHIFT: return BSP_BLUESHIFT;
	case ENGINE_SVEN_COOP: return BSP_HALFLIFE;
	case ENGINE_QUAKE_1: return BSP_QUAKE1;
	case ENGINE_QUAKE_1_BSP2: return BSP_QUAKE1_BSP2;
	}
}

bool Bsp::write(string path, bool force) {
	if (path.rfind(".bsp") != path.size() - 4) {
		path = path + ".bsp";
	}

	if (!force && !isWritable()) {
		Alert("Overflow", cstrf("This map exceeds %s engine limits. "
			"Choose an engine with higher limits before saving (Settings -> Engine).\n\n"
			"Open the Map Limits widget to see which limits are exceeded "
			"(Widgets -> Map Limits).", g_engine_names[g_settings.engine]),
			"ok", "error", 0);
		return false;
	}

	if (!force && g_settings.ripent_safe_mode) {
		LumpState state;
		BSPHEADER head;
		if (!load_lumps(path, head, state)) {
			errorf("Save aborted because the original BSP can no longer be read, and ripent safe mode is on. "
				"Lumps cannot be compared for differences.\n", path.c_str());
			for (int i = 0; i < HEADER_LUMPS; i++) {
				delete[] state.lumps[i];
			}
			return false;
		}
		int numDiscard = 0;
		for (int i = 0; i < HEADER_LUMPS; i++) {
			if (i == LUMP_ENTITIES) {
				delete[] state.lumps[i];
				continue;
			}
			if (state.lumpLen[i] == header.lump[i].nLength && !memcmp(state.lumps[i], lumps[i], state.lumpLen[i]))
				continue;
			replace_lump(i, state.lumps[i], state.lumpLen[i]);
			numDiscard++;
			debugf("Ripent safety: Discarded changes in %s lump\n", g_lump_names[i]);
		}
		if (numDiscard)
			warnf("Ripent safety: Discarded changes in %d lumps during save\n", numDiscard);

		for (int i = 0; i < HEADER_LUMPS; i++) {
			delete[] state.lumps[i];
		}
	}

	int formatFrom = lastSaveFormat != -1 ? lastSaveFormat : lastLoadformat;
	int formatTo = formatForGameEngine(g_settings.engine);

	if (formatFrom != formatTo) {
		switch (formatFrom) {
		default:
		case BSP_QUAKE1:
		case BSP_QUAKE1_BSP2:
		case BSP_QUAKE1_2PSB:
			if (formatTo == BSP_HALFLIFE || formatTo == BSP_BLUESHIFT) {
				int ret = Alert("Format conversion", "Saving a Quake 1 map in a GoldSrc format "
					"prevents the map working in Quake 1.\n\nSave anyway?",
					"yesno", "warning", 0);
				if (ret == 0)
					return false;
			}
			break;
		case BSP_HALFLIFE:
		case BSP_BLUESHIFT:
			if (formatTo == BSP_QUAKE1 || formatTo == BSP_QUAKE1_BSP2) {
				int ret = Alert("Format conversion", "Saving a GoldSrc map in the Quake 1 format "
					"results in data loss. Textures will be recolored with the Quake palette, and WAD "
					"textures will be embedded into the BSP. Colored lightmaps will be stored in an "
					"external QLIT file (.lit) for the source ports that support them."
					"\n\nThe map will no longer work in GoldSrc.\n\nSave anyway?",
					"yesno", "warning", 0);
				if (ret == 0)
					return false;
			}
			if (formatFrom == BSP_HALFLIFE && formatTo == BSP_BLUESHIFT) {
				int ret = Alert("Format conversion", "Saving a Half-Life map in the Blue Shift format "
					"prevents the map working in Half-Life.\n\nSave anyway?",
					"yesno", "warning", 0);
				if (ret == 0)
					return false;
			}
			if (formatFrom == BSP_BLUESHIFT && formatTo == BSP_HALFLIFE) {
				int ret = Alert("Format conversion", "Saving a Blue Shift map in the Half-Life format "
					"prevents the map working in Blue Shift.\n\nSave anyway?",
					"yesno", "warning", 0);
				if (ret == 0)
					return false;
			}
			break;
		}
	}

	lastSaveFormat = formatTo;

	LumpState saveLumps = duplicate_lumps(0xffffffff);

	// convert from internal format to desired format
	externalize_lumps(formatTo, saveLumps);

	BSPHEADER saveHeader;

	switch (formatTo) {
	case BSP_QUAKE1:
		saveHeader.nVersion = 29;
		break;
	case BSP_QUAKE1_BSP2:
		saveHeader.nVersion = 0x32505342;
		break;
	default:
	case BSP_HALFLIFE:
		saveHeader.nVersion = 30;
		break;
	}

	// calculate lump offsets
	int offset = sizeof(BSPHEADER);
	for (int i = 0; i < HEADER_LUMPS; i++) {
		saveHeader.lump[i].nOffset = offset;
		saveHeader.lump[i].nLength = saveLumps.lumpLen[i];
		offset += saveLumps.lumpLen[i];
	}

	ofstream file(path, ios::out | ios::binary | ios::trunc);
	if (!file.is_open()) {
		logf("Failed to open BSP file for writing:\n%s\n", path.c_str());
		return false;
	}

	file.write((char*)&saveHeader, sizeof(BSPHEADER));

	// write the lumps
	for (int i = 0; i < HEADER_LUMPS; i++) {
		file.write((char*)saveLumps.lumps[i], saveLumps.lumpLen[i]);
		//logf("LUMP %10s = %.2f MB\n", g_lump_names[i], (float)header.lump[i].nLength / (1024.0f*1024.0f));
	}

	logf("Wrote %s: %s\n", g_bsp_format_names[formatTo], path.c_str());
	return true;
}

bool Bsp::load_lumps(string fpath, BSPHEADER& head, LumpState& state)
{
	bool valid = true;

	// Read all BSP Data
	ifstream fin(fpath, ios::binary | ios::ate);
	int size = fin.tellg();
	fin.seekg(0, fin.beg);

	memset(&state, 0, sizeof(LumpState));

	if (!fin.is_open() || size < sizeof(BSPHEADER) + sizeof(BSPLUMP) * HEADER_LUMPS)
		return false;

	fin.read((char*)&head.nVersion, sizeof(int));
	debugf("Bsp version: %d\n", head.nVersion);

	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		fin.read((char*)&head.lump[i], sizeof(BSPLUMP));
		state.lumpLen[i] = head.lump[i].nLength;
		//debugf("Read lump id: %d. Len: %d. Offset %d.\n", i, state.lumpLen[i], head.lump[i].nOffset);
	}

	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		if (state.lumpLen[i] == 0) {
			continue;
		}

		fin.seekg(head.lump[i].nOffset);
		if (fin.eof()) {
			errorf("FAILED TO READ BSP LUMP %d\n", i);
			valid = false;
		}
		else
		{
			state.lumps[i] = new byte[state.lumpLen[i]];
			fin.read((char*)state.lumps[i], state.lumpLen[i]);
		}
	}

	fin.close();

	int bspFormat = formatForFileVersion(head.nVersion);

	if (bspFormat == BSP_HALFLIFE) {
		bool isBlueShiftBsp = state.lumpLen[LUMP_PLANES] % sizeof(BSPPLANE) != 0;

		if (!isBlueShiftBsp && state.lumpLen[LUMP_ENTITIES] > 1) {
			// plane lump looks ok, now do the slower ent lump check
			bool firstAsciiIsBracket = true;

			char* c = (char*)state.lumps[LUMP_ENTITIES];
			for (int i = 0; i < state.lumpLen[LUMP_ENTITIES]; i++) {
				if (c[i] == ' ' || c[i] == '\t' || c[i] == '\r' || c[i] == '\n')
					continue;

				// first non-space character in the ent lump should be an opening bracket
				isBlueShiftBsp = c[i] != '{';
				break;
			}
		}

		if (isBlueShiftBsp) {
			bspFormat = BSP_BLUESHIFT;
		}
	}

	internalize_lumps(bspFormat, state);

	lastLoadformat = bspFormat;

	return true;
}

void Bsp::internalize_face(BSPFACE_29& src, BSPFACE& dst) {
	dst.iPlane = src.iPlane;
	dst.nPlaneSide = src.nPlaneSide;
	dst.iFirstEdge = src.iFirstEdge;
	dst.nEdges = src.nEdges;
	dst.iTextureInfo = src.iTextureInfo;
	memcpy(dst.nStyles, src.nStyles, 4);
	dst.nLightmapOffset = src.nLightmapOffset;
}

void Bsp::internalize_leaf(BSPLEAF_29& src, BSPLEAF& dst) {
	dst.nContents = src.nContents;
	dst.nVisOffset = src.nVisOffset;
	dst.nMins = vec3(src.nMins[0], src.nMins[1], src.nMins[2]);
	dst.nMaxs = vec3(src.nMaxs[0], src.nMaxs[1], src.nMaxs[2]);
	dst.iFirstMarkSurface = src.iFirstMarkSurface;
	dst.nMarkSurfaces = src.nMarkSurfaces;
	memcpy(dst.nAmbientLevels, src.nAmbientLevels, 4);
}

void Bsp::internalize_leaf(BSPLEAF_2PSB& src, BSPLEAF& dst) {
	dst.nContents = src.nContents;
	dst.nVisOffset = src.nVisOffset;
	dst.nMins = vec3(src.nMins[0], src.nMins[1], src.nMins[2]);
	dst.nMaxs = vec3(src.nMaxs[0], src.nMaxs[1], src.nMaxs[2]);
	dst.iFirstMarkSurface = src.iFirstMarkSurface;
	dst.nMarkSurfaces = src.nMarkSurfaces;
	memcpy(dst.nAmbientLevels, src.nAmbientLevels, 4);
}

void Bsp::internalize_edge(BSPEDGE_29& src, BSPEDGE& dst) {
	dst.iVertex[0] = src.iVertex[0];
	dst.iVertex[1] = src.iVertex[1];
}

void Bsp::internalize_node(BSPNODE_29& src, BSPNODE& dst) {
	dst.iPlane = src.iPlane;
	dst.iChildren[0] = src.iChildren[0];
	dst.iChildren[1] = src.iChildren[1];
	dst.nMins = vec3(src.nMins[0], src.nMins[1], src.nMins[2]);
	dst.nMaxs = vec3(src.nMaxs[0], src.nMaxs[1], src.nMaxs[2]);
	dst.firstFace = src.firstFace;
	dst.nFaces = src.nFaces;
}

void Bsp::internalize_node(BSPNODE_2PSB& src, BSPNODE& dst) {
	dst.iPlane = src.iPlane;
	dst.iChildren[0] = src.iChildren[0];
	dst.iChildren[1] = src.iChildren[1];
	dst.nMins = vec3(src.nMins[0], src.nMins[1], src.nMins[2]);
	dst.nMaxs = vec3(src.nMaxs[0], src.nMaxs[1], src.nMaxs[2]);
	dst.firstFace = src.firstFace;
	dst.nFaces = src.nFaces;
}

void Bsp::internalize_clip(BSPCLIPNODE_29& src, BSPCLIPNODE& dst) {
	dst.iPlane = src.iPlane;
	dst.iChildren[0] = src.iChildren[0];
	dst.iChildren[1] = src.iChildren[1];
}

void Bsp::internalize_mark(BSPMARKSURF_29& src, BSPMARKSURF& dst) {
	dst = src;
}

void Bsp::externalize_face(BSPFACE& src, BSPFACE_29& dst) {
	dst.iPlane = src.iPlane;
	dst.nPlaneSide = src.nPlaneSide;
	dst.iFirstEdge = src.iFirstEdge;
	dst.nEdges = src.nEdges;
	dst.iTextureInfo = src.iTextureInfo;
	memcpy(dst.nStyles, src.nStyles, 4);
	dst.nLightmapOffset = src.nLightmapOffset;
}

void Bsp::externalize_leaf(BSPLEAF& src, BSPLEAF_29& dst) {
	dst.nContents = src.nContents;
	dst.nVisOffset = src.nVisOffset;
	dst.nMins[0] = src.nMins.x;
	dst.nMins[1] = src.nMins.y;
	dst.nMins[2] = src.nMins.z;
	dst.nMaxs[0] = src.nMaxs.x;
	dst.nMaxs[1] = src.nMaxs.y;
	dst.nMaxs[2] = src.nMaxs.z;
	dst.iFirstMarkSurface = src.iFirstMarkSurface;
	dst.nMarkSurfaces = src.nMarkSurfaces;
	memcpy(dst.nAmbientLevels, src.nAmbientLevels, 4);
}

void Bsp::externalize_edge(BSPEDGE& src, BSPEDGE_29& dst) {
	dst.iVertex[0] = src.iVertex[0];
	dst.iVertex[1] = src.iVertex[1];
}

void Bsp::externalize_node(BSPNODE& src, BSPNODE_29& dst) {
	dst.iPlane = src.iPlane;
	dst.iChildren[0] = src.iChildren[0];
	dst.iChildren[1] = src.iChildren[1];
	dst.nMins[0] = src.nMins.x;
	dst.nMins[1] = src.nMins.y;
	dst.nMins[2] = src.nMins.z;
	dst.nMaxs[0] = src.nMaxs.x;
	dst.nMaxs[1] = src.nMaxs.y;
	dst.nMaxs[2] = src.nMaxs.z;
	dst.firstFace = src.firstFace;
	dst.nFaces = src.nFaces;
}

void Bsp::externalize_clip(BSPCLIPNODE& src, BSPCLIPNODE_29& dst) {
	dst.iPlane = src.iPlane;
	dst.iChildren[0] = src.iChildren[0];
	dst.iChildren[1] = src.iChildren[1];
}

void Bsp::externalize_mark(BSPMARKSURF& src, BSPMARKSURF_29& dst) {
	dst = src;
}

#define CONVERT_STRUCTS(lump_state, type_from, type_to, lump_id, func) { \
	int fileStructCount = lump_state.lumpLen[lump_id] / sizeof(type_from); \
	type_from* fileStructs = (type_from*)lump_state.lumps[lump_id]; \
	type_to* internalStructs = new type_to[fileStructCount]; \
	for (int i = 0; i < fileStructCount; i++) { \
		func(fileStructs[i], internalStructs[i]); \
	} \
	delete[] lump_state.lumps[lump_id]; \
	lump_state.lumps[lump_id] = (uint8_t*)internalStructs; \
	lump_state.lumpLen[lump_id] = fileStructCount * sizeof(type_to); \
} \

void Bsp::internalize_lumps(int fromFormat, LumpState& state) {
	if (fromFormat == BSP_BLUESHIFT) {
		// Blue Shift BSPs swap the Planes and Entities lumps
		int temp = state.lumpLen[LUMP_PLANES];
		state.lumpLen[LUMP_PLANES] = state.lumpLen[LUMP_ENTITIES];
		state.lumpLen[LUMP_ENTITIES] = temp;

		byte* temp2 = state.lumps[LUMP_PLANES];
		state.lumps[LUMP_PLANES] = state.lumps[LUMP_ENTITIES];
		state.lumps[LUMP_ENTITIES] = temp2;
	}

	if (fromFormat != BSP_QUAKE1_BSP2 && fromFormat != BSP_QUAKE1_2PSB) {
		CONVERT_STRUCTS(state, BSPFACE_29, BSPFACE, LUMP_FACES, internalize_face);
		CONVERT_STRUCTS(state, BSPLEAF_29, BSPLEAF, LUMP_LEAVES, internalize_leaf);
		CONVERT_STRUCTS(state, BSPEDGE_29, BSPEDGE, LUMP_EDGES, internalize_edge);
		CONVERT_STRUCTS(state, BSPNODE_29, BSPNODE, LUMP_NODES, internalize_node);
		CONVERT_STRUCTS(state, BSPCLIPNODE_29, BSPCLIPNODE, LUMP_CLIPNODES, internalize_clip);
		CONVERT_STRUCTS(state, BSPMARKSURF_29, BSPMARKSURF, LUMP_MARKSURFACES, internalize_mark);
	}
	if (fromFormat == BSP_QUAKE1_2PSB) {
		CONVERT_STRUCTS(state, BSPLEAF_2PSB, BSPLEAF, LUMP_LEAVES, internalize_leaf);
		CONVERT_STRUCTS(state, BSPNODE_2PSB, BSPNODE, LUMP_NODES, internalize_node);
	}

	// engine specific conversions
	if (fromFormat == BSP_QUAKE1 || fromFormat == BSP_QUAKE1_BSP2 || fromFormat == BSP_QUAKE1_2PSB) {
		convert_lightmaps(state, false);
		convert_texture_palettes(state, false);
	}
}

void Bsp::externalize_lumps(int toVersion, LumpState& state) {
	if (toVersion == BSP_QUAKE1 || toVersion == BSP_QUAKE1_BSP2 || toVersion == BSP_QUAKE1_2PSB) {
		convert_lightmaps(state, true);
		convert_texture_palettes(state, true);
	}

	if (toVersion != BSP_QUAKE1_BSP2) {
		CONVERT_STRUCTS(state, BSPFACE, BSPFACE_29, LUMP_FACES, externalize_face);
		CONVERT_STRUCTS(state, BSPLEAF, BSPLEAF_29, LUMP_LEAVES, externalize_leaf);
		CONVERT_STRUCTS(state, BSPEDGE, BSPEDGE_29, LUMP_EDGES, externalize_edge);
		CONVERT_STRUCTS(state, BSPNODE, BSPNODE_29, LUMP_NODES, externalize_node);
		CONVERT_STRUCTS(state, BSPCLIPNODE, BSPCLIPNODE_29, LUMP_CLIPNODES, externalize_clip);
		CONVERT_STRUCTS(state, BSPMARKSURF, BSPMARKSURF_29, LUMP_MARKSURFACES, externalize_mark);
	}

	if (toVersion == BSP_BLUESHIFT) {
		// Blue Shift BSPs swap the Planes and Entities lumps
		int temp = state.lumpLen[LUMP_PLANES];
		state.lumpLen[LUMP_PLANES] = state.lumpLen[LUMP_ENTITIES];
		state.lumpLen[LUMP_ENTITIES] = temp;

		byte* temp2 = state.lumps[LUMP_PLANES];
		state.lumps[LUMP_PLANES] = state.lumps[LUMP_ENTITIES];
		state.lumps[LUMP_ENTITIES] = temp2;
	}
}

bool Bsp::isWritable() {
	// it's ok for textures, allocblock, lightstyles, visdata to overflow
	// because the lump structures allow for much larger sizes than the engine can load
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
		&& (modelCount == 0 || models[0].nVisLeafs < g_limits.max_worldleaves)
		&& lightstyle_count() < 255;
}

bool Bsp::did_lumps_change(bool ignoreEntLump) {
	LumpState currentLumps = duplicate_lumps(0xffffffff);

	// Read all BSP Data
	bool lumpsChanged = true;

	int saveFormat = formatForGameEngine(g_settings.engine);

	BSPHEADER oldHead;
	LumpState fileState;
	if (!load_lumps(path, oldHead, fileState)) {
		goto cleanup;
	}

	if (saveFormat == BSP_BLUESHIFT)
		saveFormat = BSP_HALFLIFE; // don't complicate comparison

	// compare lumps in the output format
	externalize_lumps(saveFormat, currentLumps);
	externalize_lumps(saveFormat, fileState);

	for (int i = 0; i < HEADER_LUMPS; i++) {
		if (i == LUMP_ENTITIES)
			continue; // special comparison later

		if (currentLumps.lumpLen[i] != fileState.lumpLen[i]) {
			goto cleanup;
		}
	}

	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		if (fileState.lumpLen[i] == 0) {
			continue;
		}
		if (i == LUMP_ENTITIES && ignoreEntLump)
			continue;

		if (i == LUMP_ENTITIES) {
			// re-create file lump in case there are differences in spacing or something when saving
			vector<Entity*> fileEnts;
			load_ents(fileState.lumps[i], fileState.lumpLen[i], fileEnts);
			delete[] fileState.lumps[i];
			fileState.lumps[i] = create_ent_lump(fileEnts, fileState.lumpLen[i]);
		}

		bool lumpChanged = currentLumps.lumpLen[i] != fileState.lumpLen[i] ||
			memcmp(fileState.lumps[i], currentLumps.lumps[i], fileState.lumpLen[i]);

		if (lumpChanged) {
			goto cleanup;
		}
	}

	lumpsChanged = false;

cleanup:
	for (int i = 0; i < HEADER_LUMPS; i++) {
		delete[] currentLumps.lumps[i];
		delete[] fileState.lumps[i];
	}

	return lumpsChanged;
}



BspModelData::BspModelData() {}

BspModelData::~BspModelData() {
	for (WADTEX& tex : textures) {
		if (tex.data) {
			delete[] tex.data;
			tex.data = NULL;
		}
	}
}

string BspModelData::serialize() {
	uint32_t dataVersion = BSPGUY_DATA_VERSION;
	uint32_t dataType = BSPGUY_BSP_MODEL;

	uint32_t planeCount = planes.size();
	uint32_t vertCount = verts.size();
	uint32_t edgeCount = edges.size();
	uint32_t surfedgeCount = surfEdges.size();
	uint32_t texturesCount = textures.size();
	uint32_t texinfosCount = texinfos.size();
	uint32_t facesCount = faces.size();
	uint32_t lightmapsCount = lightmaps.size();
	uint32_t nodesCount = nodes.size();
	uint32_t clipnodesCount = clipnodes.size();
	uint32_t leavesCount = leaves.size();

	int textureDataSz = 0;
	for (WADTEX& tex : textures) {
		if (tex.nOffsets[0] != 0)
			textureDataSz += tex.getDataSize();
	}

	int totalBytes = planeCount * sizeof(BSPPLANE) + vertCount * sizeof(vec3)
		+ edgeCount * sizeof(BSPEDGE) + surfedgeCount * sizeof(int32_t)
		+ texinfosCount * sizeof(BSPTEXTUREINFO) + facesCount * sizeof(BSPFACE)
		+ lightmapsCount * sizeof(COLOR3) + nodesCount * sizeof(BSPNODE)
		+ clipnodesCount * sizeof(BSPCLIPNODE) + leavesCount * sizeof(BSPLEAF)
		+ texturesCount * sizeof(BSPMIPTEX) + textureDataSz
		+ sizeof(BSPMODEL) + sizeof(uint32_t) * 13;

	uint8_t* serialBytes = new uint8_t[totalBytes];
	mstream data = mstream((char*)serialBytes, totalBytes);

	data.write(&dataVersion, sizeof(uint32_t));
	data.write(&dataType, sizeof(uint32_t));

	data.write(&planeCount, sizeof(uint32_t));
	data.write(&planes[0], planeCount * sizeof(BSPPLANE));

	data.write(&vertCount, sizeof(uint32_t));
	data.write(&verts[0], vertCount * sizeof(vec3));

	data.write(&edgeCount, sizeof(uint32_t));
	data.write(&edges[0], edgeCount * sizeof(BSPEDGE));

	data.write(&surfedgeCount, sizeof(uint32_t));
	data.write(&surfEdges[0], surfedgeCount * sizeof(int32_t));

	data.write(&texturesCount, sizeof(uint32_t));
	for (WADTEX& tex : textures) {
		data.write(&tex, sizeof(BSPMIPTEX));
		if (tex.nOffsets[0] != 0)
			data.write(tex.data, tex.getDataSize());
	}

	data.write(&texinfosCount, sizeof(uint32_t));
	data.write(&texinfos[0], texinfosCount * sizeof(BSPTEXTUREINFO));

	data.write(&facesCount, sizeof(uint32_t));
	data.write(&faces[0], facesCount * sizeof(BSPFACE));

	data.write(&lightmapsCount, sizeof(uint32_t));
	data.write(&lightmaps[0], lightmapsCount * sizeof(COLOR3));

	data.write(&nodesCount, sizeof(uint32_t));
	data.write(&nodes[0], nodesCount * sizeof(BSPNODE));

	data.write(&clipnodesCount, sizeof(uint32_t));
	data.write(&clipnodes[0], clipnodesCount * sizeof(BSPCLIPNODE));

	data.write(&leavesCount, sizeof(uint32_t));
	data.write(&leaves[0], leavesCount * sizeof(BSPLEAF));

	data.write(&model, sizeof(BSPMODEL));

	if (data.eom()) {
		delete[] serialBytes;
		logf("Failed to serialize model\n");
		return "";
	}

	string encoded = base64encode(serialBytes, totalBytes);

	delete[] serialBytes;
	return encoded;
}

bool BspModelData::deserialize(string serialized) {
	vector<uint8_t> bytes = base64decode(serialized);
	mstream data((char*)&bytes[0], bytes.size());

	const uint32_t maxDataSize = 131072; // prevent invalid data using up all memory

	uint32_t dataVersion;
	uint32_t dataType;
	uint32_t planeCount;
	uint32_t vertCount;
	uint32_t edgeCount;
	uint32_t surfedgeCount;
	uint32_t texturesCount;
	uint32_t texinfosCount;
	uint32_t facesCount;
	uint32_t lightmapsCount;
	uint32_t nodesCount;
	uint32_t clipnodesCount;
	uint32_t leavesCount;

	data.read(&dataVersion, 4);
	if (dataVersion != BSPGUY_DATA_VERSION) {
		logf("Unexpected data version %d in serialized entity data. Ignoring data.\n");
		return false;
	}

	data.read(&dataType, 4);
	if (dataType != BSPGUY_BSP_MODEL) {
		logf("Unexpected data type %d in serialized entity data. Ignoring data.\n");
		return false;
	}

	data.read(&planeCount, 4);
	if (planeCount > maxDataSize) {
		logf("Unexpected plane count %u in serialized BSP model data. Ignoring data.\n", planeCount);
		return false;
	}
	BSPPLANE* planeData = new BSPPLANE[planeCount];
	data.read(planeData, planeCount * sizeof(BSPPLANE));
	planes.insert(planes.end(), planeData, planeData + planeCount);
	delete[] planeData;

	data.read(&vertCount, 4);
	if (vertCount > maxDataSize) {
		logf("Unexpected vert count %u in serialized BSP model data. Ignoring data.\n", vertCount);
		return false;
	}
	vec3* vertData = new vec3[vertCount];
	data.read(vertData, vertCount * sizeof(vec3));
	verts.insert(verts.end(), vertData, vertData + vertCount);
	delete[] vertData;

	data.read(&edgeCount, 4);
	if (edgeCount > maxDataSize) {
		logf("Unexpected edge count %u in serialized BSP model data. Ignoring data.\n", edgeCount);
		return false;
	}
	BSPEDGE* edgeData = new BSPEDGE[edgeCount];
	data.read(edgeData, edgeCount * sizeof(BSPEDGE));
	edges.insert(edges.end(), edgeData, edgeData + edgeCount);
	delete[] edgeData;

	data.read(&surfedgeCount, 4);
	if (surfedgeCount > maxDataSize) {
		logf("Unexpected surfedge count %u in serialized BSP model data. Ignoring data.\n", surfedgeCount);
		return false;
	}
	int32_t* surfedgeData = new int32_t[surfedgeCount];
	data.read(surfedgeData, surfedgeCount * sizeof(int32_t));
	surfEdges.insert(surfEdges.end(), surfedgeData, surfedgeData + surfedgeCount);
	delete[] surfedgeData;

	data.read(&texturesCount, 4);
	if (texturesCount > maxDataSize) {
		logf("Unexpected texture count %u in serialized BSP model data. Ignoring data.\n", texturesCount);
		return false;
	}
	for (int i = 0; i < texturesCount; i++) {
		WADTEX tex;
		data.read(&tex, sizeof(BSPMIPTEX));

		if (tex.nOffsets[0] != 0) {
			int sz = tex.getDataSize();
			if (sz > 1024 * 1024 * 4) {
				logf("Unexpected texture size %d in serialized BSP model data. Ignoring data.\n", sz);
				return false;
			}

			tex.data = new byte[sz];
			data.read(tex.data, sz);
		}
		else {
			tex.data = NULL;
		}

		textures.push_back(tex);
	}

	data.read(&texinfosCount, 4);
	if (texinfosCount > maxDataSize) {
		logf("Unexpected texinfo count %u in serialized BSP model data. Ignoring data.\n", texinfosCount);
		return false;
	}
	BSPTEXTUREINFO* texinfosData = new BSPTEXTUREINFO[texinfosCount];
	data.read(texinfosData, texinfosCount * sizeof(BSPTEXTUREINFO));
	texinfos.insert(texinfos.end(), texinfosData, texinfosData + texinfosCount);
	delete[] texinfosData;

	data.read(&facesCount, 4);
	if (facesCount > maxDataSize) {
		logf("Unexpected face count %u in serialized BSP model data. Ignoring data.\n", facesCount);
		return false;
	}
	BSPFACE* facesData = new BSPFACE[facesCount];
	data.read(facesData, facesCount * sizeof(BSPFACE));
	faces.insert(faces.end(), facesData, facesData + facesCount);
	delete[] facesData;

	data.read(&lightmapsCount, 4);
	if (lightmapsCount > 1024 * 1024 * 64) {
		logf("Unexpected lightmap color count %u in serialized BSP model data. Ignoring data.\n", lightmapsCount);
		return false;
	}
	COLOR3* lightmapData = new COLOR3[lightmapsCount];
	data.read(lightmapData, lightmapsCount * sizeof(COLOR3));
	lightmaps.insert(lightmaps.end(), lightmapData, lightmapData + lightmapsCount);
	delete[] lightmapData;

	data.read(&nodesCount, 4);
	if (nodesCount > maxDataSize) {
		logf("Unexpected node count %u in serialized BSP model data. Ignoring data.\n", lightmapsCount);
		return false;
	}
	BSPNODE* nodeData = new BSPNODE[nodesCount];
	data.read(nodeData, nodesCount * sizeof(BSPNODE));
	nodes.insert(nodes.end(), nodeData, nodeData + nodesCount);
	delete[] nodeData;

	data.read(&clipnodesCount, 4);
	if (clipnodesCount > maxDataSize) {
		logf("Unexpected clipnode count %u in serialized BSP model data. Ignoring data.\n", lightmapsCount);
		return false;
	}
	BSPCLIPNODE* clipnodesData = new BSPCLIPNODE[clipnodesCount];
	data.read(clipnodesData, clipnodesCount * sizeof(BSPCLIPNODE));
	clipnodes.insert(clipnodes.end(), clipnodesData, clipnodesData + clipnodesCount);
	delete[] clipnodesData;

	data.read(&leavesCount, 4);
	if (leavesCount > maxDataSize) {
		logf("Unexpected leaf count %u in serialized BSP model data. Ignoring data.\n", lightmapsCount);
		return false;
	}
	BSPLEAF* leavesData = new BSPLEAF[leavesCount];
	data.read(leavesData, leavesCount * sizeof(BSPLEAF));
	leaves.insert(leaves.end(), leavesData, leavesData + leavesCount);
	delete[] leavesData;

	data.read(&model, sizeof(BSPMODEL));

	if (data.eom()) {
		logf("Unexpected EOM in serialized BSP model data. Ignoring data.\n", lightmapsCount);
		return false;
	}

	return true;
}

string Bsp::stringify_model(int modelIdx) {
	STRUCTUSAGE usage(this);
	mark_model_structures(modelIdx, &usage, true);

	STRUCTREMAP remap(this);

	BspModelData model;

	for (int i = 0; i < usage.count.planes; i++) {
		if (usage.planes[i]) {
			remap.planes[i] = model.planes.size();
			model.planes.push_back(planes[i]);
		}
	}

	for (int i = 0; i < usage.count.verts; i++) {
		if (usage.verts[i]) {
			remap.verts[i] = model.verts.size();
			model.verts.push_back(verts[i]);
		}
	}

	// surfedges use sign to index into the edge. You can't have a signed index into edge 0.
	// So, forbid index 0 being used in the cloned edges.
	if (model.verts.size()) {
		BSPEDGE dummyEdge = { 0, 0 };
		model.edges.push_back(dummyEdge);

		for (int i = 0; i < usage.count.edges; i++) {
			if (usage.edges[i]) {
				remap.edges[i] = model.edges.size();

				BSPEDGE edge = edges[i];
				for (int k = 0; k < 2; k++) {
					edge.iVertex[k] = remap.verts[edge.iVertex[k]];
				}
				model.edges.push_back(edge);
			}
		}

		for (int i = 0; i < usage.count.surfEdges; i++) {
			if (usage.surfEdges[i]) {
				remap.surfEdges[i] = model.surfEdges.size();

				int32_t surfedge = remap.edges[abs(surfedges[i])];
				if (surfedges[i] < 0)
					surfedge = -surfedge;

				model.surfEdges.push_back(surfedge);
			}
		}
	}

	for (int i = 0; i < usage.count.textures; i++) {
		if (usage.textures[i]) {
			remap.textures[i] = model.textures.size();
			WADTEX tex = load_texture(i);
			model.textures.push_back(tex);
		}
	}

	for (int i = 0; i < usage.count.texInfos; i++) {
		if (usage.texInfo[i]) {
			remap.texInfo[i] = model.texinfos.size();

			BSPTEXTUREINFO tinfo = texinfos[i];
			tinfo.iMiptex = remap.textures[tinfo.iMiptex];
			model.texinfos.push_back(tinfo);
		}
	}

	int lightmapAppendSz = 0;
	for (int i = 0; i < usage.count.faces; i++) {
		if (usage.faces[i]) {
			remap.faces[i] = model.faces.size();

			BSPFACE face = faces[i];

			face.iFirstEdge = remap.surfEdges[face.iFirstEdge];
			face.iPlane = remap.planes[face.iPlane];
			face.iTextureInfo = remap.texInfo[face.iTextureInfo];

			// TODO: Check if face even has lighting
			int size[2];
			GetFaceLightmapSize(this, i, size);
			int lightmapCount = lightmap_count(i);
			int lightmapSz = size[0] * size[1] * lightmapCount;
			COLOR3* lightmapSrc = (COLOR3*)(lightdata + face.nLightmapOffset);
			for (int k = 0; k < lightmapSz; k++) {
				model.lightmaps.push_back(lightmapSrc[k]);
			}

			face.nLightmapOffset = lightmapCount != 0 ? lightmapAppendSz : -1;
			model.faces.push_back(face);

			lightmapAppendSz += lightmapSz * sizeof(COLOR3);
		}
	}

	for (int i = 0; i < usage.count.nodes; i++) {
		if (usage.nodes[i]) {
			remap.nodes[i] = model.nodes.size();
			model.nodes.push_back(nodes[i]);
		}
	}
	for (int i = 0; i < model.nodes.size(); i++) {
		BSPNODE& node = model.nodes[i];
		node.firstFace = remap.faces[node.firstFace];
		node.iPlane = remap.planes[node.iPlane];

		for (int k = 0; k < 2; k++) {
			if (node.iChildren[k] >= 0) {
				node.iChildren[k] = remap.nodes[node.iChildren[k]];
			}
			else {
				BSPLEAF leaf = leaves[~node.iChildren[k]];
				leaf.iFirstMarkSurface = 0;
				leaf.nMarkSurfaces = 0;
				node.iChildren[k] = ~((int32_t)model.leaves.size());
				model.leaves.push_back(leaf);
			}
		}
	}

	for (int i = 0; i < usage.count.clipnodes; i++) {
		if (usage.clipnodes[i]) {
			remap.clipnodes[i] = model.clipnodes.size();
			model.clipnodes.push_back(clipnodes[i]);
		}
	}
	for (int i = 0; i < model.clipnodes.size(); i++) {
		BSPCLIPNODE& clipnode = model.clipnodes[i];
		clipnode.iPlane = remap.planes[clipnode.iPlane];

		for (int k = 0; k < 2; k++) {
			if (clipnode.iChildren[k] > 0) {
				clipnode.iChildren[k] = remap.clipnodes[clipnode.iChildren[k]];
			}
		}
	}

	BSPMODEL& oldModel = models[modelIdx];
	memcpy(&model.model, &oldModel, sizeof(BSPMODEL));

	model.model.iFirstFace = remap.faces[oldModel.iFirstFace];
	model.model.iHeadnodes[0] = oldModel.iHeadnodes[0] < 0 ? -1 : remap.nodes[oldModel.iHeadnodes[0]];
	for (int i = 1; i < MAX_MAP_HULLS; i++) {
		model.model.iHeadnodes[i] = oldModel.iHeadnodes[i] < 0 ? -1 : remap.clipnodes[oldModel.iHeadnodes[i]];
	}
	model.model.nVisLeafs = 0; // techinically should match the old model, but leaves aren't duplicated yet

	if (model.model.nFaces == 0)
		model.model.iFirstFace = 0;

	return model.serialize();
}