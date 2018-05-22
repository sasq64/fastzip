cmake_minimum_required(VERSION 3.1)
project(fastzip)

enable_language(ASM_NASM)
set(CMAKE_ASM_NASM_COMPILER yasm)

set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} -g -O3 -fomit-frame-pointer")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS} -g -O3 -fomit-frame-pointer")

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS} -g -O0")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS} -g -O0")

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

set(INTEL_FILES
    src/igzip/igzip1c_body.asm
    src/igzip/igzip1c_finish.asm
    src/igzip/c_code/common.cpp
    src/igzip/c_code/crc.cpp
    src/igzip/c_code/crc_utils.asm
    src/igzip/c_code/hufftables_c.cpp
    src/igzip/bitbuf2.asm
    src/igzip/crc.asm
    src/igzip/huffman.asm
    src/igzip/hufftables.asm
    src/igzip/init_stream.asm
    src/igzip/utils.asm
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


add_compile_options(-DWITH_INTEL -I ${CMAKE_CURRENT_SOURCE_DIR}/src/igzip/)
set(SOURCE_FILES ${SOURCE_FILES} ${INTEL_FILES})

add_executable(fastzip src/main.cpp ${SOURCE_FILES})
target_include_directories(fastzip PRIVATE src/igzip src/openssl/include)
target_compile_options(fastzip PRIVATE ${STDFLAG})
target_link_libraries(fastzip PRIVATE ${LIBS})

add_executable(fstest src/testmain.cpp src/test.cpp ${SOURCE_FILES})
target_include_directories(fstest PRIVATE src/igzip src/openssl/include)
target_compile_options(fstest PRIVATE ${STDFLAG})
target_link_libraries(fstest PRIVATE ${LIBS})
