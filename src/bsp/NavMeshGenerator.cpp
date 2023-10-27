#include "NavMeshGenerator.h"
#include "GLFW/glfw3.h"
#include "PolyOctree.h"
#include "Clipper.h"

NavMesh* NavMeshGenerator::generate(Bsp* map, int hull) {
	float NavMeshGeneratorGenStart = glfwGetTime();
	BSPMODEL& model = map->models[0];

	vector<Polygon3D*> solidFaces = getHullFaces(map, hull);
	vector<Polygon3D> faces = getInteriorFaces(map, hull, solidFaces);
	mergeFaces(map, faces);
	cullTinyFaces(faces);

	for (int i = 0; i < solidFaces.size(); i++) {
		if (solidFaces[i])
			delete solidFaces[i];
	}

	logf("Generated nav mesh in %.2fs\n", faces.size(), glfwGetTime() - NavMeshGeneratorGenStart);

	NavMesh* navmesh = new NavMesh(faces);
	linkNavPolys(navmesh);

	return navmesh;
}

vector<Polygon3D*> NavMeshGenerator::getHullFaces(Bsp* map, int hull) {
	float hullShrink = 0;
	vector<Polygon3D*> solidFaces;

	Clipper clipper;

	vector<NodeVolumeCuts> solidNodes = map->get_model_leaf_volume_cuts(0, hull, CONTENTS_SOLID);

	vector<CMesh> solidMeshes;
	for (int k = 0; k < solidNodes.size(); k++) {
		solidMeshes.push_back(clipper.clip(solidNodes[k].cuts));
	}

	// GET FACES FROM MESHES
	for (int m = 0; m < solidMeshes.size(); m++) {
		CMesh& mesh = solidMeshes[m];

		for (int f = 0; f < mesh.faces.size(); f++) {
			CFace& face = mesh.faces[f];
			if (!face.visible) {
				continue;
			}

			set<int> uniqueFaceVerts;

			for (int k = 0; k < face.edges.size(); k++) {
				for (int v = 0; v < 2; v++) {
					int vertIdx = mesh.edges[face.edges[k]].verts[v];
					if (!mesh.verts[vertIdx].visible) {
						continue;
					}
					uniqueFaceVerts.insert(vertIdx);
				}
			}

			vector<vec3> faceVerts;
			for (auto vertIdx : uniqueFaceVerts) {
				faceVerts.push_back(mesh.verts[vertIdx].pos);
			}

			faceVerts = getSortedPlanarVerts(faceVerts);

			if (faceVerts.size() < 3) {
				//logf("Degenerate clipnode face discarded %d\n", faceVerts.size());
				continue;
			}

			vec3 normal = getNormalFromVerts(faceVerts);

			if (dotProduct(face.normal, normal) < 0) {
				reverse(faceVerts.begin(), faceVerts.end());
				normal = normal.invert();
			}

			Polygon3D* poly = new Polygon3D(faceVerts, solidFaces.size());
			poly->removeDuplicateVerts();
			if (hullShrink)
				poly->extendAlongAxis(hullShrink);

			solidFaces.push_back(poly);

		}
	}

	return solidFaces;
}

void NavMeshGenerator::getOctreeBox(Bsp* map, vec3& min, vec3& max) {
	vec3 mapMins;
	vec3 mapMaxs;
	map->get_bounding_box(mapMins, mapMaxs);

	min = vec3(-MAX_COORD, -MAX_COORD, -MAX_COORD);
	max = vec3(MAX_COORD, MAX_COORD, MAX_COORD);

	while (isBoxContained(mapMins, mapMaxs, min * 0.5f, max * 0.5f)) {
		max *= 0.5f;
		min *= 0.5f;
	}
}

PolygonOctree NavMeshGenerator::createPolyOctree(Bsp* map, const vector<Polygon3D*>& faces, int treeDepth) {
	vec3 treeMin, treeMax;
	getOctreeBox(map, treeMin, treeMax);

	logf("Create octree depth %d, size %f -> %f\n", treeDepth, treeMax.x, treeMax.x / pow(2, treeDepth));
	PolygonOctree octree(treeMin, treeMax, treeDepth);

	for (int i = 0; i < faces.size(); i++) {
		octree.insertPolygon(faces[i]);
	}

	return octree;
}

vector<Polygon3D> NavMeshGenerator::getInteriorFaces(Bsp* map, int hull, vector<Polygon3D*>& faces) {
	PolygonOctree octree = createPolyOctree(map, faces, octreeDepth);

	int debugPoly = 0;
	//debugPoly = 601;

	int avgInRegion = 0;
	int regionChecks = 0;

	vector<Polygon3D> interiorFaces;

	int cuttingPolyCount = faces.size();
	int presplit = faces.size();
	int numSplits = 0;
	float startTime = glfwGetTime();
	bool doSplit = true;
	bool doCull = true;
	bool doMerge = true;
	bool doTinyCull = true;
	bool walkableSurfacesOnly = true;

	vector<bool> regionPolys;
	regionPolys.resize(cuttingPolyCount);

	for (int i = 0; i < faces.size(); i++) {
		Polygon3D* poly = faces[i];
		//if (debugPoly && i != debugPoly && i < cuttingPolys.size()) {
		//	continue;
		//}
		if (!poly->isValid) {
			continue;
		}
		if (walkableSurfacesOnly && poly->plane_z.z < 0.7) {
			continue;
		}

		//logf("debug poly idx %d -> %d\n", didx, i);
		//didx++;

		//logf("Splitting %d\n", i);

		octree.getPolysInRegion(poly, regionPolys);
		if (poly->idx < cuttingPolyCount)
			regionPolys[poly->idx] = false;
		regionChecks++;

		bool anySplits = false;
		int sz = cuttingPolyCount;

		if (!doSplit || (debugPoly && i != debugPoly && i < cuttingPolyCount))
			sz = 0;

		for (int k = 0; k < sz; k++) {
			if (!regionPolys[k]) {
				continue;
			}
			Polygon3D* cutPoly = faces[k];
			avgInRegion++;
			//if (k != 1547) {
			//	continue;
			//}

			vector<vector<vec3>> splitPolys = poly->split(*cutPoly);

			if (splitPolys.size()) {
				Polygon3D* newpoly0 = new Polygon3D(splitPolys[0], faces.size());
				Polygon3D* newpoly1 = new Polygon3D(splitPolys[1], faces.size());

				if (newpoly0->area < EPSILON || newpoly1->area < EPSILON) {
					delete newpoly0;
					delete newpoly1;
					continue;
				}

				faces.push_back(newpoly0);
				faces.push_back(newpoly1);

				anySplits = true;
				numSplits++;

				float newArea = newpoly0->area + newpoly1->area;
				if (newArea < poly->area * 0.9f) {
					logf("Poly %d area shrunk by %.1f (%.1f -> %1.f)\n", i, (poly->area - newArea), poly->area, newArea);
				}

				//logf("Split poly %d by %d into areas %.1f %.1f\n", i, k, newpoly0->area, newpoly1->area);
				break;
			}
		}
		if (!doSplit) {
			if (i < cuttingPolyCount) {
				interiorFaces.push_back(*poly);
			}
		}
		else if (!anySplits && (map->isInteriorFace(*poly, hull) || !doCull)) {
			interiorFaces.push_back(*poly);
		}
	}
	logf("Finished cutting in %.2fs\n", (float)(glfwGetTime() - startTime));
	logf("Split %d faces into %d (%d splits)\n", presplit, faces.size(), numSplits);
	logf("Average of %d in poly regions\n", regionChecks ? (avgInRegion / regionChecks) : 0);
	logf("Got %d interior faces\n", interiorFaces.size());

	return interiorFaces;
}

void NavMeshGenerator::mergeFaces(Bsp* map, vector<Polygon3D>& faces) {
	float mergeStart = glfwGetTime();

	vec3 treeMin, treeMax;
	getOctreeBox(map, treeMin, treeMax);

	int preMergePolys = faces.size();
	vector<Polygon3D> mergedFaces = faces;
	int pass = 0;
	int maxPass = 10;
	for (pass = 0; pass <= maxPass; pass++) {

		PolygonOctree mergeOctree(treeMin, treeMax, octreeDepth);
		for (int i = 0; i < mergedFaces.size(); i++) {
			mergedFaces[i].idx = i;
			//interiorFaces[i].removeColinearVerts();
			mergeOctree.insertPolygon(&mergedFaces[i]);
		}

		vector<bool> regionPolys;
		regionPolys.resize(mergedFaces.size());

		vector<Polygon3D> newMergedFaces;

		for (int i = 0; i < mergedFaces.size(); i++) {
			Polygon3D& poly = mergedFaces[i];
			if (poly.idx == -1)
				continue;
			//if (pass == 4 && i != 149)
			//	continue;

			mergeOctree.getPolysInRegion(&poly, regionPolys);
			regionPolys[poly.idx] = false;

			int sz = regionPolys.size();
			bool anyMerges = false;

			for (int k = i + 1; k < sz; k++) {
				if (!regionPolys[k]) {
					continue;
				}
				Polygon3D& mergePoly = mergedFaces[k];
				/*
				if (pass == 4 && k != 242) {
					continue;
				}
				if (pass == 4) {
					logf("debug time\n");
				}
				*/

				Polygon3D mergedPoly = poly.merge(mergePoly);

				if (!mergedPoly.isValid || mergedPoly.verts.size() > MAX_NAV_POLY_VERTS) {
					continue;
				}

				anyMerges = true;

				// prevent any further merges on the original polys
				mergePoly.idx = -1;
				poly.idx = -1;

				newMergedFaces.push_back(mergedPoly);
				break;
			}

			if (!anyMerges)
				newMergedFaces.push_back(poly);
		}

		//logf("Removed %d polys in pass %d\n", mergedFaces.size() - newMergedFaces.size(), pass + 1);

		if (mergedFaces.size() == newMergedFaces.size() || pass == maxPass) {
			break;
		}
		else {
			mergedFaces = newMergedFaces;
		}
	}

	logf("Finished merging in %.2fs\n", (float)(glfwGetTime() - mergeStart));
	logf("Merged %d polys down to %d in %d passes\n", preMergePolys, mergedFaces.size(), pass);

	faces = mergedFaces;
}

void NavMeshGenerator::cullTinyFaces(vector<Polygon3D>& faces) {
	const int TINY_POLY = 64; // cull faces smaller than this

	vector<Polygon3D> finalPolys;
	for (int i = 0; i < faces.size(); i++) {
		if (faces[i].area < TINY_POLY) {
			// TODO: only remove if there is at least one unconnected edge,
			// otherwise there will be holes
			continue;
		}
		finalPolys.push_back(faces[i]);
	}

	logf("Removed %d tiny polys\n", faces.size() - finalPolys.size());
	faces = finalPolys;
}

void NavMeshGenerator::linkNavPolys(NavMesh* mesh) {
	for (int i = 0; i < mesh->numPolys; i++) {
		Polygon3D& poly = mesh->polys[i];
		vector<vec2>& verts = poly.topdownVerts;

		for (int k = 0; k < mesh->numPolys; k++) {
			if (i == k)
				continue;
			Polygon3D& otherPoly = mesh->polys[k];
			vector<vec2>& otherVerts = otherPoly.topdownVerts;

			for (int j = 0; j < verts.size(); j++) {
				vec2& e1 = verts[j];
			}
		}
	}
}