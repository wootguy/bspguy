#pragma once
#include "util.h"
#include "Wad.h"
#include "Entity.h"

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
	std::string name;
	std::string svalue;
	int ivalue;
	bool isInteger;
};

struct KeyvalueDef {
	std::string name;
	std::string valueType;
	int iType;
	std::string description;
	std::string defaultValue;
	std::vector<KeyvalueChoice> choices;
};

class Fgd;

struct FgdClass {
	int classType;
	std::string name;
	std::string description;
	std::vector<KeyvalueDef> keyvalues;
	std::vector<std::string> baseClasses;
	std::string spawnFlagNames[32];
	std::string model;
	std::string sprite;
	std::string iconSprite;
	bool isModel;
	bool isSprite;
	bool isDecal;
	vec3 mins;
	vec3 maxs;
	COLOR3 color;
	hashmap otherTypes; // unrecognized types

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
	// reversing the std::vector changes order to oldest to youngest, left-to-right order
	void getBaseClasses(Fgd* fgd, std::vector<FgdClass*>& inheritanceList);
};

struct FgdGroup {
	std::vector<FgdClass*> classes;
	std::string groupName;
};

class Fgd {
public:
	std::string path;
	std::string name;
	std::vector<FgdClass*> classes;
	std::map<std::string, FgdClass*> classMap;

	std::vector<FgdGroup> pointEntGroups;
	std::vector<FgdGroup> solidEntGroups;

	Fgd(std::string path);
	~Fgd();

	bool parse();
	void merge(Fgd* other);

	FgdClass* getFgdClass(std::string cname);

private:
	int lineNum;
	std::string line; // current line being parsed

	void parseClassHeader(FgdClass& fgdClass);
	void parseKeyvalue(FgdClass& outClass);
	void parseChoicesOrFlags(KeyvalueDef& outKey);

	void processClassInheritance();

	void createEntGroups();
	void setSpawnflagNames();

	// true if value begins a group of strings separated by spaces
	bool stringGroupStarts(std::string s);

	// true if any closing paren or quote is found
	bool stringGroupEnds(std::string s);

	// get the value inside a prefixed set of parens
	std::string getValueInParens(std::string s);

	// groups strings separated by spaces but enclosed in quotes/parens
	std::vector<std::string> groupParts(std::vector<std::string>& ungrouped);

	std::string getValueInQuotes(std::string s);
};