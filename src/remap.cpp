#pragma once
#include "remap.h"
#include "Bsp.h"

STRUCTCOUNT::STRUCTCOUNT() {}

STRUCTCOUNT::STRUCTCOUNT(Bsp* map) {
	planes = map->header.lump[LUMP_PLANES].nLength / sizeof(BSPPLANE);
	texInfos = map->header.lump[LUMP_TEXINFO].nLength / sizeof(BSPTEXTUREINFO);
	leaves = map->header.lump[LUMP_LEAVES].nLength / sizeof(BSPLEAF);
	nodes = map->header.lump[LUMP_NODES].nLength / sizeof(BSPNODE);
	clipnodes = map->header.lump[LUMP_CLIPNODES].nLength / sizeof(BSPCLIPNODE);
	verts = map->header.lump[LUMP_VERTICES].nLength / sizeof(vec3);
	faces = map->header.lump[LUMP_FACES].nLength / sizeof(BSPFACE);
	textures = *((int32_t*)(map->lumps[LUMP_TEXTURES]));
	markSurfs = map->header.lump[LUMP_MARKSURFACES].nLength / sizeof(uint16_t);
	surfEdges = map->header.lump[LUMP_SURFEDGES].nLength / sizeof(int32_t);
	edges = map->header.lump[LUMP_EDGES].nLength / sizeof(BSPEDGE);
	models = map->header.lump[LUMP_MODELS].nLength / sizeof(BSPMODEL);
	lightdata = map->header.lump[LUMP_LIGHTING].nLength;
	visdata = map->header.lump[LUMP_VISIBILITY].nLength;
}

void STRUCTCOUNT::add(const STRUCTCOUNT& other) {
	planes += other.planes;
	texInfos += other.texInfos;
	leaves += other.leaves;
	nodes += other.nodes;
	clipnodes += other.clipnodes;
	verts += other.verts;
	faces += other.faces;
	textures += other.textures;
	markSurfs += other.markSurfs;
	surfEdges += other.surfEdges;
	edges += other.edges;
	models += other.models;
	lightdata += other.lightdata;
	visdata += other.visdata;
}

void STRUCTCOUNT::sub(const STRUCTCOUNT& other) {
	planes -= other.planes;
	texInfos -= other.texInfos;
	leaves -= other.leaves;
	nodes -= other.nodes;
	clipnodes -= other.clipnodes;
	verts -= other.verts;
	faces -= other.faces;
	textures -= other.textures;
	markSurfs -= other.markSurfs;
	surfEdges -= other.surfEdges;
	edges -= other.edges;
	models -= other.models;
	lightdata -= other.lightdata;
	visdata -= other.visdata;
}

bool STRUCTCOUNT::allZero() {
	STRUCTCOUNT zeros;
	memset(&zeros, 0, sizeof(zeros));

	return memcmp(&zeros, this, sizeof(zeros)) == 0;
}

void print_stat(int indent, int stat, const char* data) {
	if (!stat)
		return;
	for (int i = 0; i < indent; i++)
		printf("    ");
	const char* plural = "s";
	if (string(data) == "vertex") {
		plural = "es";
	}

	printf("%s %d %s%s\n", stat > 0 ? "Deleted" : "Added", abs(stat), data, abs(stat) > 1 ? plural : "");
}

void print_stat_mem(int indent, int bytes, const char* data) {
	if (!bytes)
		return;
	for (int i = 0; i < indent; i++)
		printf("    ");
	printf("%s %.2f KB of %s\n", bytes > 0 ? "Deleted" : "Added", abs(bytes) / 1024.0f, data);
}

void STRUCTCOUNT::print_delete_stats(int indent) {
	print_stat(indent, models, "model");
	print_stat(indent, planes, "plane");
	print_stat(indent, verts, "vertex");
	print_stat(indent, nodes, "node");
	print_stat(indent, texInfos, "texinfo");
	print_stat(indent, faces, "face");
	print_stat(indent, clipnodes, "clipnode");
	print_stat(indent, leaves, "leave");
	print_stat(indent, markSurfs, "marksurface");
	print_stat(indent, surfEdges, "surfedge");
	print_stat(indent, edges, "edge");
	print_stat(indent, textures, "texture");
	print_stat_mem(indent, lightdata, "lightmap data");
	print_stat_mem(indent, visdata, "VIS data");
}

STRUCTUSAGE::STRUCTUSAGE(Bsp* map) : count(map) {
	nodes = new bool[count.nodes];
	clipnodes = new bool[count.clipnodes];
	leaves = new bool[count.leaves];
	planes = new bool[count.planes];
	verts = new bool[count.verts];
	texInfo = new bool[count.texInfos];
	faces = new bool[count.faces];
	textures = new bool[count.textures];
	markSurfs = new bool[count.markSurfs];
	surfEdges = new bool[count.surfEdges];
	edges = new bool[count.edges];

	memset(nodes, 0, count.nodes * sizeof(bool));
	memset(clipnodes, 0, count.clipnodes * sizeof(bool));
	memset(leaves, 0, count.leaves * sizeof(bool));
	memset(planes, 0, count.planes * sizeof(bool));
	memset(verts, 0, count.verts * sizeof(bool));
	memset(texInfo, 0, count.texInfos * sizeof(bool));
	memset(faces, 0, count.faces * sizeof(bool));
	memset(textures, 0, count.textures * sizeof(bool));
	memset(markSurfs, 0, count.markSurfs * sizeof(bool));
	memset(surfEdges, 0, count.surfEdges * sizeof(bool));
	memset(edges, 0, count.edges * sizeof(bool));
}

void STRUCTUSAGE::compute_sum() {
	memset(&sum, 0, sizeof(STRUCTCOUNT));
	for (int i = 0; i < count.planes; i++) sum.planes += planes[i];
	for (int i = 0; i < count.texInfos; i++) sum.texInfos += texInfo[i];
	for (int i = 0; i < count.leaves; i++) sum.leaves += leaves[i];
	for (int i = 0; i < count.nodes; i++) sum.nodes += nodes[i];
	for (int i = 0; i < count.clipnodes; i++) sum.clipnodes += clipnodes[i];
	for (int i = 0; i < count.verts; i++) sum.verts += verts[i];
	for (int i = 0; i < count.faces; i++) sum.faces += faces[i];
	for (int i = 0; i < count.textures; i++) sum.textures += textures[i];
	for (int i = 0; i < count.markSurfs; i++) sum.markSurfs += markSurfs[i];
	for (int i = 0; i < count.surfEdges; i++) sum.surfEdges += surfEdges[i];
	for (int i = 0; i < count.edges; i++) sum.edges += edges[i];
}

STRUCTUSAGE::~STRUCTUSAGE() {
	delete[] nodes;
	delete[] clipnodes;
	delete[] leaves;
	delete[] planes;
	delete[] verts;
	delete[] texInfo;
	delete[] faces;
	delete[] textures;
	delete[] markSurfs;
	delete[] surfEdges;
	delete[] edges;
}

STRUCTREMAP::STRUCTREMAP(Bsp* map) : count(map) {
	nodes = new int[count.nodes];
	clipnodes = new int[count.clipnodes];
	leaves = new int[count.leaves];
	planes = new int[count.planes];
	verts = new int[count.verts];
	texInfo = new int[count.texInfos];
	faces = new int[count.faces];
	textures = new int[count.textures];
	markSurfs = new int[count.markSurfs];
	surfEdges = new int[count.surfEdges];
	edges = new int[count.edges];

	visitedNodes = new bool[count.nodes];
	visitedClipnodes = new bool[count.clipnodes];
	visitedLeaves = new bool[count.leaves];
	visitedFaces = new bool[count.faces];

	// remap to the same index by default
	for (int i = 0; i < count.nodes; i++) nodes[i] = i;
	for (int i = 0; i < count.clipnodes; i++) clipnodes[i] = i;
	for (int i = 0; i < count.leaves; i++) leaves[i] = i;
	for (int i = 0; i < count.planes; i++) planes[i] = i;
	for (int i = 0; i < count.verts; i++) verts[i] = i;
	for (int i = 0; i < count.texInfos; i++) texInfo[i] = i;
	for (int i = 0; i < count.faces; i++) faces[i] = i;
	for (int i = 0; i < count.textures; i++) textures[i] = i;
	for (int i = 0; i < count.markSurfs; i++) markSurfs[i] = i;
	for (int i = 0; i < count.surfEdges; i++) surfEdges[i] = i;
	for (int i = 0; i < count.edges; i++) edges[i] = i;

	memset(visitedClipnodes, 0, count.clipnodes * sizeof(bool));
	memset(visitedNodes, 0, count.nodes * sizeof(bool));
	memset(visitedFaces, 0, count.faces * sizeof(bool));
	memset(visitedLeaves, 0, count.leaves * sizeof(bool));
}

STRUCTREMAP::~STRUCTREMAP() {
	delete[] nodes;
	delete[] clipnodes;
	delete[] leaves;
	delete[] planes;
	delete[] verts;
	delete[] texInfo;
	delete[] faces;
	delete[] textures;
	delete[] markSurfs;
	delete[] surfEdges;
	delete[] edges;

	delete[] visitedClipnodes;
	delete[] visitedNodes;
	delete[] visitedFaces;
	delete[] visitedLeaves;
}