#ifndef VIDEO_H
#define VIDEO_H

#include <SDL.h>
#include <SDL_opengl.h>
#include <map>

#ifndef _WINDOWS
#include <stdarg.h>
#endif

#include "vec3d.h"

#include "manager.h"
#include "font.h"
#include "ddslib.h"

#define PI 3.14159265358f

typedef GLuint TextureID;

// some versions of gl.h on linux have problems - they might or might not have some of this stuff defined
// this would look better in an autoconf script but I am lazy and nobody will use this on linux anyway ;|
//#define DEFINE_ARB_MULTITEX

////////// OPENGL EXTENSIONS
#if defined(_WIN32) || defined(DEFINE_ARB_MULTITEX)
// multitexture
extern PFNGLMULTITEXCOORD2FARBPROC		glMultiTexCoord2fARB;
extern PFNGLACTIVETEXTUREARBPROC		glActiveTextureARB;
extern PFNGLCLIENTACTIVETEXTUREARBPROC		glClientActiveTextureARB;
#endif
// compressed textures
extern PFNGLCOMPRESSEDTEXIMAGE2DARBPROC glCompressedTexImage2DARB;
// VBO
extern PFNGLGENBUFFERSARBPROC glGenBuffersARB;
extern PFNGLBINDBUFFERARBPROC glBindBufferARB;
extern PFNGLBUFFERDATAARBPROC glBufferDataARB;
extern PFNGLDELETEBUFFERSARBPROC glDeleteBuffersARB;

extern PFNGLMAPBUFFERARBPROC glMapBufferARB;
extern PFNGLUNMAPBUFFERARBPROC glUnmapBufferARB;

extern PFNGLDRAWRANGEELEMENTSPROC glDrawRangeElements;


#define GL_BUFFER_OFFSET(i) ((char *)(0) + (i))

extern bool supportCompression;
extern bool supportMultiTex;
extern bool supportVBO;
extern bool supportDrawRangeElements;


////////// TEXTURE MANAGER

class Texture : public ManagedItem {
public:
	int w,h;
	GLuint id;

	Texture(std::string name):ManagedItem(name), w(0), h(0) {}

};

class TextureManager : public Manager<GLuint> {
	
	void LoadBLP(GLuint id, Texture *tex);

public:
	virtual GLuint add(std::string name);
	void doDelete(GLuint id);

};

////////// VIDEO CLASS

class Video
{
	int status;

	SDL_Surface *primary;

	void initExtensions();

public:
	Video();
	~Video();

	void init(int xres, int yres, bool fullscreen);

	void close();

	void flip();
	void clearScreen();
	void set3D();
	void set2D();

	TextureManager textures;
    
	int xres, yres;

};

extern Video video;

GLuint loadTGA(const char *filename, bool mipmaps);
void decompressDXTC(GLint format, int w, int h, size_t size, unsigned char *src, unsigned char *dest);
bool isExtensionSupported(const char *search);



#endif
