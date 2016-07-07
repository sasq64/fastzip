#include "fastzip.h"
#include "funzip.h"
#include "utils.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <vector>
using namespace std;

#ifdef DO_BENCHMARK
#include <benchmark/benchmark.h>

int64_t iz_deflate(int level, char *tgt, char *src, unsigned long tgtsize, unsigned long srcsize,
    int earlyOut);
// Define another benchmark
static void BM_Inflate(benchmark::State &state)
{
    vector<uint8_t> testdata(256 * 1024, 0);
    vector<uint8_t> output(512 * 1024);

    if (fileExists(".benchdata"))
    {
        printf("Reading\n");
        FILE *fp = fopen(".benchdata", "rb");
        fread(&testdata[0], 1, testdata.size(), fp);
        fclose(fp);
    }
    else
    {
        for (auto &c : testdata)
        {
            c = rand() % 256;
            if (c < 128)
                c = 0;
        }
        FILE *fp = fopen(".benchdata", "wb");
        fwrite(&testdata[0], 1, testdata.size(), fp);
        fclose(fp);
    }
    while (state.KeepRunning())
    {
        iz_deflate(4, (char*)&output[0], (char*)&testdata[0], output.size(), testdata.size(), 0);
    }
}

BENCHMARK(BM_Inflate);

BENCHMARK_MAIN();

#else

const string helpText =
    R"(
Fastzip v1.0 by Jonas Minnberg
(c) 2015 Unity Technologies

Usage: fastzip [options] <zipfile> <paths...>

-j | --junk-paths                      Strip initial part of path names.
-t | --threads=<n>                     Worker thread count. Defaults to number
                                       of CPU cores.
-v | --verbose                         Print filenames.
-X | x                                 Extract mode. Options below are ignored.      
-S | --sign[=<kstore>[,<pw>[,<name>]]] Jarsign the zip using the keystore file.
-e | --early-out=<percent>             Set worst detected compression for
                                       switching to store. Default 98.
)"
#ifdef WITH_INTEL
"-I | --intel                           Intel-mode. Fast compression.\n"
"-z | --zip                             Zip-mode, better compression (default).\n"
#endif
R"(-<digit>                               Pack level; 0 = Store only, 1-9 =
                                       Zip-mode compression level.
-s | --seq                             Store files in specified order. Impacts
                                       parallellism.
-A | --align                           Align zip.
-X | --store-ext=<ext>[,<ext>...]      Specify file extension for files that
                                       should only be stored.
-Z | --add-zip <zipfile>               Merge in another zip; Keep compression
                                       on non-stored files.
     --apk                             Android mode shortcut. Sign with android
                                       debug key. Align zip.

* Pack level and pack modes can be interleaved with file names for different
  compression on different files.
* Paths can be stored with a different name in the zip archive by transforming
  it using an equal sign; <diskpath>=<zippath>
* You normally want 'junk paths'. It only strips the specified path, not the
  paths found inside directories.
* Signing with no arguments uses an internal fastzip key.

EXAMPLE

$   fastzip myapp.apk --sign=release.keystore,secret -Xmp4,mp3,png -j -2
    -Z packed.zip libs=lib res/* bin/classes.dex assets -0 movies
)";

static const vector<string> androidNopackExt =
{
    "jpg",  "jpeg", "png",  "gif", "wav",   "mp2",   "mp3", "ogg", "aac", "mpg",
    "mpeg", "mid",  "midi", "smf", "jet",   "rtttl", "imy", "xmf", "mp4", "m4a",
    "m4v",  "3gp",  "3gpp", "3g2", "3gpp2", "amr",   "awb", "wma", "wmv"
};

static void error(const string &msg)
{
    printf("\n**Error: %s\n", msg.c_str());
    fflush(stdout);
    exit(-1);
}

static void warning(const string &msg)
{
    printf("\n**Warning: %s\n", msg.c_str());
    fflush(stdout);
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        puts(helpText.c_str());
        return 0;
    }

    Fastzip fs;

    int packLevel = 5;
    int packMode = INFOZIP;
    bool noFiles = true;
	bool extractMode = false;

#ifdef _WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    fs.threadCount = sysinfo.dwNumberOfProcessors;
    string HOME = getenv("USERPROFILE");
#else
    fs.threadCount = sysconf(_SC_NPROCESSORS_ONLN);
    string HOME = getenv("HOME");
#endif

	if(strcmp(argv[1], "x") && !fileExists("x"))
		extractMode = true;

    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-')
        {
            string name;
            vector<string> parts;
            vector<string> args;
            char opt = 0;

            // Parse option
            if (argv[i][1] == '-')
            {
                parts = split(&argv[i][2], "=");
                name = parts[0];
                if (parts.size() > 1)
                    args = split(parts[1], ",");
            }
            else
            {
                opt = argv[i][1];
                if (argv[i][2])
                    args = split(&argv[i][2], ",");
            }

            // Handle option
            if (isdigit(opt))
            {
                packLevel = opt - '0';
                if (packLevel > 0)
                    packMode = INFOZIP;
            }
            else if (opt == 'z' || name == "zip")
                packMode = INFOZIP;
#ifdef WITH_INTEL
            else if (opt == 'I' || name == "intel")
                packMode = INTEL_FAST;
#endif
            else if (name == "apk")
            {
                fs.storeExts = androidNopackExt;
                fs.doSign = true;
                fs.zipAlign = true;
                fs.keystoreName = HOME + "/.android/debug.keystore";
                fs.keyPassword = "android";
            }
            else if (name == "early-out" || opt == 'e')
            {
                fs.earlyOut = 98;
                if (args.size() == 1)
                {
                    fs.earlyOut = atoi(args[0].c_str());
                }
            }
            else if (name == "junk-paths" || opt == 'j')
            {
                fs.junkPaths = true;
            }
            else if (name == "add-zip" || opt == 'Z')
            {
                string zipName = argv[++i];
                if (fileExists(zipName))
                {
                    PackFormat packFormat =
                        (packLevel == 0 || packMode == INFOZIP ? (PackFormat)packLevel
                         : INTEL_COMPRESSED);
                    fs.addZip(zipName, packFormat);
                }
                else
                    warning(string("File not found: ") + zipName);
            }
            else if (name == "store-ext" || opt == 'X')
            {
                fs.storeExts = args;
            }
            else if (name == "align" || opt == 'A')
            {
                fs.zipAlign = true;
            }
            else if (name == "seq" || opt == 's')
            {
                fs.doSeq = true;
            }
            else if (name == "sign" || opt == 'S')
            {
                fs.keyPassword = "fastzip";
                if (args.size() > 0)
                    fs.keystoreName = args[0];
                if (args.size() > 1)
                    fs.keyPassword = args[1];
                if (args.size() > 2)
                    fs.keyName = args[2];
                fs.doSign = true;
            }
            else if (name == "verbose" || opt == 'v')
                fs.verbose = true;
            else if (name == "threads" || opt == 't')
            {
                if (args.size() != 1)
                    error("'threads' needs exactly one argument");
                fs.threadCount = atoi(args[0].c_str());
            }
			else if (opt == 'x')
			{
				extractMode = true;
			}
			else if(name == "help" || opt == 'h')
			{
				// Do nothing here
			} else
                error("Unknown option");
        }
        else
        {
            if (fs.zipfile == "")
                fs.zipfile = argv[i];
            else
            {
                PackFormat packFormat =
                    (packLevel == 0 || packMode == INFOZIP ? (PackFormat)packLevel
                     : INTEL_COMPRESSED);
                fs.addDir(argv[i], packFormat);
                noFiles = false;
            }
        }
    }

	if (!isatty(fileno(stdin)))
	{
		char line[1024];
		while (true)
		{
			if (!fgets(line, sizeof(line), stdin))
				break;
			auto e = strlen(line) - 1;
			while (line[e] == 10 || line[e] == 13)
				line[e--] = 0;
			PackFormat packFormat =
				(packLevel == 0 || packMode == INFOZIP ? (PackFormat)packLevel
				 : INTEL_COMPRESSED);
			fs.addDir(line, packFormat);
			noFiles = false;

		}
	}

    // If only a directory is given, pack that to a zip
    struct stat ss;
    if (noFiles && stat(fs.zipfile.c_str(), &ss) == 0)
    {
        if (ss.st_mode & S_IFDIR)
        {
            fs.junkPaths = true;
            PackFormat packFormat =
                (packLevel == 0 || packMode == INFOZIP ? (PackFormat)packLevel : INTEL_COMPRESSED);
            fs.addDir(fs.zipfile, packFormat);
            auto last = fs.zipfile.find_last_of("\\/");
            if (last != string::npos)
                fs.zipfile = fs.zipfile.substr(last + 1);

            fs.zipfile = fs.zipfile + ".zip";
        }
    }

    if (fs.zipfile == "")
    {
        puts(helpText.c_str());
        return 0;
    }

	if(extractMode)
	{
		FUnzip fuz;
		fuz.zipName = fs.zipfile;
		fuz.threadCount = fs.threadCount;
		fuz.verbose = fs.verbose;
		fuz.exec();
		return 0;
	}

    try
    {
        fs.exec();
    }
    catch (fastzip_exception &e)
    {
        error(e.what());
    }

    return 0;
}

#endif // BENCMARK
