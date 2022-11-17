#include "studio.h"
#include "mstream.h"
#include "Texture.h"
#include "VertexBuffer.h"
#include "primitives.h"

#define	Q_PI	3.14159265358979323846

struct MdlVert {
	vec3 pos;
	vec2 uv;
	vec4 color;
	short origVert;
	short origNorm;
};

struct boneVert {
	float u, v;
	float nx, ny, nz;
	float x, y, z;
};

struct MdlMeshRender {
	tnVert* verts;
	short* origVerts; // original mdl vertex used to create the rendered vertex
	short* origNorms; // original mdl normals used to create the rendered normal
	int numVerts;
	Texture* texture;
	VertexBuffer* buffer;
};

class MdlRenderer {
public:
	studiohdr_t* header = NULL;
	mstream data;
	bool valid;
	string fpath;

	uint8_t iController[4];
	uint8_t iBlender[2];
	uint8_t iMouth;
	float m_Adj[MAXSTUDIOCONTROLLERS];

	MdlRenderer(ShaderProgram* shaderProgram, string modelPath);
	~MdlRenderer();

	void draw();
	bool validate();
	bool hasExternalTextures();
	bool hasExternalSequences();
	bool isEmpty();

	void SetUpBones();
	void transformVerts();

private:
	ShaderProgram* shaderProgram;
	Texture** glTextures = NULL;
	MdlMeshRender*** meshBuffers = NULL;

	bool loadTextureData();
	bool loadMeshes();
	
	float m_bonetransform[MAXSTUDIOBONES][3][4];	// bone transformation matrix (3x4)

	// Solokiller's model viewer functions
	mstudioanim_t* GetAnim(mstudioseqdesc_t* pseqdesc);
	void CalcRotations(vec3* pos, vec4* q, const mstudioseqdesc_t* const pseqdesc, const mstudioanim_t* panim, const float f);
	void CalcBoneQuaternion(const int frame, const float s, const mstudiobone_t* const pbone, const mstudioanim_t* const panim, vec4& q);
	void CalcBonePosition(const int frame, const float s, const mstudiobone_t* const pbone, const mstudioanim_t* const panim, float* pos);
	void CalcBoneAdj();
	void SlerpBones(vec4* q1, vec3* pos1, vec4* q2, vec3* pos2, float s);
	void QuaternionMatrix(float* quaternion, float matrix[3][4]);
};