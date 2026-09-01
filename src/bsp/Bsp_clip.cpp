#include "Bsp.h"
#include "util.h"
#include "LeafNavMesh.h"
#include "LeafOctree.h"
#include "Editor.h"
#include "BspRenderer.h"

#include <queue>
#include <float.h>

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
		print_clipnode_tree(clipnodes[iNode].iChildren[i], depth + 1);
	}
}

void Bsp::get_clipnode_leaf_cuts(int iNode, vector<BSPPLANE>& clipOrder, vector<NodeVolumeCuts>& output, int32_t contents) {
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

			int insideContents = k == 5 ? CONTENTS_SOLID : clipnodeIdx + 1;

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

int Bsp::count_clipnode_solids(int iNode) {
	BSPCLIPNODE& node = clipnodes[iNode];

	if (node.iPlane < 0) {
		return 0;
	}

	int totalSolid = 0;

	for (int i = 0; i < 2; i++) {
		if (node.iChildren[i] >= 0) {
			totalSolid += count_clipnode_solids(node.iChildren[i]);
		}
		else if (node.iChildren[i] < 0 && node.iChildren[0] != CONTENTS_EMPTY) {
			totalSolid += 1;
		}
	}

	return totalSolid;
}

int Bsp::expand_clipnode_hull_node(int iNode, float vertical, float horizontal) {
	BSPCLIPNODE& node = clipnodes[iNode];

	if (node.iPlane < 0) {
		return 0;
	}

	int planesAdded = 0;

	if ((node.iChildren[0] < 0 || node.iChildren[1] < 0)) {
		BSPPLANE newPlane = planes[node.iPlane];

		int oldPlane = node.iPlane;
		const char* dir = "UP";

		if (node.iChildren[0] == CONTENTS_EMPTY) {
			newPlane.fDist += vertical * dotProduct(newPlane.vNormal, vec3(0, 0, 1));
		}
		else if (node.iChildren[1] == CONTENTS_EMPTY) {
			newPlane.fDist -= vertical * dotProduct(newPlane.vNormal, vec3(0, 0, 1));
		}
		else if (node.iChildren[1] < 0) {
			newPlane.fDist += vertical * dotProduct(newPlane.vNormal, vec3(0, 0, 1));
		}
		else if (node.iChildren[0] < 0) {
			newPlane.fDist -= vertical * dotProduct(newPlane.vNormal, vec3(0, 0, 1));
		}

		int sharePlaneIdx = -1;
		for (int i = 0; i < planeCount; i++) {
			if (!memcmp(&newPlane, &planes[i], sizeof(BSPPLANE))) {
				sharePlaneIdx = i;
				break;
			}
		}

		if (sharePlaneIdx != -1) {
			node.iPlane = sharePlaneIdx;
		}
		else {
			int newPlaneIdx = create_plane();
			planes[newPlaneIdx] = newPlane;
			node.iPlane = newPlaneIdx;
			planesAdded++;
		}

		//logf("Node %d (child %d, %d) move plane %d %s -> ID %d\n", iNode,
		//	node.iChildren[0], node.iChildren[1], oldPlane, dir, node.iPlane);
	}
	else {
		int solidFront = count_clipnode_solids(node.iChildren[0]);
		int solidBack = count_clipnode_solids(node.iChildren[1]);
		// TODO: Need some way to test which side of the node is solid
		logf("don't know how to move node %d. Solids = %d front, %d back\n", iNode, solidFront, solidBack);
	}

	for (int i = 0; i < 2; i++) {
		if (node.iChildren[i] >= 0) {
			planesAdded += expand_clipnode_hull_node(node.iChildren[i], vertical, horizontal);
		}
	}

	return planesAdded;
}

void Bsp::expand_clipnode_hull(int hull, float vertical, float horizontal) {
	int planesAdded = expand_clipnode_hull_node(models[0].iHeadnodes[1], vertical, horizontal);
	logf("Created %d new planes\n", planesAdded);
}

void Bsp::make_clipnodes_contiguous() {
	// make world nodes contiguous (fixes crash in xash3d)
	STRUCTUSAGE worldClipnodeMarks(this);
	mark_clipnode_structures(models[0].iHeadnodes[1], &worldClipnodeMarks);
	mark_clipnode_structures(models[0].iHeadnodes[2], &worldClipnodeMarks);
	mark_clipnode_structures(models[0].iHeadnodes[3], &worldClipnodeMarks);

	BSPCLIPNODE* newClipnodeOrder = new BSPCLIPNODE[clipnodeCount];
	int* clipnodesRemap = new int[clipnodeCount];
	int order = 0;

	// world clipnodes first
	for (int i = 0; i < clipnodeCount; i++) {
		if (worldClipnodeMarks.clipnodes[i]) {
			newClipnodeOrder[order] = clipnodes[i];
			clipnodesRemap[i] = order;
			order++;
		}
	}

	// entity nodes next
	for (int i = 0; i < clipnodeCount; i++) {
		if (!worldClipnodeMarks.clipnodes[i]) {
			newClipnodeOrder[order] = clipnodes[i];
			clipnodesRemap[i] = order;
			order++;
		}
	}

	for (int i = 0; i < clipnodeCount; i++) {
		BSPCLIPNODE& node = newClipnodeOrder[i];

		for (int k = 0; k < 2; k++) {
			if (node.iChildren[k] >= 0) {
				node.iChildren[k] = clipnodesRemap[node.iChildren[k]];
			}
		}
	}

	for (int i = 0; i < modelCount; i++) {
		for (int k = 1; k < MAX_MAP_HULLS; k++) {
			if (models[i].iHeadnodes[k] >= 0)
				models[i].iHeadnodes[k] = clipnodesRemap[models[i].iHeadnodes[k]];
		}
	}

	delete[] clipnodesRemap;
	replace_lump(LUMP_CLIPNODES, newClipnodeOrder, clipnodeCount * sizeof(BSPCLIPNODE));
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

void Bsp::delete_oob_clipnodes(int iNode, int32_t* parentBranch, vector<BSPPLANE>& clipOrder, int oobFlags,
	bool* oobHistory, bool isFirstPass, int& removedNodes) {
	if (iNode < 0 || iNode >= clipnodeCount)
		return;
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

void Bsp::delete_box_clipnodes(int iNode, int32_t* parentBranch, vector<BSPPLANE>& clipOrder,
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
