#include "mpq_stormlib.h"
#include "wowmapview.h"

#include <vector>
#include <string>
#include "util.h"

using namespace std;

typedef vector< pair< string, HANDLE* > > ArchiveSet;
static ArchiveSet gOpenArchives;

MPQArchive::MPQArchive(const char* filename) : ok(false)
{
	if (!SFileOpenArchive(filename, 0, MPQ_OPEN_FORCE_MPQ_V1|MPQ_OPEN_READ_ONLY, &mpq_a )) {
		int nError = GetLastError();
		gLog("Error opening archive %s, error #: 0x%x\n", filename, nError);
		return;
	}
	gLog("Opening archive %s\n", filename);
	
	// do patch, but skip cache\ directory
	/*
	if (!(Lower(BeforeLast(filename,SLASH)).find("cache") && 
		StartsWith(Lower(AfterLast(filename,SLASH)),"patch")) &&
		!isPartialMPQ(filename)) { // skip the PTCH files atrchives
		// do patch
		for(int j=(int)mpqArchives.GetCount()-1; j>=0; j--) {
			if (!mpqArchives[j].AfterLast(SLASH).StartsWith(wxT("wow-update-")))
				continue;
			if (mpqArchives[j].AfterLast(SLASH).Len() == strlen("wow-update-xxxxx.mpq")) {
				SFileOpenPatchArchive(mpq_a, mpqArchives[j], "base", 0);
				gLog("Appending base patch %s on %s", mpqArchives[j].c_str(), filename.c_str());
				SFileOpenPatchArchive(mpq_a, mpqArchives[j], langName, 0);
				gLog("Appending %s patch %s on %s", langName.c_str(), mpqArchives[j].c_str(), filename.c_str());
			} else if (mpqArchives[j].BeforeLast(SLASH) == filename.BeforeLast(SLASH)) {
				SFileOpenPatchArchive(mpq_a, mpqArchives[j], "", 0);
				gLog("Appending patch %s on %s", mpqArchives[j], filename);
			}
		}
	}
	*/

	ok = true;
	gOpenArchives.push_back( make_pair( filename, &mpq_a ) );
}

MPQArchive::~MPQArchive()
{
	/*
	for(ArchiveSet::iterator i=gOpenArchives.begin(); i!=gOpenArchives.end();++i)
	{
		mpq_archive &mpq_a = **i;
		
		free(mpq_a.header);
	}
	*/
	//gOpenArchives.erase(gOpenArchives.begin(), gOpenArchives.end());
}

bool MPQArchive::isPartialMPQ(const char* filename)
{
	if (StartsWith(AfterLast(filename,SLASH),"wow-update-"))
		return true;
	return false;
}

void MPQArchive::close()
{
	if (ok == false)
		return;
	SFileCloseArchive(mpq_a);
	for(ArchiveSet::iterator it=gOpenArchives.begin(); it!=gOpenArchives.end();++it)
	{
		HANDLE &mpq_b = *it->second;
		if (&mpq_b == &mpq_a) {
			gOpenArchives.erase(it);
			//delete (*it);
			return;
		}
	}
	
}

bool MPQFile::isPartialMPQ(const char* filename)
{
	if (StartsWith(AfterLast(filename,SLASH),"wow-update-"))
		return true;
	return false;
}

void
MPQFile::openFile(const char* filename)
{
	eof = false;
	buffer = 0;
	pointer = 0;
	size = 0;

	for(ArchiveSet::iterator i=gOpenArchives.begin(); i!=gOpenArchives.end(); ++i)
	{
		HANDLE &mpq_a = *i->second;

		HANDLE fh;

		if( !SFileOpenFileEx( mpq_a, filename, SFILE_OPEN_PATCHED_FILE, &fh ) )
			continue;

		// Found!
		DWORD filesize = SFileGetFileSize( fh );
		size = filesize;

		// HACK: in patch.mpq some files don't want to open and give 1 for filesize
		if (size<=1) {
			eof = true;
			buffer = 0;
			return;
		}

		buffer = new unsigned char[size];
		SFileReadFile( fh, buffer, (DWORD)size );
		SFileCloseFile( fh );

		return;
	}

	eof = true;
	buffer = 0;
}

MPQFile::MPQFile(const char* filename):
	eof(false),
	buffer(0),
	pointer(0),
	size(0)
{
	openFile(filename);
}

MPQFile::~MPQFile()
{
	close();
}

bool MPQFile::exists(const char* filename)
{
	for(ArchiveSet::iterator i=gOpenArchives.begin(); i!=gOpenArchives.end();++i)
	{
		HANDLE &mpq_a = *i->second;

		if( SFileHasFile( mpq_a, filename ) )
			return true;
	}

	return false;
}

void MPQFile::save(const char* filename)
{
/*
	wxFile f;
	f.Open(const char*(filename, wxConvUTF8), wxFile::write);
	f.Write(buffer, size);
	f.Close();
*/
}

size_t MPQFile::read(void* dest, size_t bytes)
{
	if (eof) 
		return 0;

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

void MPQFile::seek(ssize_t offset)
{
	pointer = offset;
	eof = (pointer >= size);
}

void MPQFile::seekRelative(ssize_t offset)
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

int MPQFile::getSize(const char* filename)
{
	for(ArchiveSet::iterator i=gOpenArchives.begin(); i!=gOpenArchives.end();++i)
	{
		HANDLE &mpq_a = *i->second;
		HANDLE fh;
		
		if( !SFileOpenFileEx( mpq_a, filename, SFILE_OPEN_PATCHED_FILE, &fh ) )
			continue;

		DWORD filesize = SFileGetFileSize( fh );
		SFileCloseFile( fh );
		return filesize;
	}

	return 0;
}

const char* MPQFile::getArchive(const char* filename)
{
	for(ArchiveSet::iterator i=gOpenArchives.begin(); i!=gOpenArchives.end();++i)
	{
		HANDLE &mpq_a = *i->second;
		HANDLE fh;
		
		if( !SFileOpenFileEx( mpq_a, filename, SFILE_OPEN_PATCHED_FILE, &fh ) )
			continue;

		return i->first.c_str();
	}

	return "unknown";
}

size_t MPQFile::getPos()
{
	return pointer;
}

unsigned char* MPQFile::getBuffer()
{
	return buffer;
}

unsigned char* MPQFile::getPointer()
{
	return buffer + pointer;
}
