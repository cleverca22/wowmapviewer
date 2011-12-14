#ifdef _WIN32
#pragma comment(lib,"OpenGL32.lib")
#pragma comment(lib,"glu32.lib")
#pragma comment(lib,"SDL.lib")
#pragma comment(lib,"SDLmain.lib")
//#pragma comment(lib,"SFmpq.lib")

#define NOMINMAX
#include <windows.h>
#include <winerror.h>
#endif

#include <ctime>
#include <cstdlib>

#include "util.h"
#include "mpq.h"
#include "video.h"
#include "appstate.h"

#include "test.h"
#include "menu.h"
#include "areadb.h"

int fullscreen = 0;


std::vector<AppState*> gStates;
bool gPop = false;

float gFPS;

GLuint ftex;
Font *f16, *f24, *f32;

#ifdef _WINDOWS
void usleep(unsigned int x) {
     Sleep(x / 1000);
}
#endif

void initFonts()
{
	ftex = loadTGA("arial.tga",false);

	f16 = new Font(ftex, 256, 256, 16, "arial.info");
	f24 = new Font(ftex, 256, 256, 24, "arial.info");
	f32 = new Font(ftex, 256, 256, 32, "arial.info");
}

void deleteFonts()
{
	glDeleteTextures(1, &ftex);

	delete f16;
	delete f24;
	delete f32;
}

#ifdef _WINDOWS
// HACK: my stupid compiler wont use main()
int CALLBACK WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	char *argv[] = { "wowmapview.exe", "-w" };
	return main(2,argv);
}
#endif

int main(int argc, char *argv[])
{
	const char *override_game_path = NULL;
	srand((unsigned int)time(0));
	check_stuff();

	int xres = 1024;
	int yres = 768;

	bool usePatch = false;

	for (int i=1; i<argc; i++) {
		if (!strcmp(argv[i],"-gamepath")) {
			i++;
			override_game_path = argv[i];
		} else if (!strcmp(argv[i],"-f")) fullscreen = 1;
		else if (!strcmp(argv[i],"-w")) fullscreen = 0;
		else if (!strcmp(argv[i],"-1024") || !strcmp(argv[i],"-1024x768")) {
			xres = 1024;
			yres = 768;
		}
		else if (!strcmp(argv[i],"-1280") || !strcmp(argv[i],"-1280x1024")) {
			xres = 1280;
			yres = 1024;
		}
		else if (!strcmp(argv[i],"-1280x960")) {
			xres = 1280;
			yres = 960;
		}
		else if (!strcmp(argv[i],"-1400") || !strcmp(argv[i],"-1400x1050")) {
			xres = 1400;
			yres = 1050;
		}
		else if (!strcmp(argv[i],"-1280x800")) {
			xres = 1280;
			yres = 800;
		}
		else if (!strcmp(argv[i],"-1600") || !strcmp(argv[i],"-1600x1200")) {
			xres = 1600;
			yres = 1200;
		}
		else if (!strcmp(argv[i],"-1920") || !strcmp(argv[i],"-1920x1200")) {
			xres = 1920;
			yres = 1200;
		}
		else if (!strcmp(argv[i],"-2048") || !strcmp(argv[i],"-2048x1536")) {
			xres = 2048;
			yres = 1536;
		}
		else if (!strcmp(argv[i],"-p")) usePatch = true;
		else if (!strcmp(argv[i],"-np")) usePatch = false;
	}

	if (override_game_path) {
		gamePath.empty();
		gamePath.append(override_game_path);
	} else getGamePath();

	char path[512];
	const char *test_files[] = { "common.MPQ", "art.MPQ" };
	bool path_valid = false;
	for (int i = 0; i < 2; i++) {
      sprintf(path,"%s%s",gamePath.c_str(),test_files[i]);
	    if (file_exists(path)) path_valid = true;
    }
    if (!path_valid) {
		printf("error finding art.MPQ in '%s', is gamepath correct?, try -gamepath fullpass to data/\n",gamePath.c_str());
		return -1;
	}

	gLog(APP_TITLE " " APP_VERSION " " APP_BUILD "\nGame path: %s\n", gamePath.c_str());

	std::vector<MPQArchive*> archives;
	
	int langID = 0;

	const char *locales[] = {"enUS", "enGB", "deDE", "frFR", "zhTW", "ruRU", "esES", "koKR", "zhCN"};

	for (size_t i=0; i<9; i++) {
		sprintf(path, "%s%s\\base-%s.MPQ", gamePath.c_str(), locales[i], locales[i]);
		if (file_exists(path)) {
			langID = i;
			break;
		}
	}
	gLog("Locale: %s\n", locales[langID]);

	if (usePatch) {
		// patch goes first -> fake priority handling
		sprintf(path, "%s%s", gamePath.c_str(), "patch-3.MPQ");
		archives.push_back(new MPQArchive(path));

		sprintf(path, "%s%s", gamePath.c_str(), "patch-2.MPQ");
		archives.push_back(new MPQArchive(path));

		sprintf(path, "%s%s", gamePath.c_str(), "patch.MPQ");
		archives.push_back(new MPQArchive(path));

		sprintf(path, "%s%s\\Patch-%s-2.MPQ", gamePath.c_str(), locales[langID], locales[langID]);
		archives.push_back(new MPQArchive(path));

		sprintf(path, "%s%s\\Patch-%s.MPQ", gamePath.c_str(), locales[langID], locales[langID]);
		archives.push_back(new MPQArchive(path));
	}

	const char* archiveNames[] = {"expansion3.MPQ", "expansion2.MPQ", "expansion1.MPQ", "world.MPQ", "sound.MPQ", "art.MPQ"};
	for (size_t i=0; i<6; i++) {
		sprintf(path, "%s%s", gamePath.c_str(), archiveNames[i]);
		archives.push_back(new MPQArchive(path));
	}

	sprintf(path, "%s%s/expansion3-locale-%s.MPQ", gamePath.c_str(), locales[langID], locales[langID]);
	archives.push_back(new MPQArchive(path));

	sprintf(path, "%s%s/expansion2-locale-%s.MPQ", gamePath.c_str(), locales[langID], locales[langID]);
	archives.push_back(new MPQArchive(path));

	sprintf(path, "%s%s/expansion1-locale-%s.MPQ", gamePath.c_str(), locales[langID], locales[langID]);
	archives.push_back(new MPQArchive(path));

	sprintf(path, "%s%s/locale-%s.MPQ", gamePath.c_str(), locales[langID], locales[langID]);
	archives.push_back(new MPQArchive(path));

	const char *updates[] = { "13914", "14007", "14333", "14480", "14545", "14946", "15005", "15050" };
	for (size_t i = 0; i < 8; i++) {
		sprintf(path, "%swow-update-base-%s.MPQ",gamePath.c_str(),updates[i]);
		archives.push_back(new MPQArchive(path));
	}
	for (size_t i = 0; i < 8; i++) {
		sprintf(path, "%s%s/wow-update-enUS-%s.MPQ", gamePath.c_str(),locales[langID],updates[i] );
		archives.push_back(new MPQArchive(path));
	}
	const char *updates2[] = { "13164", "13205", "13287", "13329", "13596", "13623" };
	for (size_t i = 0; i < 6; i++) {
		sprintf(path,"%swow-update-%s.MPQ",gamePath.c_str(),updates2[i]);
		archives.push_back(new MPQArchive(path));
	}

	OpenDBs();

	video.init(xres,yres,fullscreen!=0);
	SDL_WM_SetCaption(APP_TITLE,NULL);

	if (!(supportVBO && supportMultiTex)) {
		video.close();
		const char *msg = "Error: Cannot initialize OpenGL extensions.";
		gLog("%s\n",msg);
		gLog("Missing required extensions:\n");
		if (!supportVBO) gLog("GL_ARB_vertex_buffer_object\n");
		if (!supportMultiTex) gLog("GL_ARB_multitexture\n");
#ifdef _WIN32
		MessageBox(0, msg, 0, MB_OK|MB_ICONEXCLAMATION);
		exit(1);
#endif
	}

	initFonts();


	float ftime;
	Uint32 t, last_t, frames = 0, time = 0, fcount = 0, ft = 0;
	AppState *as;
	gFPS = 0;

	Menu *m = new Menu();
	as = m;

	gStates.push_back(as);

	bool done = false;
	t = SDL_GetTicks();

	int fps_delay = 0;

	while(gStates.size()>0 && !done) {
		last_t = t;
		t = SDL_GetTicks();
		Uint32 dt = t - last_t;
		time += dt;
		ftime = time / 1000.0f;

		as = gStates[gStates.size()-1];

		SDL_Event event;
		while ( SDL_PollEvent(&event) ) {
			if ( event.type == SDL_QUIT ) {
				done = true;
			}
			else if ( event.type == SDL_MOUSEMOTION) {
				as->mousemove(&event.motion);
			}
			else if ( event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) {
				as->mouseclick(&event.button);
			}
			else if ( event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
				as->keypressed(&event.key);
			}
		}

		as->tick(ftime, dt/1000.0f);

		as->display(ftime, dt/1000.0f);

		if (gPop) {
			gPop = false;
			gStates.pop_back();
			delete as;
		}

		frames++;
		fcount++;
		ft += dt;
		if (ft >= 1000) {
            float fps = (float)fcount / (float)ft * 1000.0f;
			gFPS = fps;
			char buf[32];
			sprintf(buf, APP_TITLE " - %.2f fps",fps);
			SDL_WM_SetCaption(buf,NULL);
            ft = 0;
			fcount = 0;

			if (fps > 40) fps_delay += 1000;
			if (fps < 30) fps_delay -= 2000;
			if (fps_delay < 0) fps_delay = 0;
		}

		video.flip();
		if (fps_delay) usleep(fps_delay);
	}


	deleteFonts();
	
	video.close();

	for (std::vector<MPQArchive*>::iterator it = archives.begin(); it != archives.end(); ++it) {
        (*it)->close();
	}
	archives.clear();

	gLog("\nExiting.\n");

	return 0;
}

float frand()
{
    return rand()/(float)RAND_MAX;
}

float randfloat(float lower, float upper)
{
	return lower + (upper-lower)*(rand()/(float)RAND_MAX);
}

int randint(int lower, int upper)
{
    return lower + (int)((upper+1-lower)*frand());
}

void fixnamen(char *name, size_t len)
{
	for (size_t i=0; i<len; i++) {
		if (i>0 && name[i]>='A' && name[i]<='Z' && isalpha(name[i-1])) {
			name[i] |= 0x20;
		} else if ((i==0 || !isalpha(name[i-1])) && name[i]>='a' && name[i]<='z') {
			name[i] &= ~0x20;
		}
	}
}

void fixname(std::string &name)
{
	for (size_t i=0; i<name.length(); i++) {
		if (i>0 && name[i]>='A' && name[i]<='Z' && isalpha(name[i-1])) {
			name[i] |= 0x20;
		} else if ((i==0 || !isalpha(name[i-1])) && name[i]>='a' && name[i]<='z') {
			name[i] &= ~0x20;
		}
	}
}
