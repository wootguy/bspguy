#include "Bsp.h"
#include "util.h"
#include "lodepng.h"
#include "vis.h"
#include "Entity.h"
#include "colors.h"
#include "globals.h"
#include <sstream>
#include <set>
#include <map>
#include <fstream>
#include <algorithm>
#include "Clipper.h"
#include <float.h>
#include "Wad.h"


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

	load_ents();
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
	if (lumps) {
		for (int i = 0; i < HEADER_LUMPS; i++)
			if (lumps[i])
				delete[] lumps[i];
		delete[] lumps;
	}

	for (int i = 0; i < ents.size(); i++)
		delete ents[i];

	if (pvsFaces) {
		delete[] pvsFaces;
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

void Bsp::get_model_vertex_bounds(int modelIdx, vec3& mins, vec3& maxs) {
	mins = vec3(FLT_MAX, FLT_MAX, FLT_MAX);
	maxs = vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);

	BSPMODEL& model = models[modelIdx];

	for (int i = 0; i < model.nFaces; i++) {
		BSPFACE& face = faces[model.iFirstFace + i];

		for (int e = 0; e < face.nEdges; e++) {
			int32_t edgeIdx = surfedges[face.iFirstEdge + e];
			BSPEDGE& edge = edges[abs(edgeIdx)];
			int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];

			expandBoundingBox(verts[vertIdx], mins, maxs);
		}
	}

	if (!model.nFaces) {
		// use the clipping hull "faces" instead
		Clipper clipper;
		vector<NodeVolumeCuts> solidNodes = get_model_leaf_volume_cuts(modelIdx, 0, CONTENTS_SOLID);

		vector<CMesh> solidMeshes;
		for (int k = 0; k < solidNodes.size(); k++) {
			solidMeshes.push_back(clipper.clip(solidNodes[k].cuts));
		}

		for (int m = 0; m < solidMeshes.size(); m++) {
			CMesh& mesh = solidMeshes[m];

			for (int i = 0; i < mesh.faces.size(); i++) {

				if (!mesh.faces[i].visible) {
					continue;
				}

				set<int> uniqueFaceVerts;

				for (int k = 0; k < mesh.faces[i].edges.size(); k++) {
					for (int v = 0; v < 2; v++) {
						int vertIdx = mesh.edges[mesh.faces[i].edges[k]].verts[v];
						if (!mesh.verts[vertIdx].visible) {
							continue;
						}
						expandBoundingBox(mesh.verts[vertIdx].pos, mins, maxs);
					}
				}
			}
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

vector<NodeVolumeCuts> Bsp::get_model_leaf_volume_cuts(int modelIdx, int hullIdx, int16_t contents) {
	vector<NodeVolumeCuts> modelVolumeCuts;

	if (hullIdx >= 0 && hullIdx < MAX_MAP_HULLS)
	{
		int nodeIdx = models[modelIdx].iHeadnodes[hullIdx];
		bool is_valid_node = false;
		
		if (hullIdx == 0) {
			is_valid_node = nodeIdx >= 0 && nodeIdx < nodeCount;
		}
		else {
			is_valid_node = nodeIdx >= 0 && nodeIdx < clipnodeCount;
		}

		if (nodeIdx >= 0 && is_valid_node) {
			vector<BSPPLANE> clipOrder;
			if (hullIdx == 0) {
				get_node_leaf_cuts(nodeIdx, clipOrder, modelVolumeCuts, contents);
			}
			else {
				get_clipnode_leaf_cuts(nodeIdx, clipOrder, modelVolumeCuts, contents);
			}
		}
	}
	return modelVolumeCuts;
}

void Bsp::get_clipnode_leaf_cuts(int iNode, vector<BSPPLANE>& clipOrder, vector<NodeVolumeCuts>& output, int16_t contents) {
	BSPCLIPNODE& node = clipnodes[iNode];

	if (node.iPlane < 0) {
		return;
	}

	for (int i = 0; i < 2; i++) {
		BSPPLANE plane = planes[node.iPlane];
		if (i != 0) {
			plane.vNormal = plane.vNormal.invert();
			plane.fDist = -plane.fDist;
		}
		clipOrder.push_back(plane);

		if (node.iChildren[i] >= 0) {
			get_clipnode_leaf_cuts(node.iChildren[i], clipOrder, output, contents);
		}
		else if (node.iChildren[i] == contents) {
			NodeVolumeCuts nodeVolumeCuts;
			nodeVolumeCuts.nodeIdx = iNode;

			// reverse order of branched planes = order of cuts to the world which define this node's volume
			// https://qph.fs.quoracdn.net/main-qimg-2a8faad60cc9d437b58a6e215e6e874d
			for (int k = clipOrder.size() - 1; k >= 0; k--) {
				nodeVolumeCuts.cuts.push_back(clipOrder[k]);
			}

			output.push_back(nodeVolumeCuts);
		}

		clipOrder.pop_back();
	}
}

void Bsp::get_node_leaf_cuts(int iNode, vector<BSPPLANE>& clipOrder, vector<NodeVolumeCuts>& output, int16_t contents) {
	BSPNODE& node = nodes[iNode];

	for (int i = 0; i < 2; i++) {
		BSPPLANE plane = planes[node.iPlane];
		if (i != 0) {
			plane.vNormal = plane.vNormal.invert();
			plane.fDist = -plane.fDist;
		}
		clipOrder.push_back(plane);

		if (node.iChildren[i] >= 0) {
			get_node_leaf_cuts(node.iChildren[i], clipOrder, output, contents);
		}
		else if (leaves[~node.iChildren[i]].nContents == contents) {
			NodeVolumeCuts nodeVolumeCuts;
			nodeVolumeCuts.nodeIdx = iNode;

			// reverse order of branched planes = order of cuts to the world which define this node's volume
			// https://qph.fs.quoracdn.net/main-qimg-2a8faad60cc9d437b58a6e215e6e874d
			for (int k = clipOrder.size() - 1; k >= 0; k--) {
				nodeVolumeCuts.cuts.push_back(clipOrder[k]);
			}

			output.push_back(nodeVolumeCuts);
		}

		clipOrder.pop_back();
	}
}

bool Bsp::is_convex(int modelIdx) {
	return models[modelIdx].iHeadnodes[0] >= 0 && is_node_hull_convex(models[modelIdx].iHeadnodes[0]);
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

bool Bsp::isInteriorFace(const Polygon3D& poly, int hull) {
	int headnode = models[0].iHeadnodes[hull];
	vec3 testPos = poly.center + poly.plane_z * 0.5f;
	return pointContents(headnode, testPos, hull) == CONTENTS_EMPTY;
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

bool Bsp::vertex_manipulation_sync(int modelIdx, vector<TransformVert>& hullVerts, bool convexCheckOnly, bool regenClipnodes) {
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
					node.iChildren[1] = temp;
				}
			}
		}
	}

	//logf("UPDATED %d planes\n", planeUpdates);

	BSPMODEL& model = models[modelIdx];
	getBoundingBox(allVertPos, model.nMins, model.nMaxs);

	if (!regenClipnodes)
		return true;

	regenerate_clipnodes(modelIdx, -1);

	return true;
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
				ori = parseVector(ents[i]->keyvalues["origin"]);
			}
			ori += offset;

			ents[i]->setOrAddKeyvalue("origin", ori.toKeyvalueString());

			if (ents[i]->hasKey("spawnorigin")) {
				vec3 spawnori = parseVector(ents[i]->keyvalues["spawnorigin"]);

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
			continue;
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
	int newLightDataSz = 0;
	int totalLightmaps = 0;
	int lightmapsResizeCount = 0;
	for (int i = 0; i < faceCount; i++) {
		BSPFACE& face = faces[i];

		g_progress.tick();

		if (lightmap_count(i) == 0)
			continue;

		BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];
		int32_t texOffset = ((int32_t*)textures)[info.iMiptex + 1];
		BSPMIPTEX& tex = *((BSPMIPTEX*)(textures + texOffset));

		int size[2];
		GetFaceLightmapSize(this, i, size);

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
		//logf("%d lightmap(s) to resize\n", lightmapsResizeCount);

		g_progress.update("Resize lightmaps", faceCount);

		int newColorCount = newLightDataSz / sizeof(COLOR3);
		COLOR3* newLightData = new COLOR3[newColorCount];
		memset(newLightData, 255, newColorCount * sizeof(COLOR3));
		int lightmapOffset = 0;

		for (int i = 0; i < faceCount; i++) {
			BSPFACE& face = faces[i];

			g_progress.tick();

			if (lightmap_count(i) == 0) // no lighting
				continue;

			LIGHTMAP& oldLight = oldLightmaps[i];
			LIGHTMAP& newLight = newLightmaps[i];
			int oldLayerSz = (oldLight.width * oldLight.height) * sizeof(COLOR3);
			int newLayerSz = (newLight.width * newLight.height) * sizeof(COLOR3);
			int oldSz = oldLayerSz * oldLight.layers;
			int newSz = newLayerSz * newLight.layers;

			totalLightmaps++;

			bool faceMoved = oldLightmaps[i].luxelFlags != NULL;
			bool lightmapResized = oldLight.width != newLight.width || oldLight.height != newLight.height;

			if (!faceMoved || !lightmapResized) {
				memcpy((byte*)newLightData + lightmapOffset, (byte*)lightdata + face.nLightmapOffset, oldSz);
				newLight.luxelFlags = NULL;
			}
			else {
				newLight.luxelFlags = new byte[newLight.width * newLight.height];
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

void Bsp::split_shared_model_structures(int modelIdx) {
	// marks which structures should not be moved
	STRUCTUSAGE shouldMove(this);
	STRUCTUSAGE shouldNotMove(this);

	g_progress.update("Split model structures", modelCount);

	mark_model_structures(modelIdx, &shouldMove, modelIdx == 0);
	for (int i = 0; i < modelCount; i++) {
		if (i != modelIdx)
			mark_model_structures(i, &shouldNotMove, false);

		g_progress.tick();
	}

	STRUCTREMAP remappedStuff(this);

	// TODO: handle all of these, assuming it's possible these are ever shared
	for (int i = 1; i < shouldNotMove.count.leaves; i++) { // skip solid leaf - it doesn't matter
		if (shouldMove.leaves[i] && shouldNotMove.leaves[i]) {
			logf("\nWarning: leaf shared with multiple models. Something might break.\n");
			break;
		}
	}
	for (int i = 0; i < shouldNotMove.count.nodes; i++) {
		if (shouldMove.nodes[i] && shouldNotMove.nodes[i]) {
			logf("\nError: node shared with multiple models. Something will break.\n");
			break;
		}
	}
	for (int i = 0; i < shouldNotMove.count.verts; i++) {
		if (shouldMove.verts[i] && shouldNotMove.verts[i]) {
			// this happens on activist series but doesn't break anything
			logf("\nError: vertex shared with multiple models. Something will break.\n");
			break;
		}
	}

	int duplicatePlanes = 0;
	int duplicateClipnodes = 0;
	int duplicateTexinfos = 0;

	for (int i = 0; i < shouldNotMove.count.planes; i++) {
		duplicatePlanes += shouldMove.planes[i] && shouldNotMove.planes[i];
	}
	for (int i = 0; i < shouldNotMove.count.clipnodes; i++) {
		duplicateClipnodes += shouldMove.clipnodes[i] && shouldNotMove.clipnodes[i];
	}
	for (int i = 0; i < shouldNotMove.count.texInfos; i++) {
		duplicateTexinfos += shouldMove.texInfo[i] && shouldNotMove.texInfo[i];
	}

	int newPlaneCount = planeCount + duplicatePlanes;
	int newClipnodeCount = clipnodeCount + duplicateClipnodes;
	int newTexinfoCount = texinfoCount + duplicateTexinfos;

	BSPPLANE* newPlanes = new BSPPLANE[newPlaneCount];
	memcpy(newPlanes, planes, planeCount * sizeof(BSPPLANE));

	BSPCLIPNODE* newClipnodes = new BSPCLIPNODE[newClipnodeCount];
	memcpy(newClipnodes, clipnodes, clipnodeCount * sizeof(BSPCLIPNODE));

	BSPTEXTUREINFO* newTexinfos = new BSPTEXTUREINFO[newTexinfoCount];
	memcpy(newTexinfos, texinfos, newTexinfoCount * sizeof(BSPTEXTUREINFO));

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

	addIdx = texinfoCount;
	for (int i = 0; i < shouldNotMove.count.texInfos; i++) {
		if (shouldMove.texInfo[i] && shouldNotMove.texInfo[i]) {
			newTexinfos[addIdx] = texinfos[i];
			remappedStuff.texInfo[i] = addIdx;
			addIdx++;
		}
	}

	replace_lump(LUMP_PLANES, newPlanes, newPlaneCount * sizeof(BSPPLANE));
	replace_lump(LUMP_CLIPNODES, newClipnodes, newClipnodeCount * sizeof(BSPCLIPNODE));
	replace_lump(LUMP_TEXINFO, newTexinfos, newTexinfoCount * sizeof(BSPTEXTUREINFO));

	bool* newVisitedClipnodes = new bool[newClipnodeCount];
	memset(newVisitedClipnodes, 0, newClipnodeCount);
	delete[] remappedStuff.visitedClipnodes;
	remappedStuff.visitedClipnodes = newVisitedClipnodes;

	remap_model_structures(modelIdx, &remappedStuff);

	if (duplicatePlanes || duplicateClipnodes || duplicateTexinfos) {
		debugf("\nShared model structures were duplicated to allow independent movement:\n");
		if (duplicatePlanes)
			debugf("    Added %d planes\n", duplicatePlanes);
		if (duplicateClipnodes)
			debugf("    Added %d clipnodes\n", duplicateClipnodes);
		if (duplicateTexinfos)
			debugf("    Added %d texinfos\n", duplicateTexinfos);
	}
}

bool Bsp::does_model_use_shared_structures(int modelIdx) {
	STRUCTUSAGE shouldMove(this);
	STRUCTUSAGE shouldNotMove(this);

	for (int i = 0; i < modelCount; i++) {
		if (i == modelIdx)
			mark_model_structures(i, &shouldMove, true);
		else
			mark_model_structures(i, &shouldNotMove, false);
	}

	for (int i = 0; i < planeCount; i++) {
		if (shouldMove.planes[i] && shouldNotMove.planes[i]) {
			return true;
		}
	}
	for (int i = 0; i < clipnodeCount; i++) {
		if (shouldMove.clipnodes[i] && shouldNotMove.clipnodes[i]) {
			return true;
		}
	}
	return false;
}

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

int Bsp::delete_embedded_textures() {
	uint headerSz = (textureCount+1) * sizeof(int32_t);
	uint newTexDataSize = headerSz + (textureCount * sizeof(BSPMIPTEX));
	byte* newTextureData = new byte[newTexDataSize];
	
	BSPMIPTEX* mips = (BSPMIPTEX*)(newTextureData + headerSz);
	
	int32_t* header = (int32_t*)newTextureData;
	*header = textureCount;
	header++;

	int numRemoved = 0;

	for (int i = 0; i < textureCount; i++) {
		int32_t oldOffset = ((int32_t*)textures)[i + 1];
		BSPMIPTEX* oldTex = (BSPMIPTEX*)(textures + oldOffset);

		if (oldTex->nOffsets[0] != -1) {
			numRemoved++;
		}

		header[i] = headerSz + i*sizeof(BSPMIPTEX);
		mips[i].nWidth = oldTex->nWidth;
		mips[i].nHeight = oldTex->nHeight;
		memcpy(mips[i].szName, oldTex->szName, MAXTEXTURENAME);
		memset(mips[i].nOffsets, 0, MIPLEVELS*sizeof(int32_t));
	}

	replace_lump(LUMP_TEXTURES, newTextureData, newTexDataSize);

	return numRemoved;
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
			load_ents();
		}
	}

	update_lump_pointers();
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

			if (offset == -1) {
				removeSize += sizeof(int32_t);
			}
			else {
				removeSize += getBspTextureSize(tex) + sizeof(int32_t);
			}
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

		if (oldOffset == -1) {
			texHeader[k + 1] = -1;
		}
		else {
			BSPMIPTEX* tex = (BSPMIPTEX*)(textures + oldOffset);
			int sz = getBspTextureSize(tex);

			memcpy(newTexData + newOffset, textures + oldOffset, sz);
			texHeader[k + 1] = newOffset;
			newOffset += sz;
		}

		remappedIndexes[i] = k;
		k++;
	}

	replace_lump(LUMP_TEXTURES, newTexData, header.lump[LUMP_TEXTURES].nLength - removeSize);

	return removeCount;
}

int Bsp::remove_unused_lightmaps(bool* usedFaces) {
	int oldLightdataSize = lightDataLength;

	int* lightmapSizes = new int[faceCount];

	int newLightDataSize = 0;
	for (int i = 0; i < faceCount; i++) {
		if (usedFaces[i]) {
			lightmapSizes[i] = GetFaceLightmapSizeBytes(this, i);
			newLightDataSize += lightmapSizes[i];
		}
	}

	byte* newColorData = new byte[newLightDataSize];

	int offset = 0;
	for (int i = 0; i < faceCount; i++) {
		BSPFACE& face = faces[i];

		if (usedFaces[i] && ((int64)face.nLightmapOffset + lightmapSizes[i]) <= (int64)lightDataLength) {
			memcpy(newColorData + offset, lightdata + face.nLightmapOffset, lightmapSizes[i]);
			face.nLightmapOffset = offset;
			offset += lightmapSizes[i];
		}
	}

	delete[] lightmapSizes;

	replace_lump(LUMP_LIGHTING, newColorData, newLightDataSize);

	return oldLightdataSize - newLightDataSize;
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
		logf("ERROR: New vis row size larger than old size. VIS will likely be broken\n");
	}

	int* oldLeafs = new int[newVisLeafCount];

	for (int i = 1; i < newVisLeafCount; i++) {
		int oldLeafIdx = 0;

		for (int k = 1; k < oldVisLeafCount; k++) {
			if (remap->leaves[k] == i) {
				oldLeafs[i] = k;
				break;
			}
		}
	}

	for (int i = 1; i < newVisLeafCount; i++) {
		byte* oldVisRow = oldDecompressedVis + (oldLeafs[i] - 1) * oldVisRowSize;
		byte* newVisRow = newDecompressedVis + (i - 1) * newVisRowSize;

		for (int k = 1; k < newVisLeafCount; k++) {
			int oldLeafIdx = oldLeafs[k] - 1;
			int oldByteOffset = oldLeafIdx / 8;
			int oldBitOffset = 1 << (oldLeafIdx % 8);

			if (oldVisRow[oldByteOffset] & oldBitOffset) {
				int newLeafIdx = k-1;
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

STRUCTCOUNT Bsp::remove_unused_model_structures() {
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

	// reversed so models can be deleted without shifting the next delete index
 	for (int i = modelCount-1; i >= 0; i--) { 
		if (!usedModels[i]) {
			delete_model(i);
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
				int16_t leafIdx = ~nodes[i].iChildren[k];
				nodes[i].iChildren[k] = ~((int16_t)remap.leaves[leafIdx]);
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
	}

	for (int i = 0; i < modelCount; i++) {
		if (models[i].nFaces > 0)
			models[i].iFirstFace = remap.faces[models[i].iFirstFace];
		else
			models[i].iFirstFace = 0;
		if (models[i].iHeadnodes[0] >= 0)
			models[i].iHeadnodes[0] = remap.nodes[models[i].iHeadnodes[0]];
		for (int k = 1; k < MAX_MAP_HULLS; k++) {
			if (models[i].iHeadnodes[k] >= 0)
				models[i].iHeadnodes[k] = remap.clipnodes[models[i].iHeadnodes[k]];
		}
	}

	models[0].nVisLeafs = 0;
	count_leaves(models[0].iHeadnodes[0], models[0].nVisLeafs);
	//models[0].nVisLeafs = leafCount;

	if (visDataLength)
		removeCount.visdata = remove_unused_visdata(&remap, (BSPLEAF*)oldLeaves, 
			usedStructures.count.leaves, oldVisLeafCount);

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
			debugf("Deleting unused model %d\n", i);

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
		entsThatNeverNeedAnyHulls.insert("func_tankcontrols");
		entsThatNeverNeedAnyHulls.insert("func_traincontrols");
		entsThatNeverNeedAnyHulls.insert("func_vehiclecontrols");
		//entsThatNeverNeedAnyHulls.insert("trigger_autosave"); // obsolete in sven
		//entsThatNeverNeedAnyHulls.insert("trigger_endsection"); // obsolete in sven

		set<string> entsThatNeverNeedCollision;
		entsThatNeverNeedCollision.insert("func_illusionary");
		entsThatNeverNeedCollision.insert("func_mortar_field");

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
		bool needsPlayerHulls = false; // HULL 1 + HULL 3
		bool needsMonsterHulls = false; // All HULLs
		bool needsVisibleHull = false; // HULL 0
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
				if (conditionalPointEntTriggers.find(cname) != conditionalPointEntTriggers.end()) {
					needsVisibleHull = spawnflags & 8; // "Everything else" flag checked
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
				needsVisibleHull = true;
			}
			else if (cname == "func_rotating") {
				needsPlayerHulls = needsMonsterHulls = !(spawnflags & 64); // "Not solid" unchecked
				needsVisibleHull = true;
			}
			else if (cname == "func_ladder") {
				needsPlayerHulls = true;
				needsVisibleHull = true;
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

		if (!needsVisibleHull && !needsMonsterHulls) {
			if (models[i].iHeadnodes[0] >= 0)
				debugf("Deleting HULL 0 from model %d, used in %s\n", i, uses.c_str());

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

			if (deletedAnyHulls)
				debugf("Deleting HULL 1-3 from model %d, used in %s\n", i, uses.c_str());

			model.iHeadnodes[1] = -1;
			model.iHeadnodes[2] = -1;
			model.iHeadnodes[3] = -1;
		}
		else if (!needsMonsterHulls) {
			if (models[i].iHeadnodes[2] >= 0)
				debugf("Deleting HULL 2 from model %d, used in %s\n", i, uses.c_str());

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

void Bsp::delete_oob_nodes(int iNode, int16_t* parentBranch, vector<BSPPLANE>& clipOrder, int oobFlags, 
	bool* oobHistory, bool isFirstPass, int& removedNodes) {
	BSPNODE& node = nodes[iNode];
	float oob_coord = g_limits.max_mapboundary;

	if (node.iPlane < 0) {
		return;
	}

	bool isoob = isFirstPass ? true : oobHistory[iNode];

	for (int i = 0; i < 2; i++) {
		BSPPLANE plane = planes[node.iPlane];
		if (i != 0) {
			plane.vNormal = plane.vNormal.invert();
			plane.fDist = -plane.fDist;
		}
		clipOrder.push_back(plane);

		if (node.iChildren[i] >= 0) {
			delete_oob_nodes(node.iChildren[i], &node.iChildren[i], clipOrder, oobFlags, oobHistory, 
				isFirstPass, removedNodes);
			if (node.iChildren[i] >= 0) {
				isoob = false; // children weren't empty, so this node isn't empty either
			}
		}
		else if (isFirstPass) {
			vector<BSPPLANE> cuts;
			for (int k = clipOrder.size() - 1; k >= 0; k--) {
				cuts.push_back(clipOrder[k]);
			}

			Clipper clipper;
			CMesh nodeVolume = clipper.clip(cuts);

			for (int k = 0; k < nodeVolume.verts.size(); k++) {
				if (!nodeVolume.verts[k].visible)
					continue;
				vec3 v = nodeVolume.verts[k].pos;

				bool oobx0 = (oobFlags & OOB_CLIP_X) ? (v.x > oob_coord) : false;
				bool oobx1 = (oobFlags & OOB_CLIP_X_NEG) ? (v.x < -oob_coord) : false;
				bool ooby0 = (oobFlags & OOB_CLIP_Y) ? (v.y > oob_coord) : false;
				bool ooby1 = (oobFlags & OOB_CLIP_Y_NEG) ? (v.y < -oob_coord) : false;
				bool oobz0 = (oobFlags & OOB_CLIP_Z) ? (v.z > oob_coord) : false;
				bool oobz1 = (oobFlags & OOB_CLIP_Z_NEG) ? (v.z < -oob_coord) : false;

				if (!oobx0 && !ooby0 && !oobz0 && !oobx1 && !ooby1 && !oobz1) {
					isoob = false; // node can't be empty if both children aren't oob
				}
			}
		}

		clipOrder.pop_back();
	}

	if (isFirstPass) {
		// only check if each node is ever considered in bounds, after considering all branches.
		// don't remove anything until the entire tree has been scanned

		if (!isoob) {
			oobHistory[iNode] = false;
		}
	}
	else if (parentBranch && isoob) {
		// we know which nodes are OOB now, so it's safe to unlink this node from the paranet
		*parentBranch = CONTENTS_SOLID;
		removedNodes++;
	}
}

void Bsp::delete_oob_clipnodes(int iNode, int16_t* parentBranch, vector<BSPPLANE>& clipOrder, int oobFlags, 
	bool* oobHistory, bool isFirstPass, int& removedNodes)  {
	BSPCLIPNODE& node = clipnodes[iNode];
	float oob_coord = g_limits.max_mapboundary;

	if (node.iPlane < 0) {
		return;
	}

	bool isoob = isFirstPass ? true : oobHistory[iNode];

	for (int i = 0; i < 2; i++) {
		BSPPLANE plane = planes[node.iPlane];
		if (i != 0) {
			plane.vNormal = plane.vNormal.invert();
			plane.fDist = -plane.fDist;
		}
		clipOrder.push_back(plane);

		if (node.iChildren[i] >= 0) {
			delete_oob_clipnodes(node.iChildren[i], &node.iChildren[i], clipOrder, oobFlags, 
				oobHistory, isFirstPass, removedNodes);
			if (node.iChildren[i] >= 0) {
				isoob = false; // children weren't empty, so this node isn't empty either
			}
		}
		else if (isFirstPass) {
			vector<BSPPLANE> cuts;
			for (int k = clipOrder.size() - 1; k >= 0; k--) {
				cuts.push_back(clipOrder[k]);
			}
			
			Clipper clipper;
			CMesh nodeVolume = clipper.clip(cuts);

			vec3 mins(FLT_MAX, FLT_MAX, FLT_MAX);
			vec3 maxs(-FLT_MAX, -FLT_MAX, -FLT_MAX);

			for (int k = 0; k < nodeVolume.verts.size(); k++) {
				if (!nodeVolume.verts[k].visible)
					continue;
				vec3 v = nodeVolume.verts[k].pos;
				
				expandBoundingBox(v, mins, maxs);
			}

			bool oobx0 = (oobFlags & OOB_CLIP_X) ? (mins.x > oob_coord) : false;
			bool oobx1 = (oobFlags & OOB_CLIP_X_NEG) ? (maxs.x < -oob_coord) : false;
			bool ooby0 = (oobFlags & OOB_CLIP_Y) ? (mins.y > oob_coord) : false;
			bool ooby1 = (oobFlags & OOB_CLIP_Y_NEG) ? (maxs.y < -oob_coord) : false;
			bool oobz0 = (oobFlags & OOB_CLIP_Z) ? (mins.z > oob_coord) : false;
			bool oobz1 = (oobFlags & OOB_CLIP_Z_NEG) ? (maxs.z < -oob_coord) : false;

			if (!oobx0 && !ooby0 && !oobz0 && !oobx1 && !ooby1 && !oobz1) {
				isoob = false; // node can't be empty if both children aren't oob
			}
		}

		clipOrder.pop_back();
	}

	// clipnodes are reused in the BSP tree. Some paths to the same node involve more plane intersections
	// than others. So, there will be some paths where the node is considered OOB and others not. If it
	// was EVER considered to be within bounds, on any branch, then don't let be stripped. Otherwise you
	// end up with broken clipnodes that are expanded too much because a deeper branch was deleted and
	// so there are fewer clipping planes to define the volume. This then then leads to players getting
	// stuck on shit and unable to escape when touching that region.

	if (isFirstPass) {
		// only check if each node is ever considered in bounds, after considering all branches.
		// don't remove anything until the entire tree has been scanned

		if (!isoob) {
			oobHistory[iNode] = false;
		}
	}
	else if (parentBranch && isoob) {
		// we know which nodes are OOB now, so it's safe to unlink this node from the paranet
		*parentBranch = CONTENTS_SOLID;
		removedNodes++;
	}
}

void Bsp::delete_oob_data(int clipFlags) {
	float oob_coord = g_limits.max_mapboundary;
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
				uint16_t faceIdx = marksurfs[leaf.iFirstMarkSurface + k];

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


void Bsp::delete_box_nodes(int iNode, int16_t* parentBranch, vector<BSPPLANE>& clipOrder,
	vec3 clipMins, vec3 clipMaxs, bool* oobHistory, bool isFirstPass, int& removedNodes) {
	BSPNODE& node = nodes[iNode];
	float oob_coord = g_limits.max_mapboundary;

	if (node.iPlane < 0) {
		return;
	}

	bool isoob = isFirstPass ? true : oobHistory[iNode];

	for (int i = 0; i < 2; i++) {
		BSPPLANE plane = planes[node.iPlane];
		if (i != 0) {
			plane.vNormal = plane.vNormal.invert();
			plane.fDist = -plane.fDist;
		}
		clipOrder.push_back(plane);

		if (node.iChildren[i] >= 0) {
			delete_box_nodes(node.iChildren[i], &node.iChildren[i], clipOrder, clipMins, clipMaxs, 
				oobHistory, isFirstPass, removedNodes);
			if (node.iChildren[i] >= 0) {
				isoob = false; // children weren't empty, so this node isn't empty either
			}
		}
		else if (isFirstPass) {
			vector<BSPPLANE> cuts;
			for (int k = clipOrder.size() - 1; k >= 0; k--) {
				cuts.push_back(clipOrder[k]);
			}

			Clipper clipper;
			CMesh nodeVolume = clipper.clip(cuts);

			for (int k = 0; k < nodeVolume.verts.size(); k++) {
				if (!nodeVolume.verts[k].visible)
					continue;
				vec3 v = nodeVolume.verts[k].pos;

				if (!pointInBox(v, clipMins, clipMaxs)) {
					isoob = false; // node can't be empty if both children aren't oob
				}
			}
		}

		clipOrder.pop_back();
	}

	if (isFirstPass) {
		// only check if each node is ever considered in bounds, after considering all branches.
		// don't remove anything until the entire tree has been scanned

		if (!isoob) {
			oobHistory[iNode] = false;
		}
	}
	else if (parentBranch && isoob) {
		// we know which nodes are OOB now, so it's safe to unlink this node from the paranet
		*parentBranch = CONTENTS_SOLID;
		removedNodes++;
	}
}

void Bsp::delete_box_clipnodes(int iNode, int16_t* parentBranch, vector<BSPPLANE>& clipOrder,
	vec3 clipMins, vec3 clipMaxs, bool* oobHistory, bool isFirstPass, int& removedNodes) {
	BSPCLIPNODE& node = clipnodes[iNode];
	float oob_coord = g_limits.max_mapboundary;

	if (node.iPlane < 0) {
		return;
	}

	bool isoob = isFirstPass ? true : oobHistory[iNode];

	for (int i = 0; i < 2; i++) {
		BSPPLANE plane = planes[node.iPlane];
		if (i != 0) {
			plane.vNormal = plane.vNormal.invert();
			plane.fDist = -plane.fDist;
		}
		clipOrder.push_back(plane);

		if (node.iChildren[i] >= 0) {
			delete_box_clipnodes(node.iChildren[i], &node.iChildren[i], clipOrder, clipMins, clipMaxs,
				oobHistory, isFirstPass, removedNodes);
			if (node.iChildren[i] >= 0) {
				isoob = false; // children weren't empty, so this node isn't empty either
			}
		}
		else if (isFirstPass) {
			vector<BSPPLANE> cuts;
			for (int k = clipOrder.size() - 1; k >= 0; k--) {
				cuts.push_back(clipOrder[k]);
			}

			Clipper clipper;
			CMesh nodeVolume = clipper.clip(cuts);

			vec3 mins(FLT_MAX, FLT_MAX, FLT_MAX);
			vec3 maxs(-FLT_MAX, -FLT_MAX, -FLT_MAX);

			for (int k = 0; k < nodeVolume.verts.size(); k++) {
				if (!nodeVolume.verts[k].visible)
					continue;
				vec3 v = nodeVolume.verts[k].pos;

				expandBoundingBox(v, mins, maxs);
			}

			if (!boxesIntersect(mins, maxs, clipMins, clipMaxs)) {
				isoob = false; // node can't be empty if both children aren't in the clip box
			}
		}

		clipOrder.pop_back();
	}

	if (isFirstPass) {
		// only check if each node is ever considered in bounds, after considering all branches.
		// don't remove anything until the entire tree has been scanned

		if (!isoob) {
			oobHistory[iNode] = false;
		}
	}
	else if (parentBranch && isoob) {
		// we know which nodes are OOB now, so it's safe to unlink this node from the paranet
		*parentBranch = CONTENTS_SOLID;
		removedNodes++;
	}
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
			bool isCullEnt = ents[i]->hasKey("classname") && ents[i]->keyvalues["classname"] == "cull";
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
				uint16_t faceIdx = marksurfs[leaf.iFirstMarkSurface + k];

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

void Bsp::count_leaves(int iNode, int& leafCount) {
	BSPNODE& node = nodes[iNode];

	for (int i = 0; i < 2; i++) {
		if (node.iChildren[i] >= 0) {
			count_leaves(node.iChildren[i], leafCount);
		}
		else {
			int16_t leafIdx = ~node.iChildren[i];
			if (leafIdx > leafCount)
				leafCount = leafIdx;
		}
	}
}

struct CompareVert {
	vec3 pos;
	float u, v;
};

struct ModelIdxRemap {
	int newIdx;
	vec3 offset;
};

void Bsp::deduplicate_models() {
	const float epsilon = 1.0f;

	map<int, ModelIdxRemap> modelRemap;

	for (int i = 1; i < modelCount; i++) {
		BSPMODEL& modelA = models[i];

		if (modelA.nFaces == 0)
			continue;

		if (modelRemap.find(i) != modelRemap.end()) {
			continue;
		}

		bool shouldCompareTextures = false;
		string modelKeyA = "*" + to_string(i);

		for (Entity* ent : ents) {
			if (ent->hasKey("model") && ent->keyvalues["model"] == modelKeyA) {
				if (ent->isEverVisible()) {
					shouldCompareTextures = true;
					break;
				}
			}
		}

		for (int k = 1; k < modelCount; k++) {
			if (i == k)
				continue;

			BSPMODEL& modelB = models[k];

			if (modelA.nFaces != modelB.nFaces)
				continue;

			vec3 minsA, maxsA, minsB, maxsB;
			get_model_vertex_bounds(i, minsA, maxsA);
			get_model_vertex_bounds(k, minsB, maxsB);

			vec3 sizeA = maxsA - minsA;
			vec3 sizeB = maxsB - minsB;

			if ((sizeB - sizeA).length() > epsilon) {
				continue;
			}

			if (!shouldCompareTextures) {
				string modelKeyB = "*" + to_string(k);

				for (Entity* ent : ents) {
					if (ent->hasKey("model") && ent->keyvalues["model"] == modelKeyB) {
						if (ent->isEverVisible()) {
							shouldCompareTextures = true;
							break;
						}
					}
				}
			}

			bool similarFaces = true;
			for (int fa = 0; fa < modelA.nFaces; fa++) {
				BSPFACE& faceA = faces[modelA.iFirstFace + fa];
				BSPTEXTUREINFO& infoA = texinfos[faceA.iTextureInfo];
				BSPPLANE& planeA = planes[faceA.iPlane];
				int32_t texOffset = ((int32_t*)textures)[infoA.iMiptex + 1];
				BSPMIPTEX& tex = *((BSPMIPTEX*)(textures + texOffset));
				float tw = 1.0f / (float)tex.nWidth;
				float th = 1.0f / (float)tex.nHeight;

				vector<CompareVert> vertsA;
				for (int e = 0; e < faceA.nEdges; e++) {
					int32_t edgeIdx = surfedges[faceA.iFirstEdge + e];
					BSPEDGE& edge = edges[abs(edgeIdx)];
					int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];

					CompareVert v;
					v.pos = verts[vertIdx];

					float fU = dotProduct(infoA.vS, v.pos) + infoA.shiftS;
					float fV = dotProduct(infoA.vT, v.pos) + infoA.shiftT;
					v.u = fU * tw;
					v.v = fV * th;

					// wrap coords
					v.u = v.u > 0 ? (v.u - (int)v.u) : 1.0f - (v.u - (int)v.u);
					v.v = v.v > 0 ? (v.v - (int)v.v) : 1.0f - (v.v - (int)v.v);

					vertsA.push_back(v);
					//logf("A Face %d vert %d uv: %.2f %.2f\n", fa, e, v.u, v.v);
				}

				bool foundMatch = false;
				for (int fb = 0; fb < modelB.nFaces; fb++) {
					BSPFACE& faceB = faces[modelB.iFirstFace + fb];
					BSPTEXTUREINFO& infoB = texinfos[faceB.iTextureInfo];
					BSPPLANE& planeB = planes[faceB.iPlane];

					if ((!shouldCompareTextures || infoA.iMiptex == infoB.iMiptex)
						&& planeA.vNormal == planeB.vNormal
						&& faceA.nPlaneSide == faceB.nPlaneSide) {
						// face planes and textures match
						// now check if vertices have same relative positions and texture coords

						vector<CompareVert> vertsB;
						for (int e = 0; e < faceB.nEdges; e++) {
							int32_t edgeIdx = surfedges[faceB.iFirstEdge + e];
							BSPEDGE& edge = edges[abs(edgeIdx)];
							int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];

							CompareVert v;
							v.pos = verts[vertIdx];

							float fU = dotProduct(infoB.vS, v.pos) + infoB.shiftS;
							float fV = dotProduct(infoB.vT, v.pos) + infoB.shiftT;
							v.u = fU * tw;
							v.v = fV * th;

							// wrap coords
							v.u = v.u > 0 ? (v.u - (int)v.u) : 1.0f - (v.u - (int)v.u);
							v.v = v.v > 0 ? (v.v - (int)v.v) : 1.0f - (v.v - (int)v.v);

							vertsB.push_back(v);
							//logf("B Face %d vert %d uv: %.2f %.2f\n", fb, e, v.u, v.v);
						}

						bool vertsMatch = true;
						for (CompareVert& vertA : vertsA) {
							bool foundVertMatch = false;

							for (CompareVert& vertB : vertsB) {
								
								float diffU = fabs(vertA.u - vertB.u);
								float diffV = fabs(vertA.v - vertB.v);
								const float uvEpsilon = 0.005f;

								bool uvsMatch = !shouldCompareTextures ||
									((diffU < uvEpsilon || fabs(diffU - 1.0f) < uvEpsilon)
									&& (diffV < uvEpsilon || fabs(diffV - 1.0f) < uvEpsilon));

								if (((vertA.pos - minsA) - (vertB.pos - minsB)).length() < epsilon
									&& uvsMatch) {
									foundVertMatch = true;
									break;
								}
							}

							if (!foundVertMatch) {
								vertsMatch = false;
								break;
							}
						}

						if (vertsMatch) {
							foundMatch = true;
							break;
						}
					}
				}

				if (!foundMatch) {
					similarFaces = false;
					break;
				}
			}

			if (!similarFaces)
				continue;			

			//logf("Model %d and %d seem very similar (%d faces)\n", i, k, modelA.nFaces);
			ModelIdxRemap remap;
			remap.newIdx = i;
			remap.offset = minsB - minsA;
			modelRemap[k] = remap;
		}
	}
	
	logf("Remapped %d BSP model references\n", modelRemap.size());

	for (Entity* ent : ents) {
		if (!ent->keyvalues.count("model")) {
			continue;
		}

		string model = ent->keyvalues["model"];

		if (model[0] != '*')
			continue;

		int modelIdx = atoi(model.substr(1).c_str());

		if (modelRemap.find(modelIdx) != modelRemap.end()) {
			ModelIdxRemap remap = modelRemap[modelIdx];

			ent->setOrAddKeyvalue("origin", (ent->getOrigin() + remap.offset).toKeyvalueString());
			ent->setOrAddKeyvalue("model", "*" + to_string(remap.newIdx));
		}
	}
}

float Bsp::calc_allocblock_usage() {
	int total = 0;

	for (int i = 0; i < faceCount; i++) {
		int size[2];
		GetFaceLightmapSize(this, i, size);

		total += size[0] * size[1];
	}

	const int allocBlockSize = 128 * 128;

	return total / (float)allocBlockSize;
}

void Bsp::allocblock_reduction() {
	int scaleCount = 0;

	for (int i = 1; i < modelCount; i++) {
		BSPMODEL& model = models[i];

		if (model.nFaces == 0)
			continue;

		bool isVisibleModel = false;
		string modelKey = "*" + to_string(i);

		for (Entity* ent : ents) {
			if (ent->hasKey("model") && ent->keyvalues["model"] == modelKey) {
				if (ent->isEverVisible()) {
					isVisibleModel = true;
					break;
				}
			}
		}

		if (isVisibleModel)
			continue;
		for (int fa = 0; fa < model.nFaces; fa++) {
			BSPFACE& face = faces[model.iFirstFace + fa];
			BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];
			info.vS = info.vS.normalize(0.01f);
			info.vT = info.vT.normalize(0.01f);
		}

		scaleCount++;
		logf("Scale up model %d\n", i);
	}

	logf("Scaled up textures on %d invisible models\n", scaleCount);
}

bool Bsp::subdivide_face(int faceIdx) {
	BSPFACE& face = faces[faceIdx];
	BSPPLANE& plane = planes[face.iPlane];
	BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];

	vector<vec3> faceVerts;
	for (int e = 0; e < face.nEdges; e++) {
		int32_t edgeIdx = surfedges[face.iFirstEdge + e];
		BSPEDGE& edge = edges[abs(edgeIdx)];
		int vertIdx = edgeIdx >= 0 ? edge.iVertex[0] : edge.iVertex[1];

		faceVerts.push_back(verts[vertIdx]);
	}

	Polygon3D poly(faceVerts);

	vec3 minVertU, maxVertU;
	vec3 minVertV, maxVertV;

	float minU = FLT_MAX;
	float maxU = -FLT_MAX;
	float minV = FLT_MAX;
	float maxV = -FLT_MAX;
	for (int i = 0; i < faceVerts.size(); i++) {
		vec3& pos = faceVerts[i];

		float u = dotProduct(info.vS, pos);
		float v = dotProduct(info.vT, pos);

		if (u < minU) {
			minU = u;
			minVertU = pos;
		}
		if (u > maxU) {
			maxU = u;
			maxVertU = pos;
		}
		if (v < minV) {
			minV = v;
			minVertV = pos;
		}
		if (v > maxV) {
			maxV = v;
			maxVertV = pos;
		}
	}
	vec2 axisU = poly.project(info.vS).normalize();
	vec2 axisV = poly.project(info.vT).normalize();

	vec2 midVertU = poly.project(minVertU + (maxVertU - minVertU) * 0.5f);
	vec2 midVertV = poly.project(minVertV + (maxVertV - minVertV) * 0.5f);

	Line2D ucut(midVertU + axisV * 1000.0f, midVertU + axisV * -1000.0f);
	Line2D vcut(midVertV + axisU * 1000.0f, midVertV + axisU * -1000.0f);

	int size[2];
	GetFaceLightmapSize(this, faceIdx, size);

	Line2D& cutLine = size[0] > size[1] ? ucut : vcut;

	vector<vector<vec3>> polys = poly.cut(cutLine);

	if (polys.empty()) {
		int32_t texOffset = ((int32_t*)textures)[info.iMiptex + 1];
		BSPMIPTEX& tex = *((BSPMIPTEX*)(textures + texOffset));
		vec3 center = get_face_center(faceIdx);
		logf("Failed to subdivide face %d %s (%d %d %d)\n", faceIdx, tex.szName,
			(int)center.x, (int)center.y, (int)center.z);
		return false;
	}
	
	int addVerts = polys[0].size() + polys[1].size();

	BSPFACE* newFaces = new BSPFACE[faceCount + 1];
	memcpy(newFaces, faces, faceIdx * sizeof(BSPFACE));
	memcpy(newFaces + faceIdx + 1, faces + faceIdx, (faceCount - faceIdx) * sizeof(BSPFACE));

	int addMarks = 0;
	for (int i = 0; i < marksurfCount; i++) {
		if (marksurfs[i] == faceIdx) {
			addMarks++;
		}
	}
	int totalMarks = marksurfCount + addMarks;
	uint16_t* newMarkSurfs = new uint16_t[totalMarks];
	memcpy(newMarkSurfs, marksurfs, marksurfCount * sizeof(uint16_t));

	BSPEDGE* newEdges = new BSPEDGE[edgeCount + addVerts];
	memcpy(newEdges, edges, edgeCount * sizeof(BSPEDGE));

	vec3* newVerts = new vec3[vertCount + addVerts];
	memcpy(newVerts, verts, vertCount * sizeof(vec3));

	int32_t* newSurfEdges = new int32_t[surfedgeCount + addVerts];
	memcpy(newSurfEdges, surfedges, surfedgeCount * sizeof(int32_t));

	int oldSurfBegin = face.iFirstEdge;
	int oldSurfEnd = face.iFirstEdge + face.nEdges;

	BSPEDGE* edgePtr = newEdges + edgeCount;
	vec3* vertPtr = newVerts + vertCount;
	int32_t* surfedgePtr = newSurfEdges + surfedgeCount;

	for (int k = 0; k < 2; k++) {
		vector<vec3>& cutPoly = polys[k];

		newFaces[faceIdx + k] = faces[faceIdx];
		newFaces[faceIdx + k].iFirstEdge = surfedgePtr - newSurfEdges;
		newFaces[faceIdx + k].nEdges = cutPoly.size();

		int vertOffset = vertPtr - newVerts;
		int edgeOffset = edgePtr - newEdges;

		for (int i = 0; i < cutPoly.size(); i++) {
			edgePtr->iVertex[0] = vertOffset + i;
			edgePtr->iVertex[1] = vertOffset + ((i + 1) % cutPoly.size());
			edgePtr++;

			*vertPtr++ = cutPoly[i];

			// TODO: make fewer edges and make use of both vertexes?
			*surfedgePtr++ = -(edgeOffset + i);
		}
	}

	for (int i = 0; i < modelCount; i++) {
		BSPMODEL& model = models[i];

		if (model.iFirstFace > faceIdx) {
			model.iFirstFace += 1;
		}
		else if (model.iFirstFace <= faceIdx && model.iFirstFace + model.nFaces > faceIdx) {
			model.nFaces++;
		}
	}

	for (int i = 0; i < nodeCount; i++) {
		BSPNODE& node = nodes[i];

		if (node.firstFace > faceIdx) {
			node.firstFace += 1;
		}
		else if (node.firstFace <= faceIdx && node.firstFace + node.nFaces > faceIdx) {
			node.nFaces++;
		}
	}

	for (int i = 0; i < totalMarks; i++) {
		if (newMarkSurfs[i] == faceIdx) {
			memmove(newMarkSurfs + i + 1, newMarkSurfs + i, (totalMarks - (i+1)) * sizeof(uint16_t));
			newMarkSurfs[i + 1] = faceIdx + 1;

			for (int k = 0; k < leafCount; k++) {
				BSPLEAF& leaf = leaves[k];

				if (!leaf.nMarkSurfaces)
					continue;
				else if (leaf.iFirstMarkSurface > i) {
					leaf.iFirstMarkSurface += 1;
				}
				else if (leaf.iFirstMarkSurface <= i && leaf.iFirstMarkSurface + leaf.nMarkSurfaces > i) {
					//logf("Added mark %d/%d to leaf %d (%d + %d)\n", i, marksurfCount, k, leaf.iFirstMarkSurface, leaf.nMarkSurfaces);
					leaf.nMarkSurfaces += 1;
				}
			}

			i++; // skip the other side of the subdivided face, or else it triggers the next block
		}
		else if (newMarkSurfs[i] > faceIdx) {
			newMarkSurfs[i]++;
		}
	}

	replace_lump(LUMP_MARKSURFACES, newMarkSurfs, totalMarks * sizeof(uint16_t));
	replace_lump(LUMP_FACES, newFaces, (faceCount + 1)*sizeof(BSPFACE));
	replace_lump(LUMP_EDGES, newEdges, (edgeCount + addVerts)*sizeof(BSPEDGE));
	replace_lump(LUMP_SURFEDGES, newSurfEdges, (surfedgeCount + addVerts) * sizeof(int32_t));
	replace_lump(LUMP_VERTICES, newVerts, (vertCount + addVerts) * sizeof(vec3));

	return true;
}

void Bsp::fix_bad_surface_extents_with_subdivide(int faceIdx) {
	vector<int> faces;
	faces.push_back(faceIdx);

	int totalFaces = 1;

	while (faces.size()) {
		int size[2];
		int i = faces[faces.size()-1];
		if (GetFaceLightmapSize(this, i, size)) {
			faces.pop_back();
			continue;
		}

		// adjust face indexes if about to split a face with a lower index 
		for (int i = 0; i < faces.size(); i++) {
			if (faces[i] > i) {
				faces[i]++;
			}
		}

		totalFaces++;
		subdivide_face(i);
		faces.push_back(i+1);
		faces.push_back(i);
	}

	logf("Subdivided into %d faces\n", totalFaces);
}

void Bsp::fix_bad_surface_extents(bool scaleNotSubdivide, bool downscaleOnly, int maxTextureDim) {
	int numSub = 0;
	int numScale = 0;
	int numShrink = 0;
	bool anySubdivides = true;

	if (scaleNotSubdivide) {
		// create unique texinfos in case any are shared with both good and bad faces
		for (int fa = 0; fa < faceCount; fa++) {
			int faceIdx = fa;
			BSPFACE& face = faces[faceIdx];
			BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];

			if (info.nFlags & TEX_SPECIAL) {
				continue;
			}

			int size[2];
			if (GetFaceLightmapSize(this, faceIdx, size)) {
				continue;
			}

			get_unique_texinfo(faceIdx);
		}
	}

	while (anySubdivides) {
		anySubdivides = false;
		for (int fa = 0; fa < faceCount; fa++) {
			int faceIdx = fa;
			BSPFACE& face = faces[faceIdx];
			BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];

			if (info.nFlags & TEX_SPECIAL) {
				continue;
			}

			int size[2];
			if (GetFaceLightmapSize(this, faceIdx, size)) {
				continue;
			}

			if (maxTextureDim > 0 && downscale_texture(info.iMiptex, maxTextureDim, false)) {
				// retry after downscaling
				numShrink++;
				fa--;
				continue;
			}

			if (downscaleOnly) {
				continue;
			}

			if (!scaleNotSubdivide) {
				if (subdivide_face(faceIdx)) {
					numSub++;
					anySubdivides = true;
					break;
				}
				// else scale the face because it was too skinny to be subdivided or something
			}
				
			vec2 oldScale(1.0f / info.vS.length(), 1.0f / info.vT.length());

			bool scaledOk = false;
			for (int i = 0; i < 128; i++) {
				info.vS *= 0.5f;
				info.vT *= 0.5f;

				if (GetFaceLightmapSize(this, faceIdx, size)) {
					scaledOk = true;
					break;
				}
			}

			if (!scaledOk) {
				int32_t texOffset = ((int32_t*)textures)[info.iMiptex + 1];
				BSPMIPTEX& tex = *((BSPMIPTEX*)(textures + texOffset));
				logf("Failed to fix face %s with scales %f %f\n", tex.szName, oldScale.x, oldScale.y);
			}
			else {
				int32_t texOffset = ((int32_t*)textures)[info.iMiptex + 1];
				BSPMIPTEX& tex = *((BSPMIPTEX*)(textures + texOffset));
				vec2 newScale(1.0f / info.vS.length(), 1.0f / info.vT.length());

				vec3 center = get_face_center(faceIdx);
				logf("Scaled up %s from %fx%f -> %fx%f (%d %d %d)\n",
					tex.szName, oldScale.x, oldScale.y, newScale.x, newScale.y,
					(int)center.x, (int)center.y, (int)center.z);
				numScale++;
			}
		}
	}

	if (numScale) {
		logf("Scaled up %d face textures\n", numScale);
	}
	if (numSub) {
		logf("Subdivided %d faces\n", numSub);
	}
	if (numShrink) {
		logf("Downscaled %d textures\n", numShrink);
	}
}

vec3 Bsp::get_face_center(int faceIdx) {
	BSPFACE& face = faces[faceIdx];

	vec3 centroid;

	for (int k = 0; k < face.nEdges; k++) {
		int32_t edgeIdx = surfedges[face.iFirstEdge + k];
		BSPEDGE& edge = edges[abs(edgeIdx)];
		int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];
		centroid += verts[vertIdx];
	}
	
	return centroid / (float)face.nEdges;
}

bool Bsp::downscale_texture(int textureId, int newWidth, int newHeight) {
	if ((newWidth % 16 != 0) || (newHeight % 16 != 0) || newWidth <= 0 || newHeight <= 0) {
		logf("Invalid downscale dimensions: %dx%d\n", newWidth, newHeight);
		return false;
	}

	int32_t texOffset = ((int32_t*)textures)[textureId + 1];
	BSPMIPTEX& tex = *((BSPMIPTEX*)(textures + texOffset));

	int oldWidth = tex.nWidth;
	int oldHeight = tex.nHeight;

	tex.nWidth = newWidth;
	tex.nHeight = newHeight;

	int lastMipSize = (oldWidth >> 3) * (oldHeight >> 3);
	byte* pixels = (byte*)(textures + texOffset + tex.nOffsets[0]);
	byte* palette = (byte*)(textures + texOffset + tex.nOffsets[3] + lastMipSize);

	int oldWidths[4];
	int oldHeights[4];
	int newWidths[4];
	int newHeights[4];
	int newOffset[4];
	for (int i = 0; i < 4; i++) {
		oldWidths[i] = oldWidth >> (1 * i);
		oldHeights[i] = oldHeight >> (1 * i);
		newWidths[i] = tex.nWidth >> (1 * i);
		newHeights[i] = tex.nHeight >> (1 * i);

		if (i > 0) {
			newOffset[i] = newOffset[i - 1] + newWidths[i - 1] * newHeights[i - 1];
		}
		else {
			newOffset[i] = sizeof(BSPMIPTEX);
		}
	}
	byte* newPalette = (byte*)(textures + texOffset + newOffset[3] + newWidths[3] * newHeights[3]);

	float srcScaleX = (float)oldWidth / tex.nWidth;
	float srcScaleY = (float)oldHeight / tex.nHeight;

	for (int i = 0; i < 4; i++) {
		byte* srcData = (byte*)(textures + texOffset + tex.nOffsets[i]);
		byte* dstData = (byte*)(textures + texOffset + newOffset[i]);
		int srcWidth = oldWidths[i];
		int dstWidth = newWidths[i];

		for (int y = 0; y < newHeights[i]; y++) {
			int srcY = (int)(srcScaleY * y + 0.5f);

			for (int x = 0; x < newWidths[i]; x++) {
				int srcX = (int)(srcScaleX * x + 0.5f);

				dstData[y * dstWidth + x] = srcData[srcY * srcWidth + srcX];
			}
		}
	}
	// 2 = palette color count (should always be 256)
	memcpy(newPalette, palette, 256 * sizeof(COLOR3) + 2);

	for (int i = 0; i < 4; i++) {
		tex.nOffsets[i] = newOffset[i];
	}

	adjust_resized_texture_coordinates(textureId, oldWidth, oldHeight);

	// shrink texture lump
	int removedBytes = palette - newPalette;
	byte* texEnd = newPalette + 256 * sizeof(COLOR3);
	int shiftBytes = (texEnd - textures) + removedBytes;

	memcpy(texEnd, texEnd + removedBytes, header.lump[LUMP_TEXTURES].nLength - shiftBytes);
	for (int k = textureId + 1; k < textureCount; k++) {
		((int32_t*)textures)[k + 1] -= removedBytes;
	}

	for (int i = 0; i < textureCount; i++) {
		int32_t texOffset = ((int32_t*)textures)[i + 1];
		BSPMIPTEX& tex = *((BSPMIPTEX*)(textures + texOffset));
	}

	logf("Downscale %s %dx%d -> %dx%d\n", tex.szName, oldWidth, oldHeight, tex.nWidth, tex.nHeight);

	return true;
}

bool Bsp::downscale_texture(int textureId, int maxDim, bool allowWad) {
	int32_t texOffset = ((int32_t*)textures)[textureId + 1];
	BSPMIPTEX& tex = *((BSPMIPTEX*)(textures + texOffset));

	int oldWidth = tex.nWidth;
	int oldHeight = tex.nHeight;
	int newWidth = tex.nWidth;
	int newHeight = tex.nHeight;

	if (tex.nWidth > maxDim && tex.nWidth > tex.nHeight) {
		float ratio = oldHeight / (float)oldWidth;
		newWidth = maxDim;
		newHeight = (int)(((newWidth * ratio) + 8) / 16) * 16;
		if (newHeight > oldHeight) {
			newHeight = (int)((newWidth * ratio) / 16) * 16;
		}
	}
	else if (tex.nHeight > maxDim) {
		float ratio = oldWidth / (float)oldHeight;
		newHeight = maxDim;
		newWidth = (int)(((newHeight * ratio) + 8) / 16) * 16;
		if (newWidth > oldWidth) {
			newWidth = (int)((newHeight * ratio) / 16) * 16;
		}
	}
	else {
		return false; // no need to downscale
	}

	if (oldWidth == newWidth && oldHeight == newHeight) {
		logf("Failed to downscale texture %s %dx%d to max dim %d\n", tex.szName, oldWidth, oldHeight, maxDim);
		return false;
	}

	if (tex.nOffsets[0] == 0) {
		if (allowWad) {
			tex.nWidth = newWidth;
			tex.nHeight = newHeight;
			adjust_resized_texture_coordinates(textureId, oldWidth, oldHeight);
			logf("Texture coords were updated for %s. The WAD texture must be updated separately.\n", tex.szName);
		}
		else {
			logf("Can't downscale WAD texture %s\n", tex.szName);
		}
		
		return false;
	}

	return downscale_texture(textureId, newWidth, newHeight);
}

vector<Wad*> Bsp::load_wads(bool verbosePrinting) {
	vector<string> wadNames = get_wad_names();
	vector<Wad*> wads;

	vector<string> tryPaths = {
		"./"
	};

	tryPaths.insert(tryPaths.end(), g_settings.resPaths.begin(), g_settings.resPaths.end());

	for (int i = 0; i < wadNames.size(); i++) {
		string path;
		for (int k = 0; k < tryPaths.size(); k++) {
			string tryPath = tryPaths[k] + wadNames[i];
			string tryPath_full = g_settings.gamedir + tryPaths[k] + wadNames[i];
			if (fileExists(tryPath)) {
				path = tryPath;
				break;
			}
			if (fileExists(tryPath_full)) {
				path = tryPath_full;
				break;
			}
		}

		if (path.empty()) {
			if (verbosePrinting)
				logf("Missing WAD: %s\n", wadNames[i].c_str());
			continue;
		}

		if (verbosePrinting)
			logf("Loading WAD %s\n", path.c_str());
		Wad* wad = new Wad(path);
		wad->readInfo();
		wads.push_back(wad);
	}

	return wads;
}

vector<string> Bsp::get_wad_names() {
	vector<string> wadNames;

	for (int i = 0; i < ents.size(); i++) {
		if (ents[i]->keyvalues["classname"] == "worldspawn") {
			wadNames = splitString(ents[i]->keyvalues["wad"], ";");

			for (int k = 0; k < wadNames.size(); k++) {
				wadNames[k] = basename(wadNames[k]);
			}
			break;
		}
	}

	return wadNames;
}

bool Bsp::embed_texture(int textureId) {
	int32_t texOffset = ((int32_t*)textures)[textureId + 1];
	BSPMIPTEX& tex = *((BSPMIPTEX*)(textures + texOffset));

	if (tex.nOffsets[0] != 0) {
		logf("Texture %s is already embedded\n", tex.szName);
		return false;
	}

	vector<Wad*> wads = load_wads(false);

	bool embedded = false;
	for (int k = 0; k < wads.size(); k++) {
		if (wads[k]->hasTexture(tex.szName)) {
			WADTEX* wadTex = wads[k]->readTexture(tex.szName);

			for (int i = 0; i < 4; i++) {
				tex.nOffsets[i] = wadTex->nOffsets[i];
			}
			if (tex.nHeight != wadTex->nHeight || tex.nWidth != wadTex->nWidth) {
				logf("Failed to embed texture %s from wad %s (dimensions don't match)\n", tex.szName, wads[k]->filename.c_str());
				delete wadTex;
				continue;
			}

			int sz = tex.nWidth * tex.nHeight;	   // miptex 0
			int sz2 = sz / 4;  // miptex 1
			int sz3 = sz2 / 4; // miptex 2
			int sz4 = sz3 / 4; // miptex 3
			int texDataSz = sz + sz2 + sz3 + sz4 + 2 + 256 * 3 + 2;
			int startOffset = texOffset + sizeof(BSPMIPTEX);
			int oldRemainder = header.lump[LUMP_TEXTURES].nLength - startOffset;

			for (int i = 0; i < textureCount; i++) {
				int32_t& offset = ((int32_t*)textures)[i+1];

				if (offset >= startOffset) {
					offset += texDataSz;
				}
			}

			byte* newTexData = new byte[header.lump[LUMP_TEXTURES].nLength + texDataSz];
			memcpy(newTexData, lumps[LUMP_TEXTURES], startOffset);
			memcpy(newTexData + startOffset, wadTex->data, texDataSz);
			memcpy(newTexData + startOffset + texDataSz, lumps[LUMP_TEXTURES] + startOffset, oldRemainder);

			logf("Embedded texture %s from wad %s\n", tex.szName, wads[k]->filename.c_str());

			delete wadTex;
			delete[] lumps[LUMP_TEXTURES];
			lumps[LUMP_TEXTURES] = newTexData;
			header.lump[LUMP_TEXTURES].nLength += texDataSz;
			update_lump_pointers();

			break;
		}
	}

	for (int i = 0; i < wads.size(); i++) {
		delete wads[i];
	}

	return embedded;
}

bool Bsp::unembed_texture(int textureId) {
	int32_t texOffset = ((int32_t*)textures)[textureId + 1];
	BSPMIPTEX& tex = *((BSPMIPTEX*)(textures + texOffset));

	if (tex.nOffsets[0] == 0) {
		logf("Texture %s is already unembedded\n", tex.szName);
		return false;
	}

	int sz = tex.nWidth * tex.nHeight;	   // miptex 0
	int sz2 = sz / 4;  // miptex 1
	int sz3 = sz2 / 4; // miptex 2
	int sz4 = sz3 / 4; // miptex 3
	int texDataSz = sz + sz2 + sz3 + sz4 + 2 + 256 * 3 + 2;
	int endOffset = texOffset + sizeof(BSPMIPTEX);
	int newTexBufferSz = header.lump[LUMP_TEXTURES].nLength - texDataSz;

	// reset texture dimensions in case it was edited inside the BSP
	vector<Wad*> wads = load_wads(false);
	bool isInWad = false;
	for (int k = 0; k < wads.size(); k++) {
		if (wads[k]->hasTexture(tex.szName)) {
			WADTEX* wadTex = wads[k]->readTexture(tex.szName);

			if (tex.nWidth != wadTex->nWidth || tex.nHeight != wadTex->nHeight) {
				int oldWidth = tex.nWidth;
				int oldHeight = tex.nHeight;
				tex.nWidth = wadTex->nWidth;
				tex.nHeight = wadTex->nHeight;
				adjust_resized_texture_coordinates(textureId, oldWidth, oldHeight);
				break;
			}
			isInWad = true;
			
			delete wadTex;
		}
	}
	for (int i = 0; i < wads.size(); i++) {
		delete wads[i];
	}
	if (!isInWad) {
		logf("Aborted unembed of %s. No WAD contains this texture. Data would be lost.\n", tex.szName);
		return false;
	}

	for (int i = 0; i < textureCount; i++) {
		int32_t& offset = ((int32_t*)textures)[i + 1];

		if (offset >= endOffset) {
			offset -= texDataSz;
		}
	}

	for (int i = 0; i < 4; i++) {
		tex.nOffsets[i] = 0;
	}

	byte* newTexData = new byte[newTexBufferSz];
	memcpy(newTexData, lumps[LUMP_TEXTURES], endOffset);
	memcpy(newTexData + endOffset, lumps[LUMP_TEXTURES] + endOffset + texDataSz, newTexBufferSz - endOffset);

	logf("Unembedded texture %s\n", tex.szName);
	delete[] lumps[LUMP_TEXTURES];
	lumps[LUMP_TEXTURES] = newTexData;
	header.lump[LUMP_TEXTURES].nLength -= texDataSz;
	update_lump_pointers();

	return true;
}

void Bsp::adjust_resized_texture_coordinates(int textureId, int oldWidth, int oldHeight) {
	int32_t texOffset = ((int32_t*)textures)[textureId + 1];
	BSPMIPTEX& tex = *((BSPMIPTEX*)(textures + texOffset));
	
	int newWidth = tex.nWidth;
	int newHeight = tex.nHeight;

	// scale up face texture coordinates
	float scaleX = newWidth / (float)oldWidth;
	float scaleY = newHeight / (float)oldHeight;

	for (int i = 0; i < faceCount; i++) {
		BSPFACE& face = faces[i];

		if (texinfos[face.iTextureInfo].iMiptex != textureId)
			continue;

		// each affected face should have a unique texinfo because
		// the shift amount may be different for every face after scaling
		BSPTEXTUREINFO* info = get_unique_texinfo(i);

		// get any vert on the face to use a reference point. Why?
		// When textures are scaled, the texture relative to the face will depend on how far away its
		// vertices are from the world origin. This means faces far away from the world origin shift many
		// pixels per scale unit, and faces aligned with the world origin don't shift at all when scaled.
		int32_t edgeIdx = surfedges[face.iFirstEdge];
		BSPEDGE& edge = edges[abs(edgeIdx)];
		int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];
		vec3 vert = verts[vertIdx];

		vec3 oldvs = info->vS;
		vec3 oldvt = info->vT;
		info->vS *= scaleX;
		info->vT *= scaleY;

		// get before/after uv coordinates
		float oldu = (dotProduct(oldvs, vert) + info->shiftS) * (1.0f / (float)oldWidth);
		float oldv = (dotProduct(oldvt, vert) + info->shiftT) * (1.0f / (float)oldHeight);
		float u = dotProduct(info->vS, vert) + info->shiftS;
		float v = dotProduct(info->vT, vert) + info->shiftT;

		// undo the shift in uv coordinates for this face
		info->shiftS += (oldu * newWidth) - u;
		info->shiftT += (oldv * newHeight) - v;
	}
}

void Bsp::downscale_invalid_textures() {
	int count = 0;

	for (int i = 0; i < textureCount; i++) {
		int32_t texOffset = ((int32_t*)textures)[i + 1];
		BSPMIPTEX& tex = *((BSPMIPTEX*)(textures + texOffset));

		if (tex.nWidth * tex.nHeight > g_limits.max_texturepixels) {
			embed_texture(i);
		}
	}

	for (int i = 0; i < textureCount; i++) {
		int32_t texOffset = ((int32_t*)textures)[i + 1];
		BSPMIPTEX& tex = *((BSPMIPTEX*)(textures + texOffset));

		if (tex.nWidth * tex.nHeight > g_limits.max_texturepixels) {
			if (tex.nOffsets[0] == 0) {
				logf("Skipping WAD texture %s (failed to embed)\n", tex.szName);
				continue;
			}

			int oldWidth = tex.nWidth;
			int oldHeight = tex.nHeight;
			int newWidth = tex.nWidth;
			int newHeight = tex.nHeight;

			float ratio = oldHeight / (float)oldWidth;

			while (newWidth > 16) {
				newWidth -= 16;
				newHeight = newWidth * ratio;

				if (newHeight % 16 != 0) {
					continue;
				}

				if (newWidth * newHeight <= g_limits.max_texturepixels) {
					break;
				}
			}

			downscale_texture(i, newWidth, newHeight);
			count++;
		}
	}

	logf("Downscaled %d textures\n", count);
}

bool Bsp::rename_texture(const char* oldName, const char* newName) {
	if (strlen(newName) > 16) {
		logf("ERROR: New texture name longer than 15 characters (%d)\n", strlen(newName));
		return false;
	}

	for (int i = 0; i < textureCount; i++) {
		int32_t texOffset = ((int32_t*)textures)[i + 1];
		BSPMIPTEX& tex = *((BSPMIPTEX*)(textures + texOffset));
		
		if (!strncmp(tex.szName, oldName, 16)) {
			strncpy(tex.szName, newName, 16);
			logf("Renamed texture '%s' -> '%s'\n", oldName, newName);
			return true;
		}
	}

	logf("No texture found with name '%s'\n", oldName);
	return false;
}

set<int> Bsp::selectConnectedTexture(int modelId, int faceId) {
	set<int> selected;
	const float epsilon = 1.0f;

	BSPMODEL& model = models[modelId];

	BSPFACE& face = faces[faceId];
	BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];
	BSPPLANE& plane = planes[face.iPlane];

	vector<vec3> selectedVerts;
	for (int e = 0; e < face.nEdges; e++) {
		int32_t edgeIdx = surfedges[face.iFirstEdge + e];
		BSPEDGE& edge = edges[abs(edgeIdx)];
		int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];
		selectedVerts.push_back(verts[vertIdx]);
	}

	bool anyNewFaces = true;
	while (anyNewFaces) {
		anyNewFaces = false;

		logf("Loop again!\n");
		for (int fa = 0; fa < model.nFaces; fa++) {
			int testFaceIdx = model.iFirstFace + fa;
			BSPFACE& faceA = faces[testFaceIdx];
			BSPTEXTUREINFO& infoA = texinfos[faceA.iTextureInfo];
			BSPPLANE& planeA = planes[faceA.iPlane];

			if (planeA.vNormal != plane.vNormal || info.iMiptex != infoA.iMiptex || selected.count(testFaceIdx)) {
				continue;
			}

			vector<vec3> uniqueVerts;
			bool isConnected = false;

			for (int e = 0; e < faceA.nEdges && !isConnected; e++) {
				int32_t edgeIdx = surfedges[faceA.iFirstEdge + e];
				BSPEDGE& edge = edges[abs(edgeIdx)];
				int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];

				bool isUnique = true;
				vec3 v2 = verts[vertIdx];
				for (vec3 v1 : selectedVerts) {
					if ((v1 - v2).length() < epsilon) {
						isConnected = true;
						break;
					}
				}
			}

			// shares an edge. Select this face
			if (isConnected) {
				for (int e = 0; e < faceA.nEdges; e++) {
					int32_t edgeIdx = surfedges[faceA.iFirstEdge + e];
					BSPEDGE& edge = edges[abs(edgeIdx)];
					int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];
					selectedVerts.push_back(verts[vertIdx]);
				}

				selected.insert(testFaceIdx);
				anyNewFaces = true;
				logf("SElect %d add %d\n", testFaceIdx, uniqueVerts.size());
			}
		}
	}
	
	return selected;
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

void Bsp::update_ent_lump(bool stripNodes) {
	stringstream ent_data;

	for (int i = 0; i < ents.size(); i++) {
		if (stripNodes) {
			string cname = ents[i]->keyvalues["classname"];
			if (cname == "info_node" || cname == "info_node_air") {
				continue;
			}
		}

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

int Bsp::lightmap_count(int faceIdx) {
	BSPFACE& face = faces[faceIdx];

	if (texinfos[face.iTextureInfo].nFlags & TEX_SPECIAL || face.nLightmapOffset >= lightDataLength)
		return 0;

	int lightmapCount = 0;
	for (int k = 0; k < 4; k++) {
		lightmapCount += face.nStyles[k] != 255;
	}

	return lightmapCount;
}

void Bsp::write(string path) {
	if (path.rfind(".bsp") != path.size() - 4) {
		path = path + ".bsp";
	}

	// calculate lump offsets
	int offset = sizeof(BSPHEADER);
	for (int i = 0; i < HEADER_LUMPS; i++) {
		header.lump[i].nOffset = offset;
		offset += header.lump[i].nLength;
	}

	// Make single backup
	if (g_settings.backUpMap && fileExists(path) && !fileExists(path + ".bak"))
	{
		int len;
		char* oldfile = loadFile(path, len);
		ofstream file(path + ".bak", ios::out | ios::binary | ios::trunc);
		if (!file.is_open()) {
			logf("Failed to open backup file for writing:\n%s\n", path.c_str());
			return;
		}
		logf("Writing backup to %s\n", (path + ".bak").c_str());

		file.write(oldfile, len);
		delete[] oldfile;
	}

	ofstream file(path, ios::out | ios::binary | ios::trunc);
	if (!file.is_open()) {
		logf("Failed to open BSP file for writing:\n%s\n", path.c_str());
		return;
	}

	logf("Writing %s\n", path.c_str());

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
#ifndef NDEBUG
	logf("Bsp version: %d\n", header.nVersion);
#endif
	
	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		fin.read((char*)&header.lump[i], sizeof(BSPLUMP));
#ifndef NDEBUG
		logf("Read lump id: %d. Len: %d. Offset %d.\n", i,header.lump[i].nLength,header.lump[i].nOffset);
#endif
	}

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

			if (ent->keyvalues.count("classname"))
				ents.push_back(ent);
			else
				logf("Found unknown classname entity. Skip it.\n");
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

	if (ents.size() > 1)
	{
		if (ents[0]->keyvalues["classname"] != "worldspawn")
		{
			logf("First entity has classname different from 'woldspawn', we do fixup it\n");
			for (int i = 1; i < ents.size(); i++)
			{
				if (ents[i]->keyvalues["classname"] == "worldspawn")
				{
					std::swap(ents[0], ents[i]);
					break;
				}
			}
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
	return false;
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
		&& ceilf(calc_allocblock_usage()) <= g_limits.max_allocblocks;
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
	int numBadExtent = 0;
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

		BSPTEXTUREINFO& info = texinfos[faces[i].iTextureInfo];
		int size[2];
		if (!(info.nFlags & TEX_SPECIAL) && !GetFaceLightmapSize(this, i, size)) {
			numBadExtent++;
		}
	}
	if (numBadExtent) {
		logf("Bad Surface Extents on %d faces\n", numBadExtent);
		isValid = false;
	}

	for (int i = 0; i < leafCount; i++) {
		if ((leaves[i].iFirstMarkSurface < 0 || leaves[i].iFirstMarkSurface + leaves[i].nMarkSurfaces > marksurfCount)) {
			logf("Bad marksurf reference in leaf %d: (%d + %d) / %d", 
				i, leaves[i].iFirstMarkSurface, leaves[i].nMarkSurfaces, marksurfCount);

			if (leaves[i].nMarkSurfaces == 0) {
				logf(" (fixed!)");
				leaves[i].iFirstMarkSurface = 0;
			}
			logf("\n");

			isValid = false;
		}
		//logf("Leaf %d: %d %d %d\n", i, marksurfs[leaves[i].iFirstMarkSurface], leaves[i].nMarkSurfaces);
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
		if ((nodes[i].firstFace < 0 || nodes[i].firstFace + nodes[i].nFaces > faceCount)) {
			logf("Bad face reference in node %d: %d / %d", i, nodes[i].firstFace, faceCount);
			if (nodes[i].nFaces == 0) {
				nodes[i].firstFace = 0;
				logf(" (fixed!)");
			}
			logf("\n");
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

		if (nodes[i].nMins[0] > nodes[i].nMaxs[0] ||
			nodes[i].nMins[1] > nodes[i].nMaxs[1] ||
			nodes[i].nMins[2] > nodes[i].nMaxs[2]) {
			logf("Backwards mins/maxs in node %d. Mins: (%d, %d, %d) Maxs: (%d %d %d)\n", i,
				(int)nodes[i].nMins[0], (int)nodes[i].nMins[1], (int)nodes[i].nMins[2],
				(int)nodes[i].nMaxs[0], (int)nodes[i].nMaxs[1], (int)nodes[i].nMaxs[2]);
			isValid = false;
		}
	}
	for (int i = 0; i < planeCount; i++) {
		BSPPLANE& plane = planes[i];
		float normLen = plane.vNormal.length();

		
		if (normLen < 0.5f) {
			logf("Bad normal for plane %d", i);
			if (normLen > 0) {
				plane.vNormal = plane.vNormal.normalize(1.0f);
				logf(" (fixed!)");
			}
			logf("\n");
			
			isValid = false;
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
		if ((models[i].iFirstFace < 0 || models[i].iFirstFace + models[i].nFaces > faceCount)) {
			logf("Bad face reference in model %d: %d / %d", i, models[i].iFirstFace, faceCount);
			if (models[i].nFaces == 0) {
				models[i].iFirstFace = 0;
				logf(" (fixed!)");
			}
			logf("\n");
			isValid = false;
		}
		if (models[i].iHeadnodes[0] >= nodeCount) {
			logf("Bad node reference in model %d hull 0: %d / %d\n", i, models[i].iHeadnodes[0], nodeCount);
			isValid = false;
		}
		for (int k = 1; k < MAX_MAP_HULLS; k++) {
			if (models[i].iHeadnodes[k] >= clipnodeCount) {
				logf("Bad clipnode reference in model %d hull %d: %d / %d\n", i, k, models[i].iHeadnodes[k], clipnodeCount);
				isValid = false;
			}
		}
		if (models[i].nMins.x > models[i].nMaxs.x ||
			models[i].nMins.y > models[i].nMaxs.y ||
			models[i].nMins.z > models[i].nMaxs.z) {
			logf("Backwards mins/maxs in model %d. Mins: (%f, %f, %f) Maxs: (%f %f %f)\n", i,
				models[i].nMins.x, models[i].nMins.y, models[i].nMins.z,
				models[i].nMaxs.x, models[i].nMaxs.y, models[i].nMaxs.z);
			isValid = false;
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

	int worldspawn_count = 0;
	for (int i = 0; i < ents.size(); i++) {
		if (ents[i]->keyvalues["classname"] == "worldspawn") {
			worldspawn_count++;
		}
	}
	if (worldspawn_count != 1) {
		logf("Found %d worldspawn entities (expected 1). This can cause crashes and svc_bad errors.\n", worldspawn_count);
		isValid = false;
	}

	if (!validate_vis_data()) {
		isValid = false;
	}

	for (Entity* ent : ents) {
		vec3 ori = ent->getOrigin();
		//float oob = g_engine_limits->max_mapboundary;
		float oob = 8192;

		if (fabs(ori.x) > oob || fabs(ori.y) > oob || fabs(ori.z) > oob) {
			logf("Entity '%s' (%s) outside map boundary at (%d %d %d)\n",
				ent->hasKey("targetname") ? ent->keyvalues["targetname"].c_str() : "",
				ent->hasKey("classname") ? ent->keyvalues["classname"].c_str() : "",
				(int)ori.x, (int)ori.y, (int)ori.z);
		}
	}

	for (int i = 0; i < textureCount; i++) {
		int32_t texOffset = ((int32_t*)textures)[i + 1];
		BSPMIPTEX& tex = *((BSPMIPTEX*)(textures + texOffset));

		if (tex.nWidth * tex.nHeight > g_limits.max_texturepixels) {
			logf("Texture '%s' too large (%dx%d)\n", tex.szName, tex.nWidth, tex.nHeight);
		}
	}

	return isValid;
}

vector<STRUCTUSAGE*> Bsp::get_sorted_model_infos(int sortMode) {
	vector<STRUCTUSAGE*> modelStructs;
	modelStructs.resize(modelCount);

	for (int i = 0; i < modelCount; i++) {
		modelStructs[i] = new STRUCTUSAGE(this);
		modelStructs[i]->modelIdx = i;
		mark_model_structures(i, modelStructs[i], false);
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
		print_stat("lightdata", lightDataLength, g_limits.max_lightdata, true);
		print_stat("visdata", visDataLength, g_limits.max_visdata, true);
		print_stat("entities", entCount, g_limits.max_entities, false);
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
		logf(getLeafContentsName(iNode));
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

int32_t Bsp::pointContents(int iNode, vec3 p, int hull, vector<int>& nodeBranch, int& leafIdx, int& childIdx) {
	if (iNode < 0) {
		leafIdx = -1;
		childIdx = -1;
		return iNode;
	}
	
	if (hull == 0) {
		while (iNode >= 0)
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
		while (iNode >= 0)
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
		if (num != CONTENTS_SOLID) {
			trace->fAllSolid = false;

			if (num == CONTENTS_EMPTY)
				trace->fInOpen = true;

			else if (num != CONTENTS_TRANSLUCENT)
				trace->fInWater = true;
		}
		else {
			trace->fStartSolid = true;
		}

		// empty
		return true;
	}

	if (num >= clipnodeCount) {
		logf("%s: bad node number\n", __func__);
		return false;
	}

	// find the point distances
	BSPCLIPNODE* node = &clipnodes[num];
	BSPPLANE* plane = &planes[node->iPlane];
	
	float t1 = dotProduct(plane->vNormal, p1) - plane->fDist;
	float t2 = dotProduct(plane->vNormal, p2) - plane->fDist;

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

	vec3 point = p2 - p1;
	vec3 mid = p1 + (point * frac);

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

	// backup the trace if the collision point is considered solid due to poor float precision
	// shouldn't really happen, but does occasionally
	int headnode = models[0].iHeadnodes[hull];
	while (pointContents(headnode, mid, hull) == CONTENTS_SOLID) {
		frac -= 0.1f;
		if (frac < 0.0f)
		{
			trace->flFraction = midf;
			trace->vecEndPos = mid;
			logf("backup past 0\n");
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

void Bsp::traceHull(vec3 start, vec3 end, int hull, TraceResult* trace)
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
	recursiveHullCheck(hull, headnode, 0.0f, 1.0f, start, end, trace);
}

const char* Bsp::getLeafContentsName(int32_t contents) {
	switch (contents) {
	case CONTENTS_EMPTY:
		return "EMPTY";
	case CONTENTS_SOLID:
		return "SOLID";
	case CONTENTS_WATER:
		return "WATER";
	case CONTENTS_SLIME:
		return "SLIME";
	case CONTENTS_LAVA:
		return "LAVA";
	case CONTENTS_SKY:
		return "SKY";
	case CONTENTS_ORIGIN:
		return "ORIGIN";
	case CONTENTS_CURRENT_0:
		return "CURRENT_0";
	case CONTENTS_CURRENT_90:
		return "CURRENT_90";
	case CONTENTS_CURRENT_180:
		return "CURRENT_180";
	case CONTENTS_CURRENT_270:
		return "CURRENT_270";
	case CONTENTS_CURRENT_UP:
		return "CURRENT_UP";
	case CONTENTS_CURRENT_DOWN:
		return "CURRENT_DOWN";
	case CONTENTS_TRANSLUCENT:
		return "TRANSLUCENT";
	default:
		return "UNKNOWN";
	}
}

int Bsp::get_leaf(vec3 pos, int hull) {
	int iNode = models->iHeadnodes[hull];

	if (hull == 0) {
		while (iNode >= 0)
		{
			BSPNODE& node = nodes[iNode];
			BSPPLANE& plane = planes[node.iPlane];

			float d = dotProduct(plane.vNormal, pos) - plane.fDist;
			if (d < 0) {
				iNode = node.iChildren[1];
			}
			else {
				iNode = node.iChildren[0];
			}
		}

		return ~iNode;
	}

	int lastNode = -1;
	int lastSide = 0;

	while (iNode >= 0)
	{
		BSPCLIPNODE& node = clipnodes[iNode];
		BSPPLANE& plane = planes[node.iPlane];

		float d = dotProduct(plane.vNormal, pos) - plane.fDist;
		if (d < 0) {
			lastNode = iNode;
			iNode = node.iChildren[1];
			lastSide = 1;
		}
		else {
			lastNode = iNode;
			iNode = node.iChildren[0];
			lastSide = 0;
		}
	}

	// clipnodes don't have leaf structs, so generate an id based on the last clipnode index and
	// the side of the plane that would be recursed to reach the leaf contents, if there were a leaf
	return lastNode * 2 + lastSide;
}

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

	memset(pvsFaces, 0, pvsFaceCount*sizeof(bool));
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
	regenerate_clipnodes(modelIdx, -1);

	return modelIdx;
}

void Bsp::add_model(Bsp* sourceMap, int modelIdx) {
	STRUCTUSAGE usage(sourceMap);
	sourceMap->mark_model_structures(modelIdx, &usage, false);

	// TODO: add the model lel

	usage.compute_sum();

	logf("");
}

BSPMIPTEX * Bsp::find_embedded_texture(const char* name) {
	if (!name || name[0] == '\0')
		return nullptr;
	for (int i = 0; i < textureCount; i++) {
		int32_t oldOffset = ((int32_t*)textures)[i + 1];
		BSPMIPTEX* oldTex = (BSPMIPTEX*)(textures + oldOffset);
		if (strcmp(name, oldTex->szName) == 0)
		{
			return oldTex;
		}
	}
	return nullptr;
}

int Bsp::add_texture(const char* name, byte* data, int width, int height) {
	if (width % 16 != 0 || height % 16 != 0) {
		logf("Dimensions not divisible by 16");
		return -1;
	}
	if (width > MAX_TEXTURE_DIMENSION || height > MAX_TEXTURE_DIMENSION) {
		logf("Width/height too large");
		return -1;
	}

	BSPMIPTEX* oldtex = find_embedded_texture(name);

	if (oldtex)
	{
		logf("Texture with name %s found in map. Just replace it.\n", name);
		if (oldtex->nWidth != width || oldtex->nHeight != height)
		{
			sprintf(oldtex->szName, "%s", "-unused_texture");
			logf("Warning! Texture size different. Need rename old texture.\n");
			oldtex = NULL;
		}
	}

	COLOR3 palette[256];
	memset(&palette, 0, sizeof(COLOR3) * 256);
	int colorCount = 0;

	// create pallete and full-rez mipmap
	byte* mip[MIPLEVELS];
	mip[0] = new byte[width * height];
	COLOR3* src = (COLOR3*)data;
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			int paletteIdx = -1;
			for (int k = 0; k < colorCount; k++) {
				if (*src == palette[k]) {
					paletteIdx = k;
					break;
				}
			}
			if (paletteIdx == -1) {
				if (colorCount >= 256) {
					logf("Too many colors");
					delete[] mip[0];
					return -1;
				}
				palette[colorCount] = *src;
				paletteIdx = colorCount;
				colorCount++;
			}

			mip[0][y*width + x] = paletteIdx;
			src++;
		}
	}
	
	int texDataSize = width * height + sizeof(COLOR3) * 256 + 4; // 4 = padding

	// generate mipmaps
	for (int i = 1; i < MIPLEVELS; i++) {
		int div = 1 << i;
		int mipWidth = width / div;
		int mipHeight = height / div;
		texDataSize += mipWidth * height;
		mip[i] = new byte[mipWidth * mipHeight];

		src = (COLOR3*)data;
		for (int y = 0; y < mipHeight; y++) {
			for (int x = 0; x < mipWidth; x++) {

				int paletteIdx = -1;
				for (int k = 0; k < colorCount; k++) {
					if (*src == palette[k]) {
						paletteIdx = k;
						break;
					}
				}

				mip[i][y * mipWidth + x] = paletteIdx;
				src += div;
			}
		}
	}

	if (oldtex != nullptr)
	{
		memcpy((byte*)oldtex + oldtex->nOffsets[0], mip[0], width * height);
		memcpy((byte*)oldtex + oldtex->nOffsets[1], mip[1], (width >> 1) * (height >> 1));
		memcpy((byte*)oldtex + oldtex->nOffsets[2], mip[2], (width >> 2) * (height >> 2));
		memcpy((byte*)oldtex + oldtex->nOffsets[3], mip[3], (width >> 3) * (height >> 3));
		memcpy((byte*)oldtex + (oldtex->nOffsets[3] + (width >> 3) * (height >> 3) + 2), palette, sizeof(COLOR3) * 256);
		for (int i = 0; i < MIPLEVELS; i++) {
			delete[] mip[i];
		}
		return 0;
	}

	int newTexLumpSize = header.lump[LUMP_TEXTURES].nLength + sizeof(int32_t) + sizeof(BSPMIPTEX) + texDataSize;
	byte* newTexData = new byte[newTexLumpSize];
	memset(newTexData, 0, sizeof(newTexLumpSize));

	// create new texture lump header
	int32_t* newLumpHeader = (int32_t*)newTexData;
	int32_t* oldLumpHeader = (int32_t*)lumps[LUMP_TEXTURES];
	*newLumpHeader = textureCount + 1;

	for (int i = 0; i < textureCount; i++) {
		*(newLumpHeader + i + 1) = *(oldLumpHeader + i + 1) + sizeof(int32_t); // make room for the new offset
	}

	// copy old texture data
	int oldTexHeaderSize = (textureCount + 1) * sizeof(int32_t);
	int newTexHeaderSize = oldTexHeaderSize + sizeof(int32_t);
	int oldTexDatSize = header.lump[LUMP_TEXTURES].nLength - (textureCount+1)*sizeof(int32_t);
	memcpy(newTexData + newTexHeaderSize, lumps[LUMP_TEXTURES] + oldTexHeaderSize, oldTexDatSize);

	// add new texture to the end of the lump
	int newTexOffset = newTexHeaderSize + oldTexDatSize;
	newLumpHeader[textureCount + 1] = newTexOffset;
	BSPMIPTEX* newMipTex = (BSPMIPTEX*)(newTexData + newTexOffset);
	newMipTex->nWidth = width;
	newMipTex->nHeight = height;
	strncpy(newMipTex->szName, name, MAXTEXTURENAME);
	
	newMipTex->nOffsets[0] = sizeof(BSPMIPTEX);
	newMipTex->nOffsets[1] = newMipTex->nOffsets[0] + width*height;
	newMipTex->nOffsets[2] = newMipTex->nOffsets[1] + (width >> 1)*(height >> 1);
	newMipTex->nOffsets[3] = newMipTex->nOffsets[2] + (width >> 2)*(height >> 2);
	int palleteOffset = newMipTex->nOffsets[3] + (width >> 3) * (height >> 3) + 2;

	memcpy(newTexData + newTexOffset + newMipTex->nOffsets[0], mip[0], width*height);
	memcpy(newTexData + newTexOffset + newMipTex->nOffsets[1], mip[1], (width >> 1) * (height >> 1));
	memcpy(newTexData + newTexOffset + newMipTex->nOffsets[2], mip[2], (width >> 2) * (height >> 2));
	memcpy(newTexData + newTexOffset + newMipTex->nOffsets[3], mip[3], (width >> 3) * (height >> 3));
	memcpy(newTexData + newTexOffset + palleteOffset, palette, sizeof(COLOR3)*256);
	uint16_t* colorCountPtr = (uint16_t*)(newTexData + newTexOffset + palleteOffset - 2);
	*colorCountPtr = 256; // required for hlrad

	for (int i = 0; i < MIPLEVELS; i++) {
		delete[] mip[i];
	}

	replace_lump(LUMP_TEXTURES, newTexData, newTexLumpSize);

	return textureCount-1;
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

	/*
		vertex and edge numbers on the cube:

			7--<3---6
		   /|      /|
		  4-+-2>--5 |
		  | |     | |   z
		  | 3--<1-+-2   | y
		  |/      |/    |/
		  0---0>--1     +--x
	*/

	// add new verts (1 for each cube corner)
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

	// add new edges (minimum needed to refrence every vertex once)
	int startEdge = edgeCount;
	{
		BSPEDGE* newEdges = new BSPEDGE[edgeCount + 8];
		memcpy(newEdges, edges, edgeCount * sizeof(BSPEDGE));

		// defining an edge for every vertex because otherwise hlrad crashes, even though
		// only 4 edges are required to reference every vertex on the cube
		for (int i = 0; i < 8; i++) {
			newEdges[startEdge + i] = BSPEDGE(startVert + i, startVert + i);
		}

		replace_lump(LUMP_EDGES, newEdges, (edgeCount + 8) * sizeof(BSPEDGE));
	}

	// add new surfedges (vertex lookups into edges which define the faces, 4 per face, clockwise order)
	int startSurfedge = surfedgeCount;
	{
		int32_t* newSurfedges = new int32_t[surfedgeCount + 24];
		memcpy(newSurfedges, surfedges, surfedgeCount * sizeof(int32_t));

		int32_t surfEdgeIdx = startSurfedge;

		// left face
		newSurfedges[surfEdgeIdx++] = startEdge + 7;
		newSurfedges[surfEdgeIdx++] = startEdge + 4;
		newSurfedges[surfEdgeIdx++] = startEdge + 0;
		newSurfedges[surfEdgeIdx++] = startEdge + 3;

		// right face
		newSurfedges[surfEdgeIdx++] = startEdge + 5;
		newSurfedges[surfEdgeIdx++] = startEdge + 6;
		newSurfedges[surfEdgeIdx++] = startEdge + 2;
		newSurfedges[surfEdgeIdx++] = startEdge + 1;

		// front face
		newSurfedges[surfEdgeIdx++] = startEdge + 4;
		newSurfedges[surfEdgeIdx++] = startEdge + 5;
		newSurfedges[surfEdgeIdx++] = startEdge + 1;
		newSurfedges[surfEdgeIdx++] = startEdge + 0;

		// back face
		newSurfedges[surfEdgeIdx++] = startEdge + 6;
		newSurfedges[surfEdgeIdx++] = startEdge + 7;
		newSurfedges[surfEdgeIdx++] = startEdge + 3;
		newSurfedges[surfEdgeIdx++] = startEdge + 2;

		// bottom face
		newSurfedges[surfEdgeIdx++] = startEdge + 0;
		newSurfedges[surfEdgeIdx++] = startEdge + 1;
		newSurfedges[surfEdgeIdx++] = startEdge + 2;
		newSurfedges[surfEdgeIdx++] = startEdge + 3;

		// top face
		newSurfedges[surfEdgeIdx++] = startEdge + 7;
		newSurfedges[surfEdgeIdx++] = startEdge + 6;
		newSurfedges[surfEdgeIdx++] = startEdge + 5;
		newSurfedges[surfEdgeIdx++] = startEdge + 4;
		
		replace_lump(LUMP_SURFEDGES, newSurfedges, (surfedgeCount + 24) * sizeof(int32_t));
	}

	// add new planes (1 for each face/node)
	int startPlane = planeCount;
	{
		BSPPLANE* newPlanes = new BSPPLANE[planeCount + 6];
		memcpy(newPlanes, planes, planeCount * sizeof(BSPPLANE));

		// normals are inverted later using nPlaneSide
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

		static vec3 faceUp[6] {
			vec3(0, 0, -1),	// left
			vec3(0, 0, -1), // right
			vec3(0, 0, -1), // front
			vec3(0, 0, -1), // back
			vec3(0, 1, 0), // bottom
			vec3(0, 1, 0) // top
		};
		static vec3 faceRt[6]{
			vec3(0, -1, 0),	// left
			vec3(0, 1, 0), // right
			vec3(1, 0, 0), // front
			vec3(-1, 0, 0), // back
			vec3(1, 0, 0), // bottom
			vec3(-1, 0, 0) // top
		};

		for (int i = 0; i < 6; i++) {
			BSPTEXTUREINFO& info = newTexinfos[startTexinfo + i];
			info.iMiptex = textureIdx;
			info.nFlags = TEX_SPECIAL;
			info.shiftS = 0;
			info.shiftT = 0;
			info.vT = faceUp[i];
			info.vS = faceRt[i];
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
			face.nPlaneSide = i % 2 == 0; // even-numbered planes use inverted normals
			face.iTextureInfo = startTexinfo+i;
			face.nLightmapOffset = 0; // TODO: Lighting
			memset(face.nStyles, 255, 4);
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

	targetModel->nMaxs = vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	targetModel->nMins = vec3(FLT_MAX, FLT_MAX, FLT_MAX);
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
			memset(face.nStyles, 255, 4);

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

	targetModel->nMaxs = vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	targetModel->nMins = vec3(FLT_MAX, FLT_MAX, FLT_MAX);
	for (int i = 0; i < solid.hullVerts.size(); i++) {
		vec3 v = verts[startVert + i];
		expandBoundingBox(v, targetModel->nMins, targetModel->nMaxs);
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

	vec3 vertMin(FLT_MAX, FLT_MAX, FLT_MAX);
	vec3 vertMax(-FLT_MAX, -FLT_MAX, -FLT_MAX);
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

int Bsp::create_texinfo() {
	BSPTEXTUREINFO* newTexinfos = new BSPTEXTUREINFO[texinfoCount + 1];
	memcpy(newTexinfos, texinfos, texinfoCount * sizeof(BSPTEXTUREINFO));

	BSPTEXTUREINFO& newTexinfo = newTexinfos[texinfoCount];
	memset(&newTexinfo, 0, sizeof(BSPTEXTUREINFO));

	replace_lump(LUMP_TEXINFO, newTexinfos, (texinfoCount + 1) * sizeof(BSPTEXTUREINFO));

	return texinfoCount - 1;
}

int Bsp::duplicate_model(int modelIdx) {
	STRUCTUSAGE usage(this);
	mark_model_structures(modelIdx, &usage, true);

	STRUCTREMAP remap(this);

	vector<BSPPLANE> newPlanes;
	for (int i = 0; i < usage.count.planes; i++) {
		if (usage.planes[i]) {
			remap.planes[i] = planeCount + newPlanes.size();
			newPlanes.push_back(planes[i]);
		}
	}

	vector<vec3> newVerts;
	for (int i = 0; i < usage.count.verts; i++) {
		if (usage.verts[i]) {
			remap.verts[i] = vertCount + newVerts.size();
			newVerts.push_back(verts[i]);
		}
	}

	vector<BSPEDGE> newEdges;
	for (int i = 0; i < usage.count.edges; i++) {
		if (usage.edges[i]) {
			remap.edges[i] = edgeCount + newEdges.size();

			BSPEDGE edge = edges[i];
			for (int k = 0; k < 2; k++)
				edge.iVertex[k] = remap.verts[edge.iVertex[k]];
			newEdges.push_back(edge);
		}
	}

	vector<int32_t> newSurfedges;
	for (int i = 0; i < usage.count.surfEdges; i++) {
		if (usage.surfEdges[i]) {
			remap.surfEdges[i] = surfedgeCount + newSurfedges.size();

			int32_t surfedge = remap.edges[abs(surfedges[i])];
			if (surfedges[i] < 0)
				surfedge = -surfedge;
			newSurfedges.push_back(surfedge);
		}
	}

	vector<BSPTEXTUREINFO> newTexinfo;
	for (int i = 0; i < usage.count.texInfos; i++) {
		if (usage.texInfo[i]) {
			remap.texInfo[i] = texinfoCount + newTexinfo.size();
			newTexinfo.push_back(texinfos[i]);
		}
	}

	vector<BSPFACE> newFaces;
	vector<COLOR3> newLightmaps;
	int lightmapAppendSz = 0;
	for (int i = 0; i < usage.count.faces; i++) {
		if (usage.faces[i]) {
			remap.faces[i] = faceCount + newFaces.size();

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
				newLightmaps.push_back(lightmapSrc[k]);
			}

			face.nLightmapOffset = lightmapCount != 0 ? lightDataLength + lightmapAppendSz : -1;
			newFaces.push_back(face);

			lightmapAppendSz += lightmapSz * sizeof(COLOR3);
		}
	}

	vector<BSPNODE> newNodes;
	for (int i = 0; i < usage.count.nodes; i++) {
		if (usage.nodes[i]) {
			remap.nodes[i] = nodeCount + newNodes.size();
			newNodes.push_back(nodes[i]);
		}
	}
	for (int i = 0; i < newNodes.size(); i++) {
		BSPNODE& node = newNodes[i];
		node.firstFace = remap.faces[node.firstFace];
		node.iPlane = remap.planes[node.iPlane];

		for (int k = 0; k < 2; k++) {
			if (node.iChildren[k] > 0) {
				node.iChildren[k] = remap.nodes[node.iChildren[k]];
			}
		}
	}

	vector<BSPCLIPNODE> newClipnodes;
	for (int i = 0; i < usage.count.clipnodes; i++) {
		if (usage.clipnodes[i]) {
			remap.clipnodes[i] = clipnodeCount + newClipnodes.size();
			newClipnodes.push_back(clipnodes[i]);
		}
	}
	for (int i = 0; i < newClipnodes.size(); i++) {
		BSPCLIPNODE& clipnode = newClipnodes[i];
		clipnode.iPlane = remap.planes[clipnode.iPlane];

		for (int k = 0; k < 2; k++) {
			if (clipnode.iChildren[k] > 0) {
				clipnode.iChildren[k] = remap.clipnodes[clipnode.iChildren[k]];
			}
		}
	}

	// MAYBE TODO: duplicate leaves(?) + marksurfs + recacl vis + update undo command lumps

	if (newClipnodes.size())
		append_lump(LUMP_CLIPNODES, &newClipnodes[0], sizeof(BSPCLIPNODE) * newClipnodes.size());
	if (newEdges.size())
		append_lump(LUMP_EDGES, &newEdges[0], sizeof(BSPEDGE) * newEdges.size());
	if (newFaces.size())
		append_lump(LUMP_FACES, &newFaces[0], sizeof(BSPFACE) * newFaces.size());
	if (newNodes.size())
		append_lump(LUMP_NODES, &newNodes[0], sizeof(BSPNODE) * newNodes.size());
	if (newPlanes.size())
		append_lump(LUMP_PLANES, &newPlanes[0], sizeof(BSPPLANE) * newPlanes.size());
	if (newSurfedges.size())
		append_lump(LUMP_SURFEDGES, &newSurfedges[0], sizeof(int32_t) * newSurfedges.size());
	if (newTexinfo.size())
		append_lump(LUMP_TEXINFO, &newTexinfo[0], sizeof(BSPTEXTUREINFO) * newTexinfo.size());
	if (newVerts.size())
		append_lump(LUMP_VERTICES, &newVerts[0], sizeof(vec3) * newVerts.size());
	if (newLightmaps.size())
		append_lump(LUMP_LIGHTING, &newLightmaps[0], sizeof(COLOR3) * newLightmaps.size());

	int newModelIdx = create_model();
	BSPMODEL& oldModel = models[modelIdx];
	BSPMODEL& newModel = models[newModelIdx];
	memcpy(&newModel, &oldModel, sizeof(BSPMODEL));

	newModel.iFirstFace = remap.faces[oldModel.iFirstFace];
	newModel.iHeadnodes[0] = oldModel.iHeadnodes[0] < 0 ? -1 : remap.nodes[oldModel.iHeadnodes[0]];
	for (int i = 1; i < MAX_MAP_HULLS; i++) {
		newModel.iHeadnodes[i] = oldModel.iHeadnodes[i] < 0 ? -1 : remap.clipnodes[oldModel.iHeadnodes[i]];
	}
	newModel.nVisLeafs = 0; // techinically should match the old model, but leaves aren't duplicated yet

	return newModelIdx;
}

BSPTEXTUREINFO* Bsp::get_unique_texinfo(int faceIdx) {
	BSPFACE& targetFace = faces[faceIdx];
	int targetInfo = targetFace.iTextureInfo;

	for (int i = 0; i < faceCount; i++) {
		if (i != faceIdx && faces[i].iTextureInfo == targetFace.iTextureInfo) {
			int newInfo = create_texinfo();
			texinfos[newInfo] = texinfos[targetInfo];
			targetInfo = newInfo;
			targetFace.iTextureInfo = newInfo;
			debugf("Create new texinfo\n");
			break;
		}
	}

	return &texinfos[targetInfo];
}

int Bsp::get_model_from_face(int faceIdx) {
	for (int i = 0; i < modelCount; i++) {
		BSPMODEL& model = models[i];
		if (faceIdx >= model.iFirstFace && faceIdx < model.iFirstFace + model.nFaces) {
			return i;
		}
	}
	return -1;
}

int16 Bsp::regenerate_clipnodes_from_nodes(int iNode, int hullIdx) {
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
			return regenerate_clipnodes_from_nodes(solidChild, hullIdx);
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
			int childIdx = regenerate_clipnodes_from_nodes(node.iChildren[i], hullIdx);
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

void Bsp::regenerate_clipnodes(int modelIdx, int hullIdx) {
	BSPMODEL& model = models[modelIdx];

	for (int i = 1; i < MAX_MAP_HULLS; i++) {
		if (hullIdx >= 0 && hullIdx != i)
			continue;

		// first create a bounding box for the model. For some reason this is needed to prevent
		// planes from extended farther than they should. All clip types do this.
		int solidNodeIdx = create_clipnode_box(model.nMins, model.nMaxs, &model, i, false); // fills in the headnode
		
		for (int k = 0; k < 2; k++) {
			if (clipnodes[solidNodeIdx].iChildren[k] == CONTENTS_SOLID) {
				clipnodes[solidNodeIdx].iChildren[k] = regenerate_clipnodes_from_nodes(model.iHeadnodes[0], i);
			}
		}

		// TODO: create clipnodes to "cap" edges that are 90+ degrees (most CSG clip types do this)
		// that will fix broken collision around those edges (invisible solid areas)
	}
}

void Bsp::dump_lightmap(int faceIdx, string outputPath) {
	int faceCount = header.lump[LUMP_FACES].nLength / sizeof(BSPFACE);

	BSPFACE& face = faces[faceIdx];

	int mins[2];
	int extents[2];
	GetFaceExtents(this, faceIdx, mins, extents);

	int lightmapSz = extents[0] * extents[1];

	lodepng_encode24_file(outputPath.c_str(), (byte*)lightdata + face.nLightmapOffset, extents[0], extents[1]);
}

void Bsp::dump_lightmap_atlas(string outputPath) {
	int lightmapWidth = g_limits.max_surface_extents;

	int lightmapsPerDim = ceil(sqrt(faceCount));
	int atlasDim = lightmapsPerDim * lightmapWidth;
	int sz = atlasDim * atlasDim;
	logf("ATLAS SIZE %d x %d (%.2f KB)", lightmapsPerDim, lightmapsPerDim, (sz * sizeof(COLOR3))/1024.0f);

	COLOR3* pngData = new COLOR3[sz];

	memset(pngData, 0, sz * sizeof(COLOR3));

	for (int i = 0; i < faceCount; i++) {
		BSPFACE& face = faces[i];

		if (face.nStyles[0] == 255)
			continue; // no lighting info

		int atlasX = (i % lightmapsPerDim)*lightmapWidth;
		int atlasY = (i / lightmapsPerDim)*lightmapWidth;

		int size[2];
		GetFaceLightmapSize(this, i, size);

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

void Bsp::print_leaf(BSPLEAF leaf) {
	logf(getLeafContentsName(leaf.nContents));
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

	if (pvsFaceCount != faceCount) {
		pvsFaceCount = faceCount;

		if (pvsFaces) {
			delete[] pvsFaces;
		}
		pvsFaces = new bool[pvsFaceCount];
	}
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
