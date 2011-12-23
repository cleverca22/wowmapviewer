#include "dbcfile.h"
#include <iostream>
#include "mpq_stormlib.h"

// DBFilesClient\\TaxiNodes.dbc iifffsii
// routeid ? x y z name flag flag
// DBFilesClient\\TaxiPath.dbc
// id from to cost

using namespace std;

int main(int argc, char *argv[]) {
	string format_out = "";
	int *min = NULL,*max = NULL,row;
	std::vector<MPQArchive*> archives;
	const char * format = "";
	if (argc < 2) {
		cout << "usage: dbclister DBFilesClient\\Movie.dbc is" << endl << "second argument is the type for each field" << endl;
		return 0;
	}
	std::string filename = argv[1];
	if (argc > 2) format = argv[2];

	archives.push_back(new MPQArchive("/home/clever/.wine/drive_c/Program Files/World of Warcraft Public Test/Data/enUS/locale-enUS.MPQ"));

	DBCFile db(filename);
	if (!db.open()) {
		cout << "error opening db file" << endl;
		goto error;
	}
	cout << "recordSize " << db.getRecordSize() << endl << "recordCount " << db.getRecordCount() << endl << "fieldCount " << db.getFieldCount() << endl << "stringSize " << db.getStringSize() << endl;

	min = new int[db.getFieldCount()];
	max = new int[db.getFieldCount()];
	for (int j = 0; j < db.getFieldCount(); j++) {
		min[j] = db.getStringSize(); // FIXME
		max[j] = 0;
	}

	row = 0;
	for(DBCFile::Iterator i=db.begin(); i!=db.end(); ++i) {
		//cout << row << " ";
		for (int j = 0; j < db.getFieldCount(); j++) {
			char type;
			if (j < strlen(format)) type = format[j];
			else type = 'i';

			int temp = i->getInt(j);
			if (temp < min[j]) min[j] = temp;
			if (temp > max[j]) max[j] = temp;
			switch (type) {
				case 's':
					cout << i->getString(j) << " ";
					break;
				case 'f':
					cout << i->getFloat(j) << "\t";
					break;
				default:
				case 'i':
					cout << i->getInt(j) << "\t";
					break;
				case '_':
					break;
			}
		}
		cout << endl;
		row++;
	}

	for (int j = 0; j < db.getFieldCount(); j++) {
		cout << "field number " << j << "\tmin: " << min[j] << "\tmax: " << max[j] << " type ";
		char type;
		if (j < strlen(format)) type = format[j];
		else type = '_';
		cout << type << " ";

		if (max[j] < (0.5f * db.getStringSize())) {
			cout << "int?" << endl;
			format_out += "i";
		} else if (max[j] < db.getStringSize()) {
			cout << "string?" << endl;
			format_out += "s";
		} else {
			cout << "int?" << endl;
			format_out += "i";
		}
	}
	cout << format_out << endl;

	error:
	for (std::vector<MPQArchive*>::iterator it = archives.begin(); it != archives.end(); ++it) {
		(*it)->close();
	}
	archives.clear();
	delete []min;
	delete[] max;
}
