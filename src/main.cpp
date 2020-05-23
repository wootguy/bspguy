#include "util.h"
#include "BspMerger.h"
#include <string>
#include <algorithm>
#include <iostream>
#include "CommandLine.h"
#include "remap.h"
#include "Renderer.h"

// super todo:
// crash merging rl00
// WARNING hull redirect spam

// todo:
// visual editor like BSP Viewer for adding custom brush ents and stuff
// add option to simplify clipnode hulls with QHull for shrinkwrap-style bounding volumes
// merge redundant submodels and duplicate structures

// minor todo:
// trigger_changesky for series maps with different skies
// warn about game_playerjoin and other special names
// fix spawners for things with custom keyvalues (apache, osprey, etc.)
// dump model info for the rest of the data types
// delete all frames from unused animated textures
// apaches not deleted sometimes
// moving maps can cause bad surface extents which could cause lightmap seams?
// see if balancing the BSP tree is possible and if helps performance at all

// refactoring:
// stop mixing printf+cout
// parse vertors in util, not Keyvalue

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


const char* version_string = "bspguy v3 WIP (May 2020)";

bool g_verbose = false;

void printIndent(int indent) {
	for (int i = 0; i < indent; i++)
		printf("    ");
}

void print_stat(int indent, int stat, const char* data) {
	if (!stat)
		return;
	for (int i = 0; i < indent; i++)
		printf("    ");
	const char* plural = "s";
	if (string(data) == "vertex") {
		plural = "es";
	}

	printf("%s %d %s%s\n", stat > 0 ? "Deleted" : "Added", abs(stat), data, abs(stat) > 1 ? plural : "");
}

void print_stat_mem(int indent, int bytes, const char* data) {
	if (!bytes)
		return;
	for (int i = 0; i < indent; i++)
		printf("    ");
	printf("%s %.2f KB of %s\n", bytes > 0 ? "Deleted" : "Added", abs(bytes) / 1024.0f, data);
}


void print_delete_stats(int indent, STRUCTCOUNT stats) {
	print_stat(indent, stats.models, "model");
	print_stat(indent, stats.planes, "plane");
	print_stat(indent, stats.verts, "vertex");
	print_stat(indent, stats.nodes, "node");
	print_stat(indent, stats.texInfos, "texinfo");
	print_stat(indent, stats.faces, "face");
	print_stat(indent, stats.clipnodes, "clipnode");
	print_stat(indent, stats.leaves, "leave");
	print_stat(indent, stats.markSurfs, "marksurface");
	print_stat(indent, stats.surfEdges, "surfedge");
	print_stat(indent, stats.edges, "edge");
	print_stat(indent, stats.textures, "texture");
	print_stat_mem(indent, stats.lightdata, "lightmap data");
	print_stat_mem(indent, stats.visdata, "VIS data");
}

// remove unused data before modifying anything to avoid misleading results
void remove_unused_data(Bsp* map) {
	STRUCTCOUNT removed = map->remove_unused_model_structures();

	if (!removed.allZero()) {
		printf("Deleting unused data:\n");
		print_delete_stats(1, removed);
		g_progress.clear();
		printf("\n");
	}
}

int test() {

	Renderer renderer = Renderer();
	renderer.renderLoop();
	return 0;

	/*
	Bsp test("merge0.bsp");
	test.simplify_model_collision(2, 0);
	test.write("yabma_move.bsp");
	test.write("D:/Steam/steamapps/common/Sven Co-op/svencoop_addon/maps/yabma_move.bsp");
	test.print_info(false, 0, 0);
	return 0;
	*/

	vector<Bsp*> maps;
	/*
	for (int i = 1; i < 22; i++) {
		Bsp* map = new Bsp("2nd/saving_the_2nd_amendment" + (i > 1 ? to_string(i) : "") + ".bsp");
		maps.push_back(map);
	}
	*/

	//maps.push_back(new Bsp("echoes/echoes01.bsp"));
	//maps.push_back(new Bsp("echoes/echoes01a.bsp"));
	//maps.push_back(new Bsp("echoes/echoes02.bsp"));

	//maps.push_back(new Bsp("merge0.bsp"));
	//maps.push_back(new Bsp("merge1.bsp"));
	//maps.push_back(new Bsp("2nd/saving_the_2nd_amendment.bsp"));

	//maps.push_back(new Bsp("op4/of1a1.bsp"));
	//maps.push_back(new Bsp("op4/of1a2.bsp"));
	//maps.push_back(new Bsp("op4/of1a3.bsp"));
	//maps.push_back(new Bsp("op4/of1a4.bsp"));

	maps.push_back(new Bsp("rl/rl00r.bsp"));
	maps.push_back(new Bsp("rl/rl00s.bsp"));

	STRUCTCOUNT removed;
	memset(&removed, 0, sizeof(removed));

	g_verbose = true;
	for (int i = 0; i < maps.size(); i++) {
		if (!maps[i]->valid) {
			return 1;
		}
		if (!maps[i]->validate()) {
			printf("");
		}
		printf("Preprocess %s\n", maps[i]->name.c_str());
		//maps[i]->delete_hull(2, 1);
		//removed.add(maps[i]->delete_unused_hulls());
		removed.add(maps[i]->remove_unused_model_structures());

		if (!maps[i]->validate())
			printf("");

		//maps[i]->print_info(true, 10, SORT_CLIPNODES);
	}

	print_delete_stats(1, removed);

	BspMerger merger;
	Bsp* result = merger.merge(maps, vec3(1, 1, 1), false);
	printf("\n");
	if (result != NULL) {
		result->write("yabma_move.bsp");
		result->write("D:/Steam/steamapps/common/Sven Co-op/svencoop_addon/maps/yabma_move.bsp");
		result->print_info(false, 0, false);
	}
	return 0;
}

int merge_maps(CommandLine& cli) {
	vector<string> input_maps = cli.getOptionList("-maps");

	if (input_maps.size() < 2) {
		cout << "ERROR: at least 2 input maps are required\n";
		return 1;
	}

	vector<Bsp*> maps;

	for (int i = 0; i < input_maps.size(); i++) {
		Bsp* map = new Bsp(input_maps[i]);
		if (!map->valid)
			return 1;
		maps.push_back(map);
	}

	for (int i = 0; i < maps.size(); i++) {
		printf("Preprocessing %s:\n", maps[i]->name.c_str());

		printf("    Deleting unused data...\n");
		STRUCTCOUNT removed = maps[i]->remove_unused_model_structures();
		g_progress.clear();
		print_delete_stats(2, removed);

		if (cli.hasOption("-nohull2") || (cli.hasOption("-optimize") && !maps[i]->has_hull2_ents())) {
			printf("    Deleting hull 2...\n");
			maps[i]->delete_hull(2, 1);
			print_delete_stats(2, maps[i]->remove_unused_model_structures() );
		}

		if (cli.hasOption("-optimize")) {
			printf("    Optmizing...\n");
			print_delete_stats(2, maps[i]->delete_unused_hulls() );
		}

		printf("\n");
	}
	
	vec3 gap = cli.hasOption("-gap") ? cli.getOptionVector("-gap") : vec3(0,0,0);

	BspMerger merger;
	Bsp* result = merger.merge(maps, gap, cli.hasOption("-noripent"));

	printf("\n");
	if (result->isValid()) result->write(cli.hasOption("-o") ? cli.getOption("-o") : cli.bspfile);
	printf("\n");
	result->print_info(false, 0, 0);

	for (int i = 0; i < maps.size(); i++) {
		delete maps[i];
	}

	return 0;
}

int print_info(CommandLine& cli) {
	Bsp* map = new Bsp(cli.bspfile);
	if (!map->valid)
		return 1;

	bool limitMode = false;
	int listLength = 10;
	int sortMode = SORT_CLIPNODES;

	if (cli.hasOption("-limit")) {
		string limitName = cli.getOption("-limit");
			
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
			cout << "ERROR: invalid limit name: " << limitName << endl;
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
		return 1;

	int model = -1;
	int hull = -1;
	int redirect = 0;

	if (cli.hasOption("-hull")) {
		hull = cli.getOptionInt("-hull");

		if (hull < 0 || hull >= MAX_MAP_HULLS) {
			cout << "ERROR: hull number must be 0-3\n";
			return 1;
		}
	}

	if (cli.hasOption("-redirect")) {
		if (!cli.hasOption("-hull")) {
			printf("ERROR: -redirect must be used with -hull\n");
			return 1;
		}
		redirect = cli.getOptionInt("-redirect");

		if (redirect < 1 || redirect >= MAX_MAP_HULLS) {
			cout << "ERROR: redirect hull number must be 1-3\n";
			return 1;
		}
		if (redirect == hull) {
			printf("ERROR: Can't redirect hull to itself\n");
			return 1;
		}
	}

	remove_unused_data(map);

	if (cli.hasOption("-model")) {
		model = cli.getOptionInt("-model");

		if (model < 0 || model >= map->modelCount) {
			cout << "ERROR: model number must be 0 - %d\n", map->modelCount;
			return 1;
		}

		if (hull != -1) {
			if (redirect)
				printf("Redirecting HULL %d to HULL %d in model %d:\n", hull, redirect, model);
			else
				printf("Deleting HULL %d from model %d:\n", hull, model);
			
			map->delete_hull(hull, model, redirect);
		}
		else {
			printf("Deleting HULL 1, 2, and 3 from model %d:\n", model);
			for (int i = 1; i < MAX_MAP_HULLS; i++) {
				map->delete_hull(i, model, redirect);
			}
		}
	}
	else {
		if (hull == 0) {
			printf("HULL 0 can't be stripped globally. The entire map would be invisible!\n");
			return 0;
		}

		if (hull != -1) {
			if (redirect)
				printf("Redirecting HULL %d to HULL %d:\n", hull, redirect);
			else
				printf("Deleting HULL %d:\n", hull);
			map->delete_hull(hull, redirect);
		}
		else {
			printf("Deleting HULL 1, 2, and 3:\n", hull);
			for (int i = 1; i < MAX_MAP_HULLS; i++) {
				map->delete_hull(i, redirect);
			}
		}
	}

	STRUCTCOUNT removed = map->remove_unused_model_structures();

	if (!removed.allZero())
		print_delete_stats(1, removed);
	else if (redirect == 0)
		printf("    Model hull(s) was previously deleted or redirected.");
	printf("\n");

	if (map->isValid()) map->write(cli.hasOption("-o") ? cli.getOption("-o") : map->path);
	printf("\n");

	map->print_info(false, 0, 0);

	delete map;

	return 0;
}

int simplify(CommandLine& cli) {
	Bsp* map = new Bsp(cli.bspfile);
	if (!map->valid)
		return 1;

	int hull = 0;

	if (!cli.hasOption("-model")) {
		printf("ERROR: -model is required\n");
		return 1;
	}

	if (cli.hasOption("-hull")) {
		hull = cli.getOptionInt("-hull");

		if (hull < 1 || hull >= MAX_MAP_HULLS) {
			cout << "ERROR: hull number must be 1-3\n";
			return 1;
		}
	}

	int modelIdx = cli.getOptionInt("-model");

	remove_unused_data(map);

	STRUCTCOUNT oldCounts(map);

	if (modelIdx < 0 || modelIdx >= map->modelCount) {
		cout << "ERROR: model number must be 0 - %d\n", map->modelCount;
		return 1;
	}

	if (hull != 0) {
		printf("Simplifying HULL %d in model %d:\n", hull, modelIdx);
	}
	else {
		printf("Simplifying collision hulls in model %d:\n", modelIdx);
	}

	map->simplify_model_collision(modelIdx, hull);

	map->remove_unused_model_structures();

	STRUCTCOUNT newCounts(map);

	STRUCTCOUNT change = oldCounts;
	change.sub(newCounts);

	if (!change.allZero())
		print_delete_stats(1, change);

	printf("\n");

	if (map->isValid()) map->write(cli.hasOption("-o") ? cli.getOption("-o") : map->path);
	printf("\n");

	map->print_info(false, 0, 0);

	delete map;

	return 0;
}

int deleteCmd(CommandLine& cli) {
	Bsp* map = new Bsp(cli.bspfile);
	if (!map->valid)
		return 1;

	remove_unused_data(map);

	if (cli.hasOption("-model")) {
		int modelIdx = cli.getOptionInt("-model");

		printf("Deleting model %d:\n", modelIdx);
		map->delete_model(modelIdx);
		map->update_ent_lump();
		STRUCTCOUNT removed = map->remove_unused_model_structures();

		if (!removed.allZero())
			print_delete_stats(1, removed);
		printf("\n");
	}

	if (map->isValid()) map->write(cli.hasOption("-o") ? cli.getOption("-o") : map->path);
	printf("\n");

	map->print_info(false, 0, 0);

	delete map;
}

int transform(CommandLine& cli) {
	Bsp* map = new Bsp(cli.bspfile);
	if (!map->valid)
		return 1;

	vec3 move;

	if (cli.hasOptionVector("-move")) {
		move = cli.getOptionVector("-move");

		printf("Applying offset (%.2f, %.2f, %.2f)\n",
			move.x, move.y, move.z);

		map->move(move);
	}
	else {
		printf("ERROR: at least one transformation option is required\n");
		return 1;
	}
	
	if (map->isValid()) map->write(cli.hasOption("-o") ? cli.getOption("-o") : map->path);
	printf("\n");

	map->print_info(false, 0, 0);

	delete map;
}

void print_help(string command) {
	if (command == "merge") {
		cout <<
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
			"  -gap \"X,Y,Z\" : Amount of extra space to add between each map\n"
			"  -v           : Verbose console output.\n"
			;
	}
	else if (command == "info") {
		cout <<
			"info - Show BSP data summary\n\n"

			"Usage:   bspguy info <mapname> [options]\n"
			"Example: bspguy info svencoop1.bsp -limit clipnodes -all\n"

			"\n[Options]\n"
			"  -limit <name> : List the models contributing most to the named limit.\n"
			"                  <name> can be one of: [clipnodes, nodes, faces, vertexes]\n"
			"  -all          : Show the full list of models when using -limit.\n"
			;
	}
	else if (command == "noclip") {
		cout <<
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
			;
	}
	else if (command == "simplify") {
		cout <<
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
			;
	}
	else if (command == "delete") {
		cout <<
			"delete - Delete BSP models.\n\n"

			"Usage:   bspguy delete <mapname> [options]\n"
			"Example: bspguy delete svencoop1.bsp -model 3\n"

			"\n[Options]\n"
			"  -model #  : Model to delete. Entities that reference the deleted\n"
			"              model will be updated to use error.mdl instead.\n"
			"  -o <file> : Output file. By default, <mapname> is overwritten.\n"
			;
	}
	else if (command == "transform") {
		cout <<
			"transform - Apply 3D transformations\n\n"

			"Usage:   bspguy transform <mapname> [options]\n"
			"Example: bspguy transform svencoop1.bsp -move \"0,0,1024\"\n"

			"\n[Options]\n"
			"  -move \"X,Y,Z\" : Units to move the map on each axis.\n"
			"  -o <file>     : Output file. By default, <mapname> is overwritten.\n"
			;
	}
	else {
		cout << version_string << endl << endl <<
			"This tool modifies Sven Co-op BSPs without having to decompile them.\n\n"
			"Usage: bspguy <command> <mapname> [options]\n"

			"\n<Commands>\n"
			"  info      : Show BSP data summary\n"
			"  merge     : Merges two or more maps together\n"
			"  noclip    : Delete some clipnodes/nodes from the BSP\n"
			"  delete    : Delete BSP models\n"
			"  simplify  : Simplify BSP models\n"
			"  transform : Apply 3D transformations to the BSP\n"

			"\nRun 'bspguy <command> help' to read about a specific command.\n"
			;
	}
}

int main(int argc, char* argv[])
{
	return test();

	CommandLine cli(argc, argv);

	if (cli.askingForHelp) {
		print_help(cli.command);
		return 0;
	}

	if (cli.command == "version" || cli.command == "--version" || cli.command == "-version" || cli.command == "-v") {
		printf(version_string);
		return 0;
	}

	if (cli.bspfile.empty()) {
		cout << "ERROR: no map specified\n"; return 1;
	}

	if (cli.hasOption("-v")) {
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
	else {
		cout << "unrecognized command: " << cli.command << endl;
	}

	return 0;
}

