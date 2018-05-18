#include "utils.h"

#include <experimental/filesystem>
#include <ctime>

#ifdef _WIN32
#include <direct.h>
#endif

namespace fs = std::experimental::filesystem;

void makedir(const std::string &name) {
	fs::create_directory(name);
}

void makedirs(const std::string &path) {
	int start = 0;
	while(true) {
		auto pos = path.find_first_of("/\\", start);
		if(pos != std::string::npos) {
			makedir(path.substr(0, pos));
			start = pos+1;
        } else {
            makedir(path);
			break;
        }
	}
}

std::string path_basename(const std::string &name) {
	auto slashPos = name.rfind('/');
	if(slashPos == std::string::npos)
		slashPos = 0;
	else
		slashPos++;
	auto dotPos = name.rfind('.');
	if(dotPos == std::string::npos || dotPos < slashPos)
		return name.substr(slashPos);
	return name.substr(slashPos, dotPos-slashPos);
}

std::string path_directory(const std::string &name) {
	auto slashPos = name.rfind('/');
	if(slashPos == std::string::npos)
		slashPos = 0;
	return name.substr(0, slashPos);
}

std::string path_filename(const std::string &name) {
	auto slashPos = name.rfind('/');
	if(slashPos == std::string::npos)
		slashPos = 0;
	else
		slashPos++;
	return name.substr(slashPos);
}

bool startsWith(const std::string &name, const std::string &pref) {
	auto pos = name.find(pref);
	return (pos == 0);
}


bool fileExists(const std::string &name)
{
    struct stat ss;
    return stat(name.c_str(), &ss) == 0;
}

void listFiles(char *dirName, const std::function<void(const std::string &path)>& f)
{
	for (const auto& p : fs::directory_iterator(dirName)) {
		auto path = p.path().string();
		if (path[0] == '.' && (path[1] == 0 || (path[1] == '.' && path[2] == 0)))
			continue;
		if (fs::is_directory(p.status()))
			listFiles(path, f);
		else
			f(path);
	}
}

void listFiles(const std::string &dirName, const std::function<void(const std::string &path)>& f)
{
    char path[16384];
    strcpy(path, dirName.c_str());
    listFiles(path, f);
}

time_t msdosToUnixTime(uint32_t m)
{
    struct tm t;
    t.tm_year = (m >> 25) + 80;
    t.tm_mon = ((m >> 21) & 0xf) - 1;
    t.tm_mday = (m >> 16) & 0x1f;
    t.tm_hour = (m >> 11) & 0x1f;
    t.tm_min = (m >> 5) & 0x3f;
    t.tm_sec = m & 0x1f;
    t.tm_isdst = -1;
    return mktime(&t);
}

fs::file_time_type msdosToFileTime(uint32_t m)
{
	return fs::file_time_type::clock::from_time_t(msdosToUnixTime(m));
}

void removeFiles(char *dirName)
{
	fs::remove_all(dirName);
}

void removeFiles(const std::string &dirName)
{
	char d[16384];
	strcpy(d, dirName.c_str());
	removeFiles(d);
}
