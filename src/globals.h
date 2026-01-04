#pragma once
#include "ProgressMeter.h"
#include <vector>
#include <string>
#include <mutex>
#include <set>
#include <thread>
#include "AppSettings.h"
#include "ShaderProgram.h"

class Editor;

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
	RENDER_CLIPNODE_OPAQUE = (1 << 20),
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

// bits for enabling different shader program features
#define SH_BSP_WIREFRAME 1
#define SH_BSP_TEX_ATLAS 2	// vert UVs are 2D offsets into a texture atlas
#define SH_BSP_TEX_ARRAY 4	// vert UVs are 3D offsets into a texture array
#define SH_BSP_TEX_PAL 8	// use paletted textures

#define SH_MDL_BONE_TEXTURE 1 // use a texture to load model bone transforms

#define SH_VEC3_DEPTH_HACK 1 // draw at slightly less depth to prevent z fighting

struct GlobalShaders {
	ShaderProgram* bsp = new ShaderProgram("BSP");
	ShaderProgram* color = new ShaderProgram("Color");
	ShaderProgram* clipnode = new ShaderProgram("Clipnode");
	ShaderProgram* texture = new ShaderProgram("Texture");
	ShaderProgram* mdl = new ShaderProgram("MDL");
	ShaderProgram* spr = new ShaderProgram("SPR");
	ShaderProgram* vec3 = new ShaderProgram("vec3");
};

struct RenderStats {
	int64_t numVerts = 0;
	int64_t numObjects = 0;
	int64_t numShaderBinds = 0;
	int64_t numTextureBinds = 0;
	int64_t numMatrixUploads = 0;
	int64_t numUniformsUploaded = 0;

	int64_t vertMem = 0;
	int64_t texMem = 0;
	int64_t numShaders = 0;
	int64_t numShadersFailed = 0;

	void clear() {
		numVerts = 0;
		numObjects = 0;
		numShaderBinds = 0;
		numTextureBinds = 0;
		numMatrixUploads = 0;
		numUniformsUploaded = 0;
	}
};

enum LogLevel {
	LOG_LEVEL_DEBUG,
	LOG_LEVEL_INFO,
	LOG_LEVEL_WARN,
	LOG_LEVEL_ERROR,
};

struct LogEntry {
	int type;
	string msg;
};

extern bool g_verbose;
extern ProgressMeter g_progress;
extern std::vector<LogEntry> g_log_buffer;
extern const char* g_version_string;
extern std::mutex g_log_mutex;

extern AppSettings g_settings;
extern Editor* g_app;
extern MapLimits g_limits;
extern MapLimits g_engine_limits[ENGINE_TYPES];
extern GlobalShaders g_shaders;
extern RenderStats g_renderStats;

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

void glCheckError(const char* checkMessage);