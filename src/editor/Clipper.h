#include "util.h"
#include "bsptypes.h"
#include "primitives.h"
#include "VertexBuffer.h"

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
	std::vector<int> edges;
	bool visible = true;
	vec3 normal;

	CFace(std::vector<int> edges, vec3 normal) {
		this->edges = std::move(edges);
		this->normal = normal;
	}
};

struct CMesh {
	std::vector<CVertex> verts;
	std::vector<CEdge> edges;
	std::vector<CFace> faces;
};

class Clipper {
public:

	Clipper();

	// clips a box against the list of clipping planes, in order, to create a convex volume
	CMesh clip(std::vector<BSPPLANE>& clips);

private:

	int clipVertices(CMesh& mesh, BSPPLANE& clip);
	void clipEdges(CMesh& mesh, BSPPLANE& clip);
	void clipFaces(CMesh& mesh, BSPPLANE& clip);
	bool getOpenPolyline(CMesh& mesh, CFace& face, int& start, int& final);

	CMesh createMaxSizeVolume();
};