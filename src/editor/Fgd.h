#pragma once
#include <vector>
#include "colors.h"
#include <string>
#include <map>
#include <vector>
#include "types.h"

class Entity;

enum FGD_CLASS_TYPES {
	FGD_CLASS_BASE,
	FGD_CLASS_SOLID,
	FGD_CLASS_POINT
};

enum FGD_KEY_TYPES {
	FGD_KEY_INTEGER,
	FGD_KEY_STRING,
	FGD_KEY_CHOICES,
	FGD_KEY_FLAGS,
	FGD_KEY_RGB,
	FGD_KEY_STUDIO,
	FGD_KEY_SOUND,
	FGD_KEY_SPRITE,
	FGD_KEY_TARGET_SRC,
	FGD_KEY_TARGET_DST
};

// for both "choice" and "flags" keyvalue types
struct KeyvalueChoice {
	string name;
	string svalue;
	int ivalue;
	bool isInteger;
};

struct KeyvalueDef {
	string name;
	string valueType;
	int iType;
	string description;
	string defaultValue;
	string fgdSource; // FGD file and class this originates from
	string sourceDesc; // description of where this keyvalue came from (fgd -> class -> key)
	COLOR3 color;
	vector<KeyvalueChoice> choices;
};

class Fgd;

struct FgdClass {
	int classType;
	string name;
	string description;
	vector<KeyvalueDef> keyvalues;
	vector<string> baseClasses;
	string spawnFlagNames[32];
	string model;
	string sprite;
	string iconSprite;
	bool isModel;
	bool isSprite;
	bool isDecal;
	vec3 mins;
	vec3 maxs;
	COLOR3 color;
	map< string, string > otherTypes; // unrecognized types

	// if false, then need to get props from the base class
	bool colorSet;
	bool sizeSet;

	FgdClass() {
		classType = FGD_CLASS_POINT;
		name = "???";
		isSprite = false;
		isModel = false;
		isDecal = false;
		colorSet = false;
		sizeSet = false;

		// default to the purple cube
		mins = vec3(-8, -8, -8);
		maxs = vec3(8, 8, 8);
		color = { 220, 0, 220 };
	}

	// get parent classes from youngest to oldest, in right-to-left order
	// reversing the vector changes order to oldest to youngest, left-to-right order
	void getBaseClasses(Fgd* fgd, vector<FgdClass*>& inheritanceList);

	bool hasKey(string key);
};

struct FgdGroup {
	vector<FgdClass*> classes;
	string groupName;
};

class Fgd {
public:
	string path;
	string name;
	vector<FgdClass*> classes;
	map<string, FgdClass*> classMap;

	vector<FgdGroup> pointEntGroups;
	vector<FgdGroup> solidEntGroups;

	Fgd(string path);
	~Fgd();

	bool parse();
	void merge(Fgd* other);

	FgdClass* getFgdClass(string cname);

private:
	int lineNum;
	string line; // current line being parsed

	void parseClassHeader(FgdClass& outClass);
	void parseKeyvalue(FgdClass& outClass, string fgdName);
	void parseChoicesOrFlags(KeyvalueDef& outKey);

	void processClassInheritance();

	void createEntGroups();
	void setSpawnflagNames();

	// true if value begins a group of strings separated by spaces
	bool stringGroupStarts(string s);

	// true if any closing paren or quote is found
	bool stringGroupEnds(string s);

	// get the value inside a prefixed set of parens
	string getValueInParens(string s);

	// groups strings separated by spaces but enclosed in quotes/parens
	vector<string> groupParts(vector<string>& ungrouped);

	string getValueInQuotes(string s);

	vector<string> splitStringIgnoringQuotes(string s, string delimitter);

	void sortClasses();
};