#pragma once

#include <exception>
#include <string>

class ZipStream;

class funzip_exception : public std::exception
{
public:
    funzip_exception(const std::exception& e) : msg(e.what()) {}
    funzip_exception(const char* ptr = "Unzip exception") : msg(ptr) {}
    virtual const char* what() const throw() { return msg; }

private:
    const char* msg;
};

class FUnzip
{
public:
    void exec();
    void smartDestDir(ZipStream& zs);
    // private:
    std::string zipName;
    int threadCount = 8;
    bool listFiles = false;
    bool verbose = false;
    std::string destinationDir;
};
