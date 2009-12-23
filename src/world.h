#ifndef WORLD_H
#define WORLD_H

#include "wowmapview.h"
#include "maptile.h"
#include "wmo.h"
#include "frustum.h"
#include "sky.h"

#include <string>

#define MAPTILECACHESIZE 16

const float detail_size = 8.0f;

class World {

	MapTile *maptilecache[MAPTILECACHESIZE];
	MapTile *current[3][3];
	int ex,ez;

public:

	std::string basename;
	int mapid;

	bool maps[64][64];
	GLuint lowrestiles[64][64];
	bool autoheight;

	std::vector<std::string> gwmos;
	std::vector<WMOInstance> gwmois;
	int gnWMO, nMaps;

	float mapdrawdistance, modeldrawdistance, doodaddrawdistance, highresdistance;
	float mapdrawdistance2, modeldrawdistance2, doodaddrawdistance2, highresdistance2;

	float culldistance, culldistance2, fogdistance;

	float l_const, l_linear, l_quadratic;

	Skies *skies;
	float time,animtime;

    bool hadSky;

	bool thirdperson,lighting,drawmodels,drawdoodads,drawterrain,drawwmo,loading,drawhighres,drawfog;
	bool uselowlod;
	bool useshaders;

	GLuint detailtexcoords, alphatexcoords;

	short *mapstrip,*mapstrip2;

	TextureID water;
	Vec3D camera, lookat;
	Frustum frustum;
	int cx,cz;
	bool oob;

	WMOManager wmomanager;
	ModelManager modelmanager;

	OutdoorLighting *ol;
	OutdoorLightStats outdoorLightStats;

	GLuint minimap;

	World(const char* name, int id);
	~World();
	void init();
	void initMinimap();
	void initDisplay();
	void initWMOs();
	void initLowresTerrain();

	void enterTile(int x, int z);
	MapTile *loadTile(int x, int z);
	void tick(float dt);
	void draw();

	void outdoorLighting();
	void outdoorLights(bool on);
	void setupFog();

	/// Get the tile on wich the camera currently is on
	unsigned int getAreaID();
};


extern World *gWorld;


void lightingDefaults();
void myFakeLighting();



#endif
