#include "Bsp.h"
#include "util.h"
#include "Clipper.h"
#include "Entity.h"

#include <unordered_map>
#include <algorithm>
#include <float.h>

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

vec3 Bsp::get_model_center(int modelIdx) {
	if (modelIdx < 0 || modelIdx > header.lump[LUMP_MODELS].nLength / sizeof(BSPMODEL)) {
		logf("Invalid model index %d. Must be 0 - %d\n", modelIdx);
		return vec3();
	}

	BSPMODEL& model = models[modelIdx];

	return model.nMins + (model.nMaxs - model.nMins) * 0.5f;
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

bool Bsp::is_convex(int modelIdx) {
	return models[modelIdx].iHeadnodes[0] >= 0 && is_node_hull_convex(models[modelIdx].iHeadnodes[0]);
}

bool Bsp::vertex_manipulation_sync(int modelIdx, vector<TransformVert>& hullVerts, bool convexCheckOnly, bool regenClipnodes) {
	set<int> affectedPlanes;

	unordered_map<int, vector<vec3>> planeVerts;
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
	unordered_map<int, BSPPLANE> newPlanes;
	unordered_map<int, bool> shouldFlipChildren;
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

void Bsp::split_shared_model_structures(int modelIdx) {
	// marks which structures should not be moved
	STRUCTUSAGE shouldMove(this);
	STRUCTUSAGE shouldNotMove(this);

	g_progress.update("Split model structures", modelCount);

	bool notMovingLeaves = modelIdx != 0;

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
			warnf("\nWarning: leaf shared with multiple models. Something might break.\n");
			break;
		}
	}
	for (int i = 0; i < shouldNotMove.count.nodes; i++) {
		if (shouldMove.nodes[i] && shouldNotMove.nodes[i]) {
			errorf("\nError: node shared with multiple models. Something will break.\n");
			break;
		}
	}
	for (int i = 0; i < shouldNotMove.count.surfEdges; i++) {
		if (shouldMove.surfEdges[i] && shouldNotMove.surfEdges[i]) {
			errorf("\nError: surfedge shared with multiple models. Something will break.\n");
			break;
		}
	}
	for (int i = 0; i < shouldNotMove.count.edges; i++) {
		if (shouldMove.edges[i] && shouldNotMove.edges[i]) {
			errorf("\nError: edge shared with multiple models. Something will break.\n");
			break;
		}
	}

	int duplicatePlanes = 0;
	int duplicateClipnodes = 0;
	int duplicateTexinfos = 0;
	int duplicateVerts = 0;

	for (int i = 0; i < shouldNotMove.count.planes; i++) {
		duplicatePlanes += shouldMove.planes[i] && shouldNotMove.planes[i];
	}
	for (int i = 0; i < shouldNotMove.count.clipnodes; i++) {
		duplicateClipnodes += shouldMove.clipnodes[i] && shouldNotMove.clipnodes[i];
	}
	for (int i = 0; i < shouldNotMove.count.texInfos; i++) {
		duplicateTexinfos += shouldMove.texInfo[i] && shouldNotMove.texInfo[i];
	}
	for (int i = 0; i < shouldNotMove.count.verts; i++) {
		duplicateVerts += shouldMove.verts[i] && shouldNotMove.verts[i];
	}

	int newPlaneCount = planeCount + duplicatePlanes;
	int newClipnodeCount = clipnodeCount + duplicateClipnodes;
	int newTexinfoCount = texinfoCount + duplicateTexinfos;
	int newVertCount = vertCount + duplicateVerts;

	BSPPLANE* newPlanes = new BSPPLANE[newPlaneCount];
	memcpy(newPlanes, planes, planeCount * sizeof(BSPPLANE));

	BSPCLIPNODE* newClipnodes = new BSPCLIPNODE[newClipnodeCount];
	memcpy(newClipnodes, clipnodes, clipnodeCount * sizeof(BSPCLIPNODE));

	BSPTEXTUREINFO* newTexinfos = new BSPTEXTUREINFO[newTexinfoCount];
	memcpy(newTexinfos, texinfos, texinfoCount * sizeof(BSPTEXTUREINFO));

	vec3* newVerts = new vec3[newVertCount];
	memcpy(newVerts, verts, newVertCount * sizeof(vec3));

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

	addIdx = vertCount;
	for (int i = 0; i < shouldNotMove.count.verts; i++) {
		if (shouldMove.verts[i] && shouldNotMove.verts[i]) {
			newVerts[addIdx] = verts[i];
			remappedStuff.verts[i] = addIdx;
			addIdx++;
		}
	}

	replace_lump(LUMP_PLANES, newPlanes, newPlaneCount * sizeof(BSPPLANE));
	replace_lump(LUMP_CLIPNODES, newClipnodes, newClipnodeCount * sizeof(BSPCLIPNODE));
	replace_lump(LUMP_TEXINFO, newTexinfos, newTexinfoCount * sizeof(BSPTEXTUREINFO));
	replace_lump(LUMP_VERTICES, newVerts, newVertCount * sizeof(vec3));

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
		if (duplicateVerts)
			debugf("    Added %d verts\n", duplicateVerts);
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

int Bsp::deduplicate_models(bool allowTextureShift, bool dryrun) {
	struct CompareVert {
		vec3 pos;
		float u, v;
	};

	struct ModelIdxRemap {
		int newIdx;
		vec3 offset;
	};

	const float epsilon = 1.0f;

	unordered_map<int, ModelIdxRemap> modelRemap;

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
		oldModels + (modelIdx + 1) * sizeof(BSPMODEL),
		(modelCount - (modelIdx + 1)) * sizeof(BSPMODEL));

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
	replace_lump(LUMP_FACES, newFaces, (faceCount + faceIndexes.size()) * sizeof(BSPFACE));

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
	newNode.nMins.x = min.x;
	newNode.nMins.y = min.y;
	newNode.nMins.z = min.z;
	newNode.nMaxs.x = max.x;
	newNode.nMaxs.y = max.y;
	newNode.nMaxs.z = max.z;

	newModel.nMins = min;
	newModel.nMaxs = max;

	return modelIdx;
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
				int32_t leafidx = ~node.iChildren[k];
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
		invalidRefs |= face.nLightmapOffset >= lightDataLength + dat.lightmaps.size() * sizeof(COLOR3);
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
				int32_t leafidx = ~node.iChildren[k];
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
		warnf("Invalid BSP structure references in serialized data. Ignoring data.\n");
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

	logf("deserialized BSP model to index %d\n", modelCount - 1);
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
			(uint32_t)separationPlaneIdx,						// plane idx
			{modelB.iHeadnodes[0], modelA.iHeadnodes[0]},		// child nodes
			{ mergedModel.nMins.x, mergedModel.nMins.y, mergedModel.nMins.z },	// mins
			{ mergedModel.nMaxs.x, mergedModel.nMaxs.y, mergedModel.nMaxs.z },	// maxs
			0, // first face
			0  // n faces (none since this plane is in the void)
		};

		if (swapNodeChildren) {
			int32_t temp = headNode.iChildren[0];
			headNode.iChildren[0] = headNode.iChildren[1];
			headNode.iChildren[1] = temp;
		}

		nodes[hull0headnodeOffset] = headNode;
		mergedModel.iHeadnodes[0] = hull0headnodeOffset;

		// only one model has a visible hull?
		if (modelA.iHeadnodes[0] != -1 && modelB.iHeadnodes[0] == -1) {
			mergedModel.iHeadnodes[0] = modelA.iHeadnodes[0];
			mergedModel.iFirstFace = modelA.iFirstFace;
		}
		else if (modelB.iHeadnodes[0] != -1 && modelA.iHeadnodes[0] == -1) {
			mergedModel.iHeadnodes[0] = modelB.iHeadnodes[0];
			mergedModel.iFirstFace = modelB.iFirstFace;
		}
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
					(int32_t)(modelB.iHeadnodes[i + 1]),
					(int32_t)(modelA.iHeadnodes[i + 1])
				},
			};

			if (modelB.iHeadnodes[i + 1] < 0) {
				newHeadNodes[i].iChildren[0] = CONTENTS_EMPTY;
			}
			if (modelA.iHeadnodes[i + 1] < 0) {
				newHeadNodes[i].iChildren[1] = CONTENTS_EMPTY;
			}

			if (swapNodeChildren) {
				int32_t temp = newHeadNodes[i].iChildren[0];
				newHeadNodes[i].iChildren[0] = newHeadNodes[i].iChildren[1];
				newHeadNodes[i].iChildren[1] = temp;
			}

			mergedModel.iHeadnodes[i + 1] = newClipnodeHeadnodesOffset + i;
		}
	}

	return newIndex;
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
		else if (~node.iChildren[i] > models[0].nVisLeafs) {
			BSPLEAF& leaf = leaves[~node.iChildren[i]];

			// submodels don't use the faces linked to leaves, so deleting the references
			// should cause no harm...
			leaf.iFirstMarkSurface = 0;
			leaf.nMarkSurfaces = 0;
		}
	}
}
