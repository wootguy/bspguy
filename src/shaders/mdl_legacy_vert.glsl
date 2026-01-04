#define STUDIO_NF_CHROME 0x02
#define STUDIO_NF_ADDITIVE 0x20

uniform mat4 modelViewProjection;

// Lighting uniforms
uniform mat4 modelView;
uniform mat4 normalMat;
uniform mat3 lights[4];
uniform int elights;
uniform vec3 ambient;

// render flags
uniform int chromeEnable;
uniform int additiveEnable;
uniform int flatshadeEnable;

// chrome uniforms
uniform vec3 viewerOrigin;
uniform vec3 viewerRight;
uniform vec2 textureST;

// vertex variables
attribute vec3 vPosition;
attribute vec3 vNormal;
attribute vec2 vTex;
attribute float vBone;

// fragment variables
varying vec2 fTex;
varying vec4 fColor;

vec4 lighting(vec3 tNormal);
vec2 chrome(vec3 tNormal);

void main()
{
	gl_Position = modelViewProjection * vec4(vPosition, 1);

	if (chromeEnable == 2) {
		fTex.x = vBone; // keeping the vertex attribute active because it's needed in the modern shader
	} else if (chromeEnable != 0) {
		fTex = chrome(vNormal);
	} else {
		fTex = vTex;
	}

	// TODO: compile multiple shaders and control this if #ifdef
	if (additiveEnable != 0) {
		fColor = vec4(1, 1, 1, 1);
	} else if (flatshadeEnable == 1) {
		fColor = vec4(ambient*0.725, 1); // trying to match HLMV
	} else if (flatshadeEnable == 2) {
		fColor = vec4(1, 1, 1, 1);
	} else {
		fColor = lighting(vNormal);
	}
}

vec3 rotateVector(vec3 v, inout mat4 mat)
{
	vec3 vout; 
	vout.x = dot(v, mat[0].xyz); 
	vout.z = -(dot(v, mat[1].xyz)); 
	vout.y = dot(v, mat[2].xyz); 
	return vout; 
}

vec2 chrome(vec3 tNormal)
{
	//vec3 bonePos = vec3(0,0,0);
	vec3 dir = normalize(viewerOrigin);

	vec3 chromeup = normalize(cross(dir, viewerRight));
	vec3 chromeright = normalize(cross(dir, chromeup));
	
	vec2 chrome;
	chrome.x = (dot(tNormal, chromeright) + 1.0) * 0.5;
	chrome.y = (dot(tNormal, chromeup) + 1.0) * 0.5;

	return chrome;
}

vec4 lighting(vec3 tNormal)
{
	float ambientScale = (96.0 / 255.0); // trying to match HLMV
	vec3 finalColor = ambient*ambientScale;
	for (int i = 0; i < 4; ++i)
	{
		if (i == elights) // Webgl won't let us use variables in our loop condition. So we have this.
			break;
		vec3 lightDirection = normalize(lights[i][0].xyz);
		vec3 diffuse = lights[i][1].xyz;
		float lightcos = dot(tNormal, lightDirection);
		float r = 1.5;
		lightcos = ( lightcos + ( r - 1.0 ) ) / r;

		finalColor += diffuse * lightcos;
	}
	return vec4(clamp(finalColor, vec3(0, 0, 0), vec3(1, 1, 1)), 1);
}