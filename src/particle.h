#ifndef PARTICLE_H
#define PARTICLE_H

class ParticleSystem;
class RibbonEmitter;

#include "model.h"
#include "animated.h"

#include <list>

Vec4D fromARGB(uint32 color);
Vec4D fromBGRA(uint32 color);

struct Particle {
	Vec3D pos, speed, down, origin, dir;
	Vec3D	corners[4];
	//Vec3D tpos;
	float size, life, maxlife;
	unsigned int tile;
	Vec4D color;
};

typedef std::list<Particle> ParticleList;

class ParticleEmitter {
protected:
	ParticleSystem *sys;
public:
	ParticleEmitter(ParticleSystem *sys): sys(sys) {}
	virtual Particle newParticle(int anim, int time, float w, float l, float spd, float var, float spr, float spr2) = 0;
};

class PlaneParticleEmitter: public ParticleEmitter {
public:
	PlaneParticleEmitter(ParticleSystem *sys): ParticleEmitter(sys) {}
	Particle newParticle(int anim, int time, float w, float l, float spd, float var, float spr, float spr2);
};

class SphereParticleEmitter: public ParticleEmitter {
public:
	SphereParticleEmitter(ParticleSystem *sys): ParticleEmitter(sys) {}
	Particle newParticle(int anim, int time, float w, float l, float spd, float var, float spr, float spr2);
};

struct TexCoordSet {
    Vec2D tc[4];
};

class ParticleSystem {
	Animated<float> speed, variation, spread, lat, gravity, lifespan, rate, areal, areaw, deacceleration;
	Animated<uint8> enabled;
	Vec4D colors[3];
	float sizes[3];
	float mid, slowdown, rotation;
	Vec3D pos;
	GLuint texture;
	ParticleEmitter *emitter;
	ParticleList particles;
	int blend, order, type;
	int manim, mtime;
	int rows, cols;
	std::vector<TexCoordSet> tiles;
	void initTile(Vec2D *tc, int num);
	bool billboard;

	float rem;
	//bool transform;

	// unknown parameters omitted for now ...
	int32 flags;
	int16 pType;

	Bone *parent;

public:
	Model *model;
	float tofs;

	ParticleSystem(): emitter(0), mid(0), rem(0)
	{
		blend = 0;
		order = 0;
		type = 0;
		manim = 0;
		mtime = 0;
		rows = 0;
		cols = 0;

		model = 0;
		parent = 0;
		texture = 0;

		slowdown = 0;
		rotation = 0;
		tofs = 0;
	}
	~ParticleSystem() { delete emitter; }

	void init(MPQFile &f, ModelParticleEmitterDef &mta, uint32 *globals);
	void update(float dt);

	void setup(int anim, int time);
	void draw();

	friend class PlaneParticleEmitter;
	friend class SphereParticleEmitter;
};


struct RibbonSegment {
	Vec3D pos, up, back;
	float len,len0;
};

class RibbonEmitter {
	Animated<Vec3D> color;
	AnimatedShort opacity;
	Animated<float> above, below;

	Bone *parent;
	float f1, f2;

	Vec3D pos;

	int manim, mtime;
	float length, seglen;
	int numsegs;
	
	Vec3D tpos;
	Vec4D tcolor;
	float tabove, tbelow;

	GLuint texture;

	std::list<RibbonSegment> segs;

public:
	Model *model;

	void init(MPQFile &f, ModelRibbonEmitterDef &mta, uint32 *globals);
	void setup(int anim, int time);
	void draw();
};



#endif
