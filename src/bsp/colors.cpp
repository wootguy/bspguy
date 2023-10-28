#include "colors.h"

COLOR3 operator*(COLOR3 c, float scale)
{
	c.r *= scale;
	c.g *= scale;
	c.b *= scale;
	return c;
}

bool operator==(COLOR3 c1, COLOR3 c2) {
	return c1.r == c2.r && c1.g == c2.g && c1.b == c2.b;
}

COLOR4 operator*(COLOR4 c, float scale)
{
	c.r *= scale;
	c.g *= scale;
	c.b *= scale;
	return c;
}

bool operator==(COLOR4 c1, COLOR4 c2) {
	return c1.r == c2.r && c1.g == c2.g && c1.b == c2.b && c1.a == c2.a;
}