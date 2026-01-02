#include "globals.h"
#include "util.h"
#include <thread>

using namespace std;

ProgressMeter g_progress;
vector<string> g_log_buffer;
mutex g_log_mutex;
std::thread::id g_main_thread_id = std::this_thread::get_id();

AppSettings g_settings;
string g_config_dir = getConfigDir();
string g_settings_path = g_config_dir + "bspguy.cfg";
Editor* g_app = NULL;
std::set<std::string> g_parsed_fgds;

MapLimits g_limits;
MapLimits g_engine_limits[ENGINE_TYPES];

GlobalShaders g_shaders;
RenderStats g_renderStats;

int g_max_texture_size = 512;
int g_max_texture_array_layers = 512;
int g_max_vtf_units = 0;
bool g_opengl_texture_array_support = false;