#include "catch.hpp"

#include <cstdlib>
#include <string>

#include "fastzip.h"
#include "funzip.h"
#include "utils.h"

#ifdef _WIN32
#    include <io.h>
#endif

enum
{
    EMPTY = 1
};

std::vector<std::string> createFiles(const std::string& templ, int count, int maxSize,
                                     int minSize = 1, int flags = 0)
{
    makedirs(path_directory(templ));
    auto tx = templ + "XXXXXXXX";
    char t[1024];
    std::vector<std::string> files(count);
    for (auto& f : files) {
        strcpy(t, tx.c_str());
        char* name = mktemp(t);
        FILE* fp = fopen(name, "wb");
        int sz = (rand() % (maxSize - minSize)) + minSize;
        uint8_t* data = new uint8_t[sz];
        if ((flags & EMPTY) == 0) {
            for (int i = 0; i < sz; i++)
                data[i] = rand() % 0x100;
        }
        fwrite(data, 1, sz, fp);
        fclose(fp);
        delete[] data;
    }
    return files;
}

bool compareFile(const std::string& a, const std::string& b)
{
    uint8_t temp0[65536];
    uint8_t temp1[65536];
    FILE* fp0 = fopen(a.c_str(), "rb");
    FILE* fp1 = fopen(b.c_str(), "rb");
    while (true) {
        int rc0 = fread(temp0, 1, sizeof(temp0), fp0);
        int rc1 = fread(temp1, 1, sizeof(temp1), fp1);
        if (rc0 != rc1) return false;
        if (memcmp(temp0, temp1, rc0) != 0) return false;
        if (rc0 <= 0) break;
    }
    fclose(fp0);
    fclose(fp1);
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
    if (flags & FORCE64) fs.force64 = true;
    if (flags & SEQ) fs.doSeq = true;
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

TEST_CASE("basic", "")
{
    removeFiles("temp/out");
    // removeFiles("temp/test.zip");
    if (!fileExists("temp/zipme")) createFiles("temp/zipme/f", 10, 128 * 1024);

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
