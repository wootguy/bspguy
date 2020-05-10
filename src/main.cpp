#include "util.h"
#include "BspMerger.h"
#include <string>
#include <algorithm>
#include <iostream>

// super todo:
// game crashes randomly, usually a few minutes after not focused on the game
// center merge cube

// minor todo:
// trigger_changesky for series maps with different skies
// reaplce trigger_autos
// warn about game_playerjoin and other special names
// move static origin targets?
// fix spawners for things with custom keyvalues (apache, osprey, etc.)


// Ideas for commands:
// optimize:
//		- merges redundant submodels (copy-pasting a picard coin all over the map)
//		- remove hull2 or func_illusionary clipnodes
// copymodel:
//		- copies a model from the source map into the target map (for adding new perfectly shaped brush ents)
// addbox:
//		- creates a new box-shaped brush model (faster than copymodel if you don't need anything fancy)
// info (default command if none set):
//		- check how close the map is to each BSP limit
// extract:
//		- extracts an isolated room from the BSP
// decompile:
//      - to RMF. Try creating brushes from convex face connections?

int merge_maps(vector<string> options) {
	
	if (options.size() <= 1) {
		cout << "ERROR: at least input maps required for merging\n";
		return 1;
	}

	string output_path = options[options.size() - 1];
	
	/*
	Bsp test("saving_the_2nd_amendment7.bsp");
	//test.strip_clipping_hull(2);
	test.move(vec3(4992, 6240, 736));
	test.dump_lightmap_atlas("_after.png");
	test.write("yabma_move.bsp");
	test.write("D:/Steam/steamapps/common/Sven Co-op/svencoop_addon/maps/yabma_move.bsp");
	return 0;
	*/

	vector<Bsp*> maps;
	/*
	for (int i = 1; i < 22; i++) {
		Bsp* map = new Bsp("saving_the_2nd_amendment" + (i > 1 ? to_string(i) : "") + ".bsp");
		map->strip_clipping_hull(2);
		maps.push_back(map);
	}
	*/

	maps.push_back(new Bsp("echoes01.bsp"));
	maps.push_back(new Bsp("echoes01a.bsp"));
	maps.push_back(new Bsp("echoes02.bsp"));

	maps.push_back(new Bsp("echoes03.bsp"));
	maps.push_back(new Bsp("echoes04.bsp"));
	maps.push_back(new Bsp("echoes05.bsp"));

	maps.push_back(new Bsp("echoes06.bsp"));
	maps.push_back(new Bsp("echoes07.bsp"));
	
	//maps.push_back(new Bsp("echoes09.bsp"));
	//maps.push_back(new Bsp("echoes09a.bsp"));

	//maps.push_back(new Bsp("echoes09b.bsp"));
	//maps.push_back(new Bsp("echoes10.bsp"));

	//maps.push_back(new Bsp("echoes12.bsp"));
	//maps.push_back(new Bsp("echoes13.bsp"));

	//maps.push_back(new Bsp("echoes14.bsp"));
	//maps.push_back(new Bsp("echoes14b.bsp"));
	
	for (int i = 0; i < maps.size(); i++) {
		//maps[i]->strip_clipping_hull(2);
	}

	BspMerger merger;
	Bsp* result = merger.merge(maps, vec3(0, 0, 0));
	printf("\n");
	result->write("yabma_move.bsp");
	result->write("D:/Steam/steamapps/common/Sven Co-op/svencoop_addon/maps/yabma_move.bsp");
	result->print_info();
	return 0;
}

int main(int argc, char* argv[])
{
	// parse command-line args
	string command;
	vector<string> options;

	for (int i = 0; i < argc; i++)
	{
		string arg = argv[i];
		string larg = arg; // lowercase arg
		std::transform(larg.begin(), larg.end(), larg.begin(), ::tolower);

		if (i == 1) {
			command = larg;
		}
		if (i > 1)
		{
			if (command == "merge") {
				options.push_back(arg);
			}
		}

		if (larg.find("-help") == 0 || argc <= 1)
		{
			cout <<
			"This tool modifies Half-Life BSPs without decompiling them.\n\n"
			"Usage: bspguy <command> <options>]\n"

			"\n<Commands>\n"
			"  merge : Merges two or more maps together.\n"

			"\nExamples:\n"
			"  bspguy merge svencoop1.bsp svencoop2.bsp svencoop_merged.bsp\n"
			;
			return 0;
		}
	}
	
	if (options.size() == 0)
	{
		cout << "ERROR: No options specified.\n";
		return 1;
	}

	if (command == "merge") {
		return merge_maps(options);
	}
	else {
		cout << "unrecognized command: " << command << endl;
	}

	return 0;
}

