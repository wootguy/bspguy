#include "BspMerger.h"
#include "Entity.h"
#include "vis.h"
#include <algorithm>

void BspMerger::merge_ents(Bsp& mapA, Bsp& mapB)
{
	g_progress.update("Merging entities", mapA.ents.size() + mapB.ents.size());

	int oldEntCount = mapA.ents.size();

	// update model indexes since this map's models will be appended after the other map's models
	int otherModelCount = (mapB.header.lump[LUMP_MODELS].nLength / sizeof(BSPMODEL)) - 1;
	for (int i = 0; i < mapA.ents.size(); i++) {
		if (!mapA.ents[i]->hasKey("model") || mapA.ents[i]->getKeyvalue("model")[0] != '*') {
			continue;
		}
		string modelIdxStr = mapA.ents[i]->getKeyvalue("model").substr(1);

		if (!isNumeric(modelIdxStr)) {
			continue;
		}

		int newModelIdx = atoi(modelIdxStr.c_str()) + otherModelCount;
		mapA.ents[i]->setOrAddKeyvalue("model", "*" + to_string(newModelIdx));

		g_progress.tick();
	}

	for (int i = 0; i < mapB.ents.size(); i++) {
		if (mapB.ents[i]->getClassname() == "worldspawn") {
			Entity* otherWorldspawn = mapB.ents[i];

			vector<string> otherWads = splitString(otherWorldspawn->getKeyvalue("wad"), ";");

			// strip paths from wad names
			for (int j = 0; j < otherWads.size(); j++) {
				otherWads[j] = basename(otherWads[j]);
			}

			Entity* worldspawn = NULL;
			for (int k = 0; k < mapA.ents.size(); k++) {
				if (mapA.ents[k]->getClassname() == "worldspawn") {
					worldspawn = mapA.ents[k];
					break;
				}
			}

			// merge wad list
			vector<string> thisWads = splitString(worldspawn->getKeyvalue("wad"), ";");

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

			worldspawn->setOrAddKeyvalue("wad", "");
			for (int j = 0; j < thisWads.size(); j++) {
				worldspawn->setOrAddKeyvalue("wad", worldspawn->getKeyvalue("wad") + thisWads[j] + ";");
			}

			// include prefixed version of the other maps keyvalues
			StringMap otherWorldKeys = otherWorldspawn->getAllKeyvalues();
			StringMap::iterator_t iter;
			while (otherWorldKeys.iterate(iter)) {
				if (!strcmp(iter.key, "classname") || !strcmp(iter.key, "wad")) {
					continue;
				}
				// TODO: unknown keyvalues crash the game? Try something else.
				//worldspawn->setOrAddKeyvalue(Keyvalue(mapB.name + "_" + it->first, it->second));
			}
		}
		else {
			Entity* copy = new Entity();
			*copy = *mapB.ents[i];
			mapA.ents.push_back(copy);
		}

		g_progress.tick();
	}

	mapA.update_ent_lump();
}

void BspMerger::merge_planes(Bsp& mapA, Bsp& mapB) {
	g_progress.update("Merging planes", mapA.planeCount + mapB.planeCount);

	vector<BSPPLANE> mergedPlanes;
	mergedPlanes.reserve(mapA.planeCount + mapB.planeCount);

	for (int i = 0; i < mapA.planeCount; i++) {
		mergedPlanes.push_back(mapA.planes[i]);
		g_progress.tick();
	}
	for (int i = 0; i < mapB.planeCount; i++) {
		bool isUnique = true;
		for (int k = 0; k < mapA.planeCount; k++) {
			if (memcmp(&mapB.planes[i], &mapA.planes[k], sizeof(BSPPLANE)) == 0) {
				isUnique = false;
				planeRemap.push_back(k);
				break;
			}
		}
		if (isUnique) {
			planeRemap.push_back(mergedPlanes.size());
			mergedPlanes.push_back(mapB.planes[i]);
		}

		g_progress.tick();
	}

	int newLen = mergedPlanes.size() * sizeof(BSPPLANE);
	int duplicates = (mapA.planeCount + mapB.planeCount) - mergedPlanes.size();

	//logf("\nRemoved %d duplicate planes\n", duplicates);

	byte* newPlanes = new byte[newLen];
	memcpy(newPlanes, &mergedPlanes[0], newLen);

	mapA.replace_lump(LUMP_PLANES, newPlanes, newLen);
}

void BspMerger::merge_textures(Bsp& mapA, Bsp& mapB) {
	uint32_t newTexCount = 0;

	// temporary buffer for holding miptex + embedded textures (too big but doesn't matter)
	uint maxMipTexDataSize = mapA.header.lump[LUMP_TEXTURES].nLength + mapB.header.lump[LUMP_TEXTURES].nLength;
	byte* newMipTexData = new byte[maxMipTexDataSize];

	byte* mipTexWritePtr = newMipTexData;

	// offsets relative to the start of the mipmap data, not the lump
	uint32_t* mipTexOffsets = new uint32_t[mapA.textureCount + mapB.textureCount];

	g_progress.update("Merging textures", mapA.textureCount + mapB.textureCount);

	uint thisMergeSz = (mapA.textureCount + 1) * sizeof(int32_t);
	for (int i = 0; i < mapA.textureCount; i++) {
		int32_t offset = ((int32_t*)mapA.textures)[i + 1];

		if (offset == -1) {
			mipTexOffsets[newTexCount] = -1;
		}
		else {
			BSPMIPTEX* tex = (BSPMIPTEX*)(mapA.textures + offset);
			int sz = getBspTextureSize(tex);
			//memset(tex->nOffsets, 0, sizeof(uint32) * 4);

			mipTexOffsets[newTexCount] = (mipTexWritePtr - newMipTexData);
			memcpy(mipTexWritePtr, tex, sz);
			mipTexWritePtr += sz;
			thisMergeSz += sz;
		}
		newTexCount++;

		g_progress.tick();
	}

	uint otherMergeSz = (mapB.textureCount + 1) * sizeof(int32_t);
	for (int i = 0; i < mapB.textureCount; i++) {
		int32_t offset = ((int32_t*)mapB.textures)[i + 1];

		if (offset != -1) {
			bool isUnique = true;
			BSPMIPTEX* tex = (BSPMIPTEX*)(mapB.textures + offset);
			int sz = getBspTextureSize(tex);

			for (int k = 0; k < mapA.textureCount; k++) {
				if (mipTexOffsets[k] == -1) {
					continue;
				}
				BSPMIPTEX* thisTex = (BSPMIPTEX*)(newMipTexData + mipTexOffsets[k]);
				if (strncmp(tex->szName, thisTex->szName, sizeof(thisTex->szName)) == 0) {
					isUnique = false;
					texRemap.push_back(k);
					break;
				}
			}

			if (isUnique) {
				mipTexOffsets[newTexCount] = (mipTexWritePtr - newMipTexData);
				texRemap.push_back(newTexCount);
				memcpy(mipTexWritePtr, tex, sz); // Note: won't work if pixel data isn't immediately after struct
				mipTexWritePtr += sz;
				newTexCount++;
				otherMergeSz += sz;
			}
		}
		else {
			mipTexOffsets[newTexCount] = -1;
			texRemap.push_back(newTexCount);
			newTexCount++;
		}

		g_progress.tick();
	}

	int duplicates = newTexCount - (mapA.textureCount + mapB.textureCount);

	uint texHeaderSize = (newTexCount + 1) * sizeof(int32_t);
	uint newLen = (mipTexWritePtr - newMipTexData) + texHeaderSize;
	byte* newTextureData = new byte[newLen];

	// write texture lump header
	uint32_t* texHeader = (uint32_t*)(newTextureData);
	texHeader[0] = newTexCount;
	for (int i = 0; i < newTexCount; i++) {
		texHeader[i + 1] = (mipTexOffsets[i] == -1) ? -1 : mipTexOffsets[i] + texHeaderSize;
	}

	memcpy(newTextureData + texHeaderSize, newMipTexData, mipTexWritePtr - newMipTexData);

	delete[] mipTexOffsets;
	mapA.replace_lump(LUMP_TEXTURES, newTextureData, newLen);
}

void BspMerger::merge_vertices(Bsp& mapA, Bsp& mapB) {
	thisVertCount = mapA.vertCount;
	int totalVertCount = thisVertCount + mapB.vertCount;

	g_progress.update("Merging verticies", 3);
	g_progress.tick();

	vec3* newVerts = new vec3[totalVertCount];
	memcpy(newVerts, mapA.verts, thisVertCount * sizeof(vec3));
	g_progress.tick();
	memcpy(newVerts + thisVertCount, mapB.verts, mapB.vertCount * sizeof(vec3));
	g_progress.tick();

	mapA.replace_lump(LUMP_VERTICES, newVerts, totalVertCount * sizeof(vec3));
}

void BspMerger::merge_texinfo(Bsp& mapA, Bsp& mapB) {
	g_progress.update("Merging texinfos", mapA.texinfoCount + mapB.texinfoCount);

	vector<BSPTEXTUREINFO> mergedInfo;
	mergedInfo.reserve(mapA.texinfoCount + mapB.texinfoCount);

	for (int i = 0; i < mapA.texinfoCount; i++) {
		mergedInfo.push_back(mapA.texinfos[i]);
		g_progress.tick();
	}

	for (int i = 0; i < mapB.texinfoCount; i++) {
		BSPTEXTUREINFO info = mapB.texinfos[i];
		info.iMiptex = texRemap[info.iMiptex];

		bool isUnique = true;
		for (int k = 0; k < mapA.texinfoCount; k++) {
			if (memcmp(&info, &mapA.texinfos[k], sizeof(BSPTEXTUREINFO)) == 0) {
				texInfoRemap.push_back(k);
				isUnique = false;
				break;
			}
		}

		if (isUnique) {
			texInfoRemap.push_back(mergedInfo.size());
			mergedInfo.push_back(info);
		}
		g_progress.tick();
	}

	int newLen = mergedInfo.size() * sizeof(BSPTEXTUREINFO);
	int duplicates = mergedInfo.size() - (mapA.texinfoCount + mapB.texinfoCount);

	byte* newTexinfoData = new byte[newLen];
	memcpy(newTexinfoData, &mergedInfo[0], newLen);

	mapA.replace_lump(LUMP_TEXINFO, newTexinfoData, newLen);
}

void BspMerger::merge_faces(Bsp& mapA, Bsp& mapB) {
	thisFaceCount = mapA.faceCount;
	otherFaceCount = mapB.faceCount;
	thisWorldFaceCount = mapA.models[0].nFaces;
	int totalFaceCount = thisFaceCount + mapB.faceCount;

	g_progress.update("Merging faces", mapB.faceCount + 1);
	g_progress.tick();

	BSPFACE* newFaces = new BSPFACE[totalFaceCount];

	// world model faces come first so they can be merged into one group (model.nFaces is used to render models)
	// assumes world model faces always come first
	int appendOffset = 0;
	// copy world faces
	int worldFaceCountA = thisWorldFaceCount;
	int worldFaceCountB = mapB.models[0].nFaces;
	memcpy(newFaces + appendOffset, mapA.faces, worldFaceCountA * sizeof(BSPFACE));
	appendOffset += worldFaceCountA;
	memcpy(newFaces + appendOffset, mapB.faces, worldFaceCountB * sizeof(BSPFACE));
	appendOffset += worldFaceCountB;

	// copy B's submodel faces followed by A's
	int submodelFaceCountA = mapA.faceCount - worldFaceCountA;
	int submodelFaceCountB = mapB.faceCount - worldFaceCountB;
	memcpy(newFaces + appendOffset, mapB.faces + worldFaceCountB, submodelFaceCountB * sizeof(BSPFACE));
	appendOffset += submodelFaceCountB;
	memcpy(newFaces + appendOffset, mapA.faces + worldFaceCountA, submodelFaceCountA * sizeof(BSPFACE));

	for (int i = 0; i < totalFaceCount; i++) {
		// only update B's faces
		if (i < worldFaceCountA || i >= worldFaceCountA + mapB.faceCount)
			continue;

		BSPFACE& face = newFaces[i];
		face.iPlane = planeRemap[face.iPlane];
		face.iFirstEdge = face.iFirstEdge + thisSurfEdgeCount;
		face.iTextureInfo = texInfoRemap[face.iTextureInfo];
		g_progress.tick();
	}

	mapA.replace_lump(LUMP_FACES, newFaces, totalFaceCount * sizeof(BSPFACE));
}

void BspMerger::merge_leaves(Bsp& mapA, Bsp& mapB) {
	thisLeafCount = mapA.header.lump[LUMP_LEAVES].nLength / sizeof(BSPLEAF);
	otherLeafCount = mapB.header.lump[LUMP_LEAVES].nLength / sizeof(BSPLEAF);

	int thisWorldLeafCount = ((BSPMODEL*)mapA.lumps[LUMP_MODELS])->nVisLeafs + 1; // include solid leaf

	g_progress.update("Merging leaves", thisLeafCount + otherLeafCount);

	vector<BSPLEAF> mergedLeaves;
	mergedLeaves.reserve(thisWorldLeafCount + otherLeafCount);
	modelLeafRemap.reserve(thisWorldLeafCount + otherLeafCount);

	for (int i = 0; i < thisWorldLeafCount; i++) {
		modelLeafRemap.push_back(i);
		mergedLeaves.push_back(mapA.leaves[i]);
		g_progress.tick();
	}

	for (int i = 0; i < otherLeafCount; i++) {
		BSPLEAF& leaf = mapB.leaves[i];
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
		g_progress.tick();
	}

	// append A's submodel leaves after B's world leaves
	// Order will be: A's world leaves -> B's world leaves -> B's submodel leaves -> A's submodel leaves
	for (int i = thisWorldLeafCount; i < thisLeafCount; i++) {
		modelLeafRemap.push_back(mergedLeaves.size());
		mergedLeaves.push_back(mapA.leaves[i]);
	}

	otherLeafCount -= 1; // solid leaf removed

	int newLen = mergedLeaves.size() * sizeof(BSPLEAF);

	byte* newLeavesData = new byte[newLen];
	memcpy(newLeavesData, &mergedLeaves[0], newLen);

	mapA.replace_lump(LUMP_LEAVES, newLeavesData, newLen);
}

void BspMerger::merge_marksurfs(Bsp& mapA, Bsp& mapB) {
	thisMarkSurfCount = mapA.marksurfCount;
	int totalSurfCount = thisMarkSurfCount + mapB.marksurfCount;

	g_progress.update("Merging marksurfaces", totalSurfCount + 1);
	g_progress.tick();

	BSPMARKSURF* newSurfs = new BSPMARKSURF[totalSurfCount];
	memcpy(newSurfs, mapA.marksurfs, thisMarkSurfCount * sizeof(BSPMARKSURF));
	memcpy(newSurfs + thisMarkSurfCount, mapB.marksurfs, mapB.marksurfCount * sizeof(BSPMARKSURF));

	for (int i = 0; i < thisMarkSurfCount; i++) {
		BSPMARKSURF& mark = newSurfs[i];
		if (mark >= thisWorldFaceCount) {
			mark = mark + otherFaceCount;
		}
		g_progress.tick();
	}

	for (int i = thisMarkSurfCount; i < totalSurfCount; i++) {
		BSPMARKSURF& mark = newSurfs[i];
		mark = mark + thisWorldFaceCount;
		g_progress.tick();
	}

	mapA.replace_lump(LUMP_MARKSURFACES, newSurfs, totalSurfCount * sizeof(BSPMARKSURF));
}

void BspMerger::merge_edges(Bsp& mapA, Bsp& mapB) {
	thisEdgeCount = mapA.header.lump[LUMP_EDGES].nLength / sizeof(BSPEDGE);
	int totalEdgeCount = thisEdgeCount + mapB.edgeCount;

	g_progress.update("Merging edges", mapB.edgeCount + 1);
	g_progress.tick();

	BSPEDGE* newEdges = new BSPEDGE[totalEdgeCount];
	memcpy(newEdges, mapA.edges, thisEdgeCount * sizeof(BSPEDGE));
	memcpy(newEdges + thisEdgeCount, mapB.edges, mapB.edgeCount * sizeof(BSPEDGE));

	for (int i = thisEdgeCount; i < totalEdgeCount; i++) {
		BSPEDGE& edge = newEdges[i];
		edge.iVertex[0] = edge.iVertex[0] + thisVertCount;
		edge.iVertex[1] = edge.iVertex[1] + thisVertCount;
		g_progress.tick();
	}

	mapA.replace_lump(LUMP_EDGES, newEdges, totalEdgeCount * sizeof(BSPEDGE));
}

void BspMerger::merge_surfedges(Bsp& mapA, Bsp& mapB) {
	thisSurfEdgeCount = mapA.surfedgeCount;
	int totalSurfCount = thisSurfEdgeCount + mapB.surfedgeCount;

	g_progress.update("Merging surfedges", mapB.edgeCount + 1);
	g_progress.tick();

	int32_t* newSurfs = new int32_t[totalSurfCount];
	memcpy(newSurfs, mapA.surfedges, thisSurfEdgeCount * sizeof(int32_t));
	memcpy(newSurfs + thisSurfEdgeCount, mapB.surfedges, mapB.surfedgeCount * sizeof(int32_t));

	for (int i = thisSurfEdgeCount; i < totalSurfCount; i++) {
		int32_t& surfEdge = newSurfs[i];
		surfEdge = surfEdge < 0 ? surfEdge - thisEdgeCount : surfEdge + thisEdgeCount;
		g_progress.tick();
	}

	mapA.replace_lump(LUMP_SURFEDGES, newSurfs, totalSurfCount * sizeof(int32_t));
}

void BspMerger::merge_nodes(Bsp& mapA, Bsp& mapB) {
	thisNodeCount = mapA.nodeCount;

	g_progress.update("Merging nodes", thisNodeCount + mapB.nodeCount);

	vector<BSPNODE> mergedNodes;
	mergedNodes.reserve(thisNodeCount + mapB.nodeCount);

	for (int i = 0; i < thisNodeCount; i++) {
		BSPNODE node = mapA.nodes[i];

		if (i > 0) { // new headnode should already be correct
			for (int k = 0; k < 2; k++) {
				if (node.iChildren[k] >= 0) {
					node.iChildren[k] += 1; // shifted from new head node
				}
				else {
					node.iChildren[k] = ~((int32_t)modelLeafRemap[~node.iChildren[k]]);
				}
			}
		}
		if (node.nFaces && node.firstFace >= thisWorldFaceCount) {
			node.firstFace += otherFaceCount;
		}

		mergedNodes.push_back(node);
		g_progress.tick();
	}

	for (int i = 0; i < mapB.nodeCount; i++) {
		BSPNODE node = mapB.nodes[i];

		for (int k = 0; k < 2; k++) {
			if (node.iChildren[k] >= 0) {
				node.iChildren[k] += thisNodeCount;
			}
			else {
				node.iChildren[k] = ~((int32_t)leavesRemap[~node.iChildren[k]]);
			}
		}
		node.iPlane = planeRemap[node.iPlane];
		if (node.nFaces) {
			node.firstFace += thisWorldFaceCount;
		}

		mergedNodes.push_back(node);
		g_progress.tick();
	}

	int newLen = mergedNodes.size() * sizeof(BSPNODE);

	byte* newNodeData = new byte[newLen];
	memcpy(newNodeData, &mergedNodes[0], newLen);

	mapA.replace_lump(LUMP_NODES, newNodeData, newLen);
}

void BspMerger::merge_clipnodes(Bsp& mapA, Bsp& mapB) {
	thisClipnodeCount = mapA.clipnodeCount;

	g_progress.update("Merging clipnodes", thisClipnodeCount + mapB.clipnodeCount);

	vector<BSPCLIPNODE> mergedNodes;
	mergedNodes.reserve(thisClipnodeCount + mapB.clipnodeCount);

	for (int i = 0; i < thisClipnodeCount; i++) {
		BSPCLIPNODE node = mapA.clipnodes[i];
		if (i > 2) { // new headnodes should already be correct
			for (int k = 0; k < 2; k++) {
				if (node.iChildren[k] >= 0) {
					node.iChildren[k] += MAX_MAP_HULLS - 1; // offset from new headnodes being added
				}
			}
		}
		mergedNodes.push_back(node);
		g_progress.tick();
	}

	for (int i = 0; i < mapB.clipnodeCount; i++) {
		BSPCLIPNODE node = mapB.clipnodes[i];
		node.iPlane = planeRemap[node.iPlane];

		for (int k = 0; k < 2; k++) {
			if (node.iChildren[k] >= 0) {
				node.iChildren[k] += thisClipnodeCount;
			}
		}
		mergedNodes.push_back(node);
		g_progress.tick();
	}

	int newLen = mergedNodes.size() * sizeof(BSPCLIPNODE);

	byte* newClipnodeData = new byte[newLen];
	memcpy(newClipnodeData, &mergedNodes[0], newLen);

	mapA.replace_lump(LUMP_CLIPNODES, newClipnodeData, newLen);
}

void BspMerger::merge_models(Bsp& mapA, Bsp& mapB) {
	g_progress.update("Merging models", mapA.modelCount + mapB.modelCount);

	vector<BSPMODEL> mergedModels;
	mergedModels.reserve(mapA.modelCount + mapB.modelCount);

	// merged world model
	mergedModels.push_back(mapA.models[0]);

	// other map's submodels
	for (int i = 1; i < mapB.modelCount; i++) {
		BSPMODEL model = mapB.models[i];
		if (model.iHeadnodes[0] >= 0)
			model.iHeadnodes[0] += thisNodeCount; // already includes new head nodes (merge_nodes comes after create_merge_headnodes)
		for (int k = 1; k < MAX_MAP_HULLS; k++) {
			if (model.iHeadnodes[k] >= 0)
				model.iHeadnodes[k] += thisClipnodeCount;
		}
		model.iFirstFace = model.iFirstFace + thisWorldFaceCount;
		mergedModels.push_back(model);
		g_progress.tick();
	}

	// this map's submodels
	for (int i = 1; i < mapA.modelCount; i++) {
		BSPMODEL model = mapA.models[i];
		if (model.iHeadnodes[0] >= 0)
			model.iHeadnodes[0] += 1; // adjust for new head node
		for (int k = 1; k < MAX_MAP_HULLS; k++) {
			if (model.iHeadnodes[k] >= 0)
				model.iHeadnodes[k] += (MAX_MAP_HULLS - 1); // adjust for new head nodes
		}
		if (model.iFirstFace >= thisWorldFaceCount) {
			model.iFirstFace += otherFaceCount;
		}
		mergedModels.push_back(model);
		g_progress.tick();
	}

	// update world head nodes
	mergedModels[0].iHeadnodes[0] = 0;
	mergedModels[0].iHeadnodes[1] = 0;
	mergedModels[0].iHeadnodes[2] = 1;
	mergedModels[0].iHeadnodes[3] = 2;
	mergedModels[0].nVisLeafs = mapA.models[0].nVisLeafs + mapB.models[0].nVisLeafs;
	mergedModels[0].nFaces = mapA.models[0].nFaces + mapB.models[0].nFaces;

	vec3 amin = mapA.models[0].nMins;
	vec3 bmin = mapB.models[0].nMins;
	vec3 amax = mapA.models[0].nMaxs;
	vec3 bmax = mapB.models[0].nMaxs;
	mergedModels[0].nMins = { min(amin.x, bmin.x), min(amin.y, bmin.y), min(amin.z, bmin.z) };
	mergedModels[0].nMaxs = { max(amax.x, bmax.x), max(amax.y, bmax.y), max(amax.z, bmax.z) };

	int newLen = mergedModels.size() * sizeof(BSPMODEL);

	byte* newModelData = new byte[newLen];
	memcpy(newModelData, &mergedModels[0], newLen);

	mapA.replace_lump(LUMP_MODELS, newModelData, newLen);
}

void BspMerger::merge_vis(Bsp& mapA, Bsp& mapB) {
	BSPLEAF* allLeaves = mapA.leaves; // combined with mapB's leaves earlier in merge_leaves

	int thisVisLeaves = thisLeafCount - 1; // VIS ignores the shared solid leaf 0
	int otherVisLeaves = otherLeafCount; // already does not include the solid leaf (see merge_leaves)
	int totalVisLeaves = thisVisLeaves + otherVisLeaves;

	int mergedWorldLeafCount = thisWorldLeafCount + otherWorldLeafCount;

	uint newVisRowSize = ((totalVisLeaves + 63) & ~63) >> 3;
	int decompressedVisSize = totalVisLeaves * newVisRowSize;

	g_progress.update("Merging visibility", thisWorldLeafCount + otherWorldLeafCount * 2 + mergedWorldLeafCount);
	g_progress.tick();

	byte* decompressedVis = new byte[decompressedVisSize];
	memset(decompressedVis, 0, decompressedVisSize);

	// decompress this map's world leaves
	// model leaves don't need to be decompressed because the game ignores VIS for them.
	decompress_vis_lump(allLeaves, mapA.visdata, mapA.visDataLength, decompressedVis,
		thisWorldLeafCount, thisVisLeaves, totalVisLeaves);

	// decompress other map's world-leaf vis data (skip empty first leaf, which now only the first map should have)
	byte* decompressedOtherVis = decompressedVis + thisWorldLeafCount * newVisRowSize;
	decompress_vis_lump(allLeaves + thisWorldLeafCount, mapB.visdata, mapB.visDataLength, decompressedOtherVis,
		otherWorldLeafCount, otherLeafCount, totalVisLeaves);

	// shift mapB's world leaves after mapA's world leaves
	for (int i = 0; i < otherWorldLeafCount; i++) {
		shiftVis(decompressedOtherVis + i * newVisRowSize, newVisRowSize, 0, thisWorldLeafCount);
		g_progress.tick();
	}

	// recompress the combined vis data
	byte* compressedVis = new byte[decompressedVisSize];
	memset(compressedVis, 0, decompressedVisSize);
	int newVisLen = CompressAll(allLeaves, decompressedVis, compressedVis, totalVisLeaves, decompressedVisSize);
	int oldLen = mapA.header.lump[LUMP_VISIBILITY].nLength;

	byte* compressedVisResize = new byte[newVisLen];
	memcpy(compressedVisResize, compressedVis, newVisLen);

	mapA.replace_lump(LUMP_VISIBILITY, compressedVisResize, newVisLen);

	delete[] decompressedVis;
	delete[] compressedVis;
}

void BspMerger::merge_lighting(Bsp& mapA, Bsp& mapB) {
	COLOR3* thisRad = (COLOR3*)mapA.lightdata;
	COLOR3* otherRad = (COLOR3*)mapB.lightdata;
	bool freemem = false;

	int thisColorCount = mapA.header.lump[LUMP_LIGHTING].nLength / sizeof(COLOR3);
	int otherColorCount = mapB.header.lump[LUMP_LIGHTING].nLength / sizeof(COLOR3);
	int totalColorCount = thisColorCount + otherColorCount;
	int totalFaceCount = mapA.header.lump[LUMP_FACES].nLength / sizeof(BSPFACE);

	g_progress.update("Merging lightmaps", 4 + totalFaceCount);

	// create a single full-bright lightmap to use for all faces, if one map has lighting but the other doesn't
	if (thisColorCount == 0 && otherColorCount != 0) {
		thisColorCount = g_limits.max_surface_extents * g_limits.max_surface_extents;
		totalColorCount += thisColorCount;
		int sz = thisColorCount * sizeof(COLOR3);
		mapA.lumps[LUMP_LIGHTING] = new byte[sz];
		mapA.header.lump[LUMP_LIGHTING].nLength = sz;
		thisRad = (COLOR3*)mapA.lumps[LUMP_LIGHTING];

		memset(thisRad, 255, sz);

		for (int i = 0; i < thisWorldFaceCount; i++) {
			mapA.faces[i].nLightmapOffset = 0;
		}
		for (int i = thisWorldFaceCount + otherFaceCount; i < totalFaceCount; i++) {
			mapA.faces[i].nLightmapOffset = 0;
		}
	}
	else if (thisColorCount != 0 && otherColorCount == 0) {
		otherColorCount = g_limits.max_surface_extents * g_limits.max_surface_extents;
		totalColorCount += otherColorCount;
		otherRad = new COLOR3[otherColorCount];
		freemem = true;
		memset(otherRad, 255, otherColorCount * sizeof(COLOR3));

		for (int i = thisWorldFaceCount; i < thisWorldFaceCount + otherFaceCount; i++) {
			mapA.faces[i].nLightmapOffset = 0;
		}
	}

	g_progress.tick();
	COLOR3* newRad = new COLOR3[totalColorCount];

	g_progress.tick();
	memcpy(newRad, thisRad, thisColorCount * sizeof(COLOR3));

	g_progress.tick();
	memcpy((byte*)newRad + thisColorCount * sizeof(COLOR3), otherRad, otherColorCount * sizeof(COLOR3));
	if (freemem)
	{
		delete[] otherRad;
	}
	g_progress.tick();
	mapA.replace_lump(LUMP_LIGHTING, newRad, totalColorCount * sizeof(COLOR3));

	for (int i = thisWorldFaceCount; i < thisWorldFaceCount + otherFaceCount; i++) {
		mapA.faces[i].nLightmapOffset += thisColorCount * sizeof(COLOR3);
		g_progress.tick();
	}
}