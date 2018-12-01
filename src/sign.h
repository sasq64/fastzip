#pragma once

#include <string>

class ZipArchive;
class KeyStore;

class sign_exception : public std::exception
{
public:
    sign_exception(const std::exception& e) : msg(e.what()) {}
    sign_exception(const char* ptr = "Sign Exception") : msg(ptr) {}
    virtual const char* what() const throw() { return msg; }

private:
    const char* msg;
};

void sign(ZipArchive& zipArchive, KeyStore& keyStore,
          const std::string& digestFile);
