#include "Renderer.h"
#include "globals.h"
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
#include <float.h>
#include "Entity.h"


LeafNavMesh* LeafNavMeshGenerator::generate(Bsp* map) {
	float NavMeshGeneratorGenStart = glfwGetTime();
	BSPMODEL& model = map->models[0];

	float createLeavesStart = glfwGetTime();
	vector<LeafNode> leaves = getHullLeaves(map, 0, CONTENTS_EMPTY);
	logf("Created %d leaf nodes in %.2fs\n", leaves.size(), glfwGetTime() - createLeavesStart);
	
	LeafOctree* octree = createLeafOctree(map, leaves, octreeDepth);
	LeafNavMesh* navmesh = new LeafNavMesh(leaves, octree);

	linkNavLeaves(map, navmesh);
	setLeafOrigins(map, navmesh);
	linkEntityLeaves(map, navmesh);
	calcPathCosts(map, navmesh);

	int totalSz = 0;
	for (int i = 0; i < navmesh->nodes.size(); i++) {
		totalSz += sizeof(LeafNode) + (sizeof(LeafLink) * navmesh->nodes[i].links.size());

		for (int k = 0; k < navmesh->nodes[i].links.size(); k++) {
			totalSz += navmesh->nodes[i].links[k].linkArea.sizeBytes() - sizeof(Polygon3D);
		}
		for (int k = 0; k < navmesh->nodes[i].leafFaces.size(); k++) {
			totalSz += navmesh->nodes[i].leafFaces[k].sizeBytes();
		}
	}

	logf("Generated %d node nav mesh in %.2fs (%d KB)\n", navmesh->nodes.size(),
		glfwGetTime() - NavMeshGeneratorGenStart, totalSz / 1024);

	return navmesh;
}

vector<LeafNode> LeafNavMeshGenerator::getHullLeaves(Bsp* map, int modelIdx, int contents) {
	vector<LeafNode> emptyLeaves;

	if (modelIdx < 0 || modelIdx >= map->modelCount) {
		return emptyLeaves;
	}

	Clipper clipper;

	vector<NodeVolumeCuts> emptyNodes = map->get_model_leaf_volume_cuts(modelIdx, NAV_HULL, contents);

	vector<CMesh> emptyMeshes;
	for (int k = 0; k < emptyNodes.size(); k++) {
		emptyMeshes.push_back(clipper.clip(emptyNodes[k].cuts));
	}

	// GET FACES FROM MESHES
	for (int m = 0; m < emptyMeshes.size(); m++) {
		CMesh& mesh = emptyMeshes[m];

		LeafNode leaf = LeafNode();
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
			leaf.id = emptyLeaves.size();
			leaf.origin = leaf.center;

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

LeafOctree* LeafNavMeshGenerator::createLeafOctree(Bsp* map, vector<LeafNode>& nodes, int treeDepth) {
	float treeStart = glfwGetTime();

	vec3 treeMin, treeMax;
	getOctreeBox(map, treeMin, treeMax);

	LeafOctree* octree = new LeafOctree(treeMin, treeMax, treeDepth);

	for (int i = 0; i < nodes.size(); i++) {
		octree->insertLeaf(&nodes[i]);
	}

	logf("Create octree depth %d, size %f -> %f in %.2fs\n", treeDepth,
		treeMax.x, treeMax.x / pow(2, treeDepth), (float)glfwGetTime() - treeStart);

	return octree;
}

void LeafNavMeshGenerator::setLeafOrigins(Bsp* map, LeafNavMesh* mesh) {
	float timeStart = glfwGetTime();

	for (int i = 0; i < mesh->nodes.size(); i++) {
		LeafNode& node = mesh->nodes[i];

		vec3 testBottom = node.center - vec3(0, 0, 4096);
		node.origin = node.center;
		int bottomFaceIdx = -1;
		for (int i = 0; i < node.leafFaces.size(); i++) {
			Polygon3D& face = node.leafFaces[i];
			if (face.intersect(node.center, testBottom, node.origin)) {
				bottomFaceIdx = i;
				break;
			}
		}
		node.origin.z += NAV_BOTTOM_EPSILON;

		if (bottomFaceIdx != -1) {
			node.origin = getBestPolyOrigin(map, node.leafFaces[bottomFaceIdx], node.origin);
		}

		for (int k = 0; k < node.links.size(); k++) {
			LeafLink& link = node.links[k];

			link.pos = getBestPolyOrigin(map, link.linkArea, link.pos);
		}
	}

	logf("Set leaf origins in %.2fs\n", (float)glfwGetTime() - timeStart);
}

vec3 LeafNavMeshGenerator::getBestPolyOrigin(Bsp* map, Polygon3D& poly, vec3 bias) {
	TraceResult tr;
	map->traceHull(bias, bias + vec3(0, 0, -4096), NAV_HULL, &tr);
	float height = bias.z - tr.vecEndPos.z;

	if (height < NAV_STEP_HEIGHT) {
		return bias;
	}

	float step = 8.0f;

	float bestHeight = FLT_MAX;
	float bestCenterDist = FLT_MAX;
	vec3 bestPos = bias;
	float pad = 1.0f + EPSILON; // don't choose a point right against a face of the volume

	for (int y = poly.localMins.y + pad; y < poly.localMaxs.y - pad; y += step) {
		for (int x = poly.localMins.x + pad; x < poly.localMaxs.x - pad; x += step) {
			vec3 testPos = poly.unproject(vec2(x, y));
			testPos.z += NAV_BOTTOM_EPSILON;

			map->traceHull(testPos, testPos + vec3(0, 0, -4096), NAV_HULL, &tr);
			float height = testPos.z - tr.vecEndPos.z;
			float heightDelta = height - bestHeight;
			float centerDist = (testPos - bias).lengthSquared();

			if (bestHeight <= NAV_STEP_HEIGHT) {
				if (height <= NAV_STEP_HEIGHT && centerDist < bestCenterDist) {
					bestHeight = height;
					bestCenterDist = centerDist;
					bestPos = testPos;
				}
			}
			else if (heightDelta < -EPSILON) {
				bestHeight = height;
				bestCenterDist = centerDist;
				bestPos = testPos;
			}
			else if (fabs(heightDelta) < EPSILON && centerDist < bestCenterDist) {
				bestHeight = height;
				bestCenterDist = centerDist;
				bestPos = testPos;
			}
		}
	}

	return bestPos;
}

void LeafNavMeshGenerator::linkNavLeaves(Bsp* map, LeafNavMesh* mesh) {
	int numLinks = 0;
	float linkStart = glfwGetTime();

	vector<bool> regionLeaves;
	regionLeaves.resize(mesh->nodes.size());

	for (int i = 0; i < mesh->nodes.size(); i++) {
		LeafNode& leaf = mesh->nodes[i];
		int leafIdx = map->get_leaf(leaf.center, 3);

		if (leafIdx >= 0 && leafIdx < MAX_MAP_CLIPNODE_LEAVES) {
			mesh->leafMap[leafIdx] = i;
		}

		mesh->octree->getLeavesInRegion(&leaf, regionLeaves);

		for (int k = i + 1; k < mesh->nodes.size(); k++) {
			if (!regionLeaves[k]) {
				continue;
			}

			numLinks += tryFaceLinkLeaves(map, mesh, i, k);
		}
	}

	logf("Added %d nav leaf links in %.2fs\n", numLinks, (float)glfwGetTime() - linkStart);
}

void LeafNavMeshGenerator::linkEntityLeaves(Bsp* map, LeafNavMesh* mesh) {
	vector<bool> regionLeaves;
	regionLeaves.resize(mesh->nodes.size());

	const vec3 pointMins = vec3(-16, -16, -36);
	const vec3 pointMaxs = vec3(16, 16, 36);
	
	for (int i = 0; i < map->ents.size(); i++) {
		Entity* ent = map->ents[i];

		if (ent->keyvalues["classname"] == "func_ladder") {
			LeafNode& entNode = addSolidEntityNode(map, mesh, i);
			entNode.maxs.z += NAV_CROUCHJUMP_HEIGHT; // players can stand on top of the ladder for more height
			entNode.origin = (entNode.mins + entNode.maxs) * 0.5f;

			linkEntityLeaves(map, mesh, entNode, regionLeaves);
		}
		else if (ent->keyvalues["classname"] == "trigger_teleport") {
			LeafNode& teleNode = addSolidEntityNode(map, mesh, i);
			linkEntityLeaves(map, mesh, teleNode, regionLeaves);

			// link teleport destination(s) to touched nodes
			int pentTarget = -1;
			vector<int> targets;

			const int SF_TELE_RANDOM_DESTINATION = 64;
			string target = ent->keyvalues["target"];
			bool randomDestinations = atoi(ent->keyvalues["spawnflags"].c_str()) & SF_TELE_RANDOM_DESTINATION;

			if (!target.length()) {
				continue;
			}

			for (int k = 0; k < map->ents.size(); k++) {
				Entity* tar = map->ents[k];
				if (tar->keyvalues["targetname"] == target) {
					if (tar->keyvalues["classname"] == "info_teleport_destination") {
						targets.push_back(k);
					}
					else if (pentTarget == -1) {
						pentTarget = k;
					}
				}
			}

			if (!randomDestinations && targets.size()) {
				pentTarget = targets[0]; // prefer teleport destinations
			}

			if (randomDestinations && !targets.empty()) {
				// link all possible targets
				for (int k = 0; k < targets.size(); k++) {
					LeafNode& entNode = addPointEntityNode(map, mesh, targets[k], pointMins, pointMaxs);
					linkEntityLeaves(map, mesh, entNode, regionLeaves);

					teleNode.addLink(entNode.id, teleNode.origin);
				}
			}
			else if (pentTarget != -1) {
				LeafNode& entNode = addPointEntityNode(map, mesh, pentTarget, pointMins, pointMaxs);
				linkEntityLeaves(map, mesh, entNode, regionLeaves);

				teleNode.addLink(entNode.id, teleNode.origin);
			}			
		}
	}
}

void LeafNavMeshGenerator::linkEntityLeaves(Bsp* map, LeafNavMesh* mesh, LeafNode& entNode, vector<bool>& regionLeaves) {
	mesh->octree->getLeavesInRegion(&entNode, regionLeaves);

	// link teleport destinations to touched nodes
	for (int i = 0; i < mesh->nodes.size(); i++) {
		if (!regionLeaves[i]) {
			continue;
		}

		LeafNode& node = mesh->nodes[i];
		if (boxesIntersect(node.mins, node.maxs, entNode.mins, entNode.maxs)) {
			vec3 linkPos = entNode.origin;
			linkPos.z = node.origin.z;

			entNode.addLink(i, linkPos);
			node.addLink(entNode.id, linkPos);
		}
	}
}

LeafNode& LeafNavMeshGenerator::addSolidEntityNode(Bsp* map, LeafNavMesh* mesh, int entidx) {
	Entity* ent = map->ents[entidx];
	vector<LeafNode> leaves = getHullLeaves(map, ent->getBspModelIdx(), CONTENTS_SOLID);

	// create a special ladder node which is a combination of all its leaves
	LeafNode ladderNode = LeafNode();
	ladderNode.mins = vec3(FLT_MAX, FLT_MAX, FLT_MAX);
	ladderNode.maxs = vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);

	for (LeafNode& node : leaves) {
		expandBoundingBox(node.mins, ladderNode.mins, ladderNode.maxs);
		expandBoundingBox(node.maxs, ladderNode.mins, ladderNode.maxs);

		for (int i = 0; i < node.leafFaces.size(); i++) {
			ladderNode.leafFaces.push_back(node.leafFaces[i]);
		}
	}
	ladderNode.origin = (ladderNode.mins + ladderNode.maxs) * 0.5f;
	ladderNode.id = mesh->nodes.size();
	ladderNode.entidx = entidx;

	mesh->nodes.push_back(ladderNode);
	return mesh->nodes[mesh->nodes.size() - 1];
}

LeafNode& LeafNavMeshGenerator::addPointEntityNode(Bsp* map, LeafNavMesh* mesh, int entidx, vec3 mins, vec3 maxs) {
	Entity* ent = map->ents[entidx];

	LeafNode node = LeafNode();
	node.origin = node.center = ent->getOrigin();
	node.mins = node.origin + mins;
	node.maxs = node.origin + maxs;
	node.id = mesh->nodes.size();
	node.entidx = entidx;

	mesh->nodes.push_back(node);
	return mesh->nodes[mesh->nodes.size() - 1];
}

int LeafNavMeshGenerator::tryFaceLinkLeaves(Bsp* map, LeafNavMesh* mesh, int srcLeafIdx, int dstLeafIdx) {
	LeafNode& srcLeaf = mesh->nodes[srcLeafIdx];
	LeafNode& dstLeaf = mesh->nodes[dstLeafIdx];

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

void LeafNavMeshGenerator::calcPathCosts(Bsp* bsp, LeafNavMesh* mesh) {
	float markStart = glfwGetTime();

	for (int i = 0; i < mesh->nodes.size(); i++) {
		LeafNode& node = mesh->nodes[i];
		
		for (int k = 0; k < node.links.size(); k++) {
			LeafLink& link = node.links[k];
			LeafNode& otherNode = mesh->nodes[link.node];

			link.baseCost = 0;
			link.costMultiplier = 1.0f;

			if (node.entidx != 0 || otherNode.entidx != 0) {
				// entity links are things like ladders and elevators and cost nothing to use
				// so that the path finder prefers them to flying or jumping off ledges
				continue;
			}

			vec3 start = node.origin;
			vec3 mid = link.pos;
			vec3 end = otherNode.origin;
			bool isDrop = end.z + EPSILON < start.z;

			TraceResult tr;
			bsp->traceHull(node.origin, link.pos, NAV_HULL, &tr);

			addPathCost(link, bsp, start, mid, isDrop);
			addPathCost(link, bsp, mid, end, isDrop);
		}
	}

	logf("Calculated path costs in %.2fs\n", (float)glfwGetTime() - markStart);
}

void LeafNavMeshGenerator::addPathCost(LeafLink& link, Bsp* bsp, vec3 start, vec3 end, bool isDrop) {
	TraceResult tr;

	int steps = (end - start).length() / 8.0f;
	vec3 delta = end - start;
	vec3 dir = delta.normalize();

	bool flyingNeeded = false;
	bool stackingNeeded = false;
	bool isSteepSlope = false;
	float maxHeight = 0;

	for (int i = 0; i < steps; i++) {
		float t = i * (1.0f / (float)steps);

		vec3 top = start + delta * t;
		vec3 bottom = top + vec3(0, 0, -4096);

		bsp->traceHull(top, bottom, NAV_HULL, &tr);
		float height = (tr.vecEndPos - top).length();

		if (tr.vecPlaneNormal.z < 0.7f) {
			isSteepSlope = true;
		}

		if (height > maxHeight) {
			maxHeight = height;
		}

		if (height > NAV_CROUCHJUMP_STACK_HEIGHT) {
			flyingNeeded = true;
		}
		else if (height > NAV_CROUCHJUMP_HEIGHT) {
			stackingNeeded = true;
		}
	}

	if (isDrop && (flyingNeeded || stackingNeeded)) {
		// probably falling. not much cost but prefer hitting the ground
		// TODO: deadly fall distances should be avoided
		link.costMultiplier = max(link.costMultiplier, 10.0f);
	}
	else if (flyingNeeded) {
		// players can't fly normally so any valid ground path will be better, no matter how long it is.
		// As a last resort, "flying" is possible by getting a bunch of players to stack or by using the 
		// gauss gun.
		link.baseCost = max(link.baseCost, 64000.0f);
		link.costMultiplier = max(link.costMultiplier, 100.0f);
	}
	else if (stackingNeeded) {
		// a player can't reach this high on their own, stacking is needed.
		// prefer walking an additional X units instead of waiting for a player or box to stack on
		link.baseCost = max(link.baseCost, 8000.0f);
		link.costMultiplier = max(link.costMultiplier, 100.0f);
	}
	else if (isSteepSlope) {
		// players can slide up slopes but its excruciatingly slow. Try to find stairs or something.
		link.costMultiplier = max(link.costMultiplier, 10.0f);
	}
	else if (maxHeight > NAV_STEP_HEIGHT) {
		// prefer paths which don't require jumping
		link.costMultiplier = max(link.costMultiplier, 2.0f);
	}
}