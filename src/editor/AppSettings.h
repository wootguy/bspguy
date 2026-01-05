#pragma once
#include <string>
#include <vector>

enum AppRenderers {
	RENDERER_OPENGL_21, // 2.1 with the assumption that the os is honest about supporting optional extensions
	RENDERER_OPENGL_21_LEGACY, // 2.1 with the assumption that os is lying about what is supported
	RENDERER_COUNT,
};

struct AppSettings {
	int windowWidth;
	int windowHeight;
	int windowX;
	int windowY;
	int maximized;
	int ui_scale;
	int render_scale;
	int engine;
	std::string gamedir;
	bool valid;
	int undoLevels;
	bool verboseLogs;
	bool autoload_layout;
	int autoload_layout_width;
	int autoload_layout_height;
	bool confirm_exit;
	bool invert_y_axis;
	bool first_load;
	int mapsize_min;
	int mapsize_max;
	bool mapsize_auto;
	bool texture_filtering;
	bool backface_wireframe;
	int renderer;
	bool animate_models;
	bool freetype_font;
	bool texture_atlas;
	bool pal_textures;
	int max_texture_size;

	bool debug_open;
	bool keyvalue_open;
	bool transform_open;
	bool log_open;
	bool settings_open;
	bool limits_open;
	bool entreport_open;
	int settings_tab;

	float fov;
	float zfar;
	float zFarMdl; // z distance for model rendering
	float moveSpeed;
	float rotSpeed;
	int render_flags;
	bool vsync;
	bool show_wpoly;
	bool show_transform_axes;

	std::vector<std::string> fgdPaths;
	std::vector<std::string> resPaths;
	std::vector<std::string> recentFiles;

	void loadDefault();
	void load();
	void save();
	void addRecentFile(std::string map);
};