#pragma once
#include "colors.h"
#include "vectors.h"
#include <map>
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
};

class PointEntRenderer {
public:
	Fgd* mergedFgd;
	vector<Fgd*> fgds;

	PointEntRenderer(Fgd* mergedFgd, const vector<Fgd*>& fgds, ShaderProgram* colorShader);
	~PointEntRenderer();

	EntCube* getEntCube(Entity* ent);
	EntCube* getEntCube(string cname);

private:
	ShaderProgram* colorShader;
	map<string, EntCube*> cubeMap;
	vector<EntCube*> entCubes;

	void genPointEntCubes();
	EntCube* getCubeMatchingProps(EntCube* cube);
	void genCubeBuffers(EntCube* cube);
};