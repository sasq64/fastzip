#ifndef FASTZIP_H
#define FASTZIP_H

#include "crypto.h"
#include "utils.h"

#include <cstdint>
#include <vector>
#include <functional>

class fastzip_exception : public std::exception
{
public:
    fastzip_exception(const std::exception &e) : msg(e.what()) {}
    fastzip_exception(const char *ptr = "Fastzip Exception") : msg(ptr) {}
    virtual const char *what() const throw () { return msg; }
private:
    const char *msg;
};

enum
{
    INTEL_FAST,
    INFOZIP
};

enum PackFormat
{
    UNCOMPRESSED,
    ZIP1_COMPRESSED,
    ZIP2_COMPRESSED,
    ZIP3_COMPRESSED,
    ZIP4_COMPRESSED,
    ZIP5_COMPRESSED,
    ZIP6_COMPRESSED,
    ZIP7_COMPRESSED,
    ZIP8_COMPRESSED,
    ZIP9_COMPRESSED,
    COMPRESSED,
    INTEL_COMPRESSED,
    UNKNOWN
};

struct FileTarget
{
    FileTarget(const std::string &source = "", const std::string &target = "",
               PackFormat pf = INTEL_COMPRESSED)
        : source(source), target(target), packFormat(pf)
    {
    }

    std::string source;
    std::string target;
    uint32_t offset = 0xffffffff;
    PackFormat packFormat;

    bool operator==(const FileTarget &other) const { return other.target == target; }

    bool operator<(const FileTarget &other) const { return other.target < target; }
};

struct ZipEntry;
class ZipArchive;

class Fastzip
{
public:
    // Variables to be set by application code
    std::string zipfile;
    bool verbose = false;
    bool junkPaths = false;
    bool doSign = false;
    bool doSeq = false;
    bool zipAlign = false;
    std::vector<std::string> storeExts;
    std::string keystoreName;
    std::string keyPassword;
    std::string keyName;
    int threadCount = 1;
    int earlyOut = 98;

    // Add a file to be packed into the target zip
    void addZip(std::string zipName, PackFormat format);
    // Add a directory to be recursively packed into the target zip
    void addDir(std::string dirName, PackFormat format);
    // Run fastzip with given options and files
    void exec();

    // Set the output function used to report warnings. Default is to print to stderr
    void setOuputFunction(std::function<void(const std::string)> f) { warning = f; }

private:
    std::function<void(const std::string)> warning = [&](const std::string &text)
        {
            fprintf(stderr, "**Warn: %s\n", text.c_str());
        };

    void packZipData(FILE *fp, int size, PackFormat inFormat, PackFormat outFormat, uint8_t *sha,
        ZipEntry &target);
    void sign(ZipArchive &archive, const std::string &digestFile);

    UniQueue<FileTarget> fileNames;
    int strLen = 0;

    KeyStore keyStore;
};

#endif // FASTZIP_H
