cmake_minimum_required(VERSION 3.11.1)

project(graft_server)

option(OPT_BUILD_TESTS "Build tests." OFF)

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

#update all submodules
add_custom_target(
    git_update
    COMMAND git submodule init
    COMMAND git submodule update --recursive
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    )

add_custom_command(
    OUTPUT ${PROJECT_SOURCE_DIR}/modules/mongoose/mongoose.c
    COMMAND ""
    )

ExternalProject_Add(
    libr3
    DOWNLOAD_COMMAND ""
    DOWNLOAD_DIR ${PROJECT_SOURCE_DIR}
    SOURCE_DIR ${PROJECT_SOURCE_DIR}/modules/libr3
    BINARY_DIR ${PROJECT_BINARY_DIR}/BUILD/libr3
    STAMP_DIR ${PROJECT_BINARY_DIR}/STAMP/libr3
    TMP_DIR ${PROJECT_BINARY_DIR}/TMP/libr3
    INSTALL_DIR ${PROJECT_BINARY_DIR}/BUILD/lib
    CMAKE_ARGS ${ExternalProjectCMakeArgs}
    )

# copy liblib3rdparty.a to BUILD/lib/ directory
add_custom_command(
    TARGET libr3
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    ${PROJECT_BINARY_DIR}/BUILD/libr3/3rdparty/liblib3rdparty.a
    ${PROJECT_BINARY_DIR}/BUILD/lib/
    )

ExternalProject_Add(
    cryptonode
    DOWNLOAD_COMMAND ""
    DOWNLOAD_DIR ${PROJECT_SOURCE_DIR}
    SOURCE_DIR ${PROJECT_SOURCE_DIR}/modules/cryptonode
    BINARY_DIR ${PROJECT_BINARY_DIR}/BUILD/cryptonode
    STAMP_DIR ${PROJECT_BINARY_DIR}/STAMP/cryptonode
    TMP_DIR ${PROJECT_BINARY_DIR}/TMP/cryptonode
    INSTALL_DIR ${PROJECT_BINARY_DIR}/BUILD
    CMAKE_ARGS ${ExternalProjectCMakeArgs}
    )

set(CMAKE_CXX_STANDARD 14)

add_definitions(-DGN_ENABLE_EVENTFD=1 -DMG_USE_READ_WRITE)

set(GS_SOURCES
    ${PROJECT_SOURCE_DIR}/src/graft_manager.cpp
    ${PROJECT_SOURCE_DIR}/src/inout.cpp
    ${PROJECT_SOURCE_DIR}/src/requesttools.cpp
    ${PROJECT_SOURCE_DIR}/src/requestdefines.cpp
    ${PROJECT_SOURCE_DIR}/src/requests.cpp
    ${PROJECT_SOURCE_DIR}/src/requests/salerequest.cpp
    ${PROJECT_SOURCE_DIR}/src/requests/salestatusrequest.cpp
    ${PROJECT_SOURCE_DIR}/src/requests/rejectsalerequest.cpp
    ${PROJECT_SOURCE_DIR}/src/requests/saledetailsrequest.cpp
    ${PROJECT_SOURCE_DIR}/src/requests/payrequest.cpp
    ${PROJECT_SOURCE_DIR}/src/requests/paystatusrequest.cpp
    ${PROJECT_SOURCE_DIR}/src/requests/rejectpayrequest.cpp
    ${PROJECT_SOURCE_DIR}/src/routers_http.cpp
    ${PROJECT_SOURCE_DIR}/src/routers_coap.cpp
    ${PROJECT_SOURCE_DIR}/modules/mongoose/mongoose.c
    )

set(GS_INCLUDE_DIRS
    ${PROJECT_SOURCE_DIR}/modules/mongoose
    ${PROJECT_SOURCE_DIR}/modules/libr3/include
    ${PROJECT_SOURCE_DIR}/include/requests
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_BINARY_DIR}
    )

set(GS_LIBS
    ${PROJECT_BINARY_DIR}/BUILD/lib/libr3.a
    ${PROJECT_BINARY_DIR}/BUILD/lib/liblib3rdparty.a
    pcre
    )


set(Boost_USE_STATIC_LIBS OFF) 
set(Boost_USE_MULTITHREADED ON)  
set(Boost_USE_STATIC_RUNTIME OFF) 

find_package(Boost 1.65 QUIET REQUIRED COMPONENTS system filesystem program_options)

if(NOT Boost_FOUND)
    die("Could not find Boost libraries, please make sure you have installed Boost or libboost-all-dev (1.65) or the equivalent")
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
target_compile_definitions(graft_server PRIVATE MG_ENABLE_COAP=1)

add_dependencies(graft_server libr3 cryptonode)

##################
### tests section
if (OPT_BUILD_TESTS)
        message("==> Build tests section included")
        ExternalProject_Add(
            googletest
            DOWNLOAD_COMMAND ""
            DOWNLOAD_DIR ${PROJECT_SOURCE_DIR}
            SOURCE_DIR ${PROJECT_SOURCE_DIR}/modules/googletest
            BINARY_DIR ${PROJECT_BINARY_DIR}/BUILD/googletest
            STAMP_DIR ${PROJECT_BINARY_DIR}/STAMP/googletest
            TMP_DIR ${PROJECT_BINARY_DIR}/TMP/googletest
            INSTALL_DIR ${PROJECT_BINARY_DIR}/BUILD/googletest
            INSTALL_COMMAND ""
            )

        ExternalProject_Get_Property(googletest SOURCE_DIR)
        set(GT_INCLUDE_DIRS ${SOURCE_DIR}/googletest/include)

        ExternalProject_Get_Property(googletest BINARY_DIR)
        set(GT_LIBS
            ${BINARY_DIR}/googlemock/gtest/${CMAKE_FIND_LIBRARY_PREFIXES}gtest.a
            ${BINARY_DIR}/googlemock/gtest/${CMAKE_FIND_LIBRARY_PREFIXES}gtest_main.a
            )

        add_executable(graft_server_test
                ${GS_SOURCES}
                ${PROJECT_SOURCE_DIR}/test/graft_server_test.cpp
        )

        target_include_directories(graft_server_test PRIVATE
                ${GS_INCLUDE_DIRS}
                ${GT_INCLUDE_DIRS}
        )

        target_link_libraries(graft_server_test
                ${GS_LIBS}
                ${GT_LIBS}
                ${CMAKE_THREAD_LIBS_INIT}
        )

        target_compile_definitions(graft_server_test PRIVATE MG_ENABLE_COAP=1)
        add_dependencies(graft_server_test libr3 cryptonode googletest)
endif (OPT_BUILD_TESTS)

# copy config file to build directory
if(NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/config.ini)
    add_custom_command(
        TARGET graft_server POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_SOURCE_DIR}/data/config.ini
        ${CMAKE_CURRENT_BINARY_DIR}/config.ini)
endif()
