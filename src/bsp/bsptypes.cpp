#include "bsptypes.h"
#include <math.h>
#include <string.h>

BSPEDGE::BSPEDGE() = default;

BSPEDGE::BSPEDGE(unsigned short v1, unsigned short v2) {
	iVertex[0] = v1;
	iVertex[1] = v2;
}

bool BSPPLANE::update(vec3 newNormal, float fdist) {
	double fx = fabs(newNormal.x);
	double fy = fabs(newNormal.y);
	double fz = fabs(newNormal.z);
	int planeType = PLANE_ANYZ;
	bool shouldFlip = false;
	if (fx > 0.9999) {
		planeType = PLANE_X;
		if (newNormal.x < 0) shouldFlip = true;
	}
	else if (fy > 0.9999) {
		planeType = PLANE_Y;
		if (newNormal.y < 0) shouldFlip = true;
	}
	else if (fz > 0.9999) {
		planeType = PLANE_Z;
		if (newNormal.z < 0) shouldFlip = true;
	}
	else {
		if (fx > fy && fx > fz) {
			planeType = PLANE_ANYX;
			//if (newNormal.x < 0) shouldFlip = true;
		}
		else if (fy > fx && fy > fz) {
			planeType = PLANE_ANYY;
			//if (newNormal.y < 0) shouldFlip = true;
		}
		else {
			planeType = PLANE_ANYZ;
			//if (newNormal.z < 0) shouldFlip = true;
		}
	}

	// TODO: negative normals seem to be working for submodels. Just doesn't work for head nodes?
	if (shouldFlip) {
		newNormal *= -1;
		fdist = -fdist;
	}

	fDist = fdist;
	vNormal = newNormal;
	nType = planeType;

	return shouldFlip;
}

bool BSPLEAF::isEmpty() {
	BSPLEAF emptyLeaf;
	memset(&emptyLeaf, 0, sizeof(BSPLEAF));
	emptyLeaf.nContents = CONTENTS_SOLID;

	return memcmp(&emptyLeaf, this, sizeof(BSPLEAF)) == 0;
}
