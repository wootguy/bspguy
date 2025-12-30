#pragma once
#include "vectors.h"

class LeafNavMesh;
class Bsp;

class NavRenderer {
	friend class DebugWidget;
public:
	NavRenderer();
	~NavRenderer();

	void renderNavMesh(Bsp* map, vec3 cameraOrigin);
	void renderLeafGraph(LeafNavMesh* mesh, vec3 cameraOrigin, Bsp* map);
	void controls();

private:
	LeafNavMesh* debugLeafNavMesh = NULL;
};