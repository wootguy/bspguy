#include "ModelRenderer.h"
#include "StudioMdlRenderer.h"
#include "SprRenderer.h"
#include "PointEntRenderer.h"
#include "Editor.h"
#include "globals.h"
#include "Fgd.h"
#include "Entity.h"
#include "render_utils.h"
#include "BspRenderer.h"

ModelRenderer::ModelRenderer() {

}

void ModelRenderer::clearCache() {
	studioModelPaths.clear();
}

BaseRenderer* ModelRenderer::loadModel(Entity* ent) {
	if (ent->hasCachedMdl) {
		return ent->cachedMdl;
	}
	if (g_loading_models.getValue() > 0) {
		return NULL;
	}

	struct ModelKey {
		string name;
		bool isClassname;
	};

	static vector<ModelKey> tryModelKeys = {
		{"model", false},
		{"new_model", false},
		{"classname", true},
		{"monstertype", true},
	};

	string model;
	string lowerModel;
	bool foundModelKey = false;
	bool isMdlNotSpr = true;
	ent->isIconSprite = false;
	for (int i = 0; i < tryModelKeys.size(); i++) {
		ModelKey key = tryModelKeys[i];
		model = ent->getKeyvalue(key.name);

		if (tryModelKeys[i].isClassname) {
			if (g_app->mergedFgd) {
				FgdClass* fgd = g_app->mergedFgd->getFgdClass(ent->getKeyvalue(key.name));
				if (fgd) {
					if (fgd->model.length()) {
						model = fgd->model;
					}
					else if (fgd->sprite.length()) {
						model = fgd->sprite;
					}
					else if (fgd->iconSprite.length()) {
						model = fgd->iconSprite;
						ent->isIconSprite = true;
					}
					lowerModel = toLowerCase(model);
				}
			}
			else {
				continue;
			}
		}
		else {
			lowerModel = toLowerCase(model);
		}

		bool hasMdlExt = lowerModel.size() > 4 && lowerModel.find(".mdl") == lowerModel.size() - 4;
		bool hasSprExt = lowerModel.size() > 4 && lowerModel.find(".spr") == lowerModel.size() - 4;
		if (hasSprExt || hasMdlExt) {
			foundModelKey = true;
			ent->cachedMdlCname = key.isClassname ? ent->getKeyvalue(key.name) : ent->getClassname();
			isMdlNotSpr = hasMdlExt;
			break;
		}
	}

	if (!foundModelKey) {
		//logf("No model key found for '%s' (%s): %s\n", ent->getKeyvalue("targetname"].c_str(), ent->getKeyvalue("classname"].c_str(), model.c_str());
		ent->hasCachedMdl = true;
		return NULL; // no MDL found
	}

	auto cache = studioModelPaths.find(lowerModel);
	if (cache == studioModelPaths.end()) {
		string findPath = findAsset(model);
		studioModelPaths[lowerModel] = findPath;
		if (!findPath.size()) {
			logf("Failed to find model for entity '%s' (%s): %s\n",
				ent->getTargetname().c_str(), ent->getClassname().c_str(),
				model.c_str());
			ent->hasCachedMdl = true;
			return NULL;
		}
	}

	string modelPath = studioModelPaths[lowerModel];
	if (!modelPath.size()) {
		//logf("Empty string for model path in entity '%s' (%s): %s\n", ent->getKeyvalue("targetname"].c_str(), ent->getKeyvalue("classname"].c_str(), model.c_str());
		ent->hasCachedMdl = true;
		return NULL;
	}

	auto mdl = studioModels.find(modelPath);
	if (mdl == studioModels.end()) {
		BaseRenderer* newModel = NULL;
		if (isMdlNotSpr) {
			newModel = new StudioMdlRenderer(modelPath);
		}
		else {
			newModel = new SprRenderer(modelPath);
		}

		studioModels[modelPath] = newModel;
		ent->cachedMdl = newModel;
		ent->hasCachedMdl = true;
		//logf("Begin load model for entity '%s' (%s): %s\n", ent->getKeyvalue("targetname"].c_str(), ent->getKeyvalue("classname"].c_str(), model.c_str());
		return newModel;
	}

	ent->cachedMdl = mdl->second;
	ent->hasCachedMdl = true;
	return mdl->second;
}

bool ModelRenderer::drawModelsAndSprites(vec3 renderOffset, vec3 cameraOrigin, vec3 cameraAngles) {
	if (!(g_settings.render_flags & RENDER_POINT_ENTS))
		return false;

	vector<Entity*> ents = g_app->ents();
	if (ents.empty()) {
		return false;
	}

	vec3 worldOffset = ents[0]->getOrigin();

	g_shaders.color->bind();
	g_shaders.color->setUniform("colorMult", vec4(1, 1, 1, 1));

	if (!g_app->previewMode && !(g_settings.render_flags & (RENDER_STUDIO_MDL | RENDER_SPRITES)))
		return false;

	glEnable(GL_CULL_FACE);
	glCullFace(GL_FRONT);

	int drawCount = 0;

	unordered_set<int> selectedEnts;
	for (int idx : g_app->pickInfo.ents) {
		selectedEnts.insert(idx);
	}

	vec3 camForward, camRight, camUp;
	makeVectors(cameraAngles, camForward, camRight, camUp);

	struct DepthSortedEnt {
		Entity* ent;
		int idx;
		vec3 origin;
		vec3 angles;
		BaseRenderer* mdl;
		float dist; // distance from camera
	};

	Frustum frustum = g_app->getCameraFrustum();

	bool modelsLoading = false;

	vector<DepthSortedEnt> depthSortedMdlEnts;
	for (int i = 0; i < ents.size(); i++) {
		Entity* ent = ents[i];
		DepthSortedEnt sent;
		sent.ent = ent;
		sent.mdl = loadModel(sent.ent);
		sent.ent->didStudioDraw = false;

		if (ent->hidden)
			continue;

		if (sent.mdl && (sent.mdl->loadState != MODEL_LOAD_DONE)) {
			modelsLoading = true;
		}

		if (sent.mdl && sent.mdl->loadState != MODEL_LOAD_INITIAL) {
			if (sent.mdl->loadState == MODEL_LOAD_WAITING) {
				if (g_loading_models.getValue() == 0) {
					g_loading_models.inc();
					sent.mdl->loadState = MODEL_LOAD_INITIAL;
					std::thread(&BaseRenderer::loadData, sent.mdl).detach();
				}
			}
			else if (!sent.mdl->valid) {
				logf("Failed to load model: %s\n", sent.mdl->fpath.c_str());
				studioModels[ent->cachedMdl->fpath] = NULL;
				delete sent.mdl;
				ent->cachedMdl = sent.mdl = NULL;
			}
			else if (sent.mdl->loadState == MODEL_LOAD_UPLOAD) {
				sent.mdl->upload();
				const char* typ = sent.mdl->isSprite() ? "SPR" : "MDL";
				if (sent.mdl->loadState != MODEL_LOAD_UPLOAD)
					debugf("Loaded %s: %s\n", typ, sent.mdl->fpath.c_str());
			}
		}

		if (sent.mdl && sent.mdl->loadState == MODEL_LOAD_DONE && sent.mdl->valid) {
			if (!ent->drawCached) {
				ent->drawOrigin = ent->getOrigin();
				ent->drawAngles = ent->getVisualAngles();
				EntRenderOpts opts = ent->getRenderOpts();

				if (sent.mdl->isStudioModel()) {
					vec3 mins, maxs;
					((StudioMdlRenderer*)sent.mdl)->getModelBoundingBox(ent->drawAngles, opts.sequence, mins, maxs);
					ent->drawMin = mins + sent.origin;
					ent->drawMax = maxs + sent.origin;
				}
				else {
					vec3 mins, maxs;
					((SprRenderer*)sent.mdl)->getBoundingBox(mins, maxs, opts.scale);
					ent->drawMin = mins + sent.origin;
					ent->drawMax = maxs + sent.origin;
				}
			}

			sent.idx = i;
			sent.origin = ent->drawOrigin;
			sent.dist = dotProduct(sent.origin - cameraOrigin, camForward);

			if (sent.ent->lastDrawCall == 0) {
				// need to draw at least once to know mins/maxs
				depthSortedMdlEnts.push_back(sent);
				continue;
			}

			if (!sent.ent->drawCached) {
				EntRenderOpts opts = ent->getRenderOpts();

				if (sent.mdl->isStudioModel()) {
					vec3 mins, maxs;
					((StudioMdlRenderer*)sent.mdl)->getModelBoundingBox(ent->drawAngles, opts.sequence, mins, maxs);
					ent->drawMin = mins + sent.origin;
					ent->drawMax = maxs + sent.origin;
				}
				else {
					EntRenderOpts opts = ent->getRenderOpts();
					vec3 mins, maxs;
					((SprRenderer*)sent.mdl)->getBoundingBox(mins, maxs, opts.scale);
					ent->drawMin = mins + sent.origin;
					ent->drawMax = maxs + sent.origin;
				}
				ent->drawCached = true;
			}

			if (isBoxInView(ent->drawMin, ent->drawMax, frustum, renderDist))
				depthSortedMdlEnts.push_back(sent);
		}
	}
	sort(depthSortedMdlEnts.begin(), depthSortedMdlEnts.end(), [](const DepthSortedEnt& a, const DepthSortedEnt& b) {
		return a.dist > b.dist;
		});

	glCheckError("Model/sprite rendering setup");

	for (int i = 0; i < depthSortedMdlEnts.size(); i++) {
		Entity* ent = depthSortedMdlEnts[i].ent;
		BaseRenderer* mdl = depthSortedMdlEnts[i].mdl;
		int entidx = depthSortedMdlEnts[i].idx;

		bool isSelected = selectedEnts.count(entidx);

		bool skipRender = !g_app->previewMode && mdl->isStudioModel() && !(g_settings.render_flags & RENDER_STUDIO_MDL)
			|| mdl->isSprite() && !(g_settings.render_flags & RENDER_SPRITES);

		if (skipRender)
			continue;

		EntCube* entcube = g_app->mapRenderer->renderEnts[depthSortedMdlEnts[i].idx].pointEntCube;
		if (!entcube->buffer->isUploaded())
			continue;

		if (!g_app->previewMode) { // draw the colored transparent cube
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

			//EntCube* entcube = mapRenderer->pointEntRenderer->getEntCube(ent);
			g_shaders.color->bind();
			g_shaders.color->pushMatrix(MAT_MODEL);
			*g_shaders.color->modelMat = g_app->mapRenderer->renderEnts[entidx].modelMat;
			g_shaders.color->modelMat->translate(renderOffset.x, renderOffset.y, renderOffset.z);
			g_shaders.color->updateMatrixes();

			if (isSelected) {
				//glDepthFunc(GL_ALWAYS); // ignore depth testing for the world but not for the model
				g_shaders.color->setUniform("colorMult", vec4(1, 1, 1, 1));
				entcube->wireframeBuffer->draw(GL_LINES);
				//glDepthFunc(GL_LESS);

				glDepthMask(GL_FALSE); // let model draw over this
				g_shaders.color->setUniform("colorMult", vec4(1, 1, 1, 0.5f));
				entcube->selectBuffer->draw(GL_TRIANGLES);
				glDepthMask(GL_TRUE);
			}
			else {
				glDepthMask(GL_FALSE);
				g_shaders.color->setUniform("colorMult", vec4(1.0f, 1.0f, 1.0f, 0.5f));
				entcube->buffer->draw(GL_TRIANGLES);
				glDepthMask(GL_TRUE);

				g_shaders.color->setUniform("colorMult", vec4(0.0f, 0.0f, 0.0f, 1.0f));
				entcube->wireframeBuffer->draw(GL_LINES);
			}

			g_shaders.color->popMatrix(MAT_MODEL);

			glCheckError("Rendering model/sprite cube");
		}

		// draw the model
		ent->didStudioDraw = true;
		EntRenderOpts renderOpts = ent->getRenderOpts();
		vec3 drawOri = ent->drawOrigin + worldOffset;
		vec3 drawAngles = ent->drawAngles;

		if (mdl->isStudioModel()) {
			((StudioMdlRenderer*)mdl)->draw(drawOri, drawAngles, ent, cameraOrigin, camRight, isSelected);
		}
		else if (mdl->isSprite()) {
			COLOR3 color = COLOR3(255, 255, 255);
			COLOR3 outlineColor = COLOR3(0, 0, 0);
			bool useRenderModes = g_app->previewMode || (g_settings.render_flags & RENDER_RENDER_MODES);
			bool treatAsIcon = ent->isIconSprite || !useRenderModes;

			if (ent->isIconSprite && g_app->previewMode)
				continue;

			if (treatAsIcon) {
				vec3 sz = entcube->maxs - entcube->mins;
				float minDim = min(min(sz.x, sz.y), sz.z);
				renderOpts.scale = ((SprRenderer*)mdl)->getScaleToFitInsideCube(minDim);

				if (ent->isIconSprite)
					color = ent->getFgdTint();

				if (!ent->canRotate()) {
					drawAngles = vec3();
				}
				if (isSelected) {
					color = COLOR3(255, 0, 0);
				}
			}
			else if (isSelected) {
				color = COLOR3(255, 32, 32);
				outlineColor = COLOR3(255, 255, 0);
			}
			bool noOutline = treatAsIcon || g_app->previewMode;

			((SprRenderer*)mdl)->draw(drawOri, drawAngles, ent, renderOpts, color, outlineColor, noOutline);
			glCheckError("Rendering SPR");
		}

		drawCount++;

		// debug the model verts bounding box
		if (false && mdl->isStudioModel()) {
			vec3 mins, maxs;
			((StudioMdlRenderer*)mdl)->getModelBoundingBox(ent->drawAngles, renderOpts.sequence, mins, maxs);
			mins += ent->drawOrigin;
			maxs += ent->drawOrigin;

			g_shaders.color->bind();
			g_shaders.color->setUniform("colorMult", vec4(1.0f, 1.0f, 1.0f, 1.0f));
			drawBox(mins, maxs, COLOR4(255, 255, 0, 255));
			glCheckError("Rendering debug MDL");
		}
	}

	//logf("Draw %d models\n", drawCount);

	glCullFace(GL_BACK);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	g_shaders.color->bind();
	g_shaders.color->setUniform("colorMult", vec4(1.0f, 1.0f, 1.0f, 1.0f));

	glCheckError("Model/sprite rendering cleanup");

	return modelsLoading;
}
