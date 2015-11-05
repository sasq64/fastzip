#include "utils.h"

bool fileExists(const std::string &name)
{
    struct stat ss;
    return stat(name.c_str(), &ss) == 0;
}

void listFiles(char *dirName, std::function<void(const std::string &path)> &f)
{
    DIR *dir;
    struct dirent *ent;

    struct stat ss;
    stat(dirName, &ss);
    if ((ss.st_mode & S_IFDIR) == 0)
    {
        f(dirName);
        return;
    }

    if ((dir = opendir(dirName)) != nullptr)
    {
        char *dirEnd = dirName + strlen(dirName);
        *dirEnd++ = PATH_SEPARATOR;
        *dirEnd = 0;
        int dirLen = strlen(dirName);
        while ((ent = readdir(dir)) != nullptr)
        {
            char *p = ent->d_name;
            if (p[0] == '.' && (p[1] == 0 || (p[1] == '.' && p[2] == 0)))
                continue;

            strcpy(dirEnd, p);
#ifdef _WIN32
            stat(dirName, &ss);
            if ((ss.st_mode & S_IFDIR) != 0)
#else
            if (ent->d_type == DT_DIR)
#endif
                listFiles(dirName, f);
            else
            {
                f(dirName);
            }
        }
        closedir(dir);
    }
}

void listFiles(const std::string &dirName, std::function<void(const std::string &path)> f)
{
    char path[16384];
    strcpy(path, dirName.c_str());
    listFiles(path, f);
}

uint32_t msdosToUnixTime(uint32_t m)
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
