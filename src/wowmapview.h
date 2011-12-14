#ifndef WOWMAPVIEW_H
#define WOWMAPVIEW_H

#include <vector>
#include <string>
#include "appstate.h"
#include "font.h"
/// XXX this really needs to be refactored into a singleton class

#define APP_TITLE "WoW Map Viewer"
#define APP_VERSION "v0.7.0.0"
#define APP_BUILD "r20"


extern std::vector<AppState*> gStates;
extern bool gPop;

extern Font *f16, *f24, *f32;

extern float gFPS;

float frand();
float randfloat(float lower, float upper);
int randint(int lower, int upper);
void fixname(std::string &name);
void fixnamen(char *name, size_t len);

extern int fullscreen;
// Area database
class AreaDB;
extern AreaDB gAreaDB;

#endif
