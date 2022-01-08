#include "util.h"
#include "Entity.h"

class CommandLine {
public:
	std::string command;
	std::string bspfile;
	std::vector<std::string> options;
	bool askingForHelp;

	CommandLine(int argc, char* argv[]);

	bool hasOption(const std::string& optionName);
	bool hasOptionVector(const std::string& optionName);

	std::string getOption(const std::string& optionName);
	int getOptionInt(const std::string& optionName);
	vec3 getOptionVector(const std::string& optionName);
	std::vector<std::string> getOptionList(const std::string& optionName);

private:
	hashmap optionVals;
};