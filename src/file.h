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

    File() : mode_(NONE) {}

    File(FILE* fp, Mode mode) : mode_(mode), fp_(fp) {}

    explicit File(std::string name, const Mode mode = NONE) : name_(std::move(name))
    {
        if (mode != NONE) openAndThrow(mode);
    }

    File(const File&) = delete;

    File& operator=(const File&) = delete;

    File(File&& other) noexcept
    {
        fp_ = other.fp_;
        name_ = other.name_;
        mode_ = other.mode_;
        other.fp_ = nullptr;
        other.mode_ = NONE;
    }

    // Destructor

    virtual ~File() { close(); }

    // Reading

    template <typename T> T Read()
    {
        openAndThrow(READ);
        T t;
        if (fread(&t, 1, sizeof(T), fp_) != sizeof(T))
            throw io_exception("Could not read object");
        return t;
    }

    template <typename T> size_t Read(T* target, size_t bytes)
    {
        openAndThrow(READ);
        return fread(target, 1, bytes, fp_);
    }

    template <typename T, size_t N> size_t Read(std::array<T, N>& target)
    {
        return Read(&target[0], target.size() * sizeof(T));
    }

    std::string readLine()
    {
        std::array<char, 10> lineTarget;
        openAndThrow(READ);
        char* ptr = fgets(&lineTarget[0], lineTarget.size(), fp_);
        if (!ptr) {
            if (feof(fp_)) return "";
            throw io_exception();
        }
        auto len = std::strlen(ptr);
        std::string result;
        while (ptr[len - 1] != '\n') {
            result += std::string{&lineTarget[0], len};
            ptr = fgets(&lineTarget[0], lineTarget.size(), fp_);
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
        if (fwrite(&t, 1, sizeof(T), fp_) != sizeof(T))
            throw io_exception("Could not write object");
    }

    template <typename T> size_t Write(const T* target, size_t bytes)
    {
        openAndThrow(WRITE);
        return fwrite(target, 1, bytes, fp_);
    }

    bool atEnd()
    {
        if (mode_ == NONE) openAndThrow(READ);
        return feof(fp_);
    }

    void seek(int64_t pos, int whence = Seek::Set)
    {
        if (mode_ == NONE) openAndThrow(READ);
#ifdef _WIN32
        _fseeki64(fp_, pos, whence);
#else
        fseek(fp_, pos, whence);
#endif
    }

    size_t tell()
    {
        if (mode_ == NONE) openAndThrow(READ);
#ifdef _WIN32
        return _ftelli64(fp_);
#else
        return ftell(fp_);
#endif
    }

    bool isOpen() const noexcept { return mode_ != NONE; }

    bool canRead() const noexcept
    {
        if (mode_ != NONE) return fp_ != nullptr;
        FILE* tmp_fp = fopen(name_.c_str(), "rb");
        fclose(tmp_fp);
        return tmp_fp != nullptr;
    }

    bool canWrite() const noexcept
    {
        if (mode_ == WRITE) return fp_ != nullptr;
        FILE* tmp_fp = fopen(name_.c_str(), "wb");
        fclose(tmp_fp);
        return tmp_fp != nullptr;
    }

    OpenResult open(Mode mode) noexcept
    {
        if (this->mode_ == mode) return OpenResult::OK;
        if (this->mode_ != NONE) return OpenResult::ALREADY_OPEN;
        fp_ = fopen(name_.c_str(), mode == READ ? "rb" : "wb");
        if (!fp_) return OpenResult::FILE_NOT_FOUND;
        this->mode_ = mode;
        return OpenResult::OK;
    }

    void openAndThrow(Mode mode)
    {
        auto result = open(mode);
        switch (result) {
        case OpenResult::OK: return;
        case OpenResult::FILE_NOT_FOUND: throw file_not_found_exception(name_);
        case OpenResult::ALREADY_OPEN:
            throw io_exception("Can not open file in different mode");
        }
    }

    void close() noexcept
    {
        if (fp_) fclose(fp_);
        fp_ = nullptr;
        mode_ = NONE;
    }

    File dup() const
    {
        if (name_.empty()) {
            auto* fp2 =
                fdopen(::dup(fileno(fp_)), mode_ == Mode::READ ? "rb" : "wb");
            return File{fp2, mode_};
        }
        File f{name_, mode_};
        if (mode_ != NONE) f.seek(const_cast<File*>(this)->tell());
        return f;
    }

    FILE* filePointer()
    {
        if (mode_ == NONE) openAndThrow(READ);
        return fp_;
    }

private:
    std::string name_;
    // Invariant; if mode != NONE, fp must point to a valid FILE*
    Mode mode_ = NONE;
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
            if (offset >= 0) f.seek(offset);
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
