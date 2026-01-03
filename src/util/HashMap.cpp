#include "util.h"
#include "HashMap.h"
#include <string.h>
#include <unordered_set>
#include <algorithm>

#define HMAP_DEFAULT_STRING_POOL_SZ 64
#define HMAP_MAX_STRING_POOL_SZ (1024*1024*16)
//#define PROFILE_MODE // uncomment to be able to print global hash table stats
#define MAX_PRIME_DOUBLES 29

// how full the map can be before it's resized, should be a low percentage because open addressing
// has problems with clustering (collisions in separate "buckets" merging to create giant chains of
// blocked space)
#define HMAP_MAX_FILL_PERCENT 0.5f

// how empty the map can be before it's resized.
#define HMAP_MIN_SHRINK_PERCENT 0.1f

const StringMap g_emptyStringMap;
std::unordered_set<BaseHashMap*> g_hashMaps;

// series of prime numbers where each value is approximately double the previous.
uint32_t g_primeDoubles[MAX_PRIME_DOUBLES] = {
	13, 29, 53, 101, 199, 389, 769, 1543, 3079, 6151, 12289, 24593, 49157, 98317, 196613,
	393241, 786433, 1572869, 3145739, 6291469, 12582917, 25165843, 50331653, 100663319, 201326611,
	402653189, 805306457, 1610612741, 3221225473
};

#define HMAP_DEFAULT_MAX_ENTRIES g_primeDoubles[0]

void BaseHashMap::global_hash_stats() {
	int totalSz = 0;
	int totalMaps = 0;
	int totalMaxDepth = 0;
	int totalCollisions = 0;
	int worstMaxDepth = 0;

	for (auto item : g_hashMaps) {
		if (item->size() > 128) {
			item->printStats();
			totalSz += item->stringPoolSz + item->maxEntries * item->entrySz;
			totalMaxDepth += item->stats.maxDepth;
			totalCollisions += item->stats.collisions;
			worstMaxDepth = max(worstMaxDepth, (int)item->stats.maxDepth);
			totalMaps++;
		}
	}

	int avgMaxDepth = totalMaps ? totalMaxDepth / totalMaps : 0;
	int avgCollisions = totalMaps ? totalCollisions / totalMaps : 0;
	logf("Worst depth %3d, Avg worst depth %3d, Avg collide %3d, Size %.2f MB\n",
		worstMaxDepth, avgMaxDepth, avgCollisions, (float)totalSz / (1024.0f*1024.0f));
}

BaseHashMap::BaseHashMap(int valueSz) {
	data = NULL;
	stringOffset = 1; // skip 0 for NULL value
	maxEntries = 0;
	stringPoolSz = 0;
	entryCount = 0;
	delCount = 0;
	this->entrySz = valueSz + sizeof(entry_header_t);
	memset(&stats, 0, sizeof(hash_map_stats_t));
}

BaseHashMap::BaseHashMap(int valueSz, int maxEntries, uint32_t stringPoolSz) {
	this->entrySz = valueSz + sizeof(entry_header_t);
	init(maxEntries, stringPoolSz);
}

BaseHashMap::BaseHashMap(const BaseHashMap& other) {
	copyFrom(other);
}

void BaseHashMap::copyFrom(const BaseHashMap& other) {
	memcpy(&stats, &other.stats, sizeof(hash_map_stats_t));
	maxEntries = other.maxEntries;
	stringOffset = other.stringOffset;
	stringPoolSz = other.stringPoolSz;
	entrySz = other.entrySz;
	entryCount = other.entryCount;
	delCount = other.delCount;

	if (other.data) {
		int dataSz = stringPoolSz + maxEntries * entrySz;
		data = new char[dataSz];
		memcpy(data, other.data, dataSz);
	}
	else {
		data = NULL;
	}
}

BaseHashMap& BaseHashMap::operator=(const BaseHashMap& other) {
	if (this != &other) {
		if (data) {
			delete[] data;
			data = NULL;
		}

		copyFrom(other);
	}
	return *this;
}

void BaseHashMap::init(int newMaxEntries, uint32_t newStringPoolSz) {
	stringOffset = 1; // skip 0 for NULL value
	entryCount = 0;
	delCount = 0;
	maxEntries = newMaxEntries;
	stringPoolSz = newStringPoolSz;

	int dataSz = stringPoolSz + maxEntries * entrySz;

	data = new char[dataSz];
	memset(data, 0, dataSz);
	memset(&stats, 0, sizeof(hash_map_stats_t));

#ifdef PROFILE_MODE
	g_hashMaps.insert(this);
#endif
}

void BaseHashMap::clear() {
	if (data) {
		delete[] data;
		data = NULL;
	}
	init(HMAP_DEFAULT_MAX_ENTRIES, HMAP_DEFAULT_STRING_POOL_SZ);
}

// FNV1a algorithm
uint32_t BaseHashMap::hash(const char* str) const {
	uint64 hash = 14695981039346656037ULL;
	uint32_t c;

	while ((c = *str++)) {
		hash = (hash * 1099511628211) ^ c;
	}

	return hash;
}

BaseHashMap::~BaseHashMap() {
	if (data) {
		delete[] data;
		data = NULL;
	}

#ifdef PROFILE_MODE
	g_hashMaps.erase(this);
#endif
}

// Store string in preallocated memory pool (avoiding malloc)
uint32_t BaseHashMap::storeString(const char* str) {
	size_t len = strlen(str) + 1;  // Include null terminator

	if ((size_t)stringOffset + len >= (size_t)stringPoolSz) {
		size_t needSz = stringPoolSz + len;
		size_t newPoolSz = stringPoolSz * 2;
		while (newPoolSz <= needSz && newPoolSz < HMAP_MAX_STRING_POOL_SZ) {
			newPoolSz *= 2;
		}

		if (!resizeStringPool(newPoolSz)) {
			logf("String pool out of memory!\n");
			return 0;  // out of memory
		}
	}

	uint32_t retOffset = stringOffset;
	char* stringPool = data;
	char* stored = stringPool + stringOffset;
	memcpy(stored, str, len);
	stringOffset += len;

	return retOffset;
}

bool BaseHashMap::find(const char* key, uint32_t& index, uint32_t& depth) const {
	uint32_t hashedKey = hash(key);
	index = hashedKey % maxEntries;
	uint32_t startIndex = index;

	// Must be coprime to the table size to be sure all cells are visited.
	// By setting the table size to a prime number, any value less than 
	// the table size is guaranteed to be coprime.
	uint32_t stepSize = 1 + (hashedKey % (maxEntries - 1));

	depth = 0;

	char* stringPool = data;
	entry_header_t* entry = (entry_header_t*)(data + stringPoolSz + index * entrySz);

	while (entry->occupied) {
		if (!strcmp(stringPool + entry->key, key)) {
			return true;
		}

		//index = (index + 1) % maxEntries; // linear probing (good cache efficiency, bad clustering)
		index = (index + stepSize) % maxEntries; // double hash probing (bad cache efficiency, less clustering)

		entry = (entry_header_t*)(data + stringPoolSz + index * entrySz);
		depth += 1;

		if (index == startIndex) {
			logf("HashMap is full. Something will break now.\n");
			return false;
		}
	}

	return false;
}

bool BaseHashMap::put(const char* key, void* value) {
	if (!key) {
		return false;
	}
	// NULL value is allowed because that's what StringSet uses (0 byte values)

	if (!data) {
		init(HMAP_DEFAULT_MAX_ENTRIES, HMAP_DEFAULT_STRING_POOL_SZ);
	}

	uint32_t index, depth;
	int valueSz = entrySz - sizeof(entry_header_t);

	if (find(key, index, depth)) {
		// overwrite existing entry
		if (value) {
			entry_header_t* entry = (entry_header_t*)(data + stringPoolSz + index * entrySz);
			memcpy((char*)entry + sizeof(entry_header_t), value, valueSz);
		}

		return true;
	}

	// add new entry

	uint32_t ikey = storeString(key);
	entry_header_t* entry = (entry_header_t*)(data + stringPoolSz + index * entrySz);
		
	entry->key = ikey;
	entry->occupied = true;
	if (value)
		memcpy((char*)entry + sizeof(entry_header_t), value, valueSz);

	if (!entry->key) {
		entry->occupied = false;
		logf("HashMap failed to insert '%s'\n", key);
		return false;
	}

	entryCount++;
	if (depth > 0) {
		stats.collisions++;
		stats.maxDepth = max((uint32_t)stats.maxDepth, depth);
		stats.totalDepth += depth;
	}

	// resize before there's a problem
	if (entryCount >= reservedSize() * HMAP_MAX_FILL_PERCENT) {	
		int actualKeys = entryCount - delCount;

		// use a prime number for table size to keep the step hash function simple
		uint32_t nextSz = maxEntries;

		if (actualKeys > (int)delCount) {
			for (int i = 0; i < MAX_PRIME_DOUBLES; i++) {
				if (g_primeDoubles[i] > nextSz) {
					nextSz = g_primeDoubles[i];
					break;
				}
			}
		} // else just rehash the table to get rid of the deleted keys

		resizeHashTable(nextSz);
	}

	return true;
}

bool BaseHashMap::putAll(const BaseHashMap& other) {
	if (!other.data) {
		return true;
	}

	if (entrySz != other.entrySz) {
		logf("BaseHashMap putAll called with mismatched map types\n");
		return false;
	}

	putAll_internal(other.data, other.maxEntries, other.stringPoolSz);

	return true;
}

void* BaseHashMap::getValue(const char* key) const {
	if (!data || !key) {
		return NULL;
	}

	uint32_t index, depth;
	if (find(key, index, depth)) {
		return data + stringPoolSz + index*entrySz + sizeof(entry_header_t);
	}

	return NULL;
}

void BaseHashMap::del(const char* key) {
	if (!data || !key) {
		return;
	}

	uint32_t index, depth;
	if (find(key, index, depth)) {
		entry_header_t* entry = (entry_header_t*)(data + stringPoolSz + index*entrySz);
		// can't actually free the node because lookups are aborted when a free node is found.
		//entry->occupied = false;
		//entryCount--;

		entry->key = 0;
		delCount++;

		int actualKeys = entryCount - delCount;
		bool shouldShrink = actualKeys <= reservedSize() * HMAP_MIN_SHRINK_PERCENT;
		bool manyDeleted = (int)delCount > actualKeys;
		if (manyDeleted && shouldShrink && maxEntries > g_primeDoubles[0]) {
			// use a prime number for table size to keep the step hash function simple
			// the next size should hold a little more than double so that it doesn't
			// resize back up immediately
			uint32_t nextSz = actualKeys * (1.0f / HMAP_MAX_FILL_PERCENT)*1.5f;
			for (int i = 0; i < MAX_PRIME_DOUBLES; i++) {
				if (g_primeDoubles[i] > nextSz) {
					nextSz = g_primeDoubles[i];
					break;
				}
			}
			
			resizeHashTable(nextSz);
		}
	}
}

std::vector<std::pair<std::string, std::string>> BaseHashMap::print() {
	char* stringPool = data;

	std::vector<std::pair<std::string, std::string>> ret;

	for (size_t i = 0; i < maxEntries; i++) {
		entry_header_t* entry = (entry_header_t*)(data + stringPoolSz + i * entrySz);

		if (entry->occupied && entry->key) {
			const char* value = getValueString((char*)entry + sizeof(entry_header_t));
			logf("{\"%s\", %s}\n", stringPool + entry->key, value);
			ret.push_back({ stringPool + entry->key, value });
		}
	}

	return ret;
}

const char* StringMap::getValueString(void* value) {
	char* stringPool = data;
	return cstrf("\"%s\"", stringPool + *(uint16_t*)value);
}

int BaseHashMap::size() const {
	return entryCount - delCount;

	/*
	if (!data) {
		return 0;
	}

	int filled = 0;

	for (size_t i = 0; i < maxEntries; i++) {
		entry_header_t* entry = (entry_header_t*)(data + stringPoolSz + i * entrySz);
		filled += entry->occupied ? 1 : 0;
	}

	return filled;
	*/
}

int BaseHashMap::reservedSize() {
	return maxEntries;
}

bool BaseHashMap::resizeStringPool(size_t newPoolSz) {
	newPoolSz = min(HMAP_MAX_STRING_POOL_SZ, (int)newPoolSz);

	if (newPoolSz <= stringPoolSz) {
		logf("StringMap can't make the string pool any larger!\n");
		return false;
	}

	//logf("StringMap resized string pool from %d to %d bytes\n", (int)stringPoolSz, (int)newPoolSz);

	char* stringPool = data;
	char* entries = data + stringPoolSz;

	int newDatSz = newPoolSz + maxEntries * entrySz;
	char* newDat = new char[newDatSz];
	memset(newDat, 0, newDatSz);
	memcpy(newDat, stringPool, stringPoolSz);
	memcpy(newDat + newPoolSz, entries, maxEntries * entrySz);

	delete[] data;
	data = newDat;
	stringPoolSz = newPoolSz;

	return true;
}

bool BaseHashMap::resizeHashTable(size_t newMaxEntries) {
	//logf("Hash table resized from %d to %d entries\n", (int)maxEntries, (int)newMaxEntries);

	char* oldDat = data;
	size_t oldMaxEntries = maxEntries;

	maxEntries = newMaxEntries;
	int dataSz = stringPoolSz + maxEntries * entrySz;
	data = new char[dataSz];
	memset(data, 0, dataSz);
	stringOffset = 1;
	entryCount = 0;
	delCount = 0;

	// reset stats so new growth isn't affected by old size stats
	//uint16_t oldWorst = stats.maxDepth;
	memset(&stats, 0, sizeof(hash_map_stats_t));
	//stats.maxDepth = oldWorst;

	// can't simply memcpy because key hashes will modulo to different slots
	putAll_internal(oldDat, oldMaxEntries, stringPoolSz);

	delete[] oldDat;

	return true;
}

void BaseHashMap::printStats() {
	int dataSz = stringPoolSz + maxEntries * entrySz + sizeof(StringMap);
	float avgDepth = entryCount ? (float)stats.totalDepth / (float)entryCount : 0.0f;

	logf("Collisions: %3d, Max: %3d, Avg: %.1f | Table: %4d / %-4d (%4d + %4d del) | %4.1f KB = %d B tbl + %d B str\n",
		stats.collisions, stats.maxDepth, avgDepth, entryCount, reservedSize(),
		entryCount - delCount, delCount,
		(float)dataSz / 1024.0f, (int)(maxEntries * entrySz), (int)stringPoolSz);
}

int BaseHashMap::calcMemUsage() {
	return sizeof(BaseHashMap) + stringPoolSz + maxEntries * entrySz;
}

StringMap::StringMap(std::initializer_list<std::pair<const char*, const char*>> init) : BaseHashMap(sizeof(hmap_string_t)) {
	for (const auto& pair : init) {
		put(pair.first, pair.second);
	}
}

bool StringMap::put(const char* key, const char* value) {
	if (!data) {
		init(HMAP_DEFAULT_MAX_ENTRIES, HMAP_DEFAULT_STRING_POOL_SZ);
	}

	if (!key || !value) {
		return false;
	}

	uint32_t ival = storeString(value);

	return BaseHashMap::put(key, &ival);
}

void StringMap::putAll_internal(char* otherData, size_t otherEntryCount, size_t otherStringPoolSz) {
	for (size_t i = 0; i < otherEntryCount; i++) {
		entry_header_t* entry = (entry_header_t*)(otherData + otherStringPoolSz + i * entrySz);
		if (!entry->occupied || !entry->key) {
			continue;
		}

		uint32_t offset = *(uint32_t*)((char*)entry + sizeof(entry_header_t));
		if (!StringMap::put(otherData + entry->key, otherData + offset)) {
			logf("StringMap failed to put during table resize\n");
		}
	}
}

const char* StringMap::get(const char* key, const char* defaultVal) const {
	uint32_t* offset = (uint32_t*)getValue(key);
	return offset ? (data + *offset) : defaultVal;
}

bool StringMap::iterate(iterator_t& iter) const {
	char* stringPool = data;

	for (; iter.offset < maxEntries; iter.offset++) {
		entry_header_t* entry = (entry_header_t*)(data + stringPoolSz + iter.offset * entrySz);

		if (entry->occupied && entry->key) {
			iter.key = stringPool + entry->key;
			iter.value = stringPool + *(uint32_t*)((char*)entry + sizeof(entry_header_t));
			iter.offset++;
			return true;
		}
	}

	return false;
}

StringSet::StringSet(std::initializer_list<const char*> init) : BaseHashMap(0) {
	for (const auto& str : init) {
		put(str);
	}
}

bool StringSet::put(const char* key) {
	return BaseHashMap::put(key, NULL);
}

void StringSet::putAll_internal(char* otherData, size_t otherEntryCount, size_t otherStringPoolSz) {
	for (size_t i = 0; i < otherEntryCount; i++) {
		entry_header_t* entry = (entry_header_t*)(otherData + otherStringPoolSz + i * entrySz);
		if (!entry->occupied || !entry->key) {
			continue;
		}

		if (!StringSet::put(otherData + entry->key)) {
			logf("StringSet failed to put during table resize\n");
		}
	}
}

bool StringSet::hasKey(const char* key) const {
	return getValue(key) != NULL;
}

bool StringSet::iterate(iterator_t& iter) const {
	char* stringPool = data;

	for (; iter.offset < maxEntries; iter.offset++) {
		entry_header_t* entry = (entry_header_t*)(data + stringPoolSz + iter.offset * entrySz);

		if (entry->occupied && entry->key) {
			iter.key = stringPool + entry->key;
			iter.offset++;
			return true;
		}
	}

	return false;
}

template <> const char* HashMap<bool>::str(void* value) { return cstrf("%d", (int)*(bool*)value); }
template <> const char* HashMap<char>::str(void* value) { return cstrf("%d", (int)*(char*)value); }
template <> const char* HashMap<short>::str(void* value) { return cstrf("%d", (int)*(short*)value); }
template <> const char* HashMap<int>::str(void* value) { return cstrf("%d", *(int*)value); }
template <> const char* HashMap<long>::str(void* value) { return cstrf("%d", *(long*)value); }
template <> const char* HashMap<uint8_t>::str(void* value) { return cstrf("%u", *(uint8_t*)value); }
template <> const char* HashMap<uint16_t>::str(void* value) { return cstrf("%u", *(uint16_t*)value); }
template <> const char* HashMap<uint32_t>::str(void* value) { return cstrf("%u", *(uint32_t*)value); }
template <> const char* HashMap<unsigned long>::str(void* value) { return cstrf("%u", *(unsigned long*)value); }
template <> const char* HashMap<int64_t>::str(void* value) { return cstrf("%ll", *(int64_t*)value); }
template <> const char* HashMap<uint64_t>::str(void* value) { return cstrf("%llu", *(uint64_t*)value); }
template <> const char* HashMap<float>::str(void* value) { return cstrf("%f", *(float*)value); }
template <> const char* HashMap<double>::str(void* value) { return cstrf("%f", *(double*)value); }
template <> const char* HashMap<long double>::str(void* value) { return cstrf("%Lf", *(long double*)value); }