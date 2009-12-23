#ifndef SHADERS_H
#define SHADERS_H

#include "video.h"

extern bool supportShaders;

extern PFNGLPROGRAMSTRINGARBPROC glProgramStringARB;
extern PFNGLBINDPROGRAMARBPROC glBindProgramARB;
extern PFNGLDELETEPROGRAMSARBPROC glDeleteProgramsARB;
extern PFNGLGENPROGRAMSARBPROC glGenProgramsARB;
extern PFNGLPROGRAMLOCALPARAMETER4FARBPROC glProgramLocalParameter4fARB;


void initShaders();
void reloadShaders();

class Shader {
	GLenum target;
	GLuint id;
public:
	bool ok;

	Shader(GLenum target, const char *program, bool fromFile = false);
	~Shader();

	virtual void bind();
	virtual void unbind();
};

class ShaderPair {
	Shader *vertex;
	Shader *fragment;
public:

	ShaderPair():vertex(0),fragment(0) {}
	ShaderPair(Shader *vs, Shader *ps):vertex(vs), fragment(ps) {}
	ShaderPair(const char *vprog, const char *fprog, bool fromFile = false);

	void bind();
	void unbind();
};

extern ShaderPair *terrainShaders[4], *wmoShader, *waterShaders[1];


#endif
