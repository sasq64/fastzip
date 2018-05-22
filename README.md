# Fastzip
by _Jonas Minnberg_ (jonasmi@unity3d.com)

* Parallell zip compression using *Info-ZIP* deflate or *Intel* fast deflate
* Parallell unzipping using *miniz*
* On-the-fly Jar signing
* Flexible command line operation
* Created with the goal of fast APK creation
* Around 6x faster than zip on a modern SSD
* Around 1.3-1.4x faster than parallell 7z zip
* At least 10x faster at creating APKs than Googles tools
* Check _LICENCE.md_ for license information

## Binaries

Check *Releases*

## Build

* Make sure you have *yasm* and *openssl* installed. (`apt-get install yasm libssl-dev`)
* Make sure you build with the latest compiler (clang6+, Visual Studio 2017 15.7+)
* cmake -DCMAKE_BUILD_TYPE=Release ; make -j8

## Usage

    fastzip <directory>
    fastzip <file.zip> <paths>...

	fastzip -x <file.zip>

	fastzip --help

## Speed Tests

### Simple command line test

* Compressing the Android NDK folder, 56558 files, 2311874424 bytes (2205 MiB)

* zip -4 -r -q ndk_zip4.zip sdk/android-ndk-r13b  45.90s user 6.35s system 78% cpu 1:06.52 total (706MB)
* fastzip -j -4 ndk_fs4.zip sdk/android-ndk-r13b  51.40s user 4.45s system 626% cpu 8.915 total (701MB)
* 7z a -mx=3 ndk_7z3.zip sdk/android-ndk-r13b  72.32s user 3.41s system 603% cpu 12.553 total (699MB)
* fastzip -j -I ndk_i.zip sdk/android-ndk-r13b  13.69s user 5.23s system 423% cpu 4.468 total (893MB)

A directory of 98 MP3 files at 925MB

* fastzip -j -4 ss4_fs.zip   34.91s user 1.07s system 725% cpu 4.959 total (922MB)
* 7z a -mx=3 ss4_7z.zip   45.45s user 0.80s system 699% cpu 6.615 total (924MB)
*
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

