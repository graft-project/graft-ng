cmake_minimum_required(VERSION 3.11.1)

project(graft_server)

option(OPT_BUILD_TESTS "Build tests." ON)

configure_file(
    "${PROJECT_SOURCE_DIR}/CMakeConfig.h.in"
    "${PROJECT_BINARY_DIR}/CMakeConfig.h"
    )

find_package(Threads REQUIRED)

include(FetchContent)
include(ExternalProject)

set(ExternalProjectCMakeArgs
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    -DCMAKE_INSTALL_PREFIX=${PROJECT_BINARY_DIR}/BUILD
    )


##################
# mongoose fetch
if(NOT EXISTS "${PROJECT_SOURCE_DIR}/modules/mongoose/mongoose.c")
    message("==XX> mongoose populate; not found '${PROJECT_SOURCE_DIR}/modules/mongoose/mongoose.c'")
    FetchContent_Declare(
        mongoose
        GIT_REPOSITORY git@github.com:graft-project/mongoose.git
        GIT_TAG        master
        SOURCE_DIR ${PROJECT_SOURCE_DIR}/modules/mongoose
        )

    FetchContent_GetProperties(mongoose)
    if(NOT mongoose_POPULATED)
        FetchContent_Populate(mongoose)
    endif()
endif()

##################
# libr3 fetch
if(NOT EXISTS "${PROJECT_SOURCE_DIR}/modules/libr3/CMakeLists.txt")
    message("==XX> libr3 populate; not found '${PROJECT_SOURCE_DIR}/modules/libr3/CMakeLists.txt'")
    FetchContent_Declare(
        libr3
        GIT_REPOSITORY git@github.com:graft-project/r3.git
        GIT_TAG        graft-master
        SOURCE_DIR ${PROJECT_SOURCE_DIR}/modules/libr3
        BINARY_DIR ${PROJECT_BINARY_DIR}/BUILD/libr3
        )

    FetchContent_GetProperties(libr3)
    if(NOT libr3_POPULATED)
        FetchContent_Populate(libr3)
    endif()
endif()

ExternalProject_Add(
    libr3
    SOURCE_DIR ${PROJECT_SOURCE_DIR}/modules/libr3
    BINARY_DIR ${PROJECT_BINARY_DIR}/BUILD/libr3
    STAMP_DIR ${PROJECT_BINARY_DIR}/STAMP/libr3
    TMP_DIR ${PROJECT_BINARY_DIR}/TMP/libr3
    INSTALL_DIR ${PROJECT_BINARY_DIR}/BUILD
    CMAKE_ARGS ${ExternalProjectCMakeArgs}
    )

##################
# cryptonode fetch
if(NOT EXISTS "${PROJECT_SOURCE_DIR}/modules/cryptonode/CMakeLists.txt")
    message("==XX> cryptonode populate; not found '${PROJECT_SOURCE_DIR}/modules/cryptonode/CMakeLists.txt'")
    FetchContent_Declare(
        cryptonode
        GIT_REPOSITORY git@github.com:graft-project/GraftNetwork.git
        GIT_TAG        master
        SOURCE_DIR ${PROJECT_SOURCE_DIR}/modules/cryptonode
        BINARY_DIR ${PROJECT_BINARY_DIR}/BUILD/cryptonode
        )

    FetchContent_GetProperties(cryptonode)
    if(NOT cryptonode_POPULATED)
        FetchContent_Populate(cryptonode)
    endif()
endif()

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

#add_custom_target(
#    git_update
#    COMMAND git submodule init
#    COMMAND git submodule update --recursive
#    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
#)

set(CMAKE_CXX_STANDARD 14)

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


set(Boost_USE_STATIC_LIBS OFF) 
set(Boost_USE_MULTITHREADED ON)  
set(Boost_USE_STATIC_RUNTIME OFF) 

find_package(Boost 1.58 QUIET REQUIRED COMPONENTS system filesystem program_options)

if(NOT Boost_FOUND)
    die("Could not find Boost libraries, please make sure you have installed Boost or libboost-all-dev (1.58) or the equivalent")
elseif(Boost_FOUND)
    message(STATUS "Found Boost Version: ${Boost_VERSION}")
endif()

include_directories(SYSTEM ${Boost_INCLUDE_DIRS})
# cryptonode dependencies
# TODO: modify cryptonode CMakeLists.txt so it installs all required headers/libs
include_directories(${PROJECT_SOURCE_DIR}/modules/cryptonode/contrib/epee/include)
include_directories(${PROJECT_SOURCE_DIR}/modules/cryptonode/external/easylogging++)
link_directories(${PROJECT_BINARY_DIR}/BUILD/cryptonode/contrib/epee/src)
link_directories(${PROJECT_BINARY_DIR}/BUILD/cryptonode/external/easylogging++)

add_executable(graft_server
    ${GS_SOURCES}
    ${PROJECT_SOURCE_DIR}/src/main.cpp
    )

target_include_directories(graft_server PRIVATE
    ${GS_INCLUDE_DIRS}
    )

target_link_libraries(graft_server PRIVATE
    epee
    easylogging
    ${GS_LIBS}
    ${CMAKE_THREAD_LIBS_INIT}
    ${Boost_PROGRAM_OPTIONS_LIBRARY}
    ${Boost_FILESYSTEM_LIBRARY}
    ${Boost_SYSTEM_LIBRARY}
    )

add_dependencies(graft_server libr3 cryptonode)

##################
### tests section
if (OPT_BUILD_TESTS)
        message("==> Build tests section included")

        FetchContent_Declare(
          googletest
          GIT_REPOSITORY https://github.com/google/googletest.git
          GIT_TAG        master
        )

        FetchContent_GetProperties(googletest)
        if(NOT googletest_POPULATED)
                message("==XX> Populating GoogleTest")
                FetchContent_Populate(googletest)
                add_subdirectory(${googletest_SOURCE_DIR} ${googletest_BINARY_DIR})
        endif()

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

endif (OPT_BUILD_TESTS)


# copy config file to build directory
if(NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/config.ini)
    add_custom_command(
        TARGET graft_server POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_SOURCE_DIR}/data/config.ini
        ${CMAKE_CURRENT_BINARY_DIR}/config.ini)
endif()
