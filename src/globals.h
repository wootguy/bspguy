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
	int max_marksurfaces;
	int max_surfedges;
	int max_edges;
	int max_textures;
	int max_lightdata;
	int max_visdata;
	int max_entities;
	int max_entdata;
	int max_allocblocks;
	int max_texturepixels;
	int max_mapboundary; // how far from the map origin you can play the game without weird glitches
};

class Renderer;

extern bool g_verbose;
extern ProgressMeter g_progress;
extern std::vector<std::string> g_log_buffer;
extern const char* g_version_string;
extern std::mutex g_log_mutex;

extern AppSettings g_settings;
extern Renderer* g_app;
extern MapLimits g_limits;
extern MapLimits g_engine_limits[ENGINE_TYPES];

extern std::string g_config_dir;
extern std::string g_settings_path;

// prevents infinite include loops
extern std::set<std::string> g_parsed_fgds;

extern std::thread::id g_main_thread_id;

extern int g_render_flags;