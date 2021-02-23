#include "rad.h"
#include "winding.h"
#include "Bsp.h"
#include <algorithm>

void qrad_get_lightmap_flags(Bsp* bsp, int faceIdx, byte* luxelFlagsOut) {

	BSPFACE* f = &bsp->faces[faceIdx];

	if (f->nStyles[0] == 255 || bsp->texinfos[f->iTextureInfo].nFlags & TEX_SPECIAL)
		return;                                            // non-lit texture

	lightinfo_t l;
	memset(&l, 0, sizeof(l));
	l.surfnum = faceIdx;
	l.face = f;

	CalcFaceExtents(bsp, &l);
	CalcPoints(bsp, &l, luxelFlagsOut);

	return;
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

const BSPPLANE getPlaneFromFace(Bsp* bsp, const BSPFACE* const face)
{
	if (!face)
	{
		logf("getPlaneFromFace() face was NULL\n");
		return BSPPLANE();
	}

	if (face->nPlaneSide)
	{
		BSPPLANE backplane = bsp->planes[face->iPlane];
		backplane.fDist = -backplane.fDist;
		backplane.vNormal = backplane.vNormal.invert();
		return backplane;
	}
	else
	{
		return bsp->planes[face->iPlane];
	}
}

void TranslateWorldToTex(Bsp* bsp, int facenum, matrix_t& m)
// without g_face_offset
{
	BSPFACE* f;
	BSPTEXTUREINFO* ti;
	
	int i;

	f = &bsp->faces[facenum];
	const BSPPLANE fp = getPlaneFromFace(bsp, f);
	ti = &bsp->texinfos[f->iTextureInfo];
	for (i = 0; i < 3; i++)
	{
		m.v[i][0] = ((vec_t*)&ti->vS)[i];
		m.v[i][1] = ((vec_t*)&ti->vT)[i];
	}
	m.v[0][2] = fp.vNormal.x;
	m.v[1][2] = fp.vNormal.y;
	m.v[2][2] = fp.vNormal.z;

	m.v[3][0] = ti->shiftS;
	m.v[3][1] = ti->shiftT;
	m.v[3][2] = -fp.fDist;
}

bool CanFindFacePosition(Bsp* bsp, int facenum)
{
	vec_t texmins[2], texmaxs[2];
	int imins[2], imaxs[2];

	matrix_t worldtotex;
	matrix_t textoworld;

	BSPFACE* f = &bsp->faces[facenum];
	if (bsp->texinfos[f->iTextureInfo].nFlags & TEX_SPECIAL)
	{
		return false;
	}

	TranslateWorldToTex(bsp, facenum, worldtotex);
	if (!InvertMatrix(worldtotex, textoworld))
	{
		return false;
	}

	Winding facewinding(bsp, *f);
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

static bool TestSampleFrag(Bsp* bsp, int facenum, vec_t s, vec_t t, const vec_t square[2][2], int maxsize)
{
	const vec3_t v_s = { 1, 0, 0 };
	const vec3_t v_t = { 0, 1, 0 };

	samplefrag_t head;

	head.facenum = facenum;

	VectorScale(v_s, 1, (vec_t*)&head.rect.planes[0].vNormal); head.rect.planes[0].fDist = square[0][0]; // smin
	VectorScale(v_s, -1, (vec_t*)&head.rect.planes[1].vNormal); head.rect.planes[1].fDist = -square[1][0]; // smax
	VectorScale(v_t, 1, (vec_t*)&head.rect.planes[2].vNormal); head.rect.planes[2].fDist = square[0][1]; // tmin
	VectorScale(v_t, -1, (vec_t*)&head.rect.planes[3].vNormal); head.rect.planes[3].fDist = -square[1][1]; // tmax

	// ChopFrag
	// get the shape of the fragment by clipping the face using the boundaries
	matrix_t worldtotex;
	BSPFACE* f = &bsp->faces[head.facenum];
	Winding facewinding(bsp, *f);

	TranslateWorldToTex(bsp, head.facenum, worldtotex);
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

	return hasPoints && CanFindFacePosition(bsp, head.facenum);
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

bool GetFaceLightmapSize(Bsp* bsp, int facenum, int size[2]) {
	int mins[2];
	int maxs[2];

	GetFaceExtents(bsp, facenum, mins, maxs);

	size[0] = (maxs[0] - mins[0]);
	size[1] = (maxs[1] - mins[1]);

	bool badSurfaceExtents = false;
	if ((size[0] > MAX_SURFACE_EXTENT) || (size[1] > MAX_SURFACE_EXTENT) || size[0] < 0 || size[1] < 0)
	{
		//logf("Bad surface extents (%d x %d)\n", size[0], size[1]);
		size[0] = min(size[0], MAX_SURFACE_EXTENT);
		size[1] = min(size[1], MAX_SURFACE_EXTENT);
		badSurfaceExtents = true;
	}

	size[0] += 1;
	size[1] += 1;

	return !badSurfaceExtents;
}

int GetFaceLightmapSizeBytes(Bsp* bsp, int facenum) {
	int size[2];
	GetFaceLightmapSize(bsp, facenum, size);
	BSPFACE& face = bsp->faces[facenum];

	int lightmapCount = 0;
	for (int k = 0; k < 4; k++) {
		lightmapCount += face.nStyles[k] != 255;
	}
	return size[0] * size[1] * lightmapCount * sizeof(COLOR3);
}

void GetFaceExtents(Bsp* bsp, int facenum, int mins_out[2], int maxs_out[2])
{
	//CorrectFPUPrecision();

	BSPFACE* f;
	float mins[2], maxs[2], val;
	int i, j, e;
	vec3* v;
	BSPTEXTUREINFO* tex;
	int bmins[2], bmaxs[2];

	f = &bsp->faces[facenum];

	mins[0] = mins[1] = FLT_MIN;
	maxs[0] = maxs[1] = FLT_MAX;

	tex = &bsp->texinfos[f->iTextureInfo];

	for (i = 0; i < f->nEdges; i++)
	{
		e = bsp->surfedges[f->iFirstEdge + i];
		if (e >= 0)
		{
			v = &bsp->verts[bsp->edges[e].iVertex[0]];
		}
		else
		{
			v = &bsp->verts[bsp->edges[-e].iVertex[1]];
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
			vec3& axis = j == 0 ? tex->vS : tex->vT;
			val = CalculatePointVecsProduct((vec_t*)v, (vec_t*)&axis);
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
		mins_out[i] = (int)floor(mins[i] / TEXTURE_STEP);
		maxs_out[i] = (int)ceil(maxs[i] / TEXTURE_STEP);
	}
}

void CalcFaceExtents(Bsp* bsp, lightinfo_t* l)
{
	const int       facenum = l->surfnum;
	BSPFACE* s;
	float           mins[2], maxs[2], val; //vec_t           mins[2], maxs[2], val; //vluzacn
	int             i, j, e;
	vec3* v;
	BSPTEXTUREINFO* tex;

	s = l->face;

	mins[0] = mins[1] = FLT_MIN;
	maxs[0] = maxs[1] = FLT_MAX;

	tex = &bsp->texinfos[s->iTextureInfo];

	for (i = 0; i < s->nEdges; i++)
	{
		e = bsp->surfedges[s->iFirstEdge + i];
		if (e >= 0)
		{
			v = bsp->verts + bsp->edges[e].iVertex[0];
		}
		else
		{
			v = bsp->verts + bsp->edges[-e].iVertex[1];
		}

		for (j = 0; j < 2; j++)
		{
			vec3& axis = j == 0 ? tex->vS : tex->vT;
			float shift = j == 0 ? tex->shiftS : tex->shiftT;

			val = v->x * axis.x + v->y * axis.y + v->z * axis.z + shift;
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
	GetFaceExtents(bsp, l->surfnum, bmins, bmaxs);
	for (i = 0; i < 2; i++)
	{
		mins[i] = bmins[i];
		maxs[i] = bmaxs[i];
		l->texmins[i] = bmins[i];
		l->texsize[i] = bmaxs[i] - bmins[i];
	}

	if (!(tex->nFlags & TEX_SPECIAL))
	{
		if ((l->texsize[0] > MAX_SURFACE_EXTENT) || (l->texsize[1] > MAX_SURFACE_EXTENT)
			|| l->texsize[0] < 0 || l->texsize[1] < 0 //--vluzacn
			)
		{
			//logf("Bad surface extents (%d x %d)\n", l->texsize[0], l->texsize[1]);
			l->texsize[0] = min(l->texsize[0], MAX_SURFACE_EXTENT);
			l->texsize[1] = min(l->texsize[1], MAX_SURFACE_EXTENT);
		}
	}
}

void CalcPoints(Bsp* bsp, lightinfo_t* l, byte* LuxelFlags)
{
	const int       facenum = l->surfnum;
	const BSPFACE* f = bsp->faces + facenum;
	const int       h = l->texsize[1] + 1;
	const int       w = l->texsize[0] + 1;
	const vec_t     starts = l->texmins[0] * TEXTURE_STEP;
	const vec_t     startt = l->texmins[1] * TEXTURE_STEP;
	byte* pLuxelFlags;

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

			*pLuxelFlags = TestSampleFrag(bsp, l->surfnum, us, ut, square, 100) ? LightNormal : LightOutside;
		}
	}


	{
		int s_other, t_other;
		byte* pLuxelFlags_other;
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