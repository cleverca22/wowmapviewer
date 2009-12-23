#include "wmo.h"
#include "world.h"
#include "liquid.h"
#include "shaders.h"

using namespace std;

/*
http://www.madx.dk/wowdev/wiki/index.php?title=WMO

WMO files contain world map objects. They, too, have a chunked structure just like the WDT files.
There are two types of WMO files, actually:
WMO root file - lists textures (BLP Files), doodads (M2 or MDX Files), etc., and orientation for the WMO groups
WMO group file - 3d model data for one unit in the world map object
The root file and the groups are stored with the following filenames:
World\wmo\path\WMOName.wmo
World\wmo\path\WMOName_NNN.wmo


WMO root file

The root file lists the following:
textures (BLP File references)
materials
models (MDX / M2 File references)
groups
visibility information
more data
*/
WMO::WMO(std::string name): ManagedItem(name), groups(0), nTextures(0), nGroups(0),
	nP(0), nLights(0), nModels(0), nDoodads(0), nDoodadSets(0), nX(0), mat(0), LiquidType(0),
	skybox(0)
{
	MPQFile f(name.c_str());
	ok = !f.isEof();
	if (!ok) {
		gLog("Error: loading WMO %s\n", name.c_str());
		return;
	}

	gLog("Loading WMO %s\n", name.c_str());

	char fourcc[5];
	size_t size;
	float ff[3];

	char *ddnames;
	char *groupnames;

	char *texbuf=0;

	while (!f.isEof()) {
		f.read(fourcc,4);
		f.read(&size, 4);
		flipcc(fourcc);
		fourcc[4] = 0;

		if (size == 0)
			continue;

		size_t nextpos = f.getPos() + size;

		if (strcmp(fourcc,"MVER")==0) {
		}
		else if (strcmp(fourcc,"MOHD")==0) {
			unsigned int col;
			// Header for the map object. 64 bytes.
			f.read(&nTextures, 4); // number of materials
			f.read(&nGroups, 4); // number of WMO groups
			f.read(&nP, 4); // number of portals
			f.read(&nLights, 4); // number of lights
			f.read(&nModels, 4); // number of M2 models imported
			f.read(&nDoodads, 4); // number of dedicated files (*see below this table!) 
			f.read(&nDoodadSets, 4); // number of doodad sets
			f.read(&col, 4); // ambient color?
			f.read(&nX, 4); // WMO ID (column 2 in WMOAreaTable.dbc)
			f.read(ff,12); // Bounding box corner 1
			v1 = Vec3D(ff[0],ff[1],ff[2]);
			f.read(ff,12); // Bounding box corner 2
			v2 = Vec3D(ff[0],ff[1],ff[2]);
			f.read(&LiquidType, 4); // LiquidType related, see below in the MLIQ chunk.

			groups = new WMOGroup[nGroups];
			mat = new WMOMaterial[nTextures];

		}
		else if (strcmp(fourcc,"MOTX")==0) {
/*
List of textures (BLP Files) used in this map object. 
A block that are complete filenames with paths. There will be further material information for each texture in the next chunk. The gaps between the filenames are padded with extra zeroes, but the material chunk does have some positional information for these strings.
The beginning of a string is always aligned to a 4Byte Adress. (0, 4, 8, C). The end of the string is Zero terminated and filled with zeros until the next aligment. Sometimes there also empty aligtments for no (it seems like no) real reason.
*/
			// textures
			texbuf = new char[size+1];
			f.read(texbuf, size);
			texbuf[size] = 0;
		}
		else if (strcmp(fourcc,"MOMT")==0) {
			// Materials used in this map object, 64 bytes per texture (BLP file), nMaterials entries.
			// WMOMaterialBlock bl;
/*
The flags might used to tweak alpha testing values, I'm not sure about it, but some grates and flags in IF seem to require an alpha testing threshold of 0, at other places this is greater than 0.
flag2 		Meaning
0x01 		?(I'm not sure atm I tend to use lightmap or something like this)
0x04 		Two-sided (disable backface culling)
0x10 		Bright at night (unshaded) (used on windows and lamps in Stormwind, for example) -ProFeT: i think that is Unshaded becase external face of windows are flagged like this.
0x20 		?
0x28 		Darkned ?, the intern face of windows are flagged 0x28
0x40 		looks like GL_CLAMP
0x80 		looks like GL_REPEAT
*/
			for (int i=0; i<nTextures; i++) {
				WMOMaterial *m = &mat[i];
				f.read(m, 0x40); // read 64 bytes, struct WMOMaterial is 68 bytes

				string texpath(texbuf+m->nameStart);
				fixname(texpath);

				m->tex = video.textures.add(texpath);
				textures.push_back(texpath);

				/*
				// material logging
				gLog("Material %d:\t%d\t%d\t%d\t%X\t%d\t%X\t%d\t%f\t%f",
					i, m->flags, m->d1, m->transparent, m->col1, m->d3, m->col2, m->d4, m->f1, m->f2);
				for (int j=0; j<2; j++) gLog("\t%d", m->dx[j]);
				gLog("\t - %s\n", texpath.c_str());
				*/
				
			}
		}
		else if (strcmp(fourcc,"MOGN")==0) {
/*
List of group names for the groups in this map object. There are nGroups entries in this chunk.
A contiguous block of zero-terminated strings. The names are purely informational, they aren't used elsewhere (to my knowledge)
i think think that his realy is zero terminated and just a name list .. but so far im not sure _what_ else it could be - tharo
*/
			groupnames = (char*)f.getPointer();
		}
		else if (strcmp(fourcc,"MOGI")==0) {
			// group info - important information! ^_^
/*
Group information for WMO groups, 32 bytes per group, nGroups entries.
Offset 	Type 		Description
0x00 	uint32 		Flags
0x04 	3 * float 	Bounding box corner 1
0x10 	3 * float 	Bounding box corner 2
0x1C 	int32 		name in MOGN chunk (or -1 for no name?)
struct WMOGroup // --Schlumpf 21:06, 30 July 2007 (CEST)
{
000h  UINT32 flags;  		
004h  float  bb1[3];  		
010h  float  bb2[3];		
01Ch  UINT32 nameoffset;
}
Groups don't have placement or orientation information, because the coordinates for the vertices in the additional .WMO files are already correctly transformed relative to (0,0,0) which is the entire WMO's base position in model space.
The name offsets seem to be incorrect (or something else entirely?). The correct name offsets are in the WMO group file headers. (along with more descriptive names for some groups)
It is just the index. You have to find the offsets by yourself. --Schlumpf 10:17, 31 July 2007 (CEST)
The flags for the groups seem to specify whether it is indoors/outdoors, probably to choose what kind of lighting to use. Not fully understood. "Indoors" and "Outdoors" are flags used to tell the client whether certain spells can be cast and abilities used. (Example: Entangling Roots cannot be used indoors).
Flag		Meaning
0x8 		Outdoor (use global lights?)
0x40		?
0x80 		?
0x2000		Indoor (use local lights?)
0x8000		Unknown, but frequently used
0x10000 	Used in Stormwind?
0x40000		Show skybox if the player is "inside" the group
*/
			for (int i=0; i<nGroups; i++) {
				groups[i].init(this, f, i, groupnames);

			}
		}
		else if (strcmp(fourcc,"MOSB")==0) {
/*
Skybox. Always 00 00 00 00. Skyboxes are now defined in DBCs (Light.dbc etc.). Contained a M2 filename that was used as skybox.
*/
			if (size>4) {
				string path = (char*)f.getPointer();
				fixname(path);
				if (path.length()) {
					gLog("SKYBOX:\n");

					sbid = gWorld->modelmanager.add(path);
					skybox = (Model*)gWorld->modelmanager.items[sbid];

					if (!skybox->ok) {
						gWorld->modelmanager.del(sbid);
						skybox = 0;
					}
				}
			}
		}
		else if (strcmp(fourcc,"MOPV")==0) {
/*
Portal vertices, 4 * 3 * float per portal, nPortals entries.
Portals are (always?) rectangles that specify where doors or entrances are in a WMO. They could be used for visibility, but I currently have no idea what relations they have to each other or how they work.
Since when "playing" WoW, you're confined to the ground, checking for passing through these portals would be enough to toggle visibility for indoors or outdoors areas, however, when randomly flying around, this is not necessarily the case.
So.... What happens when you're flying around on a gryphon, and you fly into that arch-shaped portal into Ironforge? How is that portal calculated? It's all cool as long as you're inside "legal" areas, I suppose.
It's fun, you can actually map out the topology of the WMO using this and the MOPR chunk. This could be used to speed up the rendering once/if I figure out how.
*/
			WMOPV p;
			for (int i=0; i<nP; i++) {
				f.read(ff,12);
				p.a = Vec3D(ff[0],ff[2],-ff[1]);
				f.read(ff,12);
				p.b = Vec3D(ff[0],ff[2],-ff[1]);
				f.read(ff,12);
				p.c = Vec3D(ff[0],ff[2],-ff[1]);
				f.read(ff,12);
				p.d = Vec3D(ff[0],ff[2],-ff[1]);
				pvs.push_back(p);
			}
		}
		else if (strcmp(fourcc,"MOPT")==0) {
/*
Portal information. 20 bytes per portal, nPortals entries.
Offset	Type 		Description
0x00 	uint16 		Base vertex index?
0x02 	uint16 		Number of vertices (?), always 4 (?)
0x04 	3*float 	a normal vector maybe? haven't checked.
0x10 	float 		unknown  - if this is NAN, the three floats will be (0,0,1)
*/
		}
		else if (strcmp(fourcc,"MOPR")==0) {
/*
Portal <> group relationship? 2*nPortals entries of 8 bytes.
I think this might specify the two WMO groups that a portal connects.
Offset 	Type 		Description
0x0 	uint16 		Portal index
0x2 	uint16 		WMO group index
0x4 	int16 		1 or -1
0x6 	uint16 		always 0
struct SMOPortalRef // 04-29-2005 By ObscuR
{
  000h  UINT16 portalIndex;
  000h  UINT16 groupIndex;
  004h  UINT16 side;
  006h  UINT16 filler;
  008h
};
*/
			int nn = (int)size / sizeof(WMOPR);
			WMOPR *pr = (WMOPR*)f.getPointer();
			for (int i=0; i<nn; i++) {
				prs.push_back(*pr++);
			}
		}
		else if (strcmp(fourcc,"MOVV")==0) {
/*
Visible block vertices
Just a list of vertices that corresponds to the visible block list.
*/
		}
		else if (strcmp(fourcc,"MOVB")==0) {
/*
Visible block list
unsigned short firstVertex;
unsigned short count;
*/
		}
		else if (strcmp(fourcc,"MOLT")==0) {
/*
Lighting information. 48 bytes per light, nLights entries
Offset 	Type 		Description
0x00 	4 * uint8 	Flags or something? Mostly (0,1,1,1)
0x04 	4 * uint8 	Color (B,G,R,A)
0x08 	3 * float 	Position (X,Z,-Y)
0x14 	7 * float 	Unknown (light properties?)
enum LightType 
{
	OMNI_LGT,
	SPOT_LGT,
	DIRECT_LGT,
	AMBIENT_LGT
};
struct SMOLight // 04-29-2005 By ObscuR
{
  000h  UINT8 LightType; 	
  001h  UINT8 type;
  002h  UINT8 useAtten;
  003h  UINT8 pad;
  004h  UINT8 color[4];  
  008h  float position[3];
  014h  float intensity;
  018h  float attenStart;
  01Ch  float attenEnd;
  020h  float unk1;
  024h  float unk2;
  028h  float unk3;
  02Ch  float unk4;
  030h  
};
I haven't quite figured out how WoW actually does lighting, as it seems much smoother than the regular vertex lighting in my screenshots. The light paramters might be range or attenuation information, or something else entirely. Some WMO groups reference a lot of lights at once.
The WoW client (at least on my system) uses only one light, which is always directional. Attenuation is always (0, 0.7, 0.03). So I suppose for models/doodads (both are M2 files anyway) it selects an appropriate light to turn on. Global light is handled similarly. Some WMO textures (BLP files) have specular maps in the alpha channel, the pixel shader renderpath uses these. Still don't know how to determine direction/color for either the outdoor light or WMO local lights... :)
*/
			// Lights?
			for (int i=0; i<nLights; i++) {
				WMOLight l;
				l.init(f);
				lights.push_back(l);
			}
		}
		else if (strcmp(fourcc,"MODS")==0) {
/*
This chunk defines doodad sets.
Doodads in WoW are M2 model files. There are 32 bytes per doodad set, and nSets entries. Doodad sets specify several versions of "interior decoration" for a WMO. Like, a small house might have tables and a bed laid out neatly in one set called "Set_$DefaultGlobal", and have a horrible mess of abandoned broken things in another set called "Set_Abandoned01". The names are only informative.
The doodad set number for every WMO instance is specified in the ADT files.
Offset 	Type 		Description
0x00 	20 * char 	Set name
0x14 	uint32 		index of first doodad instance in this set
0x18 	uint32 		number of doodad instances in this set
0x1C 	uint32 		unused? (always 0)
struct SMODoodadSet // --Schlumpf 17:03, 31 July 2007 (CEST)
{
000h  char   name[20];
014h  UINT32 firstinstanceindex;
018h  UINT32 numDoodads;
01Ch  UINT32 nulls;
}
*/
			for (int i=0; i<nDoodadSets; i++) {
				WMODoodadSet dds;
				f.read(&dds, sizeof(WMODoodadSet));
				doodadsets.push_back(dds);
			}
		}
		else if (strcmp(fourcc,"MODN")==0) {
			// models ...
			// MMID would be relative offsets for MMDX filenames
/*
List of filenames for M2 (mdx) models that appear in this WMO.
A block of zero-padded, zero-terminated strings. There are nModels file names in this list. They have to be .MDX!
*/
			if (size) {
				ddnames = (char*)f.getPointer();
				fixnamen(ddnames, size);

				char *p=ddnames,*end=p+size;
				int t=0;
				while (p<end) {
					string path(p);
					p+=strlen(p)+1;
					while ((p<end) && (*p==0)) p++;

					gWorld->modelmanager.add(path);
					models.push_back(path);
				}
				f.seekRelative((int)size);
			}
		}
		else if (strcmp(fourcc,"MODD")==0) {
/*
Information for doodad instances. 40 bytes per doodad instance, nDoodads entries.
While WMOs and models (M2s) in a map tile are rotated along the axes, doodads within a WMO are oriented using quaternions! Hooray for consistency!
I had to do some tinkering and mirroring to orient the doodads correctly using the quaternion, see model.cpp in the WoWmapview source code for the exact transform matrix. It's probably because I'm using another coordinate system, as a lot of other coordinates in WMOs and models also have to be read as (X,Z,-Y) to work in my system. But then again, the ADT files have the "correct" order of coordinates. Weird.
Offset 	Type 		Description
0x00 	uint32 		Offset to the start of the model's filename in the MODN chunk. 
0x04 	3 * float 	Position (X,Z,-Y)
0x10 	float 		W component of the orientation quaternion
0x14 	3 * float 	X, Y, Z components of the orientaton quaternion
0x20 	float 		Scale factor
0x24 	4 * uint8 	(B,G,R,A) Lightning-color. 
Are you sure the order of the quaternion components is W,X,Y,Z? It seems it is X,Y,Z,W -andhansen
struct SMODoodadDef // 03-29-2005 By ObscuR
{
  000h  UINT32 nameIndex
  004h  float pos[3];
  010h  float rot[4];
  020h  float scale;
  024h  UINT8 color[4];
  028h
};
*/
			nModels = (int)size / 0x28;
			for (int i=0; i<nModels; i++) {
				int ofs;
				f.read(&ofs,4);
				if (!ddnames) // Alfred, Error
					continue;
				Model *m = (Model*)gWorld->modelmanager.items[gWorld->modelmanager.get(ddnames + ofs)];
				ModelInstance mi;
				mi.init2(m,f);
				modelis.push_back(mi);
			}

		}
		else if (strcmp(fourcc,"MFOG")==0) {
/*
Fog information. Made up of blocks of 48 bytes.
Fog end: This is the distance at which all visibility ceases, and you see no objects or terrain except for the fog color.
Fog start: This is where the fog starts. Obtained by multiplying the fog end value by the fog start multiplier.
*/
			int nfogs = (int)size / 0x30;
			for (int i=0; i<nfogs; i++) {
				WMOFog fog;
				fog.init(f);
				fogs.push_back(fog);
			}
		}
		else if (strcmp(fourcc,"MCVP")==0) {
/*
MCVP chunk (optional)
Convex Volume Planes. Contains blocks of floating-point numbers.
*/
			gLog("No implement wmo chunk %s [%d].\n", fourcc, size);
		}
		else {
			gLog("No implement wmo chunk %s [%d].\n", fourcc, size);
		}

		f.seek((int)nextpos);
	}

	f.close();
	delete[] texbuf;

	for (int i=0; i<nGroups; i++) 
		groups[i].initDisplayList();

}

WMO::~WMO()
{
	if (!ok)
		return;

	gLog("Unloading WMO %s\n", name.c_str());
	if (groups)
		delete[] groups;

	for (vector<string>::iterator it = textures.begin(); it != textures.end(); ++it)
            video.textures.delbyname(*it);

	for (vector<string>::iterator it = models.begin(); it != models.end(); ++it)
		gWorld->modelmanager.delbyname(*it);

	if (mat)
		delete[] mat;

	if (skybox) {
		//delete skybox;
		gWorld->modelmanager.del(sbid);
	}
}

void WMO::draw(int doodadset, const Vec3D &ofs, const float rot)
{
	if (!ok) return;
	
	for (int i=0; i<nGroups; i++) {
		groups[i].draw(ofs, rot);
	}

	if (gWorld->drawdoodads) {
		for (int i=0; i<nGroups; i++) {
			groups[i].drawDoodads(doodadset, ofs, rot);
		}
	}

	for (int i=0; i<nGroups; i++) {
		groups[i].drawLiquid();
	}

	/*
	// draw light placeholders
	glDisable(GL_LIGHTING);
	glDisable(GL_CULL_FACE);
	glDisable(GL_TEXTURE_2D);
	glBegin(GL_TRIANGLES);
	for (int i=0; i<nLights; i++) {
		glColor4fv(lights[i].fcolor);
		glVertex3fv(lights[i].pos);
		glVertex3fv(lights[i].pos + Vec3D(-0.5f,1,0));
		glVertex3fv(lights[i].pos + Vec3D(0.5f,1,0));
	}
	glEnd();
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_CULL_FACE);
	glEnable(GL_LIGHTING);
	glColor4f(1,1,1,1);
	*/

	/*
	// draw fog positions..?
	glDisable(GL_LIGHTING);
	glDisable(GL_TEXTURE_2D);
	for (size_t i=0; i<fogs.size(); i++) {
		WMOFog &fog = fogs[i];
		glColor4f(1,1,1,1);
		glBegin(GL_LINE_LOOP);
		glVertex3fv(fog.pos);
		glVertex3fv(fog.pos + Vec3D(fog.rad1, 5, -fog.rad2));
		glVertex3fv(fog.pos + Vec3D(fog.rad1, 5, fog.rad2));
		glVertex3fv(fog.pos + Vec3D(-fog.rad1, 5, fog.rad2));
		glVertex3fv(fog.pos + Vec3D(-fog.rad1, 5, -fog.rad2));
		glEnd();
	}
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_LIGHTING);
	*/

	/*
	// draw group boundingboxes
	glDisable(GL_LIGHTING);
	glDisable(GL_TEXTURE_2D);
	for (int i=0; i<nGroups; i++) {
		WMOGroup &g = groups[i];
		float fc[2] = {1,0};
		glColor4f(fc[i%2],fc[(i/2)%2],fc[(i/3)%2],1);
		glBegin(GL_LINE_LOOP);

		glVertex3f(g.b1.x, g.b1.y, g.b1.z);
		glVertex3f(g.b1.x, g.b2.y, g.b1.z);
		glVertex3f(g.b2.x, g.b2.y, g.b1.z);
		glVertex3f(g.b2.x, g.b1.y, g.b1.z);

		glVertex3f(g.b2.x, g.b1.y, g.b2.z);
		glVertex3f(g.b2.x, g.b2.y, g.b2.z);
		glVertex3f(g.b1.x, g.b2.y, g.b2.z);
		glVertex3f(g.b1.x, g.b1.y, g.b2.z);

		glEnd();
	}
	// draw portal relations
	glBegin(GL_LINES);
	for (size_t i=0; i<prs.size(); i++) {
		WMOPR &pr = prs[i];
		WMOPV &pv = pvs[pr.portal];
		if (pr.dir>0) glColor4f(1,0,0,1);
		else glColor4f(0,0,1,1);
		Vec3D pc = (pv.a+pv.b+pv.c+pv.d)*0.25f;
		Vec3D gc = (groups[pr.group].b1 + groups[pr.group].b2)*0.5f;
		glVertex3fv(pc);
		glVertex3fv(gc);
	}
	glEnd();
	glColor4f(1,1,1,1);
	// draw portals
	for (int i=0; i<nP; i++) {
		glBegin(GL_LINE_STRIP);
		glVertex3fv(pvs[i].d);
		glVertex3fv(pvs[i].c);
		glVertex3fv(pvs[i].b);
		glVertex3fv(pvs[i].a);
		glEnd();
	}
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_LIGHTING);
	*/
}

void WMO::drawSkybox()
{
	if (skybox) {
		// TODO: only draw sky if we are "inside" the WMO... ?

		// We need to clear the depth buffer, because the skybox model can (will?)
		// require it *. This is inefficient - is there a better way to do this?
		// * planets in front of "space" in Caverns of Time
		//glClear(GL_DEPTH_BUFFER_BIT);

		// update: skybox models seem to have an explicit renderop ordering!
		// that saves us the depth buffer clear and the depth testing, too

		glDisable(GL_CULL_FACE);
		glDisable(GL_DEPTH_TEST);
		glPushMatrix();
		Vec3D o = gWorld->camera;
		glTranslatef(o.x, o.y, o.z);
		const float sc = 2.0f;
		glScalef(sc,sc,sc);
        skybox->draw();
		glPopMatrix();
		gWorld->hadSky = true;
		glEnable(GL_DEPTH_TEST);
	}
}

/*
void WMO::drawPortals()
{
	// not used ;)
	glBegin(GL_QUADS);
	for (int i=0; i<nP; i++) {
		glVertex3fv(pvs[i].d);
		glVertex3fv(pvs[i].c);
		glVertex3fv(pvs[i].b);
		glVertex3fv(pvs[i].a);
	}
	glEnd();
}
*/

void WMOLight::init(MPQFile &f)
{
	f.read(&LightType, 1);
	f.read(&type, 1);
	f.read(&useAtten, 1);
	f.read(&pad, 1);
	f.read(&color,4);
	f.read(pos, 12);
	f.read(&intensity, 4);
	f.read(&attenStart, 4);
	f.read(&attenEnd, 4);
	f.read(unk, 4*4);

	pos = Vec3D(pos.x, pos.z, -pos.y);

	fcolor = fromARGB(color);
	fcolor *= intensity;
	fcolor.w = 1.0f;

	/*
	// light logging
	gLog("Light %08x @ (%4.2f,%4.2f,%4.2f)\t %4.2f, %4.2f, %4.2f, %4.2f, %4.2f, %4.2f, %4.2f\t(%d,%d,%d,%d)\n",
		color, pos.x, pos.y, pos.z, intensity,
		unk[0], unk[1], unk[2], unk[3], unk[4], r,
		type[0], type[1], type[2], type[3]);
	*/
}

void WMOLight::setup(GLint light)
{
	// not used right now -_-

	GLfloat LightAmbient[] = {0, 0, 0, 1.0f};
	GLfloat LightPosition[] = {pos.x, pos.y, pos.z, 0.0f};

	glLightfv(light, GL_AMBIENT, LightAmbient);
	glLightfv(light, GL_DIFFUSE, fcolor);
	glLightfv(light, GL_POSITION,LightPosition);

	glEnable(light);
}

void WMOLight::setupOnce(GLint light, Vec3D dir, Vec4D lcol)
{
	Vec4D position(dir, 0);
	//Vec4D position(0,1,0,0);

	Vec4D ambient = lcol*0.3f; //Vec4D(lcol * 0.3f, 1);
	//Vec4D ambient = Vec4D(0.101961f, 0.062776f, 0, 1);
	Vec4D diffuse = lcol; //Vec4D(lcol, 1);
	//Vec4D diffuse = Vec4D(0.439216f, 0.266667f, 0, 1);

	glLightfv(light, GL_AMBIENT, ambient);
	glLightfv(light, GL_DIFFUSE, diffuse);
	glLightfv(light, GL_POSITION,position);
	
	glEnable(light);
}



void WMOGroup::init(WMO *wmo, MPQFile &f, int num, char *names)
{
	this->wmo = wmo;
	this->num = num;

	// extract group info from f
	f.read(&flags,4);
	float ff[3];
	f.read(ff,12); // Bounding box corner 1
	v1 = Vec3D(ff[0],ff[1],ff[2]);
	f.read(ff,12); // Bounding box corner 2
	v2 = Vec3D(ff[0],ff[1],ff[2]);
	int nameOfs;
	f.read(&nameOfs,4); // name in MOGN chunk (or -1 for no name?)

	// TODO: get proper name from group header and/or dbc?
	if (nameOfs > 0) {
       	name = string(names + nameOfs);
	} else 
		name = "(no name)";

	ddr = 0;
	nDoodads = 0;

	lq = 0;
}


struct WMOBatch {
	signed char bytes[12];
	unsigned int indexStart;
	unsigned short indexCount, vertexStart, vertexEnd;
	unsigned char flags, texture;
};

void setGLColor(unsigned int col)
{
	//glColor4ubv((GLubyte*)(&col));
	GLubyte r,g,b,a;
	a = (col & 0xFF000000) >> 24;
	r = (col & 0x00FF0000) >> 16;
	g = (col & 0x0000FF00) >> 8;
	b = (col & 0x000000FF);
    glColor4ub(r,g,b,1);
}

Vec4D colorFromInt(unsigned int col) {
	GLubyte r,g,b,a;
	a = (col & 0xFF000000) >> 24;
	r = (col & 0x00FF0000) >> 16;
	g = (col & 0x0000FF00) >> 8;
	b = (col & 0x000000FF);
	return Vec4D(r/255.0f, g/255.0f, b/255.0f, a/255.0f);
}

/*
The fields referenced from the MOPR chunk indicate portals leading out of the WMO group in question.
For the "Number of batches" fields, A + B + C == the total number of batches in the WMO group (in the MOBA chunk). This might be some kind of LOD thing, or just separating the batches into different types/groups...?
Flags: always contain more information than flags in MOGI. I suppose MOGI only deals with topology/culling, while flags here also include rendering info.
Flag		Meaning
0x1 		something with bounding
0x4 		Has vertex colors (MOCV chunk)
0x8 		Outdoor
0x40
0x200 		Has lights  (MOLR chunk)
0x400		
0x800 		Has doodads (MODR chunk)
0x1000 	        Has water   (MLIQ chunk)
0x2000		Indoor
0x8000
0x20000		Parses some additional chunk. No idea what it is. Oo
0x40000	        Show skybox
0x80000		isNotOcean, LiquidType related, see below in the MLIQ chunk.
*/
struct WMOGroupHeader {
    	int nameStart; // Group name (offset into MOGN chunk)
	int nameStart2; // Descriptive group name (offset into MOGN chunk)
	int flags;
	float box1[3]; // Bounding box corner 1 (same as in MOGI)
	float box2[3]; // Bounding box corner 2
	short portalStart; // Index into the MOPR chunk
	short portalCount; // Number of items used from the MOPR chunk
	short batches[4];
	uint8 fogs[4]; // Up to four indices into the WMO fog list
	int32 unk1; // LiquidType related, see below in the MLIQ chunk.
	int32 id; // WMO group ID (column 4 in WMOAreaTable.dbc)
	int32 unk2; // Always 0?
	int32 unk3; // Always 0?
};

struct SMOPoly {
	uint8 flags;
	uint8 mtlId;
};

void WMOGroup::initDisplayList()
{
	Vec3D *vertices, *normals;
	Vec2D *texcoords;
	unsigned short *indices;
	struct SMOPoly *materials;
	WMOBatch *batches;
	//int nBatches;

	WMOGroupHeader gh;

	short *useLights = 0;
	int nLR = 0;

	// open group file
	char temp[256];
	strcpy(temp, wmo->name.c_str());
	temp[wmo->name.length()-4] = 0;
	
	char fname[256];
	sprintf(fname,"%s_%03d.wmo",temp, num);

	MPQFile gf(fname);
	gf.seek(0x14); // a header at 0x14

	// read MOGP chunk header
	gf.read(&gh, sizeof(WMOGroupHeader));
	WMOFog &wf = wmo->fogs[gh.fogs[0]];
	if (wf.r2 <= 0) fog = -1; // default outdoor fog..?
	else fog = gh.fogs[0];

	b1 = Vec3D(gh.box1[0], gh.box1[2], -gh.box1[1]);
	b2 = Vec3D(gh.box2[0], gh.box2[2], -gh.box2[1]);

	gf.seek(0x58); // first chunk at 0x58

	char fourcc[5];
	size_t size;

	unsigned int *cv;
	hascv = false;

	while (!gf.isEof()) {
		gf.read(fourcc,4);
		gf.read(&size, 4);
		flipcc(fourcc);
		fourcc[4] = 0;

		if (size == 0)
			continue;

		size_t nextpos = gf.getPos() + size;

		// why copy stuff when I can just map it from memory ^_^
		
		if (strcmp(fourcc,"MOPY")==0) {
/*
Material info for triangles, two bytes per triangle. So size of this chunk in bytes is twice the number of triangles in the WMO group.
Offset	Type 	Description
0x00 	uint8 	Flags?
0x01 	uint8 	Material ID
struct SMOPoly // 03-29-2005 By ObscuR ( Maybe not accurate :p )
{
	enum  
	{
		F_NOCAMCOLLIDE,
		F_DETAIL,
 		F_COLLISION,
		F_HINT,
		F_RENDER,
		F_COLLIDE_HIT,
	};
000h  uint8 flags;
001h  uint8 mtlId;
002h  
};

Frequently used flags are 0x20 and 0x40, but I have no idea what they do.
Flag	Description
0x00 	?
0x01 	?
0x04 	no collision
0x08 	?
0x20 	?
0x40 	?
Material ID specifies an index into the material table in the root WMO file's MOMT chunk. Some of the triangles have 0xFF for the material ID, I skip these. (but there might very well be a use for them?)
The triangles with 0xFF Material ID seem to be a simplified mesh. Like for collision detection or something like that. At least stairs are flattened to ramps if you only display these polys. --shlainn 7 Jun 2009
0xFF representing -1 is used for collision-only triangles. They aren't rendered but have collision. Problem with it: WoW seems to cast and reflect light on them. Its a bug in the engine. --schlumpf_ 20:40, 7 June 2009 (CEST)
Triangles stored here are more-or-less pre-sorted by texture, so it's ok to draw them sequentially.
*/
			// materials per triangle
			nTriangles = (int)size / 2;
			materials = (struct SMOPoly*)gf.getPointer();
		}
		else if (strcmp(fourcc,"MOVI")==0) {
/*
Vertex indices for triangles. Three 16-bit integers per triangle, that are indices into the vertex list. The numbers specify the 3 vertices for each triangle, their order makes it possible to do backface culling.
*/
			// indices
			indices =  (unsigned short*)gf.getPointer();
		}
		else if (strcmp(fourcc,"MOVT")==0) {
/*
Vertices chunk. 3 floats per vertex, the coordinates are in (X,Z,-Y) order. It's likely that WMOs and models (M2s) were created in a coordinate system with the Z axis pointing up and the Y axis into the screen, whereas in OpenGL, the coordinate system used in WoWmapview the Z axis points toward the viewer and the Y axis points up. Hence the juggling around with coordinates.
*/
			nVertices = (int)size / 12;
			// let's hope it's padded to 12 bytes, not 16...
			vertices =  (Vec3D*)gf.getPointer();
			vmin = Vec3D( 9999999.0f, 9999999.0f, 9999999.0f);
			vmax = Vec3D(-9999999.0f,-9999999.0f,-9999999.0f);
			rad = 0;
			for (int i=0; i<nVertices; i++) {
				Vec3D v(vertices[i].x, vertices[i].z, -vertices[i].y);
				if (v.x < vmin.x) vmin.x = v.x;
				if (v.y < vmin.y) vmin.y = v.y;
				if (v.z < vmin.z) vmin.z = v.z;
				if (v.x > vmax.x) vmax.x = v.x;
				if (v.y > vmax.y) vmax.y = v.y;
				if (v.z > vmax.z) vmax.z = v.z;
			}
			center = (vmax + vmin) * 0.5f;
			rad = (vmax-center).length();
		}
		else if (strcmp(fourcc,"MONR")==0) {
			// Normals. 3 floats per vertex normal, in (X,Z,-Y) order.
			normals =  (Vec3D*)gf.getPointer();
		}
		else if (strcmp(fourcc,"MOTV")==0) {
			// Texture coordinates, 2 floats per vertex in (X,Y) order. The values range from 0.0 to 1.0. Vertices, normals and texture coordinates are in corresponding order, of course.
			texcoords =  (Vec2D*)gf.getPointer();
		}
		else if (strcmp(fourcc,"MOBA")==0) {
/*
Render batches. Records of 24 bytes.
struct SMOBatch // 03-29-2005 By ObscuR
{
	enum
	{
		F_RENDERED
	};
	?? lightMap;
	?? texture;
	?? bx;
	?? by;
	?? bz;
	?? tx;
	?? ty;
	?? tz;
	?? startIndex;
	?? count;
	?? minIndex;
	?? maxIndex;
	?? flags;
};
For the enUS, enGB versions, it seems to be different from the preceding struct:
Offset	Type 		Description
0x00 	uint32 		Some color?
0x04 	uint32 		Some color?
0x08 	uint32 		Some color?
0x0C 	uint32 		Start index
0x10 	uint16 		Number of indices
0x12 	uint16 		Start vertex
0x14 	uint16 		End vertex
0x16 	uint8 		0?
0x17 	uint8 		Texture
*/
			nBatches = (int)size / 24;
			batches = (WMOBatch*)gf.getPointer();
			
			/*
			// batch logging
			gLog("\nWMO group #%d - %s\nVertices: %d\nTriangles: %d\nIndices: %d\nBatches: %d\n",
				this->num, this->name.c_str(), nVertices, nTriangles, nTriangles*3, nBatches);
			WMOBatch *ba = batches;
			for (int i=0; i<nBatches; i++) {
				gLog("Batch %d:\t", i);
				
				for (int j=0; j<12; j++) {
					if ((j%4)==0 && j!=0) gLog("| ");
					gLog("%d\t", ba[i].bytes[j]);
				}
				
				gLog("| %d\t%d\t| %d\t%d\t", ba[i].indexStart, ba[i].indexCount, ba[i].vertexStart, ba[i].vertexEnd);
				gLog("%d\t%d\t%s\n", ba[i].flags, ba[i].texture, wmo->textures[ba[i].texture].c_str());

			}
			int l = nBatches-1;
			gLog("Max index: %d\n", ba[l].indexStart + ba[l].indexCount);
			*/
			
		}
		else if (strcmp(fourcc,"MOLR")==0) {
/*
Light references, one 16-bit integer per light reference.
This is basically a list of lights used in this WMO group, the numbers are indices into the WMO root file's MOLT table.
For some WMO groups there is a large number of lights specified here, more than what a typical video card will handle at once. I wonder how they do lighting properly. Currently, I just turn on the first GL_MAX_LIGHTS and hope for the best. :(
*/
			nLR = (int)size / 2;
			useLights =  (short*)gf.getPointer();
		}
		else if (strcmp(fourcc,"MODR")==0) {
/*
Doodad references, one 16-bit integer per doodad.
The numbers are indices into the doodad instance table (MODD chunk) of the WMO root file. These have to be filtered to the doodad set being used in any given WMO instance.
*/
			nDoodads = (int)size / 2;
			ddr = new short[nDoodads];
			gf.read(ddr,size);
		}
		else if (strcmp(fourcc,"MOBN")==0) {
/*
Array of t_BSP_NODE.
struct t_BSP_NODE
{	
	short planetype;          // unsure
	short children[2];        // index of bsp child node(right in this array)   
	unsigned short numfaces;  // num of triangle faces in  MOBR
	unsigned short firstface; // index of the first triangle index(in  MOBR)
	short nUnk;	          // 0
	float fDist;    
};
// The numfaces and firstface define a polygon plane.
                                        2005-4-4 by linghuye
This+BoundingBox(in wmo_root.MOGI) is used for Collision --Tigurius
*/
		}
		else if (strcmp(fourcc,"MOBR")==0) {
			// Triangle indices (in MOVI which define triangles) to describe polygon planes defined by MOBN BSP nodes.
		}
		else if (strcmp(fourcc,"MOCV")==0) {
/*
Vertex colors, 4 bytes per vertex (BGRA), for WMO groups using indoor lighting.
I don't know if this is supposed to work together with, or replace, the lights referenced in MOLR. But it sure is the only way for the ground around the goblin smelting pot to turn red in the Deadmines. (but some corridors are, in turn, too dark - how the hell does lighting work anyway, are there lightmaps hidden somewhere?)
- I'm pretty sure WoW does not use lightmaps in it's WMOs...
After further inspection, this is it, actual pre-lit vertex colors for WMOs - vertex lighting is turned off. This is used if flag 0x2000 in the MOGI chunk is on for this group. This pretty much fixes indoor lighting in Ironforge and Undercity. The "light" lights are used only for M2 models (doodads and characters). (The "too dark" corridors seemed like that because I was looking at it in a window - in full screen it looks pretty much the same as in the game) Now THAT's progress!!!
*/
			//gLog("CV: %d\n", size);
			hascv = true;
			cv = (unsigned int*)gf.getPointer();
		}
		else if (strcmp(fourcc,"MLIQ")==0) {
/*
Specifies liquids inside WMOs.
This is where the water from Stormwind and BFD etc. is hidden. (slime in Undercity, pool water in the Darnassus temple, some lava in IF)
Chunk header:
Offset	Type 		Description
0x00 	uint32 		number of X vertices (xverts)
0x04 	uint32 		number of Y vertices (yverts)
0x08 	uint32 		number of X tiles (xtiles = xverts-1)
0x0C 	uint32 		number of Y tiles (ytiles = yverts-1)
0x10 	float[3] 	base coordinates?
0x1C 	uint16 		material ID
The liquid data contains the vertex height map (xverts * yverts * 8 bytes) and the tile flags (xtiles * ytiles bytes) as descripbed in ADT files (MCLQ chunk). The length and width of a liquid tile is the same as on the map, that is, 1/8th of the length of a map chunk. (which is in turn 1/16th the length of a map tile).

the real deal

The LiquidType in the DBC is determined as follows:
If var_0x3C in the root's MOHD has the 4 bit (&4), it will take the variable in MOGP's var_0x34. If not, it checks var_0x34 for 15 (green lava). If that is set, it will take 0, else it will take var_0x34 + 1.
The result of this (0,var_0x34 or var_0x34+1) will be checked if above 21 (naxxramas slime). If yes, the entry is stored as given. Else, it will be checked for the type (ocean, water, magma, slime). Ocean might be overwritten by MOGP flags being & 0x80000.

tl;dr

MOGP.var_0x34 is LiquidType. This will be overwritten with the "WMO *" liquid types in case, this is below 21 (naxxramas slime). Additionally, it will be taken +1 if the flag in the root's header is not set.

old:

The material ID often refers to what seems to be a "special" material in the root WMO file (special because it often has a solid color/placeholder texture, or a texture from XTextures\*) - but sometimes the material referenced seems to be not special at all, so I'm not really sure how the liquid material is obtained - such as water/slime/lava.
*/
			// liquids
			WMOLiquidHeader hlq;
			gf.read(&hlq, 0x1E);

			//gLog("WMO Liquid: %dx%d, %dx%d, (%f,%f,%f) %d\n", hlq.X, hlq.Y, hlq.A, hlq.B, hlq.pos.x, hlq.pos.y, hlq.pos.z, hlq.type);

			lq = new Liquid(hlq.A, hlq.B, Vec3D(hlq.pos.x, hlq.pos.z, -hlq.pos.y));
			lq->initFromWMO(gf, wmo->mat[hlq.type], (flags&0x2000)!=0);
		} else {
			gLog("No implement wmo group chunk %s [%d].\n", fourcc, size);
		}

		// TODO: figure out/use MFOG ?

 		gf.seek((int)nextpos);
	}

	// ok, make a display list

	indoor = (flags&8192)!=0;
	//gLog("Lighting: %s %X\n\n", indoor?"Indoor":"Outdoor", flags);

	initLighting(nLR,useLights);

	//dl = glGenLists(1);
	//glNewList(dl, GL_COMPILE);
	//glDisable(GL_BLEND);
	//glColor4f(1,1,1,1);

	// generate lists for each batch individually instead
	GLuint listbase = glGenLists(nBatches);

	/*
	float xr=0,xg=0,xb=0;
	if (flags & 0x0040) xr = 1;
	if (flags & 0x2000) xg = 1;
	if (flags & 0x8000) xb = 1;
	glColor4f(xr,xg,xb,1);
	*/

	// assume that texturing is on, for unit 1

	for (int b=0; b<nBatches; b++) {

		GLuint list = listbase + b;

		WMOBatch *batch = &batches[b];
		WMOMaterial *mat = &wmo->mat[batch->texture];

		bool overbright = ((mat->flags & 0x10) && !hascv);
		bool spec_shader = (mat->specular && !hascv && !overbright);

		pair<GLuint, int> currentList;
		currentList.first = list;
		currentList.second = spec_shader ? 1 : 0;

		glNewList(list, GL_COMPILE);

        // setup texture
		glBindTexture(GL_TEXTURE_2D, mat->tex);

		bool atest = (mat->transparent) != 0;

		if (atest) {
			glEnable(GL_ALPHA_TEST);
			float aval = 0;
            if (mat->flags & 0x80) aval = 0.3f;
			if (mat->flags & 0x01) aval = 0.0f;
			glAlphaFunc(GL_GREATER, aval);
		}

		if (mat->flags & 0x04) glDisable(GL_CULL_FACE);
		else glEnable(GL_CULL_FACE);

		if (spec_shader) {
			glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, colorFromInt(mat->col2));
		} else {
			Vec4D nospec(0,0,0,1);
			glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, nospec);
		}

		/*
		float fr,fg,fb;
		fr = rand()/(float)RAND_MAX;
		fg = rand()/(float)RAND_MAX;
		fb = rand()/(float)RAND_MAX;
		glColor4f(fr,fg,fb,1);
		*/

		if (overbright) {
			// TODO: use emissive color from the WMO Material instead of 1,1,1,1
			GLfloat em[4] = {1,1,1,1};
			glMaterialfv(GL_FRONT, GL_EMISSION, em);
		}
		
		// render
		glBegin(GL_TRIANGLES);
		for (int t=0, i=batch->indexStart; t<batch->indexCount; t++,i++) {
			int a = indices[i];
			if (indoor && hascv) {
	            setGLColor(cv[a]);
			}
			glNormal3f(normals[a].x, normals[a].z, -normals[a].y);
			glTexCoord2fv(texcoords[a]);
			glVertex3f(vertices[a].x, vertices[a].z, -vertices[a].y);
		}
		glEnd();

		if (overbright) {
			GLfloat em[4] = {0,0,0,1};
			glMaterialfv(GL_FRONT, GL_EMISSION, em);
		}

		if (atest) {
			glDisable(GL_ALPHA_TEST);
		}

		glEndList();
		lists.push_back(currentList);
	}

	//glColor4f(1,1,1,1);
	//glEnable(GL_CULL_FACE);

	//glEndList();

	gf.close();

	// hmm
	indoor = false;
}


void WMOGroup::initLighting(int nLR, short *useLights)
{
	//dl_light = 0;
	// "real" lighting?
	if ((flags&0x2000) && hascv) {

		Vec3D dirmin(1,1,1);
		float lenmin;
		int lmin;

		for (int i=0; i<nDoodads; i++) {
			lenmin = 999999.0f*999999.0f;
			lmin = 0;
			ModelInstance &mi = wmo->modelis[ddr[i]];
			for (int j=0; j<wmo->nLights; j++) {
				WMOLight &l = wmo->lights[j];
				Vec3D dir = l.pos - mi.pos;
				float ll = dir.lengthSquared();
				if (ll < lenmin) {
					lenmin = ll;
					dirmin = dir;
					lmin = j;
				}
			}
			mi.light = lmin;
			mi.ldir = dirmin;
		}

		outdoorLights = false;
	} else {
		outdoorLights = true;
	}
}

void WMOGroup::draw(const Vec3D& ofs, const float rot)
{
	visible = false;
	// view frustum culling
	Vec3D pos = center + ofs;
	rotate(ofs.x,ofs.z,&pos.x,&pos.z,rot*PI/180.0f);
	if (!gWorld->frustum.intersectsSphere(pos,rad)) return;
	float dist = (pos - gWorld->camera).length() - rad;
	if (dist >= gWorld->culldistance) return;
	visible = true;
	
	if (hascv) {
		glDisable(GL_LIGHTING);
		gWorld->outdoorLights(false);
	} else {
		if (gWorld->lighting) {
			if (gWorld->skies->hasSkies()) {
				gWorld->outdoorLights(true);
			} else {
				// set up some kind of default outdoor light... ?
				glEnable(GL_LIGHT0);
				glDisable(GL_LIGHT1);
				glLightfv(GL_LIGHT0, GL_AMBIENT, Vec4D(0.4f,0.4f,0.4f,1));
				glLightfv(GL_LIGHT0, GL_DIFFUSE, Vec4D(0.8f,0.8f,0.8f,1));
				glLightfv(GL_LIGHT0, GL_POSITION, Vec4D(1,1,1,0));
			}
		} else glDisable(GL_LIGHTING);
	}
	setupFog();

	
	//glCallList(dl);
	glDisable(GL_BLEND);
	glColor4f(1,1,1,1);
	for (int i=0; i<nBatches; i++) {
		bool useshader = (supportShaders && gWorld->useshaders && lists[i].second);
		if (useshader) wmoShader->bind();
		glCallList(lists[i].first);
		if (useshader) wmoShader->unbind();
	}

	glColor4f(1,1,1,1);
	glEnable(GL_CULL_FACE);

	if (hascv) {
		if (gWorld->lighting) {
			glEnable(GL_LIGHTING);
			//glCallList(dl_light);
		}
	}


}

void WMOGroup::drawDoodads(int doodadset, const Vec3D& ofs, const float rot)
{
	if (!visible) return;
	if (nDoodads==0) return;

	gWorld->outdoorLights(outdoorLights);
	setupFog();

	/*
	float xr=0,xg=0,xb=0;
	if (flags & 0x0040) xr = 1;
	//if (flags & 0x0008) xg = 1;
	if (flags & 0x8000) xb = 1;
	glColor4f(xr,xg,xb,1);
	*/

	// draw doodads
	glColor4f(1,1,1,1);
	for (int i=0; i<nDoodads; i++) {
		short dd = ddr[i];
		bool inSet;
		// apparently, doodadset #0 (defaultGlobal) should always be visible
		inSet = ( ((dd >= wmo->doodadsets[doodadset].start) && (dd < (wmo->doodadsets[doodadset].start+wmo->doodadsets[doodadset].size))) 
			|| ( (dd >= wmo->doodadsets[0].start) && ((dd < (wmo->doodadsets[0].start+wmo->doodadsets[0].size) )) ) );
		if (inSet) {
 		//if ((dd >= wmo->doodadsets[doodadset].start) && (dd < (wmo->doodadsets[doodadset].start+wmo->doodadsets[doodadset].size))) {

			ModelInstance &mi = wmo->modelis[dd];

			if (!outdoorLights) {
				WMOLight::setupOnce(GL_LIGHT2, mi.ldir, mi.lcol);
			}

			wmo->modelis[dd].draw2(ofs,rot);
		}
	}

	glDisable(GL_LIGHT2);

	glColor4f(1,1,1,1);

}

void WMOGroup::drawLiquid()
{
	if (!visible) return;

	// draw liquid
	// TODO: culling for liquid boundingbox or something
	if (lq) {
		setupFog();
		if (outdoorLights) {
			gWorld->outdoorLights(true);
		} else {
			// TODO: setup some kind of indoor lighting... ?
			gWorld->outdoorLights(false);
			glEnable(GL_LIGHT2);
			glLightfv(GL_LIGHT2, GL_AMBIENT, Vec4D(0.1f,0.1f,0.1f,1));
			glLightfv(GL_LIGHT2, GL_DIFFUSE, Vec4D(0.8f,0.8f,0.8f,1));
			glLightfv(GL_LIGHT2, GL_POSITION, Vec4D(0,1,0,0));
		}
		glDisable(GL_BLEND);
		glDisable(GL_ALPHA_TEST);
		glDepthMask(GL_TRUE);
		glColor4f(1,1,1,1);
		lq->draw();
		glDisable(GL_LIGHT2);
	}
}

void WMOGroup::setupFog()
{
	if (outdoorLights || fog==-1) {
		gWorld->setupFog();
	} else {
		wmo->fogs[fog].setup();
	}
}



WMOGroup::~WMOGroup()
{
	//if (dl) glDeleteLists(dl, 1);
	//if (dl_light) glDeleteLists(dl_light, 1);

	//for (size_t i=0; i<lists.size(); i++) {
	//	glDeleteLists(lists[i].first);
	//}
	if (nBatches && lists.size()) glDeleteLists(lists[0].first, nBatches);

	if (nDoodads) delete[] ddr;
	if (lq) delete lq;
}


void WMOFog::init(MPQFile &f)
{
	f.read(this, 0x30);
	color = Vec4D( ((color1 & 0x00FF0000) >> 16)/255.0f, ((color1 & 0x0000FF00) >> 8)/255.0f,
					(color1 & 0x000000FF)/255.0f, ((color1 & 0xFF000000) >> 24)/255.0f);
	float temp;
	temp = pos.y;
	pos.y = pos.z;
	pos.z = -temp;
	fogstart = fogstart * fogend;
}

void WMOFog::setup()
{
	if (gWorld->drawfog) {
		glFogfv(GL_FOG_COLOR, color);
		glFogf(GL_FOG_START, fogstart);
		glFogf(GL_FOG_END, fogend);

		glEnable(GL_FOG);
	} else {
		glDisable(GL_FOG);
	}
}

int WMOManager::add(std::string name)
{
	int id;
	if (names.find(name) != names.end()) {
		id = names[name];
		items[id]->addref();
		//gLog("Loading WMO %s [already loaded]\n",name.c_str());
		return id;
	}

	// load new
	WMO *wmo = new WMO(name);
	id = nextID();
    do_add(name, id, wmo);
    return id;
}



WMOInstance::WMOInstance(WMO *wmo, MPQFile &f) : wmo (wmo)
{

	float ff[3];
    	f.read(&id, 4); // unique identifier for this instance
	f.read(ff,12); // Position (X,Y,Z)
	pos = Vec3D(ff[0],ff[1],ff[2]);
	f.read(ff,12); // Orientation (A,B,C)
	dir = Vec3D(ff[0],ff[1],ff[2]);
	f.read(ff,12); // extends
	pos2 = Vec3D(ff[0],ff[1],ff[2]);
	f.read(ff,12);
	pos3 = Vec3D(ff[0],ff[1],ff[2]);
	f.read(&flags,2);
	f.read(&doodadset,2); // Doodad set index
	f.read(&nameset, 2); // Name set
	f.read(&unk, 2);
	
	//gLog("WMO instance: %s (%d, %d)\n", wmo->name.c_str(), d2, d3);
}

void WMOInstance::draw()
{
	if (ids.find(id) != ids.end()) return;
	ids.insert(id);

	glPushMatrix();
	glTranslatef(pos.x, pos.y, pos.z);

	float rot = -90.0f + dir.y;

	// TODO: replace this with a single transform matrix calculated at load time

	glRotatef(dir.y - 90.0f, 0, 1, 0);
	glRotatef(-dir.x, 0, 0, 1);
	glRotatef(dir.z, 1, 0, 0);

	wmo->draw(doodadset,pos,-rot);

	glPopMatrix();
}

/*
void WMOInstance::drawPortals()
{
	if (ids.find(id) != ids.end()) return;
	ids.insert(id);

	glPushMatrix();
	glTranslatef(pos.x, pos.y, pos.z);

	glRotatef(dir.y - 90.0f, 0, 1, 0);
	glRotatef(-dir.x, 0, 0, 1);
	glRotatef(dir.z, 1, 0, 0);

	wmo->drawPortals();
	glPopMatrix();
}
*/

void WMOInstance::reset()
{
    ids.clear();
}

std::set<int> WMOInstance::ids;
