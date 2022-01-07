#pragma warning(disable: 4018) //amckern - 64bit - '<' Singed/Unsigned Mismatch

#include "winding.h"
#include "rad.h"
#include "Bsp.h"

#undef ON_EPSILON

#define ON_EPSILON epsilon

Winding& Winding::operator=(const Winding& other)
{
	if (&other == this)
		return *this;
	delete[] m_Points;
	m_NumPoints = other.m_NumPoints;
	m_MaxPoints = (m_NumPoints + 3) & ~3;   // groups of 4

	m_Points = new vec3_t[m_MaxPoints];
	memcpy(m_Points, other.m_Points, sizeof(vec3_t) * m_NumPoints);
	return *this;
}

Winding::Winding(unsigned int numpoints)
{
	m_NumPoints = numpoints;
	m_MaxPoints = (m_NumPoints + 3) & ~3;   // groups of 4

	m_Points = new vec3_t[m_MaxPoints];
	memset(m_Points, 0, sizeof(vec3_t) * m_NumPoints);
}

Winding::Winding(const Winding& other)
{
	m_NumPoints = other.m_NumPoints;
	m_MaxPoints = (m_NumPoints + 3) & ~3;   // groups of 4

	m_Points = new vec3_t[m_MaxPoints];
	memcpy(m_Points, other.m_Points, sizeof(vec3_t) * m_NumPoints);
}

Winding::~Winding()
{
	delete[] m_Points;
}

Winding::Winding(Bsp* bsp, const BSPFACE& face, vec_t epsilon)
{
	int             se;
	vec3* dv;
	int             v;

	m_NumPoints = face.nEdges;
	m_Points = new vec3_t[m_NumPoints];

	unsigned i;
	for (i = 0; i < face.nEdges; i++)
	{
		se = bsp->surfedges[face.iFirstEdge + i];
		if (se < 0)
		{
			v = bsp->edges[-se].iVertex[1];
		}
		else
		{
			v = bsp->edges[se].iVertex[0];
		}

		dv = &bsp->verts[v];
		VectorCopy((vec_t*)dv, m_Points[i]);
	}

	RemoveColinearPoints(
		epsilon
	);
}

// Remove the colinear point of any three points that forms a triangle which is thinner than ON_EPSILON
void Winding::RemoveColinearPoints(vec_t epsilon)
{
	unsigned int	i;
	vec3_t			v1, v2;
	vec_t* p1, * p2, * p3;
	for (i = 0; i < m_NumPoints; i++)
	{
		p1 = m_Points[(i + m_NumPoints - 1) % m_NumPoints];
		p2 = m_Points[i];
		p3 = m_Points[(i + 1) % m_NumPoints];
		VectorSubtract(p2, p1, v1);
		VectorSubtract(p3, p2, v2);
		// v1 or v2 might be close to 0
		if (DotProduct(v1, v2) * DotProduct(v1, v2) >= DotProduct(v1, v1) * DotProduct(v2, v2)
			- ON_EPSILON * ON_EPSILON * (DotProduct(v1, v1) + DotProduct(v2, v2) + ON_EPSILON * ON_EPSILON))
			// v2 == k * v1 + v3 && abs (v3) < ON_EPSILON || v1 == k * v2 + v3 && abs (v3) < ON_EPSILON
		{
			m_NumPoints--;
			for (; i < m_NumPoints; i++)
			{
				VectorCopy(m_Points[i + 1], m_Points[i]);
			}
			i = -1;
			continue;
		}
	}
}

bool Winding::Clip(const BSPPLANE& split, bool keepon, vec_t epsilon)
{
	vec_t           dists[MAX_POINTS_ON_WINDING];
	int             sides[MAX_POINTS_ON_WINDING];
	int             counts[3];
	vec_t           dot;
	int             i, j;

	counts[0] = counts[1] = counts[2] = 0;

	// determine sides for each point
	// do this exactly, with no epsilon so tiny portals still work
	for (i = 0; i < m_NumPoints; i++)
	{
		dot = DotProduct(m_Points[i], ((vec_t*)&split.vNormal));
		dot -= split.fDist;
		dists[i] = dot;
		if (dot > ON_EPSILON)
		{
			sides[i] = SIDE_FRONT;
		}
		else if (dot < -ON_EPSILON)
		{
			sides[i] = SIDE_BACK;
		}
		else
		{
			sides[i] = SIDE_ON;
		}
		counts[sides[i]]++;
	}
	sides[i] = sides[0];
	dists[i] = dists[0];

	if (keepon && !counts[0] && !counts[1])
	{
		return true;
	}

	if (!counts[0])
	{
		delete[] m_Points;
		m_Points = NULL;
		m_NumPoints = 0;
		return false;
	}

	if (!counts[1])
	{
		return true;
	}

	unsigned maxpts = m_NumPoints + 4;                            // can't use counts[0]+2 because of fp grouping errors
	unsigned newNumPoints = 0;
	vec3_t* newPoints = new vec3_t[maxpts];
	memset(newPoints, 0, sizeof(vec3_t) * maxpts);

	for (i = 0; i < m_NumPoints; i++)
	{
		vec_t* p1 = m_Points[i];

		if (sides[i] == SIDE_ON)
		{
			VectorCopy(p1, newPoints[newNumPoints]);
			newNumPoints++;
			continue;
		}
		else if (sides[i] == SIDE_FRONT)
		{
			VectorCopy(p1, newPoints[newNumPoints]);
			newNumPoints++;
		}

		if (sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i])
		{
			continue;
		}

		// generate a split point
		vec3_t mid;
		unsigned int tmp = i + 1;
		if (tmp >= m_NumPoints)
		{
			tmp = 0;
		}
		vec_t* p2 = m_Points[tmp];
		dot = dists[i] / (dists[i] - dists[i + 1]);
		for (j = 0; j < 3; j++)
		{                                                  // avoid round off error when possible
			if (((vec_t*)&split.vNormal)[j] == 1)
				mid[j] = split.fDist;
			else if (((vec_t*)&split.vNormal)[j] == -1)
				mid[j] = -split.fDist;
			else
				mid[j] = p1[j] + dot * (p2[j] - p1[j]);
		}

		VectorCopy(mid, newPoints[newNumPoints]);
		newNumPoints++;
	}

	if (newNumPoints > maxpts)
	{
		logf("Winding::Clip : points exceeded estimate\n");
	}

	delete[] m_Points;
	m_Points = newPoints;
	m_NumPoints = newNumPoints;

	RemoveColinearPoints(
		epsilon
	);
	if (m_NumPoints == 0)
	{
		delete[] m_Points;
		m_Points = NULL;
		return false;
	}

	return true;
}
