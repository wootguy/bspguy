#include "util.h"
#include "BspMerger.h"
#include <string>
#include <algorithm>
#include <iostream>

// super todo:
// ents with same name will conflict (m60 tank)

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
	for (int i = 1; i < 22; i++) {
		if (i < 7)
			continue;
		Bsp* map = new Bsp("saving_the_2nd_amendment" + (i > 1 ? to_string(i) : "") + ".bsp");
		map->strip_clipping_hull(2);
		maps.push_back(map);
	}

	BspMerger merger;
	Bsp* result = merger.merge(maps, vec3(0, 0, 0));
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

