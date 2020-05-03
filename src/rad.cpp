#include "rad.h"
#include "winding.h"
#include "Bsp.h"

dface_t g_dfaces[MAX_MAP_FACES];
dplane_t g_dplanes[MAX_MAP_PLANES];
texinfo_t g_texinfo[MAX_MAP_TEXINFOS];
int g_dsurfedges[MAX_MAP_SURFEDGES];
dedge_t g_dedges[MAX_MAP_EDGES];
dvertex_t g_dvertexes[MAX_MAP_VERTS];
dplane_t backplanes[MAX_MAP_PLANES];
vec3_t g_face_offset[MAX_MAP_FACES];

const vec3_t vec3_origin = { 0, 0, 0 };


// fill out the global vars that the qrad compiler code requires
void qrad_init_globals(Bsp* bsp) {
	BSPPLANE* planes = (BSPPLANE*)bsp->lumps[LUMP_PLANES];
	BSPTEXTUREINFO* texInfo = (BSPTEXTUREINFO*)bsp->lumps[LUMP_TEXINFO];
	BSPMODEL* models = (BSPMODEL*)bsp->lumps[LUMP_MODELS];
	BSPFACE* faces = (BSPFACE*)bsp->lumps[LUMP_FACES];
	vec3* verts = (vec3*)bsp->lumps[LUMP_VERTICES];
	int32_t* surfEdges = (int32_t*)bsp->lumps[LUMP_SURFEDGES];
	BSPEDGE* edges = (BSPEDGE*)bsp->lumps[LUMP_EDGES];

	int planeCount = bsp->header.lump[LUMP_PLANES].nLength / sizeof(BSPPLANE);
	int texInfoCount = bsp->header.lump[LUMP_TEXINFO].nLength / sizeof(BSPTEXTUREINFO);
	int modelCount = bsp->header.lump[LUMP_MODELS].nLength / sizeof(BSPMODEL);
	int vertCount = bsp->header.lump[LUMP_VERTICES].nLength / sizeof(vec3);
	int faceCount = bsp->header.lump[LUMP_FACES].nLength / sizeof(BSPFACE);
	int surfedgeCount = bsp->header.lump[LUMP_SURFEDGES].nLength / sizeof(int32_t);
	int edgeCount = bsp->header.lump[LUMP_EDGES].nLength / sizeof(BSPEDGE);

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

	for (int i = 0; i < planeCount; i++) {
		g_dplanes[i].dist = planes[i].fDist;
		g_dplanes[i].normal[0] = planes[i].vNormal.x;
		g_dplanes[i].normal[1] = planes[i].vNormal.y;
		g_dplanes[i].normal[2] = planes[i].vNormal.z;
		g_dplanes[i].type = (planetypes)planes[i].nType;

		backplanes[i].dist = -g_dplanes[i].dist;
		VectorSubtract(vec3_origin, g_dplanes[i].normal, backplanes[i].normal);
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
			
			dface_t* f = g_dfaces + fn;
			Winding w(*f);

			for (int k = 0; k < w.m_NumPoints; k++)
			{
				VectorAdd(w.m_Points[k], origin, w.m_Points[k]);
			}
		}
	}
}

lightmap_flags_t qrad_get_lightmap_flags(Bsp* bsp, int faceIdx) {

	dface_t* f = &g_dfaces[faceIdx];

	lightmap_flags_t shift;
	memset(&shift, 0, sizeof(shift));

	if (g_texinfo[f->texinfo].flags & TEX_SPECIAL)
		return shift;                                            // non-lit texture

	lightinfo_t l;
	memset(&l, 0, sizeof(l));
	l.surfnum = faceIdx;
	l.face = f;

	CalcFaceExtents(&l);
	CalcPoints(&l, shift.luxelFlags);
	
	int w = l.texsize[0] + 1;
	int h = l.texsize[1] + 1;

	shift.w = w;
	shift.h = h;

	return shift;
}

//
// BEGIN COPIED QRAD CODE
//


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
		printf("getPlaneFromFace() face was NULL\n");
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

bool CanFindFacePosition(int facenum)
{
	vec_t texmins[2], texmaxs[2];
	int imins[2], imaxs[2];

	matrix_t worldtotex;
	matrix_t textoworld;

	dface_t* f = &g_dfaces[facenum];
	if (g_texinfo[f->texinfo].flags & TEX_SPECIAL)
	{
		return false;
	}

	TranslateWorldToTex(facenum, worldtotex);
	if (!InvertMatrix(worldtotex, textoworld))
	{
		return false;
	}

	Winding facewinding(*f);
	Winding texwinding(facewinding.m_NumPoints);
	for (int x = 0; x < facewinding.m_NumPoints; x++)
	{
		ApplyMatrix(worldtotex, facewinding.m_Points[x], texwinding.m_Points[x]);
		texwinding.m_Points[x][2] = 0.0;
	}
	texwinding.RemoveColinearPoints();

	if (texwinding.m_NumPoints == 0)
	{
		return false;
	}

	for (int x = 0; x < texwinding.m_NumPoints; x++)
	{
		for (int k = 0; k < 2; k++)
		{
			if (x == 0 || texwinding.m_Points[x][k] < texmins[k])
				texmins[k] = texwinding.m_Points[x][k];
			if (x == 0 || texwinding.m_Points[x][k] > texmaxs[k])
				texmaxs[k] = texwinding.m_Points[x][k];
		}
	}

	for (int k = 0; k < 2; k++)
	{
		imins[k] = (int)floor(texmins[k] / TEXTURE_STEP + 0.5 - ON_EPSILON);
		imaxs[k] = (int)ceil(texmaxs[k] / TEXTURE_STEP - 0.5 + ON_EPSILON);
	}

	int w = imaxs[0] - imins[0] + 1;
	int h = imaxs[1] - imins[1] + 1;
	if (w <= 0 || h <= 0 || (double)w * (double)h > 99999999)
	{
		return false;
	}
	return true;
}

static bool TestSampleFrag(int facenum, vec_t s, vec_t t, const vec_t square[2][2], int maxsize)
{
	const vec3_t v_s = { 1, 0, 0 };
	const vec3_t v_t = { 0, 1, 0 };

	samplefrag_t head;

	head.facenum = facenum;

	VectorScale(v_s, 1, head.rect.planes[0].normal); head.rect.planes[0].dist = square[0][0]; // smin
	VectorScale(v_s, -1, head.rect.planes[1].normal); head.rect.planes[1].dist = -square[1][0]; // smax
	VectorScale(v_t, 1, head.rect.planes[2].normal); head.rect.planes[2].dist = square[0][1]; // tmin
	VectorScale(v_t, -1, head.rect.planes[3].normal); head.rect.planes[3].dist = -square[1][1]; // tmax

	// ChopFrag
	// get the shape of the fragment by clipping the face using the boundaries
	matrix_t worldtotex;
	dface_t* f = &g_dfaces[head.facenum];
	Winding facewinding(*f);

	TranslateWorldToTex(head.facenum, worldtotex);
	head.mywinding = new Winding(facewinding.m_NumPoints);
	for (int x = 0; x < facewinding.m_NumPoints; x++)
	{
		ApplyMatrix(worldtotex, facewinding.m_Points[x], head.mywinding->m_Points[x]);
		head.mywinding->m_Points[x][2] = 0.0;
	}
	head.mywinding->RemoveColinearPoints();

	for (int x = 0; x < 4 && head.mywinding->m_NumPoints > 0; x++)
	{
		head.mywinding->Clip(head.rect.planes[x], false);
	}

	bool hasPoints = head.mywinding->m_NumPoints != 0;
	delete head.mywinding;

	return hasPoints && CanFindFacePosition(head.facenum);
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
			printf("Bad surface extents (%d x %d)\n", l->texsize[0], l->texsize[1]);
		}
	}

}

void CalcPoints(lightinfo_t* l, light_flag_t* LuxelFlags)
{
	const int       facenum = l->surfnum;
	const dface_t* f = g_dfaces + facenum;
	const dplane_t* p = getPlaneFromFace(f);
	const vec_t* face_delta = g_face_offset[facenum];
	const int       h = l->texsize[1] + 1;
	const int       w = l->texsize[0] + 1;
	const vec_t     starts = l->texmins[0] * TEXTURE_STEP;
	const vec_t     startt = l->texmins[1] * TEXTURE_STEP;
	light_flag_t* pLuxelFlags;

	for (int t = 0; t < h; t++)
	{
		for (int s = 0; s < w; s++)
		{
			pLuxelFlags = &LuxelFlags[s + w * t];
			vec_t us = starts + s * TEXTURE_STEP;
			vec_t ut = startt + t * TEXTURE_STEP;
			vec_t square[2][2];
			square[0][0] = us - TEXTURE_STEP;
			square[0][1] = ut - TEXTURE_STEP;
			square[1][0] = us + TEXTURE_STEP;
			square[1][1] = ut + TEXTURE_STEP;

			*pLuxelFlags = TestSampleFrag(l->surfnum, us, ut, square, 100) ? LightNormal : LightOutside;
		}
	}


	{
		int s_other, t_other;
		light_flag_t* pLuxelFlags_other;
		bool adjusted;
		for (int i = 0; i < h + w; i++)
		{ // propagate valid light samples
			adjusted = false;
			for (int t = 0; t < h; t++)
			{
				for (int s = 0; s < w; s++)
				{
					pLuxelFlags = &LuxelFlags[s + w * t];
					if (*pLuxelFlags != LightOutside)
						continue;
					for (int n = 0; n < 4; n++)
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
						pLuxelFlags_other = &LuxelFlags[s_other + w * t_other];
						if (*pLuxelFlags_other != LightOutside && *pLuxelFlags_other != LightShifted)
						{
							*pLuxelFlags = LightShifted;
							adjusted = true;
							break;
						}
					}
				}
			}
			for (int t = 0; t < h; t++)
			{
				for (int s = 0; s < w; s++)
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
}