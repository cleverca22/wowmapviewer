#include "util.h"
#include "dbcfile.h"
#include "mpq.h"

DBCFile::DBCFile(const std::string &filename):
	filename(filename)
{
	data = NULL;
}

bool DBCFile::open()
{
	int db_type = 0;

	Lower(filename);

	if (EndsWith(filename,"item.dbc")) {//&& gameVersion >= 40000) {
		filename = BeforeLast(filename.c_str(),".") + ".db2";
	}

	MPQFile f(filename.c_str());
	// Need some error checking, otherwise an unhandled exception error occurs
	// if people screw with the data path.
	if (f.isEof())
		return false;

	char header[5];
	unsigned int na,nb,es,ss;

	f.read(header, 4); // File Header

	if (strncmp(header, "WDBC", 4) == 0)
		db_type = 1;
	else if (strncmp(header, "WDB2", 4) == 0)
		db_type = 2;

	if (db_type == 0) {
		gLog("dbc file corrupt %s\n",filename.c_str());
		f.close();
		data = NULL;
		return false;
	}

	//assert(header[0]=='W' && header[1]=='D' && header[2]=='B' && header[3] == 'C');

	f.read(&na,4); // Number of records
	f.read(&nb,4); // Number of fields
	f.read(&es,4); // Size of a record
	f.read(&ss,4); // String size

	if (db_type == 2) {
		f.seekRelative(28);
		// just some buggy check
		unsigned int check;
		f.read(&check, 4);
		if (check == 6) // wrong place
			f.seekRelative(-20);
		else // check == 17, right place
			f.seekRelative(-4);
	}
	
	recordSize = es;
	recordCount = na;
	fieldCount = nb;
	stringSize = ss;
	//assert(fieldCount*4 == recordSize);
	// not always true, but it works fine till now
	assert(fieldCount*4 >= recordSize);

	data = new unsigned char[recordSize*recordCount+stringSize];
	stringTable = data + recordSize*recordCount;
	f.read(data, recordSize*recordCount+stringSize);
	f.close();
	return true;
}

DBCFile::~DBCFile()
{
	delete [] data;
}

DBCFile::Record DBCFile::getRecord(size_t id)
{
	assert(data);
	return Record(*this, data + id*recordSize);
}

DBCFile::Iterator DBCFile::begin()
{
	assert(data);
	return Iterator(*this, data);
}
DBCFile::Iterator DBCFile::end()
{
	assert(data);
	return Iterator(*this, stringTable);
}

