#include "util.h"
#include "Bsp.h"
#include <string>
#include <algorithm>
#include <iostream>

int merge_maps(vector<string> options) {
	
	if (options.size() <= 1) {
		cout << "ERROR: at least input maps required for merging\n";
		return 1;
	}

	string output_path = options[options.size() - 1];

	Bsp outputMap;

	for (int i = 0; i < options.size() - 1; i++) {

	}

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

