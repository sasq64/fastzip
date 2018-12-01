#pragma once

#include "asn.h"

#include <cstdint>
#include <exception>
#include <string>
#include <unordered_map>
#include <vector>

#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;

class key_exception : public std::exception
{
public:
    key_exception(const char* ptr = "Key Exception") : msg(ptr) {}
    virtual const char* what() const throw() { return msg; }

private:
    const char* msg;
};

std::string base64_encode(unsigned char const* bytes_to_encode,
                          unsigned int in_len);

std::string toPem(std::vector<uint8_t> key);
std::vector<uint8_t> recoverKey(const std::string& password,
                                const std::vector<uint8_t>& protectedKey);

class KeyStore
{
public:
    KeyStore() {}
    KeyStore(const fs::path& fileName, const std::string& pass = "");

    bool load(const fs::path& fileName, const std::string& pass = "");
    bool load(const std::vector<uint8_t>& data, const std::string& pass = "");

    std::vector<uint8_t> getKey(const std::string& pass,
                                const std::string& name = "");
    std::vector<uint8_t> getCert() { return certificate; }

    std::vector<uint8_t> getKey()
    {
        return getKey(currentKeyPass, currentKeyName);
    }

    void setCurrentKey(const std::string& name, const std::string& pass)
    {
        currentKeyName = name;
        currentKeyPass = pass;
    }

private:
    bool load(asn1::MemBuffer& buf, const std::string& pass);

    asn1::MemBuffer membuf;

    std::vector<uint8_t> certificate;

    std::string currentKeyName;
    std::string currentKeyPass;
};
