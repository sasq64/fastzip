#pragma once

#include "crypto.h"
#include "utils.h"

#include <cstdint>
#include <experimental/filesystem>
#include <functional>
#include <vector>
namespace fs = std::experimental::filesystem;

class fastzip_exception : public std::exception
{
public:
    explicit fastzip_exception(const char* ptr = "Fastzip Exception") : msg(ptr)
    {}
    explicit fastzip_exception(const std::exception& e) : msg(e.what()) {}
    const char* what() const noexcept override { return msg; }

private:
    const char* msg;
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

struct PathAlias
{
    PathAlias(const std::string& alias)
    {
        auto parts = split(alias, "=");
        if (parts.size() > 1) {
            diskPath = parts[0];
            aliasTo = parts[1];
        } else
            diskPath = alias;
    }

    PathAlias(const char* path) : PathAlias(std::string(path)) {}
    PathAlias(const fs::path& path) : diskPath(path) {}

    fs::path diskPath;
    std::string aliasTo;
};

struct FileTarget
{
    FileTarget(const std::string& aSource = "", const std::string& aTarget = "",
               PackFormat pf = INTEL_COMPRESSED, uint64_t offs = 0xffffffff)
        : source(aSource), target(aTarget), packFormat(pf), offset(offs)
    {}

    fs::path source;
    std::string target;
    uint64_t size = 0;
    PackFormat packFormat;
    uint64_t offset = 0xffffffff;

    bool operator==(const FileTarget& other) const
    {
        return other.target == target;
    }

    bool operator<(const FileTarget& other) const
    {
        return other.target < target;
    }
};

struct ZipEntry;
class ZipArchive;
class File;

class Fastzip
{
public:
    // Variables to be set by application code
    fs::path zipfile;
    bool verbose = false;
    bool junkPaths = false;
    bool doSign = false;
    bool doSeq = false;
    bool zipAlign = false;
    std::vector<std::string> storeExts;
    fs::path keystoreName;
    std::string keyPassword;
    std::string keyName;
    int threadCount = 1;
    int earlyOut = 98;
    bool force64 = false;

    // Add a file to be packed into the target zip
    void addZip(const fs::path& zipName, PackFormat format);
    // Add a directory to be recursively packed into the target zip
    void addDir(const PathAlias& dirName, PackFormat format);
    // Run fastzip with given options and files
    void exec();

    // Set the output function used to report warnings. Default is to print to
    // stderr
    void setOuputFunction(std::function<void(const std::string)> f)
    {
        warning = std::move(f);
    }

    size_t fileCount() { return fileNames.size(); }

private:
    std::function<void(const std::string)> warning =
        [&](const std::string& text) {
            fprintf(stderr, "**Warn: %s\n", text.c_str());
        };

    void packZipData(File& f, int size, PackFormat inFormat,
                     PackFormat outFormat, uint8_t* sha, ZipEntry& target);

    UniQueue<FileTarget> fileNames;
    int strLen = 0;

    KeyStore keyStore;
};
