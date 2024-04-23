#pragma once
#include <string>
#include <vector>

struct AppSettings {
	int windowWidth;
	int windowHeight;
	int windowX;
	int windowY;
	int maximized;
	int fontSize;
	int engine;
	std::string gamedir;
	std::string workingdir;
	bool valid;
	int undoLevels;
	bool verboseLogs;

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
	float moveSpeed;
	float rotSpeed;
	int render_flags;
	bool vsync;
	bool show_transform_axes;
	bool backUpMap;

	std::vector<std::string> fgdPaths;
	std::vector<std::string> resPaths;

	void loadDefault();
	void load();
	void save();
};