#include "Bsp.h"
#include "util.h"
#include "Entity.h"

#include <algorithm>

LumpState Bsp::duplicate_lumps(int targets) {
	LumpState state;

	for (int i = 0; i < HEADER_LUMPS; i++) {
		if ((targets & (1 << i)) == 0) {
			state.lumps[i] = NULL;
			state.lumpLen[i] = 0;
			continue;
		}

		if (i == LUMP_ENTITIES) {
			update_ent_lump();
		}

		state.lumps[i] = new byte[header.lump[i].nLength];
		state.lumpLen[i] = header.lump[i].nLength;
		memcpy(state.lumps[i], lumps[i], header.lump[i].nLength);
	}

	return state;
}

void Bsp::replace_lumps(LumpState& state) {
	for (int i = 0; i < HEADER_LUMPS; i++) {
		if (state.lumps[i] == NULL) {
			continue;
		}

		delete[] lumps[i];
		lumps[i] = new byte[state.lumpLen[i]];
		memcpy(lumps[i], state.lumps[i], state.lumpLen[i]);
		header.lump[i].nLength = state.lumpLen[i];

		if (i == LUMP_ENTITIES) {
			load_ents(lumps[LUMP_ENTITIES], header.lump[LUMP_ENTITIES].nLength, ents);
		}
	}

	update_lump_pointers();
}

void Bsp::replace_lump(int lumpIdx, void* newData, int newLength) {
	delete[] lumps[lumpIdx];
	lumps[lumpIdx] = (byte*)newData;
	header.lump[lumpIdx].nLength = newLength;
	update_lump_pointers();
}

void Bsp::append_lump(int lumpIdx, void* newData, int appendLength) {
	int oldLen = header.lump[lumpIdx].nLength;
	byte* newLump = new byte[oldLen + appendLength];

	memcpy(newLump, lumps[lumpIdx], oldLen);
	memcpy(newLump + oldLen, newData, appendLength);

	replace_lump(lumpIdx, newLump, oldLen + appendLength);
}

void Bsp::update_lump_pointers() {
	planes = (BSPPLANE*)lumps[LUMP_PLANES];
	texinfos = (BSPTEXTUREINFO*)lumps[LUMP_TEXINFO];
	leaves = (BSPLEAF*)lumps[LUMP_LEAVES];
	models = (BSPMODEL*)lumps[LUMP_MODELS];
	nodes = (BSPNODE*)lumps[LUMP_NODES];
	clipnodes = (BSPCLIPNODE*)lumps[LUMP_CLIPNODES];
	faces = (BSPFACE*)lumps[LUMP_FACES];
	verts = (vec3*)lumps[LUMP_VERTICES];
	lightdata = lumps[LUMP_LIGHTING];
	surfedges = (int32_t*)lumps[LUMP_SURFEDGES];
	edges = (BSPEDGE*)lumps[LUMP_EDGES];
	marksurfs = (BSPMARKSURF*)lumps[LUMP_MARKSURFACES];
	visdata = lumps[LUMP_VISIBILITY];
	textures = lumps[LUMP_TEXTURES];

	planeCount = header.lump[LUMP_PLANES].nLength / sizeof(BSPPLANE);
	texinfoCount = header.lump[LUMP_TEXINFO].nLength / sizeof(BSPTEXTUREINFO);
	leafCount = header.lump[LUMP_LEAVES].nLength / sizeof(BSPLEAF);
	modelCount = header.lump[LUMP_MODELS].nLength / sizeof(BSPMODEL);
	nodeCount = header.lump[LUMP_NODES].nLength / sizeof(BSPNODE);
	vertCount = header.lump[LUMP_VERTICES].nLength / sizeof(vec3);
	faceCount = header.lump[LUMP_FACES].nLength / sizeof(BSPFACE);
	clipnodeCount = header.lump[LUMP_CLIPNODES].nLength / sizeof(BSPCLIPNODE);
	marksurfCount = header.lump[LUMP_MARKSURFACES].nLength / sizeof(BSPMARKSURF);
	surfedgeCount = header.lump[LUMP_SURFEDGES].nLength / sizeof(int32_t);
	edgeCount = header.lump[LUMP_EDGES].nLength / sizeof(BSPEDGE);
	textureCount = *((int32_t*)(lumps[LUMP_TEXTURES]));
	texDataLength = header.lump[LUMP_TEXTURES].nLength;
	lightDataLength = header.lump[LUMP_LIGHTING].nLength;
	visDataLength = header.lump[LUMP_VISIBILITY].nLength;

	if (!g_app) {
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
	}

	if (pvsFaceCount != faceCount) {
		pvsFaceCount = faceCount;

		if (pvsFaces) {
			delete[] pvsFaces;
		}
		pvsFaces = new bool[pvsFaceCount];
	}
}


int Bsp::remove_unused_structs(int lumpIdx, bool* usedStructs, int* remappedIndexes) {
	int structSize = 0;

	switch (lumpIdx) {
	case LUMP_PLANES: structSize = sizeof(BSPPLANE); break;
	case LUMP_VERTICES: structSize = sizeof(vec3); break;
	case LUMP_NODES: structSize = sizeof(BSPNODE); break;
	case LUMP_TEXINFO: structSize = sizeof(BSPTEXTUREINFO); break;
	case LUMP_FACES: structSize = sizeof(BSPFACE); break;
	case LUMP_CLIPNODES: structSize = sizeof(BSPCLIPNODE); break;
	case LUMP_LEAVES: structSize = sizeof(BSPLEAF); break;
	case LUMP_MARKSURFACES: structSize = sizeof(BSPMARKSURF); break;
	case LUMP_EDGES: structSize = sizeof(BSPEDGE); break;
	case LUMP_SURFEDGES: structSize = sizeof(int32_t); break;
	default:
		errorf("\nERROR: Invalid lump %d passed to remove_unused_structs\n", lumpIdx);
		return 0;
	}

	int oldStructCount = header.lump[lumpIdx].nLength / structSize;

	int removeCount = 0;
	for (int i = 0; i < oldStructCount; i++) {
		removeCount += !usedStructs[i];
	}

	int newStructCount = std::max(0, oldStructCount - removeCount);

	byte* oldStructs = lumps[lumpIdx];
	byte* newStructs = new byte[newStructCount * structSize];

	for (int i = 0, k = 0; i < oldStructCount; i++) {
		if (!usedStructs[i]) {
			remappedIndexes[i] = 0; // prevent out-of-bounds remaps later
			continue;
		}
		memcpy(newStructs + k * structSize, oldStructs + i * structSize, structSize);
		remappedIndexes[i] = k++;
	}

	replace_lump(lumpIdx, newStructs, newStructCount * structSize);

	return removeCount;
}

STRUCTCOUNT Bsp::remove_unused_model_structures(bool deleteModels) {
	int oldVisLeafCount = 0;
	count_leaves(models[0].iHeadnodes[0], oldVisLeafCount);
	//oldVisLeafCount = models[0].nVisLeafs;

	// marks which structures should not be moved
	STRUCTUSAGE usedStructures(this);

	bool* usedModels = new bool[modelCount];
	memset(usedModels, 0, sizeof(bool) * modelCount);
	usedModels[0] = true; // never delete worldspawn
	for (int i = 0; i < ents.size(); i++) {
		int modelIdx = ents[i]->getBspModelIdx();
		if (modelIdx >= 0 && modelIdx < modelCount) {
			usedModels[modelIdx] = true;
		}
	}

	int deletedModels = 0;
	// reversed so models can be deleted without shifting the next delete index
	for (int i = modelCount - 1; i >= 0; i--) {
		if (!usedModels[i] && deleteModels) {
			delete_model(i);
			deletedModels++;
		}
		else {
			mark_model_structures(i, &usedStructures, false);
		}
	}

	delete[] usedModels;

	STRUCTREMAP remap(this);
	STRUCTCOUNT removeCount;
	memset(&removeCount, 0, sizeof(STRUCTCOUNT));

	usedStructures.edges[0] = true; // first edge is never used but maps break without it?

	byte* oldLeaves = new byte[header.lump[LUMP_LEAVES].nLength];
	memcpy(oldLeaves, lumps[LUMP_LEAVES], header.lump[LUMP_LEAVES].nLength);

	if (lightDataLength) {
		removeCount.lightstyles = remove_unused_lightstyles();
		removeCount.lightdata = remove_unused_lightmaps(usedStructures.faces);
	}

	removeCount.planes = remove_unused_structs(LUMP_PLANES, usedStructures.planes, remap.planes);
	removeCount.clipnodes = remove_unused_structs(LUMP_CLIPNODES, usedStructures.clipnodes, remap.clipnodes);
	removeCount.nodes = remove_unused_structs(LUMP_NODES, usedStructures.nodes, remap.nodes);
	removeCount.leaves = remove_unused_structs(LUMP_LEAVES, usedStructures.leaves, remap.leaves);
	removeCount.markSurfs = remove_unused_structs(LUMP_MARKSURFACES, usedStructures.markSurfs, remap.markSurfs);
	removeCount.faces = remove_unused_structs(LUMP_FACES, usedStructures.faces, remap.faces);
	removeCount.surfEdges = remove_unused_structs(LUMP_SURFEDGES, usedStructures.surfEdges, remap.surfEdges);
	removeCount.texInfos = remove_unused_structs(LUMP_TEXINFO, usedStructures.texInfo, remap.texInfo);
	removeCount.edges = remove_unused_structs(LUMP_EDGES, usedStructures.edges, remap.edges);
	removeCount.verts = remove_unused_structs(LUMP_VERTICES, usedStructures.verts, remap.verts);
	removeCount.textures = remove_unused_textures(usedStructures.textures, remap.textures);
	removeCount.models = deletedModels;

	STRUCTCOUNT newCounts(this);

	for (int i = 0; i < newCounts.markSurfs; i++) {
		marksurfs[i] = remap.faces[marksurfs[i]];
	}
	for (int i = 0; i < newCounts.surfEdges; i++) {
		surfedges[i] = surfedges[i] >= 0 ? remap.edges[surfedges[i]] : -remap.edges[-surfedges[i]];
	}
	for (int i = 0; i < newCounts.edges; i++) {
		for (int k = 0; k < 2; k++) {
			edges[i].iVertex[k] = remap.verts[edges[i].iVertex[k]];
		}
	}
	for (int i = 0; i < newCounts.texInfos; i++) {
		texinfos[i].iMiptex = remap.textures[texinfos[i].iMiptex];
	}
	for (int i = 0; i < newCounts.clipnodes; i++) {
		clipnodes[i].iPlane = remap.planes[clipnodes[i].iPlane];
		for (int k = 0; k < 2; k++) {
			if (clipnodes[i].iChildren[k] >= 0) {
				clipnodes[i].iChildren[k] = remap.clipnodes[clipnodes[i].iChildren[k]];
			}
		}
	}
	for (int i = 0; i < newCounts.nodes; i++) {
		nodes[i].iPlane = remap.planes[nodes[i].iPlane];
		if (nodes[i].nFaces > 0)
			nodes[i].firstFace = remap.faces[nodes[i].firstFace];
		else
			nodes[i].firstFace = 0;
		for (int k = 0; k < 2; k++) {
			if (nodes[i].iChildren[k] >= 0) {
				nodes[i].iChildren[k] = remap.nodes[nodes[i].iChildren[k]];
			}
			else {
				int32_t leafIdx = ~nodes[i].iChildren[k];
				nodes[i].iChildren[k] = ~((int32_t)remap.leaves[leafIdx]);
			}
		}
	}
	for (int i = 1; i < newCounts.leaves; i++) {
		if (leaves[i].nMarkSurfaces > 0)
			leaves[i].iFirstMarkSurface = remap.markSurfs[leaves[i].iFirstMarkSurface];
		else
			leaves[i].iFirstMarkSurface = 0;
	}
	for (int i = 0; i < newCounts.faces; i++) {
		faces[i].iPlane = remap.planes[faces[i].iPlane];
		if (faces[i].nEdges > 0)
			faces[i].iFirstEdge = remap.surfEdges[faces[i].iFirstEdge];
		else
			faces[i].iFirstEdge = 0;
		faces[i].iTextureInfo = remap.texInfo[faces[i].iTextureInfo];

		BSPTEXTUREINFO& tinfo = texinfos[faces[i].iTextureInfo];
		BSPTEXTUREINFO* radinfo = get_embedded_rad_texinfo(tinfo);
		if (radinfo) {
			BSPMIPTEX* tex = get_texture(tinfo.iMiptex);
			if (!tex) {
				continue;
			}

			int oldIndex = atoi(&tex->szName[5]);
			int newIndex = remap.texInfo[oldIndex];

			// from VHLT loadtextures.cpp
			tex->szName[5] = '0' + (newIndex / 10000) % 10; // store the original texinfo
			tex->szName[6] = '0' + (newIndex / 1000) % 10;
			tex->szName[7] = '0' + (newIndex / 100) % 10;
			tex->szName[8] = '0' + (newIndex / 10) % 10;
			tex->szName[9] = '0' + (newIndex) % 10;
		}
	}

	for (int i = 0; i < modelCount; i++) {
		if (models[i].nFaces > 0)
			models[i].iFirstFace = remap.faces[models[i].iFirstFace];
		else
			models[i].iFirstFace = 0;
		if (models[i].iHeadnodes[0] >= nodeCount)
			models[i].iHeadnodes[0] = -1; // invalid offset
		if (models[i].iHeadnodes[0] >= 0)
			models[i].iHeadnodes[0] = remap.nodes[models[i].iHeadnodes[0]];
		for (int k = 1; k < MAX_MAP_HULLS; k++) {
			if (models[i].iHeadnodes[k] >= clipnodeCount)
				models[i].iHeadnodes[k] = -1; // invalid offset
			if (models[i].iHeadnodes[k] >= 0)
				models[i].iHeadnodes[k] = remap.clipnodes[models[i].iHeadnodes[k]];
		}
	}

	models[0].nVisLeafs = 0;
	count_leaves(models[0].iHeadnodes[0], models[0].nVisLeafs);

	if (visDataLength)
		removeCount.visdata = remove_unused_visdata(&remap, (BSPLEAF*)oldLeaves,
			usedStructures.count.leaves, oldVisLeafCount);

	return removeCount;
}


void Bsp::mark_face_structures(int iFace, STRUCTUSAGE* usage) {
	if (iFace >= faceCount)
		return;

	BSPFACE& face = faces[iFace];
	usage->faces[iFace] = true;

	for (int e = 0; e < face.nEdges; e++) {
		int32_t edgeIdx = surfedges[face.iFirstEdge + e];
		BSPEDGE& edge = edges[abs(edgeIdx)];
		int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];

		usage->surfEdges[face.iFirstEdge + e] = true;
		usage->edges[abs(edgeIdx)] = true;
		usage->verts[vertIdx] = true;
	}

	BSPTEXTUREINFO& tinfo = texinfos[face.iTextureInfo];
	BSPTEXTUREINFO* radinfo = get_embedded_rad_texinfo(tinfo);

	if (radinfo) {
		int offset = radinfo - texinfos;
		usage->texInfo[offset] = true;
		usage->textures[radinfo->iMiptex] = true;
	}

	usage->texInfo[face.iTextureInfo] = true;
	usage->planes[face.iPlane] = true;
	usage->textures[tinfo.iMiptex] = true;
}

void Bsp::mark_node_structures(int iNode, STRUCTUSAGE* usage, bool skipLeaves) {
	BSPNODE& node = nodes[iNode];

	usage->nodes[iNode] = true;
	usage->planes[node.iPlane] = true;

	for (int i = 0; i < node.nFaces; i++) {
		mark_face_structures(node.firstFace + i, usage);
	}

	for (int i = 0; i < 2; i++) {
		if (node.iChildren[i] >= 0) {
			mark_node_structures(node.iChildren[i], usage, skipLeaves);
		}
		else if (!skipLeaves) {
			BSPLEAF& leaf = leaves[~node.iChildren[i]];
			for (int k = 0; k < leaf.nMarkSurfaces; k++) {
				usage->markSurfs[leaf.iFirstMarkSurface + k] = true;
				mark_face_structures(marksurfs[leaf.iFirstMarkSurface + k], usage);
			}

			usage->leaves[~node.iChildren[i]] = true;
		}
	}
}

void Bsp::mark_clipnode_structures(int iNode, STRUCTUSAGE* usage) {
	BSPCLIPNODE& node = clipnodes[iNode];

	usage->clipnodes[iNode] = true;
	usage->planes[node.iPlane] = true;

	for (int i = 0; i < 2; i++) {
		if (node.iChildren[i] >= 0) {
			mark_clipnode_structures(node.iChildren[i], usage);
		}
	}
}

void Bsp::mark_model_structures(int modelIdx, STRUCTUSAGE* usage, bool skipLeaves) {
	BSPMODEL& model = models[modelIdx];

	for (int i = 0; i < model.nFaces; i++) {
		mark_face_structures(model.iFirstFace + i, usage);
	}

	if (model.iHeadnodes[0] >= 0 && model.iHeadnodes[0] < nodeCount)
		mark_node_structures(model.iHeadnodes[0], usage, skipLeaves);
	for (int k = 1; k < MAX_MAP_HULLS; k++) {
		if (modelIdx != 0 && model.iHeadnodes[k] == models[0].iHeadnodes[k])
			continue; // quake maps redirect unused hulls to the world. skip all that extra work.

		if (model.iHeadnodes[k] >= 0 && model.iHeadnodes[k] < clipnodeCount)
			mark_clipnode_structures(model.iHeadnodes[k], usage);
	}
}


void Bsp::remap_face_structures(int faceIdx, STRUCTREMAP* remap) {
	if (remap->visitedFaces[faceIdx]) {
		return;
	}
	remap->visitedFaces[faceIdx] = true;

	BSPFACE& face = faces[faceIdx];

	face.iPlane = remap->planes[face.iPlane];
	face.iTextureInfo = remap->texInfo[face.iTextureInfo];

	for (int i = 0; i < face.nEdges; i++) {
		int32_t edgeIdx = surfedges[face.iFirstEdge + i];
		BSPEDGE& edge = edges[abs(edgeIdx)];

		// quake maps will re-use vertices across faces in different models, but the surfedges/edges
		// are not shared, so this should be enough to fix those maps.
		int k = edgeIdx >= 0 ? 1 : 0;
		edge.iVertex[k] = remap->verts[edge.iVertex[k]];
	}
	//logf("REMAP FACE %d: %d -> %d\n", faceIdx, face.iFirstEdge, remap->surfEdges[face.iFirstEdge]);
	//logf("REMAP FACE %d: %d -> %d\n", faceIdx, face.iTextureInfo, remap->texInfo[face.iTextureInfo]);
	//face.iFirstEdge = remap->surfEdges[face.iFirstEdge];
}

void Bsp::remap_node_structures(int iNode, STRUCTREMAP* remap) {
	BSPNODE& node = nodes[iNode];

	remap->visitedNodes[iNode] = true;

	node.iPlane = remap->planes[node.iPlane];

	for (int i = 0; i < node.nFaces; i++) {
		remap_face_structures(node.firstFace + i, remap);
	}

	for (int i = 0; i < 2; i++) {
		if (node.iChildren[i] >= 0) {
			node.iChildren[i] = remap->nodes[node.iChildren[i]];
			if (!remap->visitedNodes[node.iChildren[i]]) {
				remap_node_structures(node.iChildren[i], remap);
			}
		}
	}
}

void Bsp::remap_clipnode_structures(int iNode, STRUCTREMAP* remap) {
	BSPCLIPNODE& node = clipnodes[iNode];

	remap->visitedClipnodes[iNode] = true;
	node.iPlane = remap->planes[node.iPlane];

	for (int i = 0; i < 2; i++) {
		if (node.iChildren[i] >= 0) {
			if (node.iChildren[i] < remap->count.clipnodes) {
				node.iChildren[i] = remap->clipnodes[node.iChildren[i]];
			}

			if (!remap->visitedClipnodes[node.iChildren[i]])
				remap_clipnode_structures(node.iChildren[i], remap);
		}
	}
}

void Bsp::remap_model_structures(int modelIdx, STRUCTREMAP* remap) {
	BSPMODEL& model = ((BSPMODEL*)lumps[LUMP_MODELS])[modelIdx];

	// sometimes the face index is invalid when the model has no faces
	if (model.nFaces > 0)
		model.iFirstFace = remap->faces[model.iFirstFace];

	if (model.iHeadnodes[0] >= 0) {
		model.iHeadnodes[0] = remap->nodes[model.iHeadnodes[0]];
		if (model.iHeadnodes[0] < clipnodeCount && !remap->visitedNodes[model.iHeadnodes[0]]) {
			remap_node_structures(model.iHeadnodes[0], remap);
		}
	}
	for (int k = 1; k < MAX_MAP_HULLS; k++) {
		if (model.iHeadnodes[k] >= 0) {
			model.iHeadnodes[k] = remap->clipnodes[model.iHeadnodes[k]];
			if (model.iHeadnodes[k] < clipnodeCount && !remap->visitedClipnodes[model.iHeadnodes[k]]) {
				remap_clipnode_structures(model.iHeadnodes[k], remap);
			}
		}
	}
}


int Bsp::create_plane() {
	BSPPLANE* newPlanes = new BSPPLANE[planeCount + 1];
	memcpy(newPlanes, planes, planeCount * sizeof(BSPPLANE));

	BSPPLANE& newPlane = newPlanes[planeCount];
	memset(&newPlane, 0, sizeof(BSPPLANE));

	replace_lump(LUMP_PLANES, newPlanes, (planeCount + 1) * sizeof(BSPPLANE));

	return planeCount - 1;
}

int Bsp::create_texinfo() {
	BSPTEXTUREINFO* newTexinfos = new BSPTEXTUREINFO[texinfoCount + 1];
	memcpy(newTexinfos, texinfos, texinfoCount * sizeof(BSPTEXTUREINFO));

	BSPTEXTUREINFO& newTexinfo = newTexinfos[texinfoCount];
	memset(&newTexinfo, 0, sizeof(BSPTEXTUREINFO));

	replace_lump(LUMP_TEXINFO, newTexinfos, (texinfoCount + 1) * sizeof(BSPTEXTUREINFO));

	return texinfoCount - 1;
}

int Bsp::create_clipnode() {
	BSPCLIPNODE* newNodes = new BSPCLIPNODE[clipnodeCount + 1];
	memcpy(newNodes, clipnodes, clipnodeCount * sizeof(BSPCLIPNODE));

	BSPCLIPNODE* newNode = &newNodes[clipnodeCount];
	memset(newNode, 0, sizeof(BSPCLIPNODE));

	replace_lump(LUMP_CLIPNODES, newNodes, (clipnodeCount + 1) * sizeof(BSPCLIPNODE));

	return clipnodeCount - 1;
}

int Bsp::create_model() {
	BSPMODEL* newModels = new BSPMODEL[modelCount + 1];
	memcpy(newModels, models, modelCount * sizeof(BSPMODEL));

	BSPMODEL& newModel = newModels[modelCount];
	memset(&newModel, 0, sizeof(BSPMODEL));

	int newModelIdx = modelCount;
	replace_lump(LUMP_MODELS, newModels, (modelCount + 1) * sizeof(BSPMODEL));

	return newModelIdx;
}

int Bsp::create_leaf(int contents) {
	BSPLEAF* newLeaves = new BSPLEAF[leafCount + 1];
	memcpy(newLeaves, leaves, leafCount * sizeof(BSPLEAF));

	BSPLEAF& newLeaf = newLeaves[leafCount];
	memset(&newLeaf, 0, sizeof(BSPLEAF));

	newLeaf.nVisOffset = -1;
	newLeaf.nContents = contents;

	int newLeafIdx = leafCount;

	replace_lump(LUMP_LEAVES, newLeaves, (leafCount + 1) * sizeof(BSPLEAF));

	return newLeafIdx;
}

int Bsp::create_node() {
	BSPNODE* newNodes = new BSPNODE[nodeCount + 1];
	memcpy(newNodes, nodes, nodeCount * sizeof(BSPNODE));

	BSPNODE& newNode = newNodes[nodeCount];
	memset(&newNode, 0, sizeof(BSPNODE));

	replace_lump(LUMP_NODES, newNodes, (nodeCount + 1) * sizeof(BSPNODE));

	return nodeCount - 1;
}


void Bsp::insert_marksurfs(int offset, int count) {
	BSPMARKSURF* newMarks = new BSPMARKSURF[marksurfCount + count];
	memcpy(newMarks, marksurfs, offset * sizeof(BSPMARKSURF));
	memcpy(newMarks + offset + count, marksurfs + offset, (marksurfCount - offset) * sizeof(BSPMARKSURF));

	memset(newMarks + offset, 0, count * sizeof(BSPMARKSURF));

	replace_lump(LUMP_MARKSURFACES, newMarks, (marksurfCount + count) * sizeof(BSPMARKSURF));

	for (int i = 0; i < leafCount; i++) {
		BSPLEAF& leaf = leaves[i];

		if (!leaf.nMarkSurfaces)
			continue;

		if (leaf.iFirstMarkSurface >= offset) {
			leaf.iFirstMarkSurface += count;
		}
	}
}

void Bsp::insert_leaves(int offset, int count) {
	BSPLEAF* newLeaves = new BSPLEAF[leafCount + count];
	memcpy(newLeaves, leaves, offset * sizeof(BSPLEAF));
	memcpy(newLeaves + offset + count, leaves + offset, (leafCount - offset) * sizeof(BSPLEAF));

	memset(newLeaves + offset, 0, count * sizeof(BSPLEAF));

	replace_lump(LUMP_LEAVES, newLeaves, (leafCount + count) * sizeof(BSPLEAF));

	for (int i = 0; i < nodeCount; i++) {
		BSPNODE& node = nodes[i];

		for (int k = 0; k < 2; k++) {
			if (node.iChildren[k] < 0 && ~node.iChildren[k] >= offset) {
				node.iChildren[k] = ~(~node.iChildren[k] + count);
			}
		}
	}
}

void Bsp::insert_nodes(int offset, int count) {
	BSPNODE* newNodes = new BSPNODE[nodeCount + count];
	memcpy(newNodes, nodes, offset * sizeof(BSPNODE));
	memcpy(newNodes + offset + count, nodes + offset, (nodeCount - offset) * sizeof(BSPNODE));

	memset(newNodes + offset, 0, count * sizeof(BSPNODE));

	replace_lump(LUMP_NODES, newNodes, (nodeCount + count) * sizeof(BSPNODE));

	for (int i = 0; i < nodeCount; i++) {
		BSPNODE& node = nodes[i];

		for (int k = 0; k < 2; k++) {
			if (node.iChildren[k] >= offset) {
				node.iChildren[k] += count;
			}
		}
	}

	for (int i = 0; i < modelCount; i++) {
		BSPMODEL& model = models[i];

		if (model.iHeadnodes[0] >= offset) {
			model.iHeadnodes[0] += count;
		}
	}
}
