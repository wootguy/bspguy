#include "Bsp.h"
#include "util.h"
#include <algorithm>
#include <sstream>
#include "lodepng.h"
#include "rad.h"
#include "vis.h"
#include "remap.h"
#include <set>

typedef map< string, vec3 > mapStringToVector;

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
		header.lump[i].nLength = 0;
		header.lump[i].nOffset = 0;
		lumps[i] = NULL;
	}

	update_lump_pointers();
	name = "merged";
	valid = true;
}

Bsp::Bsp(std::string fpath)
{
	if (fpath.size() < 4 || fpath.rfind(".bsp") != fpath.size() - 4) {
		fpath = fpath + ".bsp";
	}
	this->path = fpath;
	this->name = stripExt(basename(fpath));
	valid = false;

	bool exists = true;
	if (!fileExists(fpath)) {
		logf("ERROR: %s not found\n", fpath.c_str());
		return;
	}

	if (!load_lumps(fpath)) {
		logf("%s is not a valid BSP file\n", fpath.c_str());
		return;
	}

	load_ents();
	update_lump_pointers();

	valid = true;
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

void Bsp::get_bounding_box(vec3& mins, vec3& maxs) {
	BSPMODEL& thisWorld = models[0];

	// the model bounds are little bigger than the actual vertices bounds in the map,
	// but if you go by the vertices then there will be collision problems.

	mins = thisWorld.nMins;
	maxs = thisWorld.nMaxs;
}

void Bsp::get_model_vertex_bounds(int modelIdx, vec3& mins, vec3& maxs) {
	mins = vec3(9e99, 9e99, 9e99);
	maxs = vec3(-9e99, -9e99, -9e99);

	BSPMODEL& model = models[modelIdx];

	for (int i = 0; i < model.nFaces; i++) {
		BSPFACE& face = faces[model.iFirstFace + i];

		for (int e = 0; e < face.nEdges; e++) {
			int32_t edgeIdx = surfedges[face.iFirstEdge + e];
			BSPEDGE& edge = edges[abs(edgeIdx)];
			int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];
			vec3 vert = verts[vertIdx];

			if (vert.x > maxs.x) maxs.x = vert.x;
			if (vert.y > maxs.y) maxs.y = vert.y;
			if (vert.z > maxs.z) maxs.z = vert.z;

			if (vert.x < mins.x) mins.x = vert.x;
			if (vert.y < mins.y) mins.y = vert.y;
			if (vert.z < mins.z) mins.z = vert.z;
		}
	}
}

vector<TransformVert> Bsp::getModelVerts(int modelIdx) {
	vector<TransformVert> allVerts;
	set<int> visited;

	BSPMODEL& model = models[modelIdx];

	for (int i = 0; i < model.nFaces; i++) {
		BSPFACE& face = faces[model.iFirstFace + i];

		for (int e = 0; e < face.nEdges; e++) {
			int32_t edgeIdx = surfedges[face.iFirstEdge + e];
			BSPEDGE& edge = edges[abs(edgeIdx)];
			int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];

			if (visited.find(vertIdx) == visited.end()) {
				TransformVert vert;
				memset(&vert, 0, sizeof(TransformVert));
				vert.startPos = vert.undoPos = vert.pos = verts[vertIdx];
				vert.ptr = &verts[vertIdx];

				allVerts.push_back(vert);
				visited.insert(vertIdx);
			}
		}
	}

	return allVerts;
}

bool Bsp::getModelPlaneIntersectVerts(int modelIdx, vector<TransformVert>& outVerts) {
	vector<int> nodePlaneIndexes;
	BSPMODEL& model = models[modelIdx];
	getNodePlanes(model.iHeadnodes[0], nodePlaneIndexes);

	return getModelPlaneIntersectVerts(modelIdx, nodePlaneIndexes, outVerts);
}

bool Bsp::getModelPlaneIntersectVerts(int modelIdx, const vector<int>& nodePlaneIndexes, vector<TransformVert>& outVerts) {
	// TODO: this only works for convex objects. A concave solid will need
	// to get verts by creating convex hulls from each solid node in the tree.
	// That can be done by recursively cutting a huge cube but there's probably
	// a better way.
	vector<BSPPLANE> nodePlanes;

	BSPMODEL& model = models[modelIdx];

	// TODO: model center doesn't have to be inside all planes, even for convex objects(?)
	vec3 modelCenter = model.nMins + (model.nMaxs - model.nMins) * 0.5f;
	for (int i = 0; i < nodePlaneIndexes.size(); i++) {
		nodePlanes.push_back(planes[nodePlaneIndexes[i]]);
		BSPPLANE& plane = nodePlanes[i];
		vec3 planePoint = plane.vNormal * plane.fDist;
		vec3 planeDir = (planePoint - modelCenter).normalize(1.0f);
		if (dotProduct(planeDir, plane.vNormal) > 0) {
			plane.vNormal *= -1;
			plane.fDist *= -1;
		}
	}

	vector<vec3> nodeVerts = getPlaneIntersectVerts(nodePlanes);

	if (nodeVerts.size() < 4) {
		return false; // solid is either 2D or there were no intersections (not convex)
	}

	// coplanar test
	for (int i = 0; i < nodePlanes.size(); i++) {
		for (int k = 0; k < nodePlanes.size(); k++) {
			if (i == k)
				continue;

			if (nodePlanes[i].vNormal == nodePlanes[k].vNormal && nodePlanes[i].fDist - nodePlanes[k].fDist < EPSILON) {
				return false;
			}
		}
	}

	// convex test
	for (int k = 0; k < nodePlanes.size(); k++) {
		if (!vertsAllOnOneSide(nodeVerts, nodePlanes[k])) {
			return false;
		}
	}

	outVerts.clear();
	for (int k = 0; k < nodeVerts.size(); k++) {
		vec3 v = nodeVerts[k];

		TransformVert hullVert;
		hullVert.pos = hullVert.undoPos = hullVert.startPos = v;
		hullVert.ptr = NULL;
		hullVert.selected = false;

		for (int i = 0; i < nodePlanes.size(); i++) {
			BSPPLANE& p = nodePlanes[i];
			if (fabs(dotProduct(v, p.vNormal) - p.fDist) < EPSILON) {
				hullVert.iPlanes.push_back(nodePlaneIndexes[i]);
			}
		}

		for (int i = 0; i < model.nFaces && !hullVert.ptr; i++) {
			BSPFACE& face = faces[model.iFirstFace + i];

			for (int e = 0; e < face.nEdges && !hullVert.ptr; e++) {
				int32_t edgeIdx = surfedges[face.iFirstEdge + e];
				BSPEDGE& edge = edges[abs(edgeIdx)];
				int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];

				if (verts[vertIdx] != v) {
					continue;
				}

				hullVert.ptr = &verts[vertIdx];
			}
		}

		outVerts.push_back(hullVert);
	}

	return true;
}

void Bsp::getNodePlanes(int iNode, vector<int>& nodePlanes) {
	BSPNODE& node = nodes[iNode];
	nodePlanes.push_back(node.iPlane);

	for (int i = 0; i < 2; i++) {
		if (node.iChildren[i] >= 0) {
			getNodePlanes(node.iChildren[i], nodePlanes);
		}
	}
}

bool Bsp::is_convex(int modelIdx) {
	return is_node_hull_convex(models[modelIdx].iHeadnodes[0]);
}

bool Bsp::is_node_hull_convex(int iNode) {
	BSPNODE& node = nodes[iNode];

	// convex models always have one node pointing to empty space
	if (node.iChildren[0] >= 0 && node.iChildren[1] >= 0) {
		return false;
	}

	for (int i = 0; i < 2; i++) {
		if (node.iChildren[i] >= 0) {
			if (!is_node_hull_convex(node.iChildren[i])) {
				return false;
			}
		}
	}

	return true;
}

int Bsp::addTextureInfo(BSPTEXTUREINFO& copy) {
	BSPTEXTUREINFO* newInfos = new BSPTEXTUREINFO[texinfoCount + 1];
	memcpy(newInfos, texinfos, texinfoCount * sizeof(BSPTEXTUREINFO));

	int newIdx = texinfoCount;
	newInfos[newIdx] = copy;

	replace_lump(LUMP_TEXINFO, newInfos, (texinfoCount + 1) * sizeof(BSPTEXTUREINFO));

	return newIdx;
}

vector<ScalableTexinfo> Bsp::getScalableTexinfos(int modelIdx) {
	BSPMODEL& model = models[modelIdx];
	vector<ScalableTexinfo> scalable;
	set<int> visitedTexinfos;

	for (int k = 0; k < model.nFaces; k++) {
		BSPFACE& face = faces[model.iFirstFace + k];
		int texinfoIdx = face.iTextureInfo;

		if (visitedTexinfos.find(texinfoIdx) != visitedTexinfos.end()) {
			continue;
			//texinfoIdx = face.iTextureInfo = addTextureInfo(texinfos[texinfoIdx]);
		}
		visitedTexinfos.insert(texinfoIdx);

		ScalableTexinfo st;
		st.oldS = texinfos[texinfoIdx].vS;
		st.oldT = texinfos[texinfoIdx].vT;
		st.oldShiftS = texinfos[texinfoIdx].shiftS;
		st.oldShiftT = texinfos[texinfoIdx].shiftT;
		st.texinfoIdx = texinfoIdx;
		st.planeIdx = face.iPlane;
		st.faceIdx = model.iFirstFace + k;
		scalable.push_back(st);
	}

	return scalable;
}

bool Bsp::vertex_manipulation_sync(int modelIdx, vector<TransformVert>& hullVerts, bool convexCheckOnly) {
	set<int> affectedPlanes;

	map<int, vector<vec3>> planeVerts;
	vector<vec3> allVertPos;
	
	for (int i = 0; i < hullVerts.size(); i++) {
		for (int k = 0; k < hullVerts[i].iPlanes.size(); k++) {
			int iPlane = hullVerts[i].iPlanes[k];
			affectedPlanes.insert(hullVerts[i].iPlanes[k]);
			planeVerts[iPlane].push_back(hullVerts[i].pos);
		}
		allVertPos.push_back(hullVerts[i].pos);
	}

	int planeUpdates = 0;
	map<int, BSPPLANE> newPlanes;
	map<int, bool> shouldFlipChildren;
	for (auto it = planeVerts.begin(); it != planeVerts.end(); ++it) {
		int iPlane = it->first;
		vector<vec3>& verts = it->second;

		if (verts.size() < 3) {
			logf("Face has less than 3 verts\n");
			return false; // invalid solid
		}

		BSPPLANE newPlane;
		if (!getPlaneFromVerts(verts, newPlane.vNormal, newPlane.fDist)) {
			logf("Verts not planar\n");
			return false; // verts not planar
		}

		vec3 oldNormal = planes[iPlane].vNormal;
		if (dotProduct(oldNormal, newPlane.vNormal) < 0) {
			newPlane.vNormal = newPlane.vNormal.invert(); // TODO: won't work for big changes
			newPlane.fDist = -newPlane.fDist;
		}

		BSPPLANE testPlane;
		bool expectedFlip = testPlane.update(planes[iPlane].vNormal, planes[iPlane].fDist);
		bool flipped = newPlane.update(newPlane.vNormal, newPlane.fDist);
		int frontChild = flipped ? 0 : 1;

		testPlane = newPlane;

		// check that all verts are on one side of the plane.
		// plane inversions are ok according to hammer
		if (!vertsAllOnOneSide(allVertPos, testPlane)) {
			return false;
		}
		
		newPlanes[iPlane] = newPlane;
		shouldFlipChildren[iPlane] = flipped != expectedFlip;
	}

	if (convexCheckOnly)
		return true;

	for (auto it = newPlanes.begin(); it != newPlanes.end(); ++it) {
		int iPlane = it->first;
		BSPPLANE& newPlane = it->second;

		planes[iPlane] = newPlane;
		planeUpdates++;

		if (shouldFlipChildren[iPlane]) {
			for (int i = 0; i < faceCount; i++) {
				BSPFACE& face = faces[i];
				if (face.iPlane == iPlane) {
					face.nPlaneSide = !face.nPlaneSide;
				}
			}
			for (int i = 0; i < nodeCount; i++) {
				BSPNODE& node = nodes[i];
				if (node.iPlane == iPlane) {
					int16 temp = node.iChildren[0];
					node.iChildren[0] = node.iChildren[1];
					node.iChildren[1] = node.iChildren[0];
				}
			}
		}
	}

	//logf("UPDATED %d planes\n", planeUpdates);

	BSPMODEL& model = models[modelIdx];
	getBoundingBox(allVertPos, model.nMins, model.nMaxs);
	regenerate_clipnodes(modelIdx);

	return true;
}

bool Bsp::move(vec3 offset) {
	split_shared_model_structures();

	bool hasLighting = lightDataLength > 0;

	LIGHTMAP* oldLightmaps = NULL;
	LIGHTMAP* newLightmaps = NULL;

	if (hasLighting) {
		g_progress.update("Calculate lightmaps", faceCount);

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

			g_progress.tick();
		}
	}
	
	g_progress.update("Moving structures", ents.size() + modelCount);

	bool* modelHasOrigin = new bool[modelCount];
	memset(modelHasOrigin, 0, modelCount * sizeof(bool));

	for (int i = 0; i < ents.size(); i++) {
		if (!ents[i]->hasKey("origin")) {
			continue;
		}
		if (ents[i]->isBspModel()) {
			modelHasOrigin[ents[i]->getBspModelIdx()] = true;
		}

		Keyvalue keyvalue("origin", ents[i]->keyvalues["origin"]);
		vec3 ori = keyvalue.getVector() + offset;

		ents[i]->keyvalues["origin"] = ori.toKeyvalueString();

		g_progress.tick();
	}

	update_ent_lump();
	
	for (int i = 0; i < modelCount; i++) {
		BSPMODEL& model = models[i];

		if (!modelHasOrigin[i]) {
			model.nMins += offset;
			model.nMaxs += offset;

			if (fabs(model.nMins.x) > MAX_MAP_COORD ||
				fabs(model.nMins.y) > MAX_MAP_COORD ||
				fabs(model.nMins.z) > MAX_MAP_COORD ||
				fabs(model.nMaxs.z) > MAX_MAP_COORD ||
				fabs(model.nMaxs.z) > MAX_MAP_COORD ||
				fabs(model.nMaxs.z) > MAX_MAP_COORD) {
				logf("\nWARNING: Model moved past safe world boundary!\n");
			}
		}
	}

	STRUCTUSAGE shouldBeMoved(this);
	for (int i = 0; i < modelCount; i++) {
		if (!modelHasOrigin[i])
			mark_model_structures(i, &shouldBeMoved);
		g_progress.tick();
	}

	for (int i = 0; i < nodeCount; i++) {
		if (!shouldBeMoved.nodes[i]) {
			continue; // don't move submodels with origins
		}

		BSPNODE& node = nodes[i];

		if (fabs((float)node.nMins[0] + offset.x) > MAX_MAP_COORD ||
			fabs((float)node.nMaxs[0] + offset.x) > MAX_MAP_COORD ||
			fabs((float)node.nMins[1] + offset.y) > MAX_MAP_COORD ||
			fabs((float)node.nMaxs[1] + offset.y) > MAX_MAP_COORD ||
			fabs((float)node.nMins[2] + offset.z) > MAX_MAP_COORD ||
			fabs((float)node.nMaxs[2] + offset.z) > MAX_MAP_COORD) {
			logf("\nWARNING: Bounding box for node moved past safe world boundary!\n");
		}
		node.nMins[0] += offset.x;
		node.nMaxs[0] += offset.x;
		node.nMins[1] += offset.y;
		node.nMaxs[1] += offset.y;
		node.nMins[2] += offset.z;
		node.nMaxs[2] += offset.z;
	}

	for (int i = 1; i < leafCount; i++) { // don't move the solid leaf (always has 0 size)
		if (!shouldBeMoved.leaves[i]) {
			continue; // don't move submodels with origins
		}

		BSPLEAF& leaf = leaves[i];

		if (fabs((float)leaf.nMins[0] + offset.x) > MAX_MAP_COORD ||
			fabs((float)leaf.nMaxs[0] + offset.x) > MAX_MAP_COORD ||
			fabs((float)leaf.nMins[1] + offset.y) > MAX_MAP_COORD ||
			fabs((float)leaf.nMaxs[1] + offset.y) > MAX_MAP_COORD ||
			fabs((float)leaf.nMins[2] + offset.z) > MAX_MAP_COORD ||
			fabs((float)leaf.nMaxs[2] + offset.z) > MAX_MAP_COORD) {
			logf("\nWARNING: Bounding box for leaf moved past safe world boundary!\n");
		}
		leaf.nMins[0] += offset.x;
		leaf.nMaxs[0] += offset.x;
		leaf.nMins[1] += offset.y;
		leaf.nMaxs[1] += offset.y;
		leaf.nMins[2] += offset.z;
		leaf.nMaxs[2] += offset.z;
	}

	for (int i = 0; i < vertCount; i++) {
		if (!shouldBeMoved.verts[i]) {
			continue; // don't move submodels with origins
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

	uint32_t texCount = (uint32_t)(lumps[LUMP_TEXTURES])[0];
	byte* textures = lumps[LUMP_TEXTURES];

	for (int i = 0; i < texinfoCount; i++) {
		if (!shouldBeMoved.texInfo[i]) {
			continue; // don't move submodels with origins
		}

		move_texinfo(i, offset);
	}

	if (hasLighting) {
		resize_lightmaps(oldLightmaps, newLightmaps);
	}

	delete[] oldLightmaps;
	delete[] newLightmaps;

	g_progress.clear();

	return true;
}

void Bsp::move_texinfo(int idx, vec3 offset) {
	BSPTEXTUREINFO& info = texinfos[idx];

	int32_t texOffset = ((int32_t*)textures)[info.iMiptex + 1];
	BSPMIPTEX& tex = *((BSPMIPTEX*)(textures + texOffset));

	vec3 offsetDir = offset.normalize();
	float offsetLen = offset.length();

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

void Bsp::resize_lightmaps(LIGHTMAP* oldLightmaps, LIGHTMAP* newLightmaps) {
	g_progress.update("Recalculate lightmaps", faceCount);

	// calculate new lightmap sizes
	qrad_init_globals(this);
	int newLightDataSz = 0;
	int totalLightmaps = 0;
	int lightmapsResizeCount = 0;
	for (int i = 0; i < faceCount; i++) {
		BSPFACE& face = faces[i];

		g_progress.tick();

		if (face.nStyles[0] == 255 || texinfos[face.iTextureInfo].nFlags & TEX_SPECIAL)
			continue;

		BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];
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
		//logf(" %d lightmap(s) to resize", lightmapsResizeCount, totalLightmaps);

		g_progress.update("Resize lightmaps", faceCount);

		int newColorCount = newLightDataSz / sizeof(COLOR3);
		COLOR3* newLightData = new COLOR3[newColorCount];
		memset(newLightData, 255, newColorCount * sizeof(COLOR3));
		int lightmapOffset = 0;


		for (int i = 0; i < faceCount; i++) {
			BSPFACE& face = faces[i];

			g_progress.tick();

			if (face.nStyles[0] == 255 || texinfos[face.iTextureInfo].nFlags & TEX_SPECIAL) // no lighting
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

				int maxWidth = min(newLight.width, oldLight.width);
				int maxHeight = min(newLight.height, oldLight.height);

				int srcOffsetX, srcOffsetY;
				get_lightmap_shift(oldLight, newLight, srcOffsetX, srcOffsetY);

				for (int layer = 0; layer < newLight.layers; layer++) {
					int srcOffset = (face.nLightmapOffset + oldLayerSz * layer) / sizeof(COLOR3);
					int dstOffset = (lightmapOffset + newLayerSz * layer) / sizeof(COLOR3);

					int startX = newLight.width > oldLight.width ? -1 : 0;
					int startY = newLight.height > oldLight.height ? -1 : 0;

					for (int y = startY; y < newLight.height; y++) {
						for (int x = startX; x < newLight.width; x++) {
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

							COLOR3& src = ((COLOR3*)lightdata)[srcOffset + srcY * oldLight.width + srcX];
							COLOR3& dst = newLightData[dstOffset + dstY * newLight.width + dstX];

							dst = src;
						}
					}
				}
			}

			face.nLightmapOffset = lightmapOffset;
			lightmapOffset += newSz;
		}

		replace_lump(LUMP_LIGHTING, newLightData, lightmapOffset);
	}
}

void Bsp::split_shared_model_structures() {
	bool* modelHasOrigin = new bool[modelCount];
	memset(modelHasOrigin, 0, modelCount * sizeof(bool));

	for (int i = 0; i < ents.size(); i++) {
		if (ents[i]->hasKey("origin") && ents[i]->isBspModel()) {
			modelHasOrigin[ents[i]->getBspModelIdx()] = true;
		}
	}

	// marks which structures should not be moved
	STRUCTUSAGE shouldMove(this);
	STRUCTUSAGE shouldNotMove(this);

	g_progress.update("Split model structures", modelCount * 2);

	for (int i = 0; i < modelCount; i++) {
		if (modelHasOrigin[i])
			mark_model_structures(i, &shouldNotMove);
		else
			mark_model_structures(i, &shouldMove);

		g_progress.tick();
	}

	STRUCTREMAP remappedStuff(this);

	// TODO: handle all of these, assuming it's possible these are ever shared
	for (int i = 1; i < shouldNotMove.count.leaves; i++) { // skip solid leaf - it doesn't matter
		if (shouldMove.leaves[i] && shouldNotMove.leaves[i]) {
			logf("\nError: leaf shared with models of different origin types. Something will break.\n");
			break;
		}
	}
	for (int i = 0; i < shouldNotMove.count.nodes; i++) {
		if (shouldMove.nodes[i] && shouldNotMove.nodes[i]) {
			logf("\nError: node shared with models of different origin types. Something will break.\n");
			break;
		}
	}
	for (int i = 0; i < shouldNotMove.count.texInfos; i++) {
		if (shouldMove.texInfo[i] && shouldNotMove.texInfo[i] && !(texinfos[i].nFlags & TEX_SPECIAL)) {
			logf("\nError: texinfo shared with models of different origin types. Something will break.\n");
			break;
		}
	}
	for (int i = 0; i < shouldNotMove.count.verts; i++) {
		if (shouldMove.verts[i] && shouldNotMove.verts[i]) {
			logf("\nError: vertex shared with models of different origin types. Something will break.\n");
			break;
		}
	}

	int duplicatePlanes = 0;
	int duplicateClipnodes = 0;

	for (int i = 0; i < shouldNotMove.count.planes; i++) {
		duplicatePlanes += shouldMove.planes[i] && shouldNotMove.planes[i];
	}
	for (int i = 0; i < shouldNotMove.count.clipnodes; i++) {
		duplicateClipnodes += shouldMove.clipnodes[i] && shouldNotMove.clipnodes[i];
	}

	int newPlaneCount = planeCount + duplicatePlanes;
	int newClipnodeCount = clipnodeCount + duplicateClipnodes;

	BSPPLANE* newPlanes = new BSPPLANE[newPlaneCount];
	memcpy(newPlanes, planes, planeCount * sizeof(BSPPLANE));

	BSPCLIPNODE* newClipnodes = new BSPCLIPNODE[newClipnodeCount];
	memset(newClipnodes, 0, newClipnodeCount * sizeof(BSPCLIPNODE));
	memcpy(newClipnodes, clipnodes, clipnodeCount * sizeof(BSPCLIPNODE));

	int addIdx = planeCount;
	for (int i = 0; i < shouldNotMove.count.planes; i++) {
		if (shouldMove.planes[i] && shouldNotMove.planes[i]) {
			newPlanes[addIdx] = planes[i];
			remappedStuff.planes[i] = addIdx;
			addIdx++;
		}
	}

	addIdx = clipnodeCount;
	for (int i = 0; i < shouldNotMove.count.clipnodes; i++) {
		if (shouldMove.clipnodes[i] && shouldNotMove.clipnodes[i]) {
			newClipnodes[addIdx] = clipnodes[i];
			remappedStuff.clipnodes[i] = addIdx;
			addIdx++;
		}
	}

	replace_lump(LUMP_PLANES, newPlanes, newPlaneCount * sizeof(BSPPLANE));
	replace_lump(LUMP_CLIPNODES, newClipnodes, newClipnodeCount * sizeof(BSPCLIPNODE));

	bool* newVisitedClipnodes = new bool[newClipnodeCount];
	memset(newVisitedClipnodes, 0, newClipnodeCount);
	delete[] remappedStuff.visitedClipnodes;
	remappedStuff.visitedClipnodes = newVisitedClipnodes;

	for (int i = 0; i < modelCount; i++) {
		if (!modelHasOrigin[i]) {
			remap_model_structures(i, &remappedStuff);
		}
		g_progress.tick();
	}

	//if (duplicateCount)
	//	logf("\nDuplicated %d shared model planes to allow independent movement\n", duplicateCount);

	delete[] modelHasOrigin;
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
		case LUMP_MARKSURFACES: structSize = sizeof(uint16_t); break;
		case LUMP_EDGES: structSize = sizeof(BSPEDGE); break;
		case LUMP_SURFEDGES: structSize = sizeof(int32_t); break;
		default:
			logf("\nERROR: Invalid lump %d passed to remove_unused_structs\n", lumpIdx);
			return 0;
	}

	int oldStructCount = header.lump[lumpIdx].nLength / structSize;
	
	int removeCount = 0;
	for (int i = 0; i < oldStructCount; i++) {
		removeCount += !usedStructs[i];
	}

	int newStructCount = oldStructCount - removeCount;

	byte* oldStructs = lumps[lumpIdx];
	byte* newStructs = new byte[newStructCount*structSize];

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

int Bsp::remove_unused_textures(bool* usedTextures, int* remappedIndexes) {
	int oldTexCount = textureCount;

	int removeCount = 0;
	int removeSize = 0;
	for (int i = 0; i < oldTexCount; i++) {
		if (!usedTextures[i]) {
			int32_t offset = ((int32_t*)textures)[i + 1];
			BSPMIPTEX* tex = (BSPMIPTEX*)(textures + offset);

			// don't delete single frames from animated textures or else game crashes
			if (tex->szName[0] == '-' || tex->szName[0] == '+') {
				usedTextures[i] = true;
				// TODO: delete all frames if none are used
				continue;
			}

			removeSize += getBspTextureSize(tex) + sizeof(int32_t);
			removeCount++;
		}
	}

	int newTexCount = oldTexCount - removeCount;
	byte* newTexData = new byte[header.lump[LUMP_TEXTURES].nLength - removeSize];

	uint32_t* texHeader = (uint32_t*)newTexData;
	texHeader[0] = newTexCount;

	int32_t newOffset = (newTexCount + 1) * sizeof(int32_t);
	for (int i = 0, k = 0; i < oldTexCount; i++) {
		if (!usedTextures[i]) {
			continue;
		}
		int32_t oldOffset = ((int32_t*)textures)[i + 1];
		BSPMIPTEX* tex = (BSPMIPTEX*)(textures + oldOffset);
		int sz = getBspTextureSize(tex);

		memcpy(newTexData + newOffset, textures + oldOffset, sz);
		texHeader[k+1] = newOffset;
		remappedIndexes[i] = k;
		newOffset += sz;
		k++;
	}

	replace_lump(LUMP_TEXTURES, newTexData, header.lump[LUMP_TEXTURES].nLength - removeSize);

	return removeCount;
}

int Bsp::remove_unused_lightmaps(bool* usedFaces) {
	int oldLightdataSize = lightDataLength;

	qrad_init_globals(this);

	int* lightmapSizes = new int[faceCount];

	int newLightDataSize = 0;
	for (int i = 0; i < faceCount; i++) {
		if (usedFaces[i]) {
			lightmapSizes[i] = GetFaceLightmapSizeBytes(i);
			newLightDataSize += lightmapSizes[i];
		}
	}

	byte* newColorData = new byte[newLightDataSize];

	int offset = 0;
	for (int i = 0; i < faceCount; i++) {
		BSPFACE& face = faces[i];

		if (usedFaces[i] && ((int64)face.nLightmapOffset + lightmapSizes[i]) < (int64)lightDataLength) {
			memcpy(newColorData + offset, lightdata + face.nLightmapOffset, lightmapSizes[i]);
			face.nLightmapOffset = offset;
			offset += lightmapSizes[i];
		}
	}

	delete[] lightmapSizes;

	replace_lump(LUMP_LIGHTING, newColorData, newLightDataSize);

	return oldLightdataSize - newLightDataSize;
}

int Bsp::remove_unused_visdata(bool* usedLeaves, BSPLEAF* oldLeaves, int oldLeafCount) {
	int oldVisLength = visDataLength;

	// exclude solid leaf
	int oldVisLeafCount = oldLeafCount - 1;
	int newVisLeafCount = (header.lump[LUMP_LEAVES].nLength / sizeof(BSPLEAF)) - 1;

	int oldWorldLeaves = ((BSPMODEL*)lumps[LUMP_MODELS])->nVisLeafs; // TODO: allow deleting world leaves
	int newWorldLeaves = ((BSPMODEL*)lumps[LUMP_MODELS])->nVisLeafs;

	uint oldVisRowSize = ((oldVisLeafCount + 63) & ~63) >> 3;
	uint newVisRowSize = ((newVisLeafCount + 63) & ~63) >> 3;

	int decompressedVisSize = oldLeafCount * oldVisRowSize;
	byte* decompressedVis = new byte[decompressedVisSize];
	memset(decompressedVis, 0, decompressedVisSize);
	decompress_vis_lump(oldLeaves, lumps[LUMP_VISIBILITY], decompressedVis, 
		oldWorldLeaves, oldVisLeafCount, oldVisLeafCount);

	if (oldVisRowSize != newVisRowSize) {
		int newDecompressedVisSize = oldLeafCount * newVisRowSize;
		byte* newDecompressedVis = new byte[decompressedVisSize];
		memset(newDecompressedVis, 0, newDecompressedVisSize);

		int minRowSize = min(oldVisRowSize, newVisRowSize);
		for (int i = 0; i < oldWorldLeaves; i++) {
			memcpy(newDecompressedVis + i * newVisRowSize, decompressedVis + i * oldVisRowSize, minRowSize);
		}

		delete[] decompressedVis;
		decompressedVis = newDecompressedVis;
	}

	byte* compressedVis = new byte[decompressedVisSize];
	memset(compressedVis, 0, decompressedVisSize);
	int newVisLen = CompressAll(leaves, decompressedVis, compressedVis, newVisLeafCount, newWorldLeaves, decompressedVisSize);

	byte* compressedVisResized = new byte[newVisLen];
	memcpy(compressedVisResized, compressedVis, newVisLen);

	replace_lump(LUMP_VISIBILITY, compressedVisResized, newVisLen);

	delete[] decompressedVis;
	delete[] compressedVis;

	return oldVisLength - newVisLen;
}

STRUCTCOUNT Bsp::remove_unused_model_structures() {
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

	// reversed so models can be deleted without shifting the next delete index
 	for (int i = modelCount-1; i >= 0; i--) { 
		if (!usedModels[i]) {
			delete_model(i);
		}
		else {
			mark_model_structures(i, &usedStructures);
		}
	}

	delete[] usedModels;

	STRUCTREMAP remap(this);
	STRUCTCOUNT removeCount;
	memset(&removeCount, 0, sizeof(STRUCTCOUNT));

	usedStructures.edges[0] = true; // first edge is never used but maps break without it?

	byte* oldLeaves = new byte[header.lump[LUMP_LEAVES].nLength];
	memcpy(oldLeaves, lumps[LUMP_LEAVES], header.lump[LUMP_LEAVES].nLength);

	if (lightDataLength)
		removeCount.lightdata = remove_unused_lightmaps(usedStructures.faces);

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

	if (visDataLength)
		removeCount.visdata = remove_unused_visdata(usedStructures.leaves, (BSPLEAF*)oldLeaves, usedStructures.count.leaves);

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
		for (int k = 0; k < 2; k++) {
			if (nodes[i].iChildren[k] >= 0) {
				nodes[i].iChildren[k] = remap.nodes[nodes[i].iChildren[k]];
			}
			else {
				int16_t leafIdx = ~nodes[i].iChildren[k];
				nodes[i].iChildren[k] = ~((int16_t)remap.leaves[leafIdx]);
			}
		}
	}
	for (int i = 1; i < newCounts.leaves; i++) {
		if (leaves[i].nMarkSurfaces > 0)
			leaves[i].iFirstMarkSurface = remap.markSurfs[leaves[i].iFirstMarkSurface];
	}
	for (int i = 0; i < newCounts.faces; i++) {
		faces[i].iPlane = remap.planes[faces[i].iPlane];
		if (faces[i].nEdges > 0)
			faces[i].iFirstEdge = remap.surfEdges[faces[i].iFirstEdge];
		faces[i].iTextureInfo = remap.texInfo[faces[i].iTextureInfo];
	}

	for (int i = 0; i < modelCount; i++) {
		if (models[i].nFaces > 0)
			models[i].iFirstFace = remap.faces[models[i].iFirstFace];
		if (models[i].iHeadnodes[0] >= 0)
			models[i].iHeadnodes[0] = remap.nodes[models[i].iHeadnodes[0]];
		for (int k = 1; k < MAX_MAP_HULLS; k++) {
			if (models[i].iHeadnodes[k] >= 0)
				models[i].iHeadnodes[k] = remap.clipnodes[models[i].iHeadnodes[k]];
		}
	}

	return removeCount;
}

bool Bsp::has_hull2_ents() {
	// monsters that use hull 2 by default
	static set<string> largeMonsters{
		"monster_alien_grunt",
		"monster_alien_tor",
		"monster_alien_voltigore",
		"monster_babygarg",
		"monster_bigmomma",
		"monster_bullchicken",
		"monster_gargantua",
		"monster_ichthyosaur",
		"monster_kingpin",
		"monster_apache",
		"monster_blkop_apache"
		// osprey, nihilanth, and tentacle are huge but are basically nonsolid (no brush collision or triggers)
	};

	for (int i = 0; i < ents.size(); i++) {
		string cname = ents[i]->keyvalues["classname"];
		string tname = ents[i]->keyvalues["targetname"];

		if (cname.find("monster_") == 0) {
			vec3 minhull;
			vec3 maxhull;

			if (!ents[i]->keyvalues["minhullsize"].empty())
				minhull = Keyvalue("", ents[i]->keyvalues["minhullsize"]).getVector();
			if (!ents[i]->keyvalues["maxhullsize"].empty())
				maxhull = Keyvalue("", ents[i]->keyvalues["maxhullsize"]).getVector();

			if (minhull == vec3(0, 0, 0) && maxhull == vec3(0, 0, 0)) {
				// monster is using its default hull size
				if (largeMonsters.find(cname) != largeMonsters.end()) {
					return true;
				}
			}
			else if (abs(minhull.x) > MAX_HULL1_EXTENT_MONSTER || abs(maxhull.x) > MAX_HULL1_EXTENT_MONSTER
				|| abs(minhull.y) > MAX_HULL1_EXTENT_MONSTER || abs(maxhull.y) > MAX_HULL1_EXTENT_MONSTER) {
				return true;
			}
		}
		else if (cname == "func_pushable") {
			int modelIdx = ents[i]->getBspModelIdx();
			if (modelIdx < modelCount) {
				BSPMODEL& model = models[modelIdx];
				vec3 size = model.nMaxs - model.nMins;

				if (size.x > MAX_HULL1_SIZE_PUSHABLE || size.y > MAX_HULL1_SIZE_PUSHABLE) {
					return true;
				}
			}
		}
	}

	return false;
}

STRUCTCOUNT Bsp::delete_unused_hulls(bool noProgress) {
	if (!noProgress) {
		if (g_verbose)
			g_progress.update("", 0);
		else
			g_progress.update("Deleting unused hulls", modelCount - 1);
	}

	int deletedHulls = 0;

	for (int i = 1; i < modelCount; i++) {
		if (!g_verbose && !noProgress)
			g_progress.tick();

		vector<Entity*> usageEnts = get_model_ents(i);
		
		if (usageEnts.size() == 0) {
			if (g_verbose)
				logf("Deleting unused model %d\n", i);

			for (int k = 0; k < MAX_MAP_HULLS; k++)
				deletedHulls += models[i].iHeadnodes[k] >= 0;

			delete_model(i);
			//modelCount--; automatically updated when lump is replaced
			i--;
			continue;
		}

		set<string> conditionalPointEntTriggers;
		conditionalPointEntTriggers.insert("trigger_once");
		conditionalPointEntTriggers.insert("trigger_multiple");
		conditionalPointEntTriggers.insert("trigger_counter");
		conditionalPointEntTriggers.insert("trigger_gravity");
		conditionalPointEntTriggers.insert("trigger_teleport");

		set<string> entsThatNeverNeedAnyHulls;
		entsThatNeverNeedAnyHulls.insert("env_bubbles");
		entsThatNeverNeedAnyHulls.insert("func_mortar_field");
		entsThatNeverNeedAnyHulls.insert("func_tankcontrols");
		entsThatNeverNeedAnyHulls.insert("func_traincontrols");
		entsThatNeverNeedAnyHulls.insert("func_vehiclecontrols");
		entsThatNeverNeedAnyHulls.insert("trigger_autosave"); // obsolete in sven
		entsThatNeverNeedAnyHulls.insert("trigger_endsection"); // obsolete in sven

		set<string> entsThatNeverNeedCollision;
		entsThatNeverNeedCollision.insert("func_illusionary");

		set<string> passableEnts;
		passableEnts.insert("func_door");
		passableEnts.insert("func_door_rotating");
		passableEnts.insert("func_pendulum");
		passableEnts.insert("func_tracktrain");
		passableEnts.insert("func_train");
		passableEnts.insert("func_water");
		passableEnts.insert("momentary_door");

		set<string> playerOnlyTriggers;
		playerOnlyTriggers.insert("func_ladder");
		playerOnlyTriggers.insert("game_zone_player");
		playerOnlyTriggers.insert("player_respawn_zone");
		playerOnlyTriggers.insert("trigger_cdaudio");
		playerOnlyTriggers.insert("trigger_changelevel");
		playerOnlyTriggers.insert("trigger_transition");

		set<string> monsterOnlyTriggers;
		monsterOnlyTriggers.insert("func_monsterclip");
		monsterOnlyTriggers.insert("trigger_monsterjump");

		string uses = "";
		bool needsPlayerHulls = false;
		bool needsMonsterHulls = false;
		bool needsVisibleHull = false;
		for (int k = 0; k < usageEnts.size(); k++) {
			string cname = usageEnts[k]->keyvalues["classname"];
			string tname = usageEnts[k]->keyvalues["targetname"];
			int spawnflags = atoi(usageEnts[k]->keyvalues["spawnflags"].c_str());

			if (k != 0) {
				uses += ", ";
			}
			uses += "\"" + tname + "\" (" + cname + ")";

			if (entsThatNeverNeedAnyHulls.find(cname) != entsThatNeverNeedAnyHulls.end())  {
				continue; // no collision or faces needed at all
			}
			else if (entsThatNeverNeedCollision.find(cname) != entsThatNeverNeedCollision.end()) {
				needsVisibleHull = !is_invisible_solid(usageEnts[k]);
			}
			else if (passableEnts.find(cname) != passableEnts.end()) {
				needsPlayerHulls = needsMonsterHulls = !(spawnflags & 8); // "Passable" or "Not solid" unchecked
				needsVisibleHull = !(spawnflags & 8) || !is_invisible_solid(usageEnts[k]);
			}
			else if (cname.find("trigger_") == 0) {
				bool affectsPointEnts = spawnflags & 8; // "Everything else" flag checked

				if (affectsPointEnts && conditionalPointEntTriggers.find(cname) != conditionalPointEntTriggers.end()) {
					needsVisibleHull = true; // needed for point-ent collision
					needsPlayerHulls = !(spawnflags & 2); // "No clients" unchecked
					needsMonsterHulls = (spawnflags & 1) || (spawnflags & 4); // "monsters" or "pushables" checked
				}
				else if (cname == "trigger_push") { 
					needsPlayerHulls = !(spawnflags & 8); // "No clients" unchecked
					needsMonsterHulls = (spawnflags & 4) || !(spawnflags & 16); // "Pushables" checked or "No monsters" unchecked
					needsVisibleHull = true; // needed for point-ent pushing
				}
				else if (cname == "trigger_hurt") {
					needsPlayerHulls = !(spawnflags & 8); // "No clients" unchecked
					needsMonsterHulls = !(spawnflags & 16) || !(spawnflags & 32); // "Fire/Touch client only" unchecked
				}
				else {
					needsPlayerHulls = true;
					needsMonsterHulls = true;
				}
			}
			else if (cname == "func_clip") {
				needsPlayerHulls = !(spawnflags & 8); // "No clients" not checked
				needsMonsterHulls = (spawnflags & 8) || !(spawnflags & 16); // "Pushables" checked or "No monsters" unchecked
				needsVisibleHull = (spawnflags & 32) || (spawnflags & 64); // "Everything else" or "item_inv" checked
			}
			else if (cname == "func_conveyor") {
				needsPlayerHulls = needsMonsterHulls = !(spawnflags & 2); // "Not Solid" unchecked
				needsVisibleHull = !(spawnflags & 2) || !is_invisible_solid(usageEnts[k]);
			}
			else if (cname == "func_friction") {
				needsPlayerHulls = true;
				needsMonsterHulls = true;
			}
			else if (cname == "func_rot_button") {
				needsPlayerHulls = needsMonsterHulls = !(spawnflags & 1); // "Not solid" unchecked
			}
			else if (cname == "func_rotating") {
				needsPlayerHulls = needsMonsterHulls = !(spawnflags & 64); // "Not solid" unchecked
			}
			else if (playerOnlyTriggers.find(cname) != playerOnlyTriggers.end()) {
				needsPlayerHulls = true;
			}
			else if (monsterOnlyTriggers.find(cname) != monsterOnlyTriggers.end()) {
				needsMonsterHulls = true;
			}
			else {
				// assume all hulls are needed
				needsPlayerHulls = true;
				needsMonsterHulls = true;
				needsVisibleHull = true;
				break;
			}
		}

		BSPMODEL& model = ((BSPMODEL*)lumps[LUMP_MODELS])[i];

		if (!needsVisibleHull) {
			if (g_verbose && models[i].iHeadnodes[0] >= 0)
				logf("Deleting HULL 0 from model %d, used in %s\n", i, uses.c_str());

			deletedHulls += models[i].iHeadnodes[0] >= 0;

			model.iHeadnodes[0] = -1;
			model.nVisLeafs = 0;
			model.nFaces = 0;
			model.iFirstFace = 0;
		}
		if (!needsPlayerHulls && !needsMonsterHulls) {
			bool deletedAnyHulls = false;
			for (int k = 1; k < MAX_MAP_HULLS; k++) {
				deletedHulls += models[i].iHeadnodes[k] >= 0;
				if (models[i].iHeadnodes[k] >= 0) {
					deletedHulls++;
					deletedAnyHulls = true;
				}
			}

			if (g_verbose && deletedAnyHulls)
				logf("Deleting HULL 1-3 from model %d, used in %s\n", i, uses.c_str());

			model.iHeadnodes[1] = -1;
			model.iHeadnodes[2] = -1;
			model.iHeadnodes[3] = -1;
		}
		else if (!needsMonsterHulls) {
			if (g_verbose && models[i].iHeadnodes[2] >= 0)
				logf("Deleting HULL 2 from model %d, used in %s\n", i, uses.c_str());

			deletedHulls += models[i].iHeadnodes[2] >= 0;

			model.iHeadnodes[2] = -1;
		}
		else if (!needsPlayerHulls) {
			// monsters use all hulls so can't do anything about this
		}
	}

	STRUCTCOUNT removed = remove_unused_model_structures();

	update_ent_lump();

	if (!g_verbose && !noProgress) {
		g_progress.clear();
	}

	return removed;
}

bool Bsp::is_invisible_solid(Entity* ent) {
	if (!ent->isBspModel())
		return false;

	string tname = ent->keyvalues["targetname"];
	int rendermode = atoi(ent->keyvalues["rendermode"].c_str());
	int renderamt = atoi(ent->keyvalues["renderamt"].c_str());
	int renderfx = atoi(ent->keyvalues["renderfx"].c_str());

	if (rendermode == 0 || renderamt != 0) {
		return false;
	}
	switch(renderfx) {
		case 1: case 2: case 3: case 4: case 7: 
		case 8: case 15: case 16: case 17:
			return false;
		default:
			break;
	}
	
	static set<string> renderKeys {
		"rendermode",
		"renderamt",
		"renderfx"
	};

	for (int i = 0; i < ents.size(); i++) {
		string cname = ents[i]->keyvalues["classname"];

		if (cname == "env_render") {
			return false; // assume it will affect the brush since it can be moved anywhere
		}
		else if (cname == "env_render_individual") {
			if (ents[i]->keyvalues["target"] == tname) {
				return false; // assume it's making the ent visible
			}
		}
		else if (cname == "trigger_changevalue") {
			if (ents[i]->keyvalues["target"] == tname) {
				if (renderKeys.find(ents[i]->keyvalues["m_iszValueName"]) != renderKeys.end()) {
					return false; // assume it's making the ent visible
				}
			}
		}
		else if (cname == "trigger_copyvalue") {
			if (ents[i]->keyvalues["target"] == tname) {
				if (renderKeys.find(ents[i]->keyvalues["m_iszDstValueName"]) != renderKeys.end()) {
					return false; // assume it's making the ent visible
				}
			}
		}
		else if (cname == "trigger_createentity") {
			if (ents[i]->keyvalues["+model"] == tname || ents[i]->keyvalues["-model"] == ent->keyvalues["model"]) {
				return false; // assume this new ent will be visible at some point
			}
		}
		else if (cname == "trigger_changemodel") {
			if (ents[i]->keyvalues["model"] == ent->keyvalues["model"]) {
				return false; // assume the target is visible
			}
		}
	}

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

	byte* newEntData = new byte[str_data.size() + 1];
	memcpy(newEntData, str_data.c_str(), str_data.size());
	newEntData[str_data.size()] = 0; // null terminator required too(?)

	replace_lump(LUMP_ENTITIES, newEntData, str_data.size()+1);	
}

vec3 Bsp::get_model_center(int modelIdx) {
	if (modelIdx < 0 || modelIdx > header.lump[LUMP_MODELS].nLength / sizeof(BSPMODEL)) {
		logf("Invalid model index %d. Must be 0 - %d\n", modelIdx);
		return vec3();
	}

	BSPMODEL& model = models[modelIdx];

	return model.nMins + (model.nMaxs - model.nMins) * 0.5f;
}

void Bsp::write(string path) {
	if (path.rfind(".bsp") != path.size() - 4) {
		path = path + ".bsp";
	}

	logf("Writing %s", path.c_str());

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
			logf("FAILED TO READ BSP LUMP %d\n", i);
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
	for (int i = 0; i < ents.size(); i++)
		delete ents[i];
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
				logf("%s.bsp ent data (line %d): Unexpected '{'\n", path.c_str(), lineNum);
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
				logf("%s.bsp ent data (line %d): Unexpected '}'\n", path.c_str(), lineNum);
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

	if (ent != NULL)
		delete ent;
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
			targetname = ents[k]->keyvalues["targetname"];
			classname = ents[k]->keyvalues["classname"];
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

bool sortModelInfos(const STRUCTUSAGE* a, const STRUCTUSAGE* b) {
	switch (g_sort_mode) {
	case SORT_VERTS:
		return a->sum.verts > b->sum.verts;
	case SORT_NODES:
		return a->sum.nodes > b->sum.nodes;
	case SORT_CLIPNODES:
		return a->sum.clipnodes > b->sum.clipnodes;
	case SORT_FACES:
		return a->sum.faces > b->sum.faces;
	}
}

bool Bsp::isValid() {
	return modelCount < MAX_MAP_MODELS
		&& planeCount < MAX_MAP_PLANES
		&& vertCount < MAX_MAP_VERTS
		&& nodeCount < MAX_MAP_NODES
		&& texinfoCount < MAX_MAP_TEXINFOS
		&& faceCount < MAX_MAP_FACES
		&& clipnodeCount < MAX_MAP_CLIPNODES
		&& leafCount < MAX_MAP_LEAVES
		&& marksurfCount < MAX_MAP_MARKSURFS
		&& surfedgeCount < MAX_MAP_SURFEDGES
		&& edgeCount < MAX_MAP_SURFEDGES
		&& textureCount < MAX_MAP_TEXTURES
		&& lightDataLength < MAX_MAP_LIGHTDATA
		&& visDataLength < MAX_MAP_VISDATA
		&& ents.size() < MAX_MAP_ENTS;

}

bool Bsp::validate() {
	bool isValid = true;

	for (int i = 0; i < marksurfCount; i++) {
		if (marksurfs[i] >= faceCount) {
			logf("Bad face reference in marksurf %d: %d / %d\n", i, marksurfs[i], faceCount);
			isValid = false;
		}
	}
	for (int i = 0; i < surfedgeCount; i++) {
		if (abs(surfedges[i]) >= edgeCount) {
			logf("Bad edge reference in surfedge %d: %d / %d\n", i, surfedges[i], edgeCount);
			isValid = false;
		}
	}
	for (int i = 0; i < texinfoCount; i++) {
		if (texinfos[i].iMiptex < 0 || texinfos[i].iMiptex >= textureCount) {
			logf("Bad texture reference in textureinfo %d: %d / %d\n", i, texinfos[i].iMiptex, textureCount);
			isValid = false;
		}
	}
	for (int i = 0; i < faceCount; i++) {
		if (faces[i].iPlane < 0 || faces[i].iPlane >= planeCount) {
			logf("Bad plane reference in face %d: %d / %d\n", i, faces[i].iPlane, planeCount);
			isValid = false;
		}
		if (faces[i].nEdges > 0 && (faces[i].iFirstEdge < 0 || faces[i].iFirstEdge >= surfedgeCount)) {
			logf("Bad surfedge reference in face %d: %d / %d\n", i, faces[i].iFirstEdge, surfedgeCount);
			isValid = false;
		}
		if (faces[i].iTextureInfo < 0 || faces[i].iTextureInfo >= texinfoCount) {
			logf("Bad textureinfo reference in face %d: %d / %d\n", i, faces[i].iTextureInfo, texinfoCount);
			isValid = false;
		}
		if (lightDataLength > 0 && faces[i].nStyles[0] != 255 && 
			faces[i].nLightmapOffset != (uint32_t)-1 && faces[i].nLightmapOffset >= lightDataLength) 
		{
			logf("Bad lightmap offset in face %d: %d / %d\n", i, faces[i].nLightmapOffset, lightDataLength);
			isValid = false;
		}
	}
	for (int i = 0; i < leafCount; i++) {
		if (leaves[i].nMarkSurfaces > 0 && (leaves[i].iFirstMarkSurface < 0 || leaves[i].iFirstMarkSurface >= marksurfCount)) {
			logf("Bad marksurf reference in leaf %d: %d / %d\n", i, leaves[i].iFirstMarkSurface, marksurfCount);
			isValid = false;
		}
		if (visDataLength > 0 && leaves[i].nVisOffset < -1 || leaves[i].nVisOffset >= visDataLength) {
			logf("Bad vis offset in leaf %d: %d / %d\n", i, leaves[i].nVisOffset, visDataLength);
			isValid = false;
		}
	}
	for (int i = 0; i < edgeCount; i++) {
		for (int k = 0; k < 2; k++) {
			if (edges[i].iVertex[k] >= vertCount) {
				logf("Bad vertex reference in edge %d: %d / %d\n", i, edges[i].iVertex[k], vertCount);
				isValid = false;
			}
		}
	}
	for (int i = 0; i < nodeCount; i++) {
		if (nodes[i].nFaces > 0 && (nodes[i].firstFace < 0 || nodes[i].firstFace >= faceCount)) {
			logf("Bad face reference in node %d: %d / %d\n", i, nodes[i].firstFace, faceCount);
			isValid = false;
		}
		if (nodes[i].iPlane < 0 || nodes[i].iPlane >= planeCount) {
			logf("Bad plane reference in node %d: %d / %d\n", i, nodes[i].iPlane, planeCount);
			isValid = false;
		}
		for (int k = 0; k < 2; k++) {
			if (nodes[i].iChildren[k] >= nodeCount) {
				logf("Bad node reference in node %d child %d: %d / %d\n", i, k, nodes[i].iChildren[k], nodeCount);
				isValid = false;
			}
			else if (nodes[i].iChildren[k] < 0 && ~nodes[i].iChildren[k] >= leafCount) {
				logf("Bad leaf reference in node %d child %d: %d / %d\n", i, k, ~nodes[i].iChildren[k], leafCount);
				isValid = false;
			}
		}
	}
	for (int i = 0; i < clipnodeCount; i++) {
		if (clipnodes[i].iPlane < 0 || clipnodes[i].iPlane >= planeCount) {
			logf("Bad plane reference in clipnode %d: %d / %d\n", i, clipnodes[i].iPlane, planeCount);
			isValid = false;
		}
		for (int k = 0; k < 2; k++) {
			if (clipnodes[i].iChildren[k] >= clipnodeCount) {
				logf("Bad clipnode reference in clipnode %d child %d: %d / %d\n", i, k, clipnodes[i].iChildren[k], clipnodeCount);
				isValid = false;
			}
		}
	}
	for (int i = 0; i < ents.size(); i++) {
		if (ents[i]->getBspModelIdx() >= modelCount) {
			logf("Bad model reference in entity %d: %d / %d\n", i, ents[i]->getBspModelIdx(), modelCount);
			isValid = false;
		}
	}


	int totalVisLeaves = 1; // solid leaf not included in model leaf counts
	int totalFaces = 0;
	for (int i = 0; i < modelCount; i++) {
		totalVisLeaves += models[i].nVisLeafs;
		totalFaces += models[i].nFaces;
		if (models[i].nFaces > 0 && (models[i].iFirstFace < 0 || models[i].iFirstFace >= faceCount)) {
			logf("Bad face reference in model %d: %d / %d\n", i, models[i].iFirstFace, faceCount);
			isValid = false;
		}
		for (int k = 0; k < MAX_MAP_HULLS; k++) {
			if (models[i].iHeadnodes[k] >= clipnodeCount) {
				logf("Bad clipnode reference in model %d hull %d: %d / %d\n", i, k, models[i].iHeadnodes[k], clipnodeCount);
				isValid = false;
			}
		}
	}
	if (totalVisLeaves != leafCount) {
		logf("Bad model vis leaf sum: %d / %d\n", totalVisLeaves, leafCount);
		isValid = false;
	}
	if (totalFaces != faceCount) {
		logf("Bad model face sum: %d / %d\n", totalFaces, faceCount);
		isValid = false;
	}

	return isValid;
}

vector<STRUCTUSAGE*> Bsp::get_sorted_model_infos(int sortMode) {
	vector<STRUCTUSAGE*> modelStructs;
	modelStructs.resize(modelCount);

	for (int i = 0; i < modelCount; i++) {
		modelStructs[i] = new STRUCTUSAGE(this);
		modelStructs[i]->modelIdx = i;
		mark_model_structures(i, modelStructs[i]);
		modelStructs[i]->compute_sum();
	}

	g_sort_mode = sortMode;
	sort(modelStructs.begin(), modelStructs.end(), sortModelInfos);

	return modelStructs;
}

void Bsp::print_info(bool perModelStats, int perModelLimit, int sortMode) {
	int entCount = ents.size();

	if (perModelStats) {
		g_sort_mode = sortMode;

		if (planeCount >= MAX_MAP_PLANES || texinfoCount >= MAX_MAP_TEXINFOS || leafCount >= MAX_MAP_LEAVES ||
			modelCount >= MAX_MAP_MODELS || nodeCount >= MAX_MAP_NODES || vertCount >= MAX_MAP_VERTS ||
			faceCount >= MAX_MAP_FACES || clipnodeCount >= MAX_MAP_CLIPNODES || marksurfCount >= MAX_MAP_MARKSURFS ||
			surfedgeCount >= MAX_MAP_SURFEDGES || edgeCount >= MAX_MAP_EDGES || textureCount >= MAX_MAP_TEXTURES ||
			lightDataLength >= MAX_MAP_LIGHTDATA || visDataLength >= MAX_MAP_VISDATA) 
		{
			logf("Unable to show model stats while BSP limits are exceeded.\n");
			return;
		}

		vector<STRUCTUSAGE*> modelStructs = get_sorted_model_infos(sortMode);

		int maxCount;
		char* countName;

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
		print_stat("models", modelCount, MAX_MAP_MODELS, false);
		print_stat("planes", planeCount, MAX_MAP_PLANES, false);
		print_stat("vertexes", vertCount, MAX_MAP_VERTS, false);
		print_stat("nodes", nodeCount, MAX_MAP_NODES, false);
		print_stat("texinfos", texinfoCount, MAX_MAP_TEXINFOS, false);
		print_stat("faces", faceCount, MAX_MAP_FACES, false);
		print_stat("clipnodes", clipnodeCount, MAX_MAP_CLIPNODES, false);
		print_stat("leaves", leafCount, MAX_MAP_LEAVES, false);
		print_stat("marksurfaces", marksurfCount, MAX_MAP_MARKSURFS, false);
		print_stat("surfedges", surfedgeCount, MAX_MAP_SURFEDGES, false);
		print_stat("edges", edgeCount, MAX_MAP_SURFEDGES, false);
		print_stat("textures", textureCount, MAX_MAP_TEXTURES, false);
		print_stat("lightdata", lightDataLength, MAX_MAP_LIGHTDATA, true);
		print_stat("visdata", visDataLength, MAX_MAP_VISDATA, true);
		print_stat("entities", entCount, MAX_MAP_ENTS, false);
	}
}

void Bsp::print_model_bsp(int modelIdx) {
	int node = models[modelIdx].iHeadnodes[0];
	recurse_node(node, 0);
}

void Bsp::print_clipnode_tree(int iNode, int depth) {
	for (int i = 0; i < depth; i++) {
		logf("    ");
	}

	if (iNode < 0) {
		print_contents(iNode);
		logf("\n");
		return;
	}
	else {
		BSPPLANE& plane = planes[clipnodes[iNode].iPlane];
		logf("NODE (%.2f, %.2f, %.2f) @ %.2f\n", plane.vNormal.x, plane.vNormal.y, plane.vNormal.z, plane.fDist);
	}
	

	for (int i = 0; i < 2; i++) {
		print_clipnode_tree(clipnodes[iNode].iChildren[i], depth+1);
	}
}

void Bsp::print_model_hull(int modelIdx, int hull_number) {
	if (modelIdx < 0 || modelIdx > header.lump[LUMP_MODELS].nLength / sizeof(BSPMODEL)) {
		logf("Invalid model index %d. Must be 0 - %d\n", modelIdx);
		return;
	}

	// the first hull is used for point-sized clipping, but uses nodes and not clipnodes.
	if (hull_number < 0 || hull_number >= MAX_MAP_HULLS) {
		logf("Invalid hull number. Clipnode hull numbers are 0 - %d\n", MAX_MAP_HULLS);
		return;
	}

	BSPMODEL& model = models[modelIdx];

	logf("Model %d Hull %d - %s\n", modelIdx, hull_number, get_model_usage(modelIdx).c_str());

	if (hull_number == 0)
		print_model_bsp(modelIdx);
	else
		print_clipnode_tree(model.iHeadnodes[hull_number], 0);
}

string Bsp::get_model_usage(int modelIdx) {
	for (int i = 0; i < ents.size(); i++) {
		if (ents[i]->getBspModelIdx() == modelIdx) {
			return "\"" + ents[i]->keyvalues["targetname"] + "\" (" + ents[i]->keyvalues["classname"] + ")";
		}
	}
	return "(unused)";
}

vector<Entity*> Bsp::get_model_ents(int modelIdx) {
	vector<Entity*> uses;
	for (int i = 0; i < ents.size(); i++) {
		if (ents[i]->getBspModelIdx() == modelIdx) {
			uses.push_back(ents[i]);
		}
	}
	return uses;
}

void Bsp::recurse_node(int16_t nodeIdx, int depth) {
	for (int i = 0; i < depth; i++) {
		logf("    ");
	}

	if (nodeIdx < 0) {
		BSPLEAF& leaf = leaves[~nodeIdx];
		print_leaf(leaf);
		logf(" (LEAF %d)\n", ~nodeIdx);
		return;
	}
	else {
		print_node(nodes[nodeIdx]);
		logf("\n");
	}
	
	recurse_node(nodes[nodeIdx].iChildren[0], depth+1);
	recurse_node(nodes[nodeIdx].iChildren[1], depth+1);
}

void Bsp::print_node(BSPNODE node) {
	BSPPLANE& plane = planes[node.iPlane];

	logf("Plane (%f %f %f) d: %f, Faces: %d, Min(%d, %d, %d), Max(%d, %d, %d)",
		plane.vNormal.x, plane.vNormal.y, plane.vNormal.z,
		plane.fDist, node.nFaces,
		node.nMins[0], node.nMins[1], node.nMins[2],
		node.nMaxs[0], node.nMaxs[1], node.nMaxs[2]);
}

int Bsp::pointContents(int iNode, vec3 p) {
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

	//logf << "Contents at " << p.x << " " << p.y << " " << p.z << " is ";
	//print_leaf(leaves[~iNode]);
	//logf << " (LEAF " << ~iNode << ")\n";

	return leaves[~iNode].nContents;
}

void Bsp::mark_face_structures(int iFace, STRUCTUSAGE* usage) {
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

	usage->texInfo[face.iTextureInfo] = true;
	usage->planes[face.iPlane] = true;
	usage->textures[texinfos[face.iTextureInfo].iMiptex] = true;
}

void Bsp::mark_node_structures(int iNode, STRUCTUSAGE* usage) {
	BSPNODE& node = nodes[iNode];

	usage->nodes[iNode] = true;
	usage->planes[node.iPlane] = true;

	for (int i = 0; i < node.nFaces; i++) {
		mark_face_structures(node.firstFace + i, usage);
	}

	for (int i = 0; i < 2; i++) {
		if (node.iChildren[i] >= 0) {
			mark_node_structures(node.iChildren[i], usage);
		}
		else {
			BSPLEAF& leaf = leaves[~node.iChildren[i]];
			for (int i = 0; i < leaf.nMarkSurfaces; i++) {
				usage->markSurfs[leaf.iFirstMarkSurface + i] = true;
				mark_face_structures(marksurfs[leaf.iFirstMarkSurface + i], usage);
			}

			usage->leaves[~node.iChildren[i]] = true;
		}
	}
}

void Bsp::mark_clipnode_structures(int iNode, STRUCTUSAGE* usage) {
	BSPCLIPNODE& node = clipnodes[iNode];

	usage->clipnodes[iNode] = true;
	usage->planes[node.iPlane] = true;

	if (node.iPlane == 258) {
		logf("");
	}

	for (int i = 0; i < 2; i++) {
		if (node.iChildren[i] >= 0) {
			mark_clipnode_structures(node.iChildren[i], usage);
		}
	}
}

void Bsp::mark_model_structures(int modelIdx, STRUCTUSAGE* usage) {
	BSPMODEL& model = models[modelIdx];

	if (model.iHeadnodes[0] >= 0 && model.iHeadnodes[0] < nodeCount)
		mark_node_structures(model.iHeadnodes[0], usage);
	for (int k = 1; k < MAX_MAP_HULLS; k++) {
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
	//face.iTextureInfo = remap->texInfo[face.iTextureInfo];
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

void Bsp::delete_hull(int hull_number, int redirect) {
	if (hull_number < 0 || hull_number >= MAX_MAP_HULLS) {
		logf("Invalid hull number. Valid hull numbers are 1-%d\n", MAX_MAP_HULLS);
		return;
	}

	for (int i = 0; i < modelCount; i++) {
		delete_hull(hull_number, i, redirect);
	}
}

void Bsp::delete_hull(int hull_number, int modelIdx, int redirect) {
	if (modelIdx < 0 || modelIdx >= modelCount) {
		logf("Invalid model index %d. Must be 0-%d\n", modelIdx);
		return;
	}

	// the first hull is used for point-sized clipping, but uses nodes and not clipnodes.
	if (hull_number < 0 || hull_number >= MAX_MAP_HULLS) {
		logf("Invalid hull number. Valid hull numbers are 1-%d\n", MAX_MAP_HULLS);
		return;
	}

	if (redirect >= MAX_MAP_HULLS) {
		logf("Invalid redirect hull number. Valid redirect hulls are 1-%d\n", MAX_MAP_HULLS);
		return;
	}

	if (hull_number == 0 && redirect > 0) {
		logf("Hull 0 can't be redirected. Hull 0 is the only hull that doesn't use clipnodes.\n", MAX_MAP_HULLS);
		return;
	}

	BSPMODEL& model = models[modelIdx];

	if (hull_number == 0) {
		model.iHeadnodes[0] = -1; // redirect to solid leaf
		model.nVisLeafs = 0;
		model.nFaces = 0;
		model.iFirstFace = 0;
	}
	else if (redirect > 0) {
		if (model.iHeadnodes[hull_number] > 0 && model.iHeadnodes[redirect] < 0) {
			//logf("WARNING: HULL %d is empty\n", redirect);
		}
		else if (model.iHeadnodes[hull_number] == model.iHeadnodes[redirect]) {
			//logf("WARNING: HULL %d and %d are already sharing clipnodes\n", hull_number, redirect);
		}
		model.iHeadnodes[hull_number] = model.iHeadnodes[redirect];
	}
	else {
		model.iHeadnodes[hull_number] = CONTENTS_EMPTY;
	}	
}

void Bsp::delete_model(int modelIdx) {
	byte* oldModels = (byte*)models;

	int newSize = (modelCount - 1) * sizeof(BSPMODEL);
	byte* newModels = new byte[newSize];

	memcpy(newModels, oldModels, modelIdx * sizeof(BSPMODEL));
	memcpy(newModels + modelIdx * sizeof(BSPMODEL), 
		   oldModels + (modelIdx+1) * sizeof(BSPMODEL), 
		   (modelCount - (modelIdx+1))*sizeof(BSPMODEL));

	replace_lump(LUMP_MODELS, newModels, newSize);

	// update model index references
	for (int i = 0; i < ents.size(); i++) {
		int entModel = ents[i]->getBspModelIdx();
		if (entModel == modelIdx) {
			ents[i]->setOrAddKeyvalue("model", "error.mdl");
		}
		else if (entModel > modelIdx) {
			ents[i]->setOrAddKeyvalue("model", "*" + to_string(entModel - 1));
		}
	}
}

int Bsp::create_solid(vec3 mins, vec3 maxs, int textureIdx) {
	int newModelIdx = create_model();
	BSPMODEL& newModel = models[newModelIdx];

	create_node_box(mins, maxs, &newModel, textureIdx);
	create_clipnode_box(mins, maxs, &newModel);

	//remove_unused_model_structures(); // will also resize VIS data for new leaf count

	return newModelIdx;
}

int Bsp::create_solid(Solid& solid, int targetModelIdx) {
	int modelIdx = targetModelIdx >= 0 ? targetModelIdx : create_model();
	BSPMODEL& newModel = models[modelIdx];

	create_nodes(solid, &newModel);
	regenerate_clipnodes(modelIdx);

	return modelIdx;
}

void Bsp::add_model(Bsp* sourceMap, int modelIdx) {
	STRUCTUSAGE usage(sourceMap);
	sourceMap->mark_model_structures(modelIdx, &usage);

	// TODO: add the model lel

	usage.compute_sum();

	logf("");
}

int Bsp::create_leaf(int contents) {
	BSPLEAF* newLeaves = new BSPLEAF[leafCount + 1];
	memcpy(newLeaves, leaves, leafCount * sizeof(BSPLEAF));

	BSPLEAF& newLeaf = newLeaves[leafCount];
	memset(&newLeaf, 0, sizeof(BSPLEAF));

	newLeaf.nVisOffset = -1;
	newLeaf.nContents = contents;

	int newLeafIdx = leafCount;

	replace_lump(LUMP_LEAVES, newLeaves, (leafCount+1) * sizeof(BSPLEAF));

	return newLeafIdx;
}

void Bsp::create_node_box(vec3 min, vec3 max, BSPMODEL* targetModel, int textureIdx) {

	// add new verts (1 for each corner)
	// TODO: subdivide faces to prevent max surface extents error
	int startVert = vertCount;
	{
		vec3* newVerts = new vec3[vertCount + 8];
		memcpy(newVerts, verts, vertCount * sizeof(vec3));

		newVerts[vertCount + 0] = vec3(min.x, min.y, min.z); // front-left-bottom
		newVerts[vertCount + 1] = vec3(max.x, min.y, min.z); // front-right-bottom
		newVerts[vertCount + 2] = vec3(max.x, max.y, min.z); // back-right-bottom
		newVerts[vertCount + 3] = vec3(min.x, max.y, min.z); // back-left-bottom

		newVerts[vertCount + 4] = vec3(min.x, min.y, max.z); // front-left-top
		newVerts[vertCount + 5] = vec3(max.x, min.y, max.z); // front-right-top
		newVerts[vertCount + 6] = vec3(max.x, max.y, max.z); // back-right-top
		newVerts[vertCount + 7] = vec3(min.x, max.y, max.z); // back-left-top

		replace_lump(LUMP_VERTICES, newVerts, (vertCount + 8) * sizeof(vec3));
	}

	// add new edges (4 for each face)
	// TODO: subdivide >512
	int startEdge = edgeCount;
	{
		BSPEDGE* newEdges = new BSPEDGE[edgeCount + 12];
		memcpy(newEdges, edges, edgeCount * sizeof(BSPEDGE));

		// left
		newEdges[startEdge + 0] = BSPEDGE(startVert + 3, startVert + 0);
		newEdges[startEdge + 1] = BSPEDGE(startVert + 4, startVert + 7);

		// right
		newEdges[startEdge + 2] = BSPEDGE(startVert + 1, startVert + 2); // bottom edge
		newEdges[startEdge + 3] = BSPEDGE(startVert + 6, startVert + 5); // right edge

		// front
		newEdges[startEdge + 4] = BSPEDGE(startVert + 0, startVert + 1); // bottom edge
		newEdges[startEdge + 5] = BSPEDGE(startVert + 5, startVert + 4); // top edge

		// back
		newEdges[startEdge + 6] = BSPEDGE(startVert + 3, startVert + 7); // left edge
		newEdges[startEdge + 7] = BSPEDGE(startVert + 6, startVert + 2); // right edge

		// bottom
		newEdges[startEdge + 8] = BSPEDGE(startVert + 3, startVert + 2);
		newEdges[startEdge + 9] = BSPEDGE(startVert + 1, startVert + 0);

		// top
		newEdges[startEdge + 10] = BSPEDGE(startVert + 7, startVert + 4);
		newEdges[startEdge + 11] = BSPEDGE(startVert + 5, startVert + 6);

		replace_lump(LUMP_EDGES, newEdges, (edgeCount + 12) * sizeof(BSPEDGE));
	}

	// add new surfedges (2 for each edge)
	int startSurfedge = surfedgeCount;
	{
		int32_t* newSurfedges = new int32_t[surfedgeCount + 24];
		memcpy(newSurfedges, surfedges, surfedgeCount * sizeof(int32_t));

		// reverse cuz i fucked the edge order and I don't wanna redo
		for (int i = 12-1; i >= 0; i--) {
			int32_t edgeIdx = startEdge + i;
			newSurfedges[startSurfedge + (i*2)] = -edgeIdx;
			newSurfedges[startSurfedge + (i*2) + 1] = edgeIdx; // negative = use second vertex in edge
		}
		
		replace_lump(LUMP_SURFEDGES, newSurfedges, (surfedgeCount + 24) * sizeof(int32_t));
	}

	// add new planes (1 for each face/node)
	int startPlane = planeCount;
	{
		BSPPLANE* newPlanes = new BSPPLANE[planeCount + 6];
		memcpy(newPlanes, planes, planeCount * sizeof(BSPPLANE));

		newPlanes[startPlane + 0] = { vec3(1, 0, 0), min.x, PLANE_X }; // left
		newPlanes[startPlane + 1] = { vec3(1, 0, 0), max.x, PLANE_X }; // right
		newPlanes[startPlane + 2] = { vec3(0, 1, 0), min.y, PLANE_Y }; // front
		newPlanes[startPlane + 3] = { vec3(0, 1, 0), max.y, PLANE_Y }; // back
		newPlanes[startPlane + 4] = { vec3(0, 0, 1), min.z, PLANE_Z }; // bottom
		newPlanes[startPlane + 5] = { vec3(0, 0, 1), max.z, PLANE_Z }; // top

		replace_lump(LUMP_PLANES, newPlanes, (planeCount + 6) * sizeof(BSPPLANE));
	}

	int startTexinfo = texinfoCount;
	{
		BSPTEXTUREINFO* newTexinfos = new BSPTEXTUREINFO[texinfoCount + 6];
		memcpy(newTexinfos, texinfos, texinfoCount * sizeof(BSPTEXTUREINFO));

		vec3 up = vec3(0, 0, 1);
		vec3 right = vec3(1, 0, 0);
		vec3 forward = vec3(0, 1, 0);

		vec3 faceNormals[6]{
			vec3(-1, 0, 0),	// left
			vec3(1, 0, 0), // right
			vec3(0, 1, 0), // front
			vec3(0, -1, 0), // back
			vec3(0, 0, -1), // bottom
			vec3(0, 0, 1) // top
		};
		vec3 faceUp[6] {
			vec3(0, 0, 1),	// left
			vec3(0, 0, 1), // right
			vec3(0, 0, 1), // front
			vec3(0, 0, 1), // back
			vec3(0, -1, 0), // bottom
			vec3(0, 1, 0) // top
		};

		for (int i = 0; i < 6; i++) {
			BSPTEXTUREINFO& info = newTexinfos[startTexinfo + i];
			info.iMiptex = textureIdx;
			info.nFlags = 0;
			info.shiftS = 0;
			info.shiftT = 0;
			info.vT = faceUp[i];
			info.vS = crossProduct(faceUp[i], faceNormals[i]);
			// TODO: fit texture to face
		}

		replace_lump(LUMP_TEXINFO, newTexinfos, (texinfoCount + 6) * sizeof(BSPTEXTUREINFO));
	}

	// add new faces
	int startFace = faceCount;
	{
		BSPFACE* newFaces = new BSPFACE[faceCount + 6];
		memcpy(newFaces, faces, faceCount * sizeof(BSPFACE));

		for (int i = 0; i < 6; i++) {
			BSPFACE& face = newFaces[faceCount + i];
			face.iFirstEdge = startSurfedge + i * 4;
			face.iPlane = startPlane + i;
			face.nEdges = 4;
			face.nPlaneSide = i % 2 == 0; // even-numbered planes are inverted
			face.iTextureInfo = startTexinfo+i;
			face.nLightmapOffset = 0; // TODO: Lighting
			memset(face.nStyles, 0, 4);
		}

		replace_lump(LUMP_FACES, newFaces, (faceCount + 6) * sizeof(BSPFACE));
	}

	// Submodels don't use leaves like the world does. Everything except nContents is ignored.
	// There's really no need to create leaves for submodels. Every map will have a shared
	// SOLID leaf, and there should be at least one EMPTY leaf if the map isn't completely solid.
	// So, just find an existing EMPTY leaf. Also, water brushes work just fine with SOLID nodes.
	// The inner contents of a node is changed dynamically by entity properties.
	int16 sharedSolidLeaf = 0;
	int16 anyEmptyLeaf = 0;
	for (int i = 0; i < leafCount; i++) {
		if (leaves[i].nContents == CONTENTS_EMPTY) {
			anyEmptyLeaf = i;
			break;
		}
	}
	// If emptyLeaf is still 0 (SOLID), it means the map is fully solid, so the contents wouldn't matter.
	// Anyway, still setting this in case someone wants to copy the model to another map
	if (anyEmptyLeaf == 0) {
		anyEmptyLeaf = create_leaf(CONTENTS_EMPTY);
		targetModel->nVisLeafs = 1;
	}
	else {
		targetModel->nVisLeafs = 0;
	}
	
	// add new nodes
	int startNode = nodeCount;
	{
		BSPNODE* newNodes = new BSPNODE[nodeCount + 6];
		memcpy(newNodes, nodes, nodeCount * sizeof(BSPNODE));

		int16 nodeIdx = nodeCount;

		for (int k = 0; k < 6; k++) {
			BSPNODE& node = newNodes[nodeCount + k];
			memset(&node, 0, sizeof(BSPNODE));

			node.firstFace = startFace + k; // face required for decals
			node.nFaces = 1;
			node.iPlane = startPlane + k;
			// node mins/maxs don't matter for submodels. Leave them at 0.

			int16 insideContents = k == 5 ? ~sharedSolidLeaf : (int16)(nodeCount + k+1);
			int16 outsideContents = ~anyEmptyLeaf;

			// can't have negative normals on planes so children are swapped instead
			if (k % 2 == 0) {
				node.iChildren[0] = insideContents;
				node.iChildren[1] = outsideContents;
			}
			else {
				node.iChildren[0] = outsideContents;
				node.iChildren[1] = insideContents;
			}
		}

		replace_lump(LUMP_NODES, newNodes, (nodeCount + 6) * sizeof(BSPNODE));
	}

	targetModel->iHeadnodes[0] = startNode;
	targetModel->iFirstFace = startFace;
	targetModel->nFaces = 6;

	targetModel->nMaxs = vec3(-9e99, -9e99, -9e99);
	targetModel->nMins = vec3(9e99, 9e99, 9e99);
	for (int i = 0; i < 8; i++) {
		vec3 v = verts[startVert + i];

		if (v.x > targetModel->nMaxs.x) targetModel->nMaxs.x = v.x;
		if (v.y > targetModel->nMaxs.y) targetModel->nMaxs.y = v.y;
		if (v.z > targetModel->nMaxs.z) targetModel->nMaxs.z = v.z;

		if (v.x < targetModel->nMins.x) targetModel->nMins.x = v.x;
		if (v.y < targetModel->nMins.y) targetModel->nMins.y = v.y;
		if (v.z < targetModel->nMins.z) targetModel->nMins.z = v.z;
	}
}

void Bsp::create_nodes(Solid& solid, BSPMODEL* targetModel) {

	vector<int> newVertIndexes;
	int startVert = vertCount;
	{
		vec3* newVerts = new vec3[vertCount + solid.hullVerts.size()];
		memcpy(newVerts, verts, vertCount * sizeof(vec3));

		for (int i = 0; i < solid.hullVerts.size(); i++) {
			newVerts[vertCount + i] = solid.hullVerts[i].pos;
			newVertIndexes.push_back(vertCount + i);
		}

		replace_lump(LUMP_VERTICES, newVerts, (vertCount + solid.hullVerts.size()) * sizeof(vec3));
	}

	// add new edges (not actually edges - just an indirection layer for the verts)
	// TODO: subdivide >512
	int startEdge = edgeCount;
	map<int, int32_t> vertToSurfedge;
	{
		int addEdges = (solid.hullVerts.size() + 1) / 2;

		BSPEDGE* newEdges = new BSPEDGE[edgeCount + addEdges];
		memcpy(newEdges, edges, edgeCount * sizeof(BSPEDGE));

		int idx = 0;
		for (int i = 0; i < solid.hullVerts.size(); i += 2) {
			int v0 = i;
			int v1 = (i+1) % solid.hullVerts.size();
			newEdges[startEdge + idx] = BSPEDGE(newVertIndexes[v0], newVertIndexes[v1]);

			vertToSurfedge[v0] = startEdge + idx;
			if (v1 > 0) {
				vertToSurfedge[v1] = -(startEdge + idx); // negative = use second vert
			}

			idx++;
		}
		replace_lump(LUMP_EDGES, newEdges, (edgeCount + addEdges) * sizeof(BSPEDGE));
	}

	// add new surfedges (2 for each edge)
	int startSurfedge = surfedgeCount;
	{
		int addSurfedges = 0;
		for (int i = 0; i < solid.faces.size(); i++) {
			addSurfedges += solid.faces[i].verts.size();
		}

		int32_t* newSurfedges = new int32_t[surfedgeCount + addSurfedges];
		memcpy(newSurfedges, surfedges, surfedgeCount * sizeof(int32_t));

		int idx = 0;
		for (int i = 0; i < solid.faces.size(); i++) {
			for (int k = 0; k < solid.faces[i].verts.size(); k++) {
				newSurfedges[startSurfedge + idx++] = vertToSurfedge[solid.faces[i].verts[k]];
			}
		}

		replace_lump(LUMP_SURFEDGES, newSurfedges, (surfedgeCount + addSurfedges) * sizeof(int32_t));
	}

	// add new planes (1 for each face/node)
	// TODO: reuse existing planes (maybe not until shared stuff can be split when editing solids)
	int startPlane = planeCount;
	{
		BSPPLANE* newPlanes = new BSPPLANE[planeCount + solid.faces.size()];
		memcpy(newPlanes, planes, planeCount * sizeof(BSPPLANE));

		for (int i = 0; i < solid.faces.size(); i++) {
			newPlanes[startPlane + i] = solid.faces[i].plane;
		}

		replace_lump(LUMP_PLANES, newPlanes, (planeCount + solid.faces.size()) * sizeof(BSPPLANE));
	}

	// add new faces
	int startFace = faceCount;
	{
		BSPFACE* newFaces = new BSPFACE[faceCount + solid.faces.size()];
		memcpy(newFaces, faces, faceCount * sizeof(BSPFACE));

		int surfedgeOffset = 0;
		for (int i = 0; i < solid.faces.size(); i++) {
			BSPFACE& face = newFaces[faceCount + i];
			face.iFirstEdge = startSurfedge + surfedgeOffset;
			face.iPlane = startPlane + i;
			face.nEdges = solid.faces[i].verts.size();
			face.nPlaneSide = solid.faces[i].planeSide;
			//face.iTextureInfo = startTexinfo + i;
			face.iTextureInfo = solid.faces[i].iTextureInfo;
			face.nLightmapOffset = 0; // TODO: Lighting
			memset(face.nStyles, 0, 4);

			surfedgeOffset += face.nEdges;
		}

		replace_lump(LUMP_FACES, newFaces, (faceCount + solid.faces.size()) * sizeof(BSPFACE));
	}

	//TODO: move to common function
	int16 sharedSolidLeaf = 0;
	int16 anyEmptyLeaf = 0;
	for (int i = 0; i < leafCount; i++) {
		if (leaves[i].nContents == CONTENTS_EMPTY) {
			anyEmptyLeaf = i;
			break;
		}
	}
	if (anyEmptyLeaf == 0) {
		anyEmptyLeaf = create_leaf(CONTENTS_EMPTY);
		targetModel->nVisLeafs = 1;
	}
	else {
		targetModel->nVisLeafs = 0;
	}

	// add new nodes
	int startNode = nodeCount;
	{
		BSPNODE* newNodes = new BSPNODE[nodeCount + solid.faces.size()];
		memcpy(newNodes, nodes, nodeCount * sizeof(BSPNODE));

		int16 nodeIdx = nodeCount;

		for (int k = 0; k < solid.faces.size(); k++) {
			BSPNODE& node = newNodes[nodeCount + k];
			memset(&node, 0, sizeof(BSPNODE));

			node.firstFace = startFace + k; // face required for decals
			node.nFaces = 1;
			node.iPlane = startPlane + k;
			// node mins/maxs don't matter for submodels. Leave them at 0.

			int16 insideContents = k == solid.faces.size()-1 ? ~sharedSolidLeaf : (int16)(nodeCount + k + 1);
			int16 outsideContents = ~anyEmptyLeaf;

			// can't have negative normals on planes so children are swapped instead
			if (solid.faces[k].planeSide) {
				node.iChildren[0] = insideContents;
				node.iChildren[1] = outsideContents;
			}
			else {
				node.iChildren[0] = outsideContents;
				node.iChildren[1] = insideContents;
			}
		}

		replace_lump(LUMP_NODES, newNodes, (nodeCount + solid.faces.size()) * sizeof(BSPNODE));
	}

	targetModel->iHeadnodes[0] = startNode;
	targetModel->iHeadnodes[1] = CONTENTS_EMPTY;
	targetModel->iHeadnodes[2] = CONTENTS_EMPTY;
	targetModel->iHeadnodes[3] = CONTENTS_EMPTY;
	targetModel->iFirstFace = startFace;
	targetModel->nFaces = solid.faces.size();

	targetModel->nMaxs = vec3(-9e99, -9e99, -9e99);
	targetModel->nMins = vec3(9e99, 9e99, 9e99);
	for (int i = 0; i < solid.hullVerts.size(); i++) {
		vec3 v = verts[startVert + i];

		if (v.x > targetModel->nMaxs.x) targetModel->nMaxs.x = v.x;
		if (v.y > targetModel->nMaxs.y) targetModel->nMaxs.y = v.y;
		if (v.z > targetModel->nMaxs.z) targetModel->nMaxs.z = v.z;

		if (v.x < targetModel->nMins.x) targetModel->nMins.x = v.x;
		if (v.y < targetModel->nMins.y) targetModel->nMins.y = v.y;
		if (v.z < targetModel->nMins.z) targetModel->nMins.z = v.z;
	}
}

int Bsp::create_clipnode_box(vec3 mins, vec3 maxs, BSPMODEL* targetModel, int targetHull, bool skipEmpty) {
	vector<BSPPLANE> addPlanes;
	vector<BSPCLIPNODE> addNodes;
	int solidNodeIdx = 0;

	for (int i = 1; i < MAX_MAP_HULLS; i++) {
		if (skipEmpty && targetModel->iHeadnodes[i] < 0) {
			continue;
		}
		if (targetHull > 0 && i != targetHull) {
			continue;
		}

		vec3 min = mins - default_hull_extents[i];
		vec3 max = maxs + default_hull_extents[i];

		int clipnodeIdx = clipnodeCount + addNodes.size();
		int planeIdx = planeCount + addPlanes.size();

		addPlanes.push_back({ vec3(1, 0, 0), min.x, PLANE_X }); // left
		addPlanes.push_back({ vec3(1, 0, 0), max.x, PLANE_X }); // right
		addPlanes.push_back({ vec3(0, 1, 0), min.y, PLANE_Y }); // front
		addPlanes.push_back({ vec3(0, 1, 0), max.y, PLANE_Y }); // back
		addPlanes.push_back({ vec3(0, 0, 1), min.z, PLANE_Z }); // bottom
		addPlanes.push_back({ vec3(0, 0, 1), max.z, PLANE_Z }); // top

		targetModel->iHeadnodes[i] = clipnodeCount + addNodes.size();

		for (int k = 0; k < 6; k++) {
			BSPCLIPNODE node;
			node.iPlane = planeIdx++;

			int insideContents = k == 5 ? CONTENTS_SOLID : clipnodeIdx+1;

			if (insideContents == CONTENTS_SOLID)
				solidNodeIdx = clipnodeIdx;

			clipnodeIdx++;

			// can't have negative normals on planes so children are swapped instead
			if (k % 2 == 0) {
				node.iChildren[0] = insideContents;
				node.iChildren[1] = CONTENTS_EMPTY;
			}
			else {
				node.iChildren[0] = CONTENTS_EMPTY;
				node.iChildren[1] = insideContents;
			}

			addNodes.push_back(node);
		}
	}

	BSPPLANE* newPlanes = new BSPPLANE[planeCount + addPlanes.size()];
	memcpy(newPlanes, planes, planeCount * sizeof(BSPPLANE));
	memcpy(newPlanes + planeCount, &addPlanes[0], addPlanes.size() * sizeof(BSPPLANE));
	replace_lump(LUMP_PLANES, newPlanes, (planeCount + addPlanes.size()) * sizeof(BSPPLANE));

	BSPCLIPNODE* newClipnodes = new BSPCLIPNODE[clipnodeCount + addNodes.size()];
	memcpy(newClipnodes, clipnodes, clipnodeCount * sizeof(BSPCLIPNODE));
	memcpy(newClipnodes + clipnodeCount, &addNodes[0], addNodes.size() * sizeof(BSPCLIPNODE));
	replace_lump(LUMP_CLIPNODES, newClipnodes, (clipnodeCount + addNodes.size()) * sizeof(BSPCLIPNODE));

	return solidNodeIdx;
}

void Bsp::simplify_model_collision(int modelIdx, int hullIdx) {
	if (modelIdx < 0 || modelIdx >= modelCount) {
		logf("Invalid model index %d. Must be 0-%d\n", modelIdx);
		return;
	}
	if (hullIdx >= MAX_MAP_HULLS) {
		logf("Invalid hull number. Valid hull numbers are 1-%d\n", MAX_MAP_HULLS);
		return;
	}

	BSPMODEL& model = models[modelIdx];

	if (model.iHeadnodes[1] < 0 && model.iHeadnodes[2] < 0 && model.iHeadnodes[3] < 0) {
		logf("Model has no clipnode hulls left to simplify\n");
		return;
	}

	if (hullIdx > 0 && model.iHeadnodes[hullIdx] < 0) {
		logf("Hull %d has no clipnodes\n", hullIdx);
		return;
	}

	if (model.iHeadnodes[0] < 0) {
		logf("Hull 0 was deleted from this model. Can't simplify.\n");
		// TODO: create verts from plane intersections
		return;
	}

	vec3 vertMin(9e9, 9e9, 9e9);
	vec3 vertMax(-9e9, -9e9, -9e9);
	get_model_vertex_bounds(modelIdx, vertMin, vertMax);

	create_clipnode_box(vertMin, vertMax, &model, hullIdx, true);
}

int Bsp::create_clipnode() {
	BSPCLIPNODE* newNodes = new BSPCLIPNODE[clipnodeCount + 1];
	memcpy(newNodes, clipnodes, clipnodeCount * sizeof(BSPCLIPNODE));

	BSPCLIPNODE* newNode = &newNodes[clipnodeCount];
	memset(newNode, 0, sizeof(BSPCLIPNODE));

	replace_lump(LUMP_CLIPNODES, newNodes, (clipnodeCount + 1) * sizeof(BSPCLIPNODE));

	return clipnodeCount-1;
}

int Bsp::create_plane() {
	BSPPLANE* newPlanes = new BSPPLANE[planeCount + 1];
	memcpy(newPlanes, planes, planeCount * sizeof(BSPPLANE));

	BSPPLANE& newPlane = newPlanes[planeCount];
	memset(&newPlane, 0, sizeof(BSPPLANE));

	replace_lump(LUMP_PLANES, newPlanes, (planeCount + 1) * sizeof(BSPPLANE));

	return planeCount - 1;
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

int16 Bsp::regenerate_clipnodes(int iNode, int hullIdx) {
	BSPNODE& node = nodes[iNode];

	switch (planes[node.iPlane].nType) {
		case PLANE_X: case PLANE_Y: case PLANE_Z: {
			// Skip this node. Bounding box clipnodes should have already been generated.
			// Only works for convex models.
			int childContents[2] = { 0, 0 };
			for (int i = 0; i < 2; i++) {
				if (node.iChildren[i] < 0) {
					BSPLEAF& leaf = leaves[~node.iChildren[i]];
					childContents[i] = leaf.nContents;
				}
			}

			int solidChild = childContents[0] == CONTENTS_EMPTY ? node.iChildren[1] : node.iChildren[0];
			int solidContents = childContents[0] == CONTENTS_EMPTY ? childContents[1] : childContents[0];

			if (solidChild < 0) {
				if (solidContents != CONTENTS_SOLID) {
					logf("UNEXPECTED SOLID CONTENTS %d\n", solidContents);
				}
				return CONTENTS_SOLID; // solid leaf
			}
			return regenerate_clipnodes(solidChild, hullIdx);
		}
		default:
			break;
	}

	int oldCount = clipnodeCount;
	int newClipnodeIdx = create_clipnode();
	clipnodes[newClipnodeIdx].iPlane = create_plane();

	int solidChild = -1;
	for (int i = 0; i < 2; i++) {
		if (node.iChildren[i] >= 0) {
			int childIdx = regenerate_clipnodes(node.iChildren[i], hullIdx);
			clipnodes[newClipnodeIdx].iChildren[i] = childIdx;
			solidChild = solidChild == -1 ? i : -1;			
		}
		else {
			BSPLEAF& leaf = leaves[~node.iChildren[i]];
			clipnodes[newClipnodeIdx].iChildren[i] = leaf.nContents;
			if (leaf.nContents == CONTENTS_SOLID) {
				solidChild = i;
			}
		}
	}

	BSPPLANE& nodePlane = planes[node.iPlane];
	BSPPLANE& clipnodePlane = planes[clipnodes[newClipnodeIdx].iPlane];
	clipnodePlane = nodePlane;

	// TODO: pretty sure this isn't right. Angled stuff probably lerps between the hull dimensions
	float extent = 0;
	switch (clipnodePlane.nType) {
	case PLANE_X: case PLANE_ANYX: extent = default_hull_extents[hullIdx].x; break;
	case PLANE_Y: case PLANE_ANYY: extent = default_hull_extents[hullIdx].y; break;
	case PLANE_Z: case PLANE_ANYZ: extent = default_hull_extents[hullIdx].z; break;
	}

	// TODO: this won't work for concave solids. The node's face could be used to determine which
	// direction the plane should be extended but not all nodes will have faces. Also wouldn't be
	// enough to "link" clipnode planes to node planes during scaling because BSP trees might not match.
	if (solidChild != -1) {
		BSPPLANE& p = planes[clipnodes[newClipnodeIdx].iPlane];
		vec3 planePoint = p.vNormal * p.fDist;
		vec3 newPlanePoint = planePoint + p.vNormal * (solidChild == 0 ? -extent : extent);
		p.fDist = dotProduct(p.vNormal, newPlanePoint) / dotProduct(p.vNormal, p.vNormal);
	}

	return newClipnodeIdx;
}

void Bsp::regenerate_clipnodes(int modelIdx) {
	BSPMODEL& model = models[modelIdx];	

	for (int i = 1; i < MAX_MAP_HULLS; i++) {
		// first create a bounding box for the model. For some reason this is needed to prevent
		// planes from extended farther than they should. All clip types do this.
		int solidNodeIdx = create_clipnode_box(model.nMins, model.nMaxs, &model, i, false); // fills in the headnode
		
		for (int k = 0; k < 2; k++) {
			if (clipnodes[solidNodeIdx].iChildren[k] == CONTENTS_SOLID) {
				clipnodes[solidNodeIdx].iChildren[k] = regenerate_clipnodes(model.iHeadnodes[0], i);
			}
		}

		// TODO: create clipnodes to "cap" edges that are 90+ degrees (most CSG clip types do this)
		// that will fix broken collision around those edges (invisible solid areas)
	}
}

void Bsp::dump_lightmap(int faceIdx, string outputPath) {
	int faceCount = header.lump[LUMP_FACES].nLength / sizeof(BSPFACE);

	BSPFACE& face = faces[faceIdx];

	qrad_init_globals(this);

	int mins[2];
	int extents[2];
	GetFaceExtents(faceIdx, mins, extents);

	int lightmapSz = extents[0] * extents[1];

	lodepng_encode24_file(outputPath.c_str(), (byte*)lightdata + face.nLightmapOffset, extents[0], extents[1]);
}

void Bsp::dump_lightmap_atlas(string outputPath) {
	int lightmapWidth = MAX_SURFACE_EXTENT;

	int lightmapsPerDim = ceil(sqrt(faceCount));
	int atlasDim = lightmapsPerDim * lightmapWidth;
	int sz = atlasDim * atlasDim;
	logf("ATLAS SIZE %d x %d (%.2f KB)", lightmapsPerDim, lightmapsPerDim, (sz * sizeof(COLOR3))/1024.0f);

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

void Bsp::write_csg_polys(int16_t nodeIdx, FILE* polyfile, int flipPlaneSkip, bool debug) {
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

			bool flipped = (z == 1 || face.nPlaneSide) && !(z == 1 && face.nPlaneSide);

			int iPlane = !flipped ? face.iPlane : face.iPlane + flipPlaneSkip;

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

void Bsp::print_contents(int contents) {
	switch (contents) {
	case CONTENTS_EMPTY:
		logf("EMPTY"); break;
	case CONTENTS_SOLID:
		logf("SOLID"); break;
	case CONTENTS_WATER:
		logf("WATER"); break;
	case CONTENTS_SLIME:
		logf("SLIME"); break;
	case CONTENTS_LAVA:
		logf("LAVA"); break;
	case CONTENTS_SKY:
		logf("SKY"); break;
	case CONTENTS_ORIGIN:
		logf("ORIGIN"); break;
	case CONTENTS_CURRENT_0:
		logf("CURRENT_0"); break;
	case CONTENTS_CURRENT_90:
		logf("CURRENT_90"); break;
	case CONTENTS_CURRENT_180:
		logf("CURRENT_180"); break;
	case CONTENTS_CURRENT_270:
		logf("CURRENT_270"); break;
	case CONTENTS_CURRENT_UP:
		logf("CURRENT_UP"); break;
	case CONTENTS_CURRENT_DOWN:
		logf("CURRENT_DOWN"); break;
	case CONTENTS_TRANSLUCENT:
		logf("TRANSLUCENT"); break;
	default:
		logf("UNKNOWN"); break;
	}
}

void Bsp::print_leaf(BSPLEAF leaf) {
	print_contents(leaf.nContents);
	logf(" %d surfs, Min(%d, %d, %d), Max(%d %d %d)", leaf.nMarkSurfaces, 
		leaf.nMins[0], leaf.nMins[1], leaf.nMins[2],
		leaf.nMaxs[0], leaf.nMaxs[1], leaf.nMaxs[2]);
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
	marksurfs = (uint16*)lumps[LUMP_MARKSURFACES];
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
	marksurfCount = header.lump[LUMP_MARKSURFACES].nLength / sizeof(uint16_t);
	surfedgeCount = header.lump[LUMP_SURFEDGES].nLength / sizeof(int32_t);
	edgeCount = header.lump[LUMP_EDGES].nLength / sizeof(BSPEDGE);
	textureCount = *((int32_t*)(lumps[LUMP_TEXTURES]));
	lightDataLength = header.lump[LUMP_LIGHTING].nLength;
	visDataLength = header.lump[LUMP_VISIBILITY].nLength;
}

void Bsp::replace_lump(int lumpIdx, void* newData, int newLength) {
	delete[] lumps[lumpIdx];
	lumps[lumpIdx] = (byte*)newData;
	header.lump[lumpIdx].nLength = newLength;

	update_lump_pointers();
}
