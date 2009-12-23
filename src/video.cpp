#include "video.h"
#include "shaders.h"
#include "mpq.h"
#include "wowmapview.h"
using namespace std;

/////// EXTENSIONS

#if defined(_WIN32) || defined(DEFINE_ARB_MULTITEX)
// multitex
PFNGLMULTITEXCOORD2FARBPROC		glMultiTexCoord2fARB	= NULL;
PFNGLACTIVETEXTUREARBPROC		glActiveTextureARB		= NULL;
PFNGLCLIENTACTIVETEXTUREARBPROC	glClientActiveTextureARB= NULL;
#endif
// compression
PFNGLCOMPRESSEDTEXIMAGE2DARBPROC glCompressedTexImage2DARB	= NULL;
// vbo
PFNGLGENBUFFERSARBPROC glGenBuffersARB = NULL;
PFNGLBINDBUFFERARBPROC glBindBufferARB = NULL;
PFNGLBUFFERDATAARBPROC glBufferDataARB = NULL;
PFNGLDELETEBUFFERSARBPROC glDeleteBuffersARB = NULL;

PFNGLMAPBUFFERARBPROC glMapBufferARB = NULL;
PFNGLUNMAPBUFFERARBPROC glUnmapBufferARB = NULL;

PFNGLDRAWRANGEELEMENTSPROC glDrawRangeElements = NULL;

bool supportCompression = false;
bool supportMultiTex = false;
bool supportVBO = false;
bool supportDrawRangeElements = false;

////// VIDEO CLASS


Video video;

Video::Video()
{
}

Video::~Video()
{
}

void Video::init(int xres, int yres, bool fullscreen)
{
	SDL_Init(SDL_INIT_TIMER | SDL_INIT_VIDEO);
	int flags = SDL_OPENGL | SDL_HWSURFACE | SDL_ANYFORMAT | SDL_DOUBLEBUF;
	if (fullscreen) flags |= SDL_FULLSCREEN;
	// 32 bits ffs
	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
#ifdef _WIN32
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 32);
	primary = SDL_SetVideoMode(xres, yres, 32, flags);
#else
	//nvidia dont support 32bpp on my linux :(
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	primary = SDL_SetVideoMode(xres, yres, 24, flags);
#endif
	if (!primary) {
		printf("SDL Error: %s\n",SDL_GetError());
		exit(1);
	}

	initExtensions();
	initShaders();

	this->xres = xres;
	this->yres = yres;

	glViewport(0,0,xres,yres);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	//gluPerspective(45.0f, (GLfloat)xres/(GLfloat)yres, 0.01f, 1024.0f);
	gluPerspective(45.0f, (GLfloat)xres/(GLfloat)yres, 1.0f, 1024.0f);


	// hmmm...
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_NORMAL_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
}

void Video::close()
{
	SDL_Quit();
}

bool isExtensionSupported(const char *search)
{
	char *exts = (char*)glGetString(GL_EXTENSIONS);
	if (exts) {
		string str(exts);
		return (str.find(search) != string::npos);
	}
	return false;
}

/*
void Video::initExtensions()
{
#if defined(_WIN32) || defined(DEFINE_ARB_MULTITEX)
	glActiveTextureARB		= (PFNGLACTIVETEXTUREARBPROC)		SDL_GL_GetProcAddress("glActiveTextureARB");
	glClientActiveTextureARB= (PFNGLCLIENTACTIVETEXTUREARBPROC)		SDL_GL_GetProcAddress("glClientActiveTextureARB");
	glMultiTexCoord2fARB	= (PFNGLMULTITEXCOORD2FARBPROC)		SDL_GL_GetProcAddress("glMultiTexCoord2fARB");
#endif
	glCompressedTexImage2DARB	= (PFNGLCOMPRESSEDTEXIMAGE2DARBPROC)		SDL_GL_GetProcAddress("glCompressedTexImage2DARB");
	glGenBuffersARB = (PFNGLGENBUFFERSARBPROC) SDL_GL_GetProcAddress("glGenBuffersARB");
	glBindBufferARB = (PFNGLBINDBUFFERARBPROC) SDL_GL_GetProcAddress("glBindBufferARB");
	glBufferDataARB = (PFNGLBUFFERDATAARBPROC) SDL_GL_GetProcAddress("glBufferDataARB");
	glDeleteBuffersARB = (PFNGLDELETEBUFFERSARBPROC) SDL_GL_GetProcAddress("glDeleteBuffersARB");

	glMapBufferARB = (PFNGLMAPBUFFERARBPROC) SDL_GL_GetProcAddress("glMapBufferARB");
	glUnmapBufferARB = (PFNGLUNMAPBUFFERARBPROC) SDL_GL_GetProcAddress("glUnmapBufferARB");

	glDrawRangeElements = (PFNGLDRAWRANGEELEMENTSPROC) SDL_GL_GetProcAddress("glDrawRangeElements");
}
*/

void Video::initExtensions()
{
#ifdef _WIN32
	if (isExtensionSupported("GL_ARB_multitexture"))
	{
		supportMultiTex = true;
		glActiveTextureARB		= (PFNGLACTIVETEXTUREARBPROC)		SDL_GL_GetProcAddress("glActiveTextureARB");
		glClientActiveTextureARB= (PFNGLCLIENTACTIVETEXTUREARBPROC) SDL_GL_GetProcAddress("glClientActiveTextureARB");
		glMultiTexCoord2fARB	= (PFNGLMULTITEXCOORD2FARBPROC)		SDL_GL_GetProcAddress("glMultiTexCoord2fARB");
	} else supportMultiTex = false;

	glDrawRangeElements = (PFNGLDRAWRANGEELEMENTSPROC) SDL_GL_GetProcAddress("glDrawRangeElements");
	supportDrawRangeElements = (glDrawRangeElements != 0);
#else
	supportMultiTex = true;
	supportDrawRangeElements = true;
#endif
	if (isExtensionSupported("GL_ARB_texture_compression")) {
		glCompressedTexImage2DARB = (PFNGLCOMPRESSEDTEXIMAGE2DARBPROC) SDL_GL_GetProcAddress("glCompressedTexImage2DARB");
		supportCompression = isExtensionSupported("GL_EXT_texture_compression_s3tc");
	} else supportCompression = false;

	if (isExtensionSupported("GL_ARB_vertex_buffer_object"))
	{
		supportVBO = true;
		glGenBuffersARB = (PFNGLGENBUFFERSARBPROC) SDL_GL_GetProcAddress("glGenBuffersARB");
		glBindBufferARB = (PFNGLBINDBUFFERARBPROC) SDL_GL_GetProcAddress("glBindBufferARB");
		glBufferDataARB = (PFNGLBUFFERDATAARBPROC) SDL_GL_GetProcAddress("glBufferDataARB");
		glDeleteBuffersARB = (PFNGLDELETEBUFFERSARBPROC) SDL_GL_GetProcAddress("glDeleteBuffersARB");

		glMapBufferARB = (PFNGLMAPBUFFERARBPROC) SDL_GL_GetProcAddress("glMapBufferARB");
		glUnmapBufferARB = (PFNGLUNMAPBUFFERARBPROC) SDL_GL_GetProcAddress("glUnmapBufferARB");
	} else supportVBO = false;
}


void Video::flip()
{
	SDL_GL_SwapBuffers();
}

void Video::clearScreen()
{
	glClearColor(0.0f,0.0f,0.0f,0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}


void Video::set3D()
{
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(45.0f, (GLfloat)xres/(GLfloat)yres, 1.0f, 1024.0f);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}


void Video::set2D()
{
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, xres, yres, 0, -1.0, 1.0);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}


//////// TEXTURE MANAGER


GLuint TextureManager::add(std::string name)
{
	GLuint id;
	if (names.find(name) != names.end()) {
		id = names[name];
		items[id]->addref();
		return id;
	}
	glGenTextures(1,&id);

	Texture *tex = new Texture(name);
	tex->id = id;
	LoadBLP(id, tex);

	do_add(name, id, tex);

	return id;
}

void TextureManager::LoadBLP(GLuint id, Texture *tex)
{
	// load BLP texture
	glBindTexture(GL_TEXTURE_2D, id);

	int offsets[16],sizes[16],w,h, type=0;
	GLint format;

	char attr[4];

	MPQFile f(tex->name.c_str());
	if (f.isEof()) {
		tex->id = 0;
		gLog("Error: Could not load the texture '%s'\n", tex->name.c_str());
		return;
	}

	f.seek(4);
	f.read(&type,4);
	f.read(attr,4);
	f.read(&w,4);
	f.read(&h,4);
	f.read(offsets,4*16);
	f.read(sizes,4*16);

	tex->w = w;
	tex->h = h;

	bool hasmipmaps = (attr[3]>0);
	int mipmax = hasmipmaps ? 16 : 1;

	if (type != 1) {
		gLog("Error: %s:%s#%d type=%d", tex->name.c_str());
		tex->id = 0;
		return;
	}

	if (attr[0] == 2) {
		// compressed

		unsigned char *ucbuf = NULL;
		if (!supportCompression) 
			ucbuf = new unsigned char[w*h*4];

		format = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
		int blocksize = 8;

		// guesswork here :(
		if (attr[1]==8 || attr[1]==4) {
			format = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
			blocksize = 16;
		}

		if (attr[1]==8 && attr[2]==7) {
			format = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
			blocksize = 16;
		}

		unsigned char *buf = new unsigned char[sizes[0]];

		// do every mipmap level
		for (int i=0; i<mipmax; i++) {
			if (w==0) w = 1;
			if (h==0) h = 1;
			if (offsets[i] && sizes[i]) {
				f.seek(offsets[i]);
				f.read(buf,sizes[i]);

				int size = ((w+3)/4) * ((h+3)/4) * blocksize;

				if (supportCompression) {
					glCompressedTexImage2DARB(GL_TEXTURE_2D, i, format, w, h, 0, size, buf);
				} else {
					decompressDXTC(format, w, h, size, buf, ucbuf);
					glTexImage2D(GL_TEXTURE_2D, i, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, ucbuf);
				}

			} else break;
			w >>= 1;
			h >>= 1;
		}

		delete[] buf;
		if (!supportCompression) 
			delete[] ucbuf;
	}
	else if (attr[0]==1) {
		// uncompressed
		unsigned int pal[256];
		f.read(pal, 1024);

		unsigned char *buf = new unsigned char[sizes[0]];
		unsigned int *buf2 = new unsigned int[w*h];
		unsigned int *p = NULL;
		unsigned char *c = NULL, *a = NULL;

		int alphabits = attr[1];
		bool hasalpha = (alphabits!=0);

		for (int i=0; i<mipmax; i++) {
			if (w==0) w = 1;
			if (h==0) h = 1;
			if (offsets[i] && sizes[i]) {
				f.seek(offsets[i]);
				f.read(buf,sizes[i]);

				int cnt = 0;
				int alpha = 0;

				p = buf2;
				c = buf;
				a = buf + w*h;
				for (int y=0; y<h; y++) {
					for (int x=0; x<w; x++) {
						unsigned int k = pal[*c++];
						k = ((k&0x00FF0000)>>16) | ((k&0x0000FF00)) | ((k& 0x000000FF)<<16);

						if (hasalpha) {
							if (alphabits == 8) {
								alpha = (*a++);
							} else if (alphabits == 4) {
								alpha = (*a & (0xf << cnt++)) * 0x11;
								if (cnt == 2) {
									cnt = 0;
									a++;
								}
							} else if (alphabits == 1) {
								alpha = (*a & (1 << cnt++)) ? 0xff : 0;
								if (cnt == 8) {
									cnt = 0;
									a++;
								}
							}
						} else alpha = 0xff;

						k |= alpha << 24;
						*p++ = k;
					}
				}

				glTexImage2D(GL_TEXTURE_2D, i, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf2);

			} else break;
			w >>= 1;
			h >>= 1;
		}

		delete[] buf2;
		delete[] buf;
	}

	f.close();

	/*
	// TODO: Add proper support for mipmaps
	if (hasmipmaps) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	} else {
	*/
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
	//}
}


void TextureManager::doDelete(GLuint id)
{
	glDeleteTextures(1, &id);
}



#pragma pack(push,1)
struct TGAHeader {
   char  idlength;
   char  colourmaptype;
   char  datatypecode;
   short int colourmaporigin;
   short int colourmaplength;
   char  colourmapdepth;
   short int x_origin;
   short int y_origin;
   short width;
   short height;
   char  bitsperpixel;
   char  imagedescriptor;
};
#pragma pack(pop)


GLuint loadTGA(const char *filename, bool mipmaps)
{
	FILE *f = fopen(filename,"rb");
	TGAHeader h;
	fread(&h,18,1,f);
	if (h.datatypecode != 2) return 0;
	size_t s = h.width * h.height;
	GLint bppformat;
	GLint format;
	int bypp = h.bitsperpixel / 8;
	if (h.bitsperpixel == 24) {
		s *= 3;
		format = GL_RGB;
		bppformat = GL_RGB8;
	} else if (h.bitsperpixel == 32) {
		s *= 4;
		format = GL_RGBA;
		bppformat = GL_RGBA8;
	} else return 0;

	unsigned char *buf = new unsigned char[s], *buf2;
	//unsigned char *buf2 = new unsigned char[s];
	fread(buf,s,1,f);
	fclose(f);

	buf2 = buf;

	GLuint t;
	glGenTextures(1,&t);
	glBindTexture(GL_TEXTURE_2D, t);

	if (mipmaps) {
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
		gluBuild2DMipmaps (GL_TEXTURE_2D, bppformat, h.width, h.height, format, GL_UNSIGNED_BYTE, buf2);
	} else {
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
		glTexImage2D(GL_TEXTURE_2D, 0, bppformat, h.width, h.height, 0, format, GL_UNSIGNED_BYTE, buf2);
	}
    delete[] buf;
	//delete[] buf2;
	return t;
}


struct Color {
	unsigned char r, g, b;
};

void decompressDXTC(GLint format, int w, int h, size_t size, unsigned char *src, unsigned char *dest)
{
	// sort of copied from linghuye
	int bsx = (w<4) ? w : 4;
	int bsy = (h<4) ? h : 4;

	for(int y=0; y<h; y += bsy) {
		for(int x=0; x<w; x += bsx) {
			unsigned long long alpha = 0;
			unsigned int a0 = 0, a1 = 0;

			if (format == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT)	{
				alpha = *(unsigned long long*)src;
				src += 8;
			}

			unsigned int c0 = *(unsigned short*)(src + 0);
			unsigned int c1 = *(unsigned short*)(src + 2);
			src += 4;

			Color color[4];
			color[0].b = (unsigned char) ((c0 >> 11) & 0x1f) << 3;
			color[0].g = (unsigned char) ((c0 >>  5) & 0x3f) << 2;
			color[0].r = (unsigned char) ((c0      ) & 0x1f) << 3;
			color[1].b = (unsigned char) ((c1 >> 11) & 0x1f) << 3;
			color[1].g = (unsigned char) ((c1 >>  5) & 0x3f) << 2;
			color[1].r = (unsigned char) ((c1      ) & 0x1f) << 3;
			if(c0 > c1) {
				color[2].r = (color[0].r * 2 + color[1].r) / 3;
				color[2].g = (color[0].g * 2 + color[1].g) / 3;
				color[2].b = (color[0].b * 2 + color[1].b) / 3;
				color[3].r = (color[0].r + color[1].r * 2) / 3;
				color[3].g = (color[0].g + color[1].g * 2) / 3;
				color[3].b = (color[0].b + color[1].b * 2) / 3;
			} else {
				color[2].r = (color[0].r + color[1].r) / 2;
				color[2].g = (color[0].g + color[1].g) / 2;
				color[2].b = (color[0].b + color[1].b) / 2;
				color[3].r = 0;
				color[3].g = 0;
				color[3].b = 0;
			}

			for (int j=0; j<bsy; j++) {
				unsigned int index = *src++;
				unsigned char* dd = dest + (w*(y+j)+x)*4;
				for (int i=0; i<bsx; i++) {
					*dd++ = color[index & 0x03].b;
					*dd++ = color[index & 0x03].g;
					*dd++ = color[index & 0x03].r;
					if (format == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT)	{
						*dd++ = ((index & 0x03) == 3 && c0 <= c1) ? 0 : 255;
					}
					else if (format == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT)	{
						*dd++ = (unsigned char)(alpha & 0x0f) << 4;
						alpha >>= 4;
					}
					index >>= 2;
				}
			}
		}
	}
}
