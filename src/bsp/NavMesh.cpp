#include "NavMesh.h"
#include "GLFW/glfw3.h"
#include "PolyOctree.h"
#include "Clipper.h"
#include "util.h"

bool NavNode::addLink(int node, int srcEdge, int dstEdge, int16_t zDist, uint8_t flags) {
	if (srcEdge < 0 || srcEdge >= MAX_NAV_POLY_VERTS) {
		logf("Error: add link to invalid src edge %d\n", srcEdge);
		return false;
	}
	if (dstEdge < 0 || dstEdge >= MAX_NAV_POLY_VERTS) {
		logf("Error: add link to invalid dst edge %d\n", dstEdge);
		return false;
	}
	
	for (int i = 0; i < MAX_NAV_LINKS; i++) {
		if (links[i].node == node) {
			links[i].srcEdge = srcEdge;
			links[i].dstEdge = dstEdge;
			links[i].zDist = zDist;
			links[i].flags = flags;
			return true;
		}
		if (links[i].node == -1) {
			links[i].srcEdge = srcEdge;
			links[i].dstEdge = dstEdge;
			links[i].node = node;
			links[i].zDist = zDist;
			links[i].flags = flags;
			return true;
		}
	}

	logf("Error: Max links reached on node %d\n", id);
	return false;
}

int NavNode::numLinks() {
	int numLinks = 0;

	for (int i = 0; i < MAX_NAV_LINKS; i++) {
		if (links[i].node == -1) {
			break;
		}
		numLinks++;
	}

	return numLinks;
}

NavMesh::NavMesh() {
	clear();
}

void NavMesh::clear() {
	memset(nodes, 0, sizeof(NavNode) * MAX_NAV_POLYS);

	for (int i = 0; i < MAX_NAV_POLYS; i++) {
		polys[i] = Polygon3D();
		nodes[i].id = i;

		for (int k = 0; k < MAX_NAV_LINKS; k++) {
			nodes[i].links[k].srcEdge = 0;
			nodes[i].links[k].dstEdge = 0;
			nodes[i].links[k].node = -1;
			nodes[i].links[k].zDist = 0;
		}
	}
}

NavMesh::NavMesh(vector<Polygon3D> faces) {
	clear();

	for (int i = 0; i < faces.size(); i++) {
		polys[i] = Polygon3D(faces[i].verts);
		if (faces[i].verts.size() > MAX_NAV_POLY_VERTS)
			logf("Error: Face %d has %d verts (max is %d)\n", i, faces[i].verts.size(), MAX_NAV_POLY_VERTS);
	}
	numPolys = faces.size();

	logf("Created nav mesh with %d polys (x%d = %d KB)\n", 
		numPolys, sizeof(NavNode), (sizeof(NavNode)*numPolys) / 1024);

	logf("NavPolyNode = %d bytes, NavLink = %d bytes\n",
		sizeof(NavNode), sizeof(NavLink));
}

bool NavMesh::addLink(int from, int to, int srcEdge, int dstEdge, int16_t zDist, uint8_t flags) {
	if (from < 0 || to < 0 || from >= MAX_NAV_POLYS || to >= MAX_NAV_POLYS) {
		logf("Error: add link from/to invalid node %d %d\n", from, to);
		return false;
	}

	if (!nodes[from].addLink(to, srcEdge, dstEdge, zDist, flags)) {
		vec3& pos = polys[from].center;
		logf("Failed to add link at %d %d %d\n", (int)pos.x, (int)pos.y, (int)pos.z);
		return false;
	}

	return true;
}

vector<Polygon3D> NavMesh::getPolys() {
	vector<Polygon3D> ret;

	for (int i = 0; i < numPolys; i++) {
		ret.push_back(polys[i]);
	}

	return ret;
}

void NavMesh::getLinkMidPoints(int iNode, int iLink, vec3& srcMid, vec3& dstMid) {
	srcMid = dstMid = vec3();
	if (iNode < 0 || iNode >= MAX_NAV_POLYS) {
		return;
	}
	if (iLink < 0 || iLink >= MAX_NAV_LINKS) {
		return;
	}

	NavLink& link = nodes[iNode].links[iLink];
	if (link.node < 0 || link.node >= MAX_NAV_POLYS) {
		return;
	}

	Polygon3D& srcPoly = polys[iNode];
	Polygon3D& dstPoly = polys[link.node];

	int e2i = (link.srcEdge + 1) % srcPoly.verts.size();
	int e4i = (link.dstEdge + 1) % dstPoly.verts.size();
	vec2 e1 = srcPoly.topdownVerts[link.srcEdge];
	vec2 e2 = srcPoly.topdownVerts[e2i];
	vec2 e3 = dstPoly.topdownVerts[link.dstEdge];
	vec2 e4 = dstPoly.topdownVerts[e4i];

	float t0, t1, t2, t3;
	float overlapDist = Line2D(e1, e2).getOverlapRanges(Line2D(e3, e4), t0, t1, t2, t3);

	{
		vec3 edgeStart = srcPoly.verts[link.srcEdge];
		vec3 edgeDelta = srcPoly.verts[e2i] - edgeStart;
		vec3 borderStart = edgeStart + edgeDelta * t0;
		vec3 borderEnd = edgeStart + edgeDelta * t1;
		srcMid = borderStart + (borderEnd - borderStart) * 0.5f;
	}

	{
		vec3 edgeStart = dstPoly.verts[link.dstEdge];
		vec3 edgeDelta = dstPoly.verts[e4i] - edgeStart;
		vec3 borderStart = edgeStart + edgeDelta * t2;
		vec3 borderEnd = edgeStart + edgeDelta * t3;
		dstMid = borderStart + (borderEnd - borderStart) * 0.5f;
	}
}
