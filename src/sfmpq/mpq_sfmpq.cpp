#include "mpq_sfmpq.h"
#include "wowmapview.h"

int _id=1;

MPQArchive::MPQArchive(const char* filename)
{
	BOOL succ = SFileOpenArchive(filename, _id++, 0, &handle);
	if (succ) {
		MPQARCHIVE *ar = (MPQARCHIVE*)(handle);
        succ = true;
	} else {
		gLog("Error opening archive %s\n", filename);
	}
}

void MPQArchive::close()
{
	SFileCloseArchive(handle);
}

MPQFile::MPQFile(const char* filename)
{
	BOOL succ = SFileOpenFile(filename, &handle);
	pointer = 0;
	if (succ) {
		DWORD s = SFileGetFileSize(handle, 0);
		buffer = new char[s];
		SFileReadFile(handle, buffer, s, 0, 0);
		SFileCloseFile(handle);
		size = (size_t)s;
		eof = false;
	} else {
		eof = true;
		buffer = 0;
	}
}

MPQFile::~MPQFile()
{
	close();
}


size_t MPQFile::read(void* dest, size_t bytes)
{
	if (eof) return 0;

	size_t rpos = pointer + bytes;
	if (rpos > size) {
		bytes = size - pointer;
		eof = true;
	}

	memcpy(dest, &(buffer[pointer]), bytes);

	pointer = rpos;

	return bytes;
}

bool MPQFile::isEof()
{
    return eof;
}

void MPQFile::seek(int offset)
{
	pointer = offset;
	eof = (pointer >= size);
}

void MPQFile::seekRelative(int offset)
{
	pointer += offset;
	eof = (pointer >= size);
}

void MPQFile::close()
{
	if (buffer) delete[] buffer;
	buffer = 0;
	eof = true;
}

size_t MPQFile::getSize()
{
	return size;
}

size_t MPQFile::getPos()
{
	return pointer;
}

char* MPQFile::getBuffer()
{
	return buffer;
}

char* MPQFile::getPointer()
{
	return buffer + pointer;
}
