#include "LeafNavMeshGenerator.h"
#include "GLFW/glfw3.h"
#include "PolyOctree.h"
#include "Clipper.h"
#include "Bsp.h"
#include "LeafNavMesh.h"
#include <set>
#include "util.h"
#include "PolyOctree.h"
#include <algorithm>

LeafNavMesh* LeafNavMeshGenerator::generate(Bsp* map, int hull) {
	float NavMeshGeneratorGenStart = glfwGetTime();
	BSPMODEL& model = map->models[0];

	vector<LeafMesh> emptyLeaves = getHullLeaves(map, hull);
	//mergeLeaves(map, emptyLeaves);
	//cullTinyLeaves(emptyLeaves);

	logf("Generated nav mesh in %.2fs\n", glfwGetTime() - NavMeshGeneratorGenStart);

	LeafNavMesh* navmesh = new LeafNavMesh(emptyLeaves);
	linkNavLeaves(map, navmesh);

	return navmesh;
}

vector<LeafMesh> LeafNavMeshGenerator::getHullLeaves(Bsp* map, int hull) {
	vector<LeafMesh> emptyLeaves;

	Clipper clipper;

	vector<NodeVolumeCuts> emptyNodes = map->get_model_leaf_volume_cuts(0, hull, CONTENTS_EMPTY);

	vector<CMesh> emptyMeshes;
	for (int k = 0; k < emptyNodes.size(); k++) {
		emptyMeshes.push_back(clipper.clip(emptyNodes[k].cuts));
	}

	// GET FACES FROM MESHES
	for (int m = 0; m < emptyMeshes.size(); m++) {
		CMesh& mesh = emptyMeshes[m];

		LeafMesh leaf = LeafMesh();

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

			Polygon3D poly = Polygon3D(faceVerts);
			poly.removeDuplicateVerts();

			leaf.leafFaces.push_back(poly);
		}

		if (leaf.leafFaces.size()) {
			leaf.center = vec3();
			for (int i = 0; i < leaf.leafFaces.size(); i++) {
				leaf.center += leaf.leafFaces[i].center;
			}
			leaf.center /= leaf.leafFaces.size();

			emptyLeaves.push_back(leaf);
		}
	}

	return emptyLeaves;
}

void LeafNavMeshGenerator::getOctreeBox(Bsp* map, vec3& min, vec3& max) {
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

PolygonOctree* LeafNavMeshGenerator::createPolyOctree(Bsp* map, const vector<LeafMesh*>& leaves, int treeDepth) {
	vec3 treeMin, treeMax;
	getOctreeBox(map, treeMin, treeMax);

	logf("Create octree depth %d, size %f -> %f\n", treeDepth, treeMax.x, treeMax.x / pow(2, treeDepth));
	PolygonOctree* octree = new PolygonOctree(treeMin, treeMax, treeDepth);

	for (int i = 0; i < leaves.size(); i++) {
		//octree->insertPolygon(leaves[i]);
	}

	return octree;
}

void LeafNavMeshGenerator::mergeLeaves(Bsp* map, vector<LeafMesh>& leaves) {
	
}

void LeafNavMeshGenerator::cullTinyLeaves(vector<LeafMesh>& leaves) {
	
}

void LeafNavMeshGenerator::linkNavLeaves(Bsp* map, LeafNavMesh* mesh) {
	int numLinks = 0;
	float linkStart = glfwGetTime();

	for (int i = 0; i < mesh->numLeaves; i++) {
		LeafMesh& leaf = mesh->leaves[i];
		int leafIdx = map->get_leaf(leaf.center, 3);

		if (leafIdx >= 0 && leafIdx < MAX_MAP_CLIPNODE_LEAVES) {
			mesh->leafMap[leafIdx] = i;
		}

		for (int k = i + 1; k < mesh->numLeaves; k++) {
			numLinks += tryFaceLinkLeaves(map, mesh, i, k);
		}
	}

	logf("Added %d nav leaf links in %.2fs\n", numLinks, (float)glfwGetTime() - linkStart);
}

int LeafNavMeshGenerator::tryFaceLinkLeaves(Bsp* map, LeafNavMesh* mesh, int srcLeafIdx, int dstLeafIdx) {
	LeafMesh& srcLeaf = mesh->leaves[srcLeafIdx];
	LeafMesh& dstLeaf = mesh->leaves[dstLeafIdx];

	for (int i = 0; i < srcLeaf.leafFaces.size(); i++) {
		Polygon3D& srcFace = srcLeaf.leafFaces[i];

		for (int k = 0; k < dstLeaf.leafFaces.size(); k++) {
			Polygon3D& dstFace = dstLeaf.leafFaces[k];

			Polygon3D intersectFace = srcFace.intersect(dstFace);

			if (srcLeafIdx == 83 && dstLeafIdx == 84) {
				if (fabs(-srcFace.fdist - dstFace.fdist) < 0.1f && dotProduct(srcFace.plane_z, dstFace.plane_z) < -0.99f) {
					logf("zomg\n");
					intersectFace = srcFace.intersect(dstFace);
				}
			}

			if (intersectFace.isValid) {
				mesh->addLink(srcLeafIdx, dstLeafIdx, intersectFace);
				mesh->addLink(dstLeafIdx, srcLeafIdx, intersectFace);
				return 2;
			}
		}
	}

	return 0;
}