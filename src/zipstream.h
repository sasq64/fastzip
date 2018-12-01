#pragma once

#include "file.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

// Simple zip file access
class ZipStream
{
public:
    struct Entry
    {
        Entry(const std::string& aName, int64_t aOffset, uint16_t aFlags)
            : name(aName), offset(aOffset), flags(aFlags), data(nullptr)
        {}
        std::string name;
        int64_t offset;
        uint16_t flags;
        void* data;
    };

    std::vector<Entry> entries_;
    std::unique_ptr<char[]> comment_;

    char* comment() { return comment_ ? comment_.get() : nullptr; }

    auto cbegin() const { return entries_.cbegin(); }
    auto cend() const { return entries_.cend(); }
    auto begin() { return entries_.begin(); }
    auto end() { return entries_.end(); }

    ZipStream(const std::string& zipName);
    bool valid() const { return f_.isOpen(); }

    auto size() const { return entries_.size(); }
    Entry& getEntry(int i) { return entries_[i]; }
    const Entry& getEntry(int i) const { return entries_[i]; }

    File dupFile() const { return File(zipName_, File::Mode::READ); }

private:
    std::string zipName_;
    File f_;
};
