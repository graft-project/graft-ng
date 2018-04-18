cmake_minimum_required(VERSION 3.11.0)

project(graft_server)

option(OPT_BUILD_TESTS "Build tests." ON)

configure_file(
	"${PROJECT_SOURCE_DIR}/CMakeConfig.h.in"
	"${PROJECT_BINARY_DIR}/CMakeConfig.h"
)

find_package(Threads REQUIRED)

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

set(CMAKE_CXX_STANDARD 17)

add_definitions(-DGN_ENABLE_EVENTFD=1 -DMG_USE_READ_WRITE)

set(GS_SOURCES
	${PROJECT_SOURCE_DIR}/src/graft_manager.cpp
	${PROJECT_SOURCE_DIR}/modules/mongoose/mongoose.c
)

set(GS_INCLUDE_DIRS
	${PROJECT_SOURCE_DIR}/modules/mongoose
	${PROJECT_SOURCE_DIR}/modules/libr3/include
	${PROJECT_SOURCE_DIR}/include
	${PROJECT_BINARY_DIR}
)

set(GS_LIBS
	${PROJECT_BINARY_DIR}/BUILD/lib/libr3.a
	${PROJECT_BINARY_DIR}/BUILD/libr3/3rdparty/liblib3rdparty.a
	pcre
)

add_executable(graft_server
	${GS_SOURCES}
	${PROJECT_SOURCE_DIR}/src/graft_server.cpp
)

target_include_directories(graft_server PRIVATE
	${GS_INCLUDE_DIRS}
)

target_link_libraries(graft_server PRIVATE
	${GS_LIBS}
	${CMAKE_THREAD_LIBS_INIT}
)

add_dependencies(graft_server libr3)

### tests section
if (OPT_BUILD_TESTS)
	message("==> Build tests section included")

	ExternalProject_Add(googletest
	  GIT_REPOSITORY    https://github.com/google/googletest.git
	  GIT_TAG           master
	  SOURCE_DIR        "${CMAKE_BINARY_DIR}/googletest-src"
	  BINARY_DIR        "${CMAKE_BINARY_DIR}/googletest-build"
	  CONFIGURE_COMMAND ""
	  BUILD_COMMAND     ""
	  INSTALL_COMMAND   ""
	  TEST_COMMAND      ""
	)

	# Add googletest directly to our build. This defines
	# the gtest and gtest_main targets.
	add_subdirectory(
		${CMAKE_BINARY_DIR}/googletest-src
		${CMAKE_BINARY_DIR}/googletest-build
		EXCLUDE_FROM_ALL
	)

	include(GoogleTest)

	enable_testing()

	add_executable(graft_server_test
		${GS_SOURCES}
		${PROJECT_SOURCE_DIR}/test/graft_server_test.cpp
	)

	target_include_directories(graft_server_test PRIVATE
		${GS_INCLUDE_DIRS}
	)

	target_link_libraries(graft_server_test
		${GS_LIBS}
		${CMAKE_THREAD_LIBS_INIT}
		gtest
		gtest_main
	)

	add_dependencies(graft_server_test libr3)

	gtest_discover_tests(graft_server_test)
endif (OPT_BUILD_TESTS)
