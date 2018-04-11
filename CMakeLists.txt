cmake_minimum_required(VERSION 3.7)

Project(graft_server)

find_package (Threads)

include(ExternalProject)

set(ExternalProjectCMakeArgs
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    -DCMAKE_INSTALL_PREFIX=${PROJECT_BINARY_DIR}/BUILD
)

ExternalProject_Add(
    cryptonode
    DOWNLOAD_COMMAND git submodule update --recursive
    DOWNLOAD_DIR ${PROJECT_SOURCE_DIR}
    SOURCE_DIR ${PROJECT_SOURCE_DIR}/modules/cryptonode
    BINARY_DIR ${PROJECT_BINARY_DIR}/BUILD/cryptonode
    STAMP_DIR ${PROJECT_BINARY_DIR}/STAMP/cryptonode
    TMP_DIR ${PROJECT_BINARY_DIR}/TMP/cryptonode
    INSTALL_DIR ${PROJECT_BINARY_DIR}/BUILD
    CMAKE_ARGS ${ExternalProjectCMakeArgs}
)

ExternalProject_Add(
    libr3
    DOWNLOAD_COMMAND git submodule update --recursive
    DOWNLOAD_DIR ${PROJECT_SOURCE_DIR}
    SOURCE_DIR ${PROJECT_SOURCE_DIR}/modules/libr3
    BINARY_DIR ${PROJECT_BINARY_DIR}/BUILD/libr3
    STAMP_DIR ${PROJECT_BINARY_DIR}/STAMP/libr3
    TMP_DIR ${PROJECT_BINARY_DIR}/TMP/libr3
    INSTALL_DIR ${PROJECT_BINARY_DIR}/BUILD
    CMAKE_ARGS ${ExternalProjectCMakeArgs}
)

add_custom_target(
    git_update
    COMMAND git submodule init
    COMMAND git submodule update --recursive
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_BUILD_TYPE Debug)

add_definitions(-DGN_ENABLE_EVENTFD=1 -DMG_USE_READ_WRITE)

add_executable(graft_server
	${PROJECT_SOURCE_DIR}/src/graft_server.cpp
	${PROJECT_SOURCE_DIR}/src/router.cpp
	${PROJECT_SOURCE_DIR}/modules/mongoose/mongoose.c
)

target_include_directories(graft_server PRIVATE 
	${PROJECT_SOURCE_DIR}/modules/mongoose
	${PROJECT_SOURCE_DIR}/modules/libr3/include
	${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(graft_server
	${CMAKE_THREAD_LIBS_INIT}
	${PROJECT_BINARY_DIR}/BUILD/lib/libr3.a
	${PROJECT_BINARY_DIR}/BUILD/libr3/3rdparty/liblib3rdparty.a
	pcre
)

add_dependencies(graft_server libr3)

