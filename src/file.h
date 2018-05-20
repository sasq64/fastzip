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

	File(const std::string &name, const Mode mode = NONE) : name(name), mode(mode)
    {
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

    size_t Read(uint8_t* target, size_t bytes) noexcept
    {
        return fread(target, 1, bytes, fp);
    }

    void seek(uint64_t pos)
    {
        if(mode == NONE) open(READ);
#ifdef _WIN32
        _fseeki64(fp, pos, SEEK_SET);
#else
        fseek(fp, pos, SEEK_SET);
#endif
    }

    void seekForward(uint64_t pos)
    {
        if(mode == NONE) open(READ);
#ifdef _WIN32
        _fseeki64(fp, pos, SEEK_CUR);
#else
        fseek(fp, pos, SEEK_CUR);
#endif
    }

    uint64_t tell()
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

    bool canRead()
    {
        open(READ);
        return fp != nullptr;
    }

    void open(Mode mode)
    {
        if(this->mode == mode) return;
        if(this->mode != NONE) throw io_exception();
        fp = fopen(name.c_str(), mode == READ ? "rb" : "wb");
        printf("open %s %d got %p\n", name.c_str(), mode, fp);
        this->mode = mode;
    }

    void close()
    {
        if(fp) fclose(fp);
        fp = nullptr;
        mode = NONE;
    }
    FILE* filePointer()
    {
        return fp;
    }
private:
    std::string name;
    Mode mode;
    FILE* fp = nullptr;

};
