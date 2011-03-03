#ifndef MODEL_H
#define MODEL_H

// C++ files
#include <vector>
#include "video.h"
#include "vec3d.h"

class Model;
class Bone;
Vec3D fixCoordSystem(Vec3D v);

#include "manager.h"
#include "mpq.h"

#include "modelheaders.h"
#include "quaternion.h"
#include "matrix.h"

#include "animated.h"
#include "particle.h"


class Bone {
	Animated<Vec3D> trans;
	//Animated<Quaternion> rot;
	Animated<Quaternion, PACK_QUATERNION, Quat16ToQuat32> rot;
	Animated<Vec3D> scale;

public:
	Vec3D pivot, transPivot;
	int16 parent;

	bool billboard;
	Matrix mat;
	Matrix mrot;

	bool calc;
	void calcMatrix(Bone* allbones, int anim, int time);
	void init(MPQFile &f, ModelBoneDef &b, uint32 *global, MPQFile *animfiles);

};


class TextureAnim {
	Animated<Vec3D> trans, rot, scale;

public:
	Vec3D tval, rval, sval;

	void calc(int anim, int time);
	void init(MPQFile &f, ModelTexAnimDef &mta, uint32 *global);
	void setup(int anim);
};

struct ModelColor {
	Animated<Vec3D> color;
	AnimatedShort opacity;

	void init(MPQFile &f, ModelColorDef &mcd, uint32 *global);
};

struct ModelTransparency {
	AnimatedShort trans;

	void init(MPQFile &f, ModelTransDef &mtd, uint32 *global);
};

// copied from the .mdl docs? this might be completely wrong
enum BlendModes {
	BM_OPAQUE,
	BM_TRANSPARENT,
	BM_ALPHA_BLEND,
	BM_ADDITIVE,
	BM_ADDITIVE_ALPHA,
	BM_MODULATE,
	BM_MODULATE2
};

struct ModelRenderPass {
	uint32 indexStart, indexCount, vertexStart, vertexEnd;
	//TextureID texture, texture2;
	int tex;
	bool usetex2, useEnvMap, cull, trans, unlit, noZWrite, billboard;
	float p;
	
	int16 texanim, color, opacity, blendmode;
	uint16 order;

	// Geoset ID
	int geoset;

	// texture wrapping
	bool swrap, twrap;

	// colours
	Vec4D ocol, ecol;

	bool init(Model *m);
	void deinit();

	bool operator< (const ModelRenderPass &m) const
	{
		//return !trans;
		if (order<m.order) return true;
		else if (order>m.order) return false;
		else return blendmode == m.blendmode ? (p<m.p) : blendmode < m.blendmode;
	}
};

struct ModelCamera {
	bool ok;

	Vec3D pos, target;
	float nearclip, farclip, fov;
	Animated<Vec3D> tPos, tTarget;
	Animated<float> rot;

	void init(MPQFile &f, ModelCameraDef &mcd, uint32 *global);
	void setup(int time=0);

	ModelCamera():ok(false) {}
};

struct ModelLight {
	int type, parent;
	Vec3D pos, tpos, dir, tdir;
	Animated<Vec3D> diffColor, ambColor;
	Animated<float> diffIntensity, ambIntensity;

	void init(MPQFile &f, ModelLightDef &mld, uint32 *global);
	void setup(int time, GLuint l);
};

class Model: public ManagedItem {

	GLuint dlist;
	GLuint vbuf, nbuf, tbuf;
	size_t vbufsize;
	bool animated;
	bool animGeometry,animTextures,animBones;

	bool forceAnim;
	MPQFile *animfiles;

	void init(MPQFile &f);

	ModelHeader header;
	TextureAnim *texAnims;
	ModelAnimation *anims;
	uint32 *globalSequences;
	ModelColor *colors;
	ModelTransparency *transparency;
	ModelLight *lights;
	ParticleSystem *particleSystems;
	RibbonEmitter *ribbons;

	void drawModel();
	void initCommon(MPQFile &f);
	bool isAnimated(MPQFile &f);
	void initAnimated(MPQFile &f);
	void initStatic(MPQFile &f);

	ModelVertex *origVertices;
	Vec3D *vertices, *normals;
	uint16 *indices;
	size_t nIndices;
	std::vector<ModelRenderPass> passes;

	void animate(int anim);
	void calcBones(int anim, int time);

	void lightsOn(GLuint lbase);
	void lightsOff(GLuint lbase);

public:
	ModelCamera cam;
	Bone *bones;

	// ===============================
	// Toggles
	bool *showGeosets;

	// ===============================
	// Texture data
	// ===============================
	TextureID *textures;
	int specialTextures[TEXTURE_MAX];
	GLuint replaceTextures[TEXTURE_MAX];
	bool useReplaceTextures[TEXTURE_MAX];


	bool ok;
	bool ind;

	float rad;
	float trans;
	bool animcalc;
	int anim, animtime;
	std::string fullname;

	Model(std::string name, bool forceAnim=false);
	~Model();
	void draw();
	void updateEmitters(float dt);

	friend struct ModelRenderPass;
};

class ModelManager: public SimpleManager {
public:
	int add(std::string name);

	ModelManager() : v(0) {}

	int v;

	void resetAnim();
	void updateEmitters(float dt);

};


class ModelInstance {
public:
	Model *model;

	int id;

	Vec3D pos, dir;
	unsigned int scale;

	float frot,w,sc;

	int light;
	Vec3D ldir;
	Vec4D lcol;

	ModelInstance() {}
	ModelInstance(Model *m, MPQFile &f);
    void init2(Model *m, MPQFile &f);
	void draw();
	void draw2(const Vec3D& ofs, const float rot);

};

#endif
