#pragma once
#include "colors.h"
#include "vectors.h"
#include <unordered_map>
#include <vector>

class Fgd;
class Entity;
class VertexBuffer;
class ShaderProgram;

struct EntCube {
	vec3 mins;
	vec3 maxs;
	COLOR4 color;

	VertexBuffer* buffer;
	VertexBuffer* selectBuffer; // red coloring for selected ents
	VertexBuffer* wireframeBuffer; // yellow outline for selected ents

	int calcMemoryUsage();
};

class PointEntRenderer {
public:
	Fgd* mergedFgd;
	vector<Fgd*> fgds;

	PointEntRenderer(Fgd* mergedFgd, const vector<Fgd*>& fgds);
	~PointEntRenderer();

	EntCube* getEntCube(Entity* ent);
	EntCube* getEntCube(string cname);
	void uploadCubeBuffers();
	int calcMemoryUsage();

private:
	unordered_map<string, EntCube*> cubeMap;
	vector<EntCube*> entCubes;

	void genPointEntCubes();
	EntCube* getCubeMatchingProps(EntCube* cube);
	void genCubeBuffers(EntCube* cube);
};