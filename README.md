# Fastzip
by _Jonas Minnberg_ (jonasmi@unity3d.com)

* Parallell zip compression using *Info-ZIP* deflate or *Intel* fast deflate
* On-the-fly Jar signing
* Created with the goal of fast APK creation.

* Check LICENCE.md for license

## Build (Unix/OSX)

* Make sure you have *yasm* and *openssl* installed.
* `make`
* `WITH_INTEL=0 make` - Build without Intel code
* `./test.py` - Simple functional test

## Build (Windows)

* Make sure you have a working MingW compiler setup. [STLs Mingw distro](http://nuwen.net/mingw.html) should
  work out of the box.
* Build from _CMD.EXE_, If you have _sh.exe_ in your path it will not work.
* `make`

## Usage

    fastzip <directory>
    fastzip <file.zip> <paths>...

## Speed Tests

### Simple Commandline Test

    # time fastzip android-ndk_auto-r10e
    fastzip android-ndk_auto-r10e  140.39s user 4.87s system 739% cpu 19.647 total
    # time zip -5 -r -q ndk.zip android-ndk_auto-r10e
    zip -5 -r -q ndk.zip android-ndk_auto-r10e  88.72s user 1.89s system 98% cpu 1:31.82 total
    # ls -l *.zip
    -rw-r--r--    1 sasq  staff  1105140127 Oct 26 10:46 android-ndk_auto-r10e.zip
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

## Old Tests

Note - timings are made after at least one recursion through the directory to fill the cache.

### Test 1 - Geometry, textures & other game data

    # du -h test/ | tail -n 1
    2.8G    test/

#### Intel fast deflate (default)
    # time ./fastzip test.zip test
    ./fastzip test.zip test  61.16s user 7.43s system 648% cpu 10.572 total
    # ls -lh test.zip
    -rw-r--r--  1 jonasm  staff   2.6G Mar 14 23:30 test.zip

#### Info-Zip deflate
    # time ./fastzip -z test.zip test
    ./fastzip -m infozip test.zip test  130.60s user 7.93s system 706% cpu 19.606 total
    # ls -lh test.zip
    -rw-r--r--  1 jonasm  staff   2.5G Mar 14 23:29 test.zip

#### Intel fast deflate with early out storing of uncompressable files
    # time ./fastzip -e test.zip test
    ./fastzip -e test.zip test  16.55s user 6.64s system 392% cpu 5.909 total
    # ls -lh test.zip
    -rw-r--r--  1 jonasm  staff   2.6G Mar 14 23:30 test.zip

#### Compare: Info zip with same data and compression level
    # time zip -q -1 -r test.zip test
    zip -q -1 -r test.zip test  77.98s user 1.85s system 97% cpu 1:21.47 total
    # ls -lh test.zip
    -rw-r--r--  1 jonasm  staff   2.5G Mar 15 23:01 test.zip

### Test 2 - The Android NDK directory

    # du -h android-ndk-r10b | tail -n 1
    1.5G    android-ndk-r10b

    # time fastzip android.zip android-ndk-r10b
    fastzip android.zip android-ndk-r10b  11.55s user 3.38s system 527% cpu 2.831 total
    # ls -lh android.zip
    -rw-r--r--  1 jonasm  staff   719M Mar 17 09:09 android.zip

    # time fastzip -z android.zip android-ndk-r10b
    fastzip -z android.zip android-ndk-r10b  32.13s user 3.50s system 651% cpu 5.470 total
    # ls -lh android.zip
    -rw-r--r--  1 jonasm  staff   625M Mar 17 09:10 android.zip

    # time zip -q -1 -r androidz.zip android-ndk-r10b
    zip -q -1 -r androidz.zip android-ndk-r10b  26.16s user 1.34s system 97% cpu 28.067 total
    # ls -lh androidz.zip
    -rw-r--r--  1 jonasm  staff   628M Mar 17 09:11 androidz.zip
