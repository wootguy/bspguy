#pragma once
#include "ProgressMeter.h"
#include <vector>
#include <string>
#include <mutex>
#include <set>
#include <thread>
#include "AppSettings.h"

enum engine_types {
	ENGINE_HALF_LIFE,
	ENGINE_SVEN_COOP,
	ENGINE_TYPES
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

class Editor;

extern bool g_verbose;
extern ProgressMeter g_progress;
extern std::vector<std::string> g_log_buffer;
extern const char* g_version_string;
extern std::mutex g_log_mutex;

extern AppSettings g_settings;
extern Editor* g_app;
extern MapLimits g_limits;
extern MapLimits g_engine_limits[ENGINE_TYPES];

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