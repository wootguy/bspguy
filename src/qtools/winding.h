#pragma once

#include "rad.h"

#define MAX_POINTS_ON_WINDING 128
// TODO: FIX THIS STUPID SHIT (MAX_POINTS_ON_WINDING)

#define	SIDE_FRONT		0
#define	SIDE_ON			2
#define	SIDE_BACK		1
#define	SIDE_CROSS		-2

struct BSPPLANE;
struct BSPFACE;

class Winding
{
public:
	uint32  m_NumPoints;
	vec3_t* m_Points;

	Winding(Bsp* bsp, const BSPFACE& face, vec_t epsilon = ON_EPSILON);
	Winding(uint32 points);
	Winding(const Winding& other);
	virtual ~Winding();
	Winding& operator=(const Winding& other);

    void RemoveColinearPoints(vec_t epsilon = ON_EPSILON);
    bool Clip(const BSPPLANE& split, bool keepon, vec_t epsilon = ON_EPSILON);


protected:
	uint32  m_MaxPoints;
};
