#ifndef MPQ_H
#define MPQ_H

//#include "SFmpqapi.h"
#include "libmpq/mpq.h"


class MPQArchive
{
	//MPQHANDLE handle;
	mpq_archive mpq_a;
public:
	MPQArchive(const char* filename);
	~MPQArchive();

	void close();
};


class MPQFile
{
	//MPQHANDLE handle;
	bool eof;
	unsigned char *buffer;
	size_t pointer, size;

	// disable copying
	MPQFile(const MPQFile &f) {}
	void operator=(const MPQFile &f) {}

public:
	MPQFile():eof(false),size(0),buffer(0),pointer(0) {}
	MPQFile(const char* filename);	// filenames are not case sensitive
	void openFile(const char* filename);
	~MPQFile();
	size_t read(void* dest, size_t bytes);
	size_t getSize();
	size_t getPos();
	unsigned char* getBuffer();
	unsigned char* getPointer();
	bool isEof();
	void seek(int offset);
	void seekRelative(int offset);
	void close();

	static bool exists(const char* filename);
	static int getSize(const char* filename); // Used to do a quick check to see if a file is corrupted
};

inline void flipcc(char *fcc)
{
	char t;
	t=fcc[0];
	fcc[0]=fcc[3];
	fcc[3]=t;
	t=fcc[1];
	fcc[1]=fcc[2];
	fcc[2]=t;
}



#endif

