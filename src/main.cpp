#include "util.h"
#include "BspMerger.h"
#include <string>
#include <algorithm>
#include <iostream>
#include "CommandLine.h"
#include "remap.h"
#include "Renderer.h"
#include "winding.h"

// super todo:
// gui scale not accurate and mostly broken
// invalid solid undo not reverting plane vertex positions sometimes
// backwards mins/maxs when creating second teleport in scale mode and cant drag the handle
// dbm_14 invisible triggers not showing clipndoes.
// can't select by clipnodes when manually toggled on and rendering disabled for ents
// deleting ents breaks entity report list when filter is used
// update merge logic for v2 scripts
// abort scale/vertex edits if an overflow occurs
// 3d axes don't appear until moving mouse over 3D view sometimes
// "Hide" axes setting not loaded properly
// crash using 3d scale axes

// todo:
// add option to simplify clipnode hulls with QHull for shrinkwrap-style bounding volumes
// merge redundant submodels and duplicate structures
// no lightmap renders black faces if no lightmap data for face
// select overlapping entities by holding mouse down
// multi-select with ctrl
// recalculate lightmaps when scaling objects
// normalized clip type for clipnode regeneration (fixes broken collision around 90+ degree angle edges)
// lerp plane distance in regenerated clipnodes between bbox height and width
// uniform scaling
// highlight non-planar faces in vertex edit mode
// subdivided faces can't be transformed in verte edit mode
// auto-clean after a while? Unused data will pile up after a lot of face splitting
// scale fps overlay + toolbar offset with font size
// reference aaatrigger from wad instead of embedding it if it doesnt exist
// red highlight not working with lightmaps disabled
// undo history
// invalid solid log spam
// scaling allowing concave solids (merge0.bsp angled wedge)
// can't select faces sometimes
// make all commands available in the 3d editor
// transforms gradually waste more and more planes+clipnodes until the map overflows (need smarter updates)
// "Validate" doesn't return any response.. -Sparks (add a results window or something for that + clean/optimize)
// copy-paste ents from Jack -Outerbeast
// parse CFG and add bspguy_equip ents for each transition
// clipnode models sometimes missing faces or extending to infinity
// - floating point inaccuracies probably. Changing starting cube size also changes the model
// show tooltip when hovering over ent target/caller
// customize limits and remove auto-fgd logic so the editor isn't sven-specific in any way
// Add tooltips for everything
// first-time launch help window or something
// make .bsp extension optional when opening editor
// texture browser

// minor todo:
// warn about game_playerjoin and other special names
// dump model info for the rest of the data types
// delete all frames from unused animated textures
// moving maps can cause bad surface extents which could cause lightmap seams?
// see if balancing the BSP tree is possible and if helps performance at all
// - https://www.researchgate.net/publication/238348725_A_Tutorial_on_Binary_Space_Partitioning_Trees
// - "Balanced is optimal only when the geometry is uniformly distributed, which is rarely the case."
// delete all submodel leaves to save space. They're unused and waste space, yet the compiler includes them...?
// vertex editing + clipping (+ CSG?) for all BSP models. Basically reimplement all of Hammer... and hlbsp/vis/rad... kek
// delete embedded texture mipmaps to save space
// vertex manipulation: face inversions should be invalid
// vertex manipulation: max face extents should be invalid
// vertex manipulation: colplanar node planes should be invalid
// add command to check which ents are preventing hull 2 delete

// refactoring:
// stop mixing camel case and underscores
// parse vertors in util, not Keyvalue
// add class destructors and delete everything that's new'd
// render and bsp classes are way too big and doing too many things and render has too many state checks

// Ideas for commands:
// copymodel:
//		- copies a model from the source map into the target map (for adding new perfectly shaped brush ents)
// addbox:
//		- creates a new box-shaped brush model (faster than copymodel if you don't need anything fancy)
// extract:
//		- extracts an isolated room from the BSP
// decompile:
//      - to RMF. Try creating brushes from convex face connections?
// export:
//      - export BSP models to MDL models.

// Notes:
// Removing HULL 0 from any model crashes when shooting unless it's EF_NODRAW or renderamt=0
// Removing HULL 0 from solid model crashes game when standing on it


char g_version_string[] = "bspguy v4 WIP (December, 2021)";

bool g_verbose = false;

// remove unused data before modifying anything to avoid misleading results
void remove_unused_data(Bsp* map) {
	STRUCTCOUNT removed = map->remove_unused_model_structures();

	if (!removed.allZero()) {
		logf("Deleting unused data:\n");
		removed.print_delete_stats(1);
		g_progress.clear();
		logf("\n");
	}
}

#ifdef WIN32
#include <Windows.h>
#endif

void hideConsoleWindow() {
#ifdef WIN32
#ifdef NDEBUG
	::ShowWindow(::GetConsoleWindow(), SW_HIDE);
#endif
#endif
}

void start_viewer(std::string map) {
	if (map.size() > 0 && !fileExists(map)) {
		logf("ERROR: File not found: %s", map.c_str());
		return;
	}
	Renderer renderer = Renderer();
	if (map.size())
		renderer.addMap(new Bsp(map));
	renderer.reloadBspModels();
	hideConsoleWindow();
	renderer.renderLoop();
}

int test() {
	//start_viewer("hl_c09.bsp");
	//return 0;

	std::vector<Bsp*> maps;

	for (int i = 1; i < 22; i++) {
		Bsp* map = new Bsp("2nd/saving_the_2nd_amendment" + (i > 1 ? std::to_string(i) : "") + ".bsp");
		maps.push_back(map);
	}

	//maps.push_back(new Bsp("op4/of1a1.bsp"));
	//maps.push_back(new Bsp("op4/of1a2.bsp"));
	//maps.push_back(new Bsp("op4/of1a3.bsp"));
	//maps.push_back(new Bsp("op4/of1a4.bsp"));

	STRUCTCOUNT removed;
	memset(&removed, 0, sizeof(removed));

	g_verbose = true;
	for (int i = 0; i < maps.size(); i++) {
		if (!maps[i]->valid) {
			return 1;
		}
		if (!maps[i]->validate()) {
			logf("");
		}
		logf("Preprocess %s\n", maps[i]->name.c_str());
		maps[i]->delete_hull(2, 1);
		//removed.add(maps[i]->delete_unused_hulls());
		removed.add(maps[i]->remove_unused_model_structures());

		if (!maps[i]->validate())
			logf("");
	}

	removed.print_delete_stats(1);

	BspMerger merger;
	Bsp* result = merger.merge(maps, vec3(1, 1, 1), "yabma_move", false, false);
	logf("\n");
	if (result) {
		result->write("yabma_move.bsp");
		result->write("D:/Steam/steamapps/common/Sven Co-op/svencoop_addon/maps/yabma_move.bsp");
		result->print_info(false, 0, false);
	}

	start_viewer("yabma_move.bsp");

	return 0;
}

int merge_maps(CommandLine& cli) {
	std::vector<std::string> input_maps = cli.getOptionList("-maps");

	if (input_maps.size() < 2) {
		logf("ERROR: at least 2 input maps are required\n");
		return 1;
	}

	std::vector<Bsp*> maps;

	for (int i = 0; i < input_maps.size(); i++) {
		Bsp* map = new Bsp(input_maps[i]);
		if (!map->valid)
		{
			delete map;
			return 1;
		}
		maps.push_back(map);
	}

	for (int i = 0; i < maps.size(); i++) {
		logf("Preprocessing %s:\n", maps[i]->name.c_str());

		logf("    Deleting unused data...\n");
		STRUCTCOUNT removed = maps[i]->remove_unused_model_structures();
		g_progress.clear();
		removed.print_delete_stats(2);

		if (cli.hasOption("-nohull2") || (cli.hasOption("-optimize") && !maps[i]->has_hull2_ents())) {
			logf("    Deleting hull 2...\n");
			maps[i]->delete_hull(2, 1);
			maps[i]->remove_unused_model_structures().print_delete_stats(2);
		}

		if (cli.hasOption("-optimize")) {
			logf("    Optmizing...\n");
			maps[i]->delete_unused_hulls().print_delete_stats(2);
		}

		logf("\n");
	}

	vec3 gap = cli.hasOption("-gap") ? cli.getOptionVector("-gap") : vec3(0, 0, 0);

	std::string output_name = cli.hasOption("-o") ? cli.getOption("-o") : cli.bspfile;

	BspMerger merger;
	Bsp* result = merger.merge(maps, gap, output_name, cli.hasOption("-noripent"), cli.hasOption("-noscript"));

	logf("\n");
	if (result->isValid()) result->write(output_name);
	logf("\n");
	result->print_info(false, 0, 0);

	for (int i = 0; i < maps.size(); i++) {
		delete maps[i];
	}

	return 0;
}

int print_info(CommandLine& cli) {
	Bsp* map = new Bsp(cli.bspfile);
	if (!map->valid)
	{
		delete map;
		return 1;
	}

	bool limitMode = false;
	int listLength = 10;
	int sortMode = SORT_CLIPNODES;

	if (cli.hasOption("-limit")) {
		std::string limitName = cli.getOption("-limit");

		limitMode = true;
		if (limitName == "clipnodes") {
			sortMode = SORT_CLIPNODES;
		}
		else if (limitName == "nodes") {
			sortMode = SORT_NODES;
		}
		else if (limitName == "faces") {
			sortMode = SORT_FACES;
		}
		else if (limitName == "vertexes") {
			sortMode = SORT_VERTS;
		}
		else {
			logf("ERROR: invalid limit name: %s\n", limitName.c_str());
			return 0;
		}
	}
	if (cli.hasOption("-all")) {
		listLength = 32768; // should be more than enough
	}

	map->print_info(limitMode, listLength, sortMode);

	delete map;

	return 0;
}

int noclip(CommandLine& cli) {
	Bsp* map = new Bsp(cli.bspfile);
	if (!map->valid)
	{
		delete map;
		return 1;
	}

	int model = -1;
	int hull = -1;
	int redirect = 0;

	if (cli.hasOption("-hull")) {
		hull = cli.getOptionInt("-hull");

		if (hull < 0 || hull >= MAX_MAP_HULLS) {
			logf("ERROR: hull number must be 0-3\n");
			return 1;
		}
	}

	if (cli.hasOption("-redirect")) {
		if (!cli.hasOption("-hull")) {
			logf("ERROR: -redirect must be used with -hull\n");
			return 1;
		}
		redirect = cli.getOptionInt("-redirect");

		if (redirect < 1 || redirect >= MAX_MAP_HULLS) {
			logf("ERROR: redirect hull number must be 1-3\n");
			return 1;
		}
		if (redirect == hull) {
			logf("ERROR: Can't redirect hull to itself\n");
			return 1;
		}
	}

	remove_unused_data(map);

	if (cli.hasOption("-model")) {
		model = cli.getOptionInt("-model");

		if (model < 0 || (unsigned int)model >= map->modelCount) {
			logf("ERROR: model number must be 0 - %d\n", map->modelCount);
			return 1;
		}

		if (hull != -1) {
			if (redirect)
				logf("Redirecting HULL %d to HULL %d in model %d:\n", hull, redirect, model);
			else
				logf("Deleting HULL %d from model %d:\n", hull, model);

			map->delete_hull(hull, model, redirect);
		}
		else {
			logf("Deleting HULL 1, 2, and 3 from model %d:\n", model);
			for (int i = 1; i < MAX_MAP_HULLS; i++) {
				map->delete_hull(i, model, redirect);
			}
		}
	}
	else {
		if (hull == 0) {
			logf("HULL 0 can't be stripped globally. The entire map would be invisible!\n");
			return 0;
		}

		if (hull != -1) {
			if (redirect)
				logf("Redirecting HULL %d to HULL %d:\n", hull, redirect);
			else
				logf("Deleting HULL %d:\n", hull);
			map->delete_hull(hull, redirect);
		}
		else {
			logf("Deleting HULL 1, 2, and 3:\n", hull);
			for (int i = 1; i < MAX_MAP_HULLS; i++) {
				map->delete_hull(i, redirect);
			}
		}
	}

	STRUCTCOUNT removed = map->remove_unused_model_structures();

	if (!removed.allZero())
		removed.print_delete_stats(1);
	else if (redirect == 0)
		logf("    Model hull(s) was previously deleted or redirected.");
	logf("\n");

	if (map->isValid()) map->write(cli.hasOption("-o") ? cli.getOption("-o") : map->path);
	logf("\n");

	map->print_info(false, 0, 0);

	delete map;

	return 0;
}

int simplify(CommandLine& cli) {
	Bsp* map = new Bsp(cli.bspfile);
	if (!map->valid)
	{
		delete map;
		return 1;
	}

	int hull = 0;

	if (!cli.hasOption("-model")) {
		logf("ERROR: -model is required\n");
		return 1;
	}

	if (cli.hasOption("-hull")) {
		hull = cli.getOptionInt("-hull");

		if (hull < 1 || hull >= MAX_MAP_HULLS) {
			logf("ERROR: hull number must be 1-3\n");
			return 1;
		}
	}

	int modelIdx = cli.getOptionInt("-model");

	remove_unused_data(map);

	STRUCTCOUNT oldCounts(map);

	if (modelIdx < 0 || (unsigned int)modelIdx >= map->modelCount) {
		logf("ERROR: model number must be 0 - %d\n", map->modelCount);
		return 1;
	}

	if (hull != 0) {
		logf("Simplifying HULL %d in model %d:\n", hull, modelIdx);
	}
	else {
		logf("Simplifying collision hulls in model %d:\n", modelIdx);
	}

	map->simplify_model_collision(modelIdx, hull);

	map->remove_unused_model_structures();

	STRUCTCOUNT newCounts(map);

	STRUCTCOUNT change = oldCounts;
	change.sub(newCounts);

	if (!change.allZero())
		change.print_delete_stats(1);

	logf("\n");

	if (map->isValid()) map->write(cli.hasOption("-o") ? cli.getOption("-o") : map->path);
	logf("\n");

	map->print_info(false, 0, 0);

	delete map;

	return 0;
}

int deleteCmd(CommandLine& cli) {
	Bsp* map = new Bsp(cli.bspfile);
	if (!map->valid)
	{
		delete map;
		return 1;
	}
	remove_unused_data(map);

	if (cli.hasOption("-model")) {
		int modelIdx = cli.getOptionInt("-model");

		logf("Deleting model %d:\n", modelIdx);
		map->delete_model(modelIdx);
		map->update_ent_lump();
		STRUCTCOUNT removed = map->remove_unused_model_structures();

		if (!removed.allZero())
			removed.print_delete_stats(1);
		logf("\n");
	}

	if (map->isValid()) map->write(cli.hasOption("-o") ? cli.getOption("-o") : map->path);
	logf("\n");

	map->print_info(false, 0, 0);

	delete map;

	return 0;
}

int transform(CommandLine& cli) {
	Bsp* map = new Bsp(cli.bspfile);
	if (!map->valid)
	{
		delete map;
		return 1;
	}

	vec3 move;

	if (cli.hasOptionVector("-move")) {
		move = cli.getOptionVector("-move");

		logf("Applying offset (%.2f, %.2f, %.2f)\n",
			move.x, move.y, move.z);

		map->move(move);
	}
	else {
		logf("ERROR: at least one transformation option is required\n");
		delete map;
		return 1;
	}

	if (map->isValid()) map->write(cli.hasOption("-o") ? cli.getOption("-o") : map->path);
	logf("\n");

	map->print_info(false, 0, 0);

	delete map;

	return 0;
}

int unembed(CommandLine& cli) {
	Bsp* map = new Bsp(cli.bspfile);
	if (!map->valid)
	{
		delete map;
		return 1;
	}

	int deleted = map->delete_embedded_textures();
	logf("Deleted %d embedded textures\n", deleted);

	if (map->isValid()) map->write(cli.hasOption("-o") ? cli.getOption("-o") : map->path);
	logf("\n");

	return 0;
}

void print_help(std::string command) {
	if (command == "merge") {
		logf(
			"merge - Merges two or more maps together\n\n"

			"Usage:   bspguy merge <mapname> -maps \"map1, map2, ... mapN\" [options]\n"
			"Example: bspguy merge merged.bsp -maps \"svencoop1, svencoop2\"\n"

			"\n[Options]\n"
			"  -optimize    : Deletes unused model hulls before merging.\n"
			"                 This can be risky and crash the game if assumptions about\n"
			"                 entity visibility/solidity are wrong.\n"
			"  -nohull2     : Forces redirection of hull 2 to hull 1 in each map before merging.\n"
			"                 This reduces clipnodes at the expense of less accurate collision\n"
			"                 for large monsters and pushables.\n"
			"  -noripent    : By default, the input maps are assumed to be part of a series.\n"
			"                 Level changes and other things are updated so that the merged\n"
			"                 maps can be played one after another. This flag prevents any\n"
			"                 entity edits from being made (except for origins).\n"
			"  -noscript    : By default, the output map is expected to run with the bspguy\n"
			"                 map script loaded, which ensures only entities for the current\n"
			"                 map section are active. This flag replaces that script with less\n"
			"                 effective entity logic. This may cause lag in maps with lots of\n"
			"                 entities, and some ents might not spawn properly. The benefit\n"
			"                 to this flag is that you don't have deal with script setup.\n"
			"  -gap \"X,Y,Z\" : Amount of extra space to add between each map\n"
			"  -v\n"
			"  -verbose     : Verbose console output.\n"
		);
	}
	else if (command == "info") {
		logf(
			"info - Show BSP data summary\n\n"

			"Usage:   bspguy info <mapname> [options]\n"
			"Example: bspguy info svencoop1.bsp -limit clipnodes -all\n"

			"\n[Options]\n"
			"  -limit <name> : List the models contributing most to the named limit.\n"
			"                  <name> can be one of: [clipnodes, nodes, faces, vertexes]\n"
			"  -all          : Show the full list of models when using -limit.\n"
		);
	}
	else if (command == "noclip") {
		logf(
			"noclip - Delete some clipnodes from the BSP\n\n"

			"Usage:   bspguy noclip <mapname> [options]\n"
			"Example: bspguy noclip svencoop1.bsp -hull 2\n"

			"\n[Options]\n"
			"  -model #    : Model to strip collision from. By default, all models are stripped.\n"
			"  -hull #     : Collision hull to delete (0-3). By default, hulls 1-3 are deleted.\n"
			"                0 = Point-sized entities. Required for rendering\n"
			"                1 = Human-sized monsters and standing players\n"
			"                2 = Large monsters and pushables\n"
			"                3 = Small monsters, crouching players, and melee attacks\n"
			"  -redirect # : Redirect to this hull after deleting the target hull's clipnodes.\n"
			"                For example, redirecting hull 2 to hull 1 would allow large\n"
			"                monsters to function normally instead of falling out of the world.\n"
			"                Must be used with the -hull option.\n"
			"  -o <file>   : Output file. By default, <mapname> is overwritten.\n"
		);
	}
	else if (command == "simplify") {
		logf(
			"simplify - Replaces model hulls with a simple bounding box\n\n"

			"Usage:   bspguy simplify <mapname> [options]\n"
			"Example: bspguy simplify svencoop1.bsp -model 3\n"

			"\n[Options]\n"
			"  -model #    : Model to simplify. Required.\n"
			"  -hull #     : Collision hull to simplify. By default, all hulls are simplified.\n"
			"                1 = Human-sized monsters and standing players\n"
			"                2 = Large monsters and pushables\n"
			"                3 = Small monsters, crouching players, and melee attacks\n"
			"  -o <file>   : Output file. By default, <mapname> is overwritten.\n"
		);
	}
	else if (command == "delete") {
		logf(
			"delete - Delete BSP models.\n\n"

			"Usage:   bspguy delete <mapname> [options]\n"
			"Example: bspguy delete svencoop1.bsp -model 3\n"

			"\n[Options]\n"
			"  -model #  : Model to delete. Entities that reference the deleted\n"
			"              model will be updated to use error.mdl instead.\n"
			"  -o <file> : Output file. By default, <mapname> is overwritten.\n"
		);
	}
	else if (command == "transform") {
		logf(
			"transform - Apply 3D transformations\n\n"

			"Usage:   bspguy transform <mapname> [options]\n"
			"Example: bspguy transform svencoop1.bsp -move \"0,0,1024\"\n"

			"\n[Options]\n"
			"  -move \"X,Y,Z\" : Units to move the map on each axis.\n"
			"  -o <file>     : Output file. By default, <mapname> is overwritten.\n"
		);
	}
	else if (command == "unembed") {
		logf(
			"unembed - Deletes embedded texture data, so that they reference WADs instead.\n\n"

			"Usage:   bspguy unembed <mapname>\n"
			"Example: bspguy unembed c1a0.bsp\n"
		);
	}
	else if (command == "exportobj") {
		logf(
			"exportobj - Export bsp geometry to obj [WIP].\n\n"

			"Usage:   bspguy exportobj <mapname>\n"
			"Example: bspguy exportobj c1a0.bsp\n"
		);
	}
	else if (command == "editor" || command == "empty") {
		logf(
			"editor -\n"
			"empty - Open bspguy editor window.\n\n"

			"Usage:   bspguy editor\n"
			"Usage:   bspguy empty\n"
		);
	}
	else {
		logf("%s\n\n", g_version_string);
		logf(
			"This tool modifies Sven Co-op BSPs without having to decompile them.\n\n"
			"Usage: bspguy <command> <mapname> [options]\n"

			"\n<Commands>\n"
			"  info      : Show BSP data summary\n"
			"  merge     : Merges two or more maps together\n"
			"  noclip    : Delete some clipnodes/nodes from the BSP\n"
			"  delete    : Delete BSP models\n"
			"  simplify  : Simplify BSP models\n"
			"  transform : Apply 3D transformations to the BSP\n"
			"  unembed   : Deletes embedded texture data\n"
			"  exportobj   : Export bsp geometry to obj [WIP]\n"
			"  editor, empty   : Open empty bspguy window\n"

			"\nRun 'bspguy <command> help' to read about a specific command.\n"
			"\nTo launch the 3D editor. Drag and drop a .bsp file onto the executable,\n"
			"or run 'bspguy <mapname>'"
		);
	}
}

int main(int argc, char* argv[])
{
#ifdef WIN32
	::ShowWindow(::GetConsoleWindow(), SW_SHOW);
#endif

#ifdef USE_FILESYSTEM
	fs::path ph = argv[0];
	fs::current_path(ph.parent_path());
#endif


	g_settings_path = fileExists(GetCurrentWorkingDir() + "bspguy.cfg") ? GetCurrentWorkingDir() + "bspguy.cfg" : getConfigDir() + "bspguy.cfg";
	g_config_dir = fileExists(GetCurrentWorkingDir() + "bspguy.cfg") ? GetCurrentWorkingDir() : getConfigDir();

	//return test();

	CommandLine cli(argc, argv);

	if (cli.command == "help" || cli.command == "--help" || cli.command == "-help" ||
		cli.command == "h" || cli.command == "--h" || cli.command == "-h"
		|| cli.command == "/?") {
		logf(g_version_string);
		return 0;
	}

	if (cli.command == "version" || cli.command == "--version" || cli.command == "-version") {
		logf(g_version_string);
		return 0;
	}

	if (cli.command == "exportobj") {
		Bsp* tmpBsp = new Bsp(cli.bspfile);
		tmpBsp->ExportToObjWIP(cli.bspfile);
		delete tmpBsp;
		return 0;
	}

	if (cli.hasOption("-v") || cli.hasOption("-verbose")) {
		g_verbose = true;
	}

	if (cli.command == "info") {
		return print_info(cli);
	}
	else if (cli.command == "noclip") {
		return noclip(cli);
	}
	else if (cli.command == "simplify") {
		return simplify(cli);
	}
	else if (cli.command == "delete") {
		return deleteCmd(cli);
	}
	else if (cli.command == "transform") {
		return transform(cli);
	}
	else if (cli.command == "merge") {
		return merge_maps(cli);
	}
	else if (cli.command == "unembed") {
		return unembed(cli);
	}
	else {
		logf("%s\n", ("Start bspguy editor with map: " + cli.bspfile).c_str());
		logf("Load settings from : %s\n", g_settings_path.c_str());
		start_viewer(cli.bspfile);
	}
	return 0;
}

