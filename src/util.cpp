#include "util.h"
#ifdef _WINDOWS
#include <windows.h>
#endif
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <unistd.h>
#include "defines.h"

std::string gamePath;
vector<std::string> mpqArchives;

// Utilities
FILE *flog;
bool glogfirst = true;

void check_stuff() {
     assert(sizeof(__int16) == 2);
     assert(sizeof(__int32) == 4);
}
void gLog(const char *str, ...)
{
	if (glogfirst) {
		flog = fopen("log.txt","w");
		fclose(flog);
		glogfirst = false;
	}

	flog = fopen("log.txt","a");

	va_list ap;

	va_start (ap, str);
	vfprintf (flog, str, ap);
	va_end (ap);

	va_start (ap,str);
	vprintf(str,ap);
	va_end(ap);

	fclose(flog);
};

int file_exists(char *path)
{
#ifdef _WINDOWS
	FILE *f = fopen(path, "r");
	if (f) {
		fclose(f);
		return true;
	}
#else
	if (access(path,R_OK) == 0) return true;
#endif
	return false;
};


// String Effectors
void Lower(std::string &text){
	transform(text.begin(), text.end(), text.begin(), (int(*)(int)) std::tolower);
};
bool StartsWith (std::string const &fullString, std::string const &starting)
{
    if (fullString.length() > starting.length()) {
    	return (0 == fullString.compare(0, starting.length(), starting));
    } else {
    	return false;
    }
};

bool EndsWith (std::string const &fullString, std::string const &ending)
{
    if (fullString.length() > ending.length()) {
    	return (0 == fullString.compare(fullString.length() - ending.length(), ending.length(), ending));
    } else {
    	return false;
    }
};

std::string BeforeLast(const char* String, const char* Last)
{
	std::string a = String;
	size_t ending = a.rfind(Last);
	if (!ending)
		ending = a.length();
	std::string finalString = a.substr(0,ending);

	return finalString;
};

std::string AfterLast(const char* String, const char* Last)
{
	std::string a = String;
	size_t ending = a.rfind(Last);
	if (!ending)
		ending = 0;
	std::string finalString = a.substr(ending,(a.length()-ending));

	return finalString;
};

std::string GetLast(const char* String){
	std::string a = String;
	size_t b = a.length();
	return a.substr(b-1,b);
};

void getGamePath()
{
#ifdef _WINDOWS
	HKEY key;
	unsigned long t, s;
	long l;
	unsigned char path[1024];
	//memset(path, 0, sizeof(path));
	s = 1024;
	memset(path,0,s);

	l = RegOpenKeyEx(HKEY_LOCAL_MACHINE,"SOFTWARE\\Wow6432Node\\Blizzard Entertainment\\World of Warcraft\\Beta",0,KEY_QUERY_VALUE,&key);
	if (l != ERROR_SUCCESS)
		l = RegOpenKeyEx(HKEY_LOCAL_MACHINE,"SOFTWARE\\Wow6432Node\\Blizzard Entertainment\\World of Warcraft\\PTR",0,KEY_QUERY_VALUE,&key);
	if (l != ERROR_SUCCESS)
		l = RegOpenKeyEx(HKEY_LOCAL_MACHINE,"SOFTWARE\\Wow6432Node\\Blizzard Entertainment\\World of Warcraft",0,KEY_QUERY_VALUE,&key);
	if (l != ERROR_SUCCESS) 
		l = RegOpenKeyEx(HKEY_LOCAL_MACHINE,"SOFTWARE\\Blizzard Entertainment\\World of Warcraft\\Beta",0,KEY_QUERY_VALUE,&key);
	if (l != ERROR_SUCCESS)
		l = RegOpenKeyEx(HKEY_LOCAL_MACHINE,"SOFTWARE\\Blizzard Entertainment\\World of Warcraft\\PTR",0,KEY_QUERY_VALUE,&key);
	if (l != ERROR_SUCCESS)
		l = RegOpenKeyEx(HKEY_LOCAL_MACHINE,"SOFTWARE\\Blizzard Entertainment\\World of Warcraft",0,KEY_QUERY_VALUE,&key);
	if (l == ERROR_SUCCESS) {
		l = RegQueryValueEx(key,"InstallPath",0,&t,(LPBYTE)path,&s);
		RegCloseKey(key);
		gamePath.append(reinterpret_cast<const char*>(path));
		gamePath.append("Data\\");
	}
	//gLog("GamePath: %s\r\n",gamePath.c_str());
/*

	// WoW Model Viewer Edition.
	// Needs WX stuff replaced.

	vector<string> sNames;
	gamePath.empty();

	// if it failed, look for World of Warcraft install
	const char* regpaths[] = {
		// _WIN64
		"SOFTWARE\\Wow6432Node\\Blizzard Entertainment\\World of Warcraft",
		"SOFTWARE\\Wow6432Node\\Blizzard Entertainment\\World of Warcraft\\PTR",
		"SOFTWARE\\Wow6432Node\\Blizzard Entertainment\\World of Warcraft\\Beta",
		//_WIN32, but for compatible
		"SOFTWARE\\Blizzard Entertainment\\World of Warcraft",
		"SOFTWARE\\Blizzard Entertainment\\World of Warcraft\\PTR",
		"SOFTWARE\\Blizzard Entertainment\\World of Warcraft\\Beta",
	};

	for (size_t i=0; i<6; i++) {
		l = RegOpenKeyEx((HKEY)HKEY_LOCAL_MACHINE, regpaths[i], 0, KEY_QUERY_VALUE, &key);

		if (l == ERROR_SUCCESS) {
			s = sizeof(path);
			l = RegQueryValueEx(key, "InstallPath", 0, &t,(LPBYTE)path, &s);
			bool exists = false;
			if (_access( (const char*)path, 0 ) == 0 )
				exists = true;
			if ((l == ERROR_SUCCESS) && (exists == true)) {
				std::string a = reinterpret_cast<const char*>(path);
				sNames.push_back(a);
			}
			RegCloseKey(key);
		}
	}

	gLog("Number of WoW Paths Detected: %i",sNames.size());
	if (sNames.size() > 0){
		for (int i=0;i<sNames.size();i++){
			gLog("Path %i: %s",i,sNames[i]);
		}
	}
	
	if (sNames.size() == 1)
		gamePath = sNames[0].c_str();
	else if (sNames.size() > 1)
		//gamePath = wxGetSingleChoice("Please select a Path:", "Path", sNames);

	// If we found an install then set the game path, otherwise just set to C:\ for now
	if (gamePath == "") {
		gamePath = "C:\\Program Files\\World of Warcraft\\";
		if (!wxFileExists(gamePath+wxT("Wow.exe"))){
			gamePath = wxDirSelector(wxT("Please select your World of Warcraft folder:"), gamePath);
		}
	}
	if (GetLast(gamePath.c_str()) != SLASH)
		gamePath.append(SLASH);
	gamePath.append("Data\\");
#elif _MAC // Mac OS X
    gamePath = wxT("/Applications/World of Warcraft/");
	if (!wxFileExists(gamePath+wxT("Data/common.MPQ")) && !wxFileExists(gamePath+wxT("Data/art.MPQ")) ){
        gamePath = wxDirSelector(wxT("Please select your World of Warcraft folder:"), gamePath);
    }
	if (gamePath.Last() != SLASH)
		gamePath.Append(SLASH);
	gamePath.Append(wxT("Data/"));
#else // Linux
	gamePath = wxT(".")+SLASH;
	if (!wxFileExists(gamePath+wxT("Wow.exe"))){
		gamePath = wxDirSelector(wxT("Please select your World of Warcraft folder:"), gamePath);
	}
	if (gamePath.Last() != SLASH)
		gamePath.Append(SLASH);
	gamePath.Append(wxT("Data/"));
	*/
#endif
}
