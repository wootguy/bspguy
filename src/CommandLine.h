#include "util.h"
#include "Entity.h"

class CommandLine {
public:
	string command;
	string bspfile;
	vector<string> options;
	bool askingForHelp;

	CommandLine(int argc, char* argv[]);

	bool hasOption(string optionName);
	bool hasOptionVector(string optionName);

	string getOption(string optionName);
	int getOptionInt(string optionName);
	vec3 getOptionVector(string optionName);
	vector<string> getOptionList(string optionName);

private:
	hashmap optionVals;
};