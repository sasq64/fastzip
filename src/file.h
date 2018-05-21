#pragma once

#include <string>
#include <stdexcept>

class io_exception : public std::exception {
public:
	io_exception(const std::string &m = "IO Exception") : msg(m) {}
	virtual const char *what() const throw() { return msg.c_str(); }
private:
	std::string msg;
};

class file_not_found_exception : public std::exception {
public:
	file_not_found_exception(const std::string &fileName = "") : msg(std::string("File not found: ") + fileName) {}
	virtual const char *what() const throw() { return msg.c_str(); }
private:
	std::string msg;
};

class File {
public:

	enum Mode {
		NONE = 0,
		READ = 1,
		WRITE = 2
	};

	File() : mode(NONE)
    {
    }

	File(const std::string &name, const Mode mode = NONE) : name(name)
    {
        if(mode != NONE)
            open(mode);
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

    template <typename T>
    T Read()
    {
        open(READ);
        T t;
        if(fread(&t, 1, sizeof(T), fp) != sizeof(T))
            throw io_exception("Could not read object");
        return t;
    }

    template <typename T>
    size_t Read(T* target, size_t bytes) noexcept
    {
        open(READ);
        return fread(target, 1, bytes, fp);
    }

    template <typename T>
    void Write(const T& t)
    {
        open(WRITE);
        if(fwrite(&t, 1, sizeof(T), fp) != sizeof(T))
            throw io_exception("Could not write object");
    }

    template <typename T>
    size_t Write(const T* target, size_t bytes) noexcept
    {
        open(WRITE);
        return fwrite(target, 1, bytes, fp);
    }

    enum Seek {
        Set = SEEK_SET,
        Cur = SEEK_CUR,
        End = SEEK_END
    };


    void seek(int64_t pos, int whence = Seek::Set)
    {
        if(mode == NONE) open(READ);
#ifdef _WIN32
        _fseeki64(fp, pos, whence);
#else
        fseek(fp, pos, whence);
#endif
    }

    size_t tell() const
    {
#ifdef _WIN32
        return _ftelli64(fp);
#else
        return ftell(fp);
#endif
    }

	virtual ~File() {
        close();
    }

    bool isOpen() const
    {
        return mode != NONE;
    }

    bool canRead()
    {
        open(READ);
        return fp != nullptr;
    }

    bool canWrite()
    {
        open(WRITE);
        return fp != nullptr;
    }

    void open(Mode mode)
    {
        if(this->mode == mode) return;
        if(this->mode != NONE) throw io_exception();
        fp = fopen(name.c_str(), mode == READ ? "rb" : "wb");
        //printf("open %s %d got %p\n", name.c_str(), mode, fp);
        this->mode = mode;
    }

    void close()
    {
        if(fp) fclose(fp);
        fp = nullptr;
        mode = NONE;
    }

    File dup() {
        File f { name, mode };
        if(mode != NONE)
            f.seek(tell());
        return f;
    }

    FILE* filePointer()
    {
        if(mode == NONE) open(READ);
        return fp;
    }
private:
    std::string name;
    Mode mode = NONE;
    FILE* fp = nullptr;

};
