#ifndef MODELHEADERS_H
#define MODELHEADERS_H

typedef unsigned char uint8;
typedef char int8;
typedef unsigned short uint16;
typedef short int16;
typedef unsigned int uint32;
typedef int int32;

#pragma pack(push,1)

//float floats[14];
struct PhysicsSettings {
	Vec3D VertexBox[2];
	float VertexRadius;
	Vec3D BoundingBox[2];
	float BoundingRadius;
};

struct ModelHeader {
	char id[4];
	uint8 version[4];
	uint32 nameLength;
	uint32 nameOfs;
	uint32 GlobalModelFlags; // 1: tilt x, 2: tilt y, 4:, 8: add another field in header, 16: ; (no other flags as of 3.1.1);

	uint32 nGlobalSequences; // AnimationRelated
	uint32 ofsGlobalSequences; // A list of timestamps.
	uint32 nAnimations; // AnimationRelated
	uint32 ofsAnimations; // Information about the animations in the model.
	uint32 nAnimationLookup; // AnimationRelated
	uint32 ofsAnimationLookup; // Mapping of global IDs to the entries in the Animation sequences block.
	//uint32 nD;
	//uint32 ofsD;
	uint32 nBones; // BonesAndLookups
	uint32 ofsBones; // Information about the bones in this model.
	uint32 nKeyBoneLookup; // BonesAndLookups
	uint32 ofsKeyBoneLookup; // Lookup table for key skeletal bones.

	uint32 nVertices; // GeometryAndRendering
	uint32 ofsVertices; // Vertices of the model.
	uint32 nViews; // GeometryAndRendering
	//uint32 ofsViews; // Views (LOD) are now in .skins.

	uint32 nColors; // ColorsAndTransparency
	uint32 ofsColors; // Color definitions.

	uint32 nTextures; // TextureAndTheifAnimation
	uint32 ofsTextures; // Textures of this model.

	uint32 nTransparency; // H,  ColorsAndTransparency
	uint32 ofsTransparency; // Transparency of textures.
	//uint32 nI;   // always unused ?
	//uint32 ofsI;
	uint32 nTexAnims;	// J, TextureAndTheifAnimation
	uint32 ofsTexAnims;
	uint32 nTexReplace; // TextureAndTheifAnimation
	uint32 ofsTexReplace; // Replaceable Textures.

	uint32 nTexFlags; // Render Flags
	uint32 ofsTexFlags; // Blending modes / render flags.
	uint32 nBoneLookup; // BonesAndLookups
	uint32 ofsBoneLookup; // A bone lookup table.

	uint32 nTexLookup; // TextureAndTheifAnimation
	uint32 ofsTexLookup; // The same for textures.

	uint32 nTexUnitLookup;		// L, TextureAndTheifAnimation
	uint32 ofsTexUnitLookup; // And texture units. Somewhere they have to be too.
	uint32 nTransparencyLookup; // M, ColorsAndTransparency
	uint32 ofsTransparencyLookup; // Everything needs its lookup. Here are the transparencies.
	uint32 nTexAnimLookup; // TextureAndTheifAnimation
	uint32 ofsTexAnimLookup; // Wait. Do we have animated Textures? Wasn't ofsTexAnims deleted? oO

	struct PhysicsSettings ps;

	uint32 nBoundingTriangles; // Miscellaneous
	uint32 ofsBoundingTriangles;
	uint32 nBoundingVertices; // Miscellaneous
	uint32 ofsBoundingVertices;
	uint32 nBoundingNormals; // Miscellaneous
	uint32 ofsBoundingNormals;

	uint32 nAttachments; // O, Miscellaneous
	uint32 ofsAttachments; // Attachments are for weapons etc.
	uint32 nAttachLookup; // P, Miscellaneous
	uint32 ofsAttachLookup; // Of course with a lookup.
	uint32 nEvents; // 
	uint32 ofsEvents; // Used for playing sounds when dying and a lot else.
	uint32 nLights; // R
	uint32 ofsLights; // Lights are mainly used in loginscreens but in wands and some doodads too.
	uint32 nCameras; // S, Miscellaneous
	uint32 ofsCameras; // The cameras are present in most models for having a model in the Character-Tab.
	uint32 nCameraLookup; // Miscellaneous
	uint32 ofsCameraLookup; // And lookup-time again.
	uint32 nRibbonEmitters; // U, Effects
	uint32 ofsRibbonEmitters; // Things swirling around. See the CoT-entrance for light-trails.
	uint32 nParticleEmitters; // V, Effects
	uint32 ofsParticleEmitters; // Spells and weapons, doodads and loginscreens use them. Blood dripping of a blade? Particles.

};

// block B - animations
struct ModelAnimation {
	uint16 animID;
	uint16 subAnimID;
	uint32 length;

	float moveSpeed;

	uint32 loopType;
	uint32 flags;
	uint32 d1;
	uint32 d2;
	uint32 playSpeed;  // note: this can't be play speed because it's 0 for some models

	Vec3D boxA, boxB;
	float rad;

	int16 NextAnimation;
	int16 Index;
};

struct AnimationBlockHeader
{
	uint32 nEntrys;
	uint32 ofsEntrys;
};

// sub-block in block E - animation data
struct AnimationBlock {
	int16 type;		// interpolation type (0=none, 1=linear, 2=hermite)
	int16 seq;		// global sequence id or -1
	uint32 nTimes;
	uint32 ofsTimes;
	uint32 nKeys;
	uint32 ofsKeys;
};

#define	MODELBONE_BILLBOARD	8
#define	MODELBONE_TRANSFORM	512
// block E - bones
struct ModelBoneDef {
	int32 keyBoneId;
	int32 flags;
	int16 parent; // parent bone index
	int16 geoid;
	int32 unknown; // new int added to the bone definitions.  Added in WoW 2.0
	AnimationBlock translation;
	AnimationBlock rotation;
	AnimationBlock scaling;
	Vec3D pivot;
};

struct ModelTexAnimDef {
	AnimationBlock trans, rot, scale;
};

struct ModelVertex {
	Vec3D pos;
	uint8 weights[4];
	uint8 bones[4];
	Vec3D normal;
	Vec2D texcoords;
	int unk1, unk2; // always 0,0 so this is probably unused
};

struct ModelView {
	char id[4];				 // Signature
	uint32 nIndex, ofsIndex; // Vertices in this model (index into vertices[])
	uint32 nTris, ofsTris;	 // indices
	uint32 nProps, ofsProps; // additional vtx properties
	uint32 nSub, ofsSub;	 // materials/renderops/submeshes
	uint32 nTex, ofsTex;	 // material properties/textures
	int32 lod;				 // LOD bias?
};


/// One material + render operation
struct ModelGeoset {
	uint32 id;		// mesh part id?
	uint16 vstart;	// first vertex
	uint16 vcount;	// num vertices
	uint16 istart;	// first index
	uint16 icount;	// num indices
	uint16 nBones;		// number of bone indices, Number of elements in the bone lookup table.
	uint16 StartBones;		// ? always 1 to 4, Starting index in the bone lookup table.
	uint16 d5;		// ?
	uint16 rootBone;		// root bone?
	Vec3D BoundingBox[2];
	float radius;
};

#define	TEXTUREUNIT_STATIC	16
/// A texture unit (sub of material)
struct ModelTexUnit{
	// probably the texture units
	// size always >=number of materials it seems
	uint16 flags;		// Flags
	uint16 shading;		// If set to 0x8000: shaders. Used in skyboxes to ditch the need for depth buffering. See below.
	uint16 op;			// Material this texture is part of (index into mat)
	uint16 op2;			// Always same as above?
	int16 colorIndex;	// color or -1
	uint16 flagsIndex;	// more flags...
	uint16 texunit;		// Texture unit (0 or 1)
	uint16 mode;			// ? (seems to be always 1)
	uint16 textureid;	// Texture id (index into global texture list)
	uint16 texunit2;	// copy of texture unit value?
	uint16 transid;		// transparency id (index into transparency list)
	uint16 texanimid;	// texture animation id
};

enum TextureFlags {
	TEXTURE_WRAPX=1,
	TEXTURE_WRAPY
};
#define	RENDERFLAGS_UNLIT	1
#define	RENDERFLAGS_UNFOGGED	2
#define	RENDERFLAGS_TWOSIDED	4
#define	RENDERFLAGS_BILLBOARD	8
#define	RENDERFLAGS_ZBUFFERED	16
// block X - render flags
struct ModelRenderFlags {
	uint16 flags;
	uint16 blend;
};

// block G - color defs
struct ModelColorDef {
	AnimationBlock color;
	AnimationBlock opacity;
};

// block H - transp defs
struct ModelTransDef {
	AnimationBlock trans;
};

#define	TEXTURE_MAX	32
struct ModelTextureDef {
	uint32 type;
	uint32 flags;
	uint32 nameLen;
	uint32 nameOfs;
};

enum ModelLightTypes {
	MODELLIGHT_DIRECTIONAL=0,
	MODELLIGHT_POINT
};
struct ModelLightDef {
	int16 type;
	int16 bone;
	Vec3D pos;
	AnimationBlock ambColor;
	AnimationBlock ambIntensity;
	AnimationBlock color;
	AnimationBlock intensity;
	AnimationBlock attStart;
	AnimationBlock attEnd;
	AnimationBlock unk1;
};

struct ModelCameraDef {
	int32 id; // 0 is potrait camera, 1 characterinfo camera; -1 if none; referenced in CamLookup_Table
	float farclip; // Where it stops to be drawn.
	float nearclip; // Far and near. Both of them.
	AnimationBlock transPos; // (Vec3D) How the cameras position moves. Should be 3*3 floats. (? WoW parses 36 bytes = 3*3*sizeof(float))
	Vec3D pos; // float, Where the camera is located.
	AnimationBlock transTarget; // (Vec3D) How the target moves. Should be 3*3 floats. (?)
	Vec3D target; // float, Where the camera points to.
	AnimationBlock rot; // (Quat) The camera can have some roll-effect. Its 0 to 2*Pi. 3 Floats!
	AnimationBlock AnimBlock4; // (Float) One Float. cataclysm
};

struct FakeAnimationBlock {
	uint32 nTimes;
	uint32 ofsTimes;
	uint32 nKeys;
	uint32 ofsKeys;
};

struct ModelParticleParams {
	FakeAnimationBlock colors; 	// (short, vec3f)	This one points to 3 floats defining red, green and blue.
	FakeAnimationBlock opacity;      // (short, short)		Looks like opacity (short), Most likely they all have 3 timestamps for {start, middle, end}.
	FakeAnimationBlock sizes; 		// (short, vec2f)	It carries two floats per key. (x and y scale)
	int32 d[2];
	FakeAnimationBlock Intensity; 	// Some kind of intensity values seen: 0,16,17,32(if set to different it will have high intensity) (short, short)
	FakeAnimationBlock unk2; 		// (short, short)
	float unk[3];
	float scales[3];
	float slowdown;
	float unknown1[2];
	float rotation;				//Sprite Rotation
	float unknown2[2];
	float Rot1[3];					//Model Rotation 1
	float Rot2[3];					//Model Rotation 2
	float Trans[3];				//Model Translation
	float f2[4];
	int32 nUnknownReference;
	int32 ofsUnknownReferenc;
};

#define	MODELPARTICLE_DONOTTRAIL			0x10
#define	MODELPARTICLE_DONOTBILLBOARD	0x1000
struct ModelParticleEmitterDef {
    int32 id;
	int32 flags;
	Vec3D pos; // The position. Relative to the following bone.
	int16 bone; // The bone its attached to.
	int16 texture; // And the texture that is used.
	int32 nModelFileName;
	int32 ofsModelFileName;
	int32 nParticleFileName;
	int32 ofsParticleFileName; // TODO
	int8 blend;
	int8 EmitterType; // EmitterType	 1 - Plane (rectangle), 2 - Sphere, 3 - Spline? (can't be bothered to find one)
	int16 ParticleColor; // This one is used so you can assign a color to specific particles. They loop over all 
						 // particles and compare +0x2A to 11, 12 and 13. If that matches, the colors from the dbc get applied.
	int8 ParticleType; // 0 "normal" particle, 
					   // 1 large quad from the particle's origin to its position (used in Moonwell water effects)
					   // 2 seems to be the same as 0 (found some in the Deeprun Tram blinky-lights-sign thing)
	int8 HeaderTail; // 0 - Head, 1 - Tail, 2 - Both
	int16 TextureTileRotation; // TODO, Rotation for the texture tile. (Values: -1,0,1)
	int16 cols; // How many different frames are on that texture? People should learn what rows and cols are.
	int16 rows; // (2, 2) means slice texture to 2*2 pieces
	AnimationBlock EmissionSpeed; // (Float) All of the following blocks should be floats.
	AnimationBlock SpeedVariation; // (Float) Variation in the flying-speed. (range: 0 to 1)
	AnimationBlock VerticalRange; // (Float) Drifting away vertically. (range: 0 to pi)
	AnimationBlock HorizontalRange; // (Float) They can do it horizontally too! (range: 0 to 2*pi)
	AnimationBlock Gravity; // (Float) Fall down, apple!
	AnimationBlock Lifespan; // (Float) Everyone has to die.
	int32 unknown;
	AnimationBlock EmissionRate; // (Float) Stread your particles, emitter.
	int32 unknown2;
	AnimationBlock EmissionAreaLength; // (Float) Well, you can do that in this area.
	AnimationBlock EmissionAreaWidth; // (Float) 
	AnimationBlock Gravity2; // (Float) A second gravity? Its strong.
	ModelParticleParams p;
	AnimationBlock en; // (UInt16), seems unused in cataclysm
	int32 unknown3; // 12319, cataclysm
	int32 unknown4; // 12319, cataclysm
	int32 unknown5; // 12319, cataclysm
	int32 unknown6; // 12319, cataclysm
};


struct ModelRibbonEmitterDef {
	int32 id;
	int32 bone;
	Vec3D pos;
	int32 nTextures;
	int32 ofsTextures;
	int32 nUnknown;
	int32 ofsUnknown;
	AnimationBlock color;
	AnimationBlock opacity;
	AnimationBlock above;
	AnimationBlock below;
	float res, length, Emissionangle;
	int16 s1, s2;
	AnimationBlock unk1;
	AnimationBlock unk2;
	int32 unknown;
};


#pragma pack(pop)


#endif
