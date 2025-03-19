#include "Renderer.h"
#include "globals.h"
#include "LeafNavMeshGenerator.h"
#include "GLFW/glfw3.h"
#include "PolyOctree.h"
#include "Clipper.h"
#include "Bsp.h"
#include "LeafNavMesh.h"
#include <unordered_set>
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
	LeafNavMesh* mesh = new LeafNavMesh(leaves, octree);

	linkNavLeaves(map, mesh, 0);

	for (int i = 0; i < mesh->nodes.size(); i++) {
		setLeafOrigin(map, mesh, i);
	}

	for (int i = 0; i < mesh->nodes.size(); i++) {
		LeafNode& node = mesh->nodes[i];

		for (int k = 0; k < node.links.size(); k++) {
			LeafLink& link = node.links[k];
			calcPathCost(map, mesh, node, link);
		}
	}

	mesh->bspModelLeaves.clear();
	mesh->bspModelNodes.clear();
	mesh->bspModelLeaves.resize(map->modelCount);
	mesh->bspModelNodes.resize(map->modelCount);

	for (int i = 1; i < map->modelCount; i++) {
		LeafNode temp;
		getSolidEntityNode(map, mesh, i, vec3(), temp);
	}

	int totalSz = 0;
	for (int i = 0; i < mesh->nodes.size(); i++) {
		totalSz += sizeof(LeafNode) + (sizeof(LeafLink) * mesh->nodes[i].links.size());

		for (int k = 0; k < mesh->nodes[i].links.size(); k++) {
			totalSz += mesh->nodes[i].links[k].linkArea.sizeBytes() - sizeof(Polygon3D);
		}
		for (int k = 0; k < mesh->nodes[i].leafFaces.size(); k++) {
			totalSz += mesh->nodes[i].leafFaces[k].sizeBytes();
		}
	}

	logf("Generated %d node nav mesh in %.2fs (%d KB)\n", mesh->nodes.size(),
		glfwGetTime() - NavMeshGeneratorGenStart, totalSz / 1024);

	return mesh;
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
		LeafNode hull;
		getHullForClipperMesh(mesh, hull);

		if (hull.leafFaces.size()) {
			hull.id = leaves.size();
			leaves.push_back(hull);
		}
	}

	return leaves;
}

bool LeafNavMeshGenerator::getHullForClipperMesh(CMesh& mesh, LeafNode& leaf) {
	leaf = LeafNode();
	leaf.mins = vec3(FLT_MAX, FLT_MAX, FLT_MAX);
	leaf.maxs = vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);

	for (int f = 0; f < mesh.faces.size(); f++) {
		CFace& face = mesh.faces[f];
		if (!face.visible) {
			continue;
		}

		vector<vec3> faceVerts;

		for (int k = 0; k < face.edges.size(); k++) {
			CEdge& edge = mesh.edges[face.edges[k]];
			if (!edge.visible) {
				continue;
			}

			for (int v = 0; v < 2; v++) {
				int vertIdx = edge.verts[v];
				if (!mesh.verts[vertIdx].visible) {
					continue;
				}

				push_unique_vec3(faceVerts, mesh.verts[vertIdx].pos);
			}
		}

		sortPlanarVerts(faceVerts);

		vector<vec3> triangularVerts = getTriangularVerts(faceVerts);

		if (faceVerts.size() < 3 || triangularVerts.empty()) {
			leaf.leafFaces.clear();
			break;
		}

		Axes axes;
		vec3 e1 = (triangularVerts[1] - triangularVerts[0]).normalize();
		vec3 e2 = (triangularVerts[2] - triangularVerts[0]).normalize();
		axes.z = crossProduct(e1, e2).normalize();
		axes.x = e1;
		axes.y = crossProduct(axes.z, axes.x).normalize();

		if (dotProduct(face.normal, axes.z) < 0) {
			reverse(faceVerts.begin(), faceVerts.end());
			axes.z *= -1;
		}

		leaf.leafFaces.emplace_back(faceVerts, axes, true);
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
		return false;
	}

	return true;
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
	vector<vector<EntSplitter>> nodeSplits;
	nodeSplits.resize(mesh->nodes.size());

	for (int i = 0; i < map->ents.size(); i++) {
		Entity* ent = map->ents[i];
		int bspModelIdx = ent->getBspModelIdx();
		std::string cname = ent->getClassname();
		vec3 origin = ent->getOrigin();

		if (bspModelIdx == -1) {
			continue;
		}

		if (cname == "func_wall" || cname == "func_door" || cname == "func_breakable") {
		//if (cname == "func_wall" && ent->getBspModelIdx() == 129) {
		//if (cname == "func_wall") {
			LeafNode* entNode = &mesh->bspModelNodes[bspModelIdx];

			EntState state;
			state.origin = origin;
			state.angles = ent->getAngles();
			state.model = bspModelIdx;

			vec3 oldMins = entNode->mins;
			vec3 oldMaxs = entNode->maxs;
			entNode->mins += origin;
			entNode->maxs += origin;

			// WHERE I LEFT OFF: using cached nodes with an origin offset fucked up splitting somehow
			// reverting to using a new node and applying an offset to the node will fix but is much slower
			
			mesh->octree->getLeavesInRegion(entNode, regionLeaves);

			for (int k = 0; k < regionLeaves.size(); k++) {
				if (regionLeaves[k] && boxesIntersect(entNode->mins, entNode->maxs, mesh->nodes[k].mins, mesh->nodes[k].maxs)) {
					nodeSplits[k].push_back({ state, entNode });
				}
			}

			entNode->mins = oldMins;
			entNode->maxs = oldMaxs;
		}
	}

	int resplitCount = 0;
	int totalSplits = 0;

	for (int i = 0; i < nodeSplits.size(); i++) {	
		vector<EntSplitter>& splitters = nodeSplits[i];

		if (splitters.empty()) {
			continue;
		}

		totalSplits++;

		LeafNode& node = mesh->nodes[i];

		if (node.splittingEnts.size() == splitters.size()) {
			bool shouldResplit = false;

			for (int k = 0; k < splitters.size(); k++) {
				EntState& newState = splitters[k].entState;
				EntState& oldState = node.splittingEnts[k];

				if (newState.origin != oldState.origin || newState.angles != oldState.angles || newState.model != oldState.model) {
					shouldResplit = true;
					break;
				}
			}

			if (!shouldResplit) {
				continue;
			}
		}

		node.splittingEnts.clear();
		for (int k = 0; k < splitters.size(); k++) {
			vec3 origin = splitters[k].entState.origin;
			node.splittingEnts.push_back(splitters[k].entState);
		}

		resplitCount++;
		mesh->unsplitNode(i);

		splitLeafByEnts(map, mesh, i, splitters, false);
	}

	if (resplitCount)
		logf("Resplit %d / %d nodes\n", resplitCount, totalSplits);
}

void LeafNavMeshGenerator::splitLeafByEnts(Bsp* map, LeafNavMesh* mesh, int nodeIdx, vector<EntSplitter>& entNodes, bool includeSolidNode) {
	Clipper clipper = Clipper();

	LeafNode& node = mesh->nodes[nodeIdx];
	vector<LeafNode> splitNodes;
	splitNodes.push_back(node);

	for (int n = 0; n < entNodes.size(); n++) {
		LeafNode& entNode = *entNodes[n].node;
		vec3 nodeOffset = entNodes[n].entState.origin;

		for (int i = 0; i < entNode.leafFaces.size(); i++) {
			Polygon3D& face = entNode.leafFaces[i];

			BSPPLANE clip;
			clip.vNormal = face.plane_z;
			clip.fDist = face.fdist + dotProduct(clip.vNormal, nodeOffset);

			CMesh cmeshFront;
			CMesh cmeshBack;

			if (i > g_app->debugInt % entNode.leafFaces.size()) {
				//break;
			}
			//g_app->debugPoly = face;

			//logf("Split %d nodes\n", (int)splitNodes.size());
			for (int k = 0; k < splitNodes.size(); k++) {

				if (!splitNodes[k].intersects(face)) {
					// face is not touching the volume so shouldn't cut
					continue;
				}

				int ret;

				if (splitNodes[k].clipMesh.empty()) {
					ret = clipper.split(splitNodes[k].leafFaces, vec3(), clip, cmeshFront, cmeshBack);
				}
				else {
					ret = clipper.split(splitNodes[k].clipMesh, clip, cmeshFront, cmeshBack);
				}

				if (ret == 1) { // clipped?
					float sizeEpsilon = 100.0f;
					float originalSize = (splitNodes[k].maxs - splitNodes[k].mins).length() + sizeEpsilon;

					splitNodes.insert(splitNodes.begin() + k + 1, LeafNode());
					getHullForClipperMesh(cmeshFront, splitNodes[k]);
					getHullForClipperMesh(cmeshBack, splitNodes[k+1]);
					
					LeafNode& frontNode = splitNodes[k];
					LeafNode& backNode = splitNodes[k+1];
					frontNode.clipMesh = cmeshFront;
					backNode.clipMesh = cmeshBack;
					
					k++;

					if ((frontNode.maxs - frontNode.mins).length() > originalSize ||
						(backNode.maxs - backNode.mins).length() > originalSize ||
						frontNode.leafFaces.size() < 3 || backNode.leafFaces.size () < 3) {
						int entModel = map->ents[entNode.entidx]->getBspModelIdx();
						logf("Failed to clip with ent model %d! Degenerate hull.\n", entModel);
						splitNodes.erase(splitNodes.begin() + k);
						k--;
					}
				}
			}
		}
	}

	if (splitNodes.size() > 1) {
		node.childIdx = mesh->nodes.size();

		for (int i = 0; i < splitNodes.size(); i++) {
			if (!includeSolidNode) {
				bool isSolid = false;

				for (int k = 0; k < entNodes.size(); k++) {
					EntState& state = entNodes[k].entState;
					int modelIdx = state.model;
					int headnode = map->models[modelIdx].iHeadnodes[NAV_HULL];
					vec3 testPos = splitNodes[i].center - state.origin;

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
			splitNodes[i].parentIdx = nodeIdx;
			mesh->nodes.push_back(splitNodes[i]);
			setLeafOrigin(map, mesh, mesh->nodes.size() - 1);
		}

		//logf("Node %d split into %d leaves\n", (int)node.id, (int)splitNodes.size());

		linkNavChildLeaves(map, mesh, nodeIdx);
	}
}

void LeafNavMeshGenerator::setLeafOrigin(Bsp* map, LeafNavMesh* mesh, int nodeIdx) {
	float timeStart = glfwGetTime();

	LeafNode& node = mesh->nodes[nodeIdx];

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

void LeafNavMeshGenerator::linkNavChildLeaves(Bsp* map, LeafNavMesh* mesh, int nodeIdx) {
	int numLinks = 0;
	float linkStart = glfwGetTime();

	LeafNode& parent = mesh->nodes[nodeIdx];

	for (int i = parent.childIdx; i < mesh->nodes.size(); i++) {
		LeafNode& child = mesh->nodes[i];

		if (child.parentIdx != parent.id) {
			break;
		}

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

		// calc costs for the new child links
		for (int k = 0; k < child.links.size(); k++) {
			LeafLink& link = child.links[k];
			calcPathCost(map, mesh, child, link);
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
		int bspModelIdx = ent->getBspModelIdx();

		if (ent->getClassname() == "func_ladder") {
			LeafNode* entNode = mesh->findEntNode(i);
			if (!entNode) {
				LeafNode newNode;
				getSolidEntityNode(map, mesh, bspModelIdx, ent->getOrigin(), newNode);
				newNode.entidx = i;
				newNode.id = mesh->nodes.size();
				mesh->nodes.push_back(newNode);
				newNode.maxs.z += NAV_CROUCHJUMP_HEIGHT; // players can stand on top of the ladder for more height
				newNode.origin = (newNode.mins + newNode.maxs) * 0.5f;

				entNode = &mesh->nodes[mesh->nodes.size()-1];
			}

			linkEntityLeaves(map, mesh, *entNode, regionLeaves);
		}
		else if (ent->getClassname() == "trigger_teleport") {
			LeafNode* teleNode = mesh->findEntNode(i);
			if (!teleNode) {
				LeafNode newNode;
				getSolidEntityNode(map, mesh, bspModelIdx, ent->getOrigin(), newNode);
				newNode.entidx = i;
				newNode.id = mesh->nodes.size();
				mesh->nodes.push_back(newNode);
				
				teleNode = &mesh->nodes[mesh->nodes.size() - 1];
			}

			linkEntityLeaves(map, mesh, *teleNode, regionLeaves);
			

			// link teleport destination(s) to touched nodes
			int pentTarget = -1;
			vector<int> targets;

			const int SF_TELE_RANDOM_DESTINATION = 64;
			string target = ent->getKeyvalue("target");
			bool randomDestinations = atoi(ent->getKeyvalue("spawnflags").c_str()) & SF_TELE_RANDOM_DESTINATION;

			if (!target.length()) {
				continue;
			}

			for (int k = 0; k < map->ents.size(); k++) {
				Entity* tar = map->ents[k];
				if (tar->getTargetname() == target) {
					if (tar->getClassname() == "info_teleport_destination") {
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
					LeafNode* destNode = mesh->findEntNode(targets[k]);
					if (!destNode) {
						destNode = &addPointEntityNode(map, mesh, targets[k], pointMins, pointMaxs);
					}
					
					linkEntityLeaves(map, mesh, *destNode, regionLeaves);

					teleNode->addLink(destNode->id, teleNode->origin);
				}
			}
			else if (pentTarget != -1) {
				LeafNode* destNode = mesh->findEntNode(pentTarget);
				if (!destNode) {
					destNode = &addPointEntityNode(map, mesh, pentTarget, pointMins, pointMaxs);
				}

				linkEntityLeaves(map, mesh, *destNode, regionLeaves);

				teleNode->addLink(destNode->id, teleNode->origin);
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

void LeafNavMeshGenerator::getSolidEntityNode(Bsp* map, LeafNavMesh* mesh, int bspmodelIdx, vec3 origin, LeafNode& node) {
	if (mesh->bspModelLeaves[bspmodelIdx].empty()) {
		mesh->bspModelLeaves[bspmodelIdx] = getHullLeaves(map, bspmodelIdx, CONTENTS_SOLID);
	}

	vector<LeafNode>& leaves = mesh->bspModelLeaves[bspmodelIdx];

	// create a special ladder node which is a combination of all its leaves
	node = LeafNode();
	node.mins = vec3(FLT_MAX, FLT_MAX, FLT_MAX);
	node.maxs = vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);

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

	// cache the result
	if (mesh->bspModelNodes[bspmodelIdx].leafFaces.empty()) {
		mesh->bspModelNodes[bspmodelIdx] = node;
	}
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

	vec3 eps = vec3(EPSILON, EPSILON, EPSILON);
	if (!boxesIntersect(srcLeaf.mins - eps, srcLeaf.maxs + eps, dstLeaf.mins - eps, dstLeaf.maxs + eps)) {
		return 0;
	}

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