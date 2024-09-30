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

	//splitEntityLeaves(map, navmesh);

	linkNavLeaves(map, navmesh, 0);
	setLeafOrigins(map, navmesh, 0);
	linkEntityLeaves(map, navmesh, 0);
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
	vector<LeafNode> leaves;

	if (modelIdx < 0 || modelIdx >= map->modelCount) {
		return leaves;
	}

	Clipper clipper;

	vector<NodeVolumeCuts> nodes = map->get_model_leaf_volume_cuts(modelIdx, NAV_HULL, contents);

	for (int m = 0; m < nodes.size(); m++) {
		CMesh mesh = clipper.clip(nodes[m].cuts);
		LeafNode hull = getHullForClipperMesh(mesh);

		if (hull.leafFaces.size()) {
			hull.id = leaves.size();
			leaves.push_back(hull);
		}
	}

	return leaves;
}

LeafNode LeafNavMeshGenerator::getHullForClipperMesh(CMesh& mesh) {
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

		if (poly.verts.size() < 3) {
			//logf("Degenerate clipnode face discarded %d\n", faceVerts.size());
			continue;
		}

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
		leaf.origin = leaf.center;
	}
	else {
		leaf.leafFaces.clear();
	}

	return leaf;
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

void LeafNavMeshGenerator::splitEntityLeaves(Bsp* map, LeafNavMesh* mesh) {
	vector<bool> regionLeaves;
	regionLeaves.resize(mesh->nodes.size());

	int oldNodeCount = mesh->nodes.size();

	// maps a node to a list of entities that will split it
	vector<vector<LeafNode>> nodeSplits;
	nodeSplits.resize(mesh->nodes.size());

	for (int i = 0; i < map->ents.size(); i++) {
		Entity* ent = map->ents[i];
		std::string cname = ent->keyvalues["classname"];

		//if (cname == "func_wall" || cname == "func_door" || cname == "func_breakable") {
		//if (cname == "func_wall" && ent->getBspModelIdx() == 129) {
		if (cname == "func_wall") {
			LeafNode entNode = addSolidEntityNode(map, mesh, i);
			mesh->nodes.pop_back(); // solid node will be added during split

			mesh->octree->getLeavesInRegion(&entNode, regionLeaves);

			for (int k = 0; k < regionLeaves.size(); k++) {
				if (regionLeaves[k]) {
					nodeSplits[k].push_back(entNode);
				}
			}
		}
	}

	for (int i = 0; i < nodeSplits.size(); i++) {
		if (!nodeSplits[i].size()) {
			continue;
		}

		splitLeafByEnts(map, mesh, mesh->nodes[i], nodeSplits[i], false);
	}

	// add new nodes to the octree
	for (int i = oldNodeCount; i < mesh->nodes.size(); i++) {
		mesh->octree->insertLeaf(&mesh->nodes[i]);
	}
}

void LeafNavMeshGenerator::splitLeafByEnts(Bsp* map, LeafNavMesh* mesh, LeafNode& node, vector<LeafNode>& entNodes, bool includeSolidNode) {
	Clipper clipper = Clipper();
	
	vector<LeafNode> splitNodes;
	splitNodes.push_back(node);

	for (int n = 0; n < entNodes.size(); n++) {
		LeafNode& entNode = entNodes[n];

		for (int i = 0; i < entNode.leafFaces.size(); i++) {
			Polygon3D& face = entNode.leafFaces[i];

			BSPPLANE clipFront;
			clipFront.fDist = face.fdist;
			clipFront.vNormal = face.plane_z;

			BSPPLANE clipBack;
			clipBack.fDist = -face.fdist;
			clipBack.vNormal = face.plane_z * -1;

			CMesh cmesh;

			if (i > g_app->debugInt % entNode.leafFaces.size()) {
				//break;
			}
			//g_app->debugPoly = face;

			vector<LeafNode> newSplitNodes;

			//logf("Split %d nodes\n", (int)splitNodes.size());
			for (int k = 0; k < splitNodes.size(); k++) {

				if (!splitNodes[k].intersects(face)) {
					// face is not touching the volume so shouldn't cut
					newSplitNodes.push_back(splitNodes[k]);
					continue;
				}

				int ret = clipper.clip(splitNodes[k].leafFaces, clipFront, cmesh);

				if (ret == 1) {

					// clipped
					LeafNode frontNode = getHullForClipperMesh(cmesh);
					if (clipper.clip(splitNodes[k].leafFaces, clipBack, cmesh) == 1) {
						LeafNode backNode = getHullForClipperMesh(cmesh);

						float sizeEpsilon = 100.0f;
						float originalSize = (splitNodes[k].maxs - splitNodes[k].mins).length() + sizeEpsilon;

						if ((frontNode.maxs - frontNode.mins).length() > originalSize ||
							(backNode.maxs - backNode.mins).length() > originalSize ||
							frontNode.leafFaces.size() < 3 || backNode.leafFaces.size () < 3) {
							int entModel = map->ents[entNode.entidx]->getBspModelIdx();
							logf("Failed to clip with ent model %d! Degenerate hull.\n", entModel);
						}
						else {
							newSplitNodes.push_back(frontNode);
							newSplitNodes.push_back(backNode);
						}
					}
					else
						logf("Failed to clip with back plane!\n");
				}
				else if (ret == 0 || ret == -1) {
					// not clipped at all or fully clipped, no splitting done
					newSplitNodes.push_back(splitNodes[k]);
				}
			}

			splitNodes = newSplitNodes;
		}
	}

	int parentId = node.id;
	if (splitNodes.size() > 1) {
		node.childIdx = mesh->nodes.size();

		for (int i = 0; i < splitNodes.size(); i++) {
			if (!includeSolidNode) {
				bool isSolid = false;

				for (int k = 0; k < entNodes.size(); k++) {
					Entity* ent = map->ents[entNodes[k].entidx];
					int modelIdx = ent->getBspModelIdx();
					int headnode = map->models[modelIdx].iHeadnodes[NAV_HULL];
					vec3 testPos = splitNodes[i].center - ent->getOrigin();

					if (map->pointContents(headnode, testPos, NAV_HULL) == CONTENTS_SOLID) {
						isSolid = true;
						break;
					}
				}

				if (isSolid) {
					continue;
				}
			}

			splitNodes[i].id = mesh->nodes.size();
			splitNodes[i].parentIdx = parentId;
			mesh->nodes.push_back(splitNodes[i]);
		}

		//logf("Node %d split into %d leaves\n", (int)node.id, (int)splitNodes.size());
	}
}

void LeafNavMeshGenerator::setLeafOrigins(Bsp* map, LeafNavMesh* mesh, int offset) {
	float timeStart = glfwGetTime();

	for (int i = offset; i < mesh->nodes.size(); i++) {
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

	//logf("Set leaf origins in %.2fs\n", (float)glfwGetTime() - timeStart);
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

void LeafNavMeshGenerator::linkNavLeaves(Bsp* map, LeafNavMesh* mesh, int offset) {
	int numLinks = 0;
	float linkStart = glfwGetTime();

	vector<bool> regionLeaves;
	regionLeaves.resize(mesh->nodes.size());

	for (int i = offset; i < mesh->nodes.size(); i++) {
		LeafNode& leaf = mesh->nodes[i];

		if (leaf.parentIdx == NAV_INVALID_IDX) {
			// only link parent leaves to world leaves
			int leafIdx = map->get_leaf(leaf.center, NAV_HULL);

			if (leafIdx >= 0 && leafIdx < MAX_MAP_CLIPNODE_LEAVES) {
				mesh->leafMap[leafIdx] = i;
			}
		}

		mesh->octree->getLeavesInRegion(&leaf, regionLeaves);

		for (int k = i + 1; k < mesh->nodes.size(); k++) {
			if (!regionLeaves[k]) {
				continue;
			}

			if (mesh->nodes[k].childIdx != NAV_INVALID_IDX) {
				continue;
			}

			numLinks += tryFaceLinkLeaves(map, mesh, i, k);
		}
	}

	logf("Added %d nav leaf links in %.2fs\n", numLinks, (float)glfwGetTime() - linkStart);
}

void LeafNavMeshGenerator::linkNavChildLeaves(Bsp* map, LeafNavMesh* mesh, int offset) {
	int numLinks = 0;
	float linkStart = glfwGetTime();

	for (int i = offset; i < mesh->nodes.size(); i++) {
		LeafNode& leaf = mesh->nodes[i];

		if (leaf.parentIdx == NAV_INVALID_IDX) {
			continue; // parents should already be linked
		}

		if (leaf.parentIdx >= mesh->nodes.size()) {
			logf("Invalid parent idx in child leaf %d\n", i);
			continue;
		}

		LeafNode& parent = mesh->nodes[leaf.parentIdx];

		// link to other children in this same node
		for (int k = parent.childIdx; k < mesh->nodes.size(); k++) {
			if (k == i) {
				continue;
			}

			LeafNode& sibling = mesh->nodes[k];

			if (sibling.parentIdx != parent.id) {
				break;
			}

			numLinks += tryFaceLinkLeaves(map, mesh, i, k);
		}

		// link to nodes touching our parent
		for (int k = 0; k < parent.links.size(); k++) {
			LeafNode& parentSibling = mesh->nodes[parent.links[k].node];

			if (parentSibling.childIdx == NAV_INVALID_IDX) {
				// parent's neighbor wasn't split
				numLinks += tryFaceLinkLeaves(map, mesh, i, parentSibling.id);
			}
			else {
				// parent's neighbor was split into child leaves
				for (int j = parentSibling.childIdx; j < mesh->nodes.size(); j++) {
					LeafNode& cousin = mesh->nodes[j];

					if (cousin.parentIdx != parentSibling.id) {
						break;
					}

					numLinks += tryFaceLinkLeaves(map, mesh, i, j);
				}
			}
		}
	}

	for (int i = 0; i < mesh->nodes.size(); i++) {
		LeafNode& node = mesh->nodes[i];

		if (node.parentIdx == NAV_INVALID_IDX) {
			// update costs to new child links, if any
			for (int k = 0; k < node.links.size(); k++) {
				LeafLink& link = node.links[k];
				if (mesh->nodes[link.node].parentIdx != NAV_INVALID_IDX) {
					calcPathCost(map, mesh, node, link);
				}
			}
		}
		else {
			// child leaf with all new links
			for (int k = 0; k < node.links.size(); k++) {
				LeafLink& link = node.links[k];
				calcPathCost(map, mesh, node, link);
			}
		}		
	}

	//logf("Added %d nav child leaf links in %.2fs\n", numLinks, (float)glfwGetTime() - linkStart);
}

void LeafNavMeshGenerator::linkEntityLeaves(Bsp* map, LeafNavMesh* mesh, int offset) {
	vector<bool> regionLeaves;
	regionLeaves.resize(mesh->nodes.size());

	const vec3 pointMins = vec3(-16, -16, -36);
	const vec3 pointMaxs = vec3(16, 16, 36);
	
	for (int i = offset; i < map->ents.size(); i++) {
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
	for (int i = 0; i < regionLeaves.size(); i++) {
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
	LeafNode node = LeafNode();
	node.mins = vec3(FLT_MAX, FLT_MAX, FLT_MAX);
	node.maxs = vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);

	vec3 origin = ent->getOrigin();

	for (LeafNode& leaf : leaves) {
		expandBoundingBox(leaf.mins, node.mins, node.maxs);
		expandBoundingBox(leaf.maxs, node.mins, node.maxs);

		for (int i = 0; i < leaf.leafFaces.size(); i++) {
			vector<vec3> newVerts;
			for (int k = 0; k < leaf.leafFaces[i].verts.size(); k++) {
				newVerts.push_back(leaf.leafFaces[i].verts[k] + origin);
			}
			node.leafFaces.push_back(newVerts);
		}
	}

	node.mins += origin;
	node.maxs += origin;

	node.origin = (node.mins + node.maxs) * 0.5f;
	node.id = mesh->nodes.size();
	node.entidx = entidx;

	mesh->nodes.push_back(node);
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
			calcPathCost(bsp, mesh, node, link);
		}
	}

	logf("Calculated path costs in %.2fs\n", (float)glfwGetTime() - markStart);
}

void LeafNavMeshGenerator::calcPathCost(Bsp* bsp, LeafNavMesh* mesh, LeafNode& node, LeafLink& link) {
	LeafNode& otherNode = mesh->nodes[link.node];

	link.baseCost = 0;
	link.costMultiplier = 1.0f;

	if (node.entidx != 0 || otherNode.entidx != 0) {
		// entity links are things like ladders and elevators and cost nothing to use
		// so that the path finder prefers them to flying or jumping off ledges
		return;
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