#include "funzip.h"
#include "zipformat.h"
#include "zipstream.h"
#include "inflate.h"
#include "utils.h"

#include <thread>
#include <mutex>


#include <atomic>
#include <cstring>
#include <cassert>
#include <string>
#include <cstdio>
#include <vector>

#include <experimental/filesystem>
#ifndef S_IFLNK
static constexpr int S_IFLNK = 0120000;
#endif

namespace fs = std::experimental::filesystem;

static int64_t copyfile(FILE *fout, int64_t size, FILE *fin)
{
	uint8_t buf[65536*4];
	while(size > 0)
	{
		int rc = fread(buf, 1, size < (int)sizeof(buf) ? size : sizeof(buf), fin);
		if(rc <= 0) return rc;
		size -= rc;
		fwrite(buf, 1, rc, fout);
	}
	return 0;
}

static int64_t uncompress(File& fout, int64_t inSize, File& fin)
{
    int64_t total = 0;
	uint8_t buf[65536];
	const int bufSize = sizeof(buf);

	mz_stream stream;
	memset(&stream, 0, sizeof(stream));
	mz_inflateInit2(&stream, -MZ_DEFAULT_WINDOW_BITS);

	auto data = std::make_unique<uint8_t[]>(inSize > 0 ? inSize : bufSize);

	if(inSize > 0)
	{
		fin.Read(&data[0], inSize);
		stream.next_in = &data[0];
		stream.avail_in = inSize;
	}
	int rc = MZ_OK;
	while (rc == MZ_OK)
	{
		if(inSize == 0 && stream.avail_in == 0)
		{
			stream.next_in = &data[0];
			stream.avail_in = fin.Read(&data[0], bufSize);
			if(stream.avail_in == 0)
				break;
		}
		stream.next_out = buf;
		stream.avail_out = bufSize;

		rc = mz_inflate(&stream, MZ_SYNC_FLUSH);
		// Did we unpack anything?
		if(stream.avail_out == bufSize)
			return -1;
		fout.Write(buf, bufSize - stream.avail_out);
		total += (bufSize - stream.avail_out);
	}
	if(rc < 0)
		throw funzip_exception("Inflate failed");
	
	mz_inflate(&stream, MZ_FINISH);

    return total;
}

void FUnzip::smartDestDir(ZipStream &zs)
{
	if(zs.size() == 1)
		return;

	destinationDir = path_basename(zipName);

	auto n = zs.getEntry(0).name;

	auto pos = n.find('/');
	if(pos == std::string::npos)
		return;
	std::string first = n.substr(0, pos);

	for(int i = 0; i< zs.size(); i++)
	{
		const auto& e = zs.getEntry(i);
		if(!startsWith(e.name, first))
			return;
	}
	destinationDir = "";
}

static inline void setMeta(const std::string &name, uint16_t flags, uint32_t datetime, int uid, int gid, bool link = false)
{
	auto ft = msdosToFileTime(datetime);
	fs::last_write_time(name, ft);

#if 0
	auto timestamp = msdosToUnixTime(datetime);
	struct timeval t[2] = {{0,0}};
	t[0].tv_sec = t[1].tv_sec = timestamp;
	
	fs::file_time ft;
	fs::last_write_time(name, );

	if(link)
	{
		if(flags)
			lchmod(name.c_str(), flags);
		lutimes(name.c_str(), t);
		lchown(name.c_str(), uid, gid);
	}
	else
	{
		if(flags)
			chmod(name.c_str(), flags);
		chown(name.c_str(), uid, gid);
		utimes(name.c_str(), t);
	}
#endif
}
static void readExtra(File& f, int exLen, int* uid, int* gid, int64_t *compSize = nullptr, int64_t *uncompSize = nullptr)
{
	Extra extra;
	while(exLen > 0)
	{
		f.Read((uint8_t*)&extra, 4);
		f.Read(extra.data, extra.size);
		exLen -= (extra.size+4);
		//printf("EXTRA %x\n", extra.id);
		if(extra.id == 0x7875)
		{

			*uid = extra.unix2.UID;
			*gid = extra.unix2.GID;
		}
		else if(extra.id == 0x01)
		{
			if(compSize)
				*compSize = extra.zip64.compSize;
			if(uncompSize)
				*uncompSize = extra.zip64.uncompSize;
		}
		else if(extra.id == 0xd)
		{
			std::string link((char*)extra.unix.var, extra.size - 12);
			//printf("LINK:%s\n", link.c_str());
		}
	}
}

void FUnzip::exec() 
{
	std::atomic<int> entryNum(0);
	ZipStream zs{ zipName };
	if (!zs.valid())
		throw funzip_exception("Not a zip file");

	if(zs.size() == 0)
		return;

	if(listFiles)
	{
		for(int i=0; i<zs.size(); i++)
		{
			auto &e = zs.getEntry(i);
			printf("%s\n", e.name.c_str());
		}
		return;
	}

	if(destinationDir == "")
		smartDestDir(zs);
	if(destinationDir != "" && destinationDir[destinationDir.size()-1] != '/')
		destinationDir += "/";

	std::vector<int> links;
	std::vector<int> dirs;
	std::mutex lm;

    std::vector<std::thread> workerThreads(threadCount);

    for (auto &t : workerThreads)
    {
		//FILE *fp = zs.copyFP();
        t = std::thread([&zs, &entryNum, &lm, &links, &dirs, f=zs.dupFile(), verbose=verbose, destDir=destinationDir]() mutable
        {
			LocalEntry le;
            while (true)
            {
				int en = entryNum++;
				if(en >= zs.size())
					break;
				auto &e = zs.getEntry(en);
				if((e.flags & S_IFLNK) == S_IFLNK)
				{
					std::lock_guard<std::mutex> lock(lm);
					links.push_back(en);
					continue;
				}
				if((e.flags & S_IFDIR) == S_IFDIR || e.name[e.name.length()-1] == '/')
				{
					std::lock_guard<std::mutex> lock(lm);
					dirs.push_back(en);
					continue;
				}

				f.seek(e.offset);
                auto le = f.Read<LocalEntry>();

				f.seek(le.nameLen, SEEK_CUR);
				int gid = -1;
				int uid = -1;
				int64_t uncompSize = le.uncompSize;
				int64_t compSize = le.compSize;
				// Read extra fields
				readExtra(f, le.exLen, &uid, &gid, &compSize, &uncompSize);
				auto name = destDir + e.name;
				auto dname = path_directory(name);
				if(dname != "" && !fileExists(dname))
					makedirs(dname);

				if(verbose)
					printf("%s\n", name.c_str());
				//printf("%s %x %s %s\n", fileName, a, (a & S_IFDIR)  == S_IFDIR ? "DIR" : "", (a & S_IFLNK) == S_IFLNK ? "LINK" : ""); 

				//FILE* fpout = fopen(name.c_str(), "wb");
                auto fout = File{name, File::Mode::WRITE};
				if(!fout.canWrite())
				{
					//char errstr[128];
					//strerror_r(errno, errstr, sizeof(errstr));
					//fprintf(stderr, "**Warning: Could not write '%s' (%s)\n", name.c_str(), errstr);
					continue;
				}
				if(le.method == 0)
					copyfile(fout.filePointer(), uncompSize, f.filePointer());
				else
					uncompress(fout, compSize, f);
				setMeta(name, e.flags, le.dateTime, uid, gid);
			}
		});
	}
    for (auto &t : workerThreads)
		t.join();

	//LocalEntry le;
	char linkName[65536];
	int uid, gid;
	//FILE *fp = zs.copyFP();
    auto f = zs.dupFile();
	for(int i : links)
	{
		auto &e = zs.getEntry(i);
		f.seek(e.offset);
        auto le = f.Read<LocalEntry>();

		f.seek(le.nameLen, SEEK_CUR);
		uid = gid = -1;
		int64_t uncompSize = le.uncompSize;
		readExtra(f, le.exLen, &uid, &gid, nullptr, &uncompSize);
		f.Read(linkName, uncompSize);
		linkName[uncompSize] = 0;
		auto name = destinationDir + e.name;
		auto dname = path_directory(name);
		auto fname = path_filename(e.name);
		//int fd = open(dname.c_str(), 0);
		if(verbose)
			printf("Link %s/%s -> %s\n", dname.c_str(), fname.c_str(), linkName);
        fs::create_symlink(linkName, fname);
		//symlinkat(linkName, fd, fname.c_str());
		//close(fd);
		setMeta(name, e.flags, le.dateTime, uid, gid, true);
	}
	for(int i : dirs)
	{
		auto &e = zs.getEntry(i);
		f.seek(e.offset);
        auto le = f.Read<LocalEntry>();

		f.seek(le.nameLen, SEEK_CUR);
		uid = gid = -1;
		readExtra(f, le.exLen, &uid, &gid);
		auto name = destinationDir + e.name;
		auto l = name.length();
		if(name[l-1] == '/')
			name = name.substr(0, l-1);
		setMeta(name, e.flags, le.dateTime, uid, gid);
	}

	if(zs.comment)
		puts(zs.comment);
}
