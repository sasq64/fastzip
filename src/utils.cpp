#include "utils.h"

#include <ctime>
#include <experimental/filesystem>

#ifdef _WIN32
#    include <direct.h>
#endif

namespace fs = std::experimental::filesystem;

void makedir(const std::string& name)
{
    fs::create_directory(name);
}

void makedirs(const std::string& path)
{
    fs::create_directories(path);
}

std::string path_basename(const std::string& name)
{
    return fs::path(name).stem().string();
}

std::string path_directory(const std::string& name)
{
    return fs::path(name).parent_path().string();
}

std::string path_filename(const std::string& name)
{
    return fs::path(name).filename().string();
}

bool startsWith(const std::string& name, const std::string& pref)
{
    auto pos = name.find(pref);
    return (pos == 0);
}

bool fileExists(const std::string& name)
{
    struct stat ss;
    return stat(name.c_str(), &ss) == 0;
}

void _listFiles(const std::string& dirName,
                const std::function<void(const std::string& path)>& f)
{
    for (const auto& p : fs::directory_iterator(dirName)) {
        auto&& path = p.path().string();
        if (path[0] == '.' &&
            (path[1] == 0 || (path[1] == '.' && path[2] == 0)))
            continue;
        if (fs::is_directory(p.status()))
            listFiles(path, f);
        else
            f(path);
    }
}

void listFiles(const std::string& dirName,
               const std::function<void(const std::string& path)>& f)
{
    if (!fs::is_directory(dirName)) {
        f(dirName);
        return;
    }
    _listFiles(dirName, f);
}

time_t msdosToUnixTime(uint32_t m)
{
    struct tm t;
    t.tm_year = (m >> 25) + 80;
    t.tm_mon = ((m >> 21) & 0xf) - 1;
    t.tm_mday = (m >> 16) & 0x1f;
    t.tm_hour = (m >> 11) & 0x1f;
    t.tm_min = (m >> 5) & 0x3f;
    t.tm_sec = (m & 0x1f) << 1;
    t.tm_isdst = -1;
    return mktime(&t);
}

fs::file_time_type msdosToFileTime(uint32_t m)
{
    return fs::file_time_type::clock::from_time_t(msdosToUnixTime(m));
}

void removeFiles(const std::string& dirName)
{
    std::error_code ec;
    fs::remove_all(dirName, ec);
}
