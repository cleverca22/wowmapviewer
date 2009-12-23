
#ifndef FONT_H
#define FONT_H

class Font;


struct charinfo {
	int baseline;
	int x,y,w,h;
	float tx1,tx2,ty1,ty2;
};


class Font
{
public:

	unsigned int tex;
	int tw,th;

	int size;

	charinfo chars[256];

	Font(unsigned int tex, int tw, int th, int size, const char *infofile);
	~Font();

	void drawtext(int x, int y, const char *text);
	void shdrawtext(int x, int y, const char *text);
	int textwidth(const char *text);

	void print(int x, int y, const char *str, ...);
	void shprint(int x, int y, const char *str, ...);

protected:
	void drawchar(int x, int y, const char ch);

};


#endif

