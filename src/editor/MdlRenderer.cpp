#include "MdlRenderer.h"
#include "util.h"
#include "Wad.h"
#include <GL/glew.h>

MdlRenderer::MdlRenderer(ShaderProgram* shaderProgram, string modelPath) {
	this->fpath = fpath;
	this->shaderProgram = shaderProgram;
	valid = false;

	string validPath = findAsset(modelPath);

	if (!validPath.length()) {
		return;
	}

	logf("Load MDL: %s\n", validPath.c_str());

	int len;
	char* buffer = loadFile(validPath, len);
	data = mstream(buffer, len);
	header = (studiohdr_t*)buffer;

	if (!validate() || isEmpty()) {
		return;
	}

	memset(iController, 127, 4);
	memset(iBlender, 0, 2);
	iMouth = 0;

	float frame = 0;

	loadTextureData();
	SetUpBones(0, frame);
	loadMeshes();
	//transformVerts();
	
	valid = true;
}

bool MdlRenderer::validate() {
	if (string(header->name).length() <= 0) {
		return false;
	}

	if (header->id != 1414743113) {
		cout << "ERROR: Invalid ID in model header\n";
		return false;
	}

	if (header->version != 10) {
		cout << "ERROR: Invalid version in model header\n";
		return false;
	}

	if (header->numseqgroups >= 10000) {
		cout << "ERROR: Too many seqgroups (" + to_string(header->numseqgroups) + ") in model\n";
		return false;
	}

	if (data.eom())
		return false;

	for (int b = 0; b < header->numbodyparts; b++) {
		// Try loading required model info
		data.seek(header->bodypartindex + b*sizeof(mstudiobodyparts_t));
		mstudiobodyparts_t* bod = (mstudiobodyparts_t*)data.get();

		for (int i = 0; i < bod->nummodels; i++) {
			data.seek(bod->modelindex + i * sizeof(mstudiomodel_t));
			mstudiomodel_t* mod = (mstudiomodel_t*)data.get();

			if (data.eom()) {
				cout << "ERROR: Failed to load body " + to_string(i) + "/" + to_string(bod->nummodels) + "\n";
				return false;
			}

			for (int k = 0; k < mod->nummesh; k++) {
				data.seek(mod->meshindex + k * sizeof(mstudiomesh_t));

				if (data.eom()) {
					cout << "ERROR: Failed to load mesh " + to_string(k) + " in model " + to_string(i) + "\n";
					return false;
				}

				mstudiomesh_t* mesh = (mstudiomesh_t*)data.get();

				data.seek(mesh->normindex + (mesh->numnorms * sizeof(vec3)) - 1);
				if (data.eom()) {
					cout << "ERROR: Failed to load normals for mesh " + to_string(k) + " in model " + to_string(i) + "\n";
					return false;
				}

				data.seek(mesh->triindex + (mesh->numtris * sizeof(mstudiotrivert_t) * 3) - 1);
				if (data.eom()) {
					cout << "ERROR: Failed to load triangles for mesh " + to_string(k) + " in model " + to_string(i) + "\n";
					return false;
				}
			}
		}
	}

	

	for (int i = 0; i < header->numseq; i++) {
		data.seek(header->seqindex + i * sizeof(mstudioseqdesc_t));
		if (data.eom()) {
			cout << "ERROR: Failed to load sequence " + to_string(i) + "/" + to_string(header->numseq) + "\n";
			return false;
		}

		mstudioseqdesc_t* seq = (mstudioseqdesc_t*)data.get();

		for (int k = 0; k < seq->numevents; k++) {
			data.seek(seq->eventindex + k * sizeof(mstudioevent_t));

			if (data.eom()) {
				cout << "ERROR: Failed to load event " + to_string(k) + "/" + to_string(seq->numevents) + " in sequence " + to_string(i) + "\n";
				return false;
			}

			mstudioevent_t* evt = (mstudioevent_t*)data.get();
		}

		data.seek(seq->animindex + (seq->numblends * header->numbones * sizeof(mstudioanim_t) * 6) - 1);
		if (data.eom()) {
			cout << "ERROR: Failed to load bone data for sequence " + to_string(i) + "/" + to_string(header->numseq) + "\n";
			return false;
		}
	}

	for (int i = 0; i < header->numbones; i++) {
		data.seek(header->boneindex + i * sizeof(mstudiobone_t));
		if (data.eom()) {
			cout << "ERROR: Failed to load sequence " + to_string(i) + "/" + to_string(header->numseq) + "\n";
			return false;
		}

		mstudiobone_t* bone = (mstudiobone_t*)data.get();
		if (bone->parent < -1 || bone->parent >= header->numbones) {
			cout << "ERROR: Bone " + to_string(i) + " has invalid parent " + to_string(bone->parent) + "\n";
			return false;
		}
	}

	for (int i = 0; i < header->numbonecontrollers; i++) {
		data.seek(header->bonecontrollerindex + i * sizeof(mstudiobonecontroller_t));
		if (data.eom()) {
			cout << "ERROR: Failed to load bone controller " + to_string(i) + "/" + to_string(header->numbonecontrollers) + "\n";
			return false;
		}

		mstudiobonecontroller_t* ctl = (mstudiobonecontroller_t*)data.get();
		if (ctl->bone < -1 || ctl->bone >= header->numbones) {
			cout << "ERROR: Controller " + to_string(i) + " references invalid bone " + to_string(ctl->bone) + "\n";
			return false;
		}
	}

	for (int i = 0; i < header->numhitboxes; i++) {
		data.seek(header->hitboxindex + i * sizeof(mstudiobbox_t));
		if (data.eom()) {
			cout << "ERROR: Failed to load bone controller " + to_string(i) + "/" + to_string(header->numhitboxes) + "\n";
			return false;
		}

		mstudiobbox_t* box = (mstudiobbox_t*)data.get();
		if (box->bone < -1 || box->bone >= header->numbones) {
			cout << "ERROR: Hitbox " + to_string(i) + " references invalid bone " + to_string(box->bone) + "\n";
			return false;
		}
	}

	for (int i = 0; i < header->numseqgroups; i++) {
		data.seek(header->seqgroupindex + i * sizeof(mstudioseqgroup_t));
		if (data.eom()) {
			cout << "ERROR: Failed to load sequence group " + to_string(i) + "/" + to_string(header->numseqgroups) + "\n";
			return false;
		}

		mstudioseqgroup_t* grp = (mstudioseqgroup_t*)data.get();
	}

	for (int i = 0; i < header->numtextures; i++) {
		data.seek(header->textureindex + i * sizeof(mstudiotexture_t));
		if (data.eom()) {
			cout << "ERROR: Failed to load texture " + to_string(i) + "/" + to_string(header->numtextures) + "\n";
			return false;
		}

		mstudiotexture_t* tex = (mstudiotexture_t*)data.get();
		data.seek(tex->index + (tex->width * tex->height + 256 * 3) - 1);
		if (data.eom()) {
			cout << "ERROR: Failed to load texture data " + to_string(i) + "/" + to_string(header->numtextures) + "\n";
			return false;
		}
	}

	for (int i = 0; i < header->numskinfamilies; i++) {
		data.seek(header->skinindex + i * sizeof(short) * header->numskinref);
		if (data.eom()) {
			cout << "ERROR: Failed to load skin family " + to_string(i) + "/" + to_string(header->numskinfamilies) + "\n";
			return false;
		}
	}

	for (int i = 0; i < header->numattachments; i++) {
		data.seek(header->attachmentindex + i * sizeof(mstudioattachment_t));
		if (data.eom()) {
			cout << "ERROR: Failed to load attachment " + to_string(i) + "/" + to_string(header->numattachments) + "\n";
			return false;
		}

		mstudioattachment_t* att = (mstudioattachment_t*)data.get();
		if (att->bone < -1 || att->bone >= header->numbones) {
			cout << "ERROR: Attachment " + to_string(i) + " references invalid bone " + to_string(att->bone) + "\n";
			return false;
		}
	}

	return true;
}

bool MdlRenderer::isEmpty() {
	bool isEmptyModel = true;

	data.seek(header->bodypartindex);
	mstudiobodyparts_t* bod = (mstudiobodyparts_t*)data.get();
	for (int i = 0; i < bod->nummodels; i++) {
		data.seek(bod->modelindex + i * sizeof(mstudiomodel_t));
		mstudiomodel_t* mod = (mstudiomodel_t*)data.get();

		if (mod->nummesh != 0) {
			isEmptyModel = false;
			break;
		}
	}

	return isEmptyModel;
}

bool MdlRenderer::hasExternalTextures() {
	// textures aren't needed if the model has no triangles
	return header->numtextures == 0 && !isEmpty();
}

bool MdlRenderer::hasExternalSequences() {
	return header->numseqgroups > 1;
}

bool MdlRenderer::loadTextureData() {
	if (hasExternalTextures()) {
		logf("UH OH EXTERNAL TEXTURES\n");
		return false;
	}

	glTextures = new Texture*[header->numtextures];
	memset(glTextures, 0, sizeof(Texture*)*header->numtextures);

	for (int i = 0; i < header->numtextures; i++) {
		data.seek(header->textureindex + i * sizeof(mstudiotexture_t));
		if (data.eom()) {
			logf("ERROR: Failed to load texture %d/%d\n", i, header->numtextures);
			return false;
		}

		mstudiotexture_t* tex = (mstudiotexture_t*)data.get();
		data.seek(tex->index + (tex->width * tex->height + 256 * 3) - 1);
		if (data.eom()) {
			logf("ERROR: Failed to load texture %d/%d\n", i, header->numtextures);
			return false;
		}

		data.seek(tex->index);
		uint8_t* srcData = (uint8_t*)data.get();
		int imageDataSz = tex->width * tex->height;

		data.seek(tex->index + imageDataSz);
		COLOR3* palette = (COLOR3*)data.get();

		if (tex->flags & STUDIO_NF_MASKED) {
			COLOR4* imageData = new COLOR4[imageDataSz];

			for (int k = 0; k < imageDataSz; k++) {
				if (srcData[k] == 255) {
					imageData[k] = COLOR4(0, 0, 0, 0);
				}
				else {
					imageData[k] = COLOR4(palette[srcData[k]], 255);
				}
			}

			glTextures[i] = new Texture(tex->width, tex->height, imageData);
			glTextures[i]->upload(GL_RGBA);
		} else{
			COLOR3* imageData = new COLOR3[imageDataSz];

			for (int k = 0; k < imageDataSz; k++) {
				imageData[k] = palette[srcData[k]];
			}

			glTextures[i] = new Texture(tex->width, tex->height, imageData);
			glTextures[i]->upload(GL_RGB);
		}
	}

	return true;
}

mstudioanim_t* MdlRenderer::GetAnim(mstudioseqdesc_t* pseqdesc) {
	data.seek(header->seqgroupindex + pseqdesc->seqgroup*sizeof(mstudioseqgroup_t));
	mstudioseqgroup_t* pseqgroup = (mstudioseqgroup_t*)data.get();

	if (pseqdesc->seqgroup == 0)
	{
		data.seek(pseqgroup->data + pseqdesc->animindex);
		return (mstudioanim_t*)data.get();
	}

	logf("TODO: External sequences\n");
	data.seek(pseqgroup->data + pseqdesc->animindex);
	return (mstudioanim_t*)data.get();
}

void MdlRenderer::CalcBoneAdj()
{
	data.seek(header->bonecontrollerindex);
	mstudiobonecontroller_t* pbonecontroller = (mstudiobonecontroller_t*) data.get();

	for (int j = 0; j < header->numbonecontrollers; j++)
	{
		const auto i = pbonecontroller[j].index;

		float value;

		if (i <= 3)
		{
			// check for 360% wrapping
			if (pbonecontroller[j].type & STUDIO_RLOOP)
			{
				value = iController[i] * (360.0 / 256.0) + pbonecontroller[j].start;
			}
			else
			{
				value = iController[i] / 255.0;
				if (value < 0) value = 0;
				if (value > 1.0) value = 1.0;
				value = (1.0 - value) * pbonecontroller[j].start + value * pbonecontroller[j].end;
			}
			// Con_DPrintf( "%d %d %f : %f\n", m_controller[j], m_prevcontroller[j], value, dadt );
		}
		else
		{
			value = iMouth / 64.0;
			if (value > 1.0) value = 1.0;
			value = (1.0 - value) * pbonecontroller[j].start + value * pbonecontroller[j].end;
			// Con_DPrintf("%d %f\n", mouthopen, value );
		}
		switch (pbonecontroller[j].type & STUDIO_TYPES)
		{
		case STUDIO_XR:
		case STUDIO_YR:
		case STUDIO_ZR:
			m_Adj[j] = value * (Q_PI / 180.0);
			break;
		case STUDIO_X:
		case STUDIO_Y:
		case STUDIO_Z:
			m_Adj[j] = value;
			break;
		}
	}
}

void AngleQuaternion(const vec3& angles, vec4& quaternion)
{
	float		angle;
	float		sr, sp, sy, cr, cp, cy;

	// FIXME: rescale the inputs to 1/2 angle
	angle = angles.z * 0.5;
	sy = sin(angle);
	cy = cos(angle);
	angle = angles.y * 0.5;
	sp = sin(angle);
	cp = cos(angle);
	angle = angles.x * 0.5;
	sr = sin(angle);
	cr = cos(angle);

	quaternion.x = sr * cp * cy - cr * sp * sy;
	quaternion.y = cr * sp * cy + sr * cp * sy;
	quaternion.z = cr * cp * sy - sr * sp * cy;
	quaternion.w = cr * cp * cy + sr * sp * sy;
}

void QuaternionSlerp(const vec4& pVec, vec4& qVec, float t, vec4& qtVec)
{
	int i;
	float omega, cosom, sinom, sclp, sclq;
	float* p = (float*)&pVec;
	float* q = (float*)&qVec;
	float* qt = (float*)&qtVec;

	// decide if one of the quaternions is backwards
	float a = 0;
	float b = 0;
	for (i = 0; i < 4; i++) {
		a += (p[i] - q[i]) * (p[i] - q[i]);
		b += (p[i] + q[i]) * (p[i] + q[i]);
	}
	if (a > b) {
		for (i = 0; i < 4; i++) {
			q[i] = -q[i];
		}
	}

	cosom = p[0] * q[0] + p[1] * q[1] + p[2] * q[2] + p[3] * q[3];

	if ((1.0 + cosom) > 0.00000001) {
		if ((1.0 - cosom) > 0.00000001) {
			omega = acos(cosom);
			sinom = sin(omega);
			sclp = sin((1.0 - t) * omega) / sinom;
			sclq = sin(t * omega) / sinom;
		}
		else {
			sclp = 1.0 - t;
			sclq = t;
		}
		for (i = 0; i < 4; i++) {
			qt[i] = sclp * p[i] + sclq * q[i];
		}
	}
	else {
		qt[0] = -p[1];
		qt[1] = p[0];
		qt[2] = -p[3];
		qt[3] = p[2];
		sclp = sin((1.0 - t) * 0.5 * Q_PI);
		sclq = sin(t * 0.5 * Q_PI);
		for (i = 0; i < 3; i++) {
			qt[i] = sclp * p[i] + sclq * qt[i];
		}
	}
}

bool VectorCompare(const vec3& lhs, const vec3& rhs) {
	if (fabs(lhs.x - rhs.x) > EQUAL_EPSILON
		|| fabs(lhs.y - rhs.y) > EQUAL_EPSILON
		|| fabs(lhs.z - rhs.z) > EQUAL_EPSILON) {
		return false;
	}

	return true;
}

void VectorTransform(const vec3& in1, float in2[3][4], vec3& out) {
	// convert coordinate system
	/*
	out.x = dotProduct(in1, *(vec3*)(&in2[0][0])) + in2[0][3];
	out.z = -(dotProduct(in1, *(vec3*)(&in2[1][0])) + in2[1][3]);
	out.y = dotProduct(in1, *(vec3*)(&in2[2][0])) + in2[2][3];
	*/

	out.x = dotProduct(in1, *(vec3*)(&in2[0][0])) + in2[0][3];
	out.z = -(dotProduct(in1, *(vec3*)(&in2[1][0])) + in2[1][3]);
	out.y = dotProduct(in1, *(vec3*)(&in2[2][0])) + in2[2][3];
}

void VectorRotate(const vec3& in1, float in2[3][4], vec3& out) {
	// convert coordinate system
	out.x = dotProduct(in1, *(vec3*)(&in2[0][0]));
	out.y = dotProduct(in1, *(vec3*)(&in2[1][0]));
	out.z = dotProduct(in1, *(vec3*)(&in2[2][0]));
}

void VectorIRotate(vec3& vector, float matrix[3][4], vec3& outResult)
{
	/*
	outResult.x = vector.x * matrix[0][0] + vector.y * matrix[1][0] + vector.z * matrix[2][0];
	outResult.z = -(vector.x * matrix[0][1] + vector.y * matrix[1][1] + vector.z * matrix[2][1]);
	outResult.y = vector.x * matrix[0][2] + vector.y * matrix[1][2] + vector.z * matrix[2][2];
	*/
	outResult.x = vector.x * matrix[0][0] + vector.y * matrix[1][0] + vector.z * matrix[2][0];
	outResult.y = vector.x * matrix[0][1] + vector.y * matrix[1][1] + vector.z * matrix[2][1];
	outResult.z = vector.x * matrix[0][2] + vector.y * matrix[1][2] + vector.z * matrix[2][2];
}

void MdlRenderer::CalcBoneQuaternion(const int frame, const float s, const mstudiobone_t* const pbone, const mstudioanim_t* const panim, vec4& q)
{
	vec3 angle1Vec;
	vec3 angle2Vec;
	float* angle1 = (float*)&angle1Vec;
	float* angle2 = (float*)&angle2Vec;

	for (int j = 0; j < 3; j++)
	{
		if (panim->offset[j + 3] == 0)
		{
			angle2[j] = angle1[j] = pbone->value[j + 3]; // default;
		}
		else
		{
			mstudioanimvalue_t* panimvalue = (mstudioanimvalue_t*)((byte*)panim + panim->offset[j + 3]);
			int k = frame;
			while (panimvalue->num.total <= k)
			{
				k -= panimvalue->num.total;
				panimvalue += panimvalue->num.valid + 1;
			}
			// Bah, missing blend!
			if (panimvalue->num.valid > k)
			{
				angle1[j] = panimvalue[k + 1].value;

				if (panimvalue->num.valid > k + 1)
				{
					angle2[j] = panimvalue[k + 2].value;
				}
				else
				{
					if (panimvalue->num.total > k + 1)
						angle2[j] = angle1[j];
					else
						angle2[j] = panimvalue[panimvalue->num.valid + 2].value;
				}
			}
			else
			{
				angle1[j] = panimvalue[panimvalue->num.valid].value;
				if (panimvalue->num.total > k + 1)
				{
					angle2[j] = angle1[j];
				}
				else
				{
					angle2[j] = panimvalue[panimvalue->num.valid + 2].value;
				}
			}
			angle1[j] = pbone->value[j + 3] + angle1[j] * pbone->scale[j + 3];
			angle2[j] = pbone->value[j + 3] + angle2[j] * pbone->scale[j + 3];
		}

		if (pbone->bonecontroller[j + 3] != -1)
		{
			angle1[j] += m_Adj[pbone->bonecontroller[j + 3]];
			angle2[j] += m_Adj[pbone->bonecontroller[j + 3]];
		}
	}

	if (!VectorCompare(angle1Vec, angle2Vec))
	{
		vec4 q1, q2;

		AngleQuaternion(angle1Vec, q1);
		AngleQuaternion(angle2Vec, q2);
		QuaternionSlerp(q1, q2, s, q);
	}
	else
	{
		AngleQuaternion(angle1Vec, q);
	}
}

void MdlRenderer::CalcBonePosition(const int frame, const float s, const mstudiobone_t* const pbone, const mstudioanim_t* const panim, float* pos)
{
	for (int j = 0; j < 3; j++)
	{
		pos[j] = pbone->value[j]; // default;
		if (panim->offset[j] != 0)
		{
			auto panimvalue = (mstudioanimvalue_t*)((byte*)panim + panim->offset[j]);

			auto k = frame;
			// find span of values that includes the frame we want
			while (panimvalue->num.total <= k)
			{
				k -= panimvalue->num.total;
				panimvalue += panimvalue->num.valid + 1;
			}
			// if we're inside the span
			if (panimvalue->num.valid > k)
			{
				// and there's more data in the span
				if (panimvalue->num.valid > k + 1)
				{
					pos[j] += (panimvalue[k + 1].value * (1.0 - s) + s * panimvalue[k + 2].value) * pbone->scale[j];
				}
				else
				{
					pos[j] += panimvalue[k + 1].value * pbone->scale[j];
				}
			}
			else
			{
				// are we at the end of the repeating values section and there's another section with data?
				if (panimvalue->num.total <= k + 1)
				{
					pos[j] += (panimvalue[panimvalue->num.valid].value * (1.0 - s) + s * panimvalue[panimvalue->num.valid + 2].value) * pbone->scale[j];
				}
				else
				{
					pos[j] += panimvalue[panimvalue->num.valid].value * pbone->scale[j];
				}
			}
		}
		if (pbone->bonecontroller[j] != -1)
		{
			pos[j] += m_Adj[pbone->bonecontroller[j]];
		}
	}
}

void MdlRenderer::CalcRotations(vec3* pos, vec4* q, const mstudioseqdesc_t* const pseqdesc, const mstudioanim_t* panim, const float f)
{
	const int frame = (int)f;
	const float s = (f - frame);

	// add in programatic controllers
	CalcBoneAdj();

	data.seek(header->boneindex);
	mstudiobone_t* pbone = (mstudiobone_t*)data.get();

	for (int i = 0; i < header->numbones; i++, pbone++, panim++)
	{
		CalcBoneQuaternion(frame, s, pbone, panim, q[i]);
		CalcBonePosition(frame, s, pbone, panim, (float*)&pos[i]);
	}

	if (pseqdesc->motiontype & STUDIO_X)
		pos[pseqdesc->motionbone].x = 0.0;
	if (pseqdesc->motiontype & STUDIO_Y)
		pos[pseqdesc->motionbone].y = 0.0;
	if (pseqdesc->motiontype & STUDIO_Z)
		pos[pseqdesc->motionbone].z = 0.0;
}

void MdlRenderer::SlerpBones(vec4* q1, vec3* pos1, vec4* q2, vec3* pos2, float s)
{
	vec4 q3;

	if (s < 0) s = 0;
	else if (s > 1.0) s = 1.0;

	const float s1 = 1.0 - s;

	for (int i = 0; i < header->numbones; i++)
	{
		QuaternionSlerp(q1[i], q2[i], s, q3);
		q1[i] = q3;

		pos1[i] = pos1[i] * s1 + pos2[i] * s;
	}
}

void MdlRenderer::QuaternionMatrix(float* quaternion, float matrix[3][4])
{
	matrix[0][0] = 1.0 - 2.0 * quaternion[1] * quaternion[1] - 2.0 * quaternion[2] * quaternion[2];
	matrix[1][0] = 2.0 * quaternion[0] * quaternion[1] + 2.0 * quaternion[3] * quaternion[2];
	matrix[2][0] = 2.0 * quaternion[0] * quaternion[2] - 2.0 * quaternion[3] * quaternion[1];

	matrix[0][1] = 2.0 * quaternion[0] * quaternion[1] - 2.0 * quaternion[3] * quaternion[2];
	matrix[1][1] = 1.0 - 2.0 * quaternion[0] * quaternion[0] - 2.0 * quaternion[2] * quaternion[2];
	matrix[2][1] = 2.0 * quaternion[1] * quaternion[2] + 2.0 * quaternion[3] * quaternion[0];

	matrix[0][2] = 2.0 * quaternion[0] * quaternion[2] + 2.0 * quaternion[3] * quaternion[1];
	matrix[1][2] = 2.0 * quaternion[1] * quaternion[2] - 2.0 * quaternion[3] * quaternion[0];
	matrix[2][2] = 1.0 - 2.0 * quaternion[0] * quaternion[0] - 2.0 * quaternion[1] * quaternion[1];
}

void R_ConcatTransforms(float in1[3][4], float in2[3][4], float out[3][4])
{
	out[0][0] = in1[0][0] * in2[0][0] + in1[0][1] * in2[1][0] +
		in1[0][2] * in2[2][0];
	out[0][1] = in1[0][0] * in2[0][1] + in1[0][1] * in2[1][1] +
		in1[0][2] * in2[2][1];
	out[0][2] = in1[0][0] * in2[0][2] + in1[0][1] * in2[1][2] +
		in1[0][2] * in2[2][2];
	out[0][3] = in1[0][0] * in2[0][3] + in1[0][1] * in2[1][3] +
		in1[0][2] * in2[2][3] + in1[0][3];
	out[1][0] = in1[1][0] * in2[0][0] + in1[1][1] * in2[1][0] +
		in1[1][2] * in2[2][0];
	out[1][1] = in1[1][0] * in2[0][1] + in1[1][1] * in2[1][1] +
		in1[1][2] * in2[2][1];
	out[1][2] = in1[1][0] * in2[0][2] + in1[1][1] * in2[1][2] +
		in1[1][2] * in2[2][2];
	out[1][3] = in1[1][0] * in2[0][3] + in1[1][1] * in2[1][3] +
		in1[1][2] * in2[2][3] + in1[1][3];
	out[2][0] = in1[2][0] * in2[0][0] + in1[2][1] * in2[1][0] +
		in1[2][2] * in2[2][0];
	out[2][1] = in1[2][0] * in2[0][1] + in1[2][1] * in2[1][1] +
		in1[2][2] * in2[2][1];
	out[2][2] = in1[2][0] * in2[0][2] + in1[2][1] * in2[1][2] +
		in1[2][2] * in2[2][2];
	out[2][3] = in1[2][0] * in2[0][3] + in1[2][1] * in2[1][3] +
		in1[2][2] * in2[2][3] + in1[2][3];
}

void MdlRenderer::SetUpBones(int sequence, float& frame)
{
	static vec3 pos[MAXSTUDIOBONES];
	static vec4 q[MAXSTUDIOBONES];

	static vec3 pos2[MAXSTUDIOBONES];
	static vec4 q2[MAXSTUDIOBONES];
	static vec3 pos3[MAXSTUDIOBONES];
	static vec4 q3[MAXSTUDIOBONES];
	static vec3 pos4[MAXSTUDIOBONES];
	static vec4 q4[MAXSTUDIOBONES];

	data.seek(header->seqindex + sequence *sizeof(mstudioseqdesc_t));
	mstudioseqdesc_t* pseqdesc = (mstudioseqdesc_t*)data.get();

	const mstudioanim_t* panim = GetAnim(pseqdesc);

	frame = normalizeRangef(frame, 0, pseqdesc->numframes - 1.0f);

	CalcRotations(pos, q, pseqdesc, panim, frame);

	if (pseqdesc->numblends > 1)
	{
		panim += header->numbones;
		CalcRotations(pos2, q2, pseqdesc, panim, frame);
		float s = iBlender[0] / 255.0;

		SlerpBones(q, pos, q2, pos2, s);

		if (pseqdesc->numblends == 4)
		{
			panim += header->numbones;
			CalcRotations(pos3, q3, pseqdesc, panim, frame);

			panim += header->numbones;
			CalcRotations(pos4, q4, pseqdesc, panim, frame);

			s = iBlender[0] / 255.0;
			SlerpBones(q3, pos3, q4, pos4, s);

			s = iBlender[1] / 255.0;
			SlerpBones(q, pos, q3, pos3, s);
		}
	}

	data.seek(header->boneindex);
	mstudiobone_t* pbones = (mstudiobone_t*)data.get();

	float bonematrix[3][4];

	for (int i = 0; i < header->numbones; i++)
	{
		QuaternionMatrix((float*)&q[i], bonematrix);

		bonematrix[0][3] = pos[i].x;
		bonematrix[1][3] = pos[i].y;
		bonematrix[2][3] = pos[i].z;

		if (pbones[i].parent == -1)
		{
			for (int x = 0; x < 3; x++)
				for (int y = 0; y < 4; y++)
					m_bonetransform[i][x][y] = bonematrix[x][y];
		}
		else
		{
			R_ConcatTransforms(m_bonetransform[pbones[i].parent], bonematrix, m_bonetransform[i]);
		}
	}
}

void MdlRenderer::transformVerts() {
	int modelIdx = 0;
	int vertCount = 0;

	for (int b = 0; b < header->numbodyparts; b++) {
		data.seek(header->bodypartindex + b * sizeof(mstudiobodyparts_t));
		mstudiobodyparts_t* bod = (mstudiobodyparts_t*)data.get();

		for (int i = 0; i < bod->nummodels; i++) {
			data.seek(bod->modelindex + i * sizeof(mstudiomodel_t));
			mstudiomodel_t* mod = (mstudiomodel_t*)data.get();

			data.seek(mod->vertindex);
			vec3* pstudioverts = (vec3*)data.get();

			data.seek(mod->normindex);
			vec3* pstudionorms = (vec3*)data.get();

			data.seek(mod->vertinfoindex);
			uint8_t* pvertbone = (uint8_t*)data.get();

			data.seek(mod->norminfoindex);
			uint8_t* pnormbone = (uint8_t*)data.get();

			vec3 transformedVerts[MAXSTUDIOVERTS];
			vec3 transformedNormals[MAXSTUDIOVERTS];

			for (int k = 0; k < mod->numverts; k++) {
				VectorTransform(pstudioverts[k], m_bonetransform[pvertbone[k]], transformedVerts[k]);
			}

			for (int k = 0; k < mod->numnorms; k++) {
				VectorRotate(pstudionorms[k], m_bonetransform[pnormbone[k]], transformedNormals[k]);
			}

			for (int k = 0; k < mod->nummesh; k++) {
				MdlMeshRender& buffer = meshBuffers[b][i][k];

				// TODO: this does more transformations than necessary.
				// Only transform the original verts then update the GL_TRIANGLES verts by copying data,
				// or maybe that's not much faster?
				for (int v = 0; v < buffer.numVerts; v++) {
					short oldVertIdx = buffer.origVerts[v];
					short oldNormIdx = buffer.origNorms[v];

					buffer.verts[v].pos = transformedVerts[oldVertIdx];
					buffer.verts[v].normal = transformedNormals[oldNormIdx];

					//VectorTransform(pstudioverts[oldVertIdx], m_bonetransform[bone], *pos);
					//VectorRotate(pstudionorms[oldNormIdx], m_bonetransform[bone], *norm);

					//*norm = norm->normalize();
					//printf("NORM: %.1f %.1f %.1f\n", norm->x, norm->y, norm->z);
					vertCount++;
				}
			}
		}
	}

	//logf("Transformed %d verts\n", vertCount);
}

bool MdlRenderer::loadMeshes() {
	int uiDrawnPolys = 0;
	int meshBytes = 0;

	meshBuffers = new MdlMeshRender**[header->numbodyparts];
	memset(meshBuffers, 0, sizeof(MdlMeshRender**) * header->numbodyparts);

	for (int b = 0; b < header->numbodyparts; b++) {
		// Try loading required model info
		data.seek(header->bodypartindex + b * sizeof(mstudiobodyparts_t));
		mstudiobodyparts_t* bod = (mstudiobodyparts_t*)data.get();

		meshBuffers[b] = new MdlMeshRender * [bod->nummodels];
		memset(meshBuffers[b], 0, sizeof(MdlMeshRender*) * bod->nummodels);

		for (int m = 0; m < bod->nummodels; m++) {
			data.seek(bod->modelindex + m * sizeof(mstudiomodel_t));
			mstudiomodel_t* mod = (mstudiomodel_t*)data.get();

			meshBuffers[b][m] = new MdlMeshRender[mod->nummesh];
			memset(meshBuffers[b][m], 0, sizeof(MdlMeshRender) * mod->nummesh);

			data.seek(mod->vertindex);
			vec3* pstudioverts = (vec3*)data.get();

			data.seek(mod->normindex);
			vec3* pstudionorms = (vec3*)data.get();

			data.seek(mod->vertinfoindex);
			uint8_t* pvertbone = (uint8_t*)data.get();

			for (int k = 0; k < mod->nummesh; k++) {
				data.seek(mod->meshindex + k * sizeof(mstudiomesh_t));

				if (data.eom()) {
					printf("ERROR: Failed to load mesh %d in model %d\n", k, m);
					return false;
				}

				mstudiomesh_t* mesh = (mstudiomesh_t*)data.get();

				data.seek(mesh->normindex + (mesh->numnorms * sizeof(vec3)) - 1);
				if (data.eom()) {
					printf("ERROR: Failed to load normals for mesh %d in model %d\n", k, m);
					return false;
				}

				data.seek(mesh->triindex + (mesh->numtris * sizeof(mstudiotrivert_t) * 3) - 1);
				if (data.eom()) {
					printf("ERROR: Failed to load triangles for mesh %d in model %d\n", k, m);
					return false;
				}

				data.seek(header->skinindex);
				short* skins = (short*)data.get();
				int texId = skins[mesh->skinref];

				if (texId < 0 || texId >= header->numtextures) {
					printf("ERROR: invalid texture ref %d (max %d)", texId, header->numtextures);
					return false;
				}

				data.seek(header->textureindex + texId * sizeof(mstudiotexture_t));
				mstudiotexture_t* texture = (mstudiotexture_t*)data.get();
				meshBuffers[b][m][k].texture = glTextures[mesh->skinref];

				data.seek(mesh->triindex);
				short* ptricmds = (short*)data.get();

				int totalElements = 0;
				int texCoordIdx = 0;
				int colorIdx = 0;
				int vertexIdx = 0;
				int stripIdx = 0;
				int origVertIdx = 0;
				int origNormIdx = 0;
				int debugIdx = 0;

				const float s = 1.0 / (float)texture->width;
				const float t = 1.0 / (float)texture->height;

				vector<MdlVert> mdlVerts;

				mdlVerts.reserve(MAXSTUDIOVERTS*3);
				int p;

				while (p = *(ptricmds++))
				{
					int drawMode = GL_TRIANGLE_STRIP;
					if (p < 0)
					{
						p = -p;
						drawMode = GL_TRIANGLE_FAN;
					}

					int polies = p - 2;
					uiDrawnPolys += polies;

					int elementsThisStrip = 0;
					int fanStartIdx = totalElements;

					for (; p > 0; p--, ptricmds += 4)
					{
						if (elementsThisStrip++ >= 3) { // first 3 verts are always the first triangle
							// convert to GL_TRIANGLES
							if (drawMode == GL_TRIANGLE_STRIP) {
								mdlVerts.push_back(mdlVerts[totalElements - 2]);
								mdlVerts.push_back(mdlVerts[totalElements - 1]);

								totalElements += 2;
								elementsThisStrip += 2;
							}
							else if (drawMode == GL_TRIANGLE_FAN) {								
								mdlVerts.push_back(mdlVerts[fanStartIdx]);
								mdlVerts.push_back(mdlVerts[totalElements - 1]);

								totalElements += 2;
								elementsThisStrip += 2;
							}
						}

						MdlVert vert;
						
						if (texture->flags & STUDIO_NF_ADDITIVE) {
							vert.color = vec4(1.0f, 1.0f, 1.0f, 1.0f);
						}
						else {
							vert.color = vec4(pstudionorms[ptricmds[1]], 0);
						}

						if (texture->flags & STUDIO_NF_CHROME) {
							// real UVs calculated in shader
							vert.uv = vec2(0.5f, 0.5f);
						}
						else {
							vert.uv = vec2(ptricmds[2] * s, ptricmds[3] * t);
						}
						
						// TODO: hmmm
						//vert.color.w = m_pRenderInfo->flTransparency;

						vert.pos = pstudioverts[ptricmds[0]];
						vert.origVert = ptricmds[0];
						vert.origNorm = ptricmds[1];
						mdlVerts.push_back(vert);

						totalElements++;
					}

					// flip odd tris in strips (simpler than adding special logic when appending verts)
					if (drawMode == GL_TRIANGLE_STRIP) {
						for (int p = 1; p < polies; p += 2) {
							int polyOffset = p * 3;

							int vstart = polyOffset + fanStartIdx + 1;
							MdlVert t = mdlVerts[vstart];
							mdlVerts[vstart] = mdlVerts[vstart + 1];
							mdlVerts[vstart + 1] = t;
						}
					}
				}
					
				printf("%d %d %d - %d polys, %d verts, %d render verts\n", b, m, k, totalElements / 3, mod->numverts, totalElements);

				MdlMeshRender& render = meshBuffers[b][m][k];

				render.origVerts = new short[totalElements];
				render.origNorms = new short[totalElements];

				vector<boneVert> allVerts;
				allVerts.reserve(totalElements);

				for (int i = 0; i < totalElements; i++) {
					MdlVert& v = mdlVerts[i];
					render.origVerts[i] = v.origVert;
					render.origNorms[i] = v.origNorm;

					boneVert bvert;
					bvert.pos = v.pos;
					bvert.normal = v.color.xyz();
					bvert.uv = v.uv;
					bvert.bone = pvertbone[v.origVert] + 0.1f;

					allVerts.push_back(bvert);
				}

				meshBuffers[b][m][k].flags = texture->flags;
				meshBuffers[b][m][k].verts = new boneVert[totalElements];
				meshBuffers[b][m][k].numVerts = totalElements;
				memcpy(meshBuffers[b][m][k].verts, &allVerts[0], totalElements * sizeof(boneVert));
				meshBuffers[b][m][k].buffer = new VertexBuffer(shaderProgram, NORM_3F | TEX_2F | POS_3F, meshBuffers[b][m][k].verts, meshBuffers[b][m][k].numVerts);
				meshBuffers[b][m][k].buffer->addAttribute(1, GL_FLOAT, GL_FALSE, "vBone");
				meshBuffers[b][m][k].buffer->upload();

				meshBytes += totalElements * (sizeof(uint16_t) + sizeof(uint16_t) + sizeof(boneVert));
			}
		}
	}

	printf("Total polys: %d, Mesh kb: %d\n", uiDrawnPolys, (int)(meshBytes / 1024.0f));
}

void MdlRenderer::draw(vec3 origin, vec3 angles, vec3 viewerOrigin, vec3 viewerRight) {
	if (!valid) {
		return;
	}

	glDisable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE);

	shaderProgram->bind();

	uint sTexId = glGetUniformLocation(shaderProgram->ID, "sTex");
	glUniform1i(sTexId, 0);
	glActiveTexture(GL_TEXTURE0);

	// number of active lights
	uint elights = glGetUniformLocation(shaderProgram->ID, "elights");
	glUniform1i(elights, 1);

	// ambient lighting
	uint ambient = glGetUniformLocation(shaderProgram->ID, "ambient");
	glUniform3f(ambient, 0.4f, 0.4f, 0.4f);

	// lighting matrices
	mat4x4 lightModelMat;
	mat4x4 lightNormalMat;

	lightModelMat.loadIdentity();
	lightNormalMat.loadIdentity();

	uint modelView = glGetUniformLocation(shaderProgram->ID, "modelView");
	uint normalMat = glGetUniformLocation(shaderProgram->ID, "normalMat");
	glUniformMatrix4fv(modelView, 1, false, (float*)&lightModelMat);
	glUniformMatrix4fv(normalMat, 1, false, (float*)&lightNormalMat);

	// light data
	uint lightsId = glGetUniformLocation(shaderProgram->ID, "lights");
	vec3 lights[4][3];

	for (int i = 0; i < 4; i++) {
		memset(lights[i], 0, 3*sizeof(vec3));
	}
	lights[0][0] = vec3(1024, 1024, 1024); // light position
	lights[0][1] = vec3(1, 1, 1); // diffuse color
	glUniformMatrix3fv(lightsId, 4, false, (float*)lights);
	
	// bone transforms
	uint bonesId = glGetUniformLocation(shaderProgram->ID, "bones");
	glUniformMatrix4fv(bonesId, header->numbones, false, (float*)m_bonetransform);

	// chrome vars
	uint additiveEnable = glGetUniformLocation(shaderProgram->ID, "additiveEnable");
	uint chromeEnable = glGetUniformLocation(shaderProgram->ID, "chromeEnable");
	uint flatshadeEnable = glGetUniformLocation(shaderProgram->ID, "flatshadeEnable");
	uint viewerOriginId = glGetUniformLocation(shaderProgram->ID, "viewerOrigin");
	uint viewerRightId = glGetUniformLocation(shaderProgram->ID, "viewerRight");
	uint textureST = glGetUniformLocation(shaderProgram->ID, "textureST");

	shaderProgram->pushMatrix(MAT_MODEL);
	shaderProgram->modelMat->loadIdentity();
	shaderProgram->modelMat->translate(origin.x, origin.y, origin.z);
	shaderProgram->modelMat->rotateY(angles.z);
	shaderProgram->modelMat->rotateZ(angles.x);
	shaderProgram->modelMat->rotateX(angles.y);
	shaderProgram->updateMatrixes();

	data.seek(header->bodypartindex);
	mstudiobodyparts_t* bod = (mstudiobodyparts_t*)data.get();

	int meshCount = 0;
	for (int b = 0; b < header->numbodyparts; b++) {
		// Try loading required model info
		data.seek(header->bodypartindex + b * sizeof(mstudiobodyparts_t));
		mstudiobodyparts_t* bod = (mstudiobodyparts_t*)data.get();

		for (int i = 0; i < bod->nummodels; i++) {
			data.seek(bod->modelindex + i * sizeof(mstudiomodel_t));
			mstudiomodel_t* mod = (mstudiomodel_t*)data.get();

			for (int k = 0; k < mod->nummesh; k++) {
				meshCount++;
				MdlMeshRender& render = meshBuffers[b][i][k];
				render.texture->bind();

				if (render.flags & STUDIO_NF_ADDITIVE) {
					glEnable(GL_BLEND);
					glUniform1i(additiveEnable, 1);
				}
				else {
					glDisable(GL_BLEND);
					glUniform1i(additiveEnable, 0);
				}

				int flatShade = 0;
				if (render.flags & STUDIO_NF_FULLBRIGHT) {
					flatShade = 2;
				} else if (render.flags & STUDIO_NF_FLATSHADE) {
					flatShade = 1;
				}

				glUniform1i(flatshadeEnable, flatShade);

				if (render.flags & STUDIO_NF_CHROME) {
					const float s = 1.0 / (float)render.texture->width;
					const float t = 1.0 / (float)render.texture->height;
					
					glUniform1i(chromeEnable, 1);
					glUniform3f(viewerOriginId, viewerOrigin.x, viewerOrigin.y, viewerOrigin.z);
					glUniform3f(viewerRightId, viewerRight.x, viewerRight.y, viewerRight.z);
					glUniform2f(textureST, s, t);
				}
				else {
					glUniform1i(chromeEnable, 0);
				}

				render.buffer->draw(GL_TRIANGLES);
			}
		}
	}

	//logf("Draw %d meshes\n", meshCount);

	shaderProgram->popMatrix(MAT_MODEL);
}

MdlRenderer::~MdlRenderer() {
	delete[] data.getBuffer();
	header = NULL;
}