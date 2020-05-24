#include "util.h"
#include "Fgd.h"
#include "VertexBuffer.h"

struct EntCube {
	vec3 mins;
	vec3 maxs;
	COLOR3 color;

	VertexBuffer* buffer;
	VertexBuffer* selectBuffer; // red coloring for selected ents
	VertexBuffer* wireframeBuffer; // yellow outline for selected ents
};

class PointEntRenderer {
public:
	PointEntRenderer(Fgd* fgd, ShaderProgram* colorShader);

	EntCube* getEntCube(Entity* ent);

private:
	Fgd* fgd;
	ShaderProgram* colorShader;
	VertexBuffer* pointEntCubes;
	map<string, EntCube*> cubeMap;
	vector<EntCube*> entCubes;

	void genPointEntCubes();
	EntCube* getCubeMacthingProps(EntCube* cube);
	void genCubeBuffers(EntCube* cube);
};