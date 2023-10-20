#include "Clipper.h"
#include <set>
#include "bsp.h"

Clipper::Clipper() {

}

CMesh Clipper::clip(vector<BSPPLANE>& clips) {
	CMesh mesh = createMaxSizeVolume();

	for (int i = 0; i < clips.size(); i++) {
		BSPPLANE clip = clips[i];

		int result = clipVertices(mesh, clip);

		if (result == -1) {
			// everything clipped
			return CMesh();
		}
		if (result == 1) {
			// nothing clipped
			continue;
		}

		clipEdges(mesh, clip);
		clipFaces(mesh, clip);
	}

	return mesh;
}

int Clipper::clipVertices(CMesh& mesh, BSPPLANE& clip) {
	int positive = 0;
	int negative = 0;

	for (int i = 0; i < mesh.verts.size(); i++) {
		CVertex& vert = mesh.verts[i];

		if (vert.visible) {
			vert.distance = dotProduct(clip.vNormal, vert.pos) - clip.fDist;

			if (vert.distance >= EPSILON) {
				positive++;
			}
			else if (vert.distance <= EPSILON) {
				negative++;
				vert.visible = false;
			}
			else {
				// on the plane
				vert.distance = 0;
			}
		}
	}

	if (negative == 0) {
		return 1; // all verts on positive side, no clipping
	}

	if (positive == 0) {
		return -1; // all verts on negative side, everything clipped
	}

	return 0;
}

void Clipper::clipEdges(CMesh& mesh, BSPPLANE& clip) {
	for (int i = 0; i < mesh.edges.size(); i++) {
		CEdge& edge = mesh.edges[i];
		CVertex& v0 = mesh.verts[edge.verts[0]];
		CVertex& v1 = mesh.verts[edge.verts[1]];

		if (edge.visible) {
			float d0 = v0.distance;
			float d1 = v1.distance;

			if (d0 <= 0 && d1 <= 0) {
				// edge is culled, remove edge from faces sharing it
				for (int k = 0; k < 2; k++) {
					CFace& face = mesh.faces[edge.faces[k]];
					for (int e = 0; e < face.edges.size(); e++) {
						if (face.edges[e] == i) {
							face.edges.erase(face.edges.begin() + e);
							break;
						}
					}
					if (face.edges.empty()) {
						face.visible = false;
					}
				}

				edge.visible = false;
				continue;
			}

			if (d0 >= 0 && d1 >= 0) {
				continue; // edge is on positive side, no clipping
			}

			// the edge is split by the plane. Compute the point of intersection.

			float t = d0 / (d0 - d1);
			vec3 intersect = v0.pos*(1 - t) + v1.pos * t;
			int idx = mesh.verts.size();
			mesh.verts.push_back(intersect);

			if (d0 > 0) {
				edge.verts[1] = idx;
			}
			else {
				edge.verts[0] = idx;
			}
		}
	}
}

void Clipper::clipFaces(CMesh& mesh, BSPPLANE& clip) {
	CFace closeFace({}, clip.vNormal.invert());
	int findex = mesh.faces.size();

	for (int i = 0; i < mesh.faces.size(); i++) {
		CFace& face = mesh.faces[i];

		if (face.visible) {
			for (int e = 0; e < face.edges.size(); e++) {
				CEdge& edge = mesh.edges[face.edges[e]];
				mesh.verts[edge.verts[0]].occurs = 0;
				mesh.verts[edge.verts[1]].occurs = 0;
			}

			int start, final;
			if (getOpenPolyline(mesh, face, start, final)) {
				// Polyline is open. Close it.
				int eidx = mesh.edges.size();
				CEdge closeEdge = CEdge(start, final, i, findex);
				mesh.edges.push_back(closeEdge);
				face.edges.push_back(eidx);
				closeFace.edges.push_back(eidx);
			}
		}
	}

	mesh.faces.push_back(closeFace);
}

bool Clipper::getOpenPolyline(CMesh& mesh, CFace& face, int& start, int& final) {
	// count the number of occurrences of each vertex in the polyline
	for (int i = 0; i < face.edges.size(); i++) {
		CEdge& edge = mesh.edges[face.edges[i]];
		mesh.verts[edge.verts[0]].occurs++;
		mesh.verts[edge.verts[1]].occurs++;
	}

	// determine if the polyline is open
	start = -1;
	final = -1;

	for (int i = 0; i < face.edges.size(); i++) {
		CEdge& edge = mesh.edges[face.edges[i]];
		int i0 = edge.verts[0];
		int i1 = edge.verts[1];

		if (mesh.verts[i0].occurs == 1) {
			if (start == -1) {
				start = i0;
			}
			else if (final == -1) {
				final = i0;
			}
		}
		
		if (mesh.verts[i1].occurs == 1) {
			if (start == -1) {
				start = i1;
			}
			else if (final == -1) {
				final = i1;
			}
		}
	}

	return start != -1 && final != -1;
}

CMesh Clipper::createMaxSizeVolume() {
	const vec3 min = vec3(-MAX_COORD, -MAX_COORD, -MAX_COORD);
	const vec3 max = vec3(MAX_COORD, MAX_COORD, MAX_COORD);

	CMesh mesh;

	{
		mesh.verts.push_back(CVertex(vec3(min.x, min.y, min.z))); // 0 front-left-bottom
		mesh.verts.push_back(CVertex(vec3(max.x, min.y, min.z))); // 1 front-right-bottom
		mesh.verts.push_back(CVertex(vec3(max.x, max.y, min.z))); // 2 back-right-bottom
		mesh.verts.push_back(CVertex(vec3(min.x, max.y, min.z))); // 3 back-left-bottom

		mesh.verts.push_back(CVertex(vec3(min.x, min.y, max.z))); // 4 front-left-top
		mesh.verts.push_back(CVertex(vec3(max.x, min.y, max.z))); // 5 front-right-top
		mesh.verts.push_back(CVertex(vec3(max.x, max.y, max.z))); // 6 back-right-top
		mesh.verts.push_back(CVertex(vec3(min.x, max.y, max.z))); // 7 back-left-top
	}

	{
		mesh.edges.push_back(CEdge(0, 1,  0, 5)); // 0 front bottom
		mesh.edges.push_back(CEdge(0, 4,  0, 2)); // 1 front left
		mesh.edges.push_back(CEdge(4, 5,  0, 4)); // 2 front top
		mesh.edges.push_back(CEdge(5, 1,  0, 3)); // 3 front right

		mesh.edges.push_back(CEdge(3, 2,  1, 5)); // 4 back bottom
		mesh.edges.push_back(CEdge(3, 7,  1, 2)); // 5 back left
		mesh.edges.push_back(CEdge(6, 7,  1, 4)); // 6 back top
		mesh.edges.push_back(CEdge(2, 6,  1, 3)); // 7 back right

		mesh.edges.push_back(CEdge(0, 3,  2, 5)); // 8 left bottom
		mesh.edges.push_back(CEdge(4, 7,  2, 4)); // 9 left top

		mesh.edges.push_back(CEdge(1, 2,  3, 5)); // 10 right bottom
		mesh.edges.push_back(CEdge(5, 6,  3, 4)); // 11 right top
	}

	{
		mesh.faces.push_back(CFace({ 0, 1, 2, 3 },   vec3( 0, -1,  0)));	// 0 front
		mesh.faces.push_back(CFace({ 4, 5, 6, 7 },   vec3( 0,  1,  0)));	// 1 back
		mesh.faces.push_back(CFace({ 1, 5, 8, 9 },   vec3(-1,  0,  0)));	// 2 left
		mesh.faces.push_back(CFace({ 3, 7, 10, 11 }, vec3( 1,  0,  0)));	// 3 right
		mesh.faces.push_back(CFace({ 2, 6, 9, 11 },  vec3( 0,  0,  1)));	// 4 top
		mesh.faces.push_back(CFace({ 0, 4, 8, 10 },  vec3( 0,  0, -1)));	// 5 bottom
	}

	return mesh;
}
