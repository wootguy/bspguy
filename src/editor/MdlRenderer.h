#include "studio.h"
#include "mstream.h"
#include "Texture.h"
#include "VertexBuffer.h"
#include "primitives.h"

#define	EQUAL_EPSILON	0.001f

struct MdlVert {
	vec3 pos;
	vec2 uv;
	vec4 color;
	short origVert;
	short origNorm;
};

struct boneVert {
	vec2 uv;
	vec3 normal;
	vec3 pos;
	float bone;
};

struct MdlMeshRender {
	boneVert* verts;
	short* origVerts; // original mdl vertex used to create the rendered vertex
	short* origNorms; // original mdl normals used to create the rendered normal
	int numVerts;
	int flags;
	Texture* texture;
	VertexBuffer* buffer;
};

class MdlRenderer {
public:
	studiohdr_t* header = NULL;
	mstream data; // TODO: parse structures into class members instead of seeking through the original data
	bool valid;
	string fpath;

	uint8_t iController[4];
	uint8_t iBlender[2];
	uint8_t iMouth;
	float m_Adj[MAXSTUDIOCONTROLLERS];

	MdlRenderer(ShaderProgram* shaderProgram, string modelPath);
	~MdlRenderer();

	void draw(vec3 origin, vec3 viewerOrigin, vec3 viewerRight);
	bool validate();
	bool hasExternalTextures();
	bool hasExternalSequences();
	bool isEmpty();

	// frame values = 0 - 1.0 (0-100%)
	// angles = rotation for the entire model (y = pitch, z = yaw)
	void SetUpBones(vec3 angles, int sequence, float frame, int gaitsequence = -1, float gaitframe=0);
	void transformVerts();
	mstudioseqdesc_t* getSequence(int seq);

private:
	ShaderProgram* shaderProgram;
	Texture** glTextures = NULL;
	MdlMeshRender*** meshBuffers = NULL;

	bool loadTextureData();
	bool loadMeshes();
	
	float m_bonetransform[MAXSTUDIOBONES][4][4];	// bone transformation matrix (3x4)

	// Solokiller's model viewer functions
	mstudioanim_t* GetAnim(mstudioseqdesc_t* pseqdesc);
	void CalcBones(vec3* pos, vec4* q, const mstudioseqdesc_t* const pseqdesc, const mstudioanim_t* panim, const float f, bool isGait);
	void CalcBoneQuaternion(const int frame, const float s, const mstudiobone_t* const pbone, const mstudioanim_t* const panim, vec4& q);
	void CalcBonePosition(const int frame, const float s, const mstudiobone_t* const pbone, const mstudioanim_t* const panim, float* pos);
	void CalcBoneAdj();
	void SlerpBones(vec4* q1, vec3* pos1, vec4* q2, vec3* pos2, float s);
	void QuaternionMatrix(float* quaternion, float matrix[3][4]);
};