#include "catch.hpp"

#include <cstdlib>
#include <string>

#include "fastzip.h"
#include "funzip.h"
#include "utils.h"

#include "file.h"

#ifdef _WIN32
#    include <io.h>
#endif

enum
{
    EMPTY = 1
};

std::string makeTemp(const std::string& prefix)
{
    std::string result(prefix.length() + 8, ' ');
    for (unsigned i = 0; i < result.length(); i++) {
        result[i] = i < prefix.length() ? prefix[i] : ('a' + (rand() % 26));
    }
    return result;
}

std::vector<std::string> createFiles(const std::string& templ, int count,
                                     int maxSize, int minSize = 1,
                                     int flags = 0)
{
    makedirs(path_directory(templ));
    std::vector<std::string> files(count);
    for (auto& f : files) {
        auto name = makeTemp(templ);
        auto file = File{name};
        int sz = (rand() % (maxSize - minSize)) + minSize;
        auto data = std::make_unique<uint8_t[]>(sz);
        if ((flags & EMPTY) == 0) {
            for (int i = 0; i < sz; i++)
                data[i] = rand() % 0x100;
        }
        file.Write(data.get(), sz);
    }
    return files;
}

bool compareFile(const std::string& a, const std::string& b)
{
    std::array<uint8_t, 65536> temp0;
    std::array<uint8_t, 65536> temp1;
    File fp0 = File{a};
    File fp1 = File{b};
    while (true) {
        int rc0 = fp0.Read(temp0);
        int rc1 = fp1.Read(temp1);
        if (rc0 != rc1)
            return false;
        if (memcmp(&temp0[0], &temp1[0], rc0) != 0)
            return false;
        if (rc0 <= 0)
            break;
    }
    return true;
}

bool compareDir(const std::string& a, const std::string& b)
{
    bool ok = true;
    listFiles(a, [&](const std::string& fileName) {
        auto base = fileName.substr(a.size());
        auto other = b + base;
        if (fileExists(other)) {
            if (!compareFile(fileName, other)) {
                printf("%s and %s differ\n", fileName.c_str(), other.c_str());

                ok = false;
            }

        } else {
            printf("%s does not exist\n", other.c_str());
            ok = false;
        }
    });
    return ok;
}

enum
{
    FORCE64 = 1,
    SEQ = 2,
    SIGN = 4
};

void zipUnzip(const std::string& dirName, const std::string& zipName,
              const std::string& outDir, int flags = 0)
{
    Fastzip fs;
    FUnzip fu;
    fs.junkPaths = true;
    if (flags & FORCE64)
        fs.force64 = true;
    if (flags & SEQ)
        fs.doSeq = true;
    if (flags & SIGN) {
        fs.keyPassword = "fastzip";
        fs.doSign = true;
    }

    fs.addDir(dirName, PackFormat::ZIP5_COMPRESSED);
    fs.zipfile = zipName;
    fs.exec();

    fu.zipName = zipName;
    fu.destinationDir = outDir;
    fu.exec();
}

TEST_CASE("file", "")
{
    File f{"README.md"};
    while (!f.atEnd()) {
        auto line = f.readLine();
        puts(line.c_str());
    }

    f.seek(0);
    for (const auto& line : f.lines()) {
        puts(line.c_str());
    }
}

TEST_CASE("basic", "")
{
    removeFiles("temp/out");
    // removeFiles("temp/test.zip");
    if (!fileExists("temp/zipme"))
        createFiles("temp/zipme/f", 10, 128 * 1024);

    SECTION("Create normal zip")
    {
        zipUnzip("temp/zipme", "temp/test.zip", "temp/out");
        REQUIRE(compareDir("temp/zipme", "temp/out/zipme") == true);
    }
    SECTION("Create zip with zip64 extension")
    {
        zipUnzip("temp/zipme", "temp/test.zip", "temp/out", FORCE64);
        REQUIRE(compareDir("temp/zipme", "temp/out/zipme") == true);
    }

    SECTION("Create signed zip")
    {
        zipUnzip("temp/zipme", "temp/test.zip", "temp/out", SIGN);
        REQUIRE(compareDir("temp/zipme", "temp/out/zipme") == true);
    }
    // TODO: Seq, Sign, Intel, Uncompressed, include zip
    //
    // BIG TEST
}
#if 0
TEST_CASE("big", "")
{
	removeFiles("temp/outbig");
	removeFiles("temp/test_big.zip");
	zipUnzip("zeros.img", "temp/test_big.zip", "temp/out");
	//REQUIRE(compareDir("zero", "temp/out/zipalot") == true);
}
TEST_CASE("64k", "")
{
	removeFiles("temp/out");
	removeFiles("temp/test_many.zip");
	if(!fileExists("temp/zipalot"))
		createFiles("temp/zipalot/f", 76543, 8*1024, 1, EMPTY);
	zipUnzip("temp/zipalot", "temp/test_many.zip", "temp/out");
	REQUIRE(compareDir("temp/zipalot", "temp/out/zipalot") == true);

	// TODO: Seq, Sign, Intel, Uncompressed, include zip
	// smart dest dir, links, directories, permissions
	// BIG TEST

}
#endif
