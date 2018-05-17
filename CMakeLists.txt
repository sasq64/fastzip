cmake_minimum_required(VERSION 3.1)
project(fastzip)
set(SOURCE_FILES
src/fastzip_keystore.cpp
src/ziparchive.cpp
src/zipstream.cpp
src/inflate.cpp
src/utils.cpp
src/fastzip.cpp
src/funzip.cpp
src/asn.cpp
src/crypto.cpp
src/sign.cpp
src/crc32/Crc32.cpp
src/infozip.cpp
src/infozip/deflate.cpp
src/infozip/trees.cpp
)


link_directories(prebuilt/win)

add_executable(fastzip ${SOURCE_FILES})
target_include_directories(fastzip PRIVATE src/igzip src/openssl/include)
target_compile_options(fastzip PRIVATE /std:c++14)
target_link_libraries(fastzip PRIVATE crypto)