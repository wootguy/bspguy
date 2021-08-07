#include "Fgd.h"
#include <set>

std::map<std::string, int> fgdKeyTypes{
	{"integer", FGD_KEY_INTEGER},
	{"choices", FGD_KEY_CHOICES},
	{"flags", FGD_KEY_FLAGS},
	{"color255", FGD_KEY_RGB},
	{"studio", FGD_KEY_STUDIO},
	{"sound", FGD_KEY_SOUND},
	{"sprite", FGD_KEY_SPRITE},
	{"target_source", FGD_KEY_TARGET_SRC},
	{"target_destination", FGD_KEY_TARGET_DST}
};

Fgd::Fgd(std::string path) {
	this->path = path;
	this->name = stripExt(basename(path));
    this->lineNum = 0;
}

Fgd::~Fgd() {
	for (int i = 0; i < classes.size(); i++) {
		delete classes[i];
	}
}

FgdClass* Fgd::getFgdClass(std::string cname) {
	if (classMap.find(cname) == classMap.end()) {
		return NULL;
	}
	return classMap[cname];
}

void Fgd::merge(Fgd* other) {
	for (auto it = other->classMap.begin(); it != other->classMap.end(); ++it) {
		std::string className = it->first;
		FgdClass* fgdClass = it->second;

		if (classMap.find(className) != classMap.end()) {
			logf("Skipping duplicate definition for %s in FGD %s\n", className.c_str(), other->name.c_str());
			continue;
		}

		FgdClass* newClass = new FgdClass();
		*newClass = *fgdClass;

		classes.push_back(newClass);
		classMap[className] = newClass;
	}
}

bool Fgd::parse() {

	if (!fileExists(path)) {
		logf("Missing FGD: %s\n", path.c_str());
		return false;
	}

	logf("Parsing %s\n", path.c_str());

	std::ifstream in(path);

	lineNum = 0;
	int lastBracket = -1;

	FgdClass* fgdClass = new FgdClass();
	int bracketNestLevel = 0;

	line = "";
	while (getline(in, line)) {
		lineNum++;

		// strip comments
		size_t cpos = line.find("//");
		if (cpos != std::string::npos)
			line = line.substr(0, cpos);

		line = trimSpaces(line);

		if (line.empty())
			continue;
		
		if (line[0] == '@') {
			if (bracketNestLevel) {
				logf("ERROR: New FGD class definition starts before previous one ends (line %d)\n", lineNum);
			}

			parseClassHeader(*fgdClass);
		}

		if (line.find('[') != std::string::npos) {
			bracketNestLevel++;
		}
		if (line.find(']') != std::string::npos) {
			bracketNestLevel--;
			if (bracketNestLevel == 0) {
				classes.push_back(fgdClass);
				fgdClass = new FgdClass();
			}
		}

		if (line == "[" || line == "]") {
			continue;
		}

		if (bracketNestLevel == 1) {
			parseKeyvalue(*fgdClass);
		}

		if (bracketNestLevel == 2) {
			if (fgdClass->keyvalues.size() == 0) {
				logf("ERROR: Choice values begin before any keyvalue are defined (line %d)\n", lineNum);
				continue;
			}
			KeyvalueDef& lastKey = fgdClass->keyvalues[fgdClass->keyvalues.size()-1];
			parseChoicesOrFlags(lastKey);
		}
	}

	processClassInheritance();
	createEntGroups();
	setSpawnflagNames();
	return true;
}

void Fgd::parseClassHeader(FgdClass& fgdClass) {
	std::vector<std::string> headerParts = splitString(line, "=");

	// group parts enclosed in parens or quotes
	std::vector<std::string> typeParts = splitString(trimSpaces(headerParts[0]), " ");
	typeParts = groupParts(typeParts);

	std::string classType = toLowerCase(typeParts[0]);

	if (classType == "@baseclass") {
		fgdClass.classType = FGD_CLASS_BASE;
	}
	else if (classType == "@solidclass") {
		fgdClass.classType = FGD_CLASS_SOLID;
	}
	else if (classType == "@pointclass") {
		fgdClass.classType = FGD_CLASS_POINT;
	}
	else {
		logf("ERROR: Unrecognized FGD class type '%s'\n", typeParts[0].c_str());
	}
	
	// parse constructors/properties
	for (int i = 1; i < typeParts.size(); i++) {
		std::string lpart = toLowerCase(typeParts[i]);

		if (lpart.find("base(") == 0) {
			std::vector<std::string> baseClassList = splitString(getValueInParens(typeParts[i]), ",");
			for (int k = 0; k < baseClassList.size(); k++) {
				std::string baseClass = trimSpaces(baseClassList[k]);
				fgdClass.baseClasses.push_back(baseClass);
			}
		}
		else if (lpart.find("size(") == 0) {
			std::vector<std::string> sizeList = splitString(getValueInParens(typeParts[i]), ",");

			if (sizeList.size() == 1) {
				vec3 size = parseVector(sizeList[0]);
				fgdClass.mins = size * -0.5f;
				fgdClass.maxs = size * 0.5f;
			}
			else if (sizeList.size() == 2) {
				fgdClass.mins = parseVector(sizeList[0]);
				fgdClass.maxs = parseVector(sizeList[1]);
			}
			else {
				logf("ERROR: Expected 2 vectors in size() property (line %d)\n", lineNum);
			}

			fgdClass.sizeSet = true;
		}
		else if (lpart.find("color(") == 0) {
			std::vector<std::string> nums = splitString(getValueInParens(typeParts[i]), " ");

			if (nums.size() == 3) {
				fgdClass.color = { (byte)atoi(nums[0].c_str()), (byte)atoi(nums[1].c_str()), (byte)atoi(nums[2].c_str()) };
			}
			else {
				logf("ERROR: Expected 3 components in color() property (line %d)\n", lineNum);
			}

			fgdClass.colorSet = true;
		}
		else if (lpart.find("studio(") == 0) {
			fgdClass.model = getValueInParens(typeParts[i]);
			fgdClass.isModel = true;
		}
		else if (lpart.find("iconsprite(") == 0) {
			fgdClass.iconSprite = getValueInParens(typeParts[i]);
		}
		else if (lpart.find("sprite(") == 0) {
			fgdClass.sprite = getValueInParens(typeParts[i]);
			fgdClass.isSprite = true;
		}
		else if (lpart.find("decal(") == 0) {
			fgdClass.isDecal = true;
		}
		else if (typeParts[i].find("(") != std::string::npos) {
			std::string typeName = typeParts[i].substr(0, typeParts[i].find("("));
			logf("WARNING: Unrecognized type %s (line %d)\n", typeName.c_str(), lineNum);
		}
	}

	if (headerParts.size() == 0) {
		logf("ERROR: Unexpected end of class header (line %d)\n", lineNum);
		return;
	}

	std::vector<std::string> nameParts = splitStringIgnoringQuotes(headerParts[1], ":");

	if (nameParts.size() >= 2) {
		fgdClass.description = getValueInQuotes(nameParts[1]);
	}
	if (nameParts.size() >= 1) {
		fgdClass.name = trimSpaces(nameParts[0]);
		// strips brackets if they're there
		fgdClass.name = fgdClass.name.substr(0, fgdClass.name.find(" "));
	}
}

void Fgd::parseKeyvalue(FgdClass& outClass) {
	std::vector<std::string> keyParts = splitStringIgnoringQuotes(line, ":");

	KeyvalueDef def;

	def.name = keyParts[0].substr(0, keyParts[0].find("("));
	def.valueType = toLowerCase(getValueInParens(keyParts[0]));

	def.iType = FGD_KEY_STRING;
	if (fgdKeyTypes.find(def.valueType) != fgdKeyTypes.end()) {
		def.iType = fgdKeyTypes[def.valueType];
	}

	if (keyParts.size() > 1)
		def.description = getValueInQuotes(keyParts[1]);
	else {
		def.description = def.name;
		
		// capitalize (infodecal)
		if ((def.description[0] > 96) && (def.description[0] < 123)) 
			def.description[0] = def.description[0] - 32;
	}

	if (keyParts.size() > 2) {
		if (keyParts[2].find("=") != std::string::npos) { // choice
			def.defaultValue = trimSpaces(keyParts[2].substr(0, keyParts[2].find("=")));
		}
		else if (keyParts[2].find("\"") != std::string::npos) { // std::string
			def.defaultValue = getValueInQuotes(keyParts[2]);
		}
		else { // integer
			def.defaultValue = trimSpaces(keyParts[2]);
		}
	}

	outClass.keyvalues.push_back(def);

	//logf << "ADD KEY " << def.name << "(" << def.valueType << ") : " << def.description << " : " << def.defaultValue << endl;
}

void Fgd::parseChoicesOrFlags(KeyvalueDef& outKey) {
	std::vector<std::string> keyParts = splitString(line, ":");

	KeyvalueChoice def;

	if (keyParts[0].find("\"") != std::string::npos) {
		def.svalue = getValueInQuotes(keyParts[0]);
		def.ivalue = 0;
		def.isInteger = false;
	}
	else {
		def.svalue = trimSpaces(keyParts[0]);
		def.ivalue = atoi(keyParts[0].c_str());
		def.isInteger = true;
	}
	
	if (keyParts.size() > 1)
		def.name = getValueInQuotes(keyParts[1]);

	outKey.choices.push_back(def);

	//logf << "ADD CHOICE LINE " << lineNum << " = " << def.svalue << " : " << def.name << endl;
}

std::vector<std::string> Fgd::groupParts(std::vector<std::string>& ungrouped) {
	std::vector<std::string> grouped;

	for (int i = 0; i < ungrouped.size(); i++) {

		if (stringGroupStarts(ungrouped[i])) {
			std::string groupedPart = ungrouped[i];
			for (i = i + 1; i < ungrouped.size(); i++) {
				groupedPart += " " + ungrouped[i];
				if (stringGroupEnds(ungrouped[i])) {
					break;
				}
			}
			grouped.push_back(groupedPart);
		}
		else {
			grouped.push_back(ungrouped[i]);
		}
	}

	return grouped;
}

bool Fgd::stringGroupStarts(std::string s) {
	if (s.find("(") != std::string::npos) {
		return s.find(")") == std::string::npos;
	}
	
	size_t startStringPos = s.find("\"");
	if (startStringPos != std::string::npos) {
		size_t endStringPos = s.rfind("\"");
		return endStringPos == startStringPos || endStringPos == std::string::npos;
	}
	
	return false;
}

bool Fgd::stringGroupEnds(std::string s) {
	return s.find(")") != std::string::npos || s.find("\"") != std::string::npos;
}

std::string Fgd::getValueInParens(std::string s) {
	s = s.substr(s.find("(") + 1);
	s = s.substr(0, s.rfind(")"));
	return trimSpaces(s);
}

std::string Fgd::getValueInQuotes(std::string s) {
	s = s.substr(s.find("\"") + 1);
	s = s.substr(0, s.rfind("\""));
	return s;
}

void Fgd::processClassInheritance() {
	for (int i = 0; i < classes.size(); i++) {
		classMap[classes[i]->name] = classes[i];
		//logf("Got class %s\n", classes[i]->name.c_str());
	}

	for (int i = 0; i < classes.size(); i++) {
		if (classes[i]->classType == FGD_CLASS_BASE)
			continue;

		std::vector<FgdClass*> allBaseClasses;
		classes[i]->getBaseClasses(this, allBaseClasses);

		if (allBaseClasses.size() != 0)
		{
			std::vector<KeyvalueDef> newKeyvalues;
			std::vector<KeyvalueChoice> newSpawnflags;
			std::set<std::string> addedKeys;
			std::set<std::string> addedSpawnflags;
			//logf << classes[i]->name << " INHERITS FROM: ";
			for (int k = allBaseClasses.size()-1; k >= 0; k--) {
				if (!classes[i]->colorSet && allBaseClasses[k]->colorSet) {
					classes[i]->color = allBaseClasses[k]->color;
				}
				if (!classes[i]->sizeSet && allBaseClasses[k]->sizeSet) {
					classes[i]->mins = allBaseClasses[k]->mins;
					classes[i]->maxs = allBaseClasses[k]->maxs;
				}
				for (int c = 0; c < allBaseClasses[k]->keyvalues.size(); c++) {
					if (addedKeys.find(allBaseClasses[k]->keyvalues[c].name) == addedKeys.end()) {
						newKeyvalues.push_back(allBaseClasses[k]->keyvalues[c]);
						addedKeys.insert(allBaseClasses[k]->keyvalues[c].name);
					}
					if (allBaseClasses[k]->keyvalues[c].iType == FGD_KEY_FLAGS) {
						for (int f = 0; f < allBaseClasses[k]->keyvalues[c].choices.size(); f++) {
							KeyvalueChoice& spawnflagOption = allBaseClasses[k]->keyvalues[c].choices[f];
							if (addedSpawnflags.find(spawnflagOption.svalue) == addedSpawnflags.end()) {
								newSpawnflags.push_back(spawnflagOption);
								addedSpawnflags.insert(spawnflagOption.svalue);
							}
						}
					}
				}
				//logf << allBaseClasses[k]->name << " ";
			}

			for (int c = 0; c < classes[i]->keyvalues.size(); c++) {
				if (addedKeys.find(classes[i]->keyvalues[c].name) == addedKeys.end()) {
					newKeyvalues.push_back(classes[i]->keyvalues[c]);
					addedKeys.insert(classes[i]->keyvalues[c].name);
				}
				if (classes[i]->keyvalues[c].iType == FGD_KEY_FLAGS) {
					for (int f = 0; f < classes[i]->keyvalues[c].choices.size(); f++) {
						KeyvalueChoice& spawnflagOption = classes[i]->keyvalues[c].choices[f];
						if (addedSpawnflags.find(spawnflagOption.svalue) == addedSpawnflags.end()) {
							newSpawnflags.push_back(spawnflagOption);
							addedSpawnflags.insert(spawnflagOption.svalue);
						}
					}
				}
			}

			classes[i]->keyvalues = newKeyvalues;

			for (int c = 0; c < classes[i]->keyvalues.size(); c++) {
				if (classes[i]->keyvalues[c].iType == FGD_KEY_FLAGS) {
					classes[i]->keyvalues[c].choices = newSpawnflags;
				}
			}
			
			//logf << endl;
		}
		
	}
}

void FgdClass::getBaseClasses(Fgd* fgd, std::vector<FgdClass*>& inheritanceList) {
	for (int i = baseClasses.size()-1; i >= 0; i--) {
		if (fgd->classMap.find(baseClasses[i]) == fgd->classMap.end()) {
			logf("ERROR: Invalid base class %s\n", baseClasses[i].c_str());
			continue;
		}
		inheritanceList.push_back(fgd->classMap[baseClasses[i]]);
		fgd->classMap[baseClasses[i]]->getBaseClasses(fgd, inheritanceList);
	}
}

void Fgd::createEntGroups() {
	std::set<std::string> addedPointGroups;
	std::set<std::string> addedSolidGroups;

	for (int i = 0; i < classes.size(); i++) {
		if (classes[i]->classType == FGD_CLASS_BASE || classes[i]->name == "worldspawn")
			continue;
		std::string cname = classes[i]->name;
		std::string groupName = cname.substr(0, cname.find("_"));

		bool isPointEnt = classes[i]->classType == FGD_CLASS_POINT;

		std::set<std::string>* targetSet = isPointEnt ? &addedPointGroups : &addedSolidGroups;
		std::vector<FgdGroup>* targetGroup = isPointEnt ? &pointEntGroups : &solidEntGroups;

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
						logf("ERROR: Invalid spwanflag value %s\n", choice.svalue.c_str());
						continue;
					}

					int val = choice.ivalue;
					int bit = 0;
					while (val >>= 1) {
						bit++;
					}

					if (bit > 31) {
						logf("ERROR: Invalid spawnflag value %s\n", choice.svalue.c_str());
					}
					else {
						classes[i]->spawnFlagNames[bit] = choice.name;
					}
				}
			}
		}
	}
}

std::vector<std::string> Fgd::splitStringIgnoringQuotes(std::string s, std::string delimitter) {
	std::vector<std::string> split;
	if (s.size() == 0 || delimitter.size() == 0)
		return split;

	size_t delimitLen = delimitter.length();
	while (s.size()) {

		bool foundUnquotedDelimitter = false;
		int searchOffset = 0;
		while (!foundUnquotedDelimitter && searchOffset < s.size()) {
			size_t delimitPos = s.find(delimitter, searchOffset);

			if (delimitPos == std::string::npos) {
				split.push_back(s);
				return split;
			}

			int quoteCount = 0;
			for (int i = 0; i < delimitPos; i++) {
				quoteCount += s[i] == '"';
			}

			if (quoteCount % 2 == 1) {
				searchOffset = delimitPos + 1;
				continue;
			}

			split.push_back(s.substr(0, delimitPos));
			s = s.substr(delimitPos + delimitLen);
			foundUnquotedDelimitter = true;
		}

		if (!foundUnquotedDelimitter) {
			break;
		}
		
	}

	return split;
}
