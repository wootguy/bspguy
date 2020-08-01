#include "PointEntRenderer.h"
#include "primitives.h"
#include <string.h>

PointEntRenderer::PointEntRenderer(Fgd* fgd, ShaderProgram* colorShader) {
	this->fgd = fgd;
	this->colorShader = colorShader;

	genPointEntCubes();
}

PointEntRenderer::~PointEntRenderer() {
	for (int i = 0; i < entCubes.size(); i++) {
		delete entCubes[i]->buffer;
		delete entCubes[i]->selectBuffer;
		delete entCubes[i]->wireframeBuffer;
	}
}

EntCube* PointEntRenderer::getEntCube(Entity* ent) {
	string cname = ent->keyvalues["classname"];

	if (cubeMap.find(cname) != cubeMap.end()) {
		return cubeMap[cname];
	}
	return entCubes[0]; // default purple cube from hammer
}

void PointEntRenderer::genPointEntCubes() {
	// default purple cube
	EntCube* defaultCube = new EntCube();
	defaultCube->color = { 220, 0, 220, 255 };
	defaultCube->mins = { -8, -8, -8 };
	defaultCube->maxs = { 8, 8, 8 };
	genCubeBuffers(defaultCube);
	entCubes.push_back(defaultCube);

	if (fgd == NULL) {
		return;
	}

	for (int i = 0; i < fgd->classes.size(); i++) {
		FgdClass* fgdClass = fgd->classes[i];
		if (fgdClass->classType == FGD_CLASS_POINT) {
			EntCube* cube = new EntCube();
			cube->mins = fgdClass->mins;
			cube->maxs = fgdClass->maxs;
			cube->color = COLOR4(fgdClass->color, 255);

			EntCube* matchingCube = getCubeMatchingProps(cube);

			if (matchingCube == NULL) {
				genCubeBuffers(cube);
				entCubes.push_back(cube);
				cubeMap[fgdClass->name] = cube;
			}
			else {
				delete cube;
				cubeMap[fgdClass->name] = matchingCube;
			}
		}
	}

	logf("Generated %d entity cubes\n", entCubes.size());
}

EntCube* PointEntRenderer::getCubeMatchingProps(EntCube* cube) {
	for (int i = 0; i < entCubes.size(); i++) {
		if (memcmp(cube, entCubes[i], sizeof(EntCube) - sizeof(VertexBuffer*)*3) == 0) {
			return entCubes[i];
		}
	}
	return NULL;
}

void PointEntRenderer::genCubeBuffers(EntCube* entCube) {
	vec3 min = entCube->mins;
	vec3 max = entCube->maxs;

	// flip for HL coordinates
	min = vec3(min.x, min.z, -min.y);
	max = vec3(max.x, max.z, -max.y);

	cCube* cube = new cCube(min, max, entCube->color);

	// colors not where expected due to HL coordinate system
	cube->left.setColor(entCube->color * 0.66f);
	cube->right.setColor(entCube->color * 0.93f);
	cube->top.setColor(entCube->color * 0.40f);
	cube->back.setColor(entCube->color * 0.53f);

	COLOR4 selectColor = { 220, 0, 0, 255 };
	entCube->buffer = new VertexBuffer(colorShader, COLOR_4B | POS_3F, cube, 6 * 6);

	cCube* selectCube = new cCube(min, max, selectColor);

	// colors not where expected due to HL coordinate system
	selectCube->left.setColor(selectColor * 0.66f);
	selectCube->right.setColor(selectColor * 0.93f);
	selectCube->top.setColor(selectColor * 0.40f);
	selectCube->back.setColor(selectColor * 0.53f);

	entCube->selectBuffer = new VertexBuffer(colorShader, COLOR_4B | POS_3F, selectCube, 6 * 6);

	vec3 vcube[8] = {
		vec3(min.x, min.y, min.z), // front-left-bottom
		vec3(max.x, min.y, min.z), // front-right-bottom
		vec3(max.x, max.y, min.z), // back-right-bottom
		vec3(min.x, max.y, min.z), // back-left-bottom

		vec3(min.x, min.y, max.z), // front-left-top
		vec3(max.x, min.y, max.z), // front-right-top
		vec3(max.x, max.y, max.z), // back-right-top
		vec3(min.x, max.y, max.z), // back-left-top
	};

	COLOR4 yellow = { 255, 255, 0, 255 };

	// edges
	cVert selectWireframe[12 * 2] = {
		cVert(vcube[0], yellow), cVert(vcube[1], yellow), // front-bottom
		cVert(vcube[1], yellow), cVert(vcube[2], yellow), // right-bottom
		cVert(vcube[2], yellow), cVert(vcube[3], yellow), // back-bottom
		cVert(vcube[3], yellow), cVert(vcube[0], yellow), // left-bottom

		cVert(vcube[4], yellow), cVert(vcube[5], yellow), // front-top
		cVert(vcube[5], yellow), cVert(vcube[6], yellow), // right-top
		cVert(vcube[6], yellow), cVert(vcube[7], yellow), // back-top
		cVert(vcube[7], yellow), cVert(vcube[4], yellow), // left-top

		cVert(vcube[0], yellow), cVert(vcube[4], yellow), // front-left-pillar
		cVert(vcube[1], yellow), cVert(vcube[5], yellow), // front-right-pillar
		cVert(vcube[2], yellow), cVert(vcube[6], yellow), // back-right-pillar
		cVert(vcube[3], yellow), cVert(vcube[7], yellow) // back-left-pillar
	};

	cVert* selectWireframeBuf = new cVert[12 * 2];
	memcpy(selectWireframeBuf, selectWireframe, sizeof(cVert) * 12 * 2);

	entCube->wireframeBuffer = new VertexBuffer(colorShader, COLOR_4B | POS_3F, selectWireframeBuf, 2 * 12);

	entCube->buffer->ownData = true;
	entCube->selectBuffer->ownData = true;
	entCube->wireframeBuffer->ownData = true;
}
