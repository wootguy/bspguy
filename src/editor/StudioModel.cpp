#include "Model.h"
#include "util.h"
#include <iostream>
#include <fstream>
#include <string.h>
#include "lib/json.hpp"
#include "lib/md5.h"

using json::JSON;

Model::Model(string fpath)
{
	this->fpath = fpath;
	int len;
	char * buffer = loadFile(fpath, len);
	data = mstream(buffer, len);
	header = (studiohdr_t*)buffer;
}

Model::~Model()
{
	delete[] data.getBuffer();
}

bool Model::validate() {
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

	// Try loading required model info
	data.seek(header->bodypartindex);
	mstudiobodyparts_t* bod = (mstudiobodyparts_t*)data.get();

	if (data.eom())
		return false;

	for (int i = 0; i < bod->nummodels; i++) {
		data.seek(bod->modelindex + i * sizeof(mstudiomodel_t));
		mstudiomodel_t* mod = (mstudiomodel_t*)data.get();

		if (data.eom()) {
			cout << "ERROR: Failed to load body " + to_string(i) + "/" + to_string(bod->nummodels) + "\n";
			return false;
		}

		for (int k = 0; k < mod->nummesh; k++) {
			data.seek(mod->meshindex + i * sizeof(mstudiomesh_t));

			if (data.eom()) {
				cout << "ERROR: Failed to load mesh " + to_string(k) + " in model " + to_string(i) + "\n";
				return false;
			}

			mstudiomesh_t* mesh = (mstudiomesh_t*)data.get();

			data.seek(mesh->normindex + (mesh->numnorms*sizeof(vec3)) - 1);
			if (data.eom()) {
				cout << "ERROR: Failed to load normals for mesh " + to_string(k) + " in model " + to_string(i) + "\n";
				return false;
			}

			data.seek(mesh->triindex + (mesh->numtris * sizeof(mstudiotrivert_t)*3) - 1);
			if (data.eom()) {
				cout << "ERROR: Failed to load triangles for mesh " + to_string(k) + " in model " + to_string(i) + "\n";
				return false;
			}
		}
	}

	for (int i = 0; i < header->numseq; i++) {
		data.seek(header->seqindex + i*sizeof(mstudioseqdesc_t));
		if (data.eom()) {
			cout << "ERROR: Failed to load sequence " + to_string(i) + "/" + to_string(header->numseq) + "\n";
			return false;
		}

		mstudioseqdesc_t* seq = (mstudioseqdesc_t*)data.get();

		for (int k = 0; k < seq->numevents; k++) {
			data.seek(seq->eventindex + k*sizeof(mstudioevent_t));
			
			if (data.eom()) {
				cout << "ERROR: Failed to load event " + to_string(k) + "/" + to_string(seq->numevents) + " in sequence " + to_string(i) +"\n";
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
		data.seek(tex->index + (tex->width*tex->height + 256*3) - 1);
		if (data.eom()) {
			cout << "ERROR: Failed to load texture data " + to_string(i) + "/" + to_string(header->numtextures) + "\n";
			return false;
		}
	}

	for (int i = 0; i < header->numskinfamilies; i++) {
		data.seek(header->skinindex + i * sizeof(short)*header->numskinref);
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

bool Model::isEmpty() {
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

bool Model::hasExternalTextures() {
	// textures aren't needed if the model has no triangles
	return header->numtextures == 0 && !isEmpty();
}

bool Model::hasExternalSequences() {
	return header->numseqgroups > 1;
}

void Model::insertData(void* src, size_t bytes) {
	data.insert(src, bytes);
	header = (studiohdr_t*)data.getBuffer();
}

void Model::removeData(size_t bytes) {
	data.remove(bytes);
	header = (studiohdr_t*)data.getBuffer();
}

bool Model::mergeExternalTextures(bool deleteSource) {
	if (!hasExternalTextures()) {
		cout << "No external textures to merge\n";
		return false;
	}

	int lastDot = fpath.find_last_of(".");
	string ext = fpath.substr(lastDot);
	string basepath = fpath.substr(0, lastDot);
	string tpath = basepath + "t" + ext;

	if (!fileExists(tpath)) {
		tpath = basepath + "T" + ext;
		if (!fileExists(tpath)) {
			cout << "External texture model not found: " << tpath << endl;
			return false;
		}
	}

	int lastSlash = tpath.find_last_of("/\\");
	string fname = tpath.substr(lastSlash + 1);
	cout << "Merging " << fname << "\n";

	Model tmodel(tpath);

	// copy info
	header->numtextures = tmodel.header->numtextures;
	header->numskinref = tmodel.header->numskinref;
	header->numskinfamilies = tmodel.header->numskinfamilies;

	// recalculate indexes
	size_t actualtextureindex = data.size();
	size_t tmodel_textureindex = tmodel.header->textureindex;
	size_t tmodel_skinindex = tmodel.header->skinindex;
	size_t tmodel_texturedataindex = tmodel.header->texturedataindex;
	header->textureindex = actualtextureindex;
	header->skinindex = actualtextureindex + (tmodel_skinindex - tmodel_textureindex);
	header->texturedataindex = actualtextureindex + (tmodel_texturedataindex - tmodel_textureindex);

	// texture data is at the end of the file, with the structures grouped together
	data.seek(0, SEEK_END);
	tmodel.data.seek(tmodel_textureindex);
	insertData(tmodel.data.get(), tmodel.data.size() - tmodel.data.tell());

	// TODO: Align pointers or else model will crash or something? My test model has everything aligned.
	uint aligned = ((uint)header->texturedataindex + 3) & ~3;
	if (header->texturedataindex != aligned) {
		cout << "dataindex not aligned " << header->texturedataindex << endl;
	}
	aligned = ((uint)header->textureindex + 3) & ~3;
	if (header->textureindex != aligned) {
		cout << "texindex not aligned " << header->textureindex << endl;
	}
	aligned = ((uint)header->skinindex + 3) & ~3;
	if (header->skinindex != aligned) {
		cout << "skinindex not aligned " << header->skinindex << endl;
	}

	// recalculate indexes in the texture infos
	for (int i = 0; i < header->numtextures; i++) {
		data.seek(header->textureindex + i * sizeof(mstudiotexture_t));
		mstudiotexture_t* texture = (mstudiotexture_t*)data.get();
		texture->index = actualtextureindex + (texture->index - tmodel_textureindex);
	}

	header->length = data.size();

	if (deleteSource)
		remove(tpath.c_str());

	return true;
}

#define MOVE_INDEX(val, afterIdx, delta) { \
	if (val >= afterIdx) { \
		val += delta; \
		/*cout << "Updated: " << #val << endl;*/ \
	} \
}

void Model::updateIndexes(int afterIdx, int delta) {
	// skeleton
	MOVE_INDEX(header->boneindex, afterIdx, delta);
	MOVE_INDEX(header->bonecontrollerindex, afterIdx, delta);
	MOVE_INDEX(header->attachmentindex, afterIdx, delta);
	MOVE_INDEX(header->hitboxindex, afterIdx, delta);

	// sequences
	MOVE_INDEX(header->seqindex, afterIdx, delta);
	MOVE_INDEX(header->seqgroupindex, afterIdx, delta);
	MOVE_INDEX(header->transitionindex, afterIdx, delta);
	for (int k = 0; k < header->numseq; k++) {
		data.seek(header->seqindex + k * sizeof(mstudioseqdesc_t));
		mstudioseqdesc_t* seq = (mstudioseqdesc_t*)data.get();

		MOVE_INDEX(seq->eventindex, afterIdx, delta);
		MOVE_INDEX(seq->animindex, afterIdx, delta);
		MOVE_INDEX(seq->pivotindex, afterIdx, delta);
		MOVE_INDEX(seq->automoveposindex, afterIdx, delta);		// unused?
		MOVE_INDEX(seq->automoveangleindex, afterIdx, delta);	// unused?
	}

	// meshes
	MOVE_INDEX(header->bodypartindex, afterIdx, delta);
	for (int i = 0; i < header->numbodyparts; i++) {
		data.seek(header->bodypartindex + i*sizeof(mstudiobodyparts_t));
		mstudiobodyparts_t* bod = (mstudiobodyparts_t*)data.get();
		MOVE_INDEX(bod->modelindex, afterIdx, delta);
		for (int k = 0; k < bod->nummodels; k++) {
			data.seek(bod->modelindex + k * sizeof(mstudiomodel_t));
			mstudiomodel_t* mod = (mstudiomodel_t*)data.get();

			MOVE_INDEX(mod->meshindex, afterIdx, delta);
			for (int j = 0; j < mod->nummesh; j++) {
				data.seek(mod->meshindex + j * sizeof(mstudiomesh_t));
				mstudiomesh_t* mesh = (mstudiomesh_t*)data.get();
				MOVE_INDEX(mesh->normindex, afterIdx, delta); // TODO: is this a file index?
				MOVE_INDEX(mesh->triindex, afterIdx, delta);
			}
			MOVE_INDEX(mod->normindex, afterIdx, delta);
			MOVE_INDEX(mod->norminfoindex, afterIdx, delta);
			MOVE_INDEX(mod->vertindex, afterIdx, delta);
			MOVE_INDEX(mod->vertinfoindex, afterIdx, delta);
		}
	}

	// textures
	MOVE_INDEX(header->textureindex, afterIdx, delta);
	for (int i = 0; i < header->numtextures; i++) {
		data.seek(header->textureindex + i * sizeof(mstudiotexture_t));
		mstudiotexture_t* texture = (mstudiotexture_t*)data.get();
		MOVE_INDEX(texture->index, afterIdx, delta);
	}
	MOVE_INDEX(header->skinindex, afterIdx, delta);
	MOVE_INDEX(header->texturedataindex, afterIdx, delta);

	// sounds (unused?)
	MOVE_INDEX(header->soundindex, afterIdx, delta);
	MOVE_INDEX(header->soundgroupindex, afterIdx, delta);

	header->length = data.size();
}

bool Model::mergeExternalSequences(bool deleteSource) {
	if (!hasExternalSequences()) {
		cout << "No external sequences to merge\n";
		return false;
	}

	int lastDot = fpath.find_last_of(".");
	string ext = fpath.substr(lastDot);
	string basepath = fpath.substr(0, lastDot);

	// save external animations to the end of the file.
	data.seek(0, SEEK_END);

	// save old values before they're overwritten in index updates
	int* oldanimindexes = new int[header->numseq];
	for (int k = 0; k < header->numseq; k++) {
		data.seek(header->seqindex + k * sizeof(mstudioseqdesc_t));
		mstudioseqdesc_t* seq = (mstudioseqdesc_t*)data.get();
		oldanimindexes[k] = seq->animindex;
	}

	for (int i = 1; i < header->numseqgroups; i++)
	{
		string suffix = i < 10 ? "0" + to_string(i) : to_string(i);
		string spath = basepath + suffix + ext;

		if (!fileExists(spath)) {
			cout << "External sequence model not found: " << spath << endl;
			return false;
		}

		int lastSlash = spath.find_last_of("/\\");
		string fname = spath.substr(lastSlash + 1);
		cout << "Merging " << fname << "\n";

		Model smodel(spath);

		// Sequence models contain a header followed by animation data.
		// This will append those animations after the primary model's animations.
		data.seek(header->seqindex);
		smodel.data.seek(sizeof(studioseqhdr_t));
		size_t insertOffset = data.tell();
		size_t animCopySize = smodel.data.size() - sizeof(studioseqhdr_t);
		insertData(smodel.data.get(), animCopySize);
		updateIndexes(insertOffset, animCopySize);

		// update indexes for the merged animations
		for (int k = 0; k < header->numseq; k++) {
			data.seek(header->seqindex + k * sizeof(mstudioseqdesc_t));
			mstudioseqdesc_t* seq = (mstudioseqdesc_t*)data.get();

			if (seq->seqgroup != i)
				continue;

			seq->animindex = insertOffset + (oldanimindexes[k] - sizeof(studioseqhdr_t));
			seq->seqgroup = 0;
		}

		if (deleteSource)
			remove(spath.c_str());
	}

	delete[] oldanimindexes;

	// remove infos for the merged sequence groups
	data.seek(header->seqgroupindex + sizeof(mstudioseqgroup_t));
	size_t removeOffset = data.tell();
	int removeBytes = sizeof(mstudioseqgroup_t) * (header->numseqgroups - 1);
	removeData(removeBytes);
	header->numseqgroups = 1;
	updateIndexes(removeOffset, -removeBytes);

	return true;
}

bool Model::cropTexture(string cropName, int newWidth, int newHeight) {
	for (int i = 0; i < header->numtextures; i++) {
		data.seek(header->textureindex + i * sizeof(mstudiotexture_t));
		mstudiotexture_t* texture = (mstudiotexture_t*)data.get();
		string name = texture->name;

		if (string(texture->name) != cropName) {
			continue;
		}

		cout << "Cropping " << cropName << " from " << texture->width << "x" << texture->height <<
			" to " << newWidth << "x" << newHeight << endl;

		data.seek(texture->index);
		int oldSize = texture->width * texture->height;
		int newSize = newWidth * newHeight;
		int palSize = 256 * 3;
		byte* oldTexData = new byte[oldSize];
		byte* palette = new byte[palSize];
		byte* newTexData = new byte[newSize];
		data.read(oldTexData, oldSize);
		data.read(palette, palSize);
		
		for (int y = 0; y < newHeight; y++) {
			for (int x = 0; x < newWidth; x++) {
				int oldY = y >= texture->height ? texture->height-1 : y;
				int oldX = x >= texture->width ? texture->width -1 : x;
				newTexData[y * newWidth + x] = oldTexData[oldY*texture->width + oldX];
			}
		}

		data.seek(texture->index);
		data.write(newTexData, newSize);
		data.write(palette, palSize);

		texture->width = newWidth;
		texture->height = newHeight;

		size_t removeAt = data.tell();
		int removeBytes = oldSize - newSize;
		removeData(removeBytes);
		updateIndexes(removeAt, -removeBytes);

		header->length = data.size();

		return true;
	}

	cout << "ERROR: No texture found with name '" << cropName << "'\n";
	return false;
}

bool Model::renameTexture(string oldName, string newName) {
	for (int i = 0; i < header->numtextures; i++) {
		data.seek(header->textureindex + i * sizeof(mstudiotexture_t));
		mstudiotexture_t* texture = (mstudiotexture_t*)data.get();

		if (string(texture->name) == oldName) {
			strncpy(texture->name, newName.c_str(), 64);
			return true;
		}
	}

	cout << "ERROR: No texture found with name '" << oldName << "'\n";
	return false;
}

void Model::write(string fpath) {
	fstream fout = fstream(fpath.c_str(), std::ios::out | std::ios::binary);
	fout.write(data.getBuffer(), data.size());
	cout << "Wrote " << fpath << " (" << data.size() << " bytes)\n";
}

vector<vec3> Model::getVertexes(bool hdBody) {
	vector<vec3> verts;

	for (int k = 0; k < header->numbodyparts; k++) {
		data.seek(header->bodypartindex + k*sizeof(mstudiobodyparts_t));
		mstudiobodyparts_t* bod = (mstudiobodyparts_t*)data.get();
		if (data.eom()) { return verts; }

		for (int i = 0; i < bod->nummodels; i++) {
			if (!hdBody && i != 0) {
				continue;
			}
			else if (hdBody && i != bod->nummodels - 1) {
				continue;
			}

			data.seek(bod->modelindex + i * sizeof(mstudiomodel_t));
			if (data.eom()) { return verts; }

			mstudiomodel_t* mod = (mstudiomodel_t*)data.get();
			data.seek(mod->vertindex);
			if (data.eom()) { return verts; }

			vec3* vertSrc = (vec3 * )data.get();

			verts.reserve(mod->numverts);
			for (int v = 0; v < mod->numverts; v++) {
				verts.push_back(vertSrc[v]);
			}
		}
	}

	return verts;
}

void Model::dump_info(string outputPath) {
	JSON obj = json::Object();

	obj["seq_groups"] = to_string(header->numseqgroups);
	obj["t_model"] = hasExternalTextures();

	if (hasExternalTextures())
		mergeExternalTextures(false);
	if (hasExternalSequences())
		mergeExternalSequences(false);

	obj["size"] = data.size();

	MD5 hash = MD5();
	hash.add(data.getBuffer(), data.size());
	obj["md5"] = hash.getHash();

	JSON jbodies = json::Array();
	for (int k = 0; k < header->numbodyparts; k++) {
		data.seek(header->bodypartindex + k * sizeof(mstudiobodyparts_t));
		mstudiobodyparts_t* bod = (mstudiobodyparts_t*)data.get();

		JSON jbody = json::Object();
		JSON jmodels = json::Array();

		for (int i = 0; i < bod->nummodels; i++) {
			data.seek(bod->modelindex + i * sizeof(mstudiomodel_t));
			mstudiomodel_t* mod = (mstudiomodel_t*)data.get();

			JSON jmodel = json::Object();
			int polyCount = 0;

			for (int i = 0; i < mod->nummesh; i++) {
				data.seek(mod->meshindex + i * sizeof(mstudiomesh_t));
				mstudiomesh_t* mesh = (mstudiomesh_t*)data.get();
				polyCount += mesh->numtris;
			}

			jmodel["name"] = sanitize_string(mod->name);
			jmodel["polys"] = polyCount;
			jmodel["verts"] = mod->numverts;
			jmodels.append(jmodel);
		}

		jbody["name"] = sanitize_string(bod->name);
		jbody["models"] = jmodels;
		jbodies.append(jbody);
	}

	JSON jtextures = json::Array();
	for (int i = 0; i < header->numtextures; i++) {
		data.seek(header->textureindex + i * sizeof(mstudiotexture_t));
		mstudiotexture_t* texture = (mstudiotexture_t*)data.get();

		JSON jtexture = json::Object();
		jtexture["name"] = sanitize_string(texture->name);
		jtexture["flags"] = texture->flags;
		jtexture["width"] = texture->width;
		jtexture["height"] = texture->height;
		jtextures.append(jtexture);
	}

	JSON jseqs = json::Array();
	JSON jevents = json::Array();
	for (int i = 0; i < header->numseq; i++) {
		data.seek(header->seqindex + i * sizeof(mstudioseqdesc_t));
		mstudioseqdesc_t* seq = (mstudioseqdesc_t*)data.get();

		
		for (int k = 0; k < seq->numevents; k++) {
			data.seek(seq->eventindex + k * sizeof(mstudioevent_t));
			mstudioevent_t* evt = (mstudioevent_t*)data.get();

			JSON jevent = json::Object();
			jevent["event"] = evt->event;
			jevent["type"] = evt->type;
			jevent["sequence"] = i;
			jevent["frame"] = evt->frame;
			jevent["options"] = sanitize_string(evt->options);
			jevents.append(jevent);
		}

		JSON jseq = json::Object();
		jseq["name"] = sanitize_string(seq->label);
		jseq["fps"] = seq->fps;
		jseq["frames"] = seq->numframes;
		jseqs.append(jseq);
	}

	JSON jskel = json::Array();
	for (int i = 0; i < header->numbones; i++) {
		data.seek(header->boneindex + i * sizeof(mstudiobone_t));
		mstudiobone_t* bone = (mstudiobone_t*)data.get();

		JSON jbone = json::Object();
		jbone["name"] = sanitize_string(bone->name);
		jbone["parent"] = bone->parent;
		jskel.append(jbone);
	}

	JSON jattachments = json::Array();
	for (int i = 0; i < header->numattachments; i++) {
		data.seek(header->attachmentindex + i * sizeof(mstudioattachment_t));
		mstudioattachment_t* att = (mstudioattachment_t*)data.get();

		JSON jatt = json::Object();
		jatt["name"] = sanitize_string(att->name);
		jatt["bone"] = att->bone;
		jatt["type"] = att->type;
		jattachments.append(jatt);
	}

	JSON jctls = json::Array();
	for (int i = 0; i < header->numbonecontrollers; i++) {
		data.seek(header->bonecontrollerindex + i * sizeof(mstudiobonecontroller_t));
		mstudiobonecontroller_t* ctl = (mstudiobonecontroller_t*)data.get();

		JSON jctl = json::Object();
		jctl["type"] = ctl->type;
		jctl["index"] = ctl->index;
		jctl["bone"] = ctl->bone;
		jctl["start"] = ctl->start;
		jctl["end"] = ctl->end;
		jctl["rest"] = ctl->rest;
		jctls.append(jctl);
	}

	obj["name"] = sanitize_string(header->name);
	obj["textures"] = jtextures;
	obj["bodies"] = jbodies;
	obj["sequences"] = jseqs;
	obj["events"] = jevents;
	obj["skeleton"] = jskel;
	obj["controllers"] = jctls;
	obj["attachments"] = jattachments;
	obj["skins"] = header->numskinfamilies;
	obj["id"] = header->id;
	obj["version"] = header->version;

	ofstream fout;
	fout.open(outputPath);
	fout << obj << endl;
	fout.close();
}