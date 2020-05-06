#include "BspMerger.h"
#include <algorithm>

BspMerger::BspMerger() {

}

Bsp* BspMerger::merge(vector<Bsp*> maps, vec3 gap) {
	vector<vector<vector<MAPBLOCK>>> blocks = separate(maps, gap);

	printf("Arranging maps so that they don't overlap:\n");

	for (int z = 0; z < blocks.size(); z++) {
		for (int y = 0; y < blocks[z].size(); y++) {
			for (int x = 0; x < blocks[z][y].size(); x++) {
				MAPBLOCK& block = blocks[z][y][x];

				if (block.offset.x != 0 || block.offset.y != 0 || block.offset.z != 0) {
					printf("    Move %s by (%.0f, %.0f, %.0f)", block.map->name.c_str(), 
						block.offset.x, block.offset.y, block.offset.z);
					block.map->move(block.offset);
				}
			}
		}
	}

	// Merge order matters. 
	// The bounding box of a merged map is expanded to contain both maps, and bounding boxes cannot overlap.
	// TODO: Don't merge linearly. Merge gradually bigger chunks to minimize BSP tree depth.
	//       Not worth it until more than 27 maps are merged together (merge cube bigger than 3x3x3)

	printf("Merging %d maps:\n", maps.size());

	// merge maps along X axis to form rows of maps
	int rowId = 0;
	int mergeCount = 0;
	for (int z = 0; z < blocks.size(); z++) {
		for (int y = 0; y < blocks[z].size(); y++) {
			MAPBLOCK& rowStart = blocks[z][y][0];
			for (int x = 0; x < blocks[z][y].size(); x++) {
				MAPBLOCK& block = blocks[z][y][x];

				if (x != 0) {
					//printf("Merge %d,%d,%d -> %d,%d,%d\n", x, y, z, 0, y, z);
					merge(rowStart, block, "row_" + to_string(rowId));
				}
			}
			rowId++;
		}
	}

	// merge the rows along the Y axis to form layers of maps
	int colId = 0;
	for (int z = 0; z < blocks.size(); z++) {
		MAPBLOCK& colStart = blocks[z][0][0];
		for (int y = 0; y < blocks[z].size(); y++) {
			MAPBLOCK& block = blocks[z][y][0];

			if (y != 0) {
				//printf("Merge %d,%d,%d -> %d,%d,%d\n", 0, y, z, 0, 0, z);
				merge(colStart, block, "layer_" + to_string(colId));
			}
		}
		colId++;
	}

	// merge the layers to form a cube of maps
	MAPBLOCK& layerStart = blocks[0][0][0];
	for (int z = 0; z < blocks.size(); z++) {
		MAPBLOCK& block = blocks[z][0][0];

		if (z != 0) {
			//printf("Merge %d,%d,%d -> %d,%d,%d\n", 0, 0, z, 0, 0, 0);
			merge(layerStart, block, "result");
		}
	}

	return layerStart.map;
}

void BspMerger::merge(MAPBLOCK& dst, MAPBLOCK& src, string resultType) {
	string thisName = dst.merge_name.size() ? dst.merge_name : dst.map->name;
	string otherName = src.merge_name.size() ? src.merge_name : src.map->name;
	dst.merge_name = resultType;
	printf("    %-8s = %s + %s\n", dst.merge_name.c_str(), thisName.c_str(), otherName.c_str());

	dst.map->merge(*src.map);
}

vector<vector<vector<MAPBLOCK>>> BspMerger::separate(vector<Bsp*>& maps, vec3 gap) {
	vector<MAPBLOCK> blocks;

	vector<vector<vector<MAPBLOCK>>> orderedBlocks;

	vec3 maxDims = vec3(0, 0, 0);
	for (int i = 0; i < maps.size(); i++) {
		MAPBLOCK block;
		maps[i]->get_bounding_box(block.mins, block.maxs);

		block.size = block.maxs - block.mins;
		block.offset = vec3(0, 0, 0);
		block.map = maps[i];
	

		if (block.size.x > maxDims.x) {
			maxDims.x = block.size.x;
		}
		if (block.size.y > maxDims.y) {
			maxDims.y = block.size.y;
		}
		if (block.size.z > maxDims.z) {
			maxDims.z = block.size.z;
		}

		blocks.push_back(block);
	}

	bool noOverlap = true;
	for (int i = 0; i < blocks.size() && noOverlap; i++) {
		for (int k = i + i; k < blocks.size(); k++) {
			if (blocks[i].intersects(blocks[k])) {
				noOverlap = false;
				break;
			}
		}
	}

	if (noOverlap) {
		printf("Maps do not overlap. They will be merged without moving.");
		return orderedBlocks;
	}

	maxDims += gap;

	int maxMapsPerRow = (MAX_MAP_COORD * 2.0f) / maxDims.x;
	int maxMapsPerCol = (MAX_MAP_COORD * 2.0f) / maxDims.y;
	int maxMapsPerLayer = (MAX_MAP_COORD * 2.0f) / maxDims.z;

	int idealMapsPerAxis = floor(pow(maps.size(), 1 / 3.0f));
	
	if (idealMapsPerAxis * idealMapsPerAxis * idealMapsPerAxis < maps.size()) {
		idealMapsPerAxis++;
	}

	if (maxMapsPerRow * maxMapsPerCol * maxMapsPerLayer < maps.size()) {
		printf("Not enough space to merge all maps! Try moving them individually before merging.");
		return orderedBlocks;
	}

	vec3 mergedMapSize = maxDims * (float)idealMapsPerAxis;
	vec3 mergedMapMin = maxDims * -0.5f;

	printf("Max map size: %.0f %.0f %.0f\n", maxDims.x, maxDims.y, maxDims.z);
	printf("Max maps per axis: x=%d y=%d z=%d\n", maxMapsPerRow, maxMapsPerCol, maxMapsPerLayer);
	printf("Max maps of this size: %d\n", maxMapsPerRow * maxMapsPerCol * maxMapsPerLayer);

	int actualWidth = min(idealMapsPerAxis, (int)maps.size());
	int actualLength = min(idealMapsPerAxis, (int)ceil(maps.size() / (float)(idealMapsPerAxis)));
	int actualHeight = min(idealMapsPerAxis, (int)ceil(maps.size() / (float)(idealMapsPerAxis*idealMapsPerAxis)));
	printf("Merged map dimensions: %dx%dx%d maps\n", actualWidth, actualLength, actualHeight);

	vec3 targetMins = mergedMapMin;
	int blockIdx = 0;
	for (int z = 0; z < idealMapsPerAxis && blockIdx < blocks.size(); z++) {
		targetMins.y = -mergedMapMin.y;
		vector<vector<MAPBLOCK>> col;
		for (int y = 0; y < idealMapsPerAxis && blockIdx < blocks.size(); y++) {
			targetMins.x = -mergedMapMin.x;
			vector<MAPBLOCK> row;
			for (int x = 0; x < idealMapsPerAxis && blockIdx < blocks.size(); x++) {
				MAPBLOCK& block = blocks[blockIdx];

				block.offset = targetMins - block.mins;
				//printf("block %d: %.0f %.0f %.0f\n", blockIdx, targetMins.x, targetMins.y, targetMins.z);
				//printf("%s offset: %.0f %.0f %.0f\n", block.map->name.c_str(), block.offset.x, block.offset.y, block.offset.z);

				row.push_back(block);

				blockIdx++;
				targetMins.x += maxDims.x;
			}
			col.push_back(row);
			targetMins.y += maxDims.y;
		}
		orderedBlocks.push_back(col);
		targetMins.z += maxDims.z;
	}

	return orderedBlocks;
}