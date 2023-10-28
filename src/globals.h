#pragma once
#include "ProgressMeter.h"
#include <vector>
#include <string>
#include <mutex>
#include "AppSettings.h"

class Renderer;

extern bool g_verbose;
extern ProgressMeter g_progress;
extern std::vector<std::string> g_log_buffer;
extern const char* g_version_string;
extern std::mutex g_log_mutex;

extern AppSettings g_settings;
extern Renderer* g_app;

extern std::string g_config_dir;
extern std::string g_settings_path;

extern int g_render_flags;