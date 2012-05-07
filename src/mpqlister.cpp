#include <iostream>
#include "mpq_stormlib.h"
using namespace std;

int main(int argc, char *argv[]) {
	std::vector<MPQArchive*> archives;
	if (argc < 2) {
		cout << "usage: mpqlister ~/.wine/drive_c/Program\\ Files/World\\ of\\ Warcraft/Data/world.MPQ" << endl;
		return 0;
	}

	archives.push_back(new MPQArchive(argv[1]));
	MPQFile listing("(listfile)");

	if (!listing.exists("(listfile)")) {
		cout << "listfile not found" << endl;
		return 0;
	}
	while (!listing.isEof()) {
		char buffer[512];
		listing.read(buffer,512);
		cout << buffer;
	}
}
