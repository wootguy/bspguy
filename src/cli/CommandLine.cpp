#include "CommandLine.h"

CommandLine::CommandLine(int argc, char* argv[]) {

	askingForHelp = argc <= 1;

	for (int i = 0; i < argc; i++)
	{
		std::string arg = argv[i];
		std::string larg = toLowerCase(arg);

		if (i == 1) {
			command = larg;
		}
		if (i == 2) {
			bspfile = larg;
		}
		if (i > 2)
		{
			options.push_back(arg);
		}

		if ((i == 1 || i == 2) && larg.find("help") == 0 || larg.find("--help") == 0 || larg.find("-help") == 0) {
			askingForHelp = true;
		}
	}

	if (askingForHelp) {
		return;
	}

	for (int i = 0; i < options.size(); i++) {
		std::string opt = toLowerCase(options[i]);

		if (i < options.size() - 1) {
			optionVals[opt] = options[i + 1];
		}
		else {
			optionVals[opt].clear();
		}
	}

	if (argc == 2)
	{
		bspfile = argv[1];
	}
}

bool CommandLine::hasOption(std::string optionName) {
	return optionVals.find(optionName) != optionVals.end();
}

bool CommandLine::hasOptionVector(std::string optionName) {
	if (!hasOption(optionName))
		return false;

	std::string val = optionVals[optionName];
	std::vector<std::string> parts = splitString(val, ",");

	if (parts.size() != 3) {
		logf("ERROR: invalid number of coordinates for option %s\n", optionName.c_str());
		return false;
	}

	return true;
}

std::string CommandLine::getOption(std::string optionName) {
	return optionVals[optionName];
}

int CommandLine::getOptionInt(std::string optionName) {
	return atoi(optionVals[optionName].c_str());
}

vec3 CommandLine::getOptionVector(std::string optionName) {
	vec3 ret;
	std::vector<std::string> parts = splitString(optionVals[optionName], ",");

	if (parts.size() != 3) {
		logf("ERROR: invalid number of coordinates for option %s\n", optionName.c_str());
		return ret;
	}

	ret.x = atof(parts[0].c_str());
	ret.y = atof(parts[1].c_str());
	ret.z = atof(parts[2].c_str());

	return ret;
}

std::vector<std::string> CommandLine::getOptionList(std::string optionName) {
	std::vector<std::string> parts = splitString(optionVals[optionName], ",");

	for (int i = 0; i < parts.size(); i++) {
		parts[i] = trimSpaces(parts[i]);
	}

	return parts;
}
