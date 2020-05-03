#include "rad.h"
#include "winding.h"
#include "Bsp.h"

// filled directly in init_qrad_globals
dface_t g_dfaces[MAX_MAP_FACES];
dplane_t g_dplanes[MAX_MAP_PLANES];
texinfo_t g_texinfo[MAX_MAP_TEXINFOS];
int g_dsurfedges[MAX_MAP_SURFEDGES];
dedge_t g_dedges[MAX_MAP_EDGES];
dvertex_t g_dvertexes[MAX_MAP_VERTS];
dplane_t backplanes[MAX_MAP_PLANES];
vec3_t g_face_offset[MAX_MAP_FACES];
eModelLightmodes g_face_lightmode[MAX_MAP_FACES];
dnode_t g_dnodes[MAX_MAP_NODES];
tnode_t tnodes[MAX_MAP_NODES];
dleaf_t g_dleafs[MAX_MAP_LEAVES];
dmodel_t g_dmodels[MAX_MAP_MODELS];
int g_nummodels;
int g_numnodes;
int g_numfaces;
float g_blur;

// filled by CreateOpaqueNodes
opaquemodel_t* opaquemodels;
opaquenode_t* opaquenodes;
opaqueface_t* opaquefaces;
opaqueList_t* g_opaque_face_list;
unsigned g_opaque_face_count;

radtexture_t* g_textures;

// filled by PairEdges()
edgeshare_t g_edgeshare[MAX_MAP_EDGES];

// filled by FindFacePositions
positionmap_t g_face_positions[MAX_MAP_FACES];

// filled by CalcFaceCentroid
vec3_t g_face_centroids[MAX_MAP_EDGES];

float g_smoothing_threshold;
float g_smoothing_value = DEFAULT_SMOOTHING_VALUE;
float g_smoothing_threshold_2;
float g_smoothing_value_2 = DEFAULT_SMOOTHING2_VALUE;


// fill out the global vars that the qrad compiler code requires
void qrad_init_globals(Bsp* bsp) {
	BSPPLANE* planes = (BSPPLANE*)bsp->lumps[LUMP_PLANES];
	BSPTEXTUREINFO* texInfo = (BSPTEXTUREINFO*)bsp->lumps[LUMP_TEXINFO];
	BSPLEAF* leaves = (BSPLEAF*)bsp->lumps[LUMP_LEAVES];
	BSPMODEL* models = (BSPMODEL*)bsp->lumps[LUMP_MODELS];
	BSPNODE* nodes = (BSPNODE*)bsp->lumps[LUMP_NODES];
	BSPFACE* faces = (BSPFACE*)bsp->lumps[LUMP_FACES];
	vec3* verts = (vec3*)bsp->lumps[LUMP_VERTICES];
	int32_t* surfEdges = (int32_t*)bsp->lumps[LUMP_SURFEDGES];
	BSPEDGE* edges = (BSPEDGE*)bsp->lumps[LUMP_EDGES];
	byte* thisTex = bsp->lumps[LUMP_TEXTURES];

	int planeCount = bsp->header.lump[LUMP_PLANES].nLength / sizeof(BSPPLANE);
	int texInfoCount = bsp->header.lump[LUMP_TEXINFO].nLength / sizeof(BSPTEXTUREINFO);
	int leafCount = bsp->header.lump[LUMP_LEAVES].nLength / sizeof(BSPLEAF);
	int modelCount = bsp->header.lump[LUMP_MODELS].nLength / sizeof(BSPMODEL);
	int nodeCount = bsp->header.lump[LUMP_NODES].nLength / sizeof(BSPNODE);
	int vertCount = bsp->header.lump[LUMP_VERTICES].nLength / sizeof(vec3);
	int faceCount = bsp->header.lump[LUMP_FACES].nLength / sizeof(BSPFACE);
	int surfedgeCount = bsp->header.lump[LUMP_SURFEDGES].nLength / sizeof(int32_t);
	int edgeCount = bsp->header.lump[LUMP_EDGES].nLength / sizeof(BSPEDGE);
	int32_t thisTexCount = *((int32_t*)(bsp->lumps[LUMP_TEXTURES]));

	g_nummodels = modelCount;
	g_numnodes = nodeCount;
	g_numfaces = faceCount;
	g_opaque_face_count = 0;
	g_opaque_face_list = NULL;

	g_smoothing_threshold = (float)cos(g_smoothing_value * (Q_PI / 180.0));
	g_blur = 1.0f;

	g_textures = new radtexture_t[thisTexCount];

	for (int i = 0; i < thisTexCount; i++) {
		int32_t offset = ((int32_t*)thisTex)[i + 1];
		BSPMIPTEX* tex = (BSPMIPTEX*)(thisTex + offset);

		memset(&g_textures[i], 0, sizeof(radtexture_t));

		g_textures[i].width = tex->nWidth;
		g_textures[i].height = tex->nHeight;
		g_textures[i].reflectivity[0] = 1.0f;
		g_textures[i].reflectivity[1] = 1.0f;
		g_textures[i].reflectivity[2] = 1.0f;
		g_textures[i].canvas = NULL;

		memcpy(g_textures[i].name, tex->szName, 16);
	}

	for (int i = 0; i < surfedgeCount; i++) {
		g_dsurfedges[i] = surfEdges[i];
	}

	for (int i = 0; i < edgeCount; i++) {
		g_dedges[i].v[0] = edges[i].iVertex[0];
		g_dedges[i].v[1] = edges[i].iVertex[1];
	}

	for (int i = 0; i < vertCount; i++) {
		g_dvertexes[i].point[0] = verts[i].x;
		g_dvertexes[i].point[1] = verts[i].y;
		g_dvertexes[i].point[2] = verts[i].z;
	}

	for (int i = 0; i < leafCount; i++) {
		g_dleafs[i].contents = leaves[i].nContents;
		g_dleafs[i].firstmarksurface = leaves[i].iFirstMarkSurface;
		g_dleafs[i].nummarksurfaces = leaves[i].nMarkSurfaces;
		g_dleafs[i].visofs = leaves[i].nVisOffset;

		for (int k = 0; k < 3; k++) {
			g_dleafs[i].maxs[k] = leaves[i].nMaxs[k];
			g_dleafs[i].mins[k] = leaves[i].nMins[k];
		}
		for (int k = 0; k < NUM_AMBIENTS; k++)
			g_dleafs[i].ambient_level[k] = leaves[i].nAmbientLevels[k];
	}

	for (int i = 0; i < planeCount; i++) {
		g_dplanes[i].dist = planes[i].fDist;
		g_dplanes[i].normal[0] = planes[i].vNormal.x;
		g_dplanes[i].normal[1] = planes[i].vNormal.y;
		g_dplanes[i].normal[2] = planes[i].vNormal.z;
		g_dplanes[i].type = (planetypes)planes[i].nType;

		backplanes[i].dist = -g_dplanes[i].dist;
		VectorSubtract(vec3_origin, g_dplanes[i].normal, backplanes[i].normal);
	}
	
	for (int i = 0; i < nodeCount; i++) {
		g_dnodes[i].children[0] = nodes[i].iChildren[0];
		g_dnodes[i].children[1] = nodes[i].iChildren[1];
		g_dnodes[i].firstface = nodes[i].firstFace;
		g_dnodes[i].numfaces = nodes[i].nFaces;
		g_dnodes[i].planenum = nodes[i].iPlane;
		for (int k = 0; k < 3; k++) {
			g_dnodes[i].maxs[k] = nodes[i].nMaxs[k];
			g_dnodes[i].mins[k] = nodes[i].nMins[k];
		}

		BSPPLANE& plane = planes[nodes[i].iPlane];
		tnodes[i].type = (planetypes)plane.nType;
		tnodes[i].normal[0] = plane.vNormal.x;
		tnodes[i].normal[1] = plane.vNormal.y;
		tnodes[i].normal[2] = plane.vNormal.z;
		tnodes[i].dist = plane.fDist;

		for (int k = 0; k < 2; k++)
		{
			if (nodes[i].iChildren[k] < 0)
				tnodes[i].children[k] = g_dleafs[-nodes[i].iChildren[k] - 1].contents;
			else
				tnodes[i].children[k] = nodes[i].iChildren[k];
		}
	}

	for (int i = 0; i < faceCount; i++) {
		g_dfaces[i].firstedge = faces[i].iFirstEdge;
		g_dfaces[i].lightofs = faces[i].nLightmapOffset;
		g_dfaces[i].numedges = faces[i].nEdges;
		g_dfaces[i].planenum = faces[i].iPlane;
		g_dfaces[i].side = faces[i].nPlaneSide;
		g_dfaces[i].texinfo = faces[i].iTextureInfo;
		for (int k = 0; k < 4; k++)
			g_dfaces[i].styles[k] = faces[i].nStyles[k];
	}

	for (int i = 0; i < texInfoCount; i++) {
		g_texinfo[i].flags = texInfo[i].nFlags;
		g_texinfo[i].miptex = texInfo[i].iMiptex;
		g_texinfo[i].vecs[0][0] = texInfo[i].vS.x;
		g_texinfo[i].vecs[0][1] = texInfo[i].vS.y;
		g_texinfo[i].vecs[0][2] = texInfo[i].vS.z;
		g_texinfo[i].vecs[0][3] = texInfo[i].shiftS;
		g_texinfo[i].vecs[1][0] = texInfo[i].vT.x;
		g_texinfo[i].vecs[1][1] = texInfo[i].vT.y;
		g_texinfo[i].vecs[1][2] = texInfo[i].vT.z;
		g_texinfo[i].vecs[1][3] = texInfo[i].shiftT;
	}

	for (int i = 0; i < modelCount; i++) {
		BSPMODEL& mod = models[i];

		g_dmodels[i].firstface = mod.iFirstFace;
		g_dmodels[i].numfaces = mod.nFaces;
		g_dmodels[i].origin[0] = mod.vOrigin.x;
		g_dmodels[i].origin[1] = mod.vOrigin.y;
		g_dmodels[i].origin[2] = mod.vOrigin.z;
		g_dmodels[i].maxs[0] = mod.nMaxs.x;
		g_dmodels[i].maxs[1] = mod.nMaxs.y;
		g_dmodels[i].maxs[2] = mod.nMaxs.z;
		g_dmodels[i].mins[0] = mod.nMins.x;
		g_dmodels[i].mins[1] = mod.nMins.y;
		g_dmodels[i].mins[2] = mod.nMins.z;
		g_dmodels[i].visleafs = mod.nVisLeafs;

		for (int k = 0; k < MAX_MAP_HULLS; k++)
			g_dmodels[i].headnode[k] = mod.iHeadnodes[k];

		vec3_t origin;
		origin[0] = 0;
		origin[1] = 0;
		origin[2] = 0;

		// models with origin brushes need to be offset into their in-use position
		/*
		if (*(s = ValueForKey(ent, "origin")))
		{
			double v1, v2, v3;

			if (sscanf(s, "%lf %lf %lf", &v1, &v2, &v3) == 3)
			{
				origin[0] = v1;
				origin[1] = v2;
				origin[2] = v3;
			}
		}
		*/

		for (int j = 0; j < mod.nFaces; j++)
		{
			int fn = mod.iFirstFace + j;
			VectorCopy(origin, g_face_offset[fn]);
			//g_face_lightmode[fn] = lightmode;
			g_face_lightmode[fn] = eModelLightmodeNull;
			
			dface_t* f = g_dfaces + fn;
			Winding w(*f);

			for (int k = 0; k < w.m_NumPoints; k++)
			{
				VectorAdd(w.m_Points[k], origin, w.m_Points[k]);
			}
			
			CalcFaceCentroid(fn, &w);
		}
	}

	PairEdges();
	CreateOpaqueNodes();

	printf("Find face positions...");
	for (int i = 0; i < faceCount; i++)
		FindFacePositions(i);

	printf("DONE\n");
}

void qrad_cleanup_globals() {
	delete[] g_textures;
}

lightmap_flags_t qrad_get_lightmap_flags(Bsp* bsp, int faceIdx) {

	dface_t* f = &g_dfaces[faceIdx];

	lightmap_flags_t shift;
	memset(&shift, 0, sizeof(shift));

	f->lightofs = -1;

	if (g_texinfo[f->texinfo].flags & TEX_SPECIAL)
	{
		for (int j = 0; j < MAXLIGHTMAPS; j++)
		{
			f->styles[j] = 255;
		}
		return shift;                                            // non-lit texture
	}

	lightinfo_t l;
	memset(&l, 0, sizeof(l));
	l.surfnum = faceIdx;
	l.face = f;
	l.miptex = g_texinfo[f->texinfo].miptex;

	const dplane_t* plane = getPlaneFromFace(f);
	VectorCopy(plane->normal, l.facenormal);
	l.facedist = plane->dist;

	CalcFaceVectors(&l);
	CalcFaceExtents(&l);
	CalcPoints(&l, shift.luxelFlags);

	free(l.lmcache);
	free(l.lmcache_normal);
	free(l.lmcache_wallflags);
	free(l.surfpt_position);
	free(l.surfpt_surface);
	
	int w = l.texsize[0] + 1;
	int h = l.texsize[1] + 1;

	bool printMap = faceIdx == 3245;

	shift.w = w;
	shift.h = h;

	return shift;
}

//
// BEGIN COPIED QRAD CODE
//

void MatrixForScale(const vec3_t center, vec_t scale, matrix_t& m)
{
	int i;

	for (i = 0; i < 3; i++)
	{
		VectorClear(m.v[i]);
		m.v[i][i] = scale;
	}
	VectorScale(center, 1 - scale, m.v[3]);
}

// ApplyMatrix: (x y z 1)T -> matrix * (x y z 1)T
void ApplyMatrix(const matrix_t& m, const vec3_t in, vec3_t& out)
{
	int i;

	hlassume(&in[0] != &out[0], assume_first);
	VectorCopy(m.v[3], out);
	for (i = 0; i < 3; i++)
	{
		VectorMA(out, in[i], m.v[i], out);
	}
}

vec_t CalcMatrixSign(const matrix_t& m)
{
	vec3_t v;

	CrossProduct(m.v[0], m.v[1], v);
	return DotProduct(v, m.v[2]);
}

void MultiplyMatrix(const matrix_t& m_left, const matrix_t& m_right, matrix_t& m)
// The following two processes are equivalent:
//  1) ApplyMatrix (m1, v_in, v_temp), ApplyMatrix (m2, v_temp, v_out);
//  2) MultiplyMatrix (m2, m1, m), ApplyMatrix (m, v_in, v_out);
{
	int i, j;
	const vec_t lastrow[4] = { 0, 0, 0, 1 };

	hlassume(&m != &m_left && &m != &m_right, assume_first);
	for (i = 0; i < 3; i++)
	{
		for (j = 0; j < 4; j++)
		{
			m.v[j][i] = m_left.v[0][i] * m_right.v[j][0]
				+ m_left.v[1][i] * m_right.v[j][1]
				+ m_left.v[2][i] * m_right.v[j][2]
				+ m_left.v[3][i] * lastrow[j];
		}
	}
}

void ApplyMatrixOnPlane(const matrix_t& m_inverse, const vec3_t in_normal, vec_t in_dist, vec3_t& out_normal, vec_t& out_dist)
// out_normal is not normalized
{
	int i;

	hlassume(&in_normal[0] != &out_normal[0], assume_first);
	for (i = 0; i < 3; i++)
	{
		out_normal[i] = DotProduct(in_normal, m_inverse.v[i]);
	}
	out_dist = -(DotProduct(in_normal, m_inverse.v[3]) - in_dist);
}

bool InvertMatrix(const matrix_t& m, matrix_t& m_inverse)
{
	double texplanes[2][4];
	double faceplane[4];
	int i;
	double texaxis[2][3];
	double normalaxis[3];
	double det, sqrlen1, sqrlen2, sqrlen3;
	double texorg[3];

	for (i = 0; i < 4; i++)
	{
		texplanes[0][i] = m.v[i][0];
		texplanes[1][i] = m.v[i][1];
		faceplane[i] = m.v[i][2];
	}

	sqrlen1 = DotProduct(texplanes[0], texplanes[0]);
	sqrlen2 = DotProduct(texplanes[1], texplanes[1]);
	sqrlen3 = DotProduct(faceplane, faceplane);
	if (sqrlen1 <= NORMAL_EPSILON * NORMAL_EPSILON || sqrlen2 <= NORMAL_EPSILON * NORMAL_EPSILON || sqrlen3 <= NORMAL_EPSILON * NORMAL_EPSILON)
		// s gradient, t gradient or face normal is too close to 0
	{
		return false;
	}

	CrossProduct(texplanes[0], texplanes[1], normalaxis);
	det = DotProduct(normalaxis, faceplane);
	if (det * det <= sqrlen1 * sqrlen2 * sqrlen3 * NORMAL_EPSILON * NORMAL_EPSILON)
		// s gradient, t gradient and face normal are coplanar
	{
		return false;
	}
	VectorScale(normalaxis, 1 / det, normalaxis);

	CrossProduct(texplanes[1], faceplane, texaxis[0]);
	VectorScale(texaxis[0], 1 / det, texaxis[0]);

	CrossProduct(faceplane, texplanes[0], texaxis[1]);
	VectorScale(texaxis[1], 1 / det, texaxis[1]);

	VectorScale(normalaxis, -faceplane[3], texorg);
	VectorMA(texorg, -texplanes[0][3], texaxis[0], texorg);
	VectorMA(texorg, -texplanes[1][3], texaxis[1], texorg);

	VectorCopy(texaxis[0], m_inverse.v[0]);
	VectorCopy(texaxis[1], m_inverse.v[1]);
	VectorCopy(normalaxis, m_inverse.v[2]);
	VectorCopy(texorg, m_inverse.v[3]);
	return true;
}

const dplane_t* getPlaneFromFace(const dface_t* const face)
{
	if (!face)
	{
		Error("getPlaneFromFace() face was NULL\n");
	}

	if (face->side)
	{
		return &backplanes[face->planenum];
	}
	else
	{
		return &g_dplanes[face->planenum];
	}
}

void TranslateWorldToTex(int facenum, matrix_t& m)
// without g_face_offset
{
	dface_t* f;
	texinfo_t* ti;
	const dplane_t* fp;
	int i;

	f = &g_dfaces[facenum];
	ti = &g_texinfo[f->texinfo];
	fp = getPlaneFromFace(f);
	for (i = 0; i < 3; i++)
	{
		m.v[i][0] = ti->vecs[0][i];
		m.v[i][1] = ti->vecs[1][i];
		m.v[i][2] = fp->normal[i];
	}
	m.v[3][0] = ti->vecs[0][3];
	m.v[3][1] = ti->vecs[1][i];
	m.v[3][2] = -fp->dist;
}

// =====================================================================================
//  snap_to_winding
//      moves the point to the nearest point inside the winding
//      if the point is not on the plane, the distance between the point and the plane is preserved
//      the point and all the vertexes of the winding can move freely along the plane's normal without changing the result
// =====================================================================================
void snap_to_winding(const Winding& w, const dplane_t& plane, vec_t* const point)
{
	int				numpoints;
	int				x;
	vec_t* p1, * p2;
	vec3_t			delta;
	vec3_t			normal;
	vec_t			dist;
	vec_t			dot1, dot2, dot;
	vec3_t			bestpoint;
	vec_t			bestdist;
	bool			in;

	numpoints = w.m_NumPoints;

	in = true;
	for (x = 0; x < numpoints; x++)
	{
		p1 = w.m_Points[x];
		p2 = w.m_Points[(x + 1) % numpoints];
		VectorSubtract(p2, p1, delta);
		CrossProduct(delta, plane.normal, normal);
		dist = DotProduct(point, normal) - DotProduct(p1, normal);

		if (dist < 0.0)
		{
			in = false;

			CrossProduct(plane.normal, normal, delta);
			dot = DotProduct(delta, point);
			dot1 = DotProduct(delta, p1);
			dot2 = DotProduct(delta, p2);
			if (dot1 < dot && dot < dot2)
			{
				dist = dist / DotProduct(normal, normal);
				VectorMA(point, -dist, normal, point);
				return;
			}
		}
	}
	if (in)
	{
		return;
	}

	for (x = 0; x < numpoints; x++)
	{
		p1 = w.m_Points[x];
		VectorSubtract(p1, point, delta);
		dist = DotProduct(delta, plane.normal) / DotProduct(plane.normal, plane.normal);
		VectorMA(delta, -dist, plane.normal, delta);
		dot = DotProduct(delta, delta);

		if (x == 0 || dot < bestdist)
		{
			VectorAdd(point, delta, bestpoint);
			bestdist = dot;
		}
	}
	if (numpoints > 0)
	{
		VectorCopy(bestpoint, point);
	}
	return;
}

void GetPhongNormal(int facenum, const vec3_t spot, vec3_t phongnormal)
{
	int             j;
	int				s; // split every edge into two parts
	const dface_t* f = g_dfaces + facenum;
	const dplane_t* p = getPlaneFromFace(f);
	vec3_t          facenormal;

	VectorCopy(p->normal, facenormal);
	VectorCopy(facenormal, phongnormal);

	{
		// Calculate modified point normal for surface
		// Use the edge normals iff they are defined.  Bend the surface towards the edge normal(s)
		// Crude first attempt: find nearest edge normal and do a simple interpolation with facenormal.
		// Second attempt: find edge points+center that bound the point and do a three-point triangulation(baricentric)
		// Better third attempt: generate the point normals for all vertices and do baricentric triangulation.

		for (j = 0; j < f->numedges; j++)
		{
			vec3_t          p1;
			vec3_t          p2;
			vec3_t          v1;
			vec3_t          v2;
			vec3_t          vspot;
			unsigned        prev_edge;
			unsigned        next_edge;
			int             e;
			int             e1;
			int             e2;
			edgeshare_t* es;
			edgeshare_t* es1;
			edgeshare_t* es2;
			float           a1;
			float           a2;
			float           aa;
			float           bb;
			float           ab;

			if (j)
			{
				prev_edge = f->firstedge + ((j + f->numedges - 1) % f->numedges);
			}
			else
			{
				prev_edge = f->firstedge + f->numedges - 1;
			}

			if ((j + 1) != f->numedges)
			{
				next_edge = f->firstedge + ((j + 1) % f->numedges);
			}
			else
			{
				next_edge = f->firstedge;
			}

			e = g_dsurfedges[f->firstedge + j];
			e1 = g_dsurfedges[prev_edge];
			e2 = g_dsurfedges[next_edge];

			es = &g_edgeshare[abs(e)];
			es1 = &g_edgeshare[abs(e1)];
			es2 = &g_edgeshare[abs(e2)];

			if ((!es->smooth || es->coplanar) && (!es1->smooth || es1->coplanar) && (!es2->smooth || es2->coplanar))
			{
				continue;
			}

			if (e > 0)
			{
				VectorCopy(g_dvertexes[g_dedges[e].v[0]].point, p1);
				VectorCopy(g_dvertexes[g_dedges[e].v[1]].point, p2);
			}
			else
			{
				VectorCopy(g_dvertexes[g_dedges[-e].v[1]].point, p1);
				VectorCopy(g_dvertexes[g_dedges[-e].v[0]].point, p2);
			}

			// Adjust for origin-based models
			VectorAdd(p1, g_face_offset[facenum], p1);
			VectorAdd(p2, g_face_offset[facenum], p2);
			for (s = 0; s < 2; s++)
			{
				vec3_t s1, s2;
				if (s == 0)
				{
					VectorCopy(p1, s1);
				}
				else
				{
					VectorCopy(p2, s1);
				}

				VectorAdd(p1, p2, s2); // edge center
				VectorScale(s2, 0.5, s2);

				VectorSubtract(s1, g_face_centroids[facenum], v1);
				VectorSubtract(s2, g_face_centroids[facenum], v2);
				VectorSubtract(spot, g_face_centroids[facenum], vspot);

				aa = DotProduct(v1, v1);
				bb = DotProduct(v2, v2);
				ab = DotProduct(v1, v2);
				a1 = (bb * DotProduct(v1, vspot) - ab * DotProduct(vspot, v2)) / (aa * bb - ab * ab);
				a2 = (DotProduct(vspot, v2) - a1 * ab) / bb;

				// Test center to sample vector for inclusion between center to vertex vectors (Use dot product of vectors)
				if (a1 >= -0.01 && a2 >= -0.01)
				{
					// calculate distance from edge to pos
					vec3_t          n1, n2;
					vec3_t          temp;

					if (es->smooth)
						if (s == 0)
						{
							VectorCopy(es->vertex_normal[e > 0 ? 0 : 1], n1);
						}
						else
						{
							VectorCopy(es->vertex_normal[e > 0 ? 1 : 0], n1);
						}
					else if (s == 0 && es1->smooth)
					{
						VectorCopy(es1->vertex_normal[e1 > 0 ? 1 : 0], n1);
					}
					else if (s == 1 && es2->smooth)
					{
						VectorCopy(es2->vertex_normal[e2 > 0 ? 0 : 1], n1);
					}
					else
					{
						VectorCopy(facenormal, n1);
					}

					if (es->smooth)
					{
						VectorCopy(es->interface_normal, n2);
					}
					else
					{
						VectorCopy(facenormal, n2);
					}

					// Interpolate between the center and edge normals based on sample position
					VectorScale(facenormal, 1.0 - a1 - a2, phongnormal);
					VectorScale(n1, a1, temp);
					VectorAdd(phongnormal, temp, phongnormal);
					VectorScale(n2, a2, temp);
					VectorAdd(phongnormal, temp, phongnormal);
					VectorNormalize(phongnormal);
					break;
				}
			} // s=0,1
		}
	}
}

// Will modify the plane with the new dist
void TranslatePlane(dplane_t* plane, const vec_t* delta)
{
	plane->dist += DotProduct(plane->normal, delta);
}

void SnapToPlane(const dplane_t* const plane, vec_t* const point, vec_t offset)
{
	vec_t			dist;
	dist = DotProduct(point, plane->normal) - plane->dist;
	dist -= offset;
	VectorMA(point, -dist, plane->normal, point);
}

int TestLineOpaque_face(int facenum, const vec3_t hit)
{
	opaqueface_t* thisface = &opaquefaces[facenum];
	int x;
	if (thisface->numedges == 0)
	{
		//Developer(DEVELOPER_LEVEL_WARNING, "Warning: TestLineOpaque: Empty face.\n");
		return 0;
	}
	for (x = 0; x < thisface->numedges; x++)
	{
		if (DotProduct(hit, thisface->edges[x].normal) - thisface->edges[x].dist > ON_EPSILON)
		{
			return 0;
		}
	}
	if (thisface->tex_alphatest)
	{
		double x, y;
		x = DotProduct(hit, thisface->tex_vecs[0]) + thisface->tex_vecs[0][3];
		y = DotProduct(hit, thisface->tex_vecs[1]) + thisface->tex_vecs[1][3];
		x = floor(x - thisface->tex_width * floor(x / thisface->tex_width));
		y = floor(y - thisface->tex_height * floor(y / thisface->tex_height));
		x = x > thisface->tex_width - 1 ? thisface->tex_width - 1 : x < 0 ? 0 : x;
		y = y > thisface->tex_height - 1 ? thisface->tex_height - 1 : y < 0 ? 0 : y;
		if (thisface->tex_canvas[(int)y * thisface->tex_width + (int)x] == 0xFF)
		{
			return 0;
		}
	}
	return 1;
}

int TestPointOpaque_r(int nodenum, bool solid, const vec3_t point)
{
	opaquenode_t* thisnode;
	vec_t dist;
	while (1)
	{
		if (nodenum < 0)
		{
			if (solid && g_dleafs[-nodenum - 1].contents == CONTENTS_SOLID)
				return 1;
			else
				return 0;
		}
		thisnode = &opaquenodes[nodenum];
		switch (thisnode->type)
		{
		case plane_x:
			dist = point[0] - thisnode->dist;
			break;
		case plane_y:
			dist = point[1] - thisnode->dist;
			break;
		case plane_z:
			dist = point[2] - thisnode->dist;
			break;
		default:
			dist = DotProduct(point, thisnode->normal) - thisnode->dist;
		}
		if (dist > HUNT_WALL_EPSILON)
		{
			nodenum = thisnode->children[0];
		}
		else if (dist < -HUNT_WALL_EPSILON)
		{
			nodenum = thisnode->children[1];
		}
		else
		{
			break;
		}
	}
	{
		int facenum;
		for (facenum = thisnode->firstface; facenum < thisnode->firstface + thisnode->numfaces; facenum++)
		{
			if (TestLineOpaque_face(facenum, point))
			{
				return 1;
			}
		}
	}
	return TestPointOpaque_r(thisnode->children[0], solid, point)
		|| TestPointOpaque_r(thisnode->children[1], solid, point);
}

int TestPointOpaque(int modelnum, const vec3_t modelorigin, bool solid, const vec3_t point) // use "forceinline" because "inline" does nothing here
{
	opaquemodel_t* thismodel = &opaquemodels[modelnum];
	vec3_t newpoint;
	VectorSubtract(point, modelorigin, newpoint);
	int axial;
	for (axial = 0; axial < 3; axial++)
	{
		if (newpoint[axial] > thismodel->maxs[axial])
			return 0;
		if (newpoint[axial] < thismodel->mins[axial])
			return 0;
	}
	return TestPointOpaque_r(thismodel->headnode, solid, newpoint);
}

dleaf_t* PointInLeaf_Worst_r(int nodenum, const vec3_t point)
{
	vec_t			dist;
	dnode_t* node;
	dplane_t* plane;

	while (nodenum >= 0)
	{
		node = &g_dnodes[nodenum];
		plane = &g_dplanes[node->planenum];
		dist = DotProduct(point, plane->normal) - plane->dist;
		if (dist > HUNT_WALL_EPSILON)
		{
			nodenum = node->children[0];
		}
		else if (dist < -HUNT_WALL_EPSILON)
		{
			nodenum = node->children[1];
		}
		else
		{
			dleaf_t* result[2];
			result[0] = PointInLeaf_Worst_r(node->children[0], point);
			result[1] = PointInLeaf_Worst_r(node->children[1], point);
			if (result[0] == g_dleafs || result[0]->contents == CONTENTS_SOLID)
				return result[0];
			if (result[1] == g_dleafs || result[1]->contents == CONTENTS_SOLID)
				return result[1];
			if (result[0]->contents == CONTENTS_SKY)
				return result[0];
			if (result[1]->contents == CONTENTS_SKY)
				return result[1];
			if (result[0]->contents == result[1]->contents)
				return result[0];
			return g_dleafs;
		}
	}

	return &g_dleafs[-nodenum - 1];
}
dleaf_t* PointInLeaf_Worst(const vec3_t point)
{
	return PointInLeaf_Worst_r(0, point);
}

// HuntForWorld will never return CONTENTS_SKY or CONTENTS_SOLID leafs
dleaf_t* HuntForWorld(vec_t* point, const vec_t* plane_offset, const dplane_t* plane, int hunt_size, vec_t hunt_scale, vec_t hunt_offset)
{
	dleaf_t* leaf;
	int             x, y, z;
	int             a;

	vec3_t          current_point;
	vec3_t          original_point;

	vec3_t          best_point;
	dleaf_t* best_leaf = NULL;
	vec_t           best_dist = 99999999.0;

	vec3_t          scales;

	dplane_t        new_plane = *plane;


	scales[0] = 0.0;
	scales[1] = -hunt_scale;
	scales[2] = hunt_scale;

	VectorCopy(point, best_point);
	VectorCopy(point, original_point);

	TranslatePlane(&new_plane, plane_offset);


	for (a = 0; a < hunt_size; a++)
	{
		for (x = 0; x < 3; x++)
		{
			current_point[0] = original_point[0] + (scales[x % 3] * a);
			for (y = 0; y < 3; y++)
			{
				current_point[1] = original_point[1] + (scales[y % 3] * a);
				for (z = 0; z < 3; z++)
				{
					if (a == 0)
					{
						if (x || y || z)
							continue;
					}
					vec3_t          delta;
					vec_t           dist;

					current_point[2] = original_point[2] + (scales[z % 3] * a);

					SnapToPlane(&new_plane, current_point, hunt_offset);
					VectorSubtract(current_point, original_point, delta);
					dist = DotProduct(delta, delta);

					{
						int x;
						for (x = 0; x < g_opaque_face_count; x++)
						{
							if (TestPointOpaque(g_opaque_face_list[x].modelnum, g_opaque_face_list[x].origin, g_opaque_face_list[x].block, current_point))
								break;
						}
						if (x < g_opaque_face_count)
							continue;
					}
					if (dist < best_dist)
					{
						if ((leaf = PointInLeaf_Worst(current_point)) != g_dleafs)
						{
							if ((leaf->contents != CONTENTS_SKY) && (leaf->contents != CONTENTS_SOLID))
							{
								if (x || y || z)
								{
									//dist = best_dist;
									best_dist = dist;
									best_leaf = leaf;
									VectorCopy(current_point, best_point);
									continue;
								}
								else
								{
									VectorCopy(current_point, point);
									return leaf;
								}
							}
						}
					}
				}
			}
		}
		if (best_leaf)
		{
			break;
		}
	}

	VectorCopy(best_point, point);
	return best_leaf;
}

// =====================================================================================
//  point_in_winding_noedge
//      assume a ball is created from the point, this function checks whether the ball is entirely inside the winding
//      parameter 'width' : the radius of the ball
//      the point and all the vertexes of the winding can move freely along the plane's normal without changing the result
// =====================================================================================
bool point_in_winding_noedge(const Winding& w, const dplane_t& plane, const vec_t* const point, vec_t width)
{
	int				numpoints;
	int				x;
	vec3_t			delta;
	vec3_t			normal;
	vec_t			dist;

	numpoints = w.m_NumPoints;

	for (x = 0; x < numpoints; x++)
	{
		VectorSubtract(w.m_Points[(x + 1) % numpoints], w.m_Points[x], delta);
		CrossProduct(delta, plane.normal, normal);
		dist = DotProduct(point, normal) - DotProduct(w.m_Points[x], normal);

		if (dist < 0.0 || dist * dist <= width * width * DotProduct(normal, normal))
		{
			return false;
		}
	}

	return true;
}

// =====================================================================================
//  snap_to_winding_noedge
//      first snaps the point into the winding
//      then moves the point towards the inside for at most certain distance until:
//        either 1) the point is not close to any of the edges
//        or     2) the point can not be moved any more
//      returns the maximal distance that the point can be kept away from all the edges
//      in most of the cases, the maximal distance = width; in other cases, the maximal distance < width
// =====================================================================================
vec_t snap_to_winding_noedge(const Winding& w, const dplane_t& plane, vec_t* const point, vec_t width, vec_t maxmove)
{
	int pass;
	int numplanes;
	dplane_t* planes;
	int x;
	vec3_t v;
	vec_t newwidth;
	vec_t bestwidth;
	vec3_t bestpoint;

	snap_to_winding(w, plane, point);

	planes = (dplane_t*)malloc(w.m_NumPoints * sizeof(dplane_t));
	hlassume(planes != NULL, assume_NoMemory);
	numplanes = 0;
	for (x = 0; x < w.m_NumPoints; x++)
	{
		VectorSubtract(w.m_Points[(x + 1) % w.m_NumPoints], w.m_Points[x], v);
		CrossProduct(v, plane.normal, planes[numplanes].normal);
		if (!VectorNormalize(planes[numplanes].normal))
		{
			continue;
		}
		planes[numplanes].dist = DotProduct(w.m_Points[x], planes[numplanes].normal);
		numplanes++;
	}

	bestwidth = 0;
	VectorCopy(point, bestpoint);
	newwidth = width;

	for (pass = 0; pass < 5; pass++) // apply binary search method for 5 iterations to find the maximal distance that the point can be kept away from all the edges
	{
		bool failed;
		vec3_t newpoint;
		Winding* newwinding;

		failed = true;

		newwinding = new Winding(w);
		for (x = 0; x < numplanes && newwinding->m_NumPoints > 0; x++)
		{
			dplane_t clipplane = planes[x];
			clipplane.dist += newwidth;
			newwinding->Clip(clipplane, false);
		}

		if (newwinding->m_NumPoints > 0)
		{
			VectorCopy(point, newpoint);
			snap_to_winding(*newwinding, plane, newpoint);

			VectorSubtract(newpoint, point, v);
			if (VectorLength(v) <= maxmove + ON_EPSILON)
			{
				failed = false;
			}
		}

		delete newwinding;

		if (!failed)
		{
			bestwidth = newwidth;
			VectorCopy(newpoint, bestpoint);
			if (pass == 0)
			{
				break;
			}
			newwidth += width * pow(0.5, pass + 1);
		}
		else
		{
			newwidth -= width * pow(0.5, pass + 1);
		}
	}

	free(planes);

	VectorCopy(bestpoint, point);
	return bestwidth;
}

int TestLine_r(const int node, const vec3_t start, const vec3_t stop, int& linecontent, vec_t* skyhit)
{
	tnode_t* tnode;
	float           front, back;
	vec3_t          mid;
	float           frac;
	int             side;
	int             r;

	if (node < 0)
	{
		if (node == linecontent)
			return CONTENTS_EMPTY;
		if (node == CONTENTS_SOLID)
		{
			return CONTENTS_SOLID;
		}
		if (node == CONTENTS_SKY)
		{
			if (skyhit)
			{
				VectorCopy(start, skyhit);
			}
			return CONTENTS_SKY;
		}
		if (linecontent)
		{
			return CONTENTS_SOLID;
		}
		linecontent = node;
		return CONTENTS_EMPTY;
	}

	tnode = &tnodes[node];
	switch (tnode->type)
	{
	case plane_x:
		front = start[0] - tnode->dist;
		back = stop[0] - tnode->dist;
		break;
	case plane_y:
		front = start[1] - tnode->dist;
		back = stop[1] - tnode->dist;
		break;
	case plane_z:
		front = start[2] - tnode->dist;
		back = stop[2] - tnode->dist;
		break;
	default:
		front = (start[0] * tnode->normal[0] + start[1] * tnode->normal[1] + start[2] * tnode->normal[2]) - tnode->dist;
		back = (stop[0] * tnode->normal[0] + stop[1] * tnode->normal[1] + stop[2] * tnode->normal[2]) - tnode->dist;
		break;
	}

	if (front > ON_EPSILON / 2 && back > ON_EPSILON / 2)
	{
		return TestLine_r(tnode->children[0], start, stop
			, linecontent
			, skyhit
		);
	}
	if (front < -ON_EPSILON / 2 && back < -ON_EPSILON / 2)
	{
		return TestLine_r(tnode->children[1], start, stop
			, linecontent
			, skyhit
		);
	}
	if (fabs(front) <= ON_EPSILON && fabs(back) <= ON_EPSILON)
	{
		int r1 = TestLine_r(tnode->children[0], start, stop
			, linecontent
			, skyhit
		);
		if (r1 == CONTENTS_SOLID)
			return CONTENTS_SOLID;
		int r2 = TestLine_r(tnode->children[1], start, stop
			, linecontent
			, skyhit
		);
		if (r2 == CONTENTS_SOLID)
			return CONTENTS_SOLID;
		if (r1 == CONTENTS_SKY || r2 == CONTENTS_SKY)
			return CONTENTS_SKY;
		return CONTENTS_EMPTY;
	}
	side = (front - back) < 0;
	frac = front / (front - back);
	if (frac < 0) frac = 0;
	if (frac > 1) frac = 1;
	mid[0] = start[0] + (stop[0] - start[0]) * frac;
	mid[1] = start[1] + (stop[1] - start[1]) * frac;
	mid[2] = start[2] + (stop[2] - start[2]) * frac;
	r = TestLine_r(tnode->children[side], start, mid
		, linecontent
		, skyhit
	);
	if (r != CONTENTS_EMPTY)
		return r;
	return TestLine_r(tnode->children[!side], mid, stop
		, linecontent
		, skyhit
	);
}

int TestLine(const vec3_t start, const vec3_t stop, vec_t* skyhit=NULL)
{
	int linecontent = 0;
	return TestLine_r(0, start, stop
		, linecontent
		, skyhit
	);
}

int TestLineOpaque_r(int nodenum, const vec3_t start, const vec3_t stop)
{
	opaquenode_t* thisnode;
	vec_t front, back;
	if (nodenum < 0)
	{
		return 0;
	}
	thisnode = &opaquenodes[nodenum];
	switch (thisnode->type)
	{
	case plane_x:
		front = start[0] - thisnode->dist;
		back = stop[0] - thisnode->dist;
		break;
	case plane_y:
		front = start[1] - thisnode->dist;
		back = stop[1] - thisnode->dist;
		break;
	case plane_z:
		front = start[2] - thisnode->dist;
		back = stop[2] - thisnode->dist;
		break;
	default:
		front = DotProduct(start, thisnode->normal) - thisnode->dist;
		back = DotProduct(stop, thisnode->normal) - thisnode->dist;
	}
	if (front > ON_EPSILON / 2 && back > ON_EPSILON / 2)
	{
		return TestLineOpaque_r(thisnode->children[0], start, stop);
	}
	if (front < -ON_EPSILON / 2 && back < -ON_EPSILON / 2)
	{
		return TestLineOpaque_r(thisnode->children[1], start, stop);
	}
	if (fabs(front) <= ON_EPSILON && fabs(back) <= ON_EPSILON)
	{
		return TestLineOpaque_r(thisnode->children[0], start, stop)
			|| TestLineOpaque_r(thisnode->children[1], start, stop);
	}
	{
		int side;
		vec_t frac;
		vec3_t mid;
		int facenum;
		side = (front - back) < 0;
		frac = front / (front - back);
		if (frac < 0) frac = 0;
		if (frac > 1) frac = 1;
		mid[0] = start[0] + (stop[0] - start[0]) * frac;
		mid[1] = start[1] + (stop[1] - start[1]) * frac;
		mid[2] = start[2] + (stop[2] - start[2]) * frac;
		for (facenum = thisnode->firstface; facenum < thisnode->firstface + thisnode->numfaces; facenum++)
		{
			if (TestLineOpaque_face(facenum, mid))
			{
				return 1;
			}
		}
		return TestLineOpaque_r(thisnode->children[side], start, mid)
			|| TestLineOpaque_r(thisnode->children[!side], mid, stop);
	}
}

int TestLineOpaque(int modelnum, const vec3_t modelorigin, const vec3_t start, const vec3_t stop)
{
	opaquemodel_t* thismodel = &opaquemodels[modelnum];
	vec_t front, back, frac;
	vec3_t p1, p2;
	VectorSubtract(start, modelorigin, p1);
	VectorSubtract(stop, modelorigin, p2);
	int axial;
	for (axial = 0; axial < 3; axial++)
	{
		front = p1[axial] - thismodel->maxs[axial];
		back = p2[axial] - thismodel->maxs[axial];
		if (front >= -ON_EPSILON && back >= -ON_EPSILON)
		{
			return 0;
		}
		if (front > ON_EPSILON || back > ON_EPSILON)
		{
			frac = front / (front - back);
			if (front > back)
			{
				p1[0] = p1[0] + (p2[0] - p1[0]) * frac;
				p1[1] = p1[1] + (p2[1] - p1[1]) * frac;
				p1[2] = p1[2] + (p2[2] - p1[2]) * frac;
			}
			else
			{
				p2[0] = p1[0] + (p2[0] - p1[0]) * frac;
				p2[1] = p1[1] + (p2[1] - p1[1]) * frac;
				p2[2] = p1[2] + (p2[2] - p1[2]) * frac;
			}
		}
		front = thismodel->mins[axial] - p1[axial];
		back = thismodel->mins[axial] - p2[axial];
		if (front >= -ON_EPSILON && back >= -ON_EPSILON)
		{
			return 0;
		}
		if (front > ON_EPSILON || back > ON_EPSILON)
		{
			frac = front / (front - back);
			if (front > back)
			{
				p1[0] = p1[0] + (p2[0] - p1[0]) * frac;
				p1[1] = p1[1] + (p2[1] - p1[1]) * frac;
				p1[2] = p1[2] + (p2[2] - p1[2]) * frac;
			}
			else
			{
				p2[0] = p1[0] + (p2[0] - p1[0]) * frac;
				p2[1] = p1[1] + (p2[1] - p1[1]) * frac;
				p2[2] = p1[2] + (p2[2] - p1[2]) * frac;
			}
		}
	}
	return TestLineOpaque_r(thismodel->headnode, p1, p2);
}

bool TestSegmentAgainstOpaqueList(const vec_t* p1, const vec_t* p2, vec3_t& scaleout, int& opaquestyleout) // light must convert to this style. -1 = no convert
{
	int x;
	VectorFill(scaleout, 1.0);
	opaquestyleout = -1;
	for (x = 0; x < g_opaque_face_count; x++)
	{
		if (!TestLineOpaque(g_opaque_face_list[x].modelnum, g_opaque_face_list[x].origin, p1, p2))
		{
			continue;
		}
		if (g_opaque_face_list[x].transparency)
		{
			VectorMultiply(scaleout, g_opaque_face_list[x].transparency_scale, scaleout);
			continue;
		}
		if (g_opaque_face_list[x].style != -1 && (opaquestyleout == -1 || g_opaque_face_list[x].style == opaquestyleout))
		{
			opaquestyleout = g_opaque_face_list[x].style;
			continue;
		}
		VectorFill(scaleout, 0.0);
		opaquestyleout = -1;
		return true;
	}
	return false;
}

static bool IsPositionValid(positionmap_t* map, const vec3_t& pos_st, vec3_t& pos_out, bool usephongnormal = true, bool doedgetest = true, int hunt_size = 2, vec_t hunt_scale = 0.2)
{
	vec3_t pos;
	vec3_t pos_normal;
	vec_t hunt_offset;

	ApplyMatrix(map->textoworld, pos_st, pos);
	VectorAdd(pos, map->face_offset, pos);
	if (usephongnormal)
	{
		GetPhongNormal(map->facenum, pos, pos_normal);
	}
	else
	{
		VectorCopy(map->faceplanewithoffset.normal, pos_normal);
	}
	VectorMA(pos, DEFAULT_HUNT_OFFSET, pos_normal, pos);

	hunt_offset = DotProduct(pos, map->faceplanewithoffset.normal) - map->faceplanewithoffset.dist; // might be smaller than DEFAULT_HUNT_OFFSET

	// push the point 0.2 units around to avoid walls
	if (!HuntForWorld(pos, vec3_origin, &map->faceplanewithoffset, hunt_size, hunt_scale, hunt_offset))
	{
		return false;
	}

	if (doedgetest && !point_in_winding_noedge(*map->facewindingwithoffset, map->faceplanewithoffset, pos, DEFAULT_EDGE_WIDTH))
	{
		// if the sample has gone beyond face boundaries, be careful that it hasn't passed a wall
		vec3_t test;
		vec3_t transparency;
		int opaquestyle;

		VectorCopy(pos, test);
		snap_to_winding_noedge(*map->facewindingwithoffset, map->faceplanewithoffset, test, DEFAULT_EDGE_WIDTH, 4 * DEFAULT_EDGE_WIDTH);

		if (!HuntForWorld(test, vec3_origin, &map->faceplanewithoffset, hunt_size, hunt_scale, hunt_offset))
		{
			return false;
		}

		if (TestLine(pos, test) != CONTENTS_EMPTY)
		{
			return false;
		}

		if (TestSegmentAgainstOpaqueList(pos, test
			, transparency
			, opaquestyle
		) == true
			|| opaquestyle != -1
			)
		{
			return false;
		}
	}

	VectorCopy(pos, pos_out);
	return true;
}

static void CalcSinglePosition(positionmap_t* map, int is, int it)
{
	position_t* p;
	vec_t smin, smax, tmin, tmax;
	dplane_t clipplanes[4];
	const vec3_t v_s = { 1, 0, 0 };
	const vec3_t v_t = { 0, 1, 0 };
	Winding* zone;

	p = &map->grid[is + map->w * it];
	smin = map->start[0] + is * map->step[0];
	smax = map->start[0] + (is + 1) * map->step[0];
	tmin = map->start[1] + it * map->step[1];
	tmax = map->start[1] + (it + 1) * map->step[1];

	VectorScale(v_s, 1, clipplanes[0].normal); clipplanes[0].dist = smin;
	VectorScale(v_s, -1, clipplanes[1].normal); clipplanes[1].dist = -smax;
	VectorScale(v_t, 1, clipplanes[2].normal); clipplanes[2].dist = tmin;
	VectorScale(v_t, -1, clipplanes[3].normal); clipplanes[3].dist = -tmax;

	p->nudged = true; // it's nudged unless it can get its position directly from its s,t
	zone = new Winding(*map->texwinding);
	for (int x = 0; x < 4 && zone->m_NumPoints > 0; x++)
	{
		zone->Clip(clipplanes[x], false);
	}
	if (zone->m_NumPoints == 0)
	{
		p->valid = false;
	}
	else
	{
		vec3_t original_st;
		vec3_t test_st;

		original_st[0] = map->start[0] + (is + 0.5) * map->step[0];
		original_st[1] = map->start[1] + (it + 0.5) * map->step[1];
		original_st[2] = 0.0;

		p->valid = false;

		if (!p->valid)
		{
			VectorCopy(original_st, test_st);
			snap_to_winding(*zone, map->texplane, test_st);

			if (IsPositionValid(map, test_st, p->pos))
			{
				p->valid = true;
				p->nudged = false;
				p->best_s = test_st[0];
				p->best_t = test_st[1];
			}
		}

		if (!p->valid)
		{
			zone->getCenter(test_st);
			if (IsPositionValid(map, test_st, p->pos))
			{
				p->valid = true;
				p->best_s = test_st[0];
				p->best_t = test_st[1];
			}
		}

		if (!p->valid
			//&& !g_fastmode
			)
		{
			const int numnudges = 12;
			vec3_t nudgelist[numnudges] = { {0.1, 0, 0}, {-0.1, 0, 0}, {0, 0.1, 0}, {0, -0.1, 0},
										   {0.3, 0, 0}, {-0.3, 0, 0}, {0, 0.3, 0}, {0, -0.3, 0},
										   {0.3, 0.3, 0}, {-0.3, 0.3, 0}, {-0.3, -0.3, 0}, {0.3, -0.3, 0} };

			for (int i = 0; i < numnudges; i++)
			{
				VectorMultiply(nudgelist[i], map->step, test_st);
				VectorAdd(test_st, original_st, test_st);
				snap_to_winding(*zone, map->texplane, test_st);

				if (IsPositionValid(map, test_st, p->pos))
				{
					p->valid = true;
					p->best_s = test_st[0];
					p->best_t = test_st[1];
					break;
				}
			}
		}
	}
	delete zone;
}

bool TryMerge(opaqueface_t* f, const opaqueface_t* f2)
{
	if (!f->winding || !f2->winding)
	{
		return false;
	}
	if (fabs(f2->plane.dist - f->plane.dist) > ON_EPSILON
		|| fabs(f2->plane.normal[0] - f->plane.normal[0]) > NORMAL_EPSILON
		|| fabs(f2->plane.normal[1] - f->plane.normal[1]) > NORMAL_EPSILON
		|| fabs(f2->plane.normal[2] - f->plane.normal[2]) > NORMAL_EPSILON
		)
	{
		return false;
	}
	if ((f->tex_alphatest || f2->tex_alphatest) && f->texinfo != f2->texinfo)
	{
		return false;
	}

	Winding* w = f->winding;
	const Winding* w2 = f2->winding;
	const vec_t* pA, * pB, * pC, * pD, * p2A, * p2B, * p2C, * p2D;
	int i, i2;

	for (i = 0; i < w->m_NumPoints; i++)
	{
		for (i2 = 0; i2 < w2->m_NumPoints; i2++)
		{
			pA = w->m_Points[(i + w->m_NumPoints - 1) % w->m_NumPoints];
			pB = w->m_Points[i];
			pC = w->m_Points[(i + 1) % w->m_NumPoints];
			pD = w->m_Points[(i + 2) % w->m_NumPoints];
			p2A = w2->m_Points[(i2 + w2->m_NumPoints - 1) % w2->m_NumPoints];
			p2B = w2->m_Points[i2];
			p2C = w2->m_Points[(i2 + 1) % w2->m_NumPoints];
			p2D = w2->m_Points[(i2 + 2) % w2->m_NumPoints];
			if (!VectorCompare(pB, p2C) || !VectorCompare(pC, p2B))
			{
				continue;
			}
			break;
		}
		if (i2 == w2->m_NumPoints)
		{
			continue;
		}
		break;
	}
	if (i == w->m_NumPoints)
	{
		return false;
	}

	const vec_t* normal = f->plane.normal;
	vec3_t e1, e2;
	dplane_t pl1, pl2;
	int side1, side2;

	VectorSubtract(p2D, pA, e1);
	CrossProduct(normal, e1, pl1.normal); // pointing outward
	if (VectorNormalize(pl1.normal) == 0.0)
	{
		//Developer(DEVELOPER_LEVEL_WARNING, "Warning: TryMerge: Empty edge.\n");
		return false;
	}
	pl1.dist = DotProduct(pA, pl1.normal);
	if (DotProduct(pB, pl1.normal) - pl1.dist < -ON_EPSILON)
	{
		return false;
	}
	side1 = (DotProduct(pB, pl1.normal) - pl1.dist > ON_EPSILON) ? 1 : 0;

	VectorSubtract(pD, p2A, e2);
	CrossProduct(normal, e2, pl2.normal); // pointing outward
	if (VectorNormalize(pl2.normal) == 0.0)
	{
		//Developer(DEVELOPER_LEVEL_WARNING, "Warning: TryMerge: Empty edge.\n");
		return false;
	}
	pl2.dist = DotProduct(p2A, pl2.normal);
	if (DotProduct(p2B, pl2.normal) - pl2.dist < -ON_EPSILON)
	{
		return false;
	}
	side2 = (DotProduct(p2B, pl2.normal) - pl2.dist > ON_EPSILON) ? 1 : 0;

	Winding* neww = new Winding(w->m_NumPoints + w2->m_NumPoints - 4 + side1 + side2);
	int j, k;
	k = 0;
	for (j = (i + 2) % w->m_NumPoints; j != i; j = (j + 1) % w->m_NumPoints)
	{
		VectorCopy(w->m_Points[j], neww->m_Points[k]);
		k++;
	}
	if (side1)
	{
		VectorCopy(w->m_Points[j], neww->m_Points[k]);
		k++;
	}
	for (j = (i2 + 2) % w2->m_NumPoints; j != i2; j = (j + 1) % w2->m_NumPoints)
	{
		VectorCopy(w2->m_Points[j], neww->m_Points[k]);
		k++;
	}
	if (side2)
	{
		VectorCopy(w2->m_Points[j], neww->m_Points[k]);
		k++;
	}
	neww->RemoveColinearPoints();
	if (neww->m_NumPoints < 3)
	{
		//Developer(DEVELOPER_LEVEL_WARNING, "Warning: TryMerge: Empty winding.\n");
		delete neww;
		neww = NULL;
	}
	delete f->winding;
	f->winding = neww;
	return true;
}

int MergeOpaqueFaces(int firstface, int numfaces)
{
	int i, j, newnum;
	opaqueface_t* faces = &opaquefaces[firstface];
	for (i = 0; i < numfaces; i++)
	{
		for (j = 0; j < i; j++)
		{
			if (TryMerge(&faces[i], &faces[j]))
			{
				delete faces[j].winding;
				faces[j].winding = NULL;
				j = -1;
				continue;
			}
		}
	}
	for (i = 0, j = 0; i < numfaces; i++)
	{
		if (faces[i].winding)
		{
			faces[j] = faces[i];
			j++;
		}
	}
	newnum = j;
	for (; j < numfaces; j++)
	{
		memset(&faces[j], 0, sizeof(opaqueface_t));
	}
	return newnum;
}

void BuildFaceEdges(opaqueface_t* f)
{
	if (!f->winding)
		return;
	f->numedges = f->winding->m_NumPoints;
	f->edges = (dplane_t*)calloc(f->numedges, sizeof(dplane_t));
	const vec_t* p1, * p2;
	const vec_t* n = f->plane.normal;
	vec3_t e;
	dplane_t* pl;
	int x;
	for (x = 0; x < f->winding->m_NumPoints; x++)
	{
		p1 = f->winding->m_Points[x];
		p2 = f->winding->m_Points[(x + 1) % f->winding->m_NumPoints];
		pl = &f->edges[x];
		VectorSubtract(p2, p1, e);
		CrossProduct(n, e, pl->normal);
		if (VectorNormalize(pl->normal) == 0.0)
		{
			//Developer(DEVELOPER_LEVEL_WARNING, "Warning: BuildFaceEdges: Empty edge.\n");
			VectorClear(pl->normal);
			pl->dist = -1;
			continue;
		}
		pl->dist = DotProduct(pl->normal, p1);
	}
}

static bool TranslateTexToTex(int facenum, int edgenum, int facenum2, matrix_t& m, matrix_t& m_inverse)
// This function creates a matrix that can translate texture coords in face1 into texture coords in face2.
// It keeps all points in the common edge invariant. For example, if there is a point in the edge, and in the texture of face1, its (s,t)=(16,0), and in face2, its (s,t)=(128,64), then we must let matrix*(16,0,0)=(128,64,0)
{
	matrix_t worldtotex;
	matrix_t worldtotex2;
	dedge_t* e;
	int i;
	dvertex_t* vert[2];
	vec3_t face_vert[2];
	vec3_t face2_vert[2];
	vec3_t face_axis[2];
	vec3_t face2_axis[2];
	const vec3_t v_up = { 0, 0, 1 };
	vec_t len;
	vec_t len2;
	matrix_t edgetotex, edgetotex2;
	matrix_t inv, inv2;

	TranslateWorldToTex(facenum, worldtotex);
	TranslateWorldToTex(facenum2, worldtotex2);

	e = &g_dedges[edgenum];
	for (i = 0; i < 2; i++)
	{
		vert[i] = &g_dvertexes[e->v[i]];
		ApplyMatrix(worldtotex, vert[i]->point, face_vert[i]);
		face_vert[i][2] = 0; // this value is naturally close to 0 assuming that the edge is on the face plane, but let's make this more explicit.
		ApplyMatrix(worldtotex2, vert[i]->point, face2_vert[i]);
		face2_vert[i][2] = 0;
	}

	VectorSubtract(face_vert[1], face_vert[0], face_axis[0]);
	len = VectorLength(face_axis[0]);
	CrossProduct(v_up, face_axis[0], face_axis[1]);
	if (CalcMatrixSign(worldtotex) < 0.0) // the three vectors s, t, facenormal are in reverse order
	{
		VectorInverse(face_axis[1]);
	}

	VectorSubtract(face2_vert[1], face2_vert[0], face2_axis[0]);
	len2 = VectorLength(face2_axis[0]);
	CrossProduct(v_up, face2_axis[0], face2_axis[1]);
	if (CalcMatrixSign(worldtotex2) < 0.0)
	{
		VectorInverse(face2_axis[1]);
	}

	VectorCopy(face_axis[0], edgetotex.v[0]); // / v[0][0] v[1][0] \ is a rotation (possibly with a reflection by the edge)
	VectorCopy(face_axis[1], edgetotex.v[1]); // \ v[0][1] v[1][1] / 
	VectorScale(v_up, len, edgetotex.v[2]); // encode the length into the 3rd value of the matrix
	VectorCopy(face_vert[0], edgetotex.v[3]); // map (0,0) into the origin point

	VectorCopy(face2_axis[0], edgetotex2.v[0]);
	VectorCopy(face2_axis[1], edgetotex2.v[1]);
	VectorScale(v_up, len2, edgetotex2.v[2]);
	VectorCopy(face2_vert[0], edgetotex2.v[3]);

	if (!InvertMatrix(edgetotex, inv) || !InvertMatrix(edgetotex2, inv2))
	{
		return false;
	}
	MultiplyMatrix(edgetotex2, inv, m);
	MultiplyMatrix(edgetotex, inv2, m_inverse);

	return true;
}

intersecttest_t* CreateIntersectTest(const dplane_t* p, int facenum)
{
	dface_t* f = &g_dfaces[facenum];
	intersecttest_t* t;
	t = (intersecttest_t*)malloc(sizeof(intersecttest_t));
	hlassume(t != NULL, assume_NoMemory);
	t->clipplanes = (dplane_t*)malloc(f->numedges * sizeof(dplane_t));
	hlassume(t->clipplanes != NULL, assume_NoMemory);
	t->numclipplanes = 0;
	int j;
	for (j = 0; j < f->numedges; j++)
	{
		// should we use winding instead?
		int edgenum = g_dsurfedges[f->firstedge + j];
		{
			vec3_t v0, v1;
			vec3_t dir, normal;
			if (edgenum < 0)
			{
				VectorCopy(g_dvertexes[g_dedges[-edgenum].v[1]].point, v0);
				VectorCopy(g_dvertexes[g_dedges[-edgenum].v[0]].point, v1);
			}
			else
			{
				VectorCopy(g_dvertexes[g_dedges[edgenum].v[0]].point, v0);
				VectorCopy(g_dvertexes[g_dedges[edgenum].v[1]].point, v1);
			}
			VectorAdd(v0, g_face_offset[facenum], v0);
			VectorAdd(v1, g_face_offset[facenum], v1);
			VectorSubtract(v1, v0, dir);
			CrossProduct(dir, p->normal, normal); // facing inward
			if (!VectorNormalize(normal))
			{
				continue;
			}
			VectorCopy(normal, t->clipplanes[t->numclipplanes].normal);
			t->clipplanes[t->numclipplanes].dist = DotProduct(v0, normal);
			t->numclipplanes++;
		}
	}
	return t;
}

int AddFaceForVertexNormal(const int edgeabs, int& edgeabsnext, const int edgeend, int& edgeendnext, dface_t* const f, dface_t*& fnext, vec_t& angle, vec3_t& normal)
// Must guarantee these faces will form a loop or a chain, otherwise will result in endless loop.
//
//   e[end]/enext[endnext]
//  *
//  |\.
//  |a\ fnext
//  |  \,
//  | f \.
//  |    \.
//  e   enext
//
{
	VectorCopy(getPlaneFromFace(f)->normal, normal);
	int vnum = g_dedges[edgeabs].v[edgeend];
	int iedge, iedgenext, edge, edgenext;
	int i, e, count1, count2;
	vec_t dot;
	for (count1 = count2 = 0, i = 0; i < f->numedges; i++)
	{
		e = g_dsurfedges[f->firstedge + i];
		if (g_dedges[abs(e)].v[0] == g_dedges[abs(e)].v[1])
			continue;
		if (abs(e) == edgeabs)
		{
			iedge = i;
			edge = e;
			count1++;
		}
		else if (g_dedges[abs(e)].v[0] == vnum || g_dedges[abs(e)].v[1] == vnum)
		{
			iedgenext = i;
			edgenext = e;
			count2++;
		}
	}
	if (count1 != 1 || count2 != 1)
	{
		//AddFaceForVertexNormal_printerror(edgeabs, edgeend, f);
		return -1;
	}
	int vnum11, vnum12, vnum21, vnum22;
	vec3_t vec1, vec2;
	vnum11 = g_dedges[abs(edge)].v[edge > 0 ? 0 : 1];
	vnum12 = g_dedges[abs(edge)].v[edge > 0 ? 1 : 0];
	vnum21 = g_dedges[abs(edgenext)].v[edgenext > 0 ? 0 : 1];
	vnum22 = g_dedges[abs(edgenext)].v[edgenext > 0 ? 1 : 0];
	if (vnum == vnum12 && vnum == vnum21 && vnum != vnum11 && vnum != vnum22)
	{
		VectorSubtract(g_dvertexes[vnum11].point, g_dvertexes[vnum].point, vec1);
		VectorSubtract(g_dvertexes[vnum22].point, g_dvertexes[vnum].point, vec2);
		edgeabsnext = abs(edgenext);
		edgeendnext = edgenext > 0 ? 0 : 1;
	}
	else if (vnum == vnum11 && vnum == vnum22 && vnum != vnum12 && vnum != vnum21)
	{
		VectorSubtract(g_dvertexes[vnum12].point, g_dvertexes[vnum].point, vec1);
		VectorSubtract(g_dvertexes[vnum21].point, g_dvertexes[vnum].point, vec2);
		edgeabsnext = abs(edgenext);
		edgeendnext = edgenext > 0 ? 1 : 0;
	}
	else
	{
		//AddFaceForVertexNormal_printerror(edgeabs, edgeend, f);
		return -1;
	}
	VectorNormalize(vec1);
	VectorNormalize(vec2);
	dot = DotProduct(vec1, vec2);
	dot = dot > 1 ? 1 : dot < -1 ? -1 : dot;
	angle = acos(dot);
	edgeshare_t* es = &g_edgeshare[edgeabsnext];
	if (!(es->faces[0] && es->faces[1]))
		return 1;
	if (es->faces[0] == f && es->faces[1] != f)
		fnext = es->faces[1];
	else if (es->faces[1] == f && es->faces[0] != f)
		fnext = es->faces[0];
	else
	{
		//AddFaceForVertexNormal_printerror(edgeabs, edgeend, f);
		return -1;
	}
	return 0;
}

bool TestFaceIntersect(intersecttest_t* t, int facenum)
{
	dface_t* f2 = &g_dfaces[facenum];
	Winding* w = new Winding(*f2);
	int k;
	for (k = 0; k < w->m_NumPoints; k++)
	{
		VectorAdd(w->m_Points[k], g_face_offset[facenum], w->m_Points[k]);
	}
	for (k = 0; k < t->numclipplanes; k++)
	{
		if (!w->Clip(t->clipplanes[k], false
			, ON_EPSILON * 4
		))
		{
			break;
		}
	}
	bool intersect = w->m_NumPoints > 0;
	delete w;
	return intersect;
}

void FreeIntersectTest(intersecttest_t* t)
{
	free(t->clipplanes);
	free(t);
}

static bool IsSpecial(const dface_t* const f)
{
	return g_texinfo[f->texinfo].flags & TEX_SPECIAL;
}

// ripped from MakePatchForFace
void CalcFaceCentroid(const int fn, Winding* w)
{
	const dface_t* f = g_dfaces + fn;

	// No g_patches at all for the sky!
	if (!IsSpecial(f))
	{
		vec3_t          centroid = { 0, 0, 0 };

		int             numpoints = w->m_NumPoints;

		// Per-face data
		{
			int             j;

			// Centroid of face for nudging samples in direct lighting pass
			for (j = 0; j < f->numedges; j++)
			{
				int             edge = g_dsurfedges[f->firstedge + j];

				if (edge > 0)
				{
					VectorAdd(g_dvertexes[g_dedges[edge].v[0]].point, centroid, centroid);
					VectorAdd(g_dvertexes[g_dedges[edge].v[1]].point, centroid, centroid);
				}
				else
				{
					VectorAdd(g_dvertexes[g_dedges[-edge].v[1]].point, centroid, centroid);
					VectorAdd(g_dvertexes[g_dedges[-edge].v[0]].point, centroid, centroid);
				}
			}

			// Fixup centroid for anything with an altered origin (rotating models/turrets mostly)
			// Save them for moving direct lighting points towards the face center
			VectorScale(centroid, 1.0 / (f->numedges * 2), centroid);
			VectorAdd(centroid, g_face_offset[fn], g_face_centroids[fn]);
		}
	}
}

void PairEdges()
{
	int             i, j, k;
	dface_t* f;
	edgeshare_t* e;

	memset(&g_edgeshare, 0, sizeof(g_edgeshare));

	f = g_dfaces;
	for (i = 0; i < g_numfaces; i++, f++)
	{
		if (g_texinfo[f->texinfo].flags & TEX_SPECIAL)
		{
			// special textures don't have lightmaps
			continue;
		}
		for (j = 0; j < f->numedges; j++)
		{
			k = g_dsurfedges[f->firstedge + j];
			if (k < 0)
			{
				e = &g_edgeshare[-k];

				hlassert(e->faces[1] == NULL);
				e->faces[1] = f;
			}
			else
			{
				e = &g_edgeshare[k];

				hlassert(e->faces[0] == NULL);
				e->faces[0] = f;
			}

			if (e->faces[0] && e->faces[1]) {
				// determine if coplanar
				if (e->faces[0]->planenum == e->faces[1]->planenum
					&& e->faces[0]->side == e->faces[1]->side
					) {
					e->coplanar = true;
					VectorCopy(getPlaneFromFace(e->faces[0])->normal, e->interface_normal);
					e->cos_normals_angle = 1.0;
				}
				else {
					// see if they fall into a "smoothing group" based on angle of the normals
					vec3_t          normals[2];

					VectorCopy(getPlaneFromFace(e->faces[0])->normal, normals[0]);
					VectorCopy(getPlaneFromFace(e->faces[1])->normal, normals[1]);

					e->cos_normals_angle = DotProduct(normals[0], normals[1]);

					vec_t smoothvalue;
					int m0 = g_texinfo[e->faces[0]->texinfo].miptex;
					int m1 = g_texinfo[e->faces[1]->texinfo].miptex;
					//smoothvalue = qmax(g_smoothvalues[m0], g_smoothvalues[m1]);
					smoothvalue = g_smoothing_threshold;
					if (m0 != m1)
					{
						smoothvalue = qmax(smoothvalue, g_smoothing_threshold_2);
					}
					if (smoothvalue >= 1.0 - NORMAL_EPSILON)
					{
						smoothvalue = 2.0;
					}
					if (e->cos_normals_angle > (1.0 - NORMAL_EPSILON))
					{
						e->coplanar = true;
						VectorCopy(getPlaneFromFace(e->faces[0])->normal, e->interface_normal);
						e->cos_normals_angle = 1.0;
					}
					else if (e->cos_normals_angle >= qmax(smoothvalue - NORMAL_EPSILON, NORMAL_EPSILON))
					{
						{
							VectorAdd(normals[0], normals[1], e->interface_normal);
							VectorNormalize(e->interface_normal);
						}
					}
				}
				/*
				if (!VectorCompare(g_translucenttextures[g_texinfo[e->faces[0]->texinfo].miptex], g_translucenttextures[g_texinfo[e->faces[1]->texinfo].miptex]))
				{
					e->coplanar = false;
					VectorClear(e->interface_normal);
				}
				{
					int miptex0, miptex1;
					miptex0 = g_texinfo[e->faces[0]->texinfo].miptex;
					miptex1 = g_texinfo[e->faces[1]->texinfo].miptex;
					if (fabs(g_lightingconeinfo[miptex0][0] - g_lightingconeinfo[miptex1][0]) > NORMAL_EPSILON ||
						fabs(g_lightingconeinfo[miptex0][1] - g_lightingconeinfo[miptex1][1]) > NORMAL_EPSILON)
					{
						e->coplanar = false;
						VectorClear(e->interface_normal);
					}
				}
				*/
				if (!VectorCompare(e->interface_normal, vec3_origin))
				{
					e->smooth = true;
				}
				if (e->smooth)
				{
					// compute the matrix in advance
					if (!TranslateTexToTex(e->faces[0] - g_dfaces, abs(k), e->faces[1] - g_dfaces, e->textotex[0], e->textotex[1]))
					{
						e->smooth = false;
						e->coplanar = false;
						VectorClear(e->interface_normal);

						dvertex_t* dv = &g_dvertexes[g_dedges[abs(k)].v[0]];
						//Developer(DEVELOPER_LEVEL_MEGASPAM, "TranslateTexToTex failed on face %d and %d @(%f,%f,%f)", (int)(e->faces[0] - g_dfaces), (int)(e->faces[1] - g_dfaces), dv->point[0], dv->point[1], dv->point[2]);
					}
				}
			}
		}
	}
	{
		int edgeabs, edgeabsnext;
		int edgeend, edgeendnext;
		int d;
		dface_t* f, * fcurrent, * fnext;
		vec_t angle, angles;
		vec3_t normal, normals;
		vec3_t edgenormal;
		int r, count;
		for (edgeabs = 0; edgeabs < MAX_MAP_EDGES; edgeabs++)
		{
			e = &g_edgeshare[edgeabs];
			if (!e->smooth)
				continue;
			VectorCopy(e->interface_normal, edgenormal);
			if (g_dedges[edgeabs].v[0] == g_dedges[edgeabs].v[1])
			{
				vec3_t errorpos;
				VectorCopy(g_dvertexes[g_dedges[edgeabs].v[0]].point, errorpos);
				VectorAdd(errorpos, g_face_offset[e->faces[0] - g_dfaces], errorpos);
				//Developer(DEVELOPER_LEVEL_WARNING, "PairEdges: invalid edge at (%f,%f,%f)", errorpos[0], errorpos[1], errorpos[2]);
				VectorCopy(edgenormal, e->vertex_normal[0]);
				VectorCopy(edgenormal, e->vertex_normal[1]);
			}
			else
			{
				const dplane_t* p0 = getPlaneFromFace(e->faces[0]);
				const dplane_t* p1 = getPlaneFromFace(e->faces[1]);
				intersecttest_t* test0 = CreateIntersectTest(p0, e->faces[0] - g_dfaces);
				intersecttest_t* test1 = CreateIntersectTest(p1, e->faces[1] - g_dfaces);
				for (edgeend = 0; edgeend < 2; edgeend++)
				{
					vec3_t errorpos;
					VectorCopy(g_dvertexes[g_dedges[edgeabs].v[edgeend]].point, errorpos);
					VectorAdd(errorpos, g_face_offset[e->faces[0] - g_dfaces], errorpos);
					angles = 0;
					VectorClear(normals);

					for (d = 0; d < 2; d++)
					{
						f = e->faces[d];
						count = 0, fnext = f, edgeabsnext = edgeabs, edgeendnext = edgeend;
						while (1)
						{
							fcurrent = fnext;
							r = AddFaceForVertexNormal(edgeabsnext, edgeabsnext, edgeendnext, edgeendnext, fcurrent, fnext, angle, normal);
							count++;
							if (r == -1)
							{
								//Developer(DEVELOPER_LEVEL_WARNING, "PairEdges: face edges mislink at (%f,%f,%f)", errorpos[0], errorpos[1], errorpos[2]);
								break;
							}
							if (count >= 100)
							{
								//Developer(DEVELOPER_LEVEL_WARNING, "PairEdges: faces mislink at (%f,%f,%f)", errorpos[0], errorpos[1], errorpos[2]);
								break;
							}
							if (DotProduct(normal, p0->normal) <= NORMAL_EPSILON || DotProduct(normal, p1->normal) <= NORMAL_EPSILON)
								break;
							vec_t smoothvalue;
							int m0 = g_texinfo[f->texinfo].miptex;
							int m1 = g_texinfo[fcurrent->texinfo].miptex;
							//smoothvalue = qmax(g_smoothvalues[m0], g_smoothvalues[m1]);
							smoothvalue = g_smoothing_threshold;
							if (m0 != m1)
							{
								smoothvalue = qmax(smoothvalue, g_smoothing_threshold_2);
							}
							if (smoothvalue >= 1.0 - NORMAL_EPSILON)
							{
								smoothvalue = 2.0;
							}
							if (DotProduct(edgenormal, normal) < qmax(smoothvalue - NORMAL_EPSILON, NORMAL_EPSILON))
								break;
							if (fcurrent != e->faces[0] && fcurrent != e->faces[1] &&
								(TestFaceIntersect(test0, fcurrent - g_dfaces) || TestFaceIntersect(test1, fcurrent - g_dfaces)))
							{
								//Developer(DEVELOPER_LEVEL_WARNING, "Overlapping faces around corner (%f,%f,%f)\n", errorpos[0], errorpos[1], errorpos[2]);
								break;
							}
							angles += angle;
							VectorMA(normals, angle, normal, normals);
							{
								bool in = false;
								if (fcurrent == e->faces[0] || fcurrent == e->faces[1])
								{
									in = true;
								}
								for (facelist_t* l = e->vertex_facelist[edgeend]; l; l = l->next)
								{
									if (fcurrent == l->face)
									{
										in = true;
									}
								}
								if (!in)
								{
									facelist_t* l = (facelist_t*)malloc(sizeof(facelist_t));
									hlassume(l != NULL, assume_NoMemory);
									l->face = fcurrent;
									l->next = e->vertex_facelist[edgeend];
									e->vertex_facelist[edgeend] = l;
								}
							}
							if (r != 0 || fnext == f)
								break;
						}
					}

					if (angles < NORMAL_EPSILON)
					{
						VectorCopy(edgenormal, e->vertex_normal[edgeend]);
						//Developer(DEVELOPER_LEVEL_WARNING, "PairEdges: no valid faces at (%f,%f,%f)", errorpos[0], errorpos[1], errorpos[2]);
					}
					else
					{
						VectorNormalize(normals);
						VectorCopy(normals, e->vertex_normal[edgeend]);
					}
				}
				FreeIntersectTest(test0);
				FreeIntersectTest(test1);
			}
			if (e->coplanar)
			{
				if (!VectorCompare(e->vertex_normal[0], e->interface_normal) || !VectorCompare(e->vertex_normal[1], e->interface_normal))
				{
					e->coplanar = false;
				}
			}
		}
	}
}

void CreateOpaqueNodes()
{
	int i, j;
	opaquemodels = (opaquemodel_t*)calloc(g_nummodels, sizeof(opaquemodel_t));
	opaquenodes = (opaquenode_t*)calloc(g_numnodes, sizeof(opaquenode_t));
	opaquefaces = (opaqueface_t*)calloc(g_numfaces, sizeof(opaqueface_t));
	for (i = 0; i < g_numfaces; i++)
	{
		opaqueface_t* of = &opaquefaces[i];
		dface_t* df = &g_dfaces[i];
		of->winding = new Winding(*df);
		if (of->winding->m_NumPoints < 3)
		{
			delete of->winding;
			of->winding = NULL;
		}
		of->plane = g_dplanes[df->planenum];
		if (df->side)
		{
			VectorInverse(of->plane.normal);
			of->plane.dist = -of->plane.dist;
		}
		of->texinfo = df->texinfo;
		texinfo_t* info = &g_texinfo[of->texinfo];
		for (j = 0; j < 2; j++)
		{
			for (int k = 0; k < 4; k++)
			{
				of->tex_vecs[j][k] = info->vecs[j][k];
			}
		}
		radtexture_t* tex = &g_textures[info->miptex];
		of->tex_alphatest = tex->name[0] == '{';
		of->tex_width = tex->width;
		of->tex_height = tex->height;
		of->tex_canvas = tex->canvas;
	}
	for (i = 0; i < g_numnodes; i++)
	{
		opaquenode_t* on = &opaquenodes[i];
		dnode_t* dn = &g_dnodes[i];
		on->type = g_dplanes[dn->planenum].type;
		VectorCopy(g_dplanes[dn->planenum].normal, on->normal);
		on->dist = g_dplanes[dn->planenum].dist;
		on->children[0] = dn->children[0];
		on->children[1] = dn->children[1];
		on->firstface = dn->firstface;
		on->numfaces = dn->numfaces;
		on->numfaces = MergeOpaqueFaces(on->firstface, on->numfaces);
	}
	for (i = 0; i < g_numfaces; i++)
	{
		BuildFaceEdges(&opaquefaces[i]);
	}
	for (i = 0; i < g_nummodels; i++)
	{
		opaquemodel_t* om = &opaquemodels[i];
		dmodel_t* dm = &g_dmodels[i];
		om->headnode = dm->headnode[0];
		for (j = 0; j < 3; j++)
		{
			om->mins[j] = dm->mins[j] - 1;
			om->maxs[j] = dm->maxs[j] + 1;
		}
	}
}

// this function must be called after g_face_offset and g_face_centroids and g_edgeshare have been calculated
void FindFacePositions(int facenum)
{
	dface_t* f;
	positionmap_t* map;
	texinfo_t* ti;
	vec3_t v;
	const vec3_t v_up = { 0, 0, 1 };
	vec_t density;
	vec_t texmins[2], texmaxs[2];
	int imins[2], imaxs[2];
	int is, it;
	int x;
	int k;

	f = &g_dfaces[facenum];
	map = &g_face_positions[facenum];
	map->valid = true;
	map->facenum = facenum;
	map->facewinding = NULL;
	map->facewindingwithoffset = NULL;
	map->texwinding = NULL;
	map->grid = NULL;

	ti = &g_texinfo[f->texinfo];
	if (ti->flags & TEX_SPECIAL)
	{
		map->valid = false;
		return;
	}

	VectorCopy(g_face_offset[facenum], map->face_offset);
	VectorCopy(g_face_centroids[facenum], map->face_centroid);
	TranslateWorldToTex(facenum, map->worldtotex);
	if (!InvertMatrix(map->worldtotex, map->textoworld))
	{
		map->valid = false;
		return;
	}

	map->facewinding = new Winding(*f);
	map->faceplane = *getPlaneFromFace(f);
	map->facewindingwithoffset = new Winding(map->facewinding->m_NumPoints);
	for (x = 0; x < map->facewinding->m_NumPoints; x++)
	{
		VectorAdd(map->facewinding->m_Points[x], map->face_offset, map->facewindingwithoffset->m_Points[x]);
	}
	map->faceplanewithoffset = map->faceplane;
	map->faceplanewithoffset.dist = map->faceplane.dist + DotProduct(map->face_offset, map->faceplane.normal);

	map->texwinding = new Winding(map->facewinding->m_NumPoints);
	for (x = 0; x < map->facewinding->m_NumPoints; x++)
	{
		ApplyMatrix(map->worldtotex, map->facewinding->m_Points[x], map->texwinding->m_Points[x]);
		map->texwinding->m_Points[x][2] = 0.0;
	}
	map->texwinding->RemoveColinearPoints();
	VectorCopy(v_up, map->texplane.normal);
	if (CalcMatrixSign(map->worldtotex) < 0.0)
	{
		map->texplane.normal[2] *= -1;
	}
	map->texplane.dist = 0.0;
	if (map->texwinding->m_NumPoints == 0)
	{
		delete map->facewinding;
		map->facewinding = NULL;
		delete map->facewindingwithoffset;
		map->facewindingwithoffset = NULL;
		delete map->texwinding;
		map->texwinding = NULL;
		map->valid = false;
		return;
	}
	VectorSubtract(map->face_centroid, map->face_offset, v);
	ApplyMatrix(map->worldtotex, v, map->texcentroid);
	map->texcentroid[2] = 0.0;

	for (x = 0; x < map->texwinding->m_NumPoints; x++)
	{
		for (k = 0; k < 2; k++)
		{
			if (x == 0 || map->texwinding->m_Points[x][k] < texmins[k])
				texmins[k] = map->texwinding->m_Points[x][k];
			if (x == 0 || map->texwinding->m_Points[x][k] > texmaxs[k])
				texmaxs[k] = map->texwinding->m_Points[x][k];
		}
	}
	density = 3.0;
	/*
	if (g_fastmode)
	{
		density = 1.0;
	}
	*/
	map->step[0] = (vec_t)TEXTURE_STEP / density;
	map->step[1] = (vec_t)TEXTURE_STEP / density;
	map->step[2] = 1.0;
	for (k = 0; k < 2; k++)
	{
		imins[k] = (int)floor(texmins[k] / map->step[k] + 0.5 - ON_EPSILON);
		imaxs[k] = (int)ceil(texmaxs[k] / map->step[k] - 0.5 + ON_EPSILON);
	}
	map->start[0] = (imins[0] - 0.5) * map->step[0];
	map->start[1] = (imins[1] - 0.5) * map->step[1];
	map->start[2] = 0.0;
	map->w = imaxs[0] - imins[0] + 1;
	map->h = imaxs[1] - imins[1] + 1;
	if (map->w <= 0 || map->h <= 0 || (double)map->w * (double)map->h > 99999999)
	{
		delete map->facewinding;
		map->facewinding = NULL;
		delete map->facewindingwithoffset;
		map->facewindingwithoffset = NULL;
		delete map->texwinding;
		map->texwinding = NULL;
		map->valid = false;
		return;
	}

	map->grid = (position_t*)malloc(map->w * map->h * sizeof(position_t));
	hlassume(map->grid != NULL, assume_NoMemory);

	for (it = 0; it < map->h; it++)
	{
		for (is = 0; is < map->w; is++)
		{
			CalcSinglePosition(map, is, it);
		}
	}

	return;
}

bool point_in_winding(const Winding& w, const dplane_t& plane, const vec_t* const point, vec_t epsilon)
{
	int				numpoints;
	int				x;
	vec3_t			delta;
	vec3_t			normal;
	vec_t			dist;

	numpoints = w.m_NumPoints;

	for (x = 0; x < numpoints; x++)
	{
		VectorSubtract(w.m_Points[(x + 1) % numpoints], w.m_Points[x], delta);
		CrossProduct(delta, plane.normal, normal);
		dist = DotProduct(point, normal) - DotProduct(w.m_Points[x], normal);

		if (dist < 0.0
			&& (epsilon == 0.0 || dist * dist > epsilon* epsilon* DotProduct(normal, normal)))
		{
			return false;
		}
	}

	return true;
}

static bool FindBestEdge(samplefraginfo_t* info, samplefrag_t*& bestfrag, samplefragedge_t*& bestedge)
{
	samplefrag_t* f;
	samplefragedge_t* e;
	bool found;

	found = false;

	for (f = info->head; f; f = f->next)
	{
		for (e = f->edges; e < f->edges + f->numedges; e++)
		{
			if (e->tried)
			{
				continue;
			}

			bool better;

			if (!found)
			{
				better = true;
			}
			else if ((e->flippedangle < Q_PI + NORMAL_EPSILON) != (bestedge->flippedangle < Q_PI + NORMAL_EPSILON))
			{
				better = ((e->flippedangle < Q_PI + NORMAL_EPSILON) && !(bestedge->flippedangle < Q_PI + NORMAL_EPSILON));
			}
			else if (e->noseam != bestedge->noseam)
			{
				better = (e->noseam && !bestedge->noseam);
			}
			else if (fabs(e->distance - bestedge->distance) > ON_EPSILON)
			{
				better = (e->distance < bestedge->distance);
			}
			else if (fabs(e->distancereduction - bestedge->distancereduction) > ON_EPSILON)
			{
				better = (e->distancereduction > bestedge->distancereduction);
			}
			else
			{
				better = e->edgenum < bestedge->edgenum;
			}

			if (better)
			{
				found = true;
				bestfrag = f;
				bestedge = e;
			}
		}
	}

	return found;
}

static void ChopFrag(samplefrag_t* frag)
// fill winding, windingplane, mywinding, mywindingplane, numedges, edges
{
	// get the shape of the fragment by clipping the face using the boundaries
	dface_t* f;
	Winding* facewinding;
	matrix_t worldtotex;
	const vec3_t v_up = { 0, 0, 1 };

	f = &g_dfaces[frag->facenum];
	facewinding = new Winding(*f);

	TranslateWorldToTex(frag->facenum, worldtotex);
	frag->mywinding = new Winding(facewinding->m_NumPoints);
	for (int x = 0; x < facewinding->m_NumPoints; x++)
	{
		ApplyMatrix(worldtotex, facewinding->m_Points[x], frag->mywinding->m_Points[x]);
		frag->mywinding->m_Points[x][2] = 0.0;
	}
	frag->mywinding->RemoveColinearPoints();
	VectorCopy(v_up, frag->mywindingplane.normal); // this is the same as applying the worldtotex matrix to the faceplane
	if (CalcMatrixSign(worldtotex) < 0.0)
	{
		frag->mywindingplane.normal[2] *= -1;
	}
	frag->mywindingplane.dist = 0.0;

	for (int x = 0; x < 4 && frag->mywinding->m_NumPoints > 0; x++)
	{
		frag->mywinding->Clip(frag->myrect.planes[x], false);
	}

	frag->winding = new Winding(frag->mywinding->m_NumPoints);
	for (int x = 0; x < frag->mywinding->m_NumPoints; x++)
	{
		ApplyMatrix(frag->mycoordtocoord, frag->mywinding->m_Points[x], frag->winding->m_Points[x]);
	}
	frag->winding->RemoveColinearPoints();
	VectorCopy(frag->mywindingplane.normal, frag->windingplane.normal);
	if (CalcMatrixSign(frag->mycoordtocoord) < 0.0)
	{
		frag->windingplane.normal[2] *= -1;
	}
	frag->windingplane.dist = 0.0;

	delete facewinding;

	// find the edges where the fragment can grow in the future
	frag->numedges = 0;
	frag->edges = (samplefragedge_t*)malloc(f->numedges * sizeof(samplefragedge_t));
	hlassume(frag->edges != NULL, assume_NoMemory);
	for (int i = 0; i < f->numedges; i++)
	{
		samplefragedge_t* e;
		edgeshare_t* es;
		dedge_t* de;
		dvertex_t* dv1;
		dvertex_t* dv2;
		vec_t frac1, frac2;
		vec_t edgelen;
		vec_t dot, dot1, dot2;
		vec3_t tmp, v, normal;
		const matrix_t* m;
		const matrix_t* m_inverse;

		e = &frag->edges[frag->numedges];

		// some basic info
		e->edgenum = abs(g_dsurfedges[f->firstedge + i]);
		e->edgeside = (g_dsurfedges[f->firstedge + i] < 0 ? 1 : 0);
		es = &g_edgeshare[e->edgenum];
		if (!es->smooth)
		{
			continue;
		}
		if (es->faces[e->edgeside] - g_dfaces != frag->facenum)
		{
			Error("internal error 1 in GrowSingleSampleFrag");
		}
		m = &es->textotex[e->edgeside];
		m_inverse = &es->textotex[1 - e->edgeside];
		e->nextfacenum = es->faces[1 - e->edgeside] - g_dfaces;
		if (e->nextfacenum == frag->facenum)
		{
			continue; // an invalid edge (usually very short)
		}
		e->tried = false; // because the frag hasn't been linked into the list yet

		// translate the edge points from world to the texture plane of the original frag
		//   so the distances are able to be compared among edges from different frags
		de = &g_dedges[e->edgenum];
		dv1 = &g_dvertexes[de->v[e->edgeside]];
		dv2 = &g_dvertexes[de->v[1 - e->edgeside]];
		ApplyMatrix(worldtotex, dv1->point, tmp);
		ApplyMatrix(frag->mycoordtocoord, tmp, e->point1);
		e->point1[2] = 0.0;
		ApplyMatrix(worldtotex, dv2->point, tmp);
		ApplyMatrix(frag->mycoordtocoord, tmp, e->point2);
		e->point2[2] = 0.0;
		VectorSubtract(e->point2, e->point1, e->direction);
		edgelen = VectorNormalize(e->direction);
		if (edgelen <= ON_EPSILON)
		{
			continue;
		}

		// clip the edge
		frac1 = 0;
		frac2 = 1;
		for (int x = 0; x < 4; x++)
		{
			vec_t dot1;
			vec_t dot2;

			dot1 = DotProduct(e->point1, frag->rect.planes[x].normal) - frag->rect.planes[x].dist;
			dot2 = DotProduct(e->point2, frag->rect.planes[x].normal) - frag->rect.planes[x].dist;
			if (dot1 <= ON_EPSILON && dot2 <= ON_EPSILON)
			{
				frac1 = 1;
				frac2 = 0;
			}
			else if (dot1 < 0)
			{
				frac1 = qmax(frac1, dot1 / (dot1 - dot2));
			}
			else if (dot2 < 0)
			{
				frac2 = qmin(frac2, dot1 / (dot1 - dot2));
			}
		}
		if (edgelen * (frac2 - frac1) <= ON_EPSILON)
		{
			continue;
		}
		VectorMA(e->point1, edgelen * frac2, e->direction, e->point2);
		VectorMA(e->point1, edgelen * frac1, e->direction, e->point1);

		// calculate the distance, etc., which are used to determine its priority
		e->noseam = frag->noseam;
		dot = DotProduct(frag->origin, e->direction);
		dot1 = DotProduct(e->point1, e->direction);
		dot2 = DotProduct(e->point2, e->direction);
		dot = qmax(dot1, qmin(dot, dot2));
		VectorMA(e->point1, dot - dot1, e->direction, v);
		VectorSubtract(v, frag->origin, v);
		e->distance = VectorLength(v);
		CrossProduct(e->direction, frag->windingplane.normal, normal);
		VectorNormalize(normal); // points inward
		e->distancereduction = DotProduct(v, normal);
		e->flippedangle = frag->flippedangle + acos(qmin(es->cos_normals_angle, 1.0));

		// calculate the matrix
		e->ratio = (*m_inverse).v[2][2];
		if (e->ratio <= NORMAL_EPSILON || (1 / e->ratio) <= NORMAL_EPSILON)
		{
			//Developer(DEVELOPER_LEVEL_SPAM, "TranslateTexToTex failed on face %d and %d @(%f,%f,%f)", frag->facenum, e->nextfacenum, dv1->point[0], dv1->point[1], dv1->point[2]);
			continue;
		}

		if (fabs(e->ratio - 1) < 0.005)
		{
			e->prevtonext = *m;
			e->nexttoprev = *m_inverse;
		}
		else
		{
			e->noseam = false;
			e->prevtonext = *m;
			e->nexttoprev = *m_inverse;
		}

		frag->numedges++;
	}
}

static samplefrag_t* GrowSingleFrag(const samplefraginfo_t* info, samplefrag_t* parent, samplefragedge_t* edge)
{
	samplefrag_t* frag;
	bool overlap;
	int numclipplanes;
	dplane_t* clipplanes;

	frag = (samplefrag_t*)malloc(sizeof(samplefrag_t));
	hlassume(frag != NULL, assume_NoMemory);

	// some basic info
	frag->next = NULL;
	frag->parentfrag = parent;
	frag->parentedge = edge;
	frag->facenum = edge->nextfacenum;

	frag->flippedangle = edge->flippedangle;
	frag->noseam = edge->noseam;

	// calculate the matrix
	MultiplyMatrix(edge->prevtonext, parent->coordtomycoord, frag->coordtomycoord);
	MultiplyMatrix(parent->mycoordtocoord, edge->nexttoprev, frag->mycoordtocoord);

	// fill in origin
	VectorCopy(parent->origin, frag->origin);
	ApplyMatrix(frag->coordtomycoord, frag->origin, frag->myorigin);

	// fill in boundaries
	frag->rect = parent->rect;
	for (int x = 0; x < 4; x++)
	{
		// since a plane's parameters are in the dual coordinate space, we translate the original absolute plane into this relative plane by multiplying the inverse matrix
		ApplyMatrixOnPlane(frag->mycoordtocoord, frag->rect.planes[x].normal, frag->rect.planes[x].dist, frag->myrect.planes[x].normal, frag->myrect.planes[x].dist);
		double len = VectorLength(frag->myrect.planes[x].normal);
		if (!len)
		{
			//Developer(DEVELOPER_LEVEL_MEGASPAM, "couldn't translate sample boundaries on face %d", frag->facenum);
			free(frag);
			return NULL;
		}
		VectorScale(frag->myrect.planes[x].normal, 1 / len, frag->myrect.planes[x].normal);
		frag->myrect.planes[x].dist /= len;
	}

	// chop windings and edges
	ChopFrag(frag);

	if (frag->winding->m_NumPoints == 0 || frag->mywinding->m_NumPoints == 0)
	{
		// empty
		delete frag->mywinding;
		delete frag->winding;
		free(frag->edges);
		free(frag);
		return NULL;
	}

	// do overlap test

	overlap = false;
	clipplanes = (dplane_t*)malloc(frag->winding->m_NumPoints * sizeof(dplane_t));
	hlassume(clipplanes != NULL, assume_NoMemory);
	numclipplanes = 0;
	for (int x = 0; x < frag->winding->m_NumPoints; x++)
	{
		vec3_t v;
		VectorSubtract(frag->winding->m_Points[(x + 1) % frag->winding->m_NumPoints], frag->winding->m_Points[x], v);
		CrossProduct(v, frag->windingplane.normal, clipplanes[numclipplanes].normal);
		if (!VectorNormalize(clipplanes[numclipplanes].normal))
		{
			continue;
		}
		clipplanes[numclipplanes].dist = DotProduct(frag->winding->m_Points[x], clipplanes[numclipplanes].normal);
		numclipplanes++;
	}
	for (samplefrag_t* f2 = info->head; f2 && !overlap; f2 = f2->next)
	{
		Winding* w = new Winding(*f2->winding);
		for (int x = 0; x < numclipplanes && w->m_NumPoints > 0; x++)
		{
			w->Clip(clipplanes[x], false
				, 4 * ON_EPSILON
			);
		}
		if (w->m_NumPoints > 0)
		{
			overlap = true;
		}
		delete w;
	}
	free(clipplanes);
	if (overlap)
	{
		// in the original texture plane, this fragment overlaps with some existing fragments
		delete frag->mywinding;
		delete frag->winding;
		free(frag->edges);
		free(frag);
		return NULL;
	}

	return frag;
}

static samplefraginfo_t* CreateSampleFrag(int facenum, vec_t s, vec_t t, const vec_t square[2][2], int maxsize)
{
	samplefraginfo_t* info;
	const vec3_t v_s = { 1, 0, 0 };
	const vec3_t v_t = { 0, 1, 0 };

	info = (samplefraginfo_t*)malloc(sizeof(samplefraginfo_t));
	hlassume(info != NULL, assume_NoMemory);
	info->maxsize = maxsize;
	info->size = 1;
	info->head = (samplefrag_t*)malloc(sizeof(samplefrag_t));
	hlassume(info->head != NULL, assume_NoMemory);

	info->head->next = NULL;
	info->head->parentfrag = NULL;
	info->head->parentedge = NULL;
	info->head->facenum = facenum;

	info->head->flippedangle = 0.0;
	info->head->noseam = true;

	MatrixForScale(vec3_origin, 1.0, info->head->coordtomycoord);
	MatrixForScale(vec3_origin, 1.0, info->head->mycoordtocoord);

	info->head->origin[0] = s;
	info->head->origin[1] = t;
	info->head->origin[2] = 0.0;
	VectorCopy(info->head->origin, info->head->myorigin);

	VectorScale(v_s, 1, info->head->rect.planes[0].normal); info->head->rect.planes[0].dist = square[0][0]; // smin
	VectorScale(v_s, -1, info->head->rect.planes[1].normal); info->head->rect.planes[1].dist = -square[1][0]; // smax
	VectorScale(v_t, 1, info->head->rect.planes[2].normal); info->head->rect.planes[2].dist = square[0][1]; // tmin
	VectorScale(v_t, -1, info->head->rect.planes[3].normal); info->head->rect.planes[3].dist = -square[1][1]; // tmax
	info->head->myrect = info->head->rect;

	ChopFrag(info->head);

	if (info->head->winding->m_NumPoints == 0 || info->head->mywinding->m_NumPoints == 0)
	{
		// empty
		delete info->head->mywinding;
		delete info->head->winding;
		free(info->head->edges);
		free(info->head);
		info->head = NULL;
		info->size = 0;
	}
	else
	{
		// prune edges
		for (samplefragedge_t* e = info->head->edges; e < info->head->edges + info->head->numedges; e++)
		{
			if (e->nextfacenum == info->head->facenum)
			{
				e->tried = true;
			}
		}
	}

	while (info->size < info->maxsize)
	{
		samplefrag_t* bestfrag;
		samplefragedge_t* bestedge;
		samplefrag_t* newfrag;

		if (!FindBestEdge(info, bestfrag, bestedge))
		{
			break;
		}

		newfrag = GrowSingleFrag(info, bestfrag, bestedge);
		bestedge->tried = true;

		if (newfrag)
		{
			newfrag->next = info->head;
			info->head = newfrag;
			info->size++;

			for (samplefrag_t* f = info->head; f; f = f->next)
			{
				for (samplefragedge_t* e = newfrag->edges; e < newfrag->edges + newfrag->numedges; e++)
				{
					if (e->nextfacenum == f->facenum)
					{
						e->tried = true;
					}
				}
			}
			for (samplefrag_t* f = info->head; f; f = f->next)
			{
				for (samplefragedge_t* e = f->edges; e < f->edges + f->numedges; e++)
				{
					if (e->nextfacenum == newfrag->facenum)
					{
						e->tried = true;
					}
				}
			}
		}
	}

	return info;
}

static bool FindNearestPosition(int facenum, const Winding* texwinding, const dplane_t& texplane, vec_t s, vec_t t, vec3_t& pos, vec_t* best_s, vec_t* best_t, vec_t* dist, bool* nudged)
{
	positionmap_t* map;
	vec3_t original_st;
	int x;
	int itmin, itmax, ismin, ismax;
	const vec3_t v_s = { 1, 0, 0 };
	const vec3_t v_t = { 0, 1, 0 };
	int is;
	int it;
	vec3_t v;
	bool found;
	int best_is;
	int best_it;
	vec_t best_dist;

	map = &g_face_positions[facenum];
	if (!map->valid)
	{
		return false;
	}

	original_st[0] = s;
	original_st[1] = t;
	original_st[2] = 0.0;

	if (point_in_winding(*map->texwinding, map->texplane, original_st, 4 * ON_EPSILON))
	{
		itmin = (int)ceil((original_st[1] - map->start[1] - 2 * ON_EPSILON) / map->step[1]) - 1;
		itmax = (int)floor((original_st[1] - map->start[1] + 2 * ON_EPSILON) / map->step[1]);
		ismin = (int)ceil((original_st[0] - map->start[0] - 2 * ON_EPSILON) / map->step[0]) - 1;
		ismax = (int)floor((original_st[0] - map->start[0] + 2 * ON_EPSILON) / map->step[0]);
		itmin = qmax(0, itmin);
		itmax = qmin(itmax, map->h - 1);
		ismin = qmax(0, ismin);
		ismax = qmin(ismax, map->w - 1);

		found = false;
		bool best_nudged = true;
		for (it = itmin; it <= itmax; it++)
		{
			for (is = ismin; is <= ismax; is++)
			{
				position_t* p;
				vec3_t current_st;
				vec_t d;

				p = &map->grid[is + map->w * it];
				if (!p->valid)
				{
					continue;
				}
				current_st[0] = p->best_s;
				current_st[1] = p->best_t;
				current_st[2] = 0.0;

				VectorSubtract(current_st, original_st, v);
				d = VectorLength(v);

				if (!found ||
					!p->nudged && best_nudged ||
					p->nudged == best_nudged
					&&
					d < best_dist - 2 * ON_EPSILON)
				{
					found = true;
					best_is = is;
					best_it = it;
					best_dist = d;
					best_nudged = p->nudged;
				}
			}
		}

		if (found)
		{
			position_t* p;

			p = &map->grid[best_is + map->w * best_it];
			VectorCopy(p->pos, pos);
			*best_s = p->best_s;
			*best_t = p->best_t;
			*dist = 0.0;
			*nudged = p->nudged;
			return true;
		}
	}
	*nudged = true;

	itmin = map->h;
	itmax = -1;
	ismin = map->w;
	ismax = -1;
	for (x = 0; x < texwinding->m_NumPoints; x++)
	{
		it = (int)floor((texwinding->m_Points[x][1] - map->start[1] + 0.5 * ON_EPSILON) / map->step[1]);
		itmin = qmin(itmin, it);
		it = (int)ceil((texwinding->m_Points[x][1] - map->start[1] - 0.5 * ON_EPSILON) / map->step[1]) - 1;
		itmax = qmax(it, itmax);
		is = (int)floor((texwinding->m_Points[x][0] - map->start[0] + 0.5 * ON_EPSILON) / map->step[0]);
		ismin = qmin(ismin, is);
		is = (int)ceil((texwinding->m_Points[x][0] - map->start[0] - 0.5 * ON_EPSILON) / map->step[0]) - 1;
		ismax = qmax(is, ismax);
	}
	itmin = qmax(0, itmin);
	itmax = qmin(itmax, map->h - 1);
	ismin = qmax(0, ismin);
	ismax = qmin(ismax, map->w - 1);

	found = false;
	for (it = itmin; it <= itmax; it++)
	{
		for (is = ismin; is <= ismax; is++)
		{
			position_t* p;
			vec3_t current_st;
			vec_t d;

			p = &map->grid[is + map->w * it];
			if (!p->valid)
			{
				continue;
			}
			current_st[0] = p->best_s;
			current_st[1] = p->best_t;
			current_st[2] = 0.0;

			VectorSubtract(current_st, original_st, v);
			d = VectorLength(v);

			if (!found || d < best_dist - ON_EPSILON)
			{
				found = true;
				best_is = is;
				best_it = it;
				best_dist = d;
			}
		}
	}

	if (found)
	{
		position_t* p;

		p = &map->grid[best_is + map->w * best_it];
		VectorCopy(p->pos, pos);
		*best_s = p->best_s;
		*best_t = p->best_t;
		*dist = best_dist;
		return true;
	}

	return false;
}

static void SetSurfFromST(const lightinfo_t* const l, vec_t* surf, const vec_t s, const vec_t t)
{
	const int       facenum = l->surfnum;
	int             j;

	for (j = 0; j < 3; j++)
	{
		surf[j] = l->texorg[j] + l->textoworld[0][j] * s + l->textoworld[1][j] * t;
	}

	// Adjust for origin-based models
	VectorAdd(surf, g_face_offset[facenum], surf);
}

static void DeleteSampleFrag(samplefraginfo_t* fraginfo)
{
	while (fraginfo->head)
	{
		samplefrag_t* f;

		f = fraginfo->head;
		fraginfo->head = f->next;
		delete f->mywinding;
		delete f->winding;
		free(f->edges);
		free(f);
	}
	free(fraginfo);
}

static light_flag_t SetSampleFromST(vec_t* const point,
	vec_t* const position, // a valid world position for light tracing
	int* const surface, // the face used for phong normal and patch interpolation
	bool* nudged,
	const lightinfo_t* const l, const vec_t original_s, const vec_t original_t,
	const vec_t square[2][2], // {smin, tmin}, {smax, tmax}
	eModelLightmodes lightmode)
{
	light_flag_t LuxelFlag;
	int facenum;
	dface_t* face;
	const dplane_t* faceplane;
	samplefraginfo_t* fraginfo;
	samplefrag_t* f;

	facenum = l->surfnum;
	face = l->face;
	faceplane = getPlaneFromFace(face);

	fraginfo = CreateSampleFrag(facenum, original_s, original_t,
		square,
		100);

	bool found;
	samplefrag_t* bestfrag;
	vec3_t bestpos;
	vec_t bests, bestt;
	vec_t best_dist;
	bool best_nudged;

	found = false;
	for (f = fraginfo->head; f; f = f->next)
	{
		vec3_t pos;
		vec_t s, t;
		vec_t dist;

		bool nudged_one;
		if (!FindNearestPosition(f->facenum, f->mywinding, f->mywindingplane, f->myorigin[0], f->myorigin[1], pos, &s, &t, &dist
			, &nudged_one
		))
		{
			continue;
		}

		bool better;

		if (!found)
		{
			better = true;
		}
		else if (nudged_one != best_nudged)
		{
			better = !nudged_one;
		}
		else if (fabs(dist - best_dist) > 2 * ON_EPSILON)
		{
			better = (dist < best_dist);
		}
		else if (f->noseam != bestfrag->noseam)
		{
			better = (f->noseam && !bestfrag->noseam);
		}
		else
		{
			better = (f->facenum < bestfrag->facenum);
		}

		if (better)
		{
			found = true;
			bestfrag = f;
			VectorCopy(pos, bestpos);
			bests = s;
			bestt = t;
			best_dist = dist;
			best_nudged = nudged_one;
		}
	}

	if (found)
	{
		matrix_t worldtotex, textoworld;
		vec3_t tex;

		TranslateWorldToTex(bestfrag->facenum, worldtotex);
		if (!InvertMatrix(worldtotex, textoworld))
		{
			const unsigned facenum = bestfrag->facenum;
			//ThreadLock();
			Log("Malformed face (%d) normal @ \n", facenum);
			Winding* w = new Winding(g_dfaces[facenum]);
			for (int x = 0; x < w->m_NumPoints; x++)
			{
				VectorAdd(w->m_Points[x], g_face_offset[facenum], w->m_Points[x]);
			}
			//w->Print();
			delete w;
			//ThreadUnlock();
			hlassume(false, assume_MalformedTextureFace);
		}

		// point
		tex[0] = bests;
		tex[1] = bestt;
		tex[2] = 0.0;
		{vec3_t v; ApplyMatrix(textoworld, tex, v); VectorCopy(v, point); }
		VectorAdd(point, g_face_offset[bestfrag->facenum], point);
		// position
		VectorCopy(bestpos, position);
		// surface
		*surface = bestfrag->facenum;
		// whether nudged to fit
		*nudged = best_nudged;
		// returned value
		LuxelFlag = LightNormal;
	}
	else
	{
		SetSurfFromST(l, point, original_s, original_t);
		VectorMA(point, DEFAULT_HUNT_OFFSET, faceplane->normal, position);
		*surface = facenum;
		*nudged = true;
		LuxelFlag = LightOutside;
	}

	DeleteSampleFrag(fraginfo);

	return LuxelFlag;

}

float CalculatePointVecsProduct(const volatile float* point, const volatile float* vecs)
{
	volatile double val;
	volatile double tmp;

	val = (double)point[0] * (double)vecs[0]; // always do one operation at a time and save to memory
	tmp = (double)point[1] * (double)vecs[1];
	val = val + tmp;
	tmp = (double)point[2] * (double)vecs[2];
	val = val + tmp;
	val = val + (double)vecs[3];

	return (float)val;
}

void GetFaceExtents(int facenum, int mins_out[2], int maxs_out[2])
{
	//CorrectFPUPrecision();

	dface_t* f;
	float mins[2], maxs[2], val;
	int i, j, e;
	dvertex_t* v;
	texinfo_t* tex;
	int bmins[2], bmaxs[2];

	f = &g_dfaces[facenum];

	mins[0] = mins[1] = 999999;
	maxs[0] = maxs[1] = -99999;

	tex = &g_texinfo[f->texinfo];

	for (i = 0; i < f->numedges; i++)
	{
		e = g_dsurfedges[f->firstedge + i];
		if (e >= 0)
		{
			v = &g_dvertexes[g_dedges[e].v[0]];
		}
		else
		{
			v = &g_dvertexes[g_dedges[-e].v[1]];
		}
		for (j = 0; j < 2; j++)
		{
			// The old code: val = v->point[0] * tex->vecs[j][0] + v->point[1] * tex->vecs[j][1] + v->point[2] * tex->vecs[j][2] + tex->vecs[j][3];
			//   was meant to be compiled for x86 under MSVC (prior to VS 11), so the intermediate values were stored as 64-bit double by default.
			// The new code will produce the same result as the old code, but it's portable for different platforms.
			// See this article for details: Intermediate Floating-Point Precision by Bruce-Dawson http://www.altdevblogaday.com/2012/03/22/intermediate-floating-point-precision/

			// The essential reason for having this ugly code is to get exactly the same value as the counterpart of game engine.
			// The counterpart of game engine is the function CalcFaceExtents in HLSDK.
			// So we must also know how Valve compiles HLSDK. I think Valve compiles HLSDK with VC6.0 in the past.
			val = CalculatePointVecsProduct(v->point, tex->vecs[j]);
			if (val < mins[j])
			{
				mins[j] = val;
			}
			if (val > maxs[j])
			{
				maxs[j] = val;
			}
		}
	}

	for (i = 0; i < 2; i++)
	{
		bmins[i] = (int)floor(mins[i] / TEXTURE_STEP);
		bmaxs[i] = (int)ceil(maxs[i] / TEXTURE_STEP);
	}

	for (i = 0; i < 2; i++)
	{
		mins_out[i] = bmins[i];
		maxs_out[i] = bmaxs[i];
	}
}

void CalcFaceExtents(lightinfo_t* l)
{
	const int       facenum = l->surfnum;
	dface_t* s;
	float           mins[2], maxs[2], val; //vec_t           mins[2], maxs[2], val; //vluzacn
	int             i, j, e;
	dvertex_t* v;
	texinfo_t* tex;

	s = l->face;

	mins[0] = mins[1] = 99999999;
	maxs[0] = maxs[1] = -99999999;

	tex = &g_texinfo[s->texinfo];

	for (i = 0; i < s->numedges; i++)
	{
		e = g_dsurfedges[s->firstedge + i];
		if (e >= 0)
		{
			v = g_dvertexes + g_dedges[e].v[0];
		}
		else
		{
			v = g_dvertexes + g_dedges[-e].v[1];
		}

		for (j = 0; j < 2; j++)
		{
			val = v->point[0] * tex->vecs[j][0] +
				v->point[1] * tex->vecs[j][1] + v->point[2] * tex->vecs[j][2] + tex->vecs[j][3];
			if (val < mins[j])
			{
				mins[j] = val;
			}
			if (val > maxs[j])
			{
				maxs[j] = val;
			}
		}
	}

	for (i = 0; i < 2; i++)
	{
		l->exactmins[i] = mins[i];
		l->exactmaxs[i] = maxs[i];

	}
	int bmins[2];
	int bmaxs[2];
	GetFaceExtents(l->surfnum, bmins, bmaxs);
	for (i = 0; i < 2; i++)
	{
		mins[i] = bmins[i];
		maxs[i] = bmaxs[i];
		l->texmins[i] = bmins[i];
		l->texsize[i] = bmaxs[i] - bmins[i];
	}

	if (!(tex->flags & TEX_SPECIAL))
	{
		if ((l->texsize[0] > MAX_SURFACE_EXTENT) || (l->texsize[1] > MAX_SURFACE_EXTENT)
			|| l->texsize[0] < 0 || l->texsize[1] < 0 //--vluzacn
			)
		{
			//ThreadLock();
			//PrintOnce("\nfor Face %d (texture %s) at ", s - g_dfaces, TextureNameFromFace(s));

			for (i = 0; i < s->numedges; i++)
			{
				e = g_dsurfedges[s->firstedge + i];
				if (e >= 0)
				{
					v = g_dvertexes + g_dedges[e].v[0];
				}
				else
				{
					v = g_dvertexes + g_dedges[-e].v[1];
				}
				vec3_t pos;
				VectorAdd(v->point, g_face_offset[facenum], pos);
				Log("(%4.3f %4.3f %4.3f) ", pos[0], pos[1], pos[2]);
			}
			Log("\n");

			Error("Bad surface extents (%d x %d)\nCheck the file ZHLTProblems.html for a detailed explanation of this problem", l->texsize[0], l->texsize[1]);
		}
	}
	// allocate sample light cache
	{
		/*
		if (g_extra
			&& !g_fastmode
			)
		{
		*/
			l->lmcache_density = 3;
			/*
		}
		else
		{
			l->lmcache_density = 1;
		}
		*/
		l->lmcache_side = (int)ceil((0.5 * g_blur * l->lmcache_density - 0.5) * (1 - NORMAL_EPSILON));
		l->lmcache_offset = l->lmcache_side;
		l->lmcachewidth = l->texsize[0] * l->lmcache_density + 1 + 2 * l->lmcache_side;
		l->lmcacheheight = l->texsize[1] * l->lmcache_density + 1 + 2 * l->lmcache_side;
		l->lmcache = (vec3_t(*)[ALLSTYLES])malloc(l->lmcachewidth * l->lmcacheheight * sizeof(vec3_t[ALLSTYLES]));
		hlassume(l->lmcache != NULL, assume_NoMemory);
		l->lmcache_normal = (vec3_t*)malloc(l->lmcachewidth * l->lmcacheheight * sizeof(vec3_t));
		hlassume(l->lmcache_normal != NULL, assume_NoMemory);
		l->lmcache_wallflags = (int*)malloc(l->lmcachewidth * l->lmcacheheight * sizeof(int));
		hlassume(l->lmcache_wallflags != NULL, assume_NoMemory);
		l->surfpt_position = (vec3_t*)malloc(MAX_SINGLEMAP * sizeof(vec3_t));
		l->surfpt_surface = (int*)malloc(MAX_SINGLEMAP * sizeof(int));
		hlassume(l->surfpt_position != NULL && l->surfpt_surface != NULL, assume_NoMemory);
	}
}

void CalcFaceVectors(lightinfo_t* l)
{
	texinfo_t* tex;
	int             i, j;
	vec3_t          texnormal;
	vec_t           distscale;
	vec_t           dist, len;

	tex = &g_texinfo[l->face->texinfo];

	// convert from float to double
	for (i = 0; i < 2; i++)
	{
		for (j = 0; j < 3; j++)
		{
			l->worldtotex[i][j] = tex->vecs[i][j];
		}
	}

	// calculate a normal to the texture axis.  points can be moved along this
	// without changing their S/T
	CrossProduct(tex->vecs[1], tex->vecs[0], texnormal);
	VectorNormalize(texnormal);

	// flip it towards plane normal
	distscale = DotProduct(texnormal, l->facenormal);
	if (distscale == 0.0)
	{
		const unsigned facenum = l->face - g_dfaces;

		//ThreadLock();
		Log("Malformed face (%d) normal @ \n", facenum);
		Winding* w = new Winding(*l->face);
		{
			const unsigned numpoints = w->m_NumPoints;
			unsigned x;
			for (x = 0; x < numpoints; x++)
			{
				VectorAdd(w->m_Points[x], g_face_offset[facenum], w->m_Points[x]);
			}
		}
		//w->Print();
		delete w;
		//ThreadUnlock();

		hlassume(false, assume_MalformedTextureFace);
	}

	if (distscale < 0)
	{
		distscale = -distscale;
		VectorSubtract(vec3_origin, texnormal, texnormal);
	}

	// distscale is the ratio of the distance along the texture normal to
	// the distance along the plane normal
	distscale = 1.0 / distscale;

	for (i = 0; i < 2; i++)
	{
		CrossProduct(l->worldtotex[!i], l->facenormal, l->textoworld[i]);
		len = DotProduct(l->textoworld[i], l->worldtotex[i]);
		VectorScale(l->textoworld[i], 1 / len, l->textoworld[i]);
	}

	// calculate texorg on the texture plane
	for (i = 0; i < 3; i++)
	{
		l->texorg[i] = -tex->vecs[0][3] * l->textoworld[0][i] - tex->vecs[1][3] * l->textoworld[1][i];
	}

	// project back to the face plane
	dist = DotProduct(l->texorg, l->facenormal) - l->facedist;
	dist *= distscale;
	VectorMA(l->texorg, -dist, texnormal, l->texorg);
	VectorCopy(texnormal, l->texnormal);

}

void CalcPoints(lightinfo_t* l, light_flag_t* LuxelFlags)
{
	const int       facenum = l->surfnum;
	const dface_t* f = g_dfaces + facenum;
	const dplane_t* p = getPlaneFromFace(f);
	const vec_t* face_delta = g_face_offset[facenum];
	const eModelLightmodes lightmode = g_face_lightmode[facenum];
	const int       h = l->texsize[1] + 1;
	const int       w = l->texsize[0] + 1;
	const vec_t     starts = l->texmins[0] * TEXTURE_STEP;
	const vec_t     startt = l->texmins[1] * TEXTURE_STEP;
	//light_flag_t    LuxelFlags[MAX_SINGLEMAP];
	light_flag_t* pLuxelFlags;
	vec_t           us, ut;
	vec_t* surf;
	int             s, t;
	l->numsurfpt = w * h;

	for (t = 0; t < h; t++)
	{
		for (s = 0; s < w; s++)
		{
			surf = l->surfpt[s + w * t];
			pLuxelFlags = &LuxelFlags[s + w * t];
			us = starts + s * TEXTURE_STEP;
			ut = startt + t * TEXTURE_STEP;
			vec_t square[2][2];
			square[0][0] = us - TEXTURE_STEP;
			square[0][1] = ut - TEXTURE_STEP;
			square[1][0] = us + TEXTURE_STEP;
			square[1][1] = ut + TEXTURE_STEP;
			bool nudged;

			*pLuxelFlags = SetSampleFromST(surf,
				l->surfpt_position[s + w * t], &l->surfpt_surface[s + w * t],
				&nudged,
				l, us, ut,
				square,
				lightmode);
		}
	}


	{
		int i, n;
		int s_other, t_other;
		light_flag_t* pLuxelFlags_other;
		vec_t* surf_other;
		bool adjusted;
		for (i = 0; i < h + w; i++)
		{ // propagate valid light samples
			adjusted = false;
			for (t = 0; t < h; t++)
			{
				for (s = 0; s < w; s++)
				{
					surf = l->surfpt[s + w * t];
					pLuxelFlags = &LuxelFlags[s + w * t];
					if (*pLuxelFlags != LightOutside)
						continue;
					for (n = 0; n < 4; n++)
					{
						switch (n)
						{
						case 0: s_other = s + 1; t_other = t; break;
						case 1: s_other = s - 1; t_other = t; break;
						case 2: s_other = s; t_other = t + 1; break;
						case 3: s_other = s; t_other = t - 1; break;
						}
						if (t_other < 0 || t_other >= h || s_other < 0 || s_other >= w)
							continue;
						surf_other = l->surfpt[s_other + w * t_other];
						pLuxelFlags_other = &LuxelFlags[s_other + w * t_other];
						if (*pLuxelFlags_other != LightOutside && *pLuxelFlags_other != LightShifted)
						{
							*pLuxelFlags = LightShifted;
							VectorCopy(surf_other, surf);
							VectorCopy(l->surfpt_position[s_other + w * t_other], l->surfpt_position[s + w * t]);
							l->surfpt_surface[s + w * t] = l->surfpt_surface[s_other + w * t_other];
							adjusted = true;
							break;
						}
					}
				}
			}
			for (t = 0; t < h; t++)
			{
				for (s = 0; s < w; s++)
				{
					pLuxelFlags = &LuxelFlags[s + w * t];
					if (*pLuxelFlags == LightShifted)
					{
						*pLuxelFlags = LightShiftedInside;
					}
				}
			}
			if (!adjusted)
				break;
		}
	}
	for (int i = 0; i < MAX_SINGLEMAP; i++)
	{
		l->surfpt_lightoutside[i] = (LuxelFlags[i] == LightOutside);
	}
}