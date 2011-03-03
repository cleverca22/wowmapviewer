#ifndef UTIL_H
#define UTIL_H

#include <algorithm>
#include <cctype>
#include <string>
#include <stdlib.h>
#include <iostream>
#include <vector>
#include <io.h>
using namespace std;

extern std::string gamePath;
extern int gameVersion;
extern vector<std::string> mpqArchives;
extern FILE *flog;
extern bool glogfirst;

#ifdef _WINDOWS
	#define SLASH "\\"
#else
	#define SLASH "/"
#endif
#define	MPQ_SLASH "\\"


#ifdef _WIN64
	typedef __int64				ssize_t;
#else
	typedef __int32				ssize_t;
#endif

bool StartsWith (std::string const &fullString, std::string const &starting);
bool EndsWith (std::string const &fullString, std::string const &ending);
std::string BeforeLast(const char* String, const char* Last);
std::string AfterLast(const char* String, const char* Last);
std::string GetLast(const char* String);

void Lower(std::string &text);
void getGamePath();

void gLog(char *str, ...);
int file_exists(char *path);

#endif
