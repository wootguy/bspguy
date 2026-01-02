#pragma once
#include <vector>
#include "colors.h"
#include <string>
#include <unordered_map>
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
	string defaultValue; // flags only
	string desc; // optional
	int ivalue = 0;
	bool isInteger = false;

	int calcMemoryUsage();
};

struct KeyvalueDef {
	string name;
	string valueType;
	int iType;
	string smartName;
	string description;
	string defaultValue;
	string fgdSource; // FGD file and class this originates from
	string sourceDesc; // description of where this keyvalue came from (fgd -> class -> key)
	COLOR3 color;
	vector<KeyvalueChoice> choices;

	int calcMemoryUsage();
};

class Fgd;

struct FgdClass {
	int classType;
	string name;
	string description; // optional
	string url; // optional J.A.C.K field
	vector<KeyvalueDef> keyvalues;
	vector<string> baseClasses;
	string spawnFlagNames[32];
	string spawnFlagDescs[32];
	string model;
	string sprite;
	string iconSprite;
	bool isModel;
	bool isSprite;
	bool isDecal;
	vec3 mins;
	vec3 maxs;
	COLOR3 color;
	string iconColorKey; // key used to define iconsprite color in the editor
	unordered_map< string, string > otherTypes; // unrecognized types

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

	int calcMemoryUsage();
};

struct FgdGroup {
	vector<FgdClass*> classes;
	string groupName;

	int calcMemoryUsage();
};

class Fgd {
public:
	string path;
	string name;
	vector<FgdClass*> classes;
	unordered_map<string, FgdClass*> classMap;

	int mapSizeMin = 0;
	int mapSizeMax = 0;

	vector<FgdGroup> pointEntGroups;
	vector<FgdGroup> solidEntGroups;

	Fgd(string path);
	~Fgd();

	bool parse();
	void merge(Fgd* other);

	FgdClass* getFgdClass(string cname);

	int calcMemoryUsage();

private:
	char* startFileData;
	char* endFileData;

	void parseClass(char*& readPtr, FgdClass& outClass);
	void parseClassProp(char*& readPtr, FgdClass& outClass);
	void parseKeyvalue(char*& readPtr, FgdClass& outClass);
	void parseChoices(char*& readPtr, FgdClass& outClass, KeyvalueDef& outKey);

	void processClassInheritance();

	void createEntGroups();
	void setSpawnflagNames();

	void sortClasses();

	// read string up until a delimiter is found.
	// return the string and increment readPtr after it
	string readUntil(char*& readPtr, const char* delimit);
	string readUntilNot(char*& readPtr, const char* delimit);
};