#include "util.h"
#include "Entity.h"

class CommandLine {
public:
	std::string command;
	std::string bspfile;
	std::vector<std::string> options;
	bool askingForHelp;

	CommandLine(int argc, char* argv[]);

	bool hasOption(std::string optionName);
	bool hasOptionVector(std::string optionName);

	std::string getOption(std::string optionName);
	int getOptionInt(std::string optionName);
	vec3 getOptionVector(std::string optionName);
	std::vector<std::string> getOptionList(std::string optionName);

private:
	hashmap optionVals;
};