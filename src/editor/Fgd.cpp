#include "Fgd.h"
#include "util.h"
#include <set>
#include <fstream>
#include "globals.h"
#include <algorithm>

unordered_map<string, int> fgdKeyTypes{
	{"integer", FGD_KEY_INTEGER},
	{"choices", FGD_KEY_CHOICES},
	{"flags", FGD_KEY_FLAGS},
	{"color255", FGD_KEY_RGB},
	{"studio", FGD_KEY_STUDIO},
	{"sound", FGD_KEY_SOUND},
	{"sprite", FGD_KEY_SPRITE},
	{"target_source", FGD_KEY_TARGET_SRC},
	{"target_name_or_class", FGD_KEY_TARGET_SRC},
	{"target_destination", FGD_KEY_TARGET_DST},
	{"target_generic", FGD_KEY_TARGET_DST}, // for not listing missing targets in J.A.C.K
};

const char* whitespace = " \t\n\r";

Fgd::Fgd(string path) {
	this->path = path;
	this->name = stripExt(basename(path));
}

Fgd::~Fgd() {
	for (int i = 0; i < classes.size(); i++) {
		delete classes[i];
	}
}

FgdClass* Fgd::getFgdClass(string cname) {
	auto item = classMap.find(cname);
	if (item == classMap.end()) {
		return NULL;
	}
	return item->second;
}

void Fgd::merge(Fgd* other) {
	for (auto it = other->classMap.begin(); it != other->classMap.end(); ++it) {
		string className = it->first;
		FgdClass* fgdClass = it->second;

		if (classMap.find(className) != classMap.end()) {
			//debugf("Skipping duplicate definition for '%s' (merging %s.fgd into %s.fgd)\n",
			//	className.c_str(), name.c_str(), other->name.c_str());
			continue;
		}

		FgdClass* newClass = new FgdClass();
		*newClass = *fgdClass;

		classes.push_back(newClass);
		classMap[className] = newClass;
	}

	createEntGroups();
	sortClasses();
}

bool Fgd::parse() {

	if (!fileExists(path)) {
		logf("Missing FGD: %s\n", path.c_str());
		return false;
	}

	debugf("Parsing %s\n", path.c_str());

	ifstream in(path);

	int lastBracket = -1;

	FgdClass* fgdClass = new FgdClass();
	int bracketNestLevel = 0;

	string fname = path;
	int lastSlash = path.find_last_of("/\\");
	if (lastSlash != -1) {
		fname = path.substr(lastSlash + 1);
	}
	
	string fullText = "";

	string line = "";
	while (getline(in, line)) {
		fullText += line + "\n";
	}
	fullText += "\n"; // simplifies EOM tests

	const char* whitespace = " \t\n\r";
	int readPos = 0;
	char* fullFileData = new char[fullText.length()+1];
	startFileData = fullFileData;
	endFileData = fullFileData + fullText.length()+1;
	char* readPtr = fullFileData;
	memcpy(fullFileData, &fullText[0], fullText.length());
	fullFileData[fullText.length()] = 0;

	while (true) {
		// skip over comments
		readUntil(readPtr, "(/@");
		if (readPtr >= endFileData) { break; }

		if (readPtr[0] == '/' && readPtr[1] == '/') {
			readUntil(readPtr, "\n");
			continue;
		}

		// name of the definition
		readPtr += 1;
		char* oldReadPtr = readPtr;
		string defName = readUntil(readPtr, " \t\n(/@");
		string defNameLower = toLowerCase(defName);
		if (readPtr >= endFileData) { break; }

		if (defNameLower == "include") {
			string fgdName = trimSpaces(readUntil(readPtr, "(/@"));
			if (readPtr >= endFileData) { break; }

			replaceAll(fgdName, "\"", "");

			string basePath = path.substr(0, path.find_last_of("/\\") + 1) + fgdName;

			if (g_parsed_fgds.count(basePath)) {
				continue;
			}
			g_parsed_fgds.insert(basePath);

			Fgd* tmp = new Fgd(basePath);
			if (tmp->parse()) {
				merge(tmp);
			}

			delete tmp;
		}
		else if (defNameLower == "mapsize") {
			trimSpaces(readUntil(readPtr, "(/@"));
			if (readPtr >= endFileData) { break; }

			if (readPtr[0] != '(') {
				logf("Invalid @MapSize format in %s.fgd\n", name.c_str());
				continue;
			}
			readPtr++;

			string sizeStr = trimSpaces(readUntil(readPtr, ")/@"));
			if (readPtr >= endFileData) { break; }

			if (readPtr[0] != ')') {
				logf("Invalid @MapSize format in %s.fgd\n", name.c_str());
				continue;
			}

			vector<string> parts = splitString(sizeStr, ",");
			if (parts.size() != 2) {
				logf("Invalid @MapSize format '%s' in %s.fgd\n", sizeStr.c_str(), name.c_str());
				continue;
			}

			mapSizeMin = atoi(parts[0].c_str());
			mapSizeMax = atoi(parts[1].c_str());
		}
		else if (defNameLower == "baseclass" || defNameLower == "solidclass" || defNameLower == "pointclass") {
			readPtr = oldReadPtr;

			FgdClass* outClass = new FgdClass();
			parseClass(readPtr, *outClass);

			for (KeyvalueDef& def : outClass->keyvalues) {
				if (def.iType == FGD_KEY_RGB) {
					outClass->iconColorKey = def.name;
					//logf("icon color key for %s is %s\n", outClass->name.c_str(), def.name.c_str());
					break;
				}
			}

			if (outClass->name.length()) {
				classes.push_back(outClass);
				classMap[outClass->name] = outClass;
			}
		}
		else {
			logf("WARNING: Unrecognized definition type %s (%s.fgd pos %d)\n",
				defName.c_str(), name.c_str(), readPtr - startFileData);
		}
	}

	delete[] fullFileData;

	processClassInheritance();
	createEntGroups();
	setSpawnflagNames();
	sortClasses();
	return true;
}

string Fgd::readUntil(char*& readPtr, const char* delimit) {
	if (readPtr >= endFileData) return "";
	int spanEnd = strcspn(readPtr, delimit);
	string span = std::string(readPtr, spanEnd);
	readPtr += spanEnd;
	return span;
}

string Fgd::readUntilNot(char*& readPtr, const char* delimit) {
	if (readPtr >= endFileData) return "";
	int spanEnd = strspn(readPtr, delimit);
	string span = std::string(readPtr, spanEnd);
	readPtr += spanEnd;
	return span;
}

void Fgd::parseClass(char*& readPtr, FgdClass& outClass) {
	// example class with no keys:
	// @PointClass base(Targetname, Angles, Origin) studio("path/model.mdl") = 
	//		example_entity_name : "example entity description, visible in Hammers 'help' Box. []
	
	// class type
	string ctype = toLowerCase(readUntil(readPtr, whitespace));
	if (readPtr >= endFileData) { return; }

	if (ctype == "pointclass") {
		outClass.classType = FGD_CLASS_POINT;
	}
	else if (ctype == "solidclass") {
		outClass.classType = FGD_CLASS_SOLID;
	}
	else if (ctype == "baseclass") {
		outClass.classType = FGD_CLASS_BASE;
	}
	else {
		logf("WARNING: Unrecognized class definition type %s (%s.fgd pos %d)\n",
			ctype.c_str(), name.c_str(), readPtr - startFileData);
	}

	// class props
	while (true) {
		readUntilNot(readPtr, whitespace);
		if (readPtr >= endFileData) { return; }

		if (readPtr[0] == '=') {
			break; // end of props
		}

		parseClassProp(readPtr, outClass);
		if (readPtr >= endFileData) { return; }
	}

	// class name
	readPtr += 1; // skip =
	if (readPtr >= endFileData) { return; }

	outClass.name = trimSpaces(readUntil(readPtr, "[:/"));
	if (readPtr >= endFileData) { return; }

	// skip over comments
	if (readPtr[0] == '/' && readPtr[1] == '/') {
		readUntil(readPtr, "\n");
		readPtr++;
		if (readPtr >= endFileData) { return; }
	}

	// optional description, url, and any future fields
	int fieldCount = 0;
	while (true) {
		if (readPtr[0] == '[') {
			// end of optional fields
			readPtr += 1; // skip [
			if (readPtr >= endFileData) { return; }
			break;
		}

		readPtr += 1; // skip :
		if (readPtr >= endFileData) { return; }

		string optionalField = trimSpaces(readUntil(readPtr, "[:\"/"));

		// skip over comments
		if (readPtr[0] == '/' && readPtr[1] == '/') {
			readUntil(readPtr, "\n");
			readPtr++;
			if (readPtr >= endFileData) { return; }
			continue;
		}
		// skip special chars in quoted values
		if (readPtr[0] == '\"') {
			readPtr++;
			if (readPtr >= endFileData) { return; }
			optionalField = trimSpaces(readUntil(readPtr, "\""));
			if (readPtr >= endFileData) { return; }
			readUntil(readPtr, "[:");
		}
		
		if (fieldCount == 0) {
			// description field
			replaceAll(optionalField, "\"", "");
			outClass.description = optionalField;
		}
		else if (fieldCount == 1) {
			// url field (J.A.C.K. only)
			replaceAll(optionalField, "\"", "");
			outClass.url = optionalField;
		}
		else {
			logf("WARNING: Unrecognized optional class field %s (%s.fgd pos %d)\n",
				optionalField.c_str(), name.c_str(), readPtr - startFileData);
		}

		fieldCount++;
	}

	// keyvalues
	while (true) {
		readUntilNot(readPtr, whitespace);
		if (readPtr >= endFileData) { return; }

		if (readPtr[0] == ']') {
			// end of class
			readPtr += 1; // skip ]
			if (readPtr >= endFileData) { return; }
			return;
		}

		// skip over comments
		if (readPtr[0] == '/' && readPtr[1] == '/') {
			readUntil(readPtr, "\n");
			readPtr++;
			continue;
		}

		parseKeyvalue(readPtr, outClass);
		if (readPtr >= endFileData) { return; }
	}
}

void Fgd::parseClassProp(char*& readPtr, FgdClass& outClass) {
	// example props:
	// base(Targetname, Angles, Origin) studio("path/model.mdl")

	string propName = trimSpaces(readUntil(readPtr, "("));
	readPtr++;
	string propContent = trimSpaces(readUntil(readPtr, "\")"));

	// skip special chars in quoted values
	while (readPtr[0] == '\"') {
		readPtr++;
		if (readPtr >= endFileData) { return; }
		propContent += trimSpaces(readUntil(readPtr, "\""));
		readPtr++;
		if (readPtr >= endFileData) { return; }
		readUntil(readPtr, "\")");
	}

	string lowerProp = toLowerCase(propName);
	readPtr++;

	if (lowerProp == "base") {
		vector<string> baseClassList = splitString(propContent, ",");
		for (int k = 0; k < baseClassList.size(); k++) {
			string baseClass = trimSpaces(baseClassList[k]);
			outClass.baseClasses.push_back(baseClass);
		}
	}
	else if (lowerProp == "size") {
		vector<string> sizeList = splitString(propContent, ",");

		if (sizeList.size() == 1) {
			vec3 size = parseVector(sizeList[0]);
			outClass.mins = size * -0.5f;
			outClass.maxs = size * 0.5f;
		}
		else if (sizeList.size() == 2) {
			outClass.mins = parseVector(sizeList[0]);
			outClass.maxs = parseVector(sizeList[1]);
		}
		else {
			errorf("ERROR: Expected 2 vectors in size() property (%s.fgd pos %d)\n", name.c_str(), readPtr - startFileData);
		}

		outClass.sizeSet = true;
	}
	else if (lowerProp == "color") {
		vector<string> nums = splitString(propContent, " ");

		if (nums.size() == 3) {
			outClass.color = { (byte)atoi(nums[0].c_str()), (byte)atoi(nums[1].c_str()), (byte)atoi(nums[2].c_str()) };
		}
		else {
			errorf("ERROR: Expected 3 components in color() property (%s.fgd pos %d)\n", name.c_str(), readPtr - startFileData);
		}

		outClass.colorSet = true;
	}
	else if (lowerProp == "studio") {
		replaceAll(propContent, "\"", "");
		outClass.model = propContent;
		outClass.isModel = true;
	}
	else if (lowerProp == "iconsprite") {
		replaceAll(propContent, "\"", "");
		outClass.iconSprite = propContent;
	}
	else if (lowerProp == "sprite") {
		replaceAll(propContent, "\"", "");
		outClass.sprite = propContent;
		outClass.isSprite = true;
	}
	else if (lowerProp == "decal") {
		outClass.isDecal = true;
	}
	else {
		debugf("Unrecognized class prop '%s' (%s.fgd pos %d)\n",
			propName.c_str(), name.c_str(), readPtr - startFileData);
	}
}

void Fgd::parseKeyvalue(char*& readPtr, FgdClass& outClass) {
	// FGD is supposed to be whitespace agnostic but I don't see any way to delimit keyvalues here.
	// There can be any number of optional fields separated by ":". Maybe I want to add "asdf(integer)"
	// to one of my fields and ruing everything? I'm gonna have to go with newlines
	//
	// health(integer)
	// health(integer) readonly
	// health(integer) : "Strength"
	// health(integer) : "Strength" : 1 : "Number of points of damage to take before breaking. 0 means don't break."
	// health(integer) : "Strength" : : "Number of points of damage to take before breaking. 0 means don't break."
	// health(integer) readonly : "Strength" : : "This keyvalue is not editable."

	KeyvalueDef def;

	def.name = trimSpaces(readUntil(readPtr, "(]/"));
	if (readPtr >= endFileData) { return; }

	if (readPtr == "]") {
		// end of class
		return;
	}

	def.color = outClass.color;

	readPtr++;
	def.valueType = trimSpaces(readUntil(readPtr, ")]"));
	if (readPtr >= endFileData) { return; }
	if (readPtr == "]") {
		// end of class
		return;
	}
	readPtr++;

	def.iType = FGD_KEY_STRING;
	auto idef = fgdKeyTypes.find(toLowerCase(def.valueType));
	if (idef != fgdKeyTypes.end()) {
		def.iType = idef->second;
	}

	def.fgdSource = outClass.name;
	def.sourceDesc = std::string(name.c_str()) + " --> " + std::string(outClass.name.c_str())
		+ " --> " + std::string(def.name.c_str());

	int fieldCount = 0;
	while (true) {
		string field = trimSpaces(readUntil(readPtr, "\":\n=]/"));
		if (readPtr >= endFileData) { return; }

		// skip special chars in quoted values
		if (readPtr[0] == '\"') {
			readPtr++;
			if (readPtr >= endFileData) { return; }
			field = trimSpaces(readUntil(readPtr, "\""));
			if (readPtr >= endFileData) { return; }
			readUntil(readPtr, ":\n=]/");
		}
		// skip over comments
		if (readPtr[0] == '/' && readPtr[1] == '/') {
			readUntil(readPtr, "\n");
			if (readPtr >= endFileData) { return; }
		}

		if (readPtr[0] == ']') {
			// end of class
			outClass.keyvalues.push_back(def);
			return;
		}
		if (readPtr[0] == '=') {
			// start of choices or flags
			readPtr++;
			parseChoices(readPtr, outClass, def);
			outClass.keyvalues.push_back(def);
			return;
		}
		if (readPtr[0] == ':' || readPtr[0] == '\n') {
			// optional field
			if (fieldCount == 0) {
				// modifiers after key name and type
			}
			else if (fieldCount == 1) {
				def.smartName = field;
			}
			else if (fieldCount == 2) {
				def.defaultValue = field;
			}
			else if (fieldCount == 3) {
				def.description = field;
			}
			else {
				logf("WARNING: Unrecognized keyvalue prop '%s' in class %s (%s.fgd pos %d)\n",
					field.c_str(), outClass.name.c_str(), name.c_str(), readPtr - startFileData);
			}

			if (readPtr[0] == '\n') {
				// end of keyvalue
				outClass.keyvalues.push_back(def);
				return;
			}

			readPtr++;
			fieldCount++;
		}
	}
}

void Fgd::parseChoices(char*& readPtr, FgdClass& outClass, KeyvalueDef& outKey) {
	// Again, I'm not seeing a way delimit options here. Maybe I want to add "0:"" to one of my fields.
	// Going with newlines. The 3rd field is an optional description for J.A.C.K.
	// 
	// [
	//		0 : "something" : "This is something"
	//		1 : "something else (default)" : "This is something else"
	//		2 : "something completely different" : "This is something completely different"
	// ]

	readUntil(readPtr, "[");
	readPtr++;
	if (readPtr >= endFileData) { return; }

	bool isflags = outKey.iType == FGD_KEY_FLAGS;

	KeyvalueChoice choice;
	int fieldCount = 0;
	while (true) {
		string field = trimSpaces(readUntil(readPtr, "\":\n]/"));
		if (readPtr >= endFileData) { return; }

		// skip over comments
		if (readPtr[0] == '/' && readPtr[1] == '/') {
			readUntil(readPtr, "\n");
			if (readPtr >= endFileData) { return; }
		}
		// skip special chars in quoted values
		bool isString = false;
		if (readPtr[0] == '\"') {
			readPtr++;
			field = trimSpaces(readUntil(readPtr, "\""));
			if (readPtr >= endFileData) { return; }
			readUntil(readPtr, ":\n]");
			isString = true;
			if (readPtr >= endFileData) { return; }
		}

		if (readPtr[0] == ']') {
			// end of choices
			if (fieldCount > 0 || choice.svalue.length()) {
				outKey.choices.push_back(choice);
			}
			readPtr++;
			return;
		}
		else if (readPtr[0] == ':' || readPtr[0] == '\n') {
			// end of field
			if (fieldCount == 0) {
				choice.svalue = field;
				if (!isString) {
					choice.isInteger = true;
					choice.ivalue = atoi(field.c_str());
				}
			}
			else if (fieldCount == 1) {
				choice.name = field;
			}
			else if (fieldCount == 2) {
				if (isflags) {
					choice.defaultValue = field;
				}
				else {
					choice.desc = field;
				}
			}
			else {
				if (isflags && fieldCount == 3) {
					choice.desc = field;
				}
				else {
					logf("WARNING: Unrecognized keyvalue choice prop '%s' in class %s (%s.fgd pos %d)\n",
						field.c_str(), outClass.name.c_str(), name.c_str(), readPtr - startFileData);
				}
			}

			fieldCount++;

			if (readPtr[0] == '\n') {
				// end of option
				if (fieldCount > 1 || (fieldCount == 1 && choice.svalue.length())) {
					outKey.choices.push_back(choice);
				}
				choice = KeyvalueChoice();
				fieldCount = 0;
			}

			readPtr++;
		}
	}
}

void Fgd::processClassInheritance() {
	for (int i = 0; i < classes.size(); i++) {
		classMap[classes[i]->name] = classes[i];
		//logf("Got class %s\n", classes[i]->name.c_str());
	}

	for (int i = 0; i < classes.size(); i++) {
		if (classes[i]->classType == FGD_CLASS_BASE)
			continue;

		vector<FgdClass*> allBaseClasses;
		classes[i]->getBaseClasses(this, allBaseClasses);

		if (allBaseClasses.size() != 0)
		{
			vector<KeyvalueDef> childKeyvalues;
			vector<KeyvalueDef> baseKeyvalues;
			vector<KeyvalueChoice> newSpawnflags;
			set<string> addedKeys;
			set<string> addedSpawnflags;

			//debugf("%s INHERITS FROM:\n", classes[i]->name.c_str());

			// add in fields from the child class
			for (int c = 0; c < classes[i]->keyvalues.size(); c++) {
				if (!addedKeys.count(classes[i]->keyvalues[c].name)) {
					childKeyvalues.push_back(classes[i]->keyvalues[c]);
					addedKeys.insert(classes[i]->keyvalues[c].name);
				}
				if (classes[i]->keyvalues[c].iType == FGD_KEY_FLAGS) {
					for (int f = 0; f < classes[i]->keyvalues[c].choices.size(); f++) {
						KeyvalueChoice& spawnflagOption = classes[i]->keyvalues[c].choices[f];
						newSpawnflags.push_back(spawnflagOption);
						addedSpawnflags.insert(spawnflagOption.svalue);
					}
				}
			}

			// add fields from base classes if they don't override child class keys
			for (int k = allBaseClasses.size()-1; k >= 0; k--) {
				if (!classes[i]->colorSet && allBaseClasses[k]->colorSet) {
					classes[i]->color = allBaseClasses[k]->color;
				}
				if (!classes[i]->sizeSet && allBaseClasses[k]->sizeSet) {
					classes[i]->mins = allBaseClasses[k]->mins;
					classes[i]->maxs = allBaseClasses[k]->maxs;
				}
				if (classes[i]->iconColorKey.empty()) {
					classes[i]->iconColorKey = allBaseClasses[k]->iconColorKey;
				}
				for (int c = 0; c < allBaseClasses[k]->keyvalues.size(); c++) {
					if (!addedKeys.count(allBaseClasses[k]->keyvalues[c].name)) {
						baseKeyvalues.push_back(allBaseClasses[k]->keyvalues[c]);
						addedKeys.insert(allBaseClasses[k]->keyvalues[c].name);
					}
					if (allBaseClasses[k]->keyvalues[c].iType == FGD_KEY_FLAGS) {
						for (int f = 0; f < allBaseClasses[k]->keyvalues[c].choices.size(); f++) {
							KeyvalueChoice& spawnflagOption = allBaseClasses[k]->keyvalues[c].choices[f];
							if (!addedSpawnflags.count(spawnflagOption.svalue)) {
								newSpawnflags.push_back(spawnflagOption);
								addedSpawnflags.insert(spawnflagOption.svalue);
							}
						}
					}
				}
				//debugf("  %s\n", allBaseClasses[k]->name.c_str());
			}

			// base keyvalues are usually important things like "targetname" and should come first
			vector<KeyvalueDef> newKeyvalues;
			for (int i = 0; i < baseKeyvalues.size(); i++)
				newKeyvalues.push_back(baseKeyvalues[i]);
			for (int i = 0; i < childKeyvalues.size(); i++)
				newKeyvalues.push_back(childKeyvalues[i]);

			classes[i]->keyvalues = newKeyvalues;

			for (int c = 0; c < classes[i]->keyvalues.size(); c++) {
				if (classes[i]->keyvalues[c].iType == FGD_KEY_FLAGS) {
					classes[i]->keyvalues[c].choices = newSpawnflags;
				}
			}
		}
		
	}
}

void FgdClass::getBaseClasses(Fgd* fgd, vector<FgdClass*>& inheritanceList) {
	for (int i = baseClasses.size()-1; i >= 0; i--) {
		if (fgd->classMap.find(baseClasses[i]) == fgd->classMap.end()) {
			errorf("ERROR: Invalid FGD base class '%s' for %s.fgd\n", baseClasses[i].c_str(), fgd->name.c_str());
			continue;
		}
		inheritanceList.push_back(fgd->classMap[baseClasses[i]]);
		fgd->classMap[baseClasses[i]]->getBaseClasses(fgd, inheritanceList);
	}
}

bool FgdClass::hasKey(string key) {
	for (KeyvalueDef& def : keyvalues) {
		if (def.name == key) {
			return true;
		}
	}
	return false;
}

void Fgd::createEntGroups() {
	set<string> addedPointGroups;
	set<string> addedSolidGroups;

	pointEntGroups.clear();
	solidEntGroups.clear();

	for (int i = 0; i < classes.size(); i++) {
		if (classes[i]->classType == FGD_CLASS_BASE || classes[i]->name == "worldspawn")
			continue;
		string cname = classes[i]->name;
		string groupName = cname.substr(0, cname.find("_"));

		bool isPointEnt = classes[i]->classType == FGD_CLASS_POINT;

		set<string>* targetSet = isPointEnt ? &addedPointGroups : &addedSolidGroups;
		vector<FgdGroup>* targetGroup = isPointEnt ? &pointEntGroups : &solidEntGroups;

		if (targetSet->find(groupName) == targetSet->end()) {
			FgdGroup newGroup;
			newGroup.groupName = groupName;

			targetGroup->push_back(newGroup);
			targetSet->insert(groupName);
		}

		for (int k = 0; k < targetGroup->size(); k++) {
			if (targetGroup->at(k).groupName == groupName) {
				targetGroup->at(k).classes.push_back(classes[i]);
				break;
			}
		}
	}

	FgdGroup otherPointEnts;
	otherPointEnts.groupName = "other";
	for (int i = 0; i < pointEntGroups.size(); i++) {
		if (pointEntGroups[i].classes.size() == 1) {
			otherPointEnts.classes.push_back(pointEntGroups[i].classes[0]);
			pointEntGroups.erase(pointEntGroups.begin() + i);
			i--;
		}
	}
	pointEntGroups.push_back(otherPointEnts);

	FgdGroup otherSolidEnts;
	otherSolidEnts.groupName = "other";
	for (int i = 0; i < solidEntGroups.size(); i++) {
		if (solidEntGroups[i].classes.size() == 1) {
			otherSolidEnts.classes.push_back(solidEntGroups[i].classes[0]);
			solidEntGroups.erase(solidEntGroups.begin() + i);
			i--;
		}
	}
	solidEntGroups.push_back(otherSolidEnts);
}

void Fgd::setSpawnflagNames() {
	for (int i = 0; i < classes.size(); i++) {
		if (classes[i]->classType == FGD_CLASS_BASE)
			continue;

		for (int k = 0; k < classes[i]->keyvalues.size(); k++) {
			if (classes[i]->keyvalues[k].name == "spawnflags") {
				for (int c = 0; c < classes[i]->keyvalues[k].choices.size(); c++) {
					KeyvalueChoice& choice = classes[i]->keyvalues[k].choices[c];

					if (!choice.isInteger) {
						errorf("ERROR: Invalid FGD spawnflag value %s (%s.fgd)\n", choice.svalue.c_str(), name.c_str());
						continue;
					}

					int val = choice.ivalue;
					int bit = 0;
					while (val >>= 1) {
						bit++;
					}

					if (bit > 31) {
						errorf("ERROR: Invalid FGD spawnflag value %s (%s.fgd)\n", choice.svalue.c_str(), name.c_str());
					}
					else {
						classes[i]->spawnFlagNames[bit] = choice.name;
						classes[i]->spawnFlagDescs[bit] = choice.desc;
					}
				}
			}
		}
	}
}

bool sortFgdClasses(FgdClass* a, FgdClass* b) { return a->name < b->name; }
bool sortFgdGroups(const FgdGroup& a, const FgdGroup& b) { return a.groupName < b.groupName; }

void Fgd::sortClasses() {
	for (int i = 0; i < solidEntGroups.size(); i++) {
		std::sort(solidEntGroups[i].classes.begin(), solidEntGroups[i].classes.end(), sortFgdClasses);
	}
	for (int i = 0; i < pointEntGroups.size(); i++) {
		std::sort(pointEntGroups[i].classes.begin(), pointEntGroups[i].classes.end(), sortFgdClasses);
	}

	std::sort(pointEntGroups.begin(), pointEntGroups.end(), sortFgdGroups);
	std::sort(solidEntGroups.begin(), solidEntGroups.end(), sortFgdGroups);
}

int KeyvalueChoice::calcMemoryUsage() {
	int bytes = sizeof(KeyvalueChoice);
	bytes += name.size() + svalue.size() + defaultValue.size() + desc.size();
	return bytes;
}

int KeyvalueDef::calcMemoryUsage() {
	int bytes = sizeof(KeyvalueDef);
	bytes += name.size() + valueType.size() + smartName.size() + description.size() + defaultValue.size()
		+ fgdSource.size() + sourceDesc.size();
	for (KeyvalueChoice& choice : choices) {
		bytes += choice.calcMemoryUsage();
	}
	return bytes;
}

int FgdClass::calcMemoryUsage() {
	int bytes = sizeof(FgdClass);
	bytes += name.size() + description.size() + url.size() + model.size() + sprite.size()
		+ iconSprite.size() + iconColorKey.size();

	for (int i = 0; i < 32; i++) {
		bytes += spawnFlagDescs[i].size();
		bytes += spawnFlagNames[i].size();
	}
	for (const string& s : baseClasses) {
		bytes += s.size();
	}
	for (auto item : otherTypes) {
		bytes += item.first.size() + item.second.size();
	}
	for (KeyvalueDef& def : keyvalues) {
		bytes += def.calcMemoryUsage();
	}

	return bytes;
}

int FgdGroup::calcMemoryUsage() {
	return sizeof(FgdGroup) + groupName.size() + classes.size() * sizeof(FgdClass*);
}

int Fgd::calcMemoryUsage() {
	int bytes = sizeof(Fgd);
	bytes += path.size() + name.size();

	for (FgdClass* clazz : classes) {
		bytes += clazz->calcMemoryUsage();
	}

	for (auto item : classMap) {
		bytes += item.first.size() + sizeof(FgdClass*);
	}

	for (FgdGroup& group : pointEntGroups) {
		bytes += group.calcMemoryUsage();
	}

	for (FgdGroup& group : solidEntGroups) {
		bytes += group.calcMemoryUsage();
	}

	return bytes;
}