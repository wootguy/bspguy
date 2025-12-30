#pragma once
#include <unordered_map>
#include <string>
#include "vectors.h"

class BaseRenderer;
class Entity;

class ModelRenderer {
public:
	ModelRenderer();
	float renderDist = 2048.0f; // models display as cubes past this distance

	bool drawModelsAndSprites(vec3 renderOffset, vec3 cameraOrigin, vec3 cameraAngles);
	void clearCache();

private:
	std::unordered_map<std::string, BaseRenderer*> studioModels; // maps a path to a model/sprite renderer
	std::unordered_map<std::string, std::string> studioModelPaths; // maps a entity path to an existing path, or blank if not found

	BaseRenderer* loadModel(Entity* ent);
};