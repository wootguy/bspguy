#include "NavMeshGenerator.h"
#include "GLFW/glfw3.h"
#include "PolyOctree.h"
#include "Clipper.h"
#include "Bsp.h"
#include "NavMesh.h"
#include <set>
#include "util.h"
#include "PolyOctree.h"
#include <algorithm>

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
	linkNavPolys(map, navmesh);

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

			sortPlanarVerts(faceVerts);

			if (faceVerts.size() < 3) {
				//logf("Degenerate clipnode face discarded %d\n", faceVerts.size());
				continue;
			}

			vec3 normal = getNormalFromVerts(faceVerts);

			if (dotProduct(face.normal, normal) < 0) {
				reverse(faceVerts.begin(), faceVerts.end());
				normal = normal.invert();
			}

			Polygon3D* poly = new Polygon3D(faceVerts, solidFaces.size(), false);
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

	min = vec3(-MAX_MAP_COORD, -MAX_MAP_COORD, -MAX_MAP_COORD);
	max = vec3(MAX_MAP_COORD, MAX_MAP_COORD, MAX_MAP_COORD);

	while (isBoxContained(mapMins, mapMaxs, min * 0.5f, max * 0.5f)) {
		max *= 0.5f;
		min *= 0.5f;
	}
}

PolygonOctree* NavMeshGenerator::createPolyOctree(Bsp* map, const vector<Polygon3D*>& faces, int treeDepth) {
	vec3 treeMin, treeMax;
	getOctreeBox(map, treeMin, treeMax);

	logf("Create octree depth %d, size %f -> %f\n", treeDepth, treeMax.x, treeMax.x / pow(2, treeDepth));
	PolygonOctree* octree = new PolygonOctree(treeMin, treeMax, treeDepth);

	for (int i = 0; i < faces.size(); i++) {
		octree->insertPolygon(faces[i]);
	}

	return octree;
}

vector<Polygon3D> NavMeshGenerator::getInteriorFaces(Bsp* map, int hull, vector<Polygon3D*>& faces) {
	PolygonOctree* octree = createPolyOctree(map, faces, octreeDepth);

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

		octree->getPolysInRegion(poly, regionPolys);
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
				Polygon3D* newpoly0 = new Polygon3D(splitPolys[0], faces.size(), false);
				Polygon3D* newpoly1 = new Polygon3D(splitPolys[1], faces.size(), false);

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

	delete octree;

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

void NavMeshGenerator::linkNavPolys(Bsp* map, NavMesh* mesh) {
	int numLinks = 0;

	float linkStart = glfwGetTime();

	for (int i = 0; i < mesh->numPolys; i++) {
		Polygon3D& poly = mesh->polys[i];

		for (int k = i+1; k < mesh->numPolys; k++) {
			if (i == k)
				continue;
			Polygon3D& otherPoly = mesh->polys[k];

			numLinks += tryEdgeLinkPolys(map, mesh, i, k);
		}
	}

	logf("Added %d nav poly links in %.2fs\n", numLinks, (float)glfwGetTime() - linkStart);
}

int NavMeshGenerator::tryEdgeLinkPolys(Bsp* map, NavMesh* mesh, int srcPolyIdx, int dstPolyIdx) {
	const Polygon3D& srcPoly = mesh->polys[srcPolyIdx];
	const Polygon3D& dstPoly = mesh->polys[dstPolyIdx];

	for (int i = 0; i < srcPoly.topdownVerts.size(); i++) {
		int inext = (i + 1) % srcPoly.topdownVerts.size();
		Line2D thisEdge(srcPoly.topdownVerts[i], srcPoly.topdownVerts[inext]);

		for (int k = 0; k < dstPoly.topdownVerts.size(); k++) {
			int knext = (k + 1) % dstPoly.topdownVerts.size();
			Line2D otherEdge(dstPoly.topdownVerts[k], dstPoly.topdownVerts[knext]);

			if (!thisEdge.isAlignedWith(otherEdge)) {
				continue;
			}

			float t0, t1, t2, t3;
			float overlapDist = thisEdge.getOverlapRanges(otherEdge, t0, t1, t2, t3);

			if (overlapDist < 1.0f) {
				continue; // shared region too short
			}

			vec3 delta1 = srcPoly.verts[inext] - srcPoly.verts[i];
			vec3 delta2 = srcPoly.verts[knext] - srcPoly.verts[k];
			vec3 e1 = srcPoly.verts[i] + delta1 * t0;
			vec3 e2 = srcPoly.verts[i] + delta1 * t1;
			vec3 e3 = dstPoly.verts[k] + delta2 * t2;
			vec3 e4 = dstPoly.verts[k] + delta2 * t3;

			float min1 = min(e1.z, e2.z);
			float max1 = max(e1.z, e2.z);
			float min2 = min(e3.z, e4.z);
			float max2 = max(e3.z, e4.z);

			int16_t zDist = 0; // 0 = edges are are the same height or cross at some point
			if (max1 < min2) { // dst is above src
				zDist = ceilf(min2 - max1);
			}
			else if (min1 > max2) { // dst is below src
				zDist = floorf(max2 - min1);
			}

			if (fabs(zDist) > NAV_STEP_HEIGHT) {
				// trace at every point along the edge to see if this connection is possible
				// starting at the mid point and working outwards
				bool isBelow = zDist > 0;
				delta1 = e2 - e1;
				delta2 = e4 - e3;
				vec3 mid1 = e1 + delta1 * 0.5f;
				vec3 mid2 = e3 + delta2 * 0.5f;
				vec3 inwardDir = crossProduct(srcPoly.plane_z, delta1.normalize());
				vec3 testOffset = (isBelow ? inwardDir : inwardDir * -1) + vec3(0, 0, 1.0f);

				float flatLen = (e2.xy() - e1.xy()).length();
				float stepUnits = 1.0f;
				float step = stepUnits / flatLen;
				TraceResult tr;
				bool isBlocked = true;
				for (float f = 0; f < 0.5f; f += step) {
					vec3 test1 = mid1 + (delta1 * f) + testOffset;
					vec3 test2 = mid2 + (delta2 * f) + testOffset;
					vec3 test3 = mid1 + (delta1 * -f) + testOffset;
					vec3 test4 = mid2 + (delta2 * -f) + testOffset;

					map->traceHull(test1, test2, 3, &tr);
					if (!tr.fAllSolid && !tr.fStartSolid && tr.flFraction > 0.9f) {
						isBlocked = false;
						break;
					}
					map->traceHull(test3, test4, 3, &tr);
					if (!tr.fAllSolid && !tr.fStartSolid && tr.flFraction > 0.9f) {
						isBlocked = false;
						break;
					}
				}

				if (isBlocked) {
					continue;
				}
			}

			if (dotProduct(thisEdge.dir, otherEdge.dir) > 0) {
				// Polygons overlap, but this is ok when dropping down.
				// Technically it's possible to go up too but that's
				// hard to pull off and no map requires that

				if (srcPoly.verts[i].z < dstPoly.verts[k].z) {
					mesh->addLink(dstPolyIdx, srcPolyIdx, k, i, -zDist, 0);
				}
				else {
					mesh->addLink(srcPolyIdx, dstPolyIdx, i, k, zDist, 0);
				}

				return 1;
			}

			mesh->addLink(srcPolyIdx, dstPolyIdx, i, k, zDist, 0);
			mesh->addLink(dstPolyIdx, srcPolyIdx, k, i, -zDist, 0);

			// TODO: multiple edge links are possible for overlapping polys
			return 2;
		}
	}

	return 0;
}
