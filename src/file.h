#pragma once

#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#    include <io.h>
#else
#    include <unistd.h>
#endif

class io_exception : public std::exception
{
public:
    explicit io_exception(std::string m = "IO Exception") : msg(std::move(m)) {}
    const char* what() const noexcept override { return msg.c_str(); }

private:
    std::string msg;
};

class file_not_found_exception : public std::exception
{
public:
    explicit file_not_found_exception(const std::string& fileName = "")
        : msg(std::string("File not found: ") + fileName)
    {}
    const char* what() const noexcept override { return msg.c_str(); }

private:
    std::string msg;
};

#define IO_ERROR(x)                                                            \
    {}

class File
{
public:
    enum Mode
    {
        READ = 1,
        WRITE = 2
    };

    enum class OpenResult
    {
        OK,
        FILE_NOT_FOUND,
        ALREADY_OPEN
    };

    enum Seek
    {
        Set = SEEK_SET,
        Cur = SEEK_CUR,
        End = SEEK_END
    };

    static File& getStdIn()
    {
        static File _stdin{stdin};
        return _stdin;
    }

    // Constructors

    File() = default;

    File(FILE* fp) : fp_(fp) {}

    explicit File(const char* name, const Mode mode = READ)
    {
        openAndThrow(name, mode);
    }

    explicit File(const std::string& name, const Mode mode = READ)
    {
        openAndThrow(name.c_str(), mode);
    }

    bool isOpen() const { return fp_ != nullptr; }

    bool canWrite() { return true; }
    bool canRead() { return true; }

    File(const File&) = delete;

    File& operator=(const File&) = delete;

    File(File&& other) noexcept
    {
        fp_ = other.fp_;
        other.fp_ = nullptr;
    }

    // Destructor

    virtual ~File() { close(); }

    // Reading

    template <typename T> T Read()
    {
        T t;
        if (fread(&t, 1, sizeof(T), fp_) != sizeof(T))
            IO_ERROR("Could not read object");
        return t;
    }

    template <typename T> size_t Read(T* target, size_t bytes)
    {
        return fread(target, 1, bytes, fp_);
    }

    template <typename T, size_t N> size_t Read(std::array<T, N>& target)
    {
        return Read(&target[0], target.size() * sizeof(T));
    }

    std::string readLine()
    {
        std::array<char, 10> lineTarget;
        char* ptr = fgets(&lineTarget[0], lineTarget.size(), fp_);
        if (!ptr) {
            if (feof(fp_))
                return "";
            IO_ERROR();
        }
        auto len = std::strlen(ptr);
        std::string result;
        while (ptr[len - 1] != '\n') {
            result += std::string{&lineTarget[0], len};
            ptr = fgets(&lineTarget[0], lineTarget.size(), fp_);
            if (!ptr)
                IO_ERROR();
            len = std::strlen(ptr);
        }
        while (len > 0 && (ptr[len - 1] == '\n' || ptr[len - 1] == '\r'))
            len--;
        result += std::string{&lineTarget[0], len};
        return result;
    }

    inline auto lines() &;
    inline auto lines() &&;

    // Writing

    template <typename T> void Write(const T& t)
    {
        if (fwrite(&t, 1, sizeof(T), fp_) != sizeof(T))
            IO_ERROR("Could not write object");
    }

    template <typename T> size_t Write(const T* target, size_t bytes)
    {
        return fwrite(target, 1, bytes, fp_);
    }

    bool atEnd() { return feof(fp_); }

    void seek(int64_t pos, int whence = Seek::Set)
    {
#ifdef _WIN32
        _fseeki64(fp_, pos, whence);
#else
        fseek(fp_, pos, whence);
#endif
    }

    size_t tell()
    {
#ifdef _WIN32
        return _ftelli64(fp_);
#else
        return ftell(fp_);
#endif
    }

    bool open(const char* name, Mode mode) noexcept
    {
        fp_ = fopen(name, mode == READ ? "rb" : "wb");
        return fp_ != nullptr;
    }

    void openAndThrow(const char* name, Mode mode)
    {
        if (!open(name, mode))
            IO_ERROR();
    }

    void close() noexcept
    {
        if (fp_)
            fclose(fp_);
        fp_ = nullptr;
    }

    FILE* filePointer() { return fp_; }

private:
    FILE* fp_ = nullptr;
};

template <bool REFERENCE> class LineReader
{
    friend File;

    explicit LineReader(File& af) : f(af) {}
    explicit LineReader(File&& af) : f(std::move(af)) {}

    typename std::conditional<REFERENCE, File&, File>::type f;
    std::string line;

    struct iterator
    {
        iterator(File& f, ssize_t offset) : f_(f), offset_(offset)
        {
            if (offset >= 0)
                f.seek(offset);
            line = f.readLine();
        }

        File& f_;
        ssize_t offset_;
        std::string line;

        bool operator!=(const iterator& other) const
        {
            return offset_ != other.offset_;
        }

        std::string operator*() const { return line; }

        iterator& operator++()
        {
            if (f_.atEnd())
                offset_ = -1;
            else {
                offset_ = f_.tell();
                line = f_.readLine();
            }
            return *this;
        }
    };

public:
    iterator begin() { return iterator(f, 0); }
    iterator end() { return iterator(f, -1); }
};

auto File::lines() &
{
    return LineReader<true>(*this);
}

auto File::lines() &&
{
    return LineReader<false>(std::move(*this));
}
