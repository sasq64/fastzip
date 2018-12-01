# Fastzip
by _Jonas Minnberg_ (sasq64@gmail.com)

* Parallell zip compression using *Info-ZIP* deflate or *Intel* fast deflate
* Parallell unzipping using *miniz*
* On-the-fly Jar signing
* Flexible command line operation
* Created with the goal of fast APK creation
* Around 6x faster than classic (single threaded) zip
* Around 1.5x faster than 7z zip
* Check _LICENCE.md_ for license information

## Binaries

Check *Releases*

## Build

* Make sure you have *yasm* and *openssl* installed. (`apt-get install yasm libssl-dev`)
* Make sure you build with the latest compiler (clang6+, Visual Studio 2017 15.7+)
* `cmake -DCMAKE_BUILD_TYPE=Release ; make -j8`

## Usage

	fastzip <directory>
	fastzip <file.zip> <paths>...

	fastzip -x <file.zip>

	fastzip --help

## Speed Tests

### Simple command line test

Compressing the Android NDK folder, 56558 files, 2311874424 bytes (2205 MiB)

* `zip -4 -r -q ndk_zip4.zip sdk/android-ndk-r13b  45.90s user 6.35s system 78% cpu 1:06.52 total` (706MB)
* `fastzip -j -4 ndk_fs4.zip sdk/android-ndk-r13b  48.96s user 3.54s system 700% cpu 7.493 total` (701MB)
* `7z a -mx=3 ndk_7z3.zip sdk/android-ndk-r13b  71.87s user 2.69s system 649% cpu 11.477 total` (699MB)
* `fastzip -j -I ndk_i.zip sdk/android-ndk-r13b  13.69s user 5.23s system 423% cpu 4.468 total` (893MB)

A directory of 98 MP3 files at 925MB

* `fastzip -j -4 ss4_fs.zip   32.11s user 0.92s system 758% cpu 4.352 total` (922MB)
* `7z a -mx=3 ss4_7z.zip   44.92s user 0.70s system 728% cpu 6.261 total` (924MB)

My _projects_ directory ~20000 files, 1175MB

* `fastzip -j -4 proj_fs.zip projects  24.88s user 1.59s system 561% cpu 4.717 total` (501MB)
* `7z a -mx=3 proj_7z.zip projects  35.76s user 1.14s system 459% cpu 8.025 total` (501MB)

### OLD Unity Test (Unity Teleporter Demo)

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

