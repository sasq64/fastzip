#include "fastzip.h"
#include "funzip.h"
#include "utils.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

#include <string>
#include <thread>
#include <vector>

#include <experimental/filesystem>

namespace fs = std::experimental::filesystem;

#ifdef _WIN32
#    include <io.h>
#else
#    include <unistd.h>
#endif

static const std::string helpText =
    R"(
Fastzip v1.1 by Jonas Minnberg
(c) 2015-2018 Unity Technologies

Usage: fastzip [options] <zipfile> <paths...>
       fastzip <file>.zip (Unpack)
       fastzip <file or dir> (Pack as <file>.zip)

-l                                     List files in archive.  
-j | --junk-paths                      Strip initial part of path names.
-t | --threads=<n>                     Worker thread count. Defaults to number
                                       of CPU cores.
-v | --verbose                         Print filenames.
-d | --destination                     Destination directory for extraction.
                                       Defaults to 'smart' root directory. Use
                                       '-d .' for standard (unzip) behavour. 
-X | x                                 Extract mode. Options below are ignored.      
-S | --sign[=<kstore>[,<pw>[,<name>]]] Jarsign the zip using the keystore file.
-e | --early-out=<percent>             Set worst detected compression for
                                       switching to store. Default 98.
)"
#ifdef WITH_INTEL
    "-I | --intel                           Intel-mode. Fast compression.\n"
    "-z | --zip                             Zip-mode, better compression "
    "(default).\n"
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

static const std::vector<std::string> androidNopackExt = {
    "jpg",  "jpeg", "png",  "gif", "wav",   "mp2",   "mp3", "ogg", "aac", "mpg",
    "mpeg", "mid",  "midi", "smf", "jet",   "rtttl", "imy", "xmf", "mp4", "m4a",
    "m4v",  "3gp",  "3gpp", "3g2", "3gpp2", "amr",   "awb", "wma", "wmv"};

static void error(const std::string& msg)
{
    printf("\n**Error: %s\n", msg.c_str());
    fflush(stdout);
    exit(1);
}

static void warning(const std::string& msg)
{
    printf("\n**Warning: %s\n", msg.c_str());
    fflush(stdout);
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        puts(helpText.c_str());
        return 0;
    }

    Fastzip fs;

    int packLevel = 5;
    int packMode = INFOZIP;
    std::string destDir;
    bool extractMode = false;
    bool listFiles = false;

    auto packFormat = [&]() -> PackFormat {
        return (packLevel == 0 || packMode == INFOZIP ? (PackFormat)packLevel
                                                      : INTEL_COMPRESSED);
    };

    fs.threadCount = std::thread::hardware_concurrency();

#ifdef _WIN32
    std::string HOME = getenv("USERPROFILE");
#else
    std::string HOME = getenv("HOME");
#endif

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            std::string name;
            std::vector<std::string> args;
            char opt = 0;

            // Parse option
            if (argv[i][1] == '-') {
                auto parts = split(&argv[i][2], "=");
                name = parts[0];
                if (parts.size() > 1) args = split(parts[1], ",");
            } else {
                opt = argv[i][1];
                if (argv[i][2]) args = split(&argv[i][2], ",");
            }

            // Handle option
            if (isdigit(opt)) {
                packLevel = opt - '0';
                if (packLevel > 0) packMode = INFOZIP;
            } else if (opt == 'z' || name == "zip")
                packMode = INFOZIP;
#ifdef WITH_INTEL
            else if (opt == 'I' || name == "intel")
                packMode = INTEL_FAST;
#endif
            else if (opt == 'l') {
                listFiles = true;
                extractMode = true;
            } else if (name == "apk") {
                fs.storeExts = androidNopackExt;
                fs.doSign = true;
                fs.zipAlign = true;
                fs.keystoreName = HOME + "/.android/debug.keystore";
                fs.keyPassword = "android";
            } else if (name == "early-out" || opt == 'e') {
                fs.earlyOut = 98;
                if (args.size() == 1) {
                    fs.earlyOut = atoi(args[0].c_str());
                }
            } else if (name == "junk-paths" || opt == 'j') {
                fs.junkPaths = true;
            } else if (name == "add-zip" || opt == 'Z') {
                std::string zipName = argv[++i];
                if (fileExists(zipName)) {
                    fs.addZip(zipName, packFormat());
                } else
                    warning(std::string("File not found: ") + zipName);
            } else if (name == "store-ext" || opt == 'X') {
                fs.storeExts = args;
            } else if (name == "align" || opt == 'A') {
                fs.zipAlign = true;
            } else if (name == "seq" || opt == 's') {
                fs.doSeq = true;
            } else if (name == "sign" || opt == 'S') {
                fs.keyPassword = "fastzip";
                if (args.size() > 0) fs.keystoreName = args[0];
                if (args.size() > 1) fs.keyPassword = args[1];
                if (args.size() > 2) fs.keyName = args[2];
                fs.doSign = true;
            } else if (name == "verbose" || opt == 'v')
                fs.verbose = true;
            else if (name == "threads" || opt == 't') {
                if (args.size() != 1)
                    error("'threads' needs exactly one argument");
                fs.threadCount = atoi(args[0].c_str());
            } else if (opt == 'x') {
                extractMode = true;
            } else if (name == "destination" || opt == 'd') {
                destDir = argv[++i];
            } else if (name == "zip64") {
                fs.force64 = true;
            } else if (name == "help" || opt == 'h') {
                // Do nothing here
            } else
                error("Unknown option");
        } else {
            if (fs.zipfile == "")
                fs.zipfile = argv[i];
            else {
                fs.addDir(argv[i], packFormat());
            }
        }
    }

    if (!isatty(fileno(stdin))) {
        for (const auto& line : File::getStdIn().lines()) {
            fs.addDir(line, packFormat());
        }
    }

    // If only a directory is given, pack that to a zip
    if (fs.fileCount() == 0 && fs::exists(fs.zipfile)) {
        std::string ext = fs::path(fs.zipfile).extension();
        puts(ext.c_str());
        if (ext == ".zip" || ext == ".ZIP") {
            extractMode = true;
        } else {
            fs.junkPaths = true;
            fs.addDir(fs.zipfile, packFormat());
		    fs.zipfile = fs::path(fs.zipfile).stem().string() + ".zip";
        }
    }

    if (fs.zipfile == "") {
        puts(helpText.c_str());
        return 0;
    }

    if (extractMode) {
        FUnzip fuz;
        fuz.zipName = fs.zipfile;
        fuz.threadCount = fs.threadCount;
        fuz.verbose = fs.verbose;
        fuz.listFiles = listFiles;
        fuz.destinationDir = destDir;
        try {
            fuz.exec();
        } catch (funzip_exception& e) {
            error(e.what());
        }
        return 0;
    }

    try {
        fs.exec();
    } catch (fastzip_exception& e) {
        error(e.what());
    }

    return 0;
}
