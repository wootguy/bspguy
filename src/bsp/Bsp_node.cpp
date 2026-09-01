#include "Bsp.h"
#include "util.h"
#include "LeafNavMesh.h"
#include "LeafOctree.h"
#include "BspRenderer.h"

#include <float.h>
#include <algorithm>
#include <limits.h>

void Bsp::print_model_bsp(int modelIdx) {
	int node = models[modelIdx].iHeadnodes[0];
	recurse_node(node, 0);
}

void Bsp::recurse_node(int32_t nodeIdx, int depth) {
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

	recurse_node(nodes[nodeIdx].iChildren[0], depth + 1);
	recurse_node(nodes[nodeIdx].iChildren[1], depth + 1);
}

void Bsp::print_node(int nodeidx) {
	BSPNODE& node = nodes[nodeidx];
	BSPPLANE& plane = planes[node.iPlane];

	logf("Node %d, Plane (%f %f %f) d: %f, Faces: %d, Min(%d, %d, %d), Max(%d, %d, %d)",
		nodeidx,
		plane.vNormal.x, plane.vNormal.y, plane.vNormal.z,
		plane.fDist, node.nFaces,
		node.nMins.x, node.nMins.y, node.nMins.z,
		node.nMaxs.x, node.nMaxs.y, node.nMaxs.z);
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

int Bsp::get_lowest_common_node(vector<int>& leaves) {
	struct LeafBranch {
		int parent;
		vector<int> branch;
	};

	vector<LeafBranch> branches;

	int inode = models[0].iHeadnodes[0];
	int smallestBranch = INT_MAX;

	for (int i = 0; i < leaves.size(); i++) {
		LeafBranch branch;
		branch.parent = get_node_branch(inode, branch.branch, leaves[i]);
		branches.push_back(branch);

		if (branch.branch.size() < smallestBranch) {
			smallestBranch = branch.branch.size() - 1; // exclude leaf
		}
	}

	// find the lowest common parent node
	int irootNode = 0;
	for (int i = 0; i < smallestBranch; i++) {

		int lastNode = -1;
		bool allNodesMatch = true;

		for (int k = 0; k < branches.size(); k++) {
			LeafBranch& b = branches[k];
			int nodeIdx = b.branch[(b.branch.size() - 1) - i];
			if (lastNode == -1) {
				lastNode = nodeIdx;
			}
			else if (lastNode != nodeIdx) {
				allNodesMatch = false;
				break;
			}
		}

		if (allNodesMatch) {
			irootNode = lastNode;
		}
		else {
			break; // branches split off
		}
	}

	return irootNode;
}

bool Bsp::node_branch_has_forks(int iNode) {
	BSPNODE& node = nodes[iNode];

	if (node.iChildren[0] >= 0 && node.iChildren[1] >= 0) {
		return true;
	}

	for (int i = 0; i < 2; i++) {
		if (node.iChildren[i] >= 0 && node_branch_has_forks(node.iChildren[i]))
			return true;
	}

	return false;
}

void Bsp::get_child_nodes(int iNode, int depth, vector<NodeDepth>& childNodes) {
	BSPNODE& node = nodes[iNode];

	NodeDepth dnode;
	dnode.nodeIdx = iNode;
	dnode.depth = depth;
	childNodes.push_back(dnode);

	for (int i = 0; i < 2; i++) {
		if (node.iChildren[i] >= 0) {
			get_child_nodes(node.iChildren[i], depth + 1, childNodes);
		}
	}
}

int Bsp::get_node_parent(int iNode, int childIdx) {
	BSPNODE& node = nodes[iNode];

	for (int i = 0; i < 2; i++) {
		if (node.iChildren[i] == childIdx) {
			return iNode;
		}
		if (node.iChildren[i] >= 0) {
			int idx = get_node_parent(node.iChildren[i], childIdx);
			if (idx != -1)
				return idx;
		}
	}

	return -1;
}

void Bsp::get_node_faces(int iNode, vector<int>& faces) {
	BSPNODE& node = nodes[iNode];

	for (int i = 0; i < node.nFaces; i++)
		faces.push_back(node.firstFace + i);

	for (int i = 0; i < 2; i++) {
		if (node.iChildren[i] >= 0) {
			get_node_faces(node.iChildren[i], faces);
		}
	}
}

bool Bsp::node_branch_faces_are_consecutive(int iNode) {
	vector<int> faces;
	get_node_faces(iNode, faces);

	sort(faces.begin(), faces.end());

	if (faces.empty())
		return true;

	bool consecutive = true;
	int lastIdx = faces[0];
	for (int idx : faces) {
		if (idx - lastIdx > 1) {
			consecutive = false;
			break;
		}
		lastIdx = idx;
	}

	return consecutive;
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
			int v1 = (i + 1) % solid.hullVerts.size();
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

		for (int k = 0; k < solid.faces.size(); k++) {
			BSPNODE& node = newNodes[nodeCount + k];
			memset(&node, 0, sizeof(BSPNODE));

			node.firstFace = startFace + k; // face required for decals
			node.nFaces = 1;
			node.iPlane = startPlane + k;
			// node mins/maxs don't matter for submodels. Leave them at 0.

			int16 insideContents = k == solid.faces.size() - 1 ? ~sharedSolidLeaf : (int16)(nodeCount + k + 1);
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

		static vec3 faceUp[6]{
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
			face.iTextureInfo = startTexinfo + i;
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

		for (int k = 0; k < 6; k++) {
			BSPNODE& node = newNodes[nodeCount + k];
			memset(&node, 0, sizeof(BSPNODE));

			node.firstFace = startFace + k; // face required for decals
			node.nFaces = 1;
			node.iPlane = startPlane + k;
			// node mins/maxs don't matter for submodels. Leave them at 0.

			int16 insideContents = k == 5 ? ~sharedSolidLeaf : (int16)(nodeCount + k + 1);
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

void Bsp::getNodePlanes(int iNode, vector<int>& nodePlanes) {
	BSPNODE& node = nodes[iNode];
	nodePlanes.push_back(node.iPlane);

	for (int i = 0; i < 2; i++) {
		if (node.iChildren[i] >= 0) {
			getNodePlanes(node.iChildren[i], nodePlanes);
		}
	}
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

void Bsp::make_nodes_contiguous() {
	// make world nodes contiguous (fixes crash in xash3d)
	STRUCTUSAGE worldNodeMarks(this);
	mark_node_structures(0, &worldNodeMarks, true);

	BSPNODE* newNodeOrder = new BSPNODE[nodeCount];
	int* nodesRemap = new int[nodeCount];
	int order = 0;

	// world nodes first
	for (int i = 0; i < nodeCount; i++) {
		if (worldNodeMarks.nodes[i]) {
			newNodeOrder[order] = nodes[i];
			nodesRemap[i] = order;
			order++;
		}
	}

	// entity nodes next
	for (int i = 0; i < nodeCount; i++) {
		if (!worldNodeMarks.nodes[i]) {
			newNodeOrder[order] = nodes[i];
			nodesRemap[i] = order;
			order++;
		}
	}

	for (int i = 0; i < nodeCount; i++) {
		BSPNODE& node = newNodeOrder[i];

		for (int k = 0; k < 2; k++) {
			if (node.iChildren[k] >= 0) {
				node.iChildren[k] = nodesRemap[node.iChildren[k]];
			}
		}
	}

	for (int i = 0; i < modelCount; i++) {
		if (models[i].iHeadnodes[0] >= 0)
			models[i].iHeadnodes[0] = nodesRemap[models[i].iHeadnodes[0]];
	}

	delete[] nodesRemap;
	replace_lump(LUMP_NODES, newNodeOrder, nodeCount * sizeof(BSPNODE));

	make_clipnodes_contiguous();
}

void Bsp::delete_oob_nodes(int iNode, int32_t* parentBranch, vector<BSPPLANE>& clipOrder, int oobFlags,
	bool* oobHistory, bool isFirstPass, int& removedNodes) {
	if (iNode < 0 || iNode >= nodeCount)
		return;

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
		*parentBranch = ~0; // solid leaf
		removedNodes++;
	}
}

void Bsp::delete_box_nodes(int iNode, int32_t* parentBranch, vector<BSPPLANE>& clipOrder,
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
