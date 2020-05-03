#pragma warning(disable: 4018) //amckern - 64bit - '<' Singed/Unsigned Mismatch

#include "winding.h"
#include "rad.h"
#include "boundingbox.h"

#undef BOGUS_RANGE
#undef ON_EPSILON

#define	BOGUS_RANGE	80000.0
#define ON_EPSILON epsilon

void     Winding::getPlane(dplane_t& plane) const
{
    vec3_t          v1, v2;
    vec3_t          plane_normal;

    //hlassert(m_NumPoints >= 3);

    if (m_NumPoints >= 3)
    {
        VectorSubtract(m_Points[2], m_Points[1], v1);
        VectorSubtract(m_Points[0], m_Points[1], v2);

        CrossProduct(v2, v1, plane_normal);
        VectorNormalize(plane_normal);
        VectorCopy(plane_normal, plane.normal);               // change from vec_t
        plane.dist = DotProduct(m_Points[0], plane.normal);
    }
    else
    {
        VectorClear(plane.normal);
        plane.dist = 0.0;
    }
}

void            Winding::getPlane(vec3_t& normal, vec_t& dist) const
{
    vec3_t          v1, v2;

    //hlassert(m_NumPoints >= 3);

    if (m_NumPoints >= 3)
    {
        VectorSubtract(m_Points[1], m_Points[0], v1);
        VectorSubtract(m_Points[2], m_Points[0], v2);
        CrossProduct(v2, v1, normal);
        VectorNormalize(normal);
        dist = DotProduct(m_Points[0], normal);
    }
    else
    {
        VectorClear(normal);
        dist = 0.0;
    }
}

vec_t           Winding::getArea() const
{
    unsigned int    i;
    vec3_t          d1, d2, cross;
    vec_t           total;

    //hlassert(m_NumPoints >= 3);

    total = 0.0;
    if (m_NumPoints >= 3)
    {
        for (i = 2; i < m_NumPoints; i++)
        {
            VectorSubtract(m_Points[i - 1], m_Points[0], d1);
            VectorSubtract(m_Points[i], m_Points[0], d2);
            CrossProduct(d1, d2, cross);
            total += 0.5 * VectorLength(cross);
        }
    }
    return total;
}

void            Winding::getBounds(vec3_t& mins, vec3_t& maxs) const
{
    BoundingBox     bounds;
    unsigned    x;

    for (x=0; x<m_NumPoints; x++)
    {
        bounds.add(m_Points[x]);
    }
    VectorCopy(bounds.m_Mins, mins);
    VectorCopy(bounds.m_Maxs, maxs);
}

void            Winding::getBounds(BoundingBox& bounds) const
{
    bounds.reset();
    unsigned    x;

    for (x=0; x<m_NumPoints; x++)
    {
        bounds.add(m_Points[x]);
    }
}

void            Winding::getCenter(vec3_t& center) const
{
    unsigned int    i;
    vec_t           scale;

    if (m_NumPoints > 0)
    {
        VectorCopy(vec3_origin, center);
        for (i = 0; i < m_NumPoints; i++)
        {
            VectorAdd(m_Points[i], center, center);
        }

        scale = 1.0 / m_NumPoints;
        VectorScale(center, scale, center);
    }
    else
    {
        VectorClear(center);
    }
}

Winding*        Winding::Copy() const
{
    Winding* newWinding = new Winding(*this);
    return newWinding;
}

void            Winding::Check(
							   vec_t epsilon
							   ) const
{
    unsigned int    i, j;
    vec_t*          p1;
    vec_t*          p2;
    vec_t           d, edgedist;
    vec3_t          dir, edgenormal, facenormal;
    vec_t           area;
    vec_t           facedist;

    if (m_NumPoints < 3)
    {
        Error("Winding::Check : %i points", m_NumPoints);
    }

    area = getArea();
    if (area < 1)
    {
        Error("Winding::Check : %f area", area);
    }

    getPlane(facenormal, facedist);

    for (i = 0; i < m_NumPoints; i++)
    {
        p1 = m_Points[i];

        for (j = 0; j < 3; j++)
        {
            if (p1[j] > BOGUS_RANGE || p1[j] < -BOGUS_RANGE)
            {
                Error("Winding::Check : BOGUS_RANGE: %f", p1[j]);
            }
        }

        j = i + 1 == m_NumPoints ? 0 : i + 1;

        // check the point is on the face plane
        d = DotProduct(p1, facenormal) - facedist;
        if (d < -ON_EPSILON || d > ON_EPSILON)
        {
            Error("Winding::Check : point off plane");
        }

        // check the edge isn't degenerate
        p2 = m_Points[j];
        VectorSubtract(p2, p1, dir);

        if (VectorLength(dir) < ON_EPSILON)
        {
            Error("Winding::Check : degenerate edge");
        }

        CrossProduct(facenormal, dir, edgenormal);
        VectorNormalize(edgenormal);
        edgedist = DotProduct(p1, edgenormal);
        edgedist += ON_EPSILON;

        // all other points must be on front side
        for (j = 0; j < m_NumPoints; j++)
        {
            if (j == i)
            {
                continue;
            }
            d = DotProduct(m_Points[j], edgenormal);
            if (d > edgedist)
            {
                Error("Winding::Check : non-convex");
            }
        }
    }
}

bool          Winding::Valid() const
{
    // TODO: Check to ensure there are 3 non-colinear points
    if ((m_NumPoints < 3) || (!m_Points))
    {
        return false;
    }
    return true;
}

//
// Construction
//

Winding::Winding()
{
	m_Points = NULL;
	m_NumPoints = m_MaxPoints = 0;
}

Winding::Winding(vec3_t *points, uint32 numpoints)
{
	hlassert(numpoints >= 3);
	m_NumPoints = numpoints;
	m_MaxPoints = (m_NumPoints + 3) & ~3;	// groups of 4

	m_Points = new vec3_t[m_MaxPoints];
	memcpy(m_Points, points, sizeof(vec3_t) * m_NumPoints);
}

void			Winding::initFromPoints(vec3_t *points, uint32 numpoints)
{
	hlassert(numpoints >= 3);

	Reset();

	m_NumPoints = numpoints;
	m_MaxPoints = (m_NumPoints + 3) & ~3;	// groups of 4

	m_Points = new vec3_t[m_MaxPoints];
	memcpy(m_Points, points, sizeof(vec3_t) * m_NumPoints);
}

Winding&      Winding::operator=(const Winding& other)
{
    delete[] m_Points;
    m_NumPoints = other.m_NumPoints;
    m_MaxPoints = (m_NumPoints + 3) & ~3;   // groups of 4

    m_Points = new vec3_t[m_MaxPoints];
    memcpy(m_Points, other.m_Points, sizeof(vec3_t) * m_NumPoints);
    return *this;
}


Winding::Winding(uint32 numpoints)
{
    hlassert(numpoints >= 3);
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


void Winding::initFromPlane(const vec3_t normal, const vec_t dist)
{
    int             i;
    vec_t           max, v;
    vec3_t          org, vright, vup;

    // find the major axis               

    max = -BOGUS_RANGE;
    int x = -1;
    for (i = 0; i < 3; i++)          
    {
        v = fabs(normal[i]);        
        if (v > max)
        {
            max = v;
            x = i;
        }
    }                
    if (x == -1)
    {
        Error("Winding::initFromPlane no major axis found\n");
    }

    VectorCopy(vec3_origin, vup);
    switch (x) 
    {
    case 0:
    case 1:          
        vup[2] = 1;
        break;
    case 2:
        vup[0] = 1;      
        break;
    }

    v = DotProduct(vup, normal);
    VectorMA(vup, -v, normal, vup);
    VectorNormalize(vup);

    VectorScale(normal, dist, org);

    CrossProduct(vup, normal, vright);

    VectorScale(vup, BOGUS_RANGE, vup);
    VectorScale(vright, BOGUS_RANGE, vright);

    // project a really big     axis aligned box onto the plane
    m_NumPoints = 4;
    m_Points = new vec3_t[m_NumPoints];

    VectorSubtract(org, vright, m_Points[0]);
    VectorAdd(m_Points[0], vup, m_Points[0]);

    VectorAdd(org, vright, m_Points[1]);
    VectorAdd(m_Points[1], vup, m_Points[1]);

    VectorAdd(org, vright, m_Points[2]);
    VectorSubtract(m_Points[2], vup, m_Points[2]);

    VectorSubtract(org, vright, m_Points[3]);
    VectorSubtract(m_Points[3], vup, m_Points[3]);
}

Winding::Winding(const vec3_t normal, const vec_t dist)
{
    initFromPlane(normal, dist);
}

Winding::Winding(const dface_t& face, vec_t epsilon)
{
    int             se;
    dvertex_t*      dv;
    int             v;

    m_NumPoints = face.numedges;
    m_Points = new vec3_t[m_NumPoints];

    unsigned i;
    for (i = 0; i < face.numedges; i++)
    {
        se = g_dsurfedges[face.firstedge + i];
        if (se < 0)
        {
            v = g_dedges[-se].v[1];
        }
        else
        {
            v = g_dedges[se].v[0];
        }

        dv = &g_dvertexes[v];
        VectorCopy(dv->point, m_Points[i]);
    }

    RemoveColinearPoints(
		epsilon
		);
}

Winding::Winding(const dplane_t& plane)
{
    vec3_t normal;
    vec_t dist;

    VectorCopy(plane.normal, normal);
    dist = plane.dist;
    initFromPlane(normal, dist);
}

//
// Specialized Functions
//

// Remove the colinear point of any three points that forms a triangle which is thinner than ON_EPSILON
void			Winding::RemoveColinearPoints(
											  vec_t epsilon
											  )
{
	unsigned int	i;
	vec3_t			v1, v2;
	vec_t			*p1, *p2, *p3;
	for (i = 0; i < m_NumPoints; i++)
	{
		p1 = m_Points[(i+m_NumPoints-1)%m_NumPoints];
		p2 = m_Points[i];
		p3 = m_Points[(i+1)%m_NumPoints];
		VectorSubtract (p2, p1, v1);
		VectorSubtract (p3, p2, v2);
		// v1 or v2 might be close to 0
		if (DotProduct (v1, v2) * DotProduct (v1, v2) >= DotProduct (v1, v1) * DotProduct (v2, v2) 
			- ON_EPSILON * ON_EPSILON * (DotProduct (v1, v1) + DotProduct (v2, v2) + ON_EPSILON * ON_EPSILON))
			// v2 == k * v1 + v3 && abs (v3) < ON_EPSILON || v1 == k * v2 + v3 && abs (v3) < ON_EPSILON
		{
			m_NumPoints--;
			for (; i < m_NumPoints; i++)
			{
				VectorCopy (m_Points[i+1], m_Points[i]);
			}
			i = -1;
			continue;
		}
	}
}


void            Winding::Clip(const dplane_t& plane, Winding** front, Winding** back
							  , vec_t epsilon
							  )
{
    vec3_t normal;
    vec_t dist;
    VectorCopy(plane.normal, normal);
    dist = plane.dist;
    Clip(normal, dist, front, back
		, epsilon
		);
}


void            Winding::Clip(const vec3_t normal, const vec_t dist, Winding** front, Winding** back
							  , vec_t epsilon
							  )
{
    vec_t           dists[MAX_POINTS_ON_WINDING + 4];
    int             sides[MAX_POINTS_ON_WINDING + 4];
    int             counts[3];
    vec_t           dot;
    unsigned int    i, j;
    unsigned int    maxpts;

    counts[0] = counts[1] = counts[2] = 0;

    // determine sides for each point
    for (i = 0; i < m_NumPoints; i++)
    {
        dot = DotProduct(m_Points[i], normal);
        dot -= dist;
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

    if (!counts[0])
    {
        *front = NULL;
        *back = new Winding(*this);
        return;
    }
    if (!counts[1])
    {
        *front = new Winding(*this);
        *back = NULL;
        return;
    }

    maxpts = m_NumPoints + 4;                            // can't use counts[0]+2 because
    // of fp grouping errors

    Winding* f = new Winding(maxpts);
    Winding* b = new Winding(maxpts);

    *front = f;
    *back = b;

    f->m_NumPoints = 0;
    b->m_NumPoints = 0;

    for (i = 0; i < m_NumPoints; i++)
    {
        vec_t* p1 = m_Points[i];

        if (sides[i] == SIDE_ON)
        {
            VectorCopy(p1, f->m_Points[f->m_NumPoints]);
            VectorCopy(p1, b->m_Points[b->m_NumPoints]);
            f->m_NumPoints++;
            b->m_NumPoints++;
            continue;
        }
        else if (sides[i] == SIDE_FRONT)
        {
            VectorCopy(p1, f->m_Points[f->m_NumPoints]);
            f->m_NumPoints++;
        }
        else if (sides[i] == SIDE_BACK)
        {
            VectorCopy(p1, b->m_Points[b->m_NumPoints]);
            b->m_NumPoints++;
        }

        if ((sides[i + 1] == SIDE_ON) | (sides[i + 1] == sides[i]))  // | instead of || for branch optimization
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
#if 0
        for (j = 0; j < 3; j++)
        {                                                  // avoid round off error when possible
            if (normal[j] < 1.0 - NORMAL_EPSILON)
            {
                if (normal[j] > -1.0 + NORMAL_EPSILON)
                {
                    mid[j] = p1[j] + dot * (p2[j] - p1[j]);
                }
                else
                {
                    mid[j] = -dist;
                }
            }
            else
            {
                mid[j] = dist;
            }
        }
#else
        for (j = 0; j < 3; j++)
        {                                                  // avoid round off error when possible
            if (normal[j] == 1)
                mid[j] = dist;
            else if (normal[j] == -1)
                mid[j] = -dist;
            else
                mid[j] = p1[j] + dot * (p2[j] - p1[j]);
        }
#endif

        VectorCopy(mid, f->m_Points[f->m_NumPoints]);
        VectorCopy(mid, b->m_Points[b->m_NumPoints]);
        f->m_NumPoints++;
        b->m_NumPoints++;
    }

    if ((f->m_NumPoints > maxpts) | (b->m_NumPoints > maxpts)) // | instead of || for branch optimization
    {
        Error("Winding::Clip : points exceeded estimate");
    }
    if ((f->m_NumPoints > MAX_POINTS_ON_WINDING) | (b->m_NumPoints > MAX_POINTS_ON_WINDING)) // | instead of || for branch optimization
    {
        Error("Winding::Clip : MAX_POINTS_ON_WINDING");
    }
    f->RemoveColinearPoints(
		epsilon
		);
    b->RemoveColinearPoints(
		epsilon
		);
	if (f->m_NumPoints == 0)
	{
		delete f;
		*front = NULL;
	}
	if (b->m_NumPoints == 0)
	{
		delete b;
		*back = NULL;
	}
}

bool          Winding::Chop(const vec3_t normal, const vec_t dist
							, vec_t epsilon
							)
{
    Winding*      f;
    Winding*      b;

    Clip(normal, dist, &f, &b
		, epsilon
		);
    if (b)
    {
        delete b;
    }

    if (f)
    {
        delete[] m_Points;
        m_NumPoints = f->m_NumPoints;
        m_Points = f->m_Points;
        f->m_Points = NULL;
        delete f;
        return true;
    }
    else
    {
        m_NumPoints = 0;
        delete[] m_Points;
        m_Points = NULL;
        return false;
    }
}

int             Winding::WindingOnPlaneSide(const vec3_t normal, const vec_t dist
											, vec_t epsilon
											)
{
    bool            front, back;
    unsigned int    i;
    vec_t           d;

    front = false;
    back = false;
    for (i = 0; i < m_NumPoints; i++)
    {
        d = DotProduct(m_Points[i], normal) - dist;
        if (d < -ON_EPSILON)
        {
            if (front)
            {
                return SIDE_CROSS;
            }
            back = true;
            continue;
        }
        if (d > ON_EPSILON)
        {
            if (back)
            {
                return SIDE_CROSS;
            }
            front = true;
            continue;
        }
    }

    if (back)
    {
        return SIDE_BACK;
    }
    if (front)
    {
        return SIDE_FRONT;
    }
    return SIDE_ON;
}


bool Winding::Clip(const dplane_t& split, bool keepon
				   , vec_t epsilon
				   )
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
        dot = DotProduct(m_Points[i], split.normal);
        dot -= split.dist;
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
            if (split.normal[j] == 1)
                mid[j] = split.dist;
            else if (split.normal[j] == -1)
                mid[j] = -split.dist;
            else
                mid[j] = p1[j] + dot * (p2[j] - p1[j]);
        }

        VectorCopy(mid, newPoints[newNumPoints]);
        newNumPoints++;
    }

    if (newNumPoints > maxpts)
    {
        Error("Winding::Clip : points exceeded estimate");
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
		m_NumPoints = 0;
		return false;
	}

    return true;
}

/*
 * ==================
 * Divide
 * Divides a winding by a plane, producing one or two windings.  The
 * original winding is not damaged or freed.  If only on one side, the
 * returned winding will be the input winding.  If on both sides, two
 * new windings will be created.
 * ==================
 */

void Winding::Divide(const dplane_t& split, Winding** front, Winding** back
					 , vec_t epsilon
					 )
{
    vec_t           dists[MAX_POINTS_ON_WINDING];
    int             sides[MAX_POINTS_ON_WINDING];
    int             counts[3];
    vec_t           dot;
    int             i, j;
    int             maxpts;

    counts[0] = counts[1] = counts[2] = 0;

    // determine sides for each point
    for (i = 0; i < m_NumPoints; i++)
    {
        dot = DotProduct(m_Points[i], split.normal);
        dot -= split.dist;
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

    *front = *back = NULL;

	if (!counts[0] && !counts[1])
	{
		vec_t sum = 0.0;
		for (i = 0; i < m_NumPoints; i++)
		{
			dot = DotProduct(m_Points[i], split.normal);
			dot -= split.dist;
			sum += dot;
		}
		if (sum > NORMAL_EPSILON)
		{
			*front = this;
		}
		else
		{
			*back = this;
		}
		return;
	}
    if (!counts[0])
    {
        *back = this;   // Makes this function non-const
        return;
    }
    if (!counts[1])
    {
        *front = this;  // Makes this function non-const
        return;
    }

    maxpts = m_NumPoints + 4;                            // can't use counts[0]+2 because
    // of fp grouping errors

    Winding* f = new Winding(maxpts);
    Winding* b = new Winding(maxpts);

    *front = f;
    *back = b;

    f->m_NumPoints = 0;
    b->m_NumPoints = 0;

    for (i = 0; i < m_NumPoints; i++)
    {
        vec_t* p1 = m_Points[i];

        if (sides[i] == SIDE_ON)
        {
            VectorCopy(p1, f->m_Points[f->m_NumPoints]);
            VectorCopy(p1, b->m_Points[b->m_NumPoints]);
            f->m_NumPoints++;
            b->m_NumPoints++;
            continue;
        }
        else if (sides[i] == SIDE_FRONT)
        {
            VectorCopy(p1, f->m_Points[f->m_NumPoints]);
            f->m_NumPoints++;
        }
        else if (sides[i] == SIDE_BACK)
        {
            VectorCopy(p1, b->m_Points[b->m_NumPoints]);
            b->m_NumPoints++;
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
            if (split.normal[j] == 1)
                mid[j] = split.dist;
            else if (split.normal[j] == -1)
                mid[j] = -split.dist;
            else
                mid[j] = p1[j] + dot * (p2[j] - p1[j]);
        }

        VectorCopy(mid, f->m_Points[f->m_NumPoints]);
        VectorCopy(mid, b->m_Points[b->m_NumPoints]);
        f->m_NumPoints++;
        b->m_NumPoints++;
    }

    if (f->m_NumPoints > maxpts || b->m_NumPoints > maxpts)
    {
        Error("Winding::Divide : points exceeded estimate");
    }

    f->RemoveColinearPoints(
		epsilon
		);
    b->RemoveColinearPoints(
		epsilon
		);
	if (f->m_NumPoints == 0)
	{
		delete f;
		delete b;
		*front = NULL;
		*back = this;
	}
	else if (b->m_NumPoints == 0)
	{
		delete f;
		delete b;
		*back = NULL;
		*front = this;
	}
}


void            Winding::addPoint(const vec3_t newpoint)
{
    if (m_NumPoints >= m_MaxPoints)
    {
        resize(m_NumPoints + 1);
    }
    VectorCopy(newpoint, m_Points[m_NumPoints]);
    m_NumPoints++;
}


void            Winding::insertPoint(const vec3_t newpoint, const unsigned int offset)
{
    if (offset >= m_NumPoints)
    {
        addPoint(newpoint);
    }
    else
    {
        if (m_NumPoints >= m_MaxPoints)
        {
            resize(m_NumPoints + 1);
        }

        unsigned x;
        for (x = m_NumPoints; x>offset; x--)
        {
            VectorCopy(m_Points[x-1], m_Points[x]);
        }
        VectorCopy(newpoint, m_Points[x]);

        m_NumPoints++;
    }
}


void            Winding::resize(uint32 newsize)
{
    newsize = (newsize + 3) & ~3;   // groups of 4

    vec3_t* newpoints = new vec3_t[newsize];
    m_NumPoints = qmin(newsize, m_NumPoints);
    memcpy(newpoints, m_Points, m_NumPoints);
    delete[] m_Points;
    m_Points = newpoints;
    m_MaxPoints = newsize;
}

void			Winding::CopyPoints(vec3_t *points, int &numpoints)
{
	if(!points)
	{
		numpoints = 0;
		return;
	}

	memcpy(points, m_Points, sizeof(vec3_t) * m_NumPoints);

	numpoints = m_NumPoints;
}

void			Winding::Reset(void)
{
	if(m_Points)
	{
		delete [] m_Points;
		m_Points = NULL;
	}

	m_NumPoints = m_MaxPoints = 0;
}