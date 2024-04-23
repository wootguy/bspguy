#include "AppSettings.h"
#include "globals.h"
#include "BspRenderer.h"
#include <fstream>
#include "util.h"
#include "Renderer.h"

void AppSettings::loadDefault()
{
	windowWidth = 800;
	windowHeight = 600;
	windowX = 0;
#ifdef WIN32
	windowY = 30;
#else
	windowY = 0;
#endif
	maximized = 0;
	fontSize = 22;
	gamedir = std::string();
	workingdir = "/bspguy_work/";
	valid = false;
	undoLevels = 64;
	verboseLogs = false;

	debug_open = false;
	keyvalue_open = false;
	transform_open = false;
	log_open = false;
	settings_open = false;
	limits_open = false;
	entreport_open = false;
	show_transform_axes = false;
	settings_tab = 0;
	engine = ENGINE_HALF_LIFE;

	g_engine_limits[ENGINE_HALF_LIFE].max_surface_extents = 16;
	g_engine_limits[ENGINE_HALF_LIFE].max_models = 512;
	g_engine_limits[ENGINE_HALF_LIFE].max_planes = 32768;
	g_engine_limits[ENGINE_HALF_LIFE].max_vertexes = 65535;
	g_engine_limits[ENGINE_HALF_LIFE].max_nodes = 32767;
	g_engine_limits[ENGINE_HALF_LIFE].max_faces = 65535;
	g_engine_limits[ENGINE_HALF_LIFE].max_clipnodes = 32767;
	g_engine_limits[ENGINE_HALF_LIFE].max_leaves = 32760;
	g_engine_limits[ENGINE_HALF_LIFE].max_marksurfaces = 65535;
	g_engine_limits[ENGINE_HALF_LIFE].max_surfedges = 512000;
	g_engine_limits[ENGINE_HALF_LIFE].max_edges = 256000;
	g_engine_limits[ENGINE_HALF_LIFE].max_textures = 512;
	g_engine_limits[ENGINE_HALF_LIFE].max_lightdata = 48*1024*1024;
	g_engine_limits[ENGINE_HALF_LIFE].max_visdata = 8*1024*1024;
	g_engine_limits[ENGINE_HALF_LIFE].max_entdata = 2*1024*1024;
	g_engine_limits[ENGINE_HALF_LIFE].max_entities = 8192;
	g_engine_limits[ENGINE_HALF_LIFE].max_texinfos = 32767;
	g_engine_limits[ENGINE_HALF_LIFE].max_allocblocks = 64;
	g_engine_limits[ENGINE_HALF_LIFE].max_texturepixels = 262144;
	g_engine_limits[ENGINE_HALF_LIFE].max_mapboundary = 4096;

	g_engine_limits[ENGINE_SVEN_COOP].max_surface_extents = 64;
	g_engine_limits[ENGINE_SVEN_COOP].max_models = 4096;
	g_engine_limits[ENGINE_SVEN_COOP].max_planes = 65535;
	g_engine_limits[ENGINE_SVEN_COOP].max_vertexes = 65535;
	g_engine_limits[ENGINE_SVEN_COOP].max_nodes = 32768;
	g_engine_limits[ENGINE_SVEN_COOP].max_faces = 65535;
	g_engine_limits[ENGINE_SVEN_COOP].max_clipnodes = 32768;
	g_engine_limits[ENGINE_SVEN_COOP].max_leaves = 65536;
	g_engine_limits[ENGINE_SVEN_COOP].max_marksurfaces = 65535;
	g_engine_limits[ENGINE_SVEN_COOP].max_surfedges = 512000;
	g_engine_limits[ENGINE_SVEN_COOP].max_edges = 256000;
	g_engine_limits[ENGINE_SVEN_COOP].max_textures = 4096;
	g_engine_limits[ENGINE_SVEN_COOP].max_lightdata = 64 * 1024 * 1024;
	g_engine_limits[ENGINE_SVEN_COOP].max_visdata = 64 * 1024 * 1024;
	g_engine_limits[ENGINE_SVEN_COOP].max_entdata = 2 * 1024 * 1024;
	g_engine_limits[ENGINE_SVEN_COOP].max_entities = 8192;
	g_engine_limits[ENGINE_SVEN_COOP].max_texinfos = 32767;
	g_engine_limits[ENGINE_SVEN_COOP].max_allocblocks = 1024;
	g_engine_limits[ENGINE_SVEN_COOP].max_texturepixels = 1048576;
	g_engine_limits[ENGINE_SVEN_COOP].max_mapboundary = 32768;

	g_limits = g_engine_limits[ENGINE_HALF_LIFE];

	render_flags = g_render_flags = RENDER_TEXTURES | RENDER_LIGHTMAPS | RENDER_SPECIAL
		| RENDER_ENTS | RENDER_SPECIAL_ENTS | RENDER_POINT_ENTS | RENDER_WIREFRAME | RENDER_ENT_CONNECTIONS
		| RENDER_ENT_CLIPNODES;

	vsync = true;
	backUpMap = false;

	moveSpeed = 4.0f;
	fov = 75.0f;
	zfar = 262144.0f;
	rotSpeed = 5.0f;

	fgdPaths.clear();
	resPaths.clear();
}

void AppSettings::load() {
	ifstream file(g_settings_path);
	if (file.is_open()) {

		string line = "";
		while (getline(file, line)) {
			if (line.empty())
				continue;

			size_t eq = line.find("=");
			if (eq == string::npos) {
				continue;
			}

			string key = trimSpaces(line.substr(0, eq));
			string val = trimSpaces(line.substr(eq + 1));

			if (key == "window_width") { g_settings.windowWidth = atoi(val.c_str()); }
			else if (key == "window_height") { g_settings.windowHeight = atoi(val.c_str()); }
			else if (key == "window_x") { g_settings.windowX = atoi(val.c_str()); }
			else if (key == "window_y") { g_settings.windowY = atoi(val.c_str()); }
			else if (key == "window_maximized") { g_settings.maximized = atoi(val.c_str()); }
			else if (key == "debug_open") { g_settings.debug_open = atoi(val.c_str()) != 0; }
			else if (key == "keyvalue_open") { g_settings.keyvalue_open = atoi(val.c_str()) != 0; }
			else if (key == "transform_open") { g_settings.transform_open = atoi(val.c_str()) != 0; }
			else if (key == "log_open") { g_settings.log_open = atoi(val.c_str()) != 0; }
			else if (key == "settings_open") { g_settings.settings_open = atoi(val.c_str()) != 0; }
			else if (key == "limits_open") { g_settings.limits_open = atoi(val.c_str()) != 0; }
			else if (key == "entreport_open") { g_settings.entreport_open = atoi(val.c_str()) != 0; }
			else if (key == "settings_tab") { g_settings.settings_tab = atoi(val.c_str()); }
			else if (key == "vsync") { g_settings.vsync = atoi(val.c_str()) != 0; }
			else if (key == "show_transform_axes") { g_settings.show_transform_axes = atoi(val.c_str()) != 0; }
			else if (key == "verbose_logs") { g_settings.verboseLogs = atoi(val.c_str()) != 0; }
			else if (key == "fov") { g_settings.fov = atof(val.c_str()); }
			else if (key == "zfar") { g_settings.zfar = atof(val.c_str()); }
			else if (key == "move_speed") { g_settings.moveSpeed = atof(val.c_str()); }
			else if (key == "rot_speed") { g_settings.rotSpeed = atof(val.c_str()); }
			else if (key == "render_flags") { g_settings.render_flags = atoi(val.c_str()); }
			else if (key == "font_size") { g_settings.fontSize = atoi(val.c_str()); }
			else if (key == "undo_levels") { g_settings.undoLevels = atoi(val.c_str()); }
			else if (key == "gamedir") { g_settings.gamedir = val; }
			else if (key == "workingdir") { g_settings.workingdir = val; }
			else if (key == "fgd") { fgdPaths.push_back(val); }
			else if (key == "res") { resPaths.push_back(val); }
			else if (key == "savebackup") { g_settings.backUpMap = atoi(val.c_str()) != 0; }
			else if (key == "engine") { 
				g_settings.engine = clamp(atoi(val.c_str()), 0, 1);
				g_limits = g_engine_limits[g_settings.engine];
			}
		}

		g_settings.valid = true;

	}
	else {
		logf("Failed to open user config: %s\n", g_settings_path.c_str());
	}

	if (g_settings.windowY == -32000 &&
		g_settings.windowX == -32000)
	{
		g_settings.windowY = 0;
		g_settings.windowX = 0;
	}


#ifdef WIN32
	// Fix invisibled window header for primary screen.
	if (g_settings.windowY >= 0 && g_settings.windowY < 30)
	{
		g_settings.windowY = 30;
	}
#endif


	// Restore default window height if invalid.
	if (windowHeight <= 0 || windowWidth <= 0)
	{
		windowHeight = 600;
		windowWidth = 800;
	}

	if (fgdPaths.size() == 0) {
		fgdPaths.push_back("/svencoop/sven-coop.fgd");
	}

	if (resPaths.size() == 0) {
		resPaths.push_back("/svencoop/");
		resPaths.push_back("/svencoop_addon/");
		resPaths.push_back("/svencoop_downloads/");
		resPaths.push_back("/svencoop_hd/");
	}
}

void AppSettings::save() {
	if (!dirExists(g_config_dir)) {
		createDir(g_config_dir);
	}

	g_app->saveSettings();

	ofstream file(g_settings_path, ios::out | ios::trunc);
	file << "window_width=" << g_settings.windowWidth << endl;
	file << "window_height=" << g_settings.windowHeight << endl;
	file << "window_x=" << g_settings.windowX << endl;
	file << "window_y=" << g_settings.windowY << endl;
	file << "window_maximized=" << g_settings.maximized << endl;

	file << "debug_open=" << g_settings.debug_open << endl;
	file << "keyvalue_open=" << g_settings.keyvalue_open << endl;
	file << "transform_open=" << g_settings.transform_open << endl;
	file << "log_open=" << g_settings.log_open << endl;
	file << "settings_open=" << g_settings.settings_open << endl;
	file << "limits_open=" << g_settings.limits_open << endl;
	file << "entreport_open=" << g_settings.entreport_open << endl;

	file << "settings_tab=" << g_settings.settings_tab << endl;

	file << "gamedir=" << g_settings.gamedir << endl;
	file << "workingdir=" << g_settings.workingdir << endl;
	for (int i = 0; i < fgdPaths.size(); i++) {
		file << "fgd=" << g_settings.fgdPaths[i] << endl;
	}

	for (int i = 0; i < resPaths.size(); i++) {
		file << "res=" << g_settings.resPaths[i] << endl;
	}

	file << "vsync=" << g_settings.vsync << endl;
	file << "show_transform_axes=" << g_settings.show_transform_axes << endl;
	file << "verbose_logs=" << g_settings.verboseLogs << endl;
	file << "fov=" << g_settings.fov << endl;
	file << "zfar=" << g_settings.zfar << endl;
	file << "move_speed=" << g_settings.moveSpeed << endl;
	file << "rot_speed=" << g_settings.rotSpeed << endl;
	file << "render_flags=" << g_settings.render_flags << endl;
	file << "font_size=" << g_settings.fontSize << endl;
	file << "undo_levels=" << g_settings.undoLevels << endl;
	file << "savebackup=" << g_settings.backUpMap << endl;
	file << "engine=" << g_settings.engine << endl;
}