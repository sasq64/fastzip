#include "funzip.h"
#include "zipformat.h"
#include "zipstream.h"
#include "inflate.h"
#include "utils.h"

#include <thread>
#include <mutex>


#include <atomic>
#include <cstring>
#include <assert.h>
#include <fcntl.h>
#include <string>
#include <cstdio>
#include <vector>

using namespace std;

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

static int64_t uncompress(FILE *fout, int64_t inSize, FILE *fin)
{
    int64_t total = 0;
	uint8_t buf[65536];
	const int bufSize = sizeof(buf);

	mz_stream stream;
	memset(&stream, 0, sizeof(stream));
	mz_inflateInit2(&stream, -MZ_DEFAULT_WINDOW_BITS);

	uint8_t *data;

	if(inSize > 0)
	{
		data = new uint8_t [inSize];
		fread(data, 1, inSize, fin);
		stream.next_in = data;
		stream.avail_in = inSize;
	} else {
		data = new uint8_t [bufSize];
	}
	int rc = MZ_OK;
	while (rc == MZ_OK)
	{
		if(inSize == 0 && stream.avail_in == 0)
		{
			stream.next_in = data;
			stream.avail_in = fread(data, 1, bufSize, fin);
			if(stream.avail_in == 0)
				break;
		}
		stream.next_out = buf;
		stream.avail_out = bufSize;

		rc = mz_inflate(&stream, MZ_SYNC_FLUSH);
		// Did we unpack anything?
		if(stream.avail_out == bufSize)
			return -1;
		fwrite(buf, 1, bufSize - stream.avail_out, fout);
		total += (bufSize - stream.avail_out);
	}
	if(rc < 0)
		throw funzip_exception("Inflate failed");
	
	mz_inflate(&stream, MZ_FINISH);

	delete [] data;

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
	string first = n.substr(0, pos);

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
static void readExtra(FILE *fp, int exLen, int* uid, int* gid, int64_t *compSize = nullptr, int64_t *uncompSize = nullptr)
{
	Extra extra;
	while(exLen > 0)
	{
		fread(&extra, 1, 4, fp);
		fread(extra.data, 1, extra.size, fp);
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
			string link((char*)extra.unix.var, extra.size - 12);
			//printf("LINK:%s\n", link.c_str());
		}
	}
}

void FUnzip::exec() 
{
	atomic<int> entryNum(0);
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

	vector<int> links;
	vector<int> dirs;
	mutex lm;

    vector<thread> workerThreads(threadCount);

    for (auto &t : workerThreads)
    {
		FILE *fp = zs.copyFP();
		auto verbose = this->verbose;
		auto destDir = this->destinationDir;
        t = thread([&zs, &entryNum, &lm, &links, &dirs, fp, verbose, destDir]
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

				fseek_x(fp, e.offset, SEEK_SET);
				if (fread(&le, 1, sizeof(le), fp) != sizeof(le))
					throw funzip_exception("Corrupt zipfile");

				fseek_x(fp, le.nameLen, SEEK_CUR);
				int gid = -1;
				int uid = -1;
				int64_t uncompSize = le.uncompSize;
				int64_t compSize = le.compSize;
				// Read extra fields
				readExtra(fp, le.exLen, &uid, &gid, &compSize, &uncompSize);
				auto name = destDir + e.name;
				auto dname = path_directory(name);
				if(dname != "" && !fileExists(dname))
					makedirs(dname);

				if(verbose)
					printf("%s\n", name.c_str());
				//printf("%s %x %s %s\n", fileName, a, (a & S_IFDIR)  == S_IFDIR ? "DIR" : "", (a & S_IFLNK) == S_IFLNK ? "LINK" : ""); 

				FILE* fpout = fopen(name.c_str(), "wb");
				if(!fpout)
				{
					char errstr[128];
					strerror_r(errno, errstr, sizeof(errstr));
					fprintf(stderr, "**Warning: Could not write '%s' (%s)\n", name.c_str(), errstr);
					continue;
				}
				if(le.method == 0)
					copyfile(fpout, uncompSize, fp);
				else
					uncompress(fpout, compSize, fp);
				fclose(fpout);
				setMeta(name, e.flags, le.dateTime, uid, gid);
			}
			fclose(fp);
		});
	}
    for (auto &t : workerThreads)
		t.join();

	LocalEntry le;
	char linkName[65536];
	int uid, gid;
	FILE *fp = zs.copyFP();
	for(int i : links)
	{
		auto &e = zs.getEntry(i);
		fseek_x(fp, e.offset, SEEK_SET);
		if (fread(&le, 1, sizeof(le), fp) != sizeof(le))
			throw funzip_exception("Corrupt zipfile");

		fseek_x(fp, le.nameLen, SEEK_CUR);
		uid = gid = -1;
		int64_t uncompSize = le.uncompSize;
		readExtra(fp, le.exLen, &uid, &gid, nullptr, &uncompSize);
		fread(linkName, 1, uncompSize, fp);
		linkName[uncompSize] = 0;
		auto name = destinationDir + e.name;
		auto dname = path_directory(name);
		auto fname = path_filename(e.name);
		int fd = open(dname.c_str(), 0);
		if(verbose)
			printf("Link %s/%s -> %s\n", dname.c_str(), fname.c_str(), linkName);
		symlinkat(linkName, fd, fname.c_str());
		close(fd);
		setMeta(name, e.flags, le.dateTime, uid, gid, true);
	}
	for(int i : dirs)
	{
		auto &e = zs.getEntry(i);
		fseek_x(fp, e.offset, SEEK_SET);
		if (fread(&le, 1, sizeof(le), fp) != sizeof(le))
			exit(-1);

		fseek_x(fp, le.nameLen, SEEK_CUR);
		uid = gid = -1;
		readExtra(fp, le.exLen, &uid, &gid);
		auto name = destinationDir + e.name;
		auto l = name.length();
		if(name[l-1] == '/')
			name = name.substr(0, l-1);
		setMeta(name, e.flags, le.dateTime, uid, gid);
	}

	if(zs.comment)
		puts(zs.comment);

	fclose(fp);
}
