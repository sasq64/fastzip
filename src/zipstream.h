#pragma once

#include "file.h"

#include <cstdio>
#include <string>
#include <vector>

class ZipStream
{
public:
    struct Entry
    {
        Entry(const std::string& name, int64_t offset, uint16_t flags)
            : name(name), offset(offset), flags(flags), data(nullptr)
        {}
        std::string name;
        int64_t offset;
        uint16_t flags;
        void* data;
    };

    std::vector<Entry> entries;
    char* comment = nullptr;

    ZipStream(const std::string& zipName);
    ~ZipStream();
    bool valid() const { return f.isOpen(); }

    int size() const { return entries.size(); }
    Entry& getEntry(int i) { return entries[i]; }
    const Entry& getEntry(int i) const { return entries[i]; }

    File dupFile() const { return File(zipName, File::Mode::READ); }

private:
    std::string zipName;
    File f;
};
