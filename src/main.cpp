#include "util.h"
#include "Bsp.h"
#include <string>
#include <algorithm>
#include <iostream>

// super todo:
// broken lightmaps shifting stadium4 up 768 units
// levers in the air when moving stadium4

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

// Lightmap debugging:
// resizing glitchy, sometimes you offset the lightmap but not always the same way?
// lightmap size calulation has precision problems. You can shift the map so that the lightmap size
// doesn't change for a single face but probably impossible for all faces at the same time.

int merge_maps(vector<string> options) {
	
	if (options.size() <= 1) {
		cout << "ERROR: at least input maps required for merging\n";
		return 1;
	}

	//Bsp asdf("yabma_move.bsp");
	//asdf.dump_lightmap(5, "lightmap/rad.png");
	//return 0;

	Bsp* mapA = new Bsp("yabma.bsp");
	//Bsp* mapA = new Bsp("saving_the_2nd_amendment.bsp");
	//Bsp* mapB = new Bsp("saving_the_2nd_amendment2.bsp");
	//Bsp* mapC = new Bsp("saving_the_2nd_amendment3.bsp");
	//Bsp* mapD = new Bsp("saving_the_2nd_amendment4.bsp");

	vector<Bsp*> maps;
	//maps.push_back(mapB);
	//maps.push_back(mapC);
	//maps.push_back(mapD);

	//mapA->merge(maps, vec3(0, 0, 0));
	/*
	vec3 offset = vec3(3072, 2944, -736);

	Bsp map("stadium4.bsp");
	byte* oldVertData = new byte[map.header.lump[LUMP_VERTICES].nLength];
	byte* oldTexInfo = new byte[map.header.lump[LUMP_TEXINFO].nLength];
	memcpy(oldVertData, map.lumps[LUMP_VERTICES], map.header.lump[LUMP_VERTICES].nLength);
	memcpy(oldTexInfo, map.lumps[LUMP_TEXINFO], map.header.lump[LUMP_TEXINFO].nLength);

	float range = 1024;
	float step = range / 16.0f;
	for (float z = -range; z < range; z += step) {
		for (float y = -range; y < range; y += step) {
			for (float x = -range; x < range; x += step) {
				
				vec3 test = vec3(x, y, z);

				bool worked = map.move(offset);
				memcpy(map.lumps[LUMP_VERTICES], oldVertData, map.header.lump[LUMP_VERTICES].nLength);
				memcpy(map.lumps[LUMP_TEXINFO], oldTexInfo, map.header.lump[LUMP_TEXINFO].nLength);

				if (worked) {
					printf("Move %f %f %f  %s\n", test.x, test.y, test.z, "PASS");
					return 0;
				}
				else {
					printf("Move %f %f %f  %s  %d\n", test.x, test.y, test.z, "FAIL", map.mismatchCount);
				}
			}
		}
	}
	return 0;
	*/
	mapA->move(vec3(-3072, 2944, -736));
	//mapA->merge(*mapB);
	mapA->write("yabma_move.bsp");
	mapA->write("D:/Steam/steamapps/common/Sven Co-op/svencoop_addon/maps/yabma_move.bsp");

	mapA->print_info();
	//mapA.print_bsp();

	return 0;
	
	string output_path = options[options.size() - 1];

	Bsp outputMap;

	for (int i = 0; i < options.size() - 1; i++) {
		cout << "Opening " << options[i] << endl;
		Bsp inputMap(options[i]);

		outputMap.merge(inputMap);
	}

	outputMap.write(output_path);

	//outputMap.print_bsp();
	//outputMap.pointContents(0, { 256, 256, 128 });

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

