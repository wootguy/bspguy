#pragma once
#include "ProgressMeter.h"
#include <vector>
#include <string>
#include <mutex>
#include <set>
#include <thread>
#include "AppSettings.h"

class Editor;
class ShaderProgram;

enum engine_types {
	ENGINE_HALF_LIFE,
	ENGINE_SVEN_COOP,
	ENGINE_TYPES
};

enum RenderFlags {
	RENDER_TEXTURES = (1 << 0),
	RENDER_LIGHTMAPS = (1 << 1),
	RENDER_WIREFRAME = (1 << 2),
	RENDER_ENTS = (1 << 3),
	RENDER_SPECIAL = (1 << 4),
	RENDER_SPECIAL_ENTS = (1 << 5),
	RENDER_POINT_ENTS = (1 << 6),
	RENDER_ORIGIN = (1 << 7),
	RENDER_WORLD_CLIPNODES = (1 << 8),
	RENDER_ENT_CLIPNODES = (1 << 9),
	RENDER_ENT_CONNECTIONS = (1 << 10),
	RENDER_MAP_BOUNDARY = (1 << 11),
	RENDER_STUDIO_MDL = (1 << 12),
	RENDER_SPRITES = (1 << 13),
	RENDER_ENT_DIRECTIONS = (1 << 14),
	RENDER_RENDER_MODES = (1 << 15),
	RENDER_LEAF_GRAPH = (1 << 16),
	RENDER_PVS = (1 << 17),
	RENDER_NAME_TAGS = (1 << 18),
	RENDER_SKYBOX = (1 << 19),
};

struct MapLimits {
	int max_surface_extents;

	int max_models;
	int max_planes;
	int max_vertexes;
	int max_nodes;
	int max_texinfos;
	int max_faces;
	int max_clipnodes;
	int max_leaves;
	int max_worldleaves; // a bug in the HL client lowers the leaf limit per-model
	int max_marksurfaces;
	int max_surfedges;
	int max_edges;
	int max_textures;
	int max_lightdata;
	int max_lightstyles;
	int max_visdata;
	int max_entities;
	int max_entdata;
	int max_allocblocks;
	int max_texturepixels;
};

struct GlobalShaders {
	ShaderProgram* bsp = NULL;
	ShaderProgram* color = NULL;
	ShaderProgram* texture = NULL;
	ShaderProgram* mdl = NULL;
	ShaderProgram* spr = NULL;
	ShaderProgram* vec3 = NULL;
	ShaderProgram* sprOutline = NULL;
};

extern bool g_verbose;
extern ProgressMeter g_progress;
extern std::vector<std::string> g_log_buffer;
extern const char* g_version_string;
extern std::mutex g_log_mutex;

extern AppSettings g_settings;
extern Editor* g_app;
extern MapLimits g_limits;
extern MapLimits g_engine_limits[ENGINE_TYPES];
extern GlobalShaders g_shaders;

extern std::string g_config_dir;
extern std::string g_settings_path;

// prevents infinite include loops
extern std::set<std::string> g_parsed_fgds;

extern std::thread::id g_main_thread_id;

// opengl compatibility
extern int g_max_texture_size;
extern int g_max_texture_array_layers;
extern int g_max_vtf_units;
extern bool g_opengl_texture_array_support;
extern bool g_opengl_3d_texture_support;

void glCheckError(const char* checkMessage);