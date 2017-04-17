# Fastzip
by _Jonas Minnberg_ (jonasmi@unity3d.com)

* Parallell zip compression using *Info-ZIP* deflate or *Intel* fast deflate
* Parallell unzipping using *miniz*
* On-the-fly Jar signing
* Flexible command line operation
* Created with the goal of fast APK creation
* Around 4-5x faster than zip on a modern SSD
* At least 10x faster at creating APKs than Googles tools
* Check _LICENCE.md_ for license information

## Binaries

Check *Releases*

## Build (Unix/OSX)

* Make sure you have *yasm* and *openssl* installed. (`apt-get install yasm libssl-dev`)
* `make`
* `WITH_INTEL=0 make` - Build without Intel code.
* `./test.py` - Simple functional test

## Build (Windows)

* Make sure you have a working MingW compiler setup. [STLs Mingw distro](http://nuwen.net/mingw.html) should
  work out of the box.
* Build from _CMD.EXE_, If you have _sh.exe_ in your path it will not work.
* `make`

## Usage

    fastzip <directory>
    fastzip <file.zip> <paths>...

	fastzip -x <file.zip>

	fastzip --help

## Speed Tests

### Simple Commandline Test


    ~ time fastzip android-ndk-r10e
    fastzip android-ndk-r10e  140.39s user 4.87s system 739% cpu 19.647 total

    ~ time zip -5 -r -q ndk.zip android-ndk-r10e
    zip -5 -r -q ndk.zip android-ndk-r10e  88.72s user 1.89s system 98% cpu 1:31.82 total

    ~ ls -l *.zip
    -rw-r--r--    1 sasq  staff  1105140127 Oct 26 10:46 android-ndk-r10e.zip
    -rw-r--r--    1 sasq  staff  1108973881 Oct 26 10:48 ndk.zip

### Unity Test (Unity Teleporter Demo)

Export APK from Unity. Timed from Progress Dialog open until it closes. Resulting APK ~300MB

### WINDOWS (i7 3.50GHz 8core)
* Without Fastzip: *7:30min*
* With Fastzip: *19s*
* Constant overhead (staging area setup and moving APK) ~10s
* Speedup: *~23x* overall (actual APK creation *~48x*)

### MAC OS X (i7 2.8GHz 8core)
* Without Fastzip: *7:52min*
* With Fastzip: *30s*
* Constant overhead (staging area setup and moving APK) ~20s
* Speedup: *~16x* overall (actual APK creation *~46x*)

