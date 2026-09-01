#include "Bsp.h"
#include "util.h"
#include "LeafNavMesh.h"
#include "LeafOctree.h"
#include "Editor.h"
#include "BspRenderer.h"
#include <queue>

vector<NodeVolumeCuts> Bsp::get_model_leaf_volume_cuts(int modelIdx, int hullIdx, int32_t contents) {
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

void Bsp::get_node_leaf_cuts(int iNode, vector<BSPPLANE>& clipOrder, vector<NodeVolumeCuts>& output, int32_t contents) {
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

void Bsp::print_leaf(int leafidx) {
	BSPLEAF& leaf = leaves[leafidx];
	logf(getLeafContentsName(leaf.nContents));
	logf(" (LEAF %d), %d surfs, Min(%d, %d, %d), Max(%d %d %d)",
		leafidx, leaf.nMarkSurfaces,
		(int)leaf.nMins.x, (int)leaf.nMins.y, (int)leaf.nMins.z,
		(int)leaf.nMaxs.x, (int)leaf.nMaxs.y, (int)leaf.nMaxs.z);
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
		uint32_t idx = searchNodes.front();
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

void Bsp::get_child_leaves(int iNode, vector<int>& leaves) {
	BSPNODE& node = nodes[iNode];

	for (int i = 0; i < 2; i++) {
		if (node.iChildren[i] >= 0) {
			get_child_leaves(node.iChildren[i], leaves);
		}
		else if (~node.iChildren[i] != 0) {
			leaves.push_back(~node.iChildren[i]);
		}
	}
}

void Bsp::get_simple_leaf_branch(int iNode, vector<int>& branch) {
	BSPNODE& node = nodes[iNode];

	if (node.iChildren[0] >= 0 && node.iChildren[1] >= 0)
		return; // stop at forks

	branch.push_back(iNode);

	for (int i = 0; i < 2; i++) {
		if (node.iChildren[i] >= 0) {
			get_simple_leaf_branch(node.iChildren[i], branch);
		}
	}
}

void Bsp::merge_simple_leaf_chains() {
	int headNode = models[0].iHeadnodes[0];

	unordered_set<int> visitedNodes;
	vector<vector<int>> mergeNodeChains;

	std::queue<int> q;
	q.push(headNode);

	while (!q.empty()) {
		int idx = q.front();
		BSPNODE& node = nodes[idx];
		q.pop();

		bool isForkNode = node.iChildren[0] >= 0 && node.iChildren[1] >= 0;

		if (!isForkNode && !visitedNodes.count(idx)) {
			vector<int> branch;
			get_simple_leaf_branch(idx, branch);

			// parition the branch into chains that can place merged leafs at the bottom
			vector<int> chain;

			for (int k = branch.size() - 1; k >= 0; k--) {
				BSPNODE& node = nodes[branch[k]];

				bool validLeaf0 = node.iChildren[0] < 0 && ~node.iChildren[0] != 0;
				bool validLeaf1 = node.iChildren[1] < 0 && ~node.iChildren[1] != 0;

				if (!validLeaf0 && !validLeaf1) {
					continue;
				}

				// leaf attached to right child resets chain. Leaves attach to the highest node
				// right child or lowest node left child. See Mod_SetParent in xash. Leaf must
				// attach to the bottom so that all nodes are marked visible when the leaf is in PVS.
				if (validLeaf1) {
					if (chain.size() >= 1)
						mergeNodeChains.push_back(chain);
					chain.clear();
				}

				chain.push_back(branch[k]);
				visitedNodes.insert(branch[k]);
			}

			if (chain.size() >= 1)
				mergeNodeChains.push_back(chain);
		}

		for (int i = 0; i < 2; i++) {
			if (node.iChildren[i] >= 0)
				q.push(node.iChildren[i]);
		}
	}

	int totalReduce = 0;
	byte* decompressedVis = decompress_vis();

	for (int i = 0; i < mergeNodeChains.size(); i++) {
		totalReduce += merge_leaves(mergeNodeChains[i], decompressedVis, false);
	}

	compress_vis(decompressedVis);

	logf("Merged %d world leaves\n", totalReduce);
}

void Bsp::merge_sky_leaves() {
	int headNode = models[0].iHeadnodes[0];

	std::queue<int> q;
	q.push(headNode);

	unordered_set<int> skyLeaves;

	insert_leaves(models[0].nVisLeafs + 1, 1);
	int sharedSkyLeafIdx = models[0].nVisLeafs + 1;
	leaves[sharedSkyLeafIdx].nContents = CONTENTS_SKY;

	while (!q.empty()) {
		int idx = q.front();
		BSPNODE& node = nodes[idx];
		q.pop();

		for (int i = 0; i < 2; i++) {
			if (node.iChildren[i] >= 0)
				q.push(node.iChildren[i]);
			else {
				int leafIdx = ~node.iChildren[i];
				BSPLEAF& leaf = leaves[leafIdx];
				if (leaves[leafIdx].nContents == CONTENTS_SKY) {

					bool anyNormalFaces = false;
					for (int k = 0; k < leaf.nMarkSurfaces; k++) {
						BSPFACE& face = faces[marksurfs[leaf.iFirstMarkSurface + k]];
						if (!(texinfos[face.iTextureInfo].nFlags & TEX_SPECIAL)) {
							anyNormalFaces = true;
							break;
						}
					}

					if (!anyNormalFaces) {
						skyLeaves.insert(leafIdx);
						node.iChildren[i] = ~sharedSkyLeafIdx;
					}
				}
			}
		}
	}

	int mergedSky = 0;
	if (skyLeaves.size())
		mergedSky = skyLeaves.size() - 1;

	logf("Merged %d sky leaves\n", mergedSky);
}

void Bsp::get_terminal_leaves(int iNode, vector<int>& terminalLeaves) {
	BSPNODE& node = nodes[iNode];

	bool bothAreLeaves = true;

	for (int i = 0; i < 2; i++) {
		if (node.iChildren[i] >= 0) {
			get_terminal_leaves(node.iChildren[i], terminalLeaves);
			bothAreLeaves = false;
		}
	}

	if (bothAreLeaves) {
		int ileaf1 = ~node.iChildren[0];
		int ileaf2 = ~node.iChildren[1];

		if (ileaf1 != 0) {
			terminalLeaves.push_back(ileaf1);
		}
		if (ileaf2 != 0) {
			terminalLeaves.push_back(ileaf2);
		}
	}
}

void Bsp::get_leaf_counts(int iNode, unordered_map<int, int>& leafCounts) {
	BSPNODE& node = nodes[iNode];

	for (int i = 0; i < 2; i++) {
		if (node.iChildren[i] >= 0) {
			get_leaf_counts(node.iChildren[i], leafCounts);
		}
		else {
			leafCounts[~node.iChildren[i]] += 1;
		}
	}
}

int Bsp::get_leaf_graph(int iNode, vector<GraphNode>& gnodes, int depth, bool includeSolid) {
	BSPNODE& node = nodes[iNode];

	GraphNode gnode;
	gnode.nodeIdx = iNode;
	gnode.depth = depth;
	gnode.nodeType = 0;

	int maxDepth = depth + 1;

	for (int i = 0; i < 2; i++) {
		if (node.iChildren[i] >= 0) {
			gnode.links[i] = node.iChildren[i];
			maxDepth = max(get_leaf_graph(node.iChildren[i], gnodes, depth + 1, includeSolid), maxDepth);
		}
		else {
			gnode.links[i] = ((uint64_t)(~node.iChildren[i] + 1) << 32) | ((uint64_t)iNode << 1) | i;

			GraphNode leaf;
			leaf.nodeIdx = ((uint64_t)(~node.iChildren[i] + 1) << 32) | ((uint64_t)iNode << 1) | i;
			leaf.depth = depth + 1;
			leaf.links[0] = -1;
			leaf.links[1] = -1;
			leaf.nodeType = ~node.iChildren[i] == 0 ? -1 : 1;

			if (!includeSolid && leaf.nodeType == -1)
				continue;

			gnodes.push_back(leaf);
		}
	}

	gnodes.push_back(gnode);
	return maxDepth;
}

int Bsp::merge_leaves(vector<int>& inodes, bool discardVis) {
	byte* decompressedVis = decompress_vis();

	int ret = merge_leaves(inodes, decompressedVis, discardVis);

	compress_vis(decompressedVis);

	return ret;
}

int Bsp::merge_leaves(vector<int>& inodes, byte* decompressedVis, bool discardVis) {
	if (inodes.empty())
		return 0;

	vector<int> ileaves;

	if (discardVis) {
		ileaves = inodes;

		// get the actual nodes
		inodes.clear();
		unordered_set<int> findLeaves;
		for (int idx : ileaves) {
			findLeaves.insert(idx);
		}
		get_leaf_parents(models[0].iHeadnodes[0], findLeaves, inodes);
	}
	else {
		unordered_set<int> addedLeaves;

		for (int idx : inodes) {
			BSPNODE& node = nodes[idx];

			for (int i = 0; i < 2; i++) {
				if (node.iChildren[i] < 0 && ~node.iChildren[i] != 0) {
					int leafIdx = ~node.iChildren[i];

					if (!addedLeaves.count(leafIdx)) {
						ileaves.push_back(leafIdx);
						addedLeaves.insert(leafIdx);
					}
				}
			}
		}
	}

	if (ileaves.size() < 2)
		return 0;

	uint32_t rootIdx = ileaves[0]; // this leaf will expand to contain all merged leaves
	BSPLEAF& root = leaves[rootIdx];

	// replace target leaves with merged leaf idx
	for (int idx : inodes) {
		BSPNODE& parent = nodes[idx];

		for (int i = 0; i < 2; i++) {
			if (parent.iChildren[i] < 0 && ~parent.iChildren[i] != 0) {
				parent.iChildren[i] = ~rootIdx;
			}
		}
	}

	int visLeafCount = leafCount - 1;
	uint visRowSize = ((visLeafCount + 63) & ~63) >> 3;
	int decompressedVisSize = visLeafCount * visRowSize;

	byte* visRowRoot = decompressedVis + (rootIdx - 1) * visRowSize;

	int rootLeafIdx = rootIdx - 1;
	int rootByteOffset = rootLeafIdx / 8;
	int rootBitOffset = 1 << (rootLeafIdx % 8);

	unordered_set<int> existingFaces;
	vector<int> allFaces;
	vector<int> newFaces;

	for (int i = 0; i < root.nMarkSurfaces; i++) {
		existingFaces.insert(marksurfs[root.iFirstMarkSurface + i]);
		allFaces.push_back(marksurfs[root.iFirstMarkSurface + i]);
	}

	// merge leaf structs and update VIS
	for (int idx : ileaves) {
		if (idx == rootIdx)
			continue;

		BSPLEAF& leaf = leaves[idx];

		// expand bounding box
		expandBoundingBox(leaf.nMins, root.nMins, root.nMaxs);
		expandBoundingBox(leaf.nMaxs, root.nMins, root.nMaxs);

		for (int k = 0; k < leaf.nMarkSurfaces; k++) {
			int face = marksurfs[leaf.iFirstMarkSurface + k];

			if (!existingFaces.count(face)) {
				newFaces.push_back(face);
				allFaces.push_back(face);
			}
		}

		// unlink faces
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

		for (int i = 1; i <= visLeafCount; i++) {
			byte* visRow = decompressedVis + (i - 1) * visRowSize;

			if (visRow[mergedByteOffset] & mergedBitOffset) {
				visRow[rootByteOffset] |= rootBitOffset;
			}
		}
	}

	// append marksurfs for the merged leaf
	if (!discardVis) {
		if (newFaces.size()) {
			if (root.nMarkSurfaces == 0) {
				// leaf had no faces attached to begin with, the first mark offset is likely wrong,
				// so append to the end to be safe.
				root.iFirstMarkSurface = marksurfCount;
			}

			insert_marksurfs(root.iFirstMarkSurface + root.nMarkSurfaces, newFaces.size());

			// add new faces to merged leaf
			root.nMarkSurfaces = allFaces.size();
			for (int i = 0; i < root.nMarkSurfaces; i++) {
				marksurfs[root.iFirstMarkSurface + i] = allFaces[i];
			}
		}
	}
	else {
		// don't care about face visibility
		root.nMarkSurfaces = 0;
		root.iFirstMarkSurface = 0;
	}

	return ileaves.size() - 1;
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

void Bsp::add_face_to_leaf(int faceIdx, int leafIdx) {
	if (leafIdx <= 0 || leafIdx >= leafCount || faceIdx < 0 || faceIdx >= faceCount)
		return;

	BSPLEAF& leaf = leaves[leafIdx];

	bool alreadyAdded = false;
	for (int i = 0; i < leaf.nMarkSurfaces; i++) {
		if (marksurfs[leaf.iFirstMarkSurface + i] == faceIdx) {
			alreadyAdded = true;
			break;
		}
	}

	if (alreadyAdded) {
		logf("Face %d is already marked by leaf %d\n", faceIdx, leafIdx);
		return;
	}

	debugf("Linked face %d to leaf %d\n", faceIdx, leafIdx);

	BSPMARKSURF* newMarks = new BSPMARKSURF[marksurfCount + 1];
	int copyEnd = leaf.iFirstMarkSurface + leaf.nMarkSurfaces;
	memcpy(newMarks, marksurfs, sizeof(BSPMARKSURF) * copyEnd);
	memcpy(newMarks + copyEnd + 1, marksurfs + copyEnd, sizeof(BSPMARKSURF) * (marksurfCount - copyEnd));

	newMarks[leaf.iFirstMarkSurface + leaf.nMarkSurfaces] = faceIdx;
	leaf.nMarkSurfaces += 1;

	for (int i = 1; i < leafCount; i++) {
		BSPLEAF& otherLeaf = leaves[i];
		if (i != leafIdx && otherLeaf.iFirstMarkSurface >= copyEnd) {
			otherLeaf.iFirstMarkSurface++;
		}
	}

	replace_lump(LUMP_MARKSURFACES, newMarks, (marksurfCount + 1) * sizeof(BSPMARKSURF));
}

bool Bsp::add_face_to_touched_leaves(int faceIdx) {
	if (faceIdx < 0 || faceIdx >= faceCount)
		return false;

	if (!g_app->mapRenderer->leafNavMesh) {
		g_app->mapRenderer->reloadLeaves(true);
	}

	LeafNavMesh* mesh = g_app->mapRenderer->leafNavMesh;

	LeafNode dummy;
	get_face_bounding_box(faceIdx, dummy.mins, dummy.maxs);
	vector<bool> regionLeaves(leafCount);

	mesh->octree->getLeavesInRegion(&dummy, regionLeaves);

	vector<vec3> verts;
	get_face_verts(faceIdx, verts);
	Polygon3D poly(verts);

	bool linkedToAny = false;

	for (int i = 0; i < mesh->nodes.size(); i++) {
		if (!regionLeaves[i])
			continue;

		LeafNode& node = mesh->nodes[i];

		for (int k = 0; k < node.leafFaces.size(); k++) {
			if (poly.overlapping(node.leafFaces[k])) {
				add_face_to_leaf(faceIdx, node.leafIdx);
				linkedToAny = true;
				break;
			}
		}
	}

	return linkedToAny;
}

void Bsp::get_leaf_parents(int iNode, unordered_set<int>& leaves, vector<int>& parents) {
	BSPNODE& node = nodes[iNode];

	for (int i = 0; i < 2; i++) {
		if (node.iChildren[i] >= 0) {
			get_leaf_parents(node.iChildren[i], leaves, parents);
		}
		else if (leaves.count(~node.iChildren[i])) {
			parents.push_back(iNode);
		}
	}
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

vector<int> Bsp::get_leaf_faces(vector<int>& ileaves) {
	unordered_set<int> markedFaces;
	vector<int> allLeafFaces;

	for (int i = 0; i < ileaves.size(); i++) {
		BSPLEAF& leaf = leaves[ileaves[i]];

		for (int i = 0; i < leaf.nMarkSurfaces; i++) {
			int faceIdx = marksurfs[leaf.iFirstMarkSurface + i];
			if (markedFaces.count(faceIdx))
				continue;

			allLeafFaces.push_back(faceIdx);
			markedFaces.insert(faceIdx);
		}
	}

	return allLeafFaces;
}

int Bsp::convert_leaves_to_model(vector<int>& leafIndexes) {
	vector<int> allLeafFaces = get_leaf_faces(leafIndexes);
	int modelIdx = create_model_from_faces(allLeafFaces);

	merge_leaves(leafIndexes, true);

	delete_faces(allLeafFaces);

	// lazy fix for some structures still being shared, which corruptes the model when worldspawn
	// is transformed.
	int dupIdx = duplicate_model(modelIdx);

	return dupIdx;
}

void Bsp::count_leaves(int iNode, int& leafCount) {
	BSPNODE& node = nodes[iNode];

	for (int i = 0; i < 2; i++) {
		if (node.iChildren[i] >= 0) {
			count_leaves(node.iChildren[i], leafCount);
		}
		else {
			int32_t leafIdx = ~node.iChildren[i];
			if (leafIdx > leafCount)
				leafCount = leafIdx;
		}
	}
}
