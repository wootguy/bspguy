#include "NavMesh.h"
#include "GLFW/glfw3.h"
#include "PolyOctree.h"
#include "Clipper.h"

NavMesh::NavMesh() {
	clear();
}

void NavMesh::clear() {
	memset(nodes, 0, sizeof(NavPolyNode) * MAX_NAV_POLYS);

	for (int i = 0; i < MAX_NAV_POLYS; i++) {
		polys[i] = Polygon3D();
	}
}

NavMesh::NavMesh(vector<Polygon3D> faces) {
	clear();

	for (int i = 0; i < faces.size(); i++) {
		polys[i] = faces[i];
		if (faces[i].verts.size() > MAX_NAV_POLY_VERTS)
			logf("Error: Face %d has %d verts (max is %d)\n", i, faces[i].verts.size(), MAX_NAV_POLY_VERTS);
	}
	numPolys = faces.size();

	logf("Created nav mesh with %d polys (x%d = %d KB)\n", 
		numPolys, sizeof(NavPolyNode), (sizeof(NavPolyNode)*numPolys) / 1024);

	logf("NavPolyNode = %d bytes, NavLink = %d bytes\n",
		sizeof(NavPolyNode), sizeof(NavLink));
}

vector<Polygon3D> NavMesh::getPolys() {
	vector<Polygon3D> ret;

	for (int i = 0; i < numPolys; i++) {
		ret.push_back(polys[i]);
	}

	return ret;
}
