#define STUDIO_NF_CHROME 0x02
#define STUDIO_NF_ADDITIVE 0x20

uniform mat4 modelViewProjection;

// Lighting uniforms
uniform mat4 modelView;
uniform mat4 normalMat;
uniform mat3 lights[4];
uniform int elights;
uniform vec3 ambient;

#ifdef BONE_TEXTURE
	// skeleton
	// Texture as an array of mat4 (poor man's UBO). Vertex Texture Fetch requires GL 3.0 or 2.1 w/ ARB
	// Can't use UBO without upgrading to GL 3.1. Can't have 128 mat4 uniforms for all GPUs.
	uniform sampler2D boneMatrixTexture;
#endif

// render flags
uniform int chromeEnable;
uniform int additiveEnable;
uniform int flatshadeEnable;

// chrome uniforms
uniform vec3 viewerOrigin;
uniform vec3 viewerRight;

// vertex variables
attribute vec3 vPosition;
attribute vec3 vNormal;
attribute vec2 vTex;
attribute float vBone;

// fragment variables
varying vec2 fTex;
varying vec4 fColor;

vec4 lighting(vec3 tNormal);
vec3 rotateVector(vec3 v, inout mat4 mat);

#ifdef BONE_TEXTURE
vec2 chrome(vec3 tNormal, inout mat4 bone);
#else
vec2 chrome(vec3 tNormal);
#endif

void main()
{
	#ifdef BONE_TEXTURE
		mat4 bone;
		float boneCoord = (vBone / 128.0) + (1.0 / 512.0);
		bone[0] = texture2D(boneMatrixTexture, vec2(0.00 + (1.0 / 8.0), boneCoord));
		bone[1] = texture2D(boneMatrixTexture, vec2(0.25 + (1.0 / 8.0), boneCoord));
		bone[2] = texture2D(boneMatrixTexture, vec2(0.50 + (1.0 / 8.0), boneCoord));
		bone[3] = texture2D(boneMatrixTexture, vec2(0.75 + (1.0 / 8.0), boneCoord));
		
		vec3 pos = rotateVector(vPosition, bone) + vec3(bone[0][3], bone[2][3], -bone[1][3]);
		vec3 tNormal = rotateVector(vNormal, bone);
		
		gl_Position = modelViewProjection * vec4(pos, 1);
	#else
		vec3 tNormal = vNormal;
		
		gl_Position = modelViewProjection * vec4(vPosition, 1);
	#endif

	if (chromeEnable != 0) {
		#ifdef BONE_TEXTURE
			fTex = chrome(tNormal, bone);
		#else
			fTex = chrome(tNormal);
		#endif
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
		fColor = lighting(tNormal);
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

#ifdef BONE_TEXTURE

vec2 chrome(vec3 tNormal, inout mat4 bone)
{
	vec3 bonePos = vec3(bone[0][3], bone[2][3], -bone[1][3]);
	vec3 dir = normalize(viewerOrigin - bonePos);

	vec3 chromeup = normalize(cross(dir, viewerRight));
	vec3 chromeright = normalize(cross(dir, chromeup));
	
	vec2 chrome;
	chrome.x = (dot(tNormal, chromeright) + 1.0) * 0.5;
	chrome.y = (dot(tNormal, chromeup) + 1.0) * 0.5;

	return chrome;
}

#else

vec2 chrome(vec3 tNormal)
{
	vec3 dir = normalize(viewerOrigin);

	vec3 chromeup = normalize(cross(dir, viewerRight));
	vec3 chromeright = normalize(cross(dir, chromeup));
	
	vec2 chrome;
	chrome.x = (dot(tNormal, chromeright) + 1.0) * 0.5;
	chrome.y = (dot(tNormal, chromeup) + 1.0) * 0.5;

	return chrome;
}

#endif

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