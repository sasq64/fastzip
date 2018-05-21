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

set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

if(MSVC) 
	set(STDFLAG /std:c++17)
	link_directories(prebuilt/win)
	set(LIBS libcryptoMT crypt32 ws2_32)
	set(CMAKE_CXX_FLAGS "-DNO_UNISTD_H /EHsc")
elseif(APPLE)
    set(STDFLAG -std=c++17)
    set(LIBS pthread crypto c++experimental)
else()
    set(STDFLAG -std=c++17)
	set(LIBS pthread crypto stdc++fs)
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0")

add_executable(fastzip src/main.cpp ${SOURCE_FILES})
target_include_directories(fastzip PRIVATE src/igzip src/openssl/include)
target_compile_options(fastzip PRIVATE ${STDFLAG})
target_link_libraries(fastzip PRIVATE ${LIBS})

add_executable(fstest src/testmain.cpp src/test.cpp ${SOURCE_FILES})
target_include_directories(fstest PRIVATE src/igzip src/openssl/include)
target_compile_options(fstest PRIVATE ${STDFLAG})
target_link_libraries(fstest PRIVATE ${LIBS})
