cmake_minimum_required(VERSION 3.10)

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
    CMAKE_ARGS ${ExternalProjectCMakeArgs} -DBUILD_SHARED_LIBS=ON # TODO: fix so cryptonode's libs can be statically linked
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
    ${PROJECT_SOURCE_DIR}/src/requests/getinforequest.cpp
    ${PROJECT_SOURCE_DIR}/src/requests/sendrawtxrequest.cpp
    ${PROJECT_SOURCE_DIR}/src/routers_http.cpp
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
    dl
    )


set(CRYPTONODE_INCLUDE_DIRS
    ${PROJECT_SOURCE_DIR}/modules/cryptonode/contrib/epee/include
    ${PROJECT_SOURCE_DIR}/modules/cryptonode/external/easylogging++
    ${PROJECT_SOURCE_DIR}/modules/cryptonode/external
    ${PROJECT_SOURCE_DIR}/modules/cryptonode/src
    )

set(CRYPTONODE_LIB_DIRS
    ${PROJECT_BINARY_DIR}/BUILD/cryptonode/contrib/epee/src
    ${PROJECT_BINARY_DIR}/BUILD/cryptonode/external/easylogging++
    ${PROJECT_BINARY_DIR}/BUILD/cryptonode/lib
    ${PROJECT_BINARY_DIR}/BUILD/cryptonode/src/common
    ${PROJECT_BINARY_DIR}/BUILD/cryptonode/src/crypto
    ${PROJECT_BINARY_DIR}/BUILD/cryptonode/src/cryptonote_basic
    ${PROJECT_BINARY_DIR}/BUILD/cryptonode/src/cryptonote_core
    ${PROJECT_BINARY_DIR}/BUILD/cryptonode/src/mnemonics
    ${PROJECT_BINARY_DIR}/BUILD/cryptonode/src/ringct
    ${PROJECT_BINARY_DIR}/BUILD/cryptonode/src/wallet
    )

set(CRYPTONODE_LIBS
    easylogging
    epee
    cryptonote_core
    cryptonote_basic
    cncrypto
    wallet # if linked statically: once we add any call to this library from source code, weird linker errors happening
    mnemonics
    ringct
    common
    )

set(COMMON_LIBS
    readline
    )

#set(Boost_USE_STATIC_LIBS ON)
#set(Boost_USE_MULTITHREADED ON)
#set(Boost_USE_STATIC_RUNTIME ON)

find_package(Boost 1.65 QUIET REQUIRED COMPONENTS system filesystem program_options serialization thread)

if(NOT Boost_FOUND)
    die("Could not find Boost libraries, please make sure you have installed Boost or libboost-all-dev (1.65) or the equivalent")
elseif(Boost_FOUND)
    message(STATUS "Found Boost Version: ${Boost_VERSION}")
endif()

include_directories(SYSTEM ${Boost_INCLUDE_DIRS})
# cryptonode dependencies
# TODO: modify cryptonode CMakeLists.txt so it installs all required headers/libs
include_directories(${CRYPTONODE_INCLUDE_DIRS})
link_directories(${CRYPTONODE_LIB_DIRS})

add_executable(graft_server
    ${GS_SOURCES}
    ${PROJECT_SOURCE_DIR}/src/main.cpp
    )


target_include_directories(graft_server PRIVATE
    ${GS_INCLUDE_DIRS}
    )

target_link_libraries(graft_server PRIVATE
    ${CRYPTONODE_LIBS}
    ${GS_LIBS}
    ${CMAKE_THREAD_LIBS_INIT}
    ${Boost_PROGRAM_OPTIONS_LIBRARY}
    ${Boost_FILESYSTEM_LIBRARY}
    ${Boost_SYSTEM_LIBRARY}
    ${Boost_SERIALIZATION_LIBRARY}
    ${Boost_THREAD_LIBRARY}
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
            )

        add_executable(graft_server_test
                ${GS_SOURCES}
                ${PROJECT_SOURCE_DIR}/test/graft_server_test.cpp
                ${PROJECT_SOURCE_DIR}/test/json_rpc_test.cpp
                ${PROJECT_SOURCE_DIR}/test/json_test.cpp
                ${PROJECT_SOURCE_DIR}/test/cryptonode_handlers_test.cpp
                ${PROJECT_SOURCE_DIR}/test/main.cpp
        )

        target_include_directories(graft_server_test PRIVATE
                ${GS_INCLUDE_DIRS}
                ${GT_INCLUDE_DIRS}
                ${CRYPTONODE_INCLUDES}

        )

        target_link_libraries(graft_server_test
            PRIVATE
            ${CRYPTONODE_LIBS}
            ${GT_LIBS}
            ${GS_LIBS}
            ${Boost_LIBRARIES}
            )


        target_compile_definitions(graft_server_test PRIVATE MG_ENABLE_COAP=1)
        add_dependencies(graft_server_test libr3 cryptonode googletest)

        if(NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/test_wallet.keys)
            add_custom_command(
                TARGET graft_server POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_SOURCE_DIR}/data/test_wallet.keys
                ${CMAKE_CURRENT_BINARY_DIR}/test_wallet.keys)
        endif()

endif (OPT_BUILD_TESTS)

# copy config file to build directory
if(NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/config.ini)
    add_custom_command(
        TARGET graft_server POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_SOURCE_DIR}/data/config.ini
        ${CMAKE_CURRENT_BINARY_DIR}/config.ini)
endif()
