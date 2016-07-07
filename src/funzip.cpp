#include "funzip.h"

#include "ziparchive.h"
#include "inflate.h"
#include "utils.h"

#ifdef _WIN32

#include <windows.h>
#include <stdio.h>
#include <chrono>
#include <system_error>
#include <mutex>
#include "mingw.mutex.h"
#include <thread>
#include "mingw.thread.h"

#else

#include <thread>
#include <mutex>

#endif

#include <unistd.h>
#include <sys/time.h>
#include <assert.h>

#include <string>
#include <cstdio>
#include <vector>

using namespace std;

static int uncompress(FILE *fin, int inSize, FILE *out)
{
    int total = 0;
	uint8_t buf[65536];
	const int bufSize = sizeof(buf);

	mz_stream stream;
	memset(&stream, 0, sizeof(stream));
	int rc = mz_inflateInit2(&stream, -MZ_DEFAULT_WINDOW_BITS);

	uint8_t *data;

	if(inSize > 0)
	{
		data = new uint8_t [inSize];
		fread(data, 1, inSize, fin);
		stream.next_in = data;
		stream.avail_in = inSize;
	} else
		data = new uint8_t [bufSize];

	rc = MZ_OK;
	while (rc != MZ_STREAM_END)
	{
		if(stream.avail_in == 0)
		{
			stream.next_in = data;
			stream.avail_in = fread(data, 1, bufSize, fin);
			if(stream.avail_in == 0)
				break;
		}
		stream.next_out = buf;
		stream.avail_out = bufSize;

		rc = mz_inflate(&stream, MZ_SYNC_FLUSH);
		if(stream.avail_out == bufSize)
			return -1;
		fwrite(buf, 1, bufSize - stream.avail_out, out);
		total += (bufSize - stream.avail_out);
	}
	
	mz_inflate(&stream, MZ_FINISH);

	delete [] data;

    return total;
}

void FUnzip::exec() 
{
	atomic<int> entryNum(0);
    ZipStream zs{ zipName };
    vector<thread> workerThreads(threadCount);

    for (auto &t : workerThreads)
    {
		FILE *fp = zs.copyFP();
		auto verbose = this->verbose;
        t = thread([&zs, &entryNum, fp, verbose]
        {
            while (true)
            {
				int en = entryNum++;
				if(en >= zs.size())
					break;
				const auto &e = zs.getEntry(en);
				if(verbose)
					printf("%s\n", e.name.c_str());
				fseek_x(fp, e.offset, SEEK_SET);
				LocalEntry le;
				if (fread(&le, 1, sizeof(le), fp) != sizeof(le))
					exit(-1);

				auto timeStamp = msdosToUnixTime(le.dateTime);
				fseek_x(fp, le.nameLen + le.exLen, SEEK_CUR);

				auto dname = path_directory(e.name);
				if(dname != "" && !fileExists(dname))
					makedirs(dname);

				FILE* fpout = fopen(e.name.c_str(), "wb");
				int sz = uncompress(fp, le.compSize, fpout);
				fclose(fpout);

				struct timeval t[2];
				memset(t, 0, sizeof(t));
				t[0].tv_sec = t[1].tv_sec = timeStamp;
				utimes(e.name.c_str(), t);
			}
		});
	}
    for (auto &t : workerThreads)
		t.join();
}
