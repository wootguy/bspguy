#pragma once
#include "bsptypes.h"
#include "Polygon3D.h"

// https://www.geometrictools.com/Documentation/ClipMesh.pdf

struct CVertex {
	vec3 pos;
	float distance = 0;
	int occurs = 0;
	bool visible = true;

	CVertex(vec3 pos) : pos(pos) {}
};

struct CEdge {
	int verts[2];
	int faces[2];
	bool visible = true;

	CEdge(int v1, int v2) {
		verts[0] = v1;
		verts[1] = v2;
		faces[0] = -1;
		faces[1] = -1;
	}

	CEdge(int v1, int v2, int f1, int f2) {
		verts[0] = v1;
		verts[1] = v2;
		faces[0] = f1;
		faces[1] = f2;
	}
};

struct CFace {
	vector<int> edges;
	bool visible = true;
	vec3 normal;

	CFace(vector<int> edges, vec3 normal) {
		this->edges = edges;
		this->normal = normal;
	}
};

struct CMesh {
	vector<CVertex> verts;
	vector<CEdge> edges;
	vector<CFace> faces;
};

class Clipper {
public:

	Clipper();

	// clips a box against the list of clipping planes, in order, to create a convex volume
	CMesh clip(vector<BSPPLANE>& clips);

	// load mesh from a set of polygons and clip it by another poly
	// 0 = nothing clipped
	// 1 = clipped
	// -1 = everything clipped
	int clip(vector<Polygon3D>& polys, BSPPLANE& clipPoly, CMesh& mesh);

private:

	int clipVertices(CMesh& mesh, BSPPLANE& clip);
	void clipEdges(CMesh& mesh, BSPPLANE& clip);
	void clipFaces(CMesh& mesh, BSPPLANE& clip);
	bool getOpenPolyline(CMesh& mesh, CFace& face, int& start, int& final);

	CMesh createMaxSizeVolume();
};