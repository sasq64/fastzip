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
    io_exception(const std::string& m = "IO Exception") : msg(m) {}
    virtual const char* what() const throw() { return msg.c_str(); }

private:
    std::string msg;
};

class file_not_found_exception : public std::exception
{
public:
    file_not_found_exception(const std::string& fileName = "")
        : msg(std::string("File not found: ") + fileName)
    {}
    virtual const char* what() const throw() { return msg.c_str(); }

private:
    std::string msg;
};

class File
{
public:
    enum Mode
    {
        NONE = 0,
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
        static File _stdin{stdin, Mode::READ};
        return _stdin;
    }

    // Constructors

    File() : mode(NONE) {}

    File(FILE* fp, Mode mode) : mode(mode), fp(fp) {}

    File(const std::string& name, const Mode mode = NONE) : name(name)
    {
        if (mode != NONE) openAndThrow(mode);
    }

    File(const File&) = delete;

    File& operator=(const File&) = delete;

    File(File&& other)
    {
        fp = other.fp;
        name = other.name;
        mode = other.mode;
        other.fp = nullptr;
        other.mode = NONE;
    }

    // Destructor

    virtual ~File() { close(); }

    // Reading

    template <typename T> T Read()
    {
        openAndThrow(READ);
        T t;
        if (fread(&t, 1, sizeof(T), fp) != sizeof(T))
            throw io_exception("Could not read object");
        return t;
    }

    template <typename T> size_t Read(T* target, size_t bytes)
    {
        openAndThrow(READ);
        return fread(target, 1, bytes, fp);
    }

    template <typename T, size_t N> size_t Read(std::array<T, N>& target)
    {
        return Read(&target[0], target.size() * sizeof(T));
    }

    std::string readLine()
    {
        std::array<char, 10> lineTarget;
        openAndThrow(READ);
        char* ptr = fgets(&lineTarget[0], lineTarget.size(), fp);
        if (!ptr) {
            if (feof(fp)) return "";
            throw io_exception();
        }
        auto len = std::strlen(ptr);
        std::string result;
        while (ptr[len - 1] != '\n') {
            result += std::string{&lineTarget[0], len};
            ptr = fgets(&lineTarget[0], lineTarget.size(), fp);
            if (!ptr) throw io_exception();
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
        openAndThrow(WRITE);
        if (fwrite(&t, 1, sizeof(T), fp) != sizeof(T))
            throw io_exception("Could not write object");
    }

    template <typename T> size_t Write(const T* target, size_t bytes)
    {
        openAndThrow(WRITE);
        return fwrite(target, 1, bytes, fp);
    }

    bool atEnd()
    {
        if (mode == NONE) openAndThrow(READ);
        return feof(fp);
    }

    void seek(int64_t pos, int whence = Seek::Set)
    {
        if (mode == NONE) openAndThrow(READ);
#ifdef _WIN32
        _fseeki64(fp, pos, whence);
#else
        fseek(fp, pos, whence);
#endif
    }

    size_t tell()
    {
        if (mode == NONE) openAndThrow(READ);
#ifdef _WIN32
        return _ftelli64(fp);
#else
        return ftell(fp);
#endif
    }

    bool isOpen() const noexcept { return mode != NONE; }

    bool canRead() const noexcept
    {
        if (mode != NONE) return fp != nullptr;
        FILE* tmp_fp = fopen(name.c_str(), "rb");
        fclose(tmp_fp);
        return tmp_fp != nullptr;
    }

    bool canWrite() const noexcept
    {
        if (mode == WRITE) return fp != nullptr;
        FILE* tmp_fp = fopen(name.c_str(), "wb");
        fclose(tmp_fp);
        return tmp_fp != nullptr;
    }

    OpenResult open(Mode mode) noexcept
    {
        if (this->mode == mode) return OpenResult::OK;
        if (this->mode != NONE) return OpenResult::ALREADY_OPEN;
        fp = fopen(name.c_str(), mode == READ ? "rb" : "wb");
        if (!fp) return OpenResult::FILE_NOT_FOUND;
        this->mode = mode;
        return OpenResult::OK;
    }

    void openAndThrow(Mode mode)
    {
        auto result = open(mode);
        switch (result) {
        case OpenResult::OK: return;
        case OpenResult::FILE_NOT_FOUND: throw file_not_found_exception(name);
        case OpenResult::ALREADY_OPEN:
            throw io_exception("Can not open file in different mode");
        }
    }

    void close() noexcept
    {
        if (fp) fclose(fp);
        fp = nullptr;
        mode = NONE;
    }

    File dup() const
    {
        if (name == "") {
            auto* fp2 =
                fdopen(::dup(fileno(fp)), mode == Mode::READ ? "rb" : "wb");
            return File{fp2, mode};
        }
        File f{name, mode};
        if (mode != NONE) f.seek(const_cast<File*>(this)->tell());
        return f;
    }

    FILE* filePointer()
    {
        if (mode == NONE) openAndThrow(READ);
        return fp;
    }

private:
    std::string name;
    // Invariant; if mode != NONE, fp must point to a valid FILE*
    Mode mode = NONE;
    FILE* fp = nullptr;
};

template <bool REFERENCE> class LineReader
{
    friend File;

    LineReader(File& af) : f(af) {}
    LineReader(File&& af) : f(std::move(af)) {}

    typename std::conditional<REFERENCE, File&, File>::type f;
    std::string line;

    struct iterator
    {
        iterator(File& f, ssize_t offset) : f(f), offset(offset)
        {
            if (offset >= 0) f.seek(offset);
            line = f.readLine();
        }

        File& f;
        ssize_t offset;
        std::string line;

        bool operator!=(const iterator& other) const
        {
            return offset != other.offset;
        }

        std::string operator*() const { return line; }

        iterator& operator++()
        {
            if (f.atEnd())
                offset = -1;
            else {
                offset = f.tell();
                line = f.readLine();
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
