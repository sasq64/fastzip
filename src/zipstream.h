#pragma once

#include <cstdio>
#include <string>
#include <vector>

class ZipStream
{
public:
    struct Entry
    {
        Entry(const std::string &name, int64_t offset, uint16_t flags) : name(name), offset(offset), flags(flags), data(nullptr) {}
        std::string name;
        int64_t offset;
		uint16_t flags;
		void *data;
    };

    std::vector<Entry> entries;
	char* comment = nullptr;

    ZipStream(const std::string &zipName);
	~ZipStream();
	bool valid() const { return fp != nullptr; }

    int size() const { return entries.size(); }
    Entry& getEntry(int i) { return entries[i]; }
    const Entry& getEntry(int i) const { return entries[i]; }

    template<typename T> T read()
    {
        T t;
        fread(&t, 1, sizeof(T), fp);
        return t;
    }

	FILE *copyFP() const {
		return fopen(zipName.c_str(), "rb");
		//return fdopen(dup(fileno(fp)), "r");
	}

private:
	std::string zipName;
    FILE *fp;
};
