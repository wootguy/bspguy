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
#include <queue>
#include <fstream>
#include <algorithm>
#include "Clipper.h"
#include <float.h>
#include "Wad.h"
#include <unordered_set>
#include "Renderer.h"
#include "icons/aaatrigger.h"
#include "mstream.h"
#include "Fgd.h"
#include "Texture.h"
#include "LeafNavMeshGenerator.h"
#include "NavMeshGenerator.h"
#include "PolyOctree.h"

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

void Bsp::get_model_vertex_bounds(int modelIdx, vec3& mins, vec3& maxs) {
	mins = vec3(FLT_MAX, FLT_MAX, FLT_MAX);
	maxs = vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);

	BSPMODEL& model = models[modelIdx];

	for (int i = 0; i < model.nFaces && model.iFirstFace + i < faceCount; i++) {
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
		if (solidNodes.empty()) {
			solidNodes = get_model_leaf_volume_cuts(modelIdx, 3, CONTENTS_SOLID);
		}
		if (solidNodes.empty()) {
			solidNodes = get_model_leaf_volume_cuts(modelIdx, 1, CONTENTS_SOLID);
		}
		if (solidNodes.empty()) {
			solidNodes = get_model_leaf_volume_cuts(modelIdx, 2, CONTENTS_SOLID);
		}

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

void Bsp::get_model_hull_bounds(int modelIdx, int hull, vec3& mins, vec3& maxs) {
	mins = vec3(FLT_MAX, FLT_MAX, FLT_MAX);
	maxs = vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);

	Clipper clipper;
	vector<NodeVolumeCuts> solidNodes = get_model_leaf_volume_cuts(modelIdx, hull, CONTENTS_SOLID);

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

void Bsp::get_model_merge_bounds(int modelIdx, vec3& mins, vec3& maxs) {
	get_model_vertex_bounds(modelIdx, mins, maxs);

	// allow a tiny bit of overlap for models that are touching each other
	mins += vec3(EPSILON, EPSILON, EPSILON);
	maxs -= vec3(EPSILON, EPSILON, EPSILON);
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
		else if (node.iChildren[i] == contents || contents == CONTENTS_ANY || (contents == CONTENTS_NOT_SOLID && node.iChildren[i] != CONTENTS_SOLID)) {
			NodeVolumeCuts nodeVolumeCuts;
			nodeVolumeCuts.nodeIdx = iNode;
			nodeVolumeCuts.leafIdx = -1;

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

		int leafIdx = ~node.iChildren[i];

		if (node.iChildren[i] >= 0) {
			get_node_leaf_cuts(node.iChildren[i], clipOrder, output, contents);
		}
		else if (leaves[leafIdx].nContents == contents || contents == CONTENTS_ANY
			|| (contents == CONTENTS_NOT_LEAF_0 && leafIdx != 0)
			|| (contents == CONTENTS_NOT_SOLID && leaves[leafIdx].nContents != CONTENTS_SOLID)) {
			NodeVolumeCuts nodeVolumeCuts;
			nodeVolumeCuts.nodeIdx = iNode;
			nodeVolumeCuts.leafIdx = leafIdx;

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
			debugf("Face has less than 3 verts\n");
			return false; // invalid solid
		}

		BSPPLANE newPlane;
		if (!getPlaneFromVerts(verts, newPlane.vNormal, newPlane.fDist)) {
			debugf("Verts not planar\n");
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

	BSPMIPTEX* tex = get_texture(info.iMiptex);
	if (!tex) {
		return;
	}

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
	while (fabs(info.shiftS) > tex->nWidth) {
		info.shiftS += (info.shiftS < 0) ? (int)tex->nWidth : -(int)(tex->nWidth);
	}
	while (fabs(info.shiftT) > tex->nHeight) {
		info.shiftT += (info.shiftT < 0) ? (int)tex->nHeight : -(int)(tex->nHeight);
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

	bool notMovingLeaves = modelIdx != 0;

	int startIdx;
	mark_model_structures(modelIdx, &shouldMove, notMovingLeaves);
	for (int i = 0; i < modelCount; i++) {
		if (i != modelIdx)
			mark_model_structures(i, &shouldNotMove, notMovingLeaves);

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
	memcpy(newTexinfos, texinfos, texinfoCount * sizeof(BSPTEXTUREINFO));

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
 	for (int i = modelCount-1; i >= 0; i--) { 
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
		string cname = ents[i]->getClassname();
		string tname = ents[i]->getTargetname();

		if (cname.find("monster_") == 0) {
			vec3 minhull;
			vec3 maxhull;

			if (!ents[i]->getKeyvalue("minhullsize").empty())
				minhull = Keyvalue("", ents[i]->getKeyvalue("minhullsize")).getVector();
			if (!ents[i]->getKeyvalue("maxhullsize").empty())
				maxhull = Keyvalue("", ents[i]->getKeyvalue("maxhullsize")).getVector();

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
			string cname = usageEnts[k]->getClassname();
			string tname = usageEnts[k]->getTargetname();
			int spawnflags = atoi(usageEnts[k]->getKeyvalue("spawnflags").c_str());

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
				bool notPassable = !(spawnflags & 8); // "Passable" or "Not solid" unchecked
				needsPlayerHulls |= notPassable;
				needsMonsterHulls |= notPassable;
				needsVisibleHull |= notPassable || !is_invisible_solid(usageEnts[k]);
			}
			else if (cname.find("trigger_") == 0) {
				if (conditionalPointEntTriggers.find(cname) != conditionalPointEntTriggers.end()) {
					needsVisibleHull |= !!(spawnflags & 8); // "Everything else" flag checked
					needsPlayerHulls |= !(spawnflags & 2); // "No clients" unchecked
					needsMonsterHulls |= (spawnflags & 1) || (spawnflags & 4); // "monsters" or "pushables" checked
				}
				else if (cname == "trigger_push") { 
					needsPlayerHulls |= !(spawnflags & 8); // "No clients" unchecked
					needsMonsterHulls |= (spawnflags & 4) || !(spawnflags & 16); // "Pushables" checked or "No monsters" unchecked
					needsVisibleHull |= true; // needed for point-ent pushing
				}
				else if (cname == "trigger_hurt") {
					needsPlayerHulls |= !(spawnflags & 8); // "No clients" unchecked
					needsMonsterHulls |= !(spawnflags & 16) || !(spawnflags & 32); // "Fire/Touch client only" unchecked
				}
				else {
					needsPlayerHulls |= true;
					needsMonsterHulls |= true;
				}
			}
			else if (cname == "func_clip") {
				needsPlayerHulls |= !(spawnflags & 8); // "No clients" not checked
				needsMonsterHulls |= (spawnflags & 8) || !(spawnflags & 16); // "Pushables" checked or "No monsters" unchecked
				needsVisibleHull |= (spawnflags & 32) || (spawnflags & 64); // "Everything else" or "item_inv" checked
			}
			else if (cname == "func_conveyor") {
				bool isSolid = !(spawnflags & 2); // "Not Solid" unchecked
				needsPlayerHulls |= isSolid;
				needsMonsterHulls |= isSolid;
				needsVisibleHull |= isSolid || !is_invisible_solid(usageEnts[k]);
			}
			else if (cname == "func_friction") {
				needsPlayerHulls |= true;
				needsMonsterHulls |= true;
			}
			else if (cname == "func_rot_button") {
				bool isSolid = !(spawnflags & 1); // "Not Solid" unchecked
				needsPlayerHulls |= isSolid;
				needsMonsterHulls |= isSolid;
				needsVisibleHull |= true;
			}
			else if (cname == "func_rotating") {
				bool isSolid = !(spawnflags & 64); // "Not Solid" unchecked
				needsPlayerHulls |= isSolid;
				needsMonsterHulls |= isSolid;
				needsVisibleHull |= true;
			}
			else if (cname == "func_ladder") {
				needsPlayerHulls |= true;
				needsVisibleHull |= true;
			}
			else if (playerOnlyTriggers.find(cname) != playerOnlyTriggers.end()) {
				needsPlayerHulls |= true;
			}
			else if (monsterOnlyTriggers.find(cname) != monsterOnlyTriggers.end()) {
				needsMonsterHulls |= true;
			}
			else {
				// assume all hulls are needed
				needsPlayerHulls |= true;
				needsMonsterHulls |= true;
				needsVisibleHull |= true;
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
	float oob_coord = g_settings.mapsize_max;

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
	float oob_coord = g_settings.mapsize_max;

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
	float oob_coord = g_settings.mapsize_max;

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
	float oob_coord = g_settings.mapsize_max;

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

int Bsp::deduplicate_models(bool allowTextureShift, bool dryrun) {
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
			if (ent->hasKey("model") && ent->getKeyvalue("model") == modelKeyA) {
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
					if (ent->hasKey("model") && ent->getKeyvalue("model") == modelKeyB) {
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
				BSPMIPTEX* tex = get_texture(infoA.iMiptex);
				if (!tex) {
					continue;
				}
				float tw = 1.0f / (float)tex->nWidth;
				float th = 1.0f / (float)tex->nHeight;

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

								if (allowTextureShift) {
									uvsMatch = true;
								}

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

	unordered_set<int> oldUniqueModels;
	unordered_set<int> newUniqueModels;

	for (Entity* ent : ents) {
		if (!ent->hasKey("model")) {
			continue;
		}
		if (ent->hidden)
			continue;

		string model = ent->getKeyvalue("model");

		if (model[0] != '*')
			continue;

		int modelIdx = atoi(model.substr(1).c_str());

		if (modelRemap.find(modelIdx) != modelRemap.end()) {
			ModelIdxRemap remap = modelRemap[modelIdx];

			oldUniqueModels.insert(modelIdx);
			newUniqueModels.insert(remap.newIdx);

			if (!dryrun) {
				ent->setOrAddKeyvalue("origin", (ent->getOrigin() + remap.offset).toKeyvalueString());
				ent->setOrAddKeyvalue("model", "*" + to_string(remap.newIdx));
			}
		}
	}

	int refsRemoved = oldUniqueModels.size() - newUniqueModels.size();
	if (!dryrun) {
		logf("Removed %d BSP model references\n", refsRemoved);
	}

	return refsRemoved;
}

int Bsp::get_entity_index(Entity* ent) {
	for (int i = 0; i < ents.size(); i++) {
		if (ents[i] == ent) {
			return i;
		}
	}

	return -1;
}

float Bsp::calc_allocblock_usage() {
	int total = 0;

	for (int i = 0; i < faceCount; i++) {
		BSPFACE& face = faces[i];
		BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];
		if (info.nFlags & TEX_SPECIAL)
			continue; // does not use lightmaps

		BSPMIPTEX* tex = get_texture(info.iMiptex);
		if (tex && tex->szName[0] == '!')
			continue; // water doesn't use lightmaps

		int size[2];
		GetFaceLightmapSize(this, i, size);

		total += size[0] * size[1];
	}

	const int allocBlockSize = 128 * 128;

	return total / (float)allocBlockSize;
}

void Bsp::get_scaled_texture_dimensions(int textureIdx, float scale, int& newWidth, int& newHeight) {
	BSPMIPTEX* tex = get_texture(textureIdx);
	if (!tex) {
		return;
	}

	newWidth = max(16, (((int)(tex->nWidth * scale) + 7) / 16) * 16);
	newHeight = max(16, (((int)(tex->nHeight * scale) + 7) / 16) * 16);
	if (newWidth == tex->nWidth) {
		newWidth = max(16, ((int)(tex->nWidth * scale) / 16) * 16);
	}
	if (newHeight == tex->nHeight) {
		newHeight = max(16, ((int)(tex->nHeight * scale) / 16) * 16);
	}
}

bool Bsp::has_bad_extents(int textureIdx, float scale) {
	BSPMIPTEX* tex = get_texture(textureIdx);
	if (!tex) {
		return false;
	}

	int newWidth, newHeight;
	get_scaled_texture_dimensions(textureIdx, scale, newWidth, newHeight);

	float actualScaleX = (float)newWidth / tex->nWidth;
	float actualScaleY = (float)newHeight / tex->nHeight;

	for (int i = 0; i < faceCount; i++) {
		BSPTEXTUREINFO& info = texinfos[faces[i].iTextureInfo];

		if ((info.nFlags & TEX_SPECIAL) || info.iMiptex != textureIdx) {
			continue;
		}

		BSPTEXTUREINFO oldInfo = info;
		
		adjust_resized_texture_coordinates(faces[i], info, newWidth, newHeight, tex->nWidth, tex->nHeight);

		int size[2];
		if (!GetFaceLightmapSize(this, i, size)) {
			info = oldInfo;
			return true;
		}

		info = oldInfo;
	}

	return false;
}

float Bsp::get_scale_to_fix_bad_extents(int textureIdx) {
	float bestScale = 1.0f;
	float lastWorkingScale = 1.0f;

	while (has_bad_extents(textureIdx, bestScale)) {
		bestScale -= bestScale > 0.1f ? 0.1f : 0.01f; // coarse adjust
		if (bestScale < 0) {
			bestScale = FLT_MIN;
			break;
		}
	}
	while (!has_bad_extents(textureIdx, bestScale)) {
		lastWorkingScale = bestScale;
		bestScale += 0.01f; // fine tuning
	}
	bestScale = lastWorkingScale - 0.02f; // undo last bad step and add epsilon

	return bestScale;
}

int Bsp::allocblock_reduction() {
	int scaleCount = 0;

	for (int i = 1; i < modelCount; i++) {
		BSPMODEL& model = models[i];

		if (model.nFaces == 0)
			continue;

		bool isVisibleModel = false;
		string usedBy = "";

		for (Entity* ent : ents) {
			if (ent->getBspModelIdx() == i) {
				if (ent->isEverVisible()) {
					isVisibleModel = true;
					break;
				}
				usedBy = ent->getTargetname() + " (" + ent->getClassname() + ")";
			}
		}

		if (isVisibleModel)
			continue;

		bool anyScales = false;
		for (int fa = 0; fa < model.nFaces; fa++) {
			BSPFACE& face = faces[model.iFirstFace + fa];
			BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];
			
			if (info.vS.length() > 0.01f || info.vT.length() > 0.01f) {
				BSPTEXTUREINFO* newinfo = get_unique_texinfo(model.iFirstFace + fa);

				if (info.vS.length() > 0.01f) {
					newinfo->vS = info.vS.normalize(0.01f);
					anyScales = true;
				}
				if (info.vT.length() > 0.01f) {
					newinfo->vT = info.vT.normalize(0.01f);
					anyScales = true;
				}
			}			
		}

		if (anyScales) {
			scaleCount++;
			logf("Scale up textures on model %d used by %s\n", i, usedBy.c_str());
		}
	}

	logf("Scaled up textures on %d invisible models\n", scaleCount);

	return scaleCount;
}

bool Bsp::subdivide_face(int faceIdx, bool dryRunForExtents) {
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
		return false;
	}
	
	int addVerts = polys[0].size() + polys[1].size();

	int addMarks = 0;
	if (!dryRunForExtents) {
		for (int i = 0; i < marksurfCount; i++) {
			if (marksurfs[i] == faceIdx) {
				addMarks++;
			}
		}
	}
	int totalMarks = marksurfCount + addMarks;

	bool limitExceeded = false;
	if (totalMarks > g_limits.max_marksurfaces) {
		logf("Exceeded max marksurfaces while subdividing face\n");
		limitExceeded = true;
	}
	if (faceCount + 1 > g_limits.max_faces) {
		logf("Exceeded max faces while subdividing face\n");
		limitExceeded = true;
	}
	if (edgeCount + addVerts > g_limits.max_edges) {
		logf("Exceeded max edges while subdividing face\n");
		limitExceeded = true;
	}
	if (surfedgeCount + addVerts > g_limits.max_surfedges) {
		logf("Exceeded max edges while subdividing face\n");
		limitExceeded = true;
	}
	if (vertCount + addVerts > g_limits.max_vertexes) {
		logf("Exceeded max vertexes while subdividing face\n");
		limitExceeded = true;
	}

	if (limitExceeded) {
		return false;
	}

	BSPFACE* newFaces = new BSPFACE[faceCount + 1];
	memcpy(newFaces, faces, faceIdx * sizeof(BSPFACE));
	memcpy(newFaces + faceIdx + 1, faces + faceIdx, (faceCount - faceIdx) * sizeof(BSPFACE));

	uint16_t* newMarkSurfs = NULL;
	if (!dryRunForExtents) {
		newMarkSurfs = new uint16_t[totalMarks];
		memcpy(newMarkSurfs, marksurfs, marksurfCount * sizeof(uint16_t));
	}

	BSPEDGE* newEdges = new BSPEDGE[addVerts];
	vec3* newVerts = new vec3[addVerts];
	int32_t* newSurfEdges = new int32_t[addVerts];

	int oldSurfBegin = face.iFirstEdge;
	int oldSurfEnd = face.iFirstEdge + face.nEdges;

	BSPEDGE* edgePtr = newEdges;
	vec3* vertPtr = newVerts;
	int32_t* surfedgePtr = newSurfEdges;

	for (int k = 0; k < 2; k++) {
		vector<vec3>& cutPoly = polys[k];

		newFaces[faceIdx + k] = faces[faceIdx];
		newFaces[faceIdx + k].iFirstEdge = (surfedgePtr - newSurfEdges) + surfedgeCount;
		newFaces[faceIdx + k].nEdges = cutPoly.size();

		int vertOffset = (vertPtr - newVerts) + vertCount;
		int edgeOffset = (edgePtr - newEdges) + edgeCount;

		for (int i = 0; i < cutPoly.size(); i++) {
			edgePtr->iVertex[0] = vertOffset + i;
			edgePtr->iVertex[1] = vertOffset + ((i + 1) % cutPoly.size());
			edgePtr++;

			*vertPtr++ = cutPoly[i];

			// IMPORTANT: Logically it shouldn't matter if you use the first or second index of
			// the edge, but using the first one crashes the software renderer. You will see the
			// face stretching out to infinity before the crash. Doesn't make sense because the
			// same verts are visited and in the same order, just with a different offset.
			*surfedgePtr++ = edgeOffset + i;
		}
	}

	if (!dryRunForExtents) {
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
				memmove(newMarkSurfs + i + 1, newMarkSurfs + i, (totalMarks - (i + 1)) * sizeof(uint16_t));
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
	}

	if (!dryRunForExtents) {
		replace_lump(LUMP_MARKSURFACES, newMarkSurfs, totalMarks * sizeof(uint16_t));
	}

	replace_lump(LUMP_FACES, newFaces, (faceCount + 1)*sizeof(BSPFACE));
	append_lump(LUMP_EDGES, newEdges, addVerts*sizeof(BSPEDGE));
	append_lump(LUMP_SURFEDGES, newSurfEdges, addVerts * sizeof(int32_t));
	append_lump(LUMP_VERTICES, newVerts, addVerts * sizeof(vec3));

	delete[] newEdges;

	return true;
}

int Bsp::get_subdivisions_needed_to_fix_mip_extents(int mip) {
	bool anySubdivides = true;
	
	LumpState oldLumps = duplicate_lumps(
		(1 << LUMP_FACES) | (1 << LUMP_EDGES) | (1 << LUMP_SURFEDGES) | (1 << LUMP_VERTICES)
	);

	int numSub = 0;

	// dry run to see how many subdivisions are needed
	for (int fa = 0; fa < faceCount; fa++) {
		int faceIdx = fa;
		BSPFACE& face = faces[faceIdx];
		BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];

		if (info.nFlags & TEX_SPECIAL || info.iMiptex != mip) {
			continue;
		}

		int size[2];
		if (GetFaceLightmapSize(this, faceIdx, size)) {
			continue;
		}

		if (subdivide_face(faceIdx, true)) {
			anySubdivides = true;
			numSub++;
			fa--;
		}
		else {
			numSub++;
		}
	}

	// undo all changes
	replace_lumps(oldLumps);

	return numSub;
}

void Bsp::fix_all_bad_surface_extents_with_subdivide(int subdivideLimitPerTexture) {
	unordered_set<int> bad_extent_mips;
	for (int fa = 0; fa < faceCount; fa++) {
		BSPFACE& face = faces[fa];
		BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];

		if (info.nFlags & TEX_SPECIAL)
			continue;

		int size[2];
		if (GetFaceLightmapSize(this, fa, size)) {
			continue;
		}

		bad_extent_mips.insert(info.iMiptex);
	}
	
	// count subdivisions needed for each mip
	unordered_map<int, int> bad_extent_mips_count;
	for (int mip : bad_extent_mips) {
		bad_extent_mips_count[mip] = get_subdivisions_needed_to_fix_mip_extents(mip);
	}

	// exclude mips that needed more subdivisions than is allowed
	unordered_set<int> subdivide_mips;
	for (auto item : bad_extent_mips_count) {
		if (item.second < subdivideLimitPerTexture && item.second > 0) {
			BSPMIPTEX* tex = get_texture(item.first);
			if (!tex) {
				continue;
			}
			logf("%d subdivides needed for texture %s (%dx%d)\n", item.second, tex->szName, tex->nWidth, tex->nHeight);
			subdivide_mips.insert(item.first);
		}
	}

	unordered_set<int> repeatErrors;
	int numSub = 0;

	// subdivide again only for the target mips

	for (int fa = 0; fa < faceCount; fa++) {
		int faceIdx = fa;
		BSPFACE& face = faces[faceIdx];
		BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];

		if (info.nFlags & TEX_SPECIAL || !subdivide_mips.count(info.iMiptex)) {
			continue;
		}

		int size[2];
		if (GetFaceLightmapSize(this, faceIdx, size)) {
			continue;
		}

		if (subdivide_face(faceIdx)) {
			numSub++;
			fa--;
		}
		else {
			BSPMIPTEX* tex = get_texture(info.iMiptex);
			if (!tex) {
				continue;
			}
			vec3 center = get_face_center(faceIdx);
			if (!repeatErrors.count(faceIdx)) {
				logf("Failed to subdivide face %d %s (%d %d %d)\n", faceIdx, tex->szName,
					(int)center.x, (int)center.y, (int)center.z);
				repeatErrors.insert(faceIdx);
			}
		}
	}

	logf("Subdivided %d faces across %d textures (%d skipped).\n", numSub, subdivide_mips.size(),
		bad_extent_mips.size() - subdivide_mips.size());
}

int Bsp::fix_bad_surface_extents_with_subdivide(int faceIdx) {
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

		if (subdivide_face(i)) {
			totalFaces++;
			faces.push_back(i + 1);
			faces.push_back(i);
		}
	}

	if (totalFaces != 1) {
		logf("Subdivided face %d into %d faces\n", faceIdx, totalFaces);
	}
	else {
		debugf("Face %d does not need to be subdivided\n", faceIdx);
	}
	
	return totalFaces - 1;
}

void Bsp::fix_bad_surface_extents_with_downscale(int minTextureDim) {
	int numShrink = 0;

	static vector<Wad*> emptyWads;
	vector<Wad*>& wads = g_app->mapRenderer ? g_app->mapRenderer->wads : emptyWads;

	unordered_set<int> bad_extent_mips;

	for (int fa = 0; fa < faceCount; fa++) {
		int faceIdx = fa;
		BSPFACE& face = faces[faceIdx];
		BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];

		int size[2];
		if (GetFaceLightmapSize(this, faceIdx, size)) {
			continue;
		}

		bad_extent_mips.insert(info.iMiptex);
	}

	unordered_set<int> embedded_mips;
	unordered_set<int> resized_mips;
	for (int mip : bad_extent_mips) {
		BSPMIPTEX* tex = get_texture(mip);
		if (!tex) {
			continue;
		}

		if (tex->nOffsets[0] != 0) {
			continue;
		}

		if (tex->nWidth > minTextureDim || tex->nHeight > minTextureDim) {
			embed_texture(mip, wads);
			embedded_mips.insert(mip);
		}
	}

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

		int mip = info.iMiptex;
		if (downscale_texture(mip, minTextureDim, false)) {
			// retry after downscaling
			resized_mips.insert(mip);
			numShrink++;
			fa--;
			continue;
		}
	}

	for (int mip : embedded_mips) {
		if (resized_mips.find(mip) != resized_mips.end()) {
			continue;
		}

		unembed_texture(mip, wads);
	}

	logf("Downscaled %d textures\n", numShrink);
}

int Bsp::count_faces_for_mip(int miptex) {
	int count = 0;

	for (int fa = 0; fa < faceCount; fa++) {
		int faceIdx = fa;
		BSPFACE& face = faces[faceIdx];
		BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];

		if (info.iMiptex == miptex) {
			count++;
		}
	}

	return count;
}

bool Bsp::fix_bad_surface_extents_with_scale(int faceIdx) {
	BSPFACE& face = faces[faceIdx];
	BSPTEXTUREINFO* info = &texinfos[face.iTextureInfo];

	if (info->nFlags & TEX_SPECIAL) {
		return false;
	}

	int size[2];
	if (GetFaceLightmapSize(this, faceIdx, size)) {
		return false;
	}

	info = get_unique_texinfo(faceIdx);

	vec2 oldScale(1.0f / info->vS.length(), 1.0f / info->vT.length());
	BSPTEXTUREINFO oldInfo = *info;

	bool scaledOk = false;
	for (int i = 0; i < 128; i++) {
		info->vS *= 0.5f;
		info->vT *= 0.5f;

		if (GetFaceLightmapSize(this, faceIdx, size)) {
			scaledOk = true;
			break;
		}
	}

	BSPMIPTEX* tex = get_texture(info->iMiptex);
	if (!tex) {
		return false;
	}

	if (!scaledOk) {
		*info = oldInfo;
		logf("Failed to fix face %s with scales %f %f\n", tex->szName, oldScale.x, oldScale.y);
	}
	else {
		vec2 newScale(1.0f / info->vS.length(), 1.0f / info->vT.length());

		vec3 center = get_face_center(faceIdx);
		logf("Scaled up %s from %.2fx%.2f -> %.2fx%.2f (%d %d %d)\n",
			tex->szName, oldScale.x, oldScale.y, newScale.x, newScale.y,
			(int)center.x, (int)center.y, (int)center.z);
	}

	return true;
}

void Bsp::fix_bad_surface_extents_with_scale() {
	int numScale = 0;

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
	

	for (int fa = 0; fa < faceCount; fa++) {
		if (fix_bad_surface_extents_with_scale(fa)) {
			numScale++;
		}
	}

	logf("Scaled up %d face textures\n", numScale);
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

vec3 Bsp::get_face_ut_reference(int faceIdx) {
	BSPFACE& face = faces[faceIdx];
	
	vec3 a, b;
	for (int k = 0; k < face.nEdges && k < 2; k++) {
		int32_t edgeIdx = surfedges[face.iFirstEdge + k];
		BSPEDGE& edge = edges[abs(edgeIdx)];
		int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];
		
		if (k == 0) {
			a = verts[vertIdx];
		}
		else {
			b = verts[vertIdx];
		}
	}

	return (a - b).normalize();
}

int Bsp::get_default_texture_idx() {
	int32_t totalTextures = ((int32_t*)textures)[0];
	for (uint i = 0; i < totalTextures; i++) {
		BSPMIPTEX* tex = get_texture(i);
		if (!tex) {
			continue;
		}
		if (strcmp(tex->szName, "aaatrigger") == 0) {
			return i;
		}
	}

	// add the aaatrigger texture if it doesn't already exist
	byte* tex_dat = NULL;
	uint w, h;

	logf("Added aaatrigger texture\n");
	lodepng_decode24(&tex_dat, &w, &h, aaatrigger_dat, sizeof(aaatrigger_dat));
	int aaatriggerIdx = add_texture("aaatrigger", tex_dat, w, h);
	delete[] tex_dat;

	return aaatriggerIdx;
}

bool Bsp::downscale_texture(int textureId, int newWidth, int newHeight, int resampleMode) {
	if ((newWidth % 16 != 0) || (newHeight % 16 != 0) || newWidth <= 0 || newHeight <= 0) {
		logf("Invalid downscale dimensions: %dx%d\n", newWidth, newHeight);
		return false;
	}

	BSPMIPTEX* tex = get_texture(textureId);
	if (!tex) {
		return false;
	}

	int32_t texOffset = ((int32_t*)textures)[textureId + 1];

	int oldWidth = tex->nWidth;
	int oldHeight = tex->nHeight;

	tex->nWidth = newWidth;
	tex->nHeight = newHeight;

	int lastMipSize = (oldWidth >> 3) * (oldHeight >> 3);
	byte* pixels = (byte*)(textures + texOffset + tex->nOffsets[0]);
	byte* palette = (byte*)(textures + texOffset + tex->nOffsets[3] + lastMipSize);
	COLOR3* paletteColors = (COLOR3*)(palette + 2); // skip color count

	int newWidths[4];
	int newHeights[4];
	int newOffset[4];
	for (int i = 0; i < 4; i++) {
		newWidths[i] = tex->nWidth >> (1 * i);
		newHeights[i] = tex->nHeight >> (1 * i);

		if (i > 0) {
			newOffset[i] = newOffset[i - 1] + newWidths[i - 1] * newHeights[i - 1];
		}
		else {
			newOffset[i] = sizeof(BSPMIPTEX);
		}
	}

	float srcScaleX = (float)oldWidth / tex->nWidth;
	float srcScaleY = (float)oldHeight / tex->nHeight;

	byte* srcPixels = (byte*)(textures + texOffset + tex->nOffsets[0]);
	COLOR3* srcColors = new COLOR3[oldWidth * oldHeight];
	for (int i = 0; i < oldWidth * oldHeight; i++) {
		srcColors[i] = paletteColors[srcPixels[i]];
	}

	COLOR3* dstColors = new COLOR3[newWidth * newHeight];
	vector<COLOR3> newColors = Texture::resample(srcColors, oldWidth, oldHeight, dstColors,
		newWidth, newHeight, resampleMode, tex->szName[0] == '{', paletteColors[255]);

	if (newColors.empty()) {
		for (int i = newColors.size(); i < 256; i++) {
			newColors.push_back(paletteColors[i]);
		}
	}

	// convert pixels to palette indexes
	byte* mip0 = (byte*)(textures + texOffset + newOffset[0]);
	for (int i = 0; i < newWidth * newHeight; i++) {
		for (int k = 0; k < newColors.size(); k++) {
			if (newColors[k] == dstColors[i]) {
				mip0[i] = k;
				break;
			}
		}
	}
	delete[] dstColors;

	// nearest neighbor mipmap resize
	byte* srcData = (byte*)(textures + texOffset + newOffset[0]);
	for (int i = 1; i < 4; i++) {
		byte* dstData = (byte*)(textures + texOffset + newOffset[i]);
		int mipWidth = newWidth >> i;
		int mipHeight = newHeight >> i;
		int mipScale = 1 << i;

		for (int y = 0; y < mipHeight; y++) {
			for (int x = 0; x < mipWidth; x++) {
				dstData[y * mipWidth + x] = srcData[y * mipScale * newWidth + x * mipScale];
			}
		}
	}
	// 2 = palette color count (should always be 256)
	byte* newPalette = (byte*)(textures + texOffset + newOffset[3] + newWidths[3] * newHeights[3]);
	memcpy(newPalette, palette, 2);
	memset(newPalette + 2, 0, sizeof(COLOR3) * 256);
	memcpy(newPalette + 2, &newColors[0], sizeof(COLOR3) * newColors.size());

	for (int i = 0; i < 4; i++) {
		tex->nOffsets[i] = newOffset[i];
	}

	adjust_resized_texture_coordinates(textureId, oldWidth, oldHeight);

	// shrink texture lump
	int removedBytes = palette - newPalette;
	byte* texEnd = newPalette + 256 * sizeof(COLOR3) + 2;
	int shiftBytes = (texEnd - textures) + removedBytes;

	memcpy(texEnd, texEnd + removedBytes, header.lump[LUMP_TEXTURES].nLength - shiftBytes);
	for (int k = textureId + 1; k < textureCount; k++) {
		((int32_t*)textures)[k + 1] -= removedBytes;
	}

	logf("Downscale %s %dx%d -> %dx%d\n", tex->szName, oldWidth, oldHeight, tex->nWidth, tex->nHeight);

	return true;
}

bool Bsp::downscale_texture(int textureId, int minDim, bool allowWad) {
	BSPMIPTEX* tex = get_texture(textureId);
	if (!tex) {
		return false;
	}

	int oldWidth = tex->nWidth;
	int oldHeight = tex->nHeight;
	int newWidth = tex->nWidth;
	int newHeight = tex->nHeight;

	float scale = get_scale_to_fix_bad_extents(textureId);

	if (scale == 1.0f) {
		return false;
	}

	get_scaled_texture_dimensions(textureId, scale, newWidth, newHeight);

	if (max(newWidth, newHeight) < minDim) {
		return false;
	}
	
	if (oldWidth == newWidth && oldHeight == newHeight) {
		logf("Failed to downscale texture %s %dx%d\n", tex->szName, oldWidth, oldHeight);
		return false;
	}

	if (tex->nOffsets[0] == 0) {
		if (allowWad) {
			tex->nWidth = newWidth;
			tex->nHeight = newHeight;
			adjust_resized_texture_coordinates(textureId, oldWidth, oldHeight);
			logf("Texture coords were updated for %s. The WAD texture must be updated separately.\n", tex->szName);
		}
		else {
			logf("Can't downscale WAD texture %s\n", tex->szName);
		}
		
		return false;
	}

	return downscale_texture(textureId, newWidth, newHeight, KernelTypeLanczos3);
}

string Bsp::get_texture_source(string texname, vector<Wad*>& wads) {
	for (int i = 0; i < textureCount; i++) {
		BSPMIPTEX* tex = get_texture(i);
		if (!tex) {
			return name + ".bsp";
		}

		if (tex->nOffsets[0] != 0 && !strcasecmp(tex->szName, texname.c_str())) {
			return name + ".bsp";
		}
	}

	string src;

	for (int k = 0; k < wads.size(); k++) {
		if (wads[k]->hasTexture(texname.c_str())) {
			src = wads[k]->getName();
			break;
		}
	}

	return src;
}

void Bsp::remove_unused_wads(vector<Wad*>& wads) {
	vector<string> wadNames = get_wad_names();
	unordered_set<Wad*> used_wads;

	int missing_textures = 0;

	for (int i = 0; i < textureCount; i++) {
		BSPMIPTEX* tex = get_texture(i);
		if (!tex) {
			continue;
		}

		if (tex->nOffsets[0] == 0) {
			bool foundTexture = false;
			for (int k = 0; k < wads.size(); k++) {
				if (wads[k]->hasTexture(tex->szName)) {
					used_wads.insert(wads[k]);
					foundTexture = true;
					break;
				}
			}
			if (!foundTexture) {
				missing_textures++;
			}
		}
	}

	string newWadList = "";

	for (Wad* wad : wads) {
		if (!used_wads.count(wad)) {
			logf("Removed unused WAD: %s\n", wad->getName().c_str());
		}
		else {
			newWadList += toLowerCase(wad->getName()) + ";";
		}
	}

	logf("Kept %d of %d wads.\n", used_wads.size(), wadNames.size());

	if (missing_textures && wadNames.size() != wads.size()) {
		logf("Warning: The map has missing textures. Missing WADs were removed which may actually be required.\n");
	}

	int worldspawn_count = 0;
	for (int i = 0; i < ents.size(); i++) {
		if (ents[i]->getClassname() == "worldspawn") {
			ents[i]->setOrAddKeyvalue("wad", newWadList);
			break;
		}
	}
}

// returns data for all embedded textures, ready to be wrtten to a WAD
vector<WADTEX> Bsp::get_embedded_textures() {
	vector<WADTEX> wadTextures;

	for (int i = 0; i < textureCount; i++) {
		int32_t offset = ((int32_t*)textures)[i + 1];
		BSPMIPTEX* tex = (BSPMIPTEX*)(textures + offset);

		if (tex->nOffsets[0] == 0) {
			continue; // not embedded
		}

		WADTEX copy;
		memcpy(&copy, tex, sizeof(BSPMIPTEX)); // copy name, offset, dimenions
		int dataSz = copy.getDataSize();
		copy.data = new byte[dataSz];
		memcpy(copy.data, (byte*)tex + tex->nOffsets[0], dataSz);

		wadTextures.push_back(copy);
	}

	return wadTextures;
}

int Bsp::get_texture_id(string name) {
	for (int i = 0; i < textureCount; i++) {
		int32_t offset = ((int32_t*)textures)[i + 1];
		BSPMIPTEX* tex = (BSPMIPTEX*)(textures + offset);

		if (!strcasecmp(tex->szName, name.c_str())) {
			return i;
		}
	}

	return -1;
}

int Bsp::zero_entity_origins(string classname) {
	int moveCount = 0;

	for (int i = 0; i < ents.size(); i++) {
		if (ents[i]->getClassname() == classname) {
			vec3 ori = ents[i]->getOrigin();
			if (ori.x || ori.y || ori.z) {
				int modelIdx = ents[i]->getBspModelIdx();
				if (modelIdx > 0) {
					if (does_model_use_shared_structures(modelIdx)) {
						modelIdx = duplicate_model(modelIdx);
						ents[i]->setOrAddKeyvalue("model", "*" + to_string(modelIdx));
					}

					g_progress.hide = true;
					move(ori, modelIdx);
					g_progress.hide = false;

					ents[i]->setOrAddKeyvalue("origin", "0 0 0");
					moveCount++;
				}
			}
		}
	}

	if (moveCount)
		logf("Zeroed %d %s origins\n", moveCount, classname.c_str());

	return moveCount;
}

vector<string> Bsp::get_wad_names() {
	vector<string> wadNames;

	for (int i = 0; i < ents.size(); i++) {
		if (ents[i]->getClassname() == "worldspawn") {
			wadNames = splitString(ents[i]->getKeyvalue("wad"), ";");

			for (int k = 0; k < wadNames.size(); k++) {
				wadNames[k] = basename(wadNames[k]);
			}
			break;
		}
	}

	return wadNames;
}

bool Bsp::embed_texture(int textureId, vector<Wad*>& wads) {
	BSPMIPTEX* tex = get_texture(textureId);
	if (!tex) {
		return false;
	}

	int32_t texOffset = ((int32_t*)textures)[textureId + 1];

	if (tex->nOffsets[0] != 0) {
		logf("Texture %s is already embedded\n", tex->szName);
		return false;
	}

	bool embedded = false;
	for (int k = 0; k < wads.size(); k++) {
		if (wads[k]->hasTexture(tex->szName)) {
			WADTEX* wadTex = wads[k]->readTexture(tex->szName);

			if (tex->nHeight != wadTex->nHeight || tex->nWidth != wadTex->nWidth) {
				logf("Failed to embed texture %s from wad %s (dimensions don't match)\n", tex->szName, wads[k]->filename.c_str());
				delete wadTex;
				continue;
			}

			for (int i = 0; i < 4; i++) {
				tex->nOffsets[i] = wadTex->nOffsets[i];
			}

			int sz = tex->nWidth * tex->nHeight;	   // miptex 0
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

			logf("Embedded texture %s from wad %s\n", tex->szName, wads[k]->filename.c_str());

			delete wadTex;
			delete[] lumps[LUMP_TEXTURES];
			lumps[LUMP_TEXTURES] = newTexData;
			header.lump[LUMP_TEXTURES].nLength += texDataSz;
			update_lump_pointers();
			embedded = true;

			break;
		}
	}

	if (!embedded) {
		logf("Failed to embed %s. Texture not found in any loaded WAD.\n", tex->szName);
	}

	return embedded;
}

int Bsp::unembed_texture(int textureId, vector<Wad*>& wads, bool force, bool quiet) {
	int32_t texOffset = ((int32_t*)textures)[textureId + 1];

	BSPMIPTEX* tex = get_texture(textureId);
	if (!tex) {
		return 0;
	}

	if (tex->nOffsets[0] == 0) {
		logf("Texture %s is already unembedded\n", tex->szName);
		return 0;
	}

	int sz = tex->nWidth * tex->nHeight;	   // miptex 0
	int sz2 = sz / 4;  // miptex 1
	int sz3 = sz2 / 4; // miptex 2
	int sz4 = sz3 / 4; // miptex 3
	int texDataSz = sz + sz2 + sz3 + sz4 + 2 + 256 * 3 + 2;
	int endOffset = texOffset + sizeof(BSPMIPTEX);
	int newTexBufferSz = header.lump[LUMP_TEXTURES].nLength - texDataSz;

	// reset texture dimensions in case it was edited inside the BSP
	bool wasResized = false;
	bool isInWad = false;
	for (int k = 0; k < wads.size(); k++) {
		if (wads[k]->hasTexture(tex->szName)) {
			WADTEX* wadTex = wads[k]->readTexture(tex->szName);

			if (tex->nWidth != wadTex->nWidth || tex->nHeight != wadTex->nHeight) {
				int oldWidth = tex->nWidth;
				int oldHeight = tex->nHeight;
				tex->nWidth = wadTex->nWidth;
				tex->nHeight = wadTex->nHeight;
				adjust_resized_texture_coordinates(textureId, oldWidth, oldHeight);
				wasResized = true;
			}

			isInWad = true;
			delete wadTex;
			break;
		}
	}
	if (!isInWad && !force) {
		logf("Aborted unembed of %s. No WAD contains this texture. Data would be lost.\n", tex->szName);
		return 0;
	}

	for (int i = 0; i < textureCount; i++) {
		int32_t& offset = ((int32_t*)textures)[i + 1];

		if (offset >= endOffset) {
			offset -= texDataSz;
		}
	}

	for (int i = 0; i < 4; i++) {
		tex->nOffsets[i] = 0;
	}

	byte* newTexData = new byte[newTexBufferSz];
	memcpy(newTexData, lumps[LUMP_TEXTURES], endOffset);
	memcpy(newTexData + endOffset, lumps[LUMP_TEXTURES] + endOffset + texDataSz, newTexBufferSz - endOffset);

	if (!quiet)
		logf("Unembedded texture %s\n", tex->szName);
	delete[] lumps[LUMP_TEXTURES];
	lumps[LUMP_TEXTURES] = newTexData;
	header.lump[LUMP_TEXTURES].nLength -= texDataSz;
	update_lump_pointers();

	return wasResized ? 2 : 1;
}

int Bsp::add_texture_from_wad(WADTEX* tex) {
	((int32_t*)textures)[0]++;

	for (int i = 0; i < textureCount; i++) {
		int32_t& offset = ((int32_t*)textures)[i + 1];
		offset += sizeof(int32_t); // shift after the new header int
	}

	BSPMIPTEX newTex;
	memset(&newTex, 0, sizeof(BSPMIPTEX));
	memcpy(newTex.szName, tex->szName, 16);
	newTex.szName[15] = 0;
	newTex.nOffsets[0] = 0;
	newTex.nOffsets[1] = 0;
	newTex.nOffsets[2] = 0;
	newTex.nOffsets[3] = 0;
	newTex.nWidth = tex->nWidth;
	newTex.nHeight = tex->nHeight;

	int addedSz = sizeof(BSPMIPTEX) + sizeof(int32_t);
	byte* newTexData = new byte[header.lump[LUMP_TEXTURES].nLength + addedSz];
	byte* srcData = lumps[LUMP_TEXTURES];
	byte* dstData = newTexData;

	int headerCopySz = sizeof(int32_t) * (textureCount+1);
	memcpy(dstData, srcData, headerCopySz);
	dstData += headerCopySz;
	srcData += headerCopySz;

	int32_t newOffset = header.lump[LUMP_TEXTURES].nLength + sizeof(int32_t);
	memcpy(dstData, &newOffset, sizeof(int32_t));
	dstData += sizeof(int32_t);

	int oldDataLeft = header.lump[LUMP_TEXTURES].nLength - (srcData - lumps[LUMP_TEXTURES]);
	memcpy(dstData, srcData, oldDataLeft);
	dstData += oldDataLeft;

	memcpy(dstData, &newTex, sizeof(BSPMIPTEX));

	delete[] lumps[LUMP_TEXTURES];
	lumps[LUMP_TEXTURES] = newTexData;
	header.lump[LUMP_TEXTURES].nLength += addedSz;
	update_lump_pointers();

	return textureCount-1;
}

WADTEX Bsp::load_texture(int textureIdx) {
	WADTEX out;
	memset(&out, 0, sizeof(WADTEX));

	if (textureIdx < 0 || textureIdx >= textureCount) {
		return out;
	}

	BSPMIPTEX* tex = get_texture(textureIdx);
	if (!tex) {
		return out;
	}
	memcpy(&out, tex, sizeof(BSPMIPTEX));

	int sz = out.getDataSize();
	out.data = new byte[sz];

	if (tex->nOffsets[0] != 0) {
		// embedded texture
		memcpy(out.data, ((byte*)&tex) + tex->nOffsets[0], sz);
	}
	else {
		// try loading from WAD
		static vector<Wad*> emptyWads;
		vector<Wad*>& wads = g_app->mapRenderer ? g_app->mapRenderer->wads : emptyWads;

		bool foundTex = false;

		for (int k = 0; k < wads.size(); k++) {
			if (wads[k]->hasTexture(tex->szName)) {
				
				WADTEX* wadtex = wads[k]->readTexture(tex->szName);

				if (!wadtex) {
					logf("Failed to read texture %s from WAD: %s\n", wads[k]->filename.c_str());
					continue;
				}

				if (wadtex->nHeight != out.nHeight || wadtex->nWidth != out.nWidth) {
					debugf("Not using texture %s from wad because dimensions don't match: %s\n",
						tex->szName, wads[k]->filename.c_str());
					delete wadtex;
					continue;
				}

				memcpy(&out, wadtex, sizeof(BSPMIPTEX));
				memcpy(out.data, wadtex->data, sz);
				delete wadtex;
				foundTex = true;
				break;
			}
		}

		if (!foundTex) {
			delete[] out.data;
			out.data = NULL;
		}
	}

	return out;
}

bool Bsp::replace_texture(int textureIdx, WADTEX& newtex) {
	BSPMIPTEX* tex = get_texture(textureIdx);
	if (!tex) {
		return false;
	}

	memcpy(newtex.szName, tex->szName, MAXTEXTURENAME);
	tex->szName[0] = 0;

	int newIdx = add_texture(newtex);

	for (int i = 0; i < texinfoCount; i++) {
		BSPTEXTUREINFO& tinfo = texinfos[i];
		if (tinfo.iMiptex == textureIdx) {
			tinfo.iMiptex = newIdx;
		}
	}

	tex = get_texture(textureIdx);
	if (tex && (newtex.nWidth != tex->nWidth || newtex.nHeight != tex->nHeight)) {
		adjust_resized_texture_coordinates(newIdx, tex->nWidth, tex->nHeight);
	}

	return true;
}

void Bsp::adjust_resized_texture_coordinates(BSPFACE& face, BSPTEXTUREINFO& info, int newWidth, int newHeight, int oldWidth, int oldHeight) {
	// scale up face texture coordinates
	float scaleX = newWidth / (float)oldWidth;
	float scaleY = newHeight / (float)oldHeight;

	// get any vert on the face to use a reference point. Why?
	// When textures are scaled, the texture relative to the face will depend on how far away its
	// vertices are from the world origin. This means faces far away from the world origin shift many
	// pixels per scale unit, and faces aligned with the world origin don't shift at all when scaled.
	int32_t edgeIdx = surfedges[face.iFirstEdge];
	BSPEDGE& edge = edges[abs(edgeIdx)];
	int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];
	vec3 vert = verts[vertIdx];

	vec3 oldvs = info.vS;
	vec3 oldvt = info.vT;
	info.vS *= scaleX;
	info.vT *= scaleY;

	// get before/after uv coordinates
	float oldu = (dotProduct(oldvs, vert) + info.shiftS) * (1.0f / (float)oldWidth);
	float oldv = (dotProduct(oldvt, vert) + info.shiftT) * (1.0f / (float)oldHeight);
	float u = dotProduct(info.vS, vert) + info.shiftS;
	float v = dotProduct(info.vT, vert) + info.shiftT;

	// undo the shift in uv coordinates for this face
	info.shiftS += (oldu * newWidth) - u;
	info.shiftT += (oldv * newHeight) - v;
}

void Bsp::adjust_resized_texture_coordinates(int textureId, int oldWidth, int oldHeight) {
	BSPMIPTEX* tex = get_texture(textureId);
	if (!tex) {
		return;
	}
	
	int newWidth = tex->nWidth;
	int newHeight = tex->nHeight;

	for (int i = 0; i < faceCount; i++) {
		BSPFACE& face = faces[i];

		if (texinfos[face.iTextureInfo].iMiptex != textureId)
			continue;

		// each affected face should have a unique texinfo because
		// the shift amount may be different for every face after scaling
		BSPTEXTUREINFO* info = get_unique_texinfo(i);

		adjust_resized_texture_coordinates(face, *info, newWidth, newHeight, oldWidth, oldHeight);
	}
}

int Bsp::downscale_invalid_textures(vector<Wad*>& wads) {
	int count = 0;

	for (int i = 0; i < textureCount; i++) {
		BSPMIPTEX* tex = get_texture(i);
		if (!tex) {
			continue;
		}

		if (tex->nWidth * tex->nHeight > g_limits.max_texturepixels) {
			embed_texture(i, wads);
		}
	}

	for (int i = 0; i < textureCount; i++) {
		BSPMIPTEX* tex = get_texture(i);
		if (!tex) {
			continue;
		}

		if (tex->nWidth * tex->nHeight > g_limits.max_texturepixels) {
			if (tex->nOffsets[0] == 0) {
				logf("Skipping WAD texture %s (failed to embed)\n", tex->szName);
				continue;
			}

			int oldWidth = tex->nWidth;
			int oldHeight = tex->nHeight;
			int newWidth = tex->nWidth;
			int newHeight = tex->nHeight;

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

			downscale_texture(i, newWidth, newHeight, KernelTypeLanczos3);
			count++;
		}
	}

	logf("Downscaled %d textures\n", count);

	return count;
}

bool Bsp::rename_texture(const char* oldName, const char* newName) {
	if (strlen(newName) > 16) {
		logf("ERROR: New texture name longer than 15 characters (%d)\n", strlen(newName));
		return false;
	}

	for (int i = 0; i < textureCount; i++) {
		BSPMIPTEX* tex = get_texture(i);
		if (!tex) {
			continue;
		}
		
		if (!strncmp(tex->szName, oldName, 16)) {
			strncpy(tex->szName, newName, 16);
			logf("Renamed texture '%s' -> '%s'\n", oldName, newName);
			return true;
		}
	}

	logf("No texture found with name '%s'\n", oldName);
	return false;
}

unordered_set<int> Bsp::select_connected_faces(vector<int>& srcFaces, unordered_set<int>& ignoreFaces, bool planarOnly, bool textureOnly) {
	unordered_set<int> selected;
	vector<Polygon3D*> selectedFaces;
	queue<Polygon3D*> testPolys;
	unordered_set<int> validMiptex;
	vector<vec3> validNormals;
	vector<Polygon3D*> polys;
	vector<int> polyModels; // maps a polygon index to a model index

	for (int idx : srcFaces) {
		BSPFACE& face = faces[idx];
		BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];
		BSPPLANE& plane = planes[face.iPlane];

		vector<vec3> selectedVerts;
		for (int e = 0; e < face.nEdges; e++) {
			int32_t edgeIdx = surfedges[face.iFirstEdge + e];
			BSPEDGE& edge = edges[abs(edgeIdx)];
			int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];
			selectedVerts.push_back(verts[vertIdx]);
		}

		Polygon3D* poly = new Polygon3D(selectedVerts, idx, true);
		selectedFaces.push_back(poly);
		testPolys.push(poly);
		validMiptex.insert(info.iMiptex);
		push_unique_vec3(validNormals, plane.vNormal, 0.0001f);
	}

	for (int fa = 0; fa < faceCount; fa++) {
		polyModels.push_back(get_model_from_face(fa));

		BSPFACE& face = faces[fa];

		vector<vec3> faceVerts;

		for (int e = 0; e < face.nEdges; e++) {
			int32_t edgeIdx = surfedges[face.iFirstEdge + e];
			BSPEDGE& edge = edges[abs(edgeIdx)];
			int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];
			faceVerts.push_back(verts[vertIdx]);
		}

		Polygon3D* poly = new Polygon3D(faceVerts, polys.size(), true);

		polys.push_back(poly);
	}

	PolygonOctree* octree = NavMeshGenerator::createPolyOctree(this, polys, 6);

	while (testPolys.size()) {
		Polygon3D* poly = testPolys.front();
		testPolys.pop();

		int srcModel = polyModels[poly->idx];
		unordered_set<int> regionPolys = octree->getPolysInRegion(poly);

		for (int ridx : regionPolys) {
			int idx = polys[ridx]->idx;
			BSPFACE& faceA = faces[idx];

			if (polyModels[idx] != srcModel)
				continue;

			if (selected.count(idx) || ignoreFaces.count(idx))
				continue;

			if (textureOnly && !validMiptex.count(texinfos[faceA.iTextureInfo].iMiptex))
				continue;

			if (planarOnly) {
				BSPPLANE& plane = planes[faceA.iPlane];

				bool isPlanar = false;
				for (const vec3& norm : validNormals) {
					if (plane.vNormal == norm) {
						isPlanar = true;
						break;
					}
				}

				if (!isPlanar)
					continue;
			}

			bool isConnected = false;

			for (int e = 0; e < faceA.nEdges && !isConnected; e++) {
				int32_t edgeIdx = surfedges[faceA.iFirstEdge + e];
				BSPEDGE& edge = edges[abs(edgeIdx)];
				int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];
				const float epsilon = 1.0f;

				vec3& v2 = verts[vertIdx];
				for (const vec3& v1 : poly->verts) {
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
				}

				selected.insert(idx);
				testPolys.push(polys[ridx]);
			}
		}
	}

	for (Polygon3D* poly : polys) {
		delete poly;
	}
	for (Polygon3D* poly : selectedFaces) {
		delete poly;
	}
	delete octree;

	return selected;
}

bool Bsp::is_invisible_solid(Entity* ent) {
	if (!ent->isBspModel())
		return false;

	string tname = ent->getTargetname();
	int rendermode = atoi(ent->getKeyvalue("rendermode").c_str());
	int renderamt = atoi(ent->getKeyvalue("renderamt").c_str());
	int renderfx = atoi(ent->getKeyvalue("renderfx").c_str());

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
		string cname = ents[i]->getClassname();

		if (cname == "env_render") {
			return false; // assume it will affect the brush since it can be moved anywhere
		}
		else if (cname == "env_render_individual") {
			if (ents[i]->getKeyvalue("target") == tname) {
				return false; // assume it's making the ent visible
			}
		}
		else if (cname == "trigger_changevalue") {
			if (ents[i]->getKeyvalue("target") == tname) {
				if (renderKeys.find(ents[i]->getKeyvalue("m_iszValueName")) != renderKeys.end()) {
					return false; // assume it's making the ent visible
				}
			}
		}
		else if (cname == "trigger_copyvalue") {
			if (ents[i]->getKeyvalue("target") == tname) {
				if (renderKeys.find(ents[i]->getKeyvalue("m_iszDstValueName")) != renderKeys.end()) {
					return false; // assume it's making the ent visible
				}
			}
		}
		else if (cname == "trigger_createentity") {
			if (ents[i]->getKeyvalue("+model") == tname || ents[i]->getKeyvalue("-model") == ent->getKeyvalue("model")) {
				return false; // assume this new ent will be visible at some point
			}
		}
		else if (cname == "trigger_changemodel") {
			if (ents[i]->getKeyvalue("model") == ent->getKeyvalue("model")) {
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
			string cname = ents[i]->getClassname();
			if (cname == "info_node" || cname == "info_node_air") {
				continue;
			}
		}

		ent_data << "{\n";

		for (int k = 0; k < ents[i]->keyOrder.size(); k++) {
			string key = ents[i]->keyOrder[k];
			ent_data << "\"" << key << "\" \"" << ents[i]->getKeyvalue(key) << "\"\n";
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

int Bsp::lightstyle_count() {
	int maxStyle = TOGGLED_LIGHT_STYLE_OFFSET-1;

	for (int i = 0; i < ents.size(); i++) {
		string cname = ents[i]->getClassname();

		if (cname.find("light") == 0) {
			int style = atoi(ents[i]->getKeyvalue("style").c_str());
			maxStyle = max(maxStyle, style);
		}
	}
	
	return maxStyle - (TOGGLED_LIGHT_STYLE_OFFSET-1);
}

void Bsp::bake_lightmap(int style) {
	for (int f = 0; f < faceCount; f++) {
		BSPFACE& face = faces[f];

		int baseLightStyle = face.nStyles[0];

		int styleIdx = -1;
		for (int s = 0; s < MAXLIGHTMAPS; s++) {
			if (face.nStyles[s] == style) {
				if (styleIdx != -1) {
					logf("WARNING: Face %d has 2+ lightmaps linked to the same style\n");
				}
				styleIdx = s;
			}
		}

		if (styleIdx == -1)
			continue;

		if (styleIdx == 0) {
			// base lightmap had the style link. Just remove the link and the light stays on
			face.nStyles[0] = 0;
			continue;
		}

		int size[2];
		int dummy[2];
		int imins[2];
		int imaxs[2];
		GetFaceLightmapSize(this, f, size);
		GetFaceExtents(this, f, imins, imaxs);

		int width = size[0];
		int height = size[1];
		int lightmapSz = width * height * sizeof(COLOR3);

		if (face.nStyles[0] > 0 && face.nStyles[0] < 255) {
			// move the toggled base light into this previously toggled style
			// then disable the style link in the base light to keep it on

			int offsetSrc = face.nLightmapOffset + styleIdx * lightmapSz;
			int offsetDst = face.nLightmapOffset;
			COLOR3* lightSrc = (COLOR3*)(lightdata + offsetSrc);
			COLOR3* lightDst = (COLOR3*)(lightdata + offsetDst);
			for (int idx = 0; idx < width * height; idx++) {
				if (offsetSrc + idx * sizeof(COLOR3) < lightDataLength) {
					COLOR3& src = lightSrc[idx];
					COLOR3& dst = lightDst[idx];
					COLOR3 temp = src;
					src = dst;
					dst = temp;
				}
			}

			face.nStyles[styleIdx] = baseLightStyle;
			face.nStyles[0] = 0;
			continue;
		}

		// the base lightmap isn't toggled. The toggled style needs to be added to the base light
		// to disable toggling.
		
		int offset = face.nLightmapOffset + styleIdx * lightmapSz;
		COLOR3* lightSrc = (COLOR3*)(lightdata + offset);
		COLOR3* lightDst = (COLOR3*)(lightdata + face.nLightmapOffset);
		for (int idx = 0; idx < width * height; idx++) {
			if (offset + idx * sizeof(COLOR3) < lightDataLength) {
				COLOR3& src = lightSrc[idx];
				COLOR3& dst = lightDst[idx];
				dst.r = min(255, src.r + dst.r);
				dst.g = min(255, src.g + dst.g);
				dst.b = min(255, src.b + dst.b);
			}
		}

		// keep style refs contiguous
		for (int i = styleIdx; i < MAXLIGHTMAPS - 1; i++) {
			face.nStyles[i] = face.nStyles[i + 1];
		}
		face.nStyles[MAXLIGHTMAPS - 1] = 255;
	}
}

int Bsp::remove_unused_lightstyles() {
	bool usedStyles[256];
	memset(usedStyles, 0, sizeof(bool) * 256);

	for (int i = 0; i < ents.size(); i++) {
		if (ents[i]->getClassname().find("light") == 0) {
			int style = atoi(ents[i]->getKeyvalue("style").c_str());
			if (style > 0 && style < 255)
				usedStyles[style] = true;
		}
	}

	bool deletedStyles[256];
	uint8_t styleRemaps[256];
	for (int i = 0; i < 256; i++) {
		styleRemaps[i] = i;
		deletedStyles[i] = false;
	}

	int lightBakes = 0;
	int lastUsedIdx = 31;
	for (int i = TOGGLED_LIGHT_STYLE_OFFSET; i < 255; i++) {
		if (usedStyles[i]) {
			int gap = (i - lastUsedIdx) - 1;
			if (gap > 0) {
				for (int k = lastUsedIdx + 1; k < i; k++) {
					deletedStyles[k] = true;
					bake_lightmap(k);
					lightBakes++;
				}
				for (int k = i; k < 256; k++) {
					styleRemaps[k] -= gap;
				}
			}
			lastUsedIdx = i;
		}
	}

	for (int k = 0; k < faceCount; k++) {
		BSPFACE& face = faces[k];

		for (int s = 0; s < MAXLIGHTMAPS; s++) {
			uint8_t& style = face.nStyles[s];
			if (style >= TOGGLED_LIGHT_STYLE_OFFSET && style < 255) {
				if (!deletedStyles[style] && style != styleRemaps[style]) {
					style = styleRemaps[style];
				}
			}
		}
	}

	for (int i = 0; i < ents.size(); i++) {
		string cname = ents[i]->getClassname();

		if (cname.find("light") == 0) {
			int style = atoi(ents[i]->getKeyvalue("style").c_str());
			if (style >= TOGGLED_LIGHT_STYLE_OFFSET && style < 255 && style != styleRemaps[style]) {
				ents[i]->setOrAddKeyvalue("style", to_string(styleRemaps[style]));
			}
		}
	}

	return lightBakes;
}

bool Bsp::shift_lightstyles(uint32_t shift) {
	if (lightstyle_count() + shift >= 255) {
		logf("Unable to shift lightstyles by %u without losing some of them\n", shift);
		return false;
	}

	for (int i = 0; i < ents.size(); i++) {
		if (ents[i]->getClassname().find("light") == 0) {
			int style = atoi(ents[i]->getKeyvalue("style").c_str());
			ents[i]->setOrAddKeyvalue("style", to_string(style + shift));
		}
	}

	for (int k = 0; k < faceCount; k++) {
		BSPFACE& face = faces[k];

		for (int s = 0; s < MAXLIGHTMAPS; s++) {
			uint8_t& style = face.nStyles[s];
			if (style >= TOGGLED_LIGHT_STYLE_OFFSET && style < 255) {
				style += shift;
			}
		}
	}

	return true;
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
		//logf("LUMP %10s = %.2f MB\n", g_lump_names[i], (float)header.lump[i].nLength / (1024.0f*1024.0f));
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
	debugf("Bsp version: %d\n", header.nVersion);
	
	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		fin.read((char*)&header.lump[i], sizeof(BSPLUMP));
		debugf("Read lump id: %d. Len: %d. Offset %d.\n", i,header.lump[i].nLength,header.lump[i].nOffset);
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

			if (ent->hasKey("classname"))
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
				ent->setOrAddKeyvalue(k.key, k.value);
		}
	}

	if (ents.size() > 1)
	{
		if (ents[0]->getClassname() != "worldspawn")
		{
			logf("First entity has classname different from 'woldspawn', we do fixup it\n");
			for (int i = 1; i < ents.size(); i++)
			{
				if (ents[i]->getClassname() == "worldspawn")
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
		&& lightstyle_count() < g_limits.max_lightstyles
		&& ceilf(calc_allocblock_usage()) <= g_limits.max_allocblocks;
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
		&& lightstyle_count() < 255;
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
	int numBadRadTexture = 0;
	int numBadSubdivides = 0;
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
		logf("Bad Surface Extents on %d faces\n", numBadExtent);
		isValid = false;
	}
	if (numBadRadTexture) {
		logf("%d faces have invalid RAD textures. VHLT will complain about malformed faces.\n", numBadRadTexture);
		isValid = false;
	}
	if (numBadSubdivides) {
		logf("Bad v5 subdivides detected on %d faces. These crash the software renderer. (fixed!)\n", numBadSubdivides);
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

			get_model_hull_bounds(i, 0, models[i].nMins, models[i].nMaxs);
			logf("    Recalculated as Mins: (%f, %f, %f) Maxs: (%f %f %f)\n", i,
				models[i].nMins.x, models[i].nMins.y, models[i].nMins.z,
				models[i].nMaxs.x, models[i].nMaxs.y, models[i].nMaxs.z);
			isValid = false;
		}

		STRUCTUSAGE usage = STRUCTUSAGE(this);
		mark_model_structures(i, &usage, false);
		usage.compute_sum();
		if (usage.sum.faces != models[i].nFaces) {
			//logf("Bad face count in model %d: %d / %d (fixed)\n", i, usage.sum.faces, models[i].nFaces);
			logf("Bad face count in model %d: %d / %d\n", i, usage.sum.faces, models[i].nFaces);
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
		if (ents[i]->getClassname() == "worldspawn") {
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

	int oobCount = 0;
	int badOriginCount = 0;
	int badModelRefCount = 0;
	int missingBspModelCount = 0;
	for (Entity* ent : ents) {
		vec3 ori = ent->getOrigin();
		//float oob = g_engine_limits->max_mapboundary;
		float oob = 8192;

		if (ori.x || ori.y || ori.z) {
			string cname = ent->getClassname();
			if (cname == "func_ladder" || cname == "func_water" || cname == "func_mortar_field") {
				badOriginCount++;
			}
		}

		if (fabs(ori.x) > oob || fabs(ori.y) > oob || fabs(ori.z) > oob) {
			/*
			logf("Entity '%s' (%s) outside map boundary at (%d %d %d)\n",
				ent->hasKey("targetname") ? ent->getKeyvalue("targetname").c_str() : "",
				ent->hasKey("classname") ? ent->getKeyvalue("classname").c_str() : "",
				(int)ori.x, (int)ori.y, (int)ori.z);
				*/
			oobCount++;
		}
		if (ent->getBspModelIdx() >= modelCount) {
			badModelRefCount++;
		}
		FgdClass* fgd = g_app->mergedFgd ? g_app->mergedFgd->getFgdClass(ent->getClassname()) : NULL;
		if (fgd && fgd->classType == FGD_CLASS_SOLID && ent->getBspModelIdx() < 0) {
			missingBspModelCount++;
		}
	}
	if (missingBspModelCount) {
		logf("%d solid entities have no model key set\n", missingBspModelCount);
	}
	if (badModelRefCount) {
		logf("%d entities have invalid BSP model references\n", badModelRefCount);
	}
	if (oobCount) {
		logf("%d entities outside of the map boundaries\n", oobCount);
	}
	if (badOriginCount) {
		logf("%d entities have origins that may cause problems (see \"Zero Entity Origins\" tool)\n", badOriginCount);
	}

	for (int i = 0; i < textureCount; i++) {
		BSPMIPTEX* tex = get_texture(i);
		if (!tex) {
			int32_t texOffset = ((int32_t*)textures)[i + 1];
			logf("Invalid offset %d for texture ID %d\n", texOffset, i);
			continue;
		}

		if (tex->nWidth * tex->nHeight > g_limits.max_texturepixels) {
			logf("Texture '%s' too large (%dx%d)\n", tex->szName, tex->nWidth, tex->nHeight);
		}
	}

	int missing_textures = count_missing_textures();
	if (missing_textures) {
		logf("%d missing textures\n", missing_textures);
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
		logf("NODE %d (%.2f, %.2f, %.2f) @ %.2f\n", iNode, plane.vNormal.x, plane.vNormal.y, plane.vNormal.z, plane.fDist);
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
			return "\"" + ents[i]->getTargetname() + "\" (" + ents[i]->getClassname() + ")";
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
		print_leaf(~nodeIdx);
		return;
	}
	else {
		print_node(nodeIdx);
		logf("\n");
	}
	
	recurse_node(nodes[nodeIdx].iChildren[0], depth+1);
	recurse_node(nodes[nodeIdx].iChildren[1], depth+1);
}

void Bsp::print_node(int nodeidx) {
	BSPNODE& node = nodes[nodeidx];
	BSPPLANE& plane = planes[node.iPlane];

	logf("Node %d, Plane (%f %f %f) d: %f, Faces: %d, Min(%d, %d, %d), Max(%d, %d, %d)",
		nodeidx,
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

int Bsp::get_leaf_from_face(int faceIdx) {
	for (int i = 0; i < leafCount; i++) {
		BSPLEAF& leaf = leaves[i];

		for (int k = 0; k < leaf.nMarkSurfaces; k++) {
			if (marksurfs[leaf.iFirstMarkSurface + k] == faceIdx) {
				return i;
			}
		}
	}

	return -1;
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
				if ((pvs[p] & bit) && lf < leafCount && lf < models[0].nVisLeafs) // leaf is flagged as visible
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
		if (leafIdx-1 >= leafCount) {
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
		if (leafIdx-1 >= visLeafCount) {
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

vector<int> Bsp::get_connected_leaves(LeafNavMesh* mesh, const vector<int>& ileaves, const unordered_set<int>& ignoreLeaves) {
	unordered_set<int> visited;
	queue<int> searchNodes;
	vector<int> connected;

	for (int ileaf : ileaves) {
		searchNodes.push(mesh->leafMap[ileaf]);
		visited.insert(mesh->leafMap[ileaf]);
	}

	for (int ileaf : ignoreLeaves) {
		visited.insert(mesh->leafMap[ileaf]);
	}

	while (searchNodes.size()) {
		int idx = searchNodes.front();
		searchNodes.pop();

		if (idx == NAV_INVALID_IDX) {
			logf("Invalid node in selection\n");
			continue; // should never happen
		}

		LeafNode& node = mesh->nodes[idx];

		for (LeafLink& link : node.links) {
			if (visited.count(link.node)) {
				continue;
			}

			searchNodes.push(link.node);
			visited.insert(link.node);
			connected.push_back(mesh->nodes[link.node].leafIdx);
		}
	}

	return connected;
}

int Bsp::get_node_branch(int iNode, vector<int>& branch, int ileaf) {
	BSPNODE& node = nodes[iNode];

	for (int i = 0; i < 2; i++) {
		if (node.iChildren[i] >= 0) {
			int n = get_node_branch(node.iChildren[i], branch, ileaf);

			if (n != -1) {
				branch.push_back(iNode);
				return n;
			}
		}
		else if (~node.iChildren[i] == ileaf) {
			branch.push_back(ileaf);
			branch.push_back(iNode);
			return iNode;
		}
	}

	return -1;
}

void Bsp::merge_leaves_broken(int ileafa, int ileafb) {
	// TODO: This doesn't work unless the leaves are nearby in the BSP tree.
	// Their common ancestor node probably needs to have the common faces appended, which will
	// slow down rendering a lot if merging thousands of leaves, because in a lot of cases the
	// lowest common ancestor will be leaf 0. Better to move faces to a new model if there are
	// too many leaves. See below for a note on where i left off.

	BSPLEAF& leafa = leaves[ileafa];
	BSPLEAF& leafb = leaves[ileafb];

	// expand the bounding boxes
	int inode = models[0].iHeadnodes[0];
	vector<int> brancha;
	vector<int> branchb;
	int parenta = get_node_branch(inode, brancha, ileafa);
	int parentb = get_node_branch(inode, branchb, ileafb);

	if (parenta == -1 || parentb == -1) {
		logf("Failed to find parent node for one of the leaves\n");
		return;
	}

	BSPNODE& nodea = nodes[parenta];
	BSPNODE& nodeb = nodes[parentb];

	for (int i = 0; i < 3; i++) {
		leafa.nMins[i] = min(leafa.nMins[i], leafb.nMins[i]);
		leafa.nMaxs[i] = max(leafa.nMaxs[i], leafb.nMaxs[i]);

		// test
		leafa.nMins[i] = -16384;
		leafa.nMaxs[i] = 16384;

		nodes[parenta].nMins[i] = -16384;
		nodes[parenta].nMaxs[i] = 16384;
		nodes[parentb].nMins[i] = -16384;
		nodes[parentb].nMaxs[i] = 16384;
	}

	unordered_set<int> replace;
	replace.insert(ileafb);
	replace_leaves(models[0].iHeadnodes[0], replace, ileafa);

	unordered_set<int> existingFaces;
	vector<int> allFaces;
	for (int i = 0; i < leafa.nMarkSurfaces; i++) {
		existingFaces.insert(marksurfs[leafa.iFirstMarkSurface + i]);
		allFaces.push_back(marksurfs[leafa.iFirstMarkSurface + i]);
	}

	vector<int> newFaces;
	for (int i = 0; i < leafb.nMarkSurfaces; i++) {
		int face = marksurfs[leafb.iFirstMarkSurface + i];

		if (!existingFaces.count(face)) {
			newFaces.push_back(face);
			allFaces.push_back(face);
		}
	}

	// find the lowest common parent node
	int irootNode = 0;
	for (int i = 0; i < brancha.size() && i < branchb.size(); i++) {
		int na = brancha[(brancha.size() - 1) - i];
		int nb = branchb[(branchb.size() - 1) - i];

		if (na == nb) {
			irootNode = na;
		}
		else {
			break; // branches split off
		}
	}
	BSPNODE& root = nodes[irootNode];
	int oldRootFaceCount = root.nFaces;
	
	// add new faces to the parent node's face list
	// 
	// TODO: the entire leaf is now invisible when viewed from certain angles. Something to do with node planes?
	// The issue is that renderer marks the entire node branch for a leaf to render faces piecemeal.
	// Only one branch will be ascended if 2 leaves are in different branches. Leaves only mark faces
	// for visibility. Nodes do the actual rendering. Each node will only have a few faces while leaves
	// may have much more. It's not enough simply to add faces to a leaf. The entire node branch should
	// be updated as well, which probably wouldn't make sense anymore if the faces aren't associated with
	// the clipping planes that define the tree. Try a better hack to reduce world leaves.
	{
		// add all faces in the merged leaf to the common parent, so that all faces are considered
		// for rendering no matter which branch was taken upward to reach the parent.
		// This may reduce performance a lot.
		BSPFACE* newFaceLump = new BSPFACE[faceCount + allFaces.size()];
		int copyEnd = root.firstFace + root.nFaces;
		memcpy(newFaceLump, faces, copyEnd * sizeof(BSPFACE));
		memcpy(newFaceLump + copyEnd + allFaces.size(), faces + copyEnd, (faceCount - copyEnd) * sizeof(BSPFACE));

		// copy leafb faces into leafa's parent node face list
		for (int i = 0; i < allFaces.size(); i++) {
			newFaceLump[root.firstFace + root.nFaces + i] = faces[allFaces[i]];
		}

		// shift face indexes in other structures
		for (int i = 0; i < nodeCount; i++) {
			if (nodes[i].firstFace >= root.firstFace + root.nFaces) {
				nodes[i].firstFace += allFaces.size();
			}
		}
		for (int i = 0; i < marksurfCount; i++) {
			if (marksurfs[i] >= root.firstFace + root.nFaces) {
				marksurfs[i] += allFaces.size();
			}
		}
		for (int i = 0; i < modelCount; i++) {
			if (models[i].iFirstFace >= root.firstFace + root.nFaces) {
				models[i].iFirstFace += allFaces.size();
			}
		}
		models[0].nFaces += allFaces.size();

		for (int i = 0; i < 3; i++) {
			root.nMins[i] = -16384;
			root.nMaxs[i] = 16384;
		}

		root.nFaces += allFaces.size();
		logf("Node %d face count is %d (+%d)\n", irootNode, root.nFaces, allFaces.size());

		replace_lump(LUMP_FACES, newFaceLump, (faceCount + allFaces.size())*sizeof(BSPFACE));
	}

	uint16* newMarkSurfs = new uint16[marksurfCount + newFaces.size()];
	int copyEnd = leafa.iFirstMarkSurface + leafa.nMarkSurfaces;

	// copy old data in, leaving a gap for the new faces being added to leafa
	memcpy(newMarkSurfs, marksurfs, copyEnd*sizeof(uint16));
	memcpy(newMarkSurfs + copyEnd + newFaces.size(), marksurfs + copyEnd, (marksurfCount - copyEnd) * sizeof(uint16));

	// shift surface mark indexes
	for (int i = 0; i < leafCount; i++) {
		BSPLEAF& leaf = leaves[i];
		if (leaf.iFirstMarkSurface >= leafa.iFirstMarkSurface + leafa.nMarkSurfaces) {
			leaf.iFirstMarkSurface += newFaces.size();
		}
	}

	// use the new faces to leafa
	leafa.nMarkSurfaces = allFaces.size();
	for (int i = 0; i < leafa.nMarkSurfaces; i++) {
		newMarkSurfs[leafa.iFirstMarkSurface + i] = root.firstFace + oldRootFaceCount + i;
	}

	// unmark the merged leaf faces
	leafb.nMarkSurfaces = 0;
	leafb.iFirstMarkSurface = 0;

	replace_lump(LUMP_MARKSURFACES, newMarkSurfs, (marksurfCount + newFaces.size()) * sizeof(uint16));

	// merge PVS
	int visLeafCount = leafCount - 1;
	uint visRowSize = ((visLeafCount + 63) & ~63) >> 3;

	int decompressedVisSize = visLeafCount * visRowSize;
	byte* decompressedVis = new byte[decompressedVisSize];
	memset(decompressedVis, 0, decompressedVisSize);
	decompress_vis_lump(leaves, lumps[LUMP_VISIBILITY], visDataLength, decompressedVis, visLeafCount);

	byte* visRowa = decompressedVis + (ileafa - 1) * visRowSize;
	byte* visRowb = decompressedVis + (ileafb - 1) * visRowSize;

	// all leaves visible from B should now also be visible from A
	for (int k = 1; k < visRowSize; k++) {
		visRowa[k] |= visRowb[k];
	}

	// all leaves that could see B should now also see A
	int oldLeafIdx = ileafb - 1;
	int oldByteOffset = oldLeafIdx / 8;
	int oldBitOffset = 1 << (oldLeafIdx % 8);
	int newLeafIdx = ileafa - 1;
	int newByteOffset = newLeafIdx / 8;
	int newBitOffset = 1 << (newLeafIdx % 8);

	for (int i = 1; i < visLeafCount; i++) {
		byte* visRow = decompressedVis + (i - 1) * visRowSize;

		if (visRow[oldByteOffset] & oldBitOffset) {
			visRow[newByteOffset] |= newBitOffset;
		}

		//memset(visRow, 0xff, visRowSize); // test
	}

	byte* compressedVis = new byte[decompressedVisSize]; // assuming compressed will reduce size
	memset(compressedVis, 0, decompressedVisSize);
	int newVisLen = CompressAll(leaves, decompressedVis, compressedVis, visLeafCount, decompressedVisSize);

	byte* compressedVisResized = new byte[newVisLen];
	memcpy(compressedVisResized, compressedVis, newVisLen);

	replace_lump(LUMP_VISIBILITY, compressedVisResized, newVisLen);

	delete[] decompressedVis;
	delete[] compressedVis;
}

void Bsp::merge_leaves(vector<int>& ileaves) {
	if (ileaves.empty())
		return;

	uint16_t rootIdx = ileaves[0];
	BSPLEAF& root = leaves[rootIdx];
	unordered_set<int> ileafSet;

	int visLeafCount = leafCount - 1;
	uint visRowSize = ((visLeafCount + 63) & ~63) >> 3;

	int decompressedVisSize = visLeafCount * visRowSize;
	byte* decompressedVis = new byte[decompressedVisSize];
	memset(decompressedVis, 0, decompressedVisSize);
	decompress_vis_lump(leaves, lumps[LUMP_VISIBILITY], visDataLength, decompressedVis, visLeafCount);

	byte* visRowRoot = decompressedVis + (rootIdx - 1) * visRowSize;

	int rootLeafIdx = rootIdx - 1;
	int rootByteOffset = rootLeafIdx / 8;
	int rootBitOffset = 1 << (rootLeafIdx % 8);

	for (int idx : ileaves) {
		if (idx == rootIdx)
			continue;

		BSPLEAF& leaf = leaves[idx];

		ileafSet.insert(idx);

		// expand bounding box
		for (int i = 0; i < 3; i++) {
			root.nMins[i] = min(root.nMins[i], leaf.nMins[i]);
			root.nMaxs[i] = max(root.nMaxs[i], leaf.nMaxs[i]);
		}

		// remove faces from visibility
		leaf.nMarkSurfaces = 0;
		leaf.iFirstMarkSurface = 0;

		// merge PVS
		byte* visRow = decompressedVis + (idx - 1) * visRowSize;

		// all leaves visible from this leaf should now also be visible in the root leaf
		for (int k = 1; k < visRowSize; k++) {
			visRowRoot[k] |= visRow[k];
		}

		// all leaves that could see this leaf should now also see the root
		int mergedLeafIdx = idx - 1;
		int mergedByteOffset = mergedLeafIdx / 8;
		int mergedBitOffset = 1 << (mergedLeafIdx % 8);

		for (int i = 1; i < visLeafCount; i++) {
			byte* visRow = decompressedVis + (i - 1) * visRowSize;

			if (visRow[mergedByteOffset] & mergedBitOffset) {
				visRow[rootByteOffset] |= rootBitOffset;
			}
		}
	}

	replace_leaves(models[0].iHeadnodes[0], ileafSet, rootIdx);

	byte* compressedVis = new byte[decompressedVisSize]; // assuming compressed will reduce size
	memset(compressedVis, 0, decompressedVisSize);
	int newVisLen = CompressAll(leaves, decompressedVis, compressedVis, visLeafCount, decompressedVisSize);

	byte* compressedVisResized = new byte[newVisLen];
	memcpy(compressedVisResized, compressedVis, newVisLen);

	replace_lump(LUMP_VISIBILITY, compressedVisResized, newVisLen);

	delete[] decompressedVis;
	delete[] compressedVis;
}

// returns all faces marked by the given leaf
vector<int> Bsp::get_leaf_faces(int ileaf) {
	BSPLEAF& leaf = leaves[ileaf];

	vector<int> faces;

	for (int i = 0; i < leaf.nMarkSurfaces; i++) {
		faces.push_back(marksurfs[leaf.iFirstMarkSurface + i]);
	}

	return faces;
}

void Bsp::replace_leaves(int iNode, unordered_set<int>& replace, int dstLeaf) {
	BSPNODE& node = nodes[iNode];

	for (int i = 0; i < 2; i++) {
		if (node.iChildren[i] >= 0) {
			replace_leaves(node.iChildren[i], replace, dstLeaf);
		}
		else if (replace.count(~node.iChildren[i])) {
			node.iChildren[i] = ~dstLeaf;
		}
	}
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
		if (model.iHeadnodes[k] >= 0 && model.iHeadnodes[k] < clipnodeCount)
			mark_clipnode_structures(model.iHeadnodes[k], usage);
	}
}

void Bsp::unlink_model_leaf_faces(int modelIdx) {
	BSPMODEL& model = models[modelIdx];

	if (model.iHeadnodes[0] >= 0 && model.iHeadnodes[0] < nodeCount)
		unlink_model_leaf_faces_by_node(model.iHeadnodes[0]);
}

void Bsp::unlink_model_leaf_faces_by_node(int iNode) {
	BSPNODE& node = nodes[iNode];

	for (int i = 0; i < 2; i++) {
		if (node.iChildren[i] >= 0) {
			unlink_model_leaf_faces_by_node(node.iChildren[i]);
		}
		else {
			BSPLEAF& leaf = leaves[~node.iChildren[i]];

			// submodels don't use the faces linked to leaves, so deleting the references
			// should cause no harm...
			leaf.iFirstMarkSurface = 0;
			leaf.nMarkSurfaces = 0;
		}
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

int Bsp::find_texture(const char* name) {
	if (!name || name[0] == '\0')
		return -1;

	for (int i = 0; i < textureCount; i++) {
		int32_t oldOffset = ((int32_t*)textures)[i + 1];
		BSPMIPTEX* oldTex = (BSPMIPTEX*)(textures + oldOffset);
		if (strcasecmp(name, oldTex->szName) == 0) {
			return i;
		}
	}

	return -1;
}

int Bsp::add_texture(const char* texname, byte* data, int width, int height) {
	if (width % 16 != 0 || height % 16 != 0) {
		logf("Texture %s dimensions are not divisible by 16.", texname);
		return -1;
	}
	if (width * height > g_limits.max_texturepixels) {
		logf("Texture %s is too big to add.", texname);
		return -1;
	}

	int existingIdx = find_texture(texname);

	if (existingIdx != -1)
	{
		debugf("A texture with the name %s already exists in this map.\n", texname);
		return existingIdx;
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
		texDataSize += mipWidth * mipHeight;
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
	strncpy(newMipTex->szName, texname, MAXTEXTURENAME);
	
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

int Bsp::add_texture(WADTEX texture) {
	int existingIdx = find_texture(texture.szName);

	if (existingIdx != -1)
	{
		debugf("A texture with the name %s already exists in this map.\n", texture.szName);
		return existingIdx;
	}

	int newTexLumpSize = header.lump[LUMP_TEXTURES].nLength + sizeof(int32_t) + sizeof(BSPMIPTEX) + texture.getDataSize();
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
	int oldTexDatSize = header.lump[LUMP_TEXTURES].nLength - (textureCount + 1) * sizeof(int32_t);
	memcpy(newTexData + newTexHeaderSize, lumps[LUMP_TEXTURES] + oldTexHeaderSize, oldTexDatSize);

	// add new texture to the end of the lump
	int newTexOffset = newTexHeaderSize + oldTexDatSize;
	newLumpHeader[textureCount + 1] = newTexOffset;

	memcpy(newTexData + newTexOffset, (BSPMIPTEX*)&texture, sizeof(BSPMIPTEX));
	memcpy(newTexData + newTexOffset + sizeof(BSPMIPTEX), texture.data, texture.getDataSize());

	replace_lump(LUMP_TEXTURES, newTexData, newTexLumpSize);

	logf("Embedded new texture: %s (%dx%d)\n", texture.szName, texture.nWidth, texture.nHeight);

	return textureCount - 1;
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

int Bsp::create_node() {
	BSPNODE* newNodes = new BSPNODE[nodeCount + 1];
	memcpy(newNodes, nodes, nodeCount * sizeof(BSPNODE));

	BSPNODE& newNode = newNodes[nodeCount];
	memset(&newNode, 0, sizeof(BSPNODE));

	replace_lump(LUMP_NODES, newNodes, (nodeCount + 1) * sizeof(BSPNODE));

	return nodeCount - 1;
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

	// will fix "bad model face sum" after a clean due to old face references in leaves that weren't duplicated
	unlink_model_leaf_faces(newModelIdx);

	return newModelIdx;
}

int Bsp::create_model_from_faces(vector<int>& faceIndexes) {
	BSPFACE* newFaces = new BSPFACE[faceCount + faceIndexes.size()];
	memcpy(newFaces, faces, faceCount * sizeof(BSPFACE));

	vec3 min(FLT_MAX, FLT_MAX, FLT_MAX);
	vec3 max(-FLT_MAX, -FLT_MAX, -FLT_MAX);

	for (int i = 0; i < faceIndexes.size(); i++) {
		BSPFACE& face = faces[faceIndexes[i]];
		
		for (int e = face.iFirstEdge; e < face.iFirstEdge + face.nEdges; e++) {
			int32_t edgeIdx = surfedges[e];
			BSPEDGE& edge = edges[abs(edgeIdx)];
			vec3 v = edgeIdx >= 0 ? verts[edge.iVertex[1]] : verts[edge.iVertex[0]];
			expandBoundingBox(v, min, max);
		}

		newFaces[faceCount + i] = face;
	}

	int oldFaceCount = faceCount;
	replace_lump(LUMP_FACES, newFaces, (faceCount + faceIndexes.size())*sizeof(BSPFACE));

	int modelIdx = create_model();
	BSPMODEL& newModel = models[modelIdx];

	newModel.iFirstFace = oldFaceCount;
	newModel.nFaces = faceIndexes.size();

	// no collision - completely solid
	for (int i = 0; i < MAX_MAP_HULLS; i++) {
		newModel.iHeadnodes[0] = -1;
	}

	newModel.iHeadnodes[0] = create_node();
	BSPNODE& newNode = nodes[newModel.iHeadnodes[0]];
	newNode.firstFace = newModel.iFirstFace;
	newNode.nFaces = newModel.nFaces;
	newNode.iChildren[0] = -1;
	newNode.iChildren[1] = -1;
	newNode.nMins[0] = min.x;
	newNode.nMins[1] = min.y;
	newNode.nMins[2] = min.z;
	newNode.nMaxs[0] = max.x;
	newNode.nMaxs[1] = max.y;
	newNode.nMaxs[2] = max.z;
	
	newModel.nMins = min;
	newModel.nMaxs = max;

	return modelIdx;
}

int Bsp::convert_leaves_to_model(vector<int>& leafIndexes) {
	// create a new model from leaf faces
	unordered_set<int> markedFaces;
	vector<int> allLeafFaces;
	for (int i = 0; i < leafIndexes.size(); i++) {
		vector<int> leafFaces = get_leaf_faces(leafIndexes[i]);

		for (int k = 0; k < leafFaces.size(); k++) {
			if (markedFaces.count(leafFaces[k]))
				continue;

			allLeafFaces.push_back(leafFaces[k]);
			markedFaces.insert(leafFaces[k]);
		}
	}

	int modelIdx = create_model_from_faces(allLeafFaces);

	merge_leaves(leafIndexes);

	return modelIdx;
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
		+ sizeof(BSPMODEL) + sizeof(uint32_t)*13;

	uint8_t* serialBytes = new uint8_t[totalBytes];
	mstream data = mstream((char*)serialBytes, totalBytes);

	data.write(&dataVersion, sizeof(uint32_t));
	data.write(&dataType, sizeof(uint32_t));

	data.write(&planeCount, sizeof(uint32_t));
	data.write(&planes[0], planeCount*sizeof(BSPPLANE));

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
	if (lightmapsCount > 1024*1024*64) {
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
				node.iChildren[k] = ~((int16_t)model.leaves.size());
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

	return model.serialize();
}

int Bsp::add_model(string serialized) {
	BspModelData dat;
	if (!dat.deserialize(serialized)) {
		return -1;
	}

	for (int i = 0; i < dat.edges.size(); i++) {
		dat.edges[i].iVertex[0] += vertCount;
		dat.edges[i].iVertex[1] += vertCount;
	}

	for (int i = 0; i < dat.surfEdges.size(); i++) {
		int32_t& surfedge = dat.surfEdges[i];
		if (surfedge >= 0) {
			surfedge += edgeCount;
		}
		else {
			surfedge -= edgeCount;
		}
	}

	vector<int> textureIndexes;
	for (WADTEX& tex : dat.textures) {
		textureIndexes.push_back(add_texture(tex));
	}

	for (int i = 0; i < dat.texinfos.size(); i++) {
		BSPTEXTUREINFO& tinfo = dat.texinfos[i];
		tinfo.iMiptex = textureIndexes[tinfo.iMiptex];
	}

	for (int i = 0; i < dat.faces.size(); i++) {
		BSPFACE& face = dat.faces[i];
		face.iFirstEdge += surfedgeCount;
		face.iPlane += planeCount;
		face.iTextureInfo += texinfoCount;
		face.nLightmapOffset += lightDataLength;
	}

	for (int i = 0; i < dat.nodes.size(); i++) {
		BSPNODE& node = dat.nodes[i];
		node.firstFace += faceCount;
		node.iPlane += planeCount;

		for (int k = 0; k < 2; k++) {
			if (node.iChildren[k] >= 0) {
				node.iChildren[k] += nodeCount;
			}
			else {
				int16_t leafidx = ~node.iChildren[k];
				leafidx += leafCount;
				node.iChildren[k] = ~leafidx;
			}
		}
	}

	for (int i = 0; i < dat.clipnodes.size(); i++) {
		BSPCLIPNODE& clipnode = dat.clipnodes[i];
		clipnode.iPlane += planeCount;

		for (int k = 0; k < 2; k++) {
			if (clipnode.iChildren[k] >= 0) {
				clipnode.iChildren[k] += clipnodeCount;
			}
		}
	}

	for (int i = 0; i < dat.leaves.size(); i++) {
		BSPLEAF& leaf = dat.leaves[i];
		leaf.iFirstMarkSurface = 0;
		leaf.nMarkSurfaces = 0;
		leaf.nVisOffset = 0;
	}

	dat.model.iFirstFace += faceCount;
	dat.model.iHeadnodes[0] += nodeCount;
	for (int i = 1; i < MAX_MAP_HULLS; i++) {
		dat.model.iHeadnodes[i] += clipnodeCount;
	}
	dat.model.nVisLeafs = 0; // techinically should match the old model, but leaves aren't duplicated yet
	
	//
	// Validate data
	//

	bool invalidRefs = false;
	for (int i = 0; i < dat.edges.size(); i++) {
		for (int k = 0; k < 2; k++)
			invalidRefs |= dat.edges[i].iVertex[k] >= vertCount + dat.verts.size();
	}

	for (int i = 0; i < dat.surfEdges.size(); i++) {
		invalidRefs |= abs(dat.surfEdges[i]) >= edgeCount + dat.edges.size();
	}

	for (int i = 0; i < dat.faces.size(); i++) {
		BSPFACE& face = dat.faces[i];
		invalidRefs |= face.iFirstEdge >= surfedgeCount + dat.surfEdges.size();
		invalidRefs |= face.iPlane >= planeCount + dat.planes.size();
		invalidRefs |= face.iTextureInfo >= texinfoCount + dat.texinfos.size();
		invalidRefs |= face.nLightmapOffset >= lightDataLength + dat.lightmaps.size()*sizeof(COLOR3);
	}

	for (int i = 0; i < dat.nodes.size(); i++) {
		BSPNODE& node = dat.nodes[i];
		invalidRefs |= node.firstFace >= surfedgeCount + dat.surfEdges.size();
		invalidRefs |= node.iPlane >= planeCount + dat.planes.size();

		for (int k = 0; k < 2; k++) {
			if (node.iChildren[k] >= 0) {
				invalidRefs |= node.iChildren[k] >= nodeCount + dat.nodes.size();
			}
			else {
				int16_t leafidx = ~node.iChildren[k];
				invalidRefs |= leafidx >= leafCount + dat.leaves.size();
			}
		}
	}

	for (int i = 0; i < dat.clipnodes.size(); i++) {
		BSPCLIPNODE& clipnode = dat.clipnodes[i];
		invalidRefs |= clipnode.iPlane >= planeCount + dat.planes.size();

		for (int k = 0; k < 2; k++) {
			if (clipnode.iChildren[k] >= 0) {
				invalidRefs |= clipnode.iChildren[k] >= clipnodeCount + dat.clipnodes.size();
			}
		}
	}

	invalidRefs |= dat.model.iFirstFace + dat.model.nFaces > faceCount + dat.faces.size();
	invalidRefs |= dat.model.iHeadnodes[0] >= nodeCount + dat.nodes.size();
	for (int i = 1; i < MAX_MAP_HULLS; i++) {
		invalidRefs |= dat.model.iHeadnodes[i] >= clipnodeCount + dat.clipnodes.size();
	}
	
	if (invalidRefs) {
		logf("Invalid BSP structure references in serialized data. Ignoring data.\n");
		return -1;
	}

	append_lump(LUMP_PLANES, &dat.planes[0], dat.planes.size() * sizeof(BSPPLANE));
	append_lump(LUMP_VERTICES, &dat.verts[0], dat.verts.size() * sizeof(vec3));
	append_lump(LUMP_LIGHTING, &dat.lightmaps[0], dat.lightmaps.size() * sizeof(COLOR3));
	append_lump(LUMP_EDGES, &dat.edges[0], dat.edges.size() * sizeof(BSPEDGE));
	append_lump(LUMP_SURFEDGES, &dat.surfEdges[0], dat.surfEdges.size() * sizeof(int32_t));
	append_lump(LUMP_TEXINFO, &dat.texinfos[0], dat.texinfos.size() * sizeof(BSPTEXTUREINFO));
	append_lump(LUMP_FACES, &dat.faces[0], dat.faces.size() * sizeof(BSPFACE));
	append_lump(LUMP_NODES, &dat.nodes[0], dat.nodes.size() * sizeof(BSPNODE));
	append_lump(LUMP_CLIPNODES, &dat.clipnodes[0], dat.clipnodes.size() * sizeof(BSPCLIPNODE));
	append_lump(LUMP_LEAVES, &dat.leaves[0], dat.leaves.size() * sizeof(BSPLEAF));
	append_lump(LUMP_MODELS, &dat.model, sizeof(BSPMODEL));

	// recompressing VIS data with a larger visrow size doesn't seem to be necessary.
	// Either the model leaves are being ignored or the visrow size only affects decompressed data.
	// The lump isn't always identical if using the code below though. Needs more testing.

	/*
	int oldLeafCount = leafCount - dat.leaves.size();
	uint oldVisRowSize = ((oldLeafCount + 63) & ~63) >> 3;
	uint newVisRowSize = ((leafCount + 63) & ~63) >> 3;

	// TODO: this doesn't seem necessary. I can skip this and VIS works fine. Why?
	if (oldVisRowSize != newVisRowSize) {
		logf("O SHID GOTTA REDO VIS DATA\n");
		int decompressedVisSize = leafCount * newVisRowSize;

		byte* decompressedVis = new byte[decompressedVisSize];
		memset(decompressedVis, 0, decompressedVisSize);

		// decompress this map's world leaves
		// model leaves don't need to be decompressed because the game ignores VIS for them.
		decompress_vis_lump(leaves, visdata, visDataLength, decompressedVis,
			models[0].nVisLeafs, oldLeafCount-1, leafCount-1);

		// recompress with new vis row size
		byte* compressedVis = new byte[decompressedVisSize];
		memset(compressedVis, 0, decompressedVisSize);
		int newVisLen = CompressAll(leaves, decompressedVis, compressedVis, leafCount-1, decompressedVisSize);

		byte* compressedVisResize = new byte[newVisLen];
		memcpy(compressedVisResize, compressedVis, newVisLen);

		if (newVisLen == visDataLength && memcmp(compressedVisResize, visdata, visDataLength) == 0) {
			logf("WTF SAME VIS DATA\n");
		}

		replace_lump(LUMP_VISIBILITY, compressedVisResize, newVisLen);

		delete[] decompressedVis;
		delete[] compressedVis;
	}
	*/

	logf("deserialized BSP model to index %d\n", modelCount-1);
	return modelCount - 1;
}

int Bsp::merge_models(vector<Entity*> mergeEnts, bool allowClipnodeOverlap) {
	// Note: much of this code is duplicated in BspMerger::solveMerge
	struct MergedEntity {
		Entity* ent;
		vec3 min[MAX_MAP_HULLS];
		vec3 max[MAX_MAP_HULLS];
		bool hasHull[MAX_MAP_HULLS];
	};

	vector<MergedEntity> mergedEnts;

	for (Entity* ent : mergeEnts) {
		int idx = ent->getBspModelIdx();

		if (idx >= modelCount) {
			logf("Merge failed. Invalid model selected for merging: %d\n", idx);
			return -1;
		}

		BSPMODEL& model = models[idx];

		if (idx >= 0) {
			MergedEntity ment;
			get_model_merge_bounds(idx, ment.min[0], ment.max[0]);
			for (int i = 1; i < MAX_MAP_HULLS; i++)
				get_model_hull_bounds(idx, i, ment.min[i], ment.max[i]);

			vec3 ori = ent->getOrigin();
			for (int i = 0; i < MAX_MAP_HULLS; i++) {
				ment.min[i] += ori;
				ment.max[i] += ori;
				ment.hasHull[i] = model.iHeadnodes[i] >= 0;
			}

			ment.ent = ent;
			mergedEnts.push_back(ment);
		}
		
	}

	if (mergedEnts.size() <= 1) {
		logf("no models to merge\n");
		return -1;
	}

	bool clipnodesOverlap = false;
	// check if any bounds overlap
	for (MergedEntity& enta : mergedEnts) {
		for (MergedEntity& entb : mergedEnts) {
			if (enta.ent == entb.ent)
				continue;

			for (int i = 0; i < MAX_MAP_HULLS; i++) {
				if (!enta.hasHull[i] || !entb.hasHull[i]) {
					continue;
				}

				if (boxesIntersect(enta.min[i] + enta.ent->getOrigin(), enta.max[i] + enta.ent->getOrigin(),
					entb.min[i] + entb.ent->getOrigin(), entb.max[i] + entb.ent->getOrigin())) {
					
					if (i == 0) {
						logf("Merge failed. Selected entities are intersecting or can't be divided by an axis-aligned plane.\n");
						return -1;
					}
					
					clipnodesOverlap = true;
				}
			}
		}
	}

	if (!allowClipnodeOverlap && clipnodesOverlap) {
		return -2;
	}

	// create a BSP tree of the models by expanding a bounding box to enclose
	// 1 additional object at each step. Multiple bounding boxes can be expanding in parallel

	struct MergeOp {
		Entity* enta;
		Entity* entb;
	};
	vector<MergeOp> mergeOperations;

	int hullOrder[MAX_MAP_HULLS] = { 1, 2, 3, 0 }; // biggest to smallest

	// do a dry run in case the merger can't find the right order to merge things
	while (mergedEnts.size() > 1) {

		// find the best next expansion. Something that will not intersect more than one new entity
		// and which creates the smallest bounding box.
		bool foundMerge = false;
		for (int h = 0; h < MAX_MAP_HULLS; h++) {
			int hull = hullOrder[h];

			if (h != 0) {
				clipnodesOverlap = true;
			}

			for (int i = 0; i < mergedEnts.size(); i++) {
				MergedEntity& enta = mergedEnts[i];

				if (!enta.hasHull[hull])
					continue;

				int bestMerge = -1;
				Entity* bestMergeEnt = NULL;
				float bestVolume = FLT_MAX;
				
				vec3 amin = enta.min[hull];
				vec3 amax = enta.max[hull];

				for (int k = 0; k < mergedEnts.size(); k++) {
					MergedEntity& entb = mergedEnts[k];
					if (enta.ent == entb.ent)
						continue;

					if (!entb.hasHull[hull])
						continue;

					vec3 bmin = entb.min[hull];
					vec3 bmax = entb.max[hull];
					vec3 mergedMins = vec3(min(amin.x, bmin.x), min(amin.y, bmin.y), min(amin.z, bmin.z));
					vec3 mergedMaxs = vec3(max(amax.x, bmax.x), max(amax.y, bmax.y), max(amax.z, bmax.z));
					vec3 mergedSize = mergedMaxs - mergedMins;

					float volume = mergedSize.x * mergedSize.y * mergedSize.z;
					if (volume >= bestVolume) {
						//logf("Merge %d to %d would be bigger than to %d\n", enta.ent->getBspModelIdx(), entb.ent->getBspModelIdx(), bestMergeEnt->getBspModelIdx());
						continue;
					}

					BSPPLANE separator = get_separation_plane(amin, amax, bmin, bmax);
					if (separator.nType == -1) {
						// can't find a separating plane
						//logf("No sep plane with %d and %d\n", enta.ent->getBspModelIdx(), entb.ent->getBspModelIdx());
						continue;
					}

					bool wouldMergeIntersectOtherEnts = false;
					for (const MergedEntity& entc : mergedEnts) {
						if (entc.ent == enta.ent || entc.ent == entb.ent || !entc.hasHull[hull])
							continue;

						if (boxesIntersect(mergedMins, mergedMaxs, entc.min[hull], entc.max[hull])) {
							wouldMergeIntersectOtherEnts = true;
							break;
						}
					}

					if (!wouldMergeIntersectOtherEnts) {
						bestVolume = volume;
						bestMerge = k;
						bestMergeEnt = entb.ent;
					}
				}

				if (bestMerge != -1) {
					//logf("will merge %d into %d\n", enta.ent->getBspModelIdx(), mergedEnts[bestMerge].ent->getBspModelIdx());

					// A absorbs B
					for (int h = 0; h < MAX_MAP_HULLS; h++) {
						vec3 amin = enta.min[h];
						vec3 amax = enta.max[h];
						vec3 bmin = mergedEnts[bestMerge].min[h];
						vec3 bmax = mergedEnts[bestMerge].max[h];
						enta.min[h] = vec3(min(amin.x, bmin.x), min(amin.y, bmin.y), min(amin.z, bmin.z));
						enta.max[h] = vec3(max(amax.x, bmax.x), max(amax.y, bmax.y), max(amax.z, bmax.z));
					}
					mergedEnts.erase(mergedEnts.begin() + bestMerge); // goodbye, B

					MergeOp op;
					op.enta = enta.ent;
					op.entb = bestMergeEnt;
					mergeOperations.push_back(op);

					foundMerge = true;
					break;
				}
			}
		}

		if (!foundMerge) {
			logf("The model merger is not smart enough to merge these models. Try merging a smaller group.\n");
			return -1;
		}
	}

	if (!allowClipnodeOverlap && clipnodesOverlap) {
		return -2;
	}

	remove_unused_model_structures();

	vector<int> entsToRemove;

	Entity* finalEnt = NULL;
	for (const MergeOp& op : mergeOperations) {
		int newIdx = merge_models(op.enta, op.entb);
		if (newIdx == -1)
			return -3; // shouldn't happen, but does. Special value meaning to make this undoable
		op.enta->setOrAddKeyvalue("model", "*" + to_string(newIdx));
		op.entb->removeKeyvalue("model");

		int idx = get_entity_index(op.entb);
		if (idx != -1) {
			entsToRemove.push_back(idx);
		}

		remove_unused_model_structures();
		finalEnt = op.enta;
	}

	// delete merged ents from highest index to lowest
	sort(entsToRemove.begin(), entsToRemove.end(), [](const int& a, const int& b) {
		return a > b;
	});
	for (int idx : entsToRemove) {
		ents.erase(ents.begin() + idx);
	}

	return finalEnt->getBspModelIdx();
}

int Bsp::merge_models(Entity* enta, Entity* entb) {
	int modelIdxA = enta->getBspModelIdx();
	int modelIdxB = entb->getBspModelIdx();

	if (modelIdxA < 0 || modelIdxB < 0 || modelIdxA >= modelCount || modelIdxB >= modelCount) {
		logf("Invalid model indexes selected for merging\n");
		return -1;
	}

	BSPPLANE separator;
	{
		BSPMODEL& modelA = models[modelIdxA];
		BSPMODEL& modelB = models[modelIdxB];

		vec3 mina, minb, maxa, maxb;
		get_model_merge_bounds(modelIdxA, mina, maxa);
		get_model_merge_bounds(modelIdxB, minb, maxb);

		separator = get_separation_plane(mina + enta->getOrigin(), maxa + enta->getOrigin(),
			minb + entb->getOrigin(), maxb + entb->getOrigin());

		if (separator.nType == -1) {
			logf("Merge failed. Model bounds overlap.\n");
			return -1;
		}
	}

	// reserve space for new headnodes.
	// hlds expects the headnode index to be smaller than any child node indexes.
	// So these nodes need to come first, before the model clipnodes are duplicated
	const int newHeadnodeCount = 3;
	int newClipnodeHeadnodesOffset = clipnodeCount;
	BSPCLIPNODE appendClipNodes[newHeadnodeCount];
	memset(appendClipNodes, 0, newHeadnodeCount * sizeof(BSPCLIPNODE));
	append_lump(LUMP_CLIPNODES, appendClipNodes, newHeadnodeCount * sizeof(BSPCLIPNODE));

	// and reserve space for the non-clipnode headnode
	BSPNODE newHull0Node;
	memset(&newHull0Node, 0, sizeof(BSPNODE));
	int hull0headnodeOffset = nodeCount;
	append_lump(LUMP_NODES, &newHull0Node, sizeof(BSPNODE));
	
	// lazy way to make the faces contiguous. They probably need duplicating for movement anyway
	modelIdxA = duplicate_model(modelIdxA);
	modelIdxB = duplicate_model(modelIdxB);

	g_progress.hide = true;
	if (enta->hasKey("origin")) {
		move(enta->getOrigin(), modelIdxA);
		enta->removeKeyvalue("origin");
	}
	if (entb->hasKey("origin")) {
		move(entb->getOrigin(), modelIdxB);
		entb->removeKeyvalue("origin");
	}
	g_progress.hide = false;	

	int newIndex = create_model();

	BSPMODEL& modelA = models[modelIdxA];
	BSPMODEL& modelB = models[modelIdxB];

	vec3 amin = modelA.nMins;
	vec3 amax = modelA.nMaxs;
	vec3 bmin = modelB.nMins;
	vec3 bmax = modelB.nMaxs;

	BSPMODEL& mergedModel = models[newIndex];
	mergedModel.nMins = vec3(min(amin.x, bmin.x), min(amin.y, bmin.y), min(amin.z, bmin.z));
	mergedModel.nMaxs = vec3(max(amax.x, bmax.x), max(amax.y, bmax.y), max(amax.z, bmax.z));
	mergedModel.nVisLeafs = modelA.nVisLeafs + modelB.nVisLeafs;
	mergedModel.iFirstFace = min(modelA.iFirstFace, modelB.iFirstFace);
	mergedModel.nFaces = modelA.nFaces + modelB.nFaces;
	mergedModel.nVisLeafs = modelA.nVisLeafs + modelB.nVisLeafs; // also hope this isn't a problem
	mergedModel.vOrigin = vec3();

	// planes with negative normals mess up VIS and lighting stuff, so swap children instead
	bool swapNodeChildren = separator.vNormal.x < 0 || separator.vNormal.y < 0 || separator.vNormal.z < 0;
	if (swapNodeChildren) {
		separator.vNormal = separator.vNormal.invert();
	}

	//logf("Separating plane: (%.0f, %.0f, %.0f) %.0f\n", separationPlane.vNormal.x, separationPlane.vNormal.y, separationPlane.vNormal.z, separationPlane.fDist);

	// write separating plane

	BSPPLANE* newPlanes = new BSPPLANE[planeCount + 1];
	memcpy(newPlanes, planes, planeCount * sizeof(BSPPLANE));
	newPlanes[planeCount] = separator;

	replace_lump(LUMP_PLANES, newPlanes, (planeCount + 1) * sizeof(BSPPLANE));

	int separationPlaneIdx = planeCount - 1;

	// write new head node (visible BSP)
	{
		BSPNODE headNode = {
			separationPlaneIdx,			// plane idx
			{modelB.iHeadnodes[0], modelA.iHeadnodes[0]},		// child nodes
			{ mergedModel.nMins.x, mergedModel.nMins.y, mergedModel.nMins.z },	// mins
			{ mergedModel.nMaxs.x, mergedModel.nMaxs.y, mergedModel.nMaxs.z },	// maxs
			0, // first face
			0  // n faces (none since this plane is in the void)
		};

		if (swapNodeChildren) {
			int16_t temp = headNode.iChildren[0];
			headNode.iChildren[0] = headNode.iChildren[1];
			headNode.iChildren[1] = temp;
		}

		nodes[hull0headnodeOffset] = headNode;
		mergedModel.iHeadnodes[0] = hull0headnodeOffset;
	}

	// write new head node (clipnode BSP)
	{
		const int NEW_NODE_COUNT = MAX_MAP_HULLS - 1;

		BSPCLIPNODE* newHeadNodes = clipnodes + newClipnodeHeadnodesOffset;

		for (int i = 0; i < NEW_NODE_COUNT; i++) {
			//logf("HULL %d starts at %d\n", i+1, thisWorld.iHeadnodes[i+1]);
			newHeadNodes[i] = {
				separationPlaneIdx,	// plane idx
				{	// child nodes
					(int16_t)(modelB.iHeadnodes[i + 1]),
					(int16_t)(modelA.iHeadnodes[i + 1])
				},
			};

			if (modelB.iHeadnodes[i + 1] < 0) {
				newHeadNodes[i].iChildren[0] = CONTENTS_EMPTY;
			}
			if (modelA.iHeadnodes[i + 1] < 0) {
				newHeadNodes[i].iChildren[1] = CONTENTS_EMPTY;
			}

			if (swapNodeChildren) {
				int16_t temp = newHeadNodes[i].iChildren[0];
				newHeadNodes[i].iChildren[0] = newHeadNodes[i].iChildren[1];
				newHeadNodes[i].iChildren[1] = temp;
			}

			mergedModel.iHeadnodes[i+1] = newClipnodeHeadnodesOffset + i;
		}
	}

	return newIndex;
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

bool Bsp::is_embedded_rad_texture_name(const char* name) {
	if (strlen(name) > 5) {
		char c = name[0];
		bool hasRadPrefix = strstr(name, "_rad") == name + 1 && (c == '_' || c == '{' || c == '!');
		if (hasRadPrefix && name[5] >= '0' && name[5] <= '9') {
			return true;
		}
	}

	return false;
}

BSPTEXTUREINFO* Bsp::get_embedded_rad_texinfo(BSPTEXTUREINFO& info) {
	if (info.iMiptex >= textureCount) {
		return NULL;
	}

	BSPMIPTEX* tex = get_texture(info.iMiptex);
	if (!tex) {
		return NULL;
	}

	/*
	* -------------------------------------------
	* The VHLT Embedded rad texture naming format
	* -------------------------------------------
	* 
	* Example name:    __rad03319Jxi03
	* Name components: __rad 03319 Jxi 03
	* 
	* The components are:
	*	- A prefix, which is one of:
	*		__rad
	*		!_rad     (this is commented out in VHLT, but SCHLT may enable it soon for func_water)
	*		{_rad
	*	- The original texinfo index as a decimal value.
	*	- A hash string computed from the texture index and size of the texture.
	*	- A count value representing the face index in the model modulo'd by 62.
	* 
	* Only the original texinfo index is read when running hlrad again. The hash and count is there only
	* to make the name unique. So, to keep this in sync with bspguy, rewrite the texinfo if it ever
	* changes, and leave the rest alone. The original map can be used to load lost texinfos.
	*/

	if (is_embedded_rad_texture_name(tex->szName)) {
		int offset = atoi(&tex->szName[5]);
		if (offset >= 0 && offset < texinfoCount) {
			return &texinfos[offset];
		}
	}

	return NULL;
}

BSPTEXTUREINFO* Bsp::get_embedded_rad_texinfo(const char* texName) {
	for (int i = 0; i < texinfoCount; i++) {
		BSPTEXTUREINFO& info = texinfos[i];
		BSPMIPTEX* tex = get_texture(info.iMiptex);

		if (tex && !strncmp(texName, tex->szName, MAXTEXTURENAME)) {
			return get_embedded_rad_texinfo(info);
		}
	}

	int hashOffset = strlen("__rad12345");
	const char* searchhash = texName + hashOffset;
	if (strlen(texName) < 12)
		return NULL;

	// no texture with that exact name exists, probably because bspguy changed the texinfo part
	// try finding a match on the last part of the texture name which should be unique per texture
	int matchOffset = 0;
	int matchIdx = -1;

	for (int i = 0; i < texinfoCount; i++) {
		BSPTEXTUREINFO& info = texinfos[i];
		BSPMIPTEX* tex = get_texture(info.iMiptex);

		if (!is_embedded_rad_texture_name(tex->szName) || strlen(texName) < 12) {
			continue;
		}

		const char* hashpart = tex->szName + strlen("__rad12345");

		if (tex && !strncmp(searchhash, hashpart, MAXTEXTURENAME - hashOffset)) {
			int offset = atoi(&tex->szName[5]);

			if (matchIdx == -1 || offset == matchOffset) {
				matchOffset = offset;
				matchIdx = i;
			}
			else {
				matchIdx = -1;
				break;
			}
		}
	}

	if (matchIdx != 1) {
		return get_embedded_rad_texinfo(texinfos[matchIdx]);
	}
	
	return NULL;
}

bool Bsp::do_entities_share_models() {
	unordered_set<int> uniqueModels;

	for (Entity* ent : ents) {
		int modelIdx = ent->getBspModelIdx();
		if (modelIdx > 0) {
			if (uniqueModels.count(modelIdx)) {
				return true;
			}
			uniqueModels.insert(modelIdx);
		}
	}

	return false;
}

int Bsp::count_missing_textures() {
	static vector<Wad*> emptyWads;
	vector<Wad*>& wads = g_app->mapRenderer ? g_app->mapRenderer->wads : emptyWads;

	int missing_textures = 0;

	for (int i = 0; i < textureCount; i++) {
		BSPMIPTEX* tex = get_texture(i);
		if (!tex) {
			missing_textures++;
			continue;
		}

		if (tex->nOffsets[0] == 0) {
			bool foundTexture = false;
			for (int k = 0; k < wads.size(); k++) {
				if (wads[k]->hasTexture(tex->szName)) {
					foundTexture = true;
					break;
				}
			}
			if (!foundTexture) {
				missing_textures++;
			}
		}

		if (tex->nWidth * tex->nHeight > g_limits.max_texturepixels) {
			logf("Texture '%s' too large (%dx%d)\n", tex->szName, tex->nWidth, tex->nHeight);
		}
	}

	return missing_textures;
}

void Bsp::generate_wa_file() {
	int numMissing = 0;
	
	vector<WADTEX> wadTextures;
	for (int i = 0; i < textureCount; i++) {
		int32_t offset = ((int32_t*)textures)[i + 1];
		BSPMIPTEX* tex = (BSPMIPTEX*)(textures + offset);

		if (tex->nOffsets[0] != 0) {
			continue; // embedded
		}

		WADTEX copy = load_texture(i);

		if (copy.data)
			wadTextures.push_back(copy);
		else
			numMissing++;
	}

	string fname = path;
	replaceAll(fname, ".bsp", ".wa_");

	Wad outWad = Wad();
	outWad.write(fname, &wadTextures[0], wadTextures.size());
	logf("Wrote %d WAD textures to: %s\n", wadTextures.size(), fname.c_str());

	for (WADTEX& tex : wadTextures) {
		delete[] tex.data;
	}
}

int Bsp::make_unique_texlight_models() {
	unordered_map<string, string> texlights = get_tex_lights();
	unordered_set<int> texlight_models;

	for (Entity* ent : ents) {
		if (ent->getClassname() == "light_surface") {
			texlights[toUpperCase(ent->getKeyvalue("_tex"))] = ent->getKeyvalue("_light");
		}
	}


	for (int i = 1; i < modelCount; i++) {
		BSPMODEL& model = models[i];

		for (int k = model.iFirstFace; k < model.iFirstFace + model.nFaces; k++) {
			BSPMIPTEX* tex = get_texture(texinfos[faces[k].iTextureInfo].iMiptex);

			if (tex && texlights.count(toUpperCase(tex->szName))) {
				// Distance, name, and classnames filters aren't checked for simplicity.
				// Duplicated models can be deduplicated after running RAD.
				texlight_models.insert(i);
				break;
			}
		}
	}

	int numDups = 0;

	for (Entity* ent : ents) {
		int modelidx = ent->getBspModelIdx();

		if (texlight_models.count(modelidx)) {
			bool isUnique = true;

			for (Entity* ent2 : ents) {
				if (ent != ent2 && ent2->getBspModelIdx() == modelidx) {
					isUnique = false;
					break;
				}
			}

			if (!isUnique) {
				logf("Duplicate model %d\n", modelidx);
				int newModelIdx = duplicate_model(modelidx);
				ent->setOrAddKeyvalue("model", "*" + to_string(newModelIdx));
				numDups++;
			}
		}
	}

	return numDups;
}

unordered_map<string, string> Bsp::get_tex_lights() {
	unordered_map<string, string> texlights;

	for (int i = 0; i < ents.size(); i++) {
		Entity* ent = ents[i];
		if (ent->getClassname() == "info_texlights") {
			unordered_map<string, string> keys = ent->getAllKeyvalues();
			for (auto item : keys) {
				if (item.first != "classname")
					texlights[toUpperCase(item.first)] = item.second;
			}
		}
	}

	return texlights;
}

unordered_map<string, string> Bsp::filter_tex_lights(const unordered_map<string, string>& inputLights) {
	unordered_map<string, string> filtered;

	unordered_set<string> textureNames;
	for (int i = 0; i < faceCount; i++) {
		BSPMIPTEX* tex = get_texture(texinfos[faces[i].iTextureInfo].iMiptex);

		if (tex) {
			textureNames.insert(toUpperCase(tex->szName));
		}
	}

	for (auto item : inputLights) {
		if (textureNames.count(toUpperCase(item.first))) {
			filtered[item.first] = item.second;
		}
	}

	return filtered;
}

unordered_map<string, string> Bsp::load_texlights_from_file(string fname) {
	std::ifstream file(fname); // Open file
	if (!file) {
		logf("Failed to open file: %s\n", fname.c_str());
		return unordered_map<string, string>();
	}

	unordered_map<string, string> texlights;

	string line;
	while (std::getline(file, line)) {
		string name, args;
		if (load_texlight_from_string(line, name, args)) {
			texlights[name] = args;
		}
	}

	return filter_tex_lights(texlights);
}

bool Bsp::load_texlight_from_string(string line, string& name, string& args) {
	int comment = line.find("//");
	if (comment != -1) {
		line = line.substr(0, comment);
	}
	line = trimSpaces(line);

	int space = line.find_first_of(" \t");
	if (space == -1)
		return false;

	name = toUpperCase(line.substr(0, space));
	args = trimSpaces(line.substr(space));
	
	return true;
}

bool Bsp::add_texlights(const unordered_map<string, string>& newLights) {
	unordered_map<string, string> texlights = get_tex_lights();

	bool anyChanges = false;

	if (texlights.size())
		logf("Parsed %d texlights from existing info_texlights entities.\n", texlights.size());

	int oldSize = texlights.size();

	unordered_map<string, string> filteredNewLights = filter_tex_lights(newLights);

	if (!filteredNewLights.size()) {
		logf("No new texlights to add/update.\n");
		return false;
	}

	logf("Added/updated %d texlights\n", filteredNewLights.size());
	for (auto item : filteredNewLights) {
		texlights[toUpperCase(item.first)] = item.second;
	}

	for (int i = 0; i < ents.size(); i++) {
		Entity* ent = ents[i];
		if (ent->getClassname() == "info_texlights") {
			delete ent;
			ents.erase(ents.begin() + i);
			i--;
		}
	}

	Entity* texlights_ent = new Entity();

	std::vector<std::string> keys;
	for (const auto& item : texlights) {
		keys.push_back(item.first);
	}

	sort(keys.begin(), keys.end());

	for (const string& key : keys) {
		texlights_ent->setOrAddKeyvalue(key, texlights[key]);
	}

	texlights_ent->setOrAddKeyvalue("classname", "info_texlights");

	ents.push_back(texlights_ent);

	return true;
}

bool Bsp::replace_texlights(string texlightString) {
	vector<string> lines = splitString(texlightString, "\n");

	unordered_map<string, string> texlights;

	for (string line : lines) {
		int comment = line.find("//");
		if (comment != -1) {
			line = line.substr(0, comment);
		}
		line = trimSpaces(line);

		int space = line.find_first_of(" \t");
		if (space == -1)
			continue;

		string name = toUpperCase(line.substr(0, space));
		string args = trimSpaces(line.substr(space));
		texlights[name] = args;
	}

	for (int i = 0; i < ents.size(); i++) {
		Entity* ent = ents[i];
		if (ent->getClassname() == "info_texlights") {
			delete ent;
			ents.erase(ents.begin() + i);
			i--;
		}
	}

	return add_texlights(texlights);
}

unordered_map<string, string> Bsp::estimate_texlights(int epsilon) {
	unordered_map<string, string> texlights = get_tex_lights();
	unordered_map<string, string> newTexlights;

	unordered_set<string> light_surface_names;
	unordered_set<string> global_light_surface_names; // texture names that are always affected by light_surface
	vector<Entity*> light_surface_ents;
	for (Entity* ent : ents) {
		string cname = ent->getClassname();
		if (cname == "light_surface" || cname == "light" || cname == "light_spot") {
			string texname = toUpperCase(ent->getKeyvalue("_tex"));
			if (texname.empty())
				continue;

			light_surface_ents.push_back(ent);

			if (!ent->hasKey("_frange") && !ent->hasKey("_fdist") && !ent->hasKey("_fclass") && !ent->hasKey("_fname")) {
				global_light_surface_names.insert(texname);
			}
			else {
				light_surface_names.insert(texname);
			}
		}
	}

	for (int i = 0; i < textureCount; i++) {
		BSPMIPTEX* tex = get_texture(i);

		if (!tex || is_embedded_rad_texture_name(tex->szName))
			continue;
		
		string surfaceName = toUpperCase(tex->szName);

		if (global_light_surface_names.count(surfaceName)) {
			// a light_surface entity in the map is affecting every face using this texture,
			// so adding it as a texlight would have no effect.
			continue;
		}

		if (texlights.count(surfaceName)) {
			// already specified in info_texlights
			continue;
		}

		// true if only some faces are affected by light_surface ents in the map
		bool affectedByLightSurface = light_surface_names.count(surfaceName);

		bool anyLightmaps = false;
		bool isTexlight = true;
		bool hasLightcolor = false;
		COLOR3 minColor(255, 255, 255);
		COLOR3 maxColor(0,0,0);
		const int defaultBrightness = 8000; // better too bright than too dark
		int numFaces = 0;

		for (int k = 0; k < faceCount; k++) {
			BSPFACE& face = faces[k];
			BSPTEXTUREINFO& info = texinfos[faces[k].iTextureInfo];
			if (get_texture(info.iMiptex) != tex) {
				continue;
			}

			if (info.nFlags & TEX_SPECIAL) {
				continue; // special faces don't have lightmaps
			}

			if (affectedByLightSurface) {
				// TODO: skip face if this face is affected by light_surface
			}

			int size[2];
			if (!GetFaceLightmapSize(this, k, size)) {
				debugf("Invalid extents for face %d. Can't check lightmaps", k);
				continue;
			}
			int w = size[0];
			int h = size[1];

			if (face.nStyles[0] == 255) {
				continue; // pitch black faces can't be texlights
			}

			// texlights can receive lighting (c1a0 monitor) so don't skip if it has lightstyles
			/*
			if (face.nStyles[1] != 255) {
				isTexlight = false;
				break; // texlights receive no lighting, so this can't be a texlight if it has styles
			}
			*/

			anyLightmaps = true;
			numFaces++;

			int lightmapSz = w * h * sizeof(COLOR3);
			int offset = face.nLightmapOffset;
			COLOR3* lightSrc = (COLOR3*)(lightdata + offset);			
			for (int y = 0; y < h && isTexlight; y++) {
				for (int x = 0; x < w; x++) {
					COLOR3 color = lightSrc[y * w + x];
					minColor.r = min(minColor.r, color.r);
					minColor.g = min(minColor.g, color.g);
					minColor.b = min(minColor.b, color.b);
					maxColor.r = max(maxColor.r, color.r);
					maxColor.g = max(maxColor.g, color.g);
					maxColor.b = max(maxColor.b, color.b);
				}
			}

			if (!isTexlight) {
				break;
			}
		}

		COLOR3 avgColor(
			minColor.r + (maxColor.r - minColor.r) * 0.5f,
			minColor.g + (maxColor.g - minColor.g) * 0.5f,
			minColor.b + (maxColor.b - minColor.b) * 0.5f
		);
		int maxChannel = max(avgColor.r, max(avgColor.g, avgColor.b));
		if (maxChannel < 90) {
			// too dark to be a texlight. With -dlight 1.0, the minimum value
			// for the brightest texlight color channel is 92.
			isTexlight = false;
		}
		else {
			if (abs((int)minColor.r - (int)maxColor.r) > epsilon
				|| abs((int)minColor.g - (int)maxColor.g) > epsilon
				|| abs((int)minColor.b - (int)maxColor.b) > epsilon) {
				// lightmap is not entirely the same color as every other lightmap pixel
				// for every other lightmap for this texture. Must not be a texlight.
				isTexlight = false;
			}
		}

		if (isTexlight && anyLightmaps) {
			newTexlights[tex->szName] = to_string(maxColor.r) + " " + to_string(maxColor.g) + " "
				+ to_string(maxColor.b) + " " + to_string(defaultBrightness);
		}
	}

	return newTexlights;
}

BSPMIPTEX* Bsp::get_texture(int iMiptex) {
	if (iMiptex < textureCount) {
		int32_t texOffset = ((int32_t*)textures)[iMiptex + 1];
		if (texOffset + sizeof(BSPMIPTEX) <= header.lump[LUMP_TEXTURES].nLength && texOffset > 0) {
			return ((BSPMIPTEX*)(textures + texOffset));
		}
	}

	return NULL;
}

int Bsp::delete_embedded_rad_textures(Bsp* originalMap) {

	// first check that the original texture references are valid
	int numBadRadTexture = 0;
	for (int i = 0; i < faceCount; i++) {
		BSPFACE& face = faces[i];
		BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];
		BSPMIPTEX* tex = get_texture(info.iMiptex);

		if (!tex)
			continue;

		BSPTEXTUREINFO* radinfo = get_embedded_rad_texinfo(info);

		if (!radinfo)
			continue;

		if (originalMap) {
			radinfo = originalMap->get_embedded_rad_texinfo(tex->szName);

			if (!radinfo) {
				numBadRadTexture++;
				continue;
			}
		}

		BSPPLANE& plane = planes[face.iPlane];
		vec3 faceNormal = plane.vNormal * (face.nPlaneSide ? -1 : 1);
		vec3 texnormal = crossProduct(radinfo->vT, radinfo->vS).normalize();
		float distscale = dotProduct(texnormal, faceNormal);

		if (distscale == 0) {
			debugf("Invalid RAD texture axes in %s\n", tex->szName);
			numBadRadTexture++;
		}
	}

	if (numBadRadTexture > 0) {
		return -1;
	}

	int numRemoved = 0;

	for (int i = 0; i < faceCount; i++) {
		BSPFACE& face = faces[i];
		BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];
		BSPMIPTEX* tex = get_texture(info.iMiptex);

		BSPTEXTUREINFO* radinfo = get_embedded_rad_texinfo(info);

		if (!radinfo) {
			continue;
		}

		numRemoved++;

		if (originalMap) {
			radinfo = originalMap->get_embedded_rad_texinfo(tex->szName);

			if (!radinfo) {
				continue;
			}

			BSPMIPTEX* ogtex = originalMap->get_texture(radinfo->iMiptex);
			if (!ogtex) {
				continue;
			}

			info = *radinfo;
			info.iMiptex = 0;

			bool foundTexture = false;
			for (int k = 0; k < textureCount; k++) {
				BSPMIPTEX* testTex = get_texture(k);
				if (!memcmp(ogtex, testTex, sizeof(BSPMIPTEX))) {
					debugf("Found existing texture referenced by other map: %s\n", ogtex->szName);
					info.iMiptex = k;
					foundTexture = true;
					break;
				}
			}

			if (!foundTexture) {
				if (ogtex->nOffsets[0] == 0) {
					// texture loads from a WAD, just add the reference
					WADTEX* tex = new WADTEX;
					memcpy(tex, ogtex, sizeof(BSPMIPTEX));
					info.iMiptex = add_texture_from_wad(tex);
					delete tex;
					logf("Added texture reference for %s\n", ogtex->szName);
				}
				else {
					WADTEX tex = originalMap->load_texture(radinfo->iMiptex);
					info.iMiptex = add_texture(tex);
					if (tex.data)
						delete tex.data;
					logf("Copied embedded texture %s\n", ogtex->szName);
				}
			}
		}
		else {
			info = *radinfo;
		}
	}

	if (numRemoved)
		remove_unused_model_structures(false).print_delete_stats(1);

	return numRemoved;
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

void Bsp::get_portal_file_leaf_numbers(int iNode, unordered_map<uint16_t, uint16_t>& leafMap, int& leafCount) {
	if (iNode >= 0) {
		BSPNODE& node = nodes[iNode];
		get_portal_file_leaf_numbers(node.iChildren[0], leafMap, leafCount);
		get_portal_file_leaf_numbers(node.iChildren[1], leafMap, leafCount);
		return;
	}

	uint16_t leafIdx = ~iNode;
	BSPLEAF& leaf = leaves[leafIdx];

	if (leaf.nContents != CONTENTS_SOLID)
		leafMap[leafIdx] = leafCount++;
}

int Bsp::write_portal_file_portals(int iNode, LeafNavMesh* mesh, unordered_set<uint32_t>& visited, 
	unordered_map<uint16_t, uint16_t>& leafMap, FILE* fout)
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

	uint16_t leafIdx = ~iNode;
	int nodeIdx = mesh->leafMap[leafIdx];

	if (nodeIdx == NAV_INVALID_IDX) {
		if (fout)
			logf("Leaf %d has no mesh\n", (int)leafIdx);
		return 0;
	}

	LeafNode& node = mesh->nodes[nodeIdx];
	uint16_t src = leafIdx;
	int count = 0;

	for (LeafLink& link : node.links) {
		if (link.node == NAV_INVALID_IDX)
			continue;

		uint16_t dst = mesh->nodes[link.node].leafIdx;

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
	unordered_map<uint16_t, uint16_t> leafIndexMap;
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

void Bsp::print_leaf(int leafidx) {
	BSPLEAF& leaf = leaves[leafidx];
	logf(getLeafContentsName(leaf.nContents));
	logf(" (LEAF %d), %d surfs, Min(%d, %d, %d), Max(%d %d %d)",
		leafidx, leaf.nMarkSurfaces, 
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
