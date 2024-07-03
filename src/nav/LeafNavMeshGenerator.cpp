#include "LeafNavMeshGenerator.h"
#include "GLFW/glfw3.h"
#include "PolyOctree.h"
#include "Clipper.h"
#include "Bsp.h"
#include "LeafNavMesh.h"
#include <set>
#include "util.h"
#include "LeafOctree.h"
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
	markWalkableLinks(map, navmesh);

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
		leaf.mins = vec3(FLT_MAX, FLT_MAX, FLT_MAX);
		leaf.maxs = vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);

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

		if (leaf.leafFaces.size() > 2) {
			leaf.center = vec3();
			for (int i = 0; i < leaf.leafFaces.size(); i++) {
				Polygon3D& face = leaf.leafFaces[i];
				leaf.center += face.center;

				for (int k = 0; k < face.verts.size(); k++) {
					expandBoundingBox(face.verts[k], leaf.mins, leaf.maxs);
				}
			}
			leaf.center /= leaf.leafFaces.size();
			leaf.idx = emptyLeaves.size();

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

LeafOctree* LeafNavMeshGenerator::createLeafOctree(Bsp* map, LeafNavMesh* mesh, int treeDepth) {
	float treeStart = glfwGetTime();

	vec3 treeMin, treeMax;
	getOctreeBox(map, treeMin, treeMax);

	LeafOctree* octree = new LeafOctree(treeMin, treeMax, treeDepth);

	for (int i = 0; i < mesh->numLeaves; i++) {
		octree->insertLeaf(&mesh->leaves[i]);
	}

	logf("Create octree depth %d, size %f -> %f in %.2fs\n", treeDepth,
		treeMax.x, treeMax.x / pow(2, treeDepth), (float)glfwGetTime() - treeStart);

	return octree;
}

void LeafNavMeshGenerator::mergeLeaves(Bsp* map, vector<LeafMesh>& leaves) {
	
}

void LeafNavMeshGenerator::cullTinyLeaves(vector<LeafMesh>& leaves) {
	
}

void LeafNavMeshGenerator::linkNavLeaves(Bsp* map, LeafNavMesh* mesh) {
	
	
	LeafOctree* octree = createLeafOctree(map, mesh, octreeDepth);

	int numLinks = 0;
	float linkStart = glfwGetTime();

	vector<bool> regionLeaves;
	regionLeaves.resize(mesh->numLeaves);

	for (int i = 0; i < mesh->numLeaves; i++) {
		LeafMesh& leaf = mesh->leaves[i];
		int leafIdx = map->get_leaf(leaf.center, 3);

		if (leafIdx >= 0 && leafIdx < MAX_MAP_CLIPNODE_LEAVES) {
			mesh->leafMap[leafIdx] = i;
		}

		octree->getLeavesInRegion(&leaf, regionLeaves);

		for (int k = i + 1; k < mesh->numLeaves; k++) {
			if (!regionLeaves[k]) {
				continue;
			}

			numLinks += tryFaceLinkLeaves(map, mesh, i, k);
		}
	}

	delete octree;

	logf("Added %d nav leaf links in %.2fs\n", numLinks, (float)glfwGetTime() - linkStart);
}

int LeafNavMeshGenerator::tryFaceLinkLeaves(Bsp* map, LeafNavMesh* mesh, int srcLeafIdx, int dstLeafIdx) {
	LeafMesh& srcLeaf = mesh->leaves[srcLeafIdx];
	LeafMesh& dstLeaf = mesh->leaves[dstLeafIdx];

	for (int i = 0; i < srcLeaf.leafFaces.size(); i++) {
		Polygon3D& srcFace = srcLeaf.leafFaces[i];

		for (int k = 0; k < dstLeaf.leafFaces.size(); k++) {
			Polygon3D& dstFace = dstLeaf.leafFaces[k];

			Polygon3D intersectFace = srcFace.coplanerIntersectArea(dstFace);

			if (intersectFace.isValid) {
				mesh->addLink(srcLeafIdx, dstLeafIdx, intersectFace);
				mesh->addLink(dstLeafIdx, srcLeafIdx, intersectFace);
				return 2;
			}
		}
	}

	return 0;
}

void LeafNavMeshGenerator::markWalkableLinks(Bsp* bsp, LeafNavMesh* mesh) {
	float markStart = glfwGetTime();

	for (int i = 0; i < mesh->numLeaves; i++) {
		LeafMesh& leaf = mesh->leaves[i];
		LeafNavNode& node = mesh->nodes[i];
		
		for (int k = 0; k < MAX_NAV_LEAF_LINKS; k++) {
			LeafNavLink& link = node.links[k];

			if (link.node == -1) {
				break;
			}

			LeafMesh& otherMesh = mesh->leaves[link.node];

			vec3 start = leaf.center;
			vec3 end = link.linkArea.center;


		}
	}

	logf("Marked link walkability in %.2fs\n", (float)glfwGetTime() - markStart);
}

bool LeafNavMeshGenerator::isWalkable(Bsp* bsp, vec3 start, vec3 end) {
	return true;
}