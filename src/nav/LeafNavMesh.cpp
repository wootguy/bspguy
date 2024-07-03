#include "LeafNavMesh.h"
#include "GLFW/glfw3.h"
#include "PolyOctree.h"
#include "Clipper.h"
#include "util.h"
#include <string.h>

bool LeafNavNode::addLink(int node, Polygon3D linkArea) {	
	for (int i = 0; i < MAX_NAV_LEAF_LINKS; i++) {
		if (links[i].node == node || links[i].node == -1) {
			links[i].linkArea = linkArea;
			links[i].node = node;
			return true;
		}
	}

	logf("Error: Max links reached on node %d\n", id);
	return false;
}

int LeafNavNode::numLinks() {
	int numLinks = 0;

	for (int i = 0; i < MAX_NAV_LEAF_LINKS; i++) {
		if (links[i].node == -1) {
			break;
		}
		numLinks++;
	}

	return numLinks;
}

LeafNavMesh::LeafNavMesh() {
	clear();
}

void LeafNavMesh::clear() {
	memset(nodes, 0, sizeof(LeafNavNode) * MAX_NAV_LEAVES);
	memset(leafMap, 65535, sizeof(uint16_t) * MAX_MAP_CLIPNODE_LEAVES);

	for (int i = 0; i < MAX_NAV_LEAVES; i++) {
		leaves[i] = LeafMesh();
		nodes[i].id = i;

		for (int k = 0; k < MAX_NAV_LEAF_LINKS; k++) {
			nodes[i].links[k].linkArea = Polygon3D();
			nodes[i].links[k].node = -1;
		}
	}
}

LeafNavMesh::LeafNavMesh(vector<LeafMesh> inleaves) {
	clear();

	for (int i = 0; i < inleaves.size(); i++) {
		leaves[i] = inleaves[i];
	}
	numLeaves = inleaves.size();

	logf("Created leaf nav mesh with %d leaves (x%d = %d KB)\n", 
		numLeaves, sizeof(LeafNavNode), (sizeof(LeafNavNode)* numLeaves) / 1024);

	logf("LeafNavNode = %d bytes, LeafNavLink = %d bytes\n",
		sizeof(LeafNavNode), sizeof(LeafNavLink));
}

bool LeafNavMesh::addLink(int from, int to, Polygon3D linkArea) {
	if (from < 0 || to < 0 || from >= MAX_NAV_LEAVES || to >= MAX_NAV_LEAVES) {
		logf("Error: add link from/to invalid node %d %d\n", from, to);
		return false;
	}

	if (!nodes[from].addLink(to, linkArea)) {
		vec3& pos = leaves[from].center;
		logf("Failed to add link at %d %d %d\n", (int)pos.x, (int)pos.y, (int)pos.z);
		return false;
	}

	return true;
}
