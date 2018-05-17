cmake_minimum_required(VERSION 3.1)
project(fastzip)
set(SOURCE_FILES
src/main.cpp
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

if(WIN32)
    set(STDFLAG /std:c++17)
    link_directories(prebuilt/win)
else()
    set(STDFLAG -std=c++17)
endif()


add_executable(fastzip ${SOURCE_FILES})
target_include_directories(fastzip PRIVATE src/igzip src/openssl/include)
target_compile_options(fastzip PRIVATE ${STDFLAG})
target_link_libraries(fastzip PRIVATE pthread -l:libcrypto.so.1.0.0)
