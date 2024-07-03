#include "Renderer.h"
#include "LeafNavMesh.h"
#include "GLFW/glfw3.h"
#include "PolyOctree.h"
#include "Clipper.h"
#include "util.h"
#include <string.h>
#include "Bsp.h"
#include <unordered_map>
#include "Entity.h"
#include "Fgd.h"
#include "globals.h"

bool LeafMesh::isInside(vec3 p) {
	for (int i = 0; i < leafFaces.size(); i++) {
		if (leafFaces[i].distance(p) > 0) {
			return false;
		}
	}

	return true;
}

bool LeafNavNode::addLink(int node, Polygon3D linkArea) {	
	for (int i = 0; i < MAX_NAV_LEAF_LINKS; i++) {
		if (links[i].node == node || links[i].node == -1) {
			links[i].linkArea = linkArea;
			links[i].node = node;
			return true;
		}
	}

	logf("Error: Max links reached on node %d\n", id);
	return false;
}

int LeafNavNode::numLinks() {
	int numLinks = 0;

	for (int i = 0; i < MAX_NAV_LEAF_LINKS; i++) {
		if (links[i].node == -1) {
			break;
		}
		numLinks++;
	}

	return numLinks;
}

LeafNavMesh::LeafNavMesh() {
	clear();
}

void LeafNavMesh::clear() {
	memset(nodes, 0, sizeof(LeafNavNode) * MAX_NAV_LEAVES);
	memset(leafMap, 65535, sizeof(uint16_t) * MAX_MAP_CLIPNODE_LEAVES);

	for (int i = 0; i < MAX_NAV_LEAVES; i++) {
		leaves[i] = LeafMesh();
		nodes[i].id = i;

		for (int k = 0; k < MAX_NAV_LEAF_LINKS; k++) {
			nodes[i].links[k].linkArea = Polygon3D();
			nodes[i].links[k].node = -1;
		}
	}
}

LeafNavMesh::LeafNavMesh(vector<LeafMesh> inleaves) {
	clear();

	for (int i = 0; i < inleaves.size(); i++) {
		leaves[i] = inleaves[i];
	}
	numLeaves = inleaves.size();

	logf("Created leaf nav mesh with %d leaves (x%d = %d KB)\n", 
		numLeaves, sizeof(LeafNavNode), (sizeof(LeafNavNode)* numLeaves) / 1024);

	logf("LeafNavNode = %d bytes, LeafNavLink = %d bytes\n",
		sizeof(LeafNavNode), sizeof(LeafNavLink));
}

bool LeafNavMesh::addLink(int from, int to, Polygon3D linkArea) {
	if (from < 0 || to < 0 || from >= MAX_NAV_LEAVES || to >= MAX_NAV_LEAVES) {
		logf("Error: add link from/to invalid node %d %d\n", from, to);
		return false;
	}

	if (!nodes[from].addLink(to, linkArea)) {
		vec3& pos = leaves[from].center;
		logf("Failed to add link at %d %d %d\n", (int)pos.x, (int)pos.y, (int)pos.z);
		return false;
	}

	return true;
}

int LeafNavMesh::getNodeIdx(Bsp* map, Entity* ent) {
	vec3 ori = ent->getOrigin();
	vec3 mins, maxs;
	int modelIdx = ent->getBspModelIdx();

	if (modelIdx != -1) {
		BSPMODEL& model = map->models[modelIdx];

		map->get_model_vertex_bounds(modelIdx, mins, maxs);
		ori += (maxs + mins) * 0.5f;
	}
	else {
		FgdClass* fclass = g_app->fgd->getFgdClass(ent->keyvalues["classname"]);
		if (fclass->sizeSet) {
			mins = fclass->mins;
			maxs = fclass->maxs;
		}
	}

	// first try testing a few points on the entity box for an early exit

	mins += ori;
	maxs += ori;

	vec3 testPoints[10] = {
		ori,
		(mins + maxs) * 0.5f,
		vec3(mins.x, mins.y, mins.z),
		vec3(mins.x, mins.y, maxs.z),
		vec3(mins.x, maxs.y, mins.z),
		vec3(mins.x, maxs.y, maxs.z),
		vec3(maxs.x, mins.y, mins.z),
		vec3(maxs.x, mins.y, maxs.z),
		vec3(maxs.x, maxs.y, mins.z),
		vec3(maxs.x, maxs.y, maxs.z),
	};

	for (int i = 0; i < 10; i++) {
		int targetLeaf = map->get_leaf(testPoints[i], 3);
		int targetLeafNavIdx = MAX_NAV_LEAVES;

		if (targetLeaf >= 0 && targetLeaf < MAX_MAP_CLIPNODE_LEAVES) {
			int navIdx = leafMap[targetLeaf];
			
			if (navIdx < 65535) {
				return navIdx;
			}
		}
	}

	if ((maxs - mins).length() < 1) {
		return -1; // point sized, so can't intersect any leaf
	}

	// no points are inside, so test for plane intersections

	cCube entCube(mins, maxs, COLOR4(0, 0, 0, 0));
	cQuad* faces[6] = {
		&entCube.top,
		&entCube.bottom,
		&entCube.left,
		&entCube.right,
		&entCube.front,
		&entCube.back,
	};

	Polygon3D boxPolys[6];
	for (int i = 0; i < 6; i++) {
		cQuad& face = *faces[i];
		boxPolys[i] = vector<vec3>{ face.v1.pos(), face.v2.pos(), face.v3.pos(), face.v6.pos() };
	}

	for (int i = 0; i < numLeaves; i++) {
		LeafMesh& mesh = leaves[i];
		
		for (int k = 0; k < mesh.leafFaces.size(); k++) {
			Polygon3D& leafFace = mesh.leafFaces[k];

			for (int k = 0; k < 6; k++) {
				if (leafFace.intersects(boxPolys[k])) {
					return i;
				}
			}
		}
	}

	return -1;
}

float LeafNavMesh::path_cost(int a, int b) {

	LeafNavNode& nodea = nodes[a];
	LeafNavNode& nodeb = nodes[b];
	LeafMesh& mesha = leaves[a];
	LeafMesh& meshb = leaves[b];
	vec3 delta = mesha.center - meshb.center;

	for (int i = 0; i < MAX_NAV_LEAF_LINKS; i++) {
		LeafNavLink& link = nodea.links[i];
		if (link.node == -1) {
			break;
		}
	}

	return delta.length();
}

vector<int> LeafNavMesh::AStarRoute(Bsp* map, int startNodeIdx, int endNodeIdx)
{
	set<int> closedSet;
	set<int> openSet;

	unordered_map<int, float> gScore;
	unordered_map<int, float> fScore;
	unordered_map<int, int> cameFrom;
	
	vector<int> emptyRoute;

	if (startNodeIdx < 0 || endNodeIdx < 0 || startNodeIdx > MAX_NAV_LEAVES || endNodeIdx > MAX_NAV_LEAVES) {
		logf("AStarRoute: invalid start/end nodes\n");
		return emptyRoute;
	}

	if (startNodeIdx == endNodeIdx) {
		emptyRoute.push_back(startNodeIdx);
		return emptyRoute;
	}

	LeafNavNode& start = nodes[startNodeIdx];
	LeafNavNode& goal = nodes[endNodeIdx];

	openSet.insert(startNodeIdx);
	gScore[startNodeIdx] = 0;
	fScore[startNodeIdx] = path_cost(start.id, goal.id);

	const int maxIter = 8192;
	int curIter = 0;
	while (!openSet.empty()) {
		if (++curIter > maxIter) {
			logf("AStarRoute exceeded max iterations searching path (%d)", maxIter);
			break;
		}

		// get node in openset with lowest cost
		int current = -1;
		float bestScore = 9e99;
		for (int nodeId : openSet)
		{
			float score = fScore[nodeId];
			if (score < bestScore) {
				bestScore = score;
				current = nodeId;
			}
		}

		//println("Current is " + current);

		if (current == goal.id) {
			//println("MAde it to the goal");
			// goal reached, build the route
			vector<int> path;
			path.push_back(current);

			int maxPathLen = 1000;
			int i = 0;
			while (cameFrom.count(current)) {
				current = cameFrom[current];
				path.push_back(current);
				if (++i > maxPathLen) {
					logf("AStarRoute exceeded max path length (%d)", maxPathLen);
					break;
				}
			}

			return path;
		}

		openSet.erase(current);
		closedSet.insert(current);

		LeafNavNode& currentNode = nodes[current];

		for (int i = 0; i < MAX_NAV_LEAF_LINKS; i++) {
			LeafNavLink& link = currentNode.links[i];
			if (link.node == -1) {
				break;
			}

			int neighbor = link.node;
			if (neighbor < 0 || neighbor >= MAX_NAV_LEAVES) {
				continue;
			}
			if (closedSet.count(neighbor))
				continue;
			//if (currentNode.blockers.size() > i and currentNode.blockers[i] & blockers != 0)
			//	continue; // blocked by something (monsterclip, normal clip, etc.). Don't route through this path.

			// discover a new node
			openSet.insert(neighbor);

			// The distance from start to a neighbor
			LeafNavNode& neighborNode = nodes[neighbor];

			float tentative_gScore = gScore[current];
			tentative_gScore += path_cost(currentNode.id, neighborNode.id);

			float neighbor_gScore = 9e99;
			if (gScore.count(neighbor))
				neighbor_gScore = gScore[neighbor];

			if (tentative_gScore >= neighbor_gScore)
				continue; // not a better path

			// This path is the best until now. Record it!
			cameFrom[neighbor] = current;
			gScore[neighbor] = tentative_gScore;
			fScore[neighbor] = tentative_gScore + path_cost(neighborNode.id, goal.id);
		}
	}
	
	return emptyRoute;
}