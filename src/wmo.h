#ifndef WMO_H
#define WMO_H

#include "manager.h"
#include "vec3d.h"
#include "mpq.h"
#include "model.h"
#include <vector>
#include <set>
#include "video.h"

class WMO;
class WMOGroup;
class WMOInstance;
class WMOManager;
class Liquid;


class WMOGroup {
	WMO *wmo;
	int flags;
	Vec3D v1,v2;
	int nTriangles, nVertices;
	//GLuint dl,dl_light;
	Vec3D center;
	float rad;
	int num;
	int fog;
	int nDoodads;
	unsigned int nBatches;
	short *ddr;
	Liquid *lq;
	std::vector< std::pair<GLuint, int> > lists;
public:
	Vec3D b1,b2;
	Vec3D vmin, vmax;
	bool indoor, hascv;
	bool visible;

	bool outdoorLights;
	std::string name;

	WMOGroup():nBatches(0) {}
	~WMOGroup();
	void init(WMO *wmo, MPQFile &f, int num, char *names);
	void initDisplayList();
	void initLighting(int nLR, short *useLights);
	void draw(const Vec3D& ofs, const float rot);
	void drawLiquid();
	void drawDoodads(int doodadset, const Vec3D& ofs, const float rot);
	void setupFog();
};

struct WMOMaterial {
	int flags;
	int specular;
	int transparent; // Blending: 0 for opaque, 1 for transparent
	int nameStart; // Start position for the first texture filename in the MOTX data block	
	unsigned int col1; // color
	int d3; // flag
	int nameEnd; // Start position for the second texture filename in the MOTX data block
	unsigned int col2; // color
	int d4; // flag
	unsigned int col3;
	float f2;
	float diffColor[3];
	int texture1; // this is the first texture object. of course only in RAM. leave this alone. :D
	int texture2; // this is the second texture object.
	// read up to here -_-
	TextureID tex;
};

enum LightType 
{
	OMNI_LGT,
	SPOT_LGT,
	DIRECT_LGT,
	AMBIENT_LGT
};

struct WMOLight {
	uint8 LightType;
	uint8 type;
	uint8 useAtten;
	uint8 pad;
	unsigned int color;
	Vec3D pos;
	float intensity;
	float attenStart;
	float attenEnd;
	float unk[4];
	// struct read stop
	Vec4D fcolor;

	void init(MPQFile &f);
	void setup(GLint light);

	static void setupOnce(GLint light, Vec3D dir, Vec4D lcol);
};

struct WMOPV {
	Vec3D a,b,c,d;
};

struct WMOPR {
	short portal, group, dir, reserved;
};

struct WMODoodadSet {
	char name[0x14]; // Set name
	int start; // index of first doodad instance in this set
	int size; // number of doodad instances in this set
	int unused; // always 0
};

struct WMOLiquidHeader {
	int X, Y, A, B;
	Vec3D pos;
	short type;
};

struct WMOFog {
	unsigned int flags;
	Vec3D pos;
	float r1; // Smaller radius
	float r2; // Larger radius
	float fogend; // Fog end
	float fogstart; // Fog start multiplier (0..1)
	unsigned int color1; // The back buffer is also cleared to this colour 
	float f2; // Unknown (almost always 222.222)
	float f3; // Unknown (-1 or -0.5)
	unsigned int color2;
	// read to here (0x30 bytes)
	Vec4D color;
	void init(MPQFile &f);
	void setup();
};

class WMO: public ManagedItem {
public:
	WMOGroup *groups;
	int nTextures, nGroups, nP, nLights, nModels, nDoodads, nDoodadSets, nX;
	WMOMaterial *mat;
	Vec3D v1,v2;
	int LiquidType;
	bool ok;
	std::vector<std::string> textures;
	std::vector<std::string> models;
	std::vector<ModelInstance> modelis;

	std::vector<WMOLight> lights;
	std::vector<WMOPV> pvs;
	std::vector<WMOPR> prs;

	std::vector<WMOFog> fogs;

	std::vector<WMODoodadSet> doodadsets;

	Model *skybox;
	int sbid;

	WMO(std::string name);
	~WMO();
	void draw(int doodadset, const Vec3D& ofs, const float rot);
	//void drawPortals();
	void drawSkybox();
};


class WMOManager: public SimpleManager {
public:
	int add(std::string name);
};

struct SMMapObjDef {
	uint32 nameId;
	uint32 id;
	Vec3D pos;
	Vec3D dir;
	Vec3D pos2, pos3;
	uint16 flags;
	uint16 doodadset;
	uint16 nameset;
	uint16 unk;
};

class WMOInstance {
	static std::set<int> ids;
public:
	WMO *wmo;

	uint32 id;
	Vec3D pos;
	Vec3D dir;
	Vec3D pos2, pos3;
	uint16 flags;
	uint16 doodadset;
	uint16 nameset;
	uint16 unk;

	WMOInstance(WMO *wmo, MPQFile &f);
	void draw();
	//void drawPortals();

	static void reset();
};


#endif
