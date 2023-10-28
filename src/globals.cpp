#include "globals.h"
#include "util.h"

using namespace std;

ProgressMeter g_progress;
int g_render_flags;
vector<string> g_log_buffer;
mutex g_log_mutex;

AppSettings g_settings;
string g_config_dir = getConfigDir();
string g_settings_path = g_config_dir + "bspguy.cfg";
Renderer* g_app = NULL;