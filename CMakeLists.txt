cmake_minimum_required(VERSION 3.10)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
   set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)

if(CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)
    message("==> The configuration is ${CMAKE_BUILD_TYPE}. Debug info will be extracted into separate files.")

    function (add_executable _name)
        _add_executable(${ARGV})

        if (TARGET ${_name})
            add_custom_command(TARGET ${_name} POST_BUILD
                COMMAND echo "$<TARGET_FILE_NAME:${_name}>: extracting debug info"
                COMMAND ${CMAKE_COMMAND} -E chdir $<TARGET_FILE_DIR:${_name}> objcopy --only-keep-debug "$<TARGET_FILE_NAME:${_name}>" "$<TARGET_FILE_NAME:${_name}>.debug"
                COMMAND ${CMAKE_COMMAND} -E chdir $<TARGET_FILE_DIR:${_name}> strip --strip-debug --strip-unneeded "$<TARGET_FILE_NAME:${_name}>"
                COMMAND ${CMAKE_COMMAND} -E chdir $<TARGET_FILE_DIR:${_name}> objcopy --add-gnu-debuglink="$<TARGET_FILE_NAME:${_name}>.debug" "$<TARGET_FILE_NAME:${_name}>"
            )
        endif()
    endfunction()

    function (add_library _name _type)
        _add_library(${ARGV})

        if (TARGET ${_name} AND ${_type} STREQUAL SHARED)
            add_custom_command(TARGET ${_name} POST_BUILD
                COMMAND echo "$<TARGET_FILE_NAME:${_name}>: extracting debug info"
                COMMAND ${CMAKE_COMMAND} -E chdir $<TARGET_FILE_DIR:${_name}> objcopy --only-keep-debug "$<TARGET_FILE_NAME:${_name}>" "$<TARGET_FILE_NAME:${_name}>.debug"
                COMMAND ${CMAKE_COMMAND} -E chdir $<TARGET_FILE_DIR:${_name}> strip --strip-debug --strip-unneeded "$<TARGET_FILE_NAME:${_name}>"
                COMMAND ${CMAKE_COMMAND} -E chdir $<TARGET_FILE_DIR:${_name}> objcopy --add-gnu-debuglink="$<TARGET_FILE_NAME:${_name}>.debug" "$<TARGET_FILE_NAME:${_name}>"
            )
        endif()
    endfunction()

endif(CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)

set(CMAKE_C_FLAGS "-march=x86-64")
set(CMAKE_CXX_FLAGS "-march=x86-64")

project(graft_server)

option(OPT_BUILD_TESTS "Build tests." OFF)
option(ENABLE_SYSLOG "SYSLOG support. It can be compiled for UNIX-like platforms only." OFF)
option(STATIC_LINK "Link executables and libraries statically" ON)

if(NOT DEFINED CMAKE_ROOT_SOURCE_DIR)
    # CMAKE_ROOT_SOURCE_DIR variable is required, because CMAKE_SOURCE_DIR works well for include like commands and does not for external projects
    set(CMAKE_ROOT_SOURCE_DIR "${CMAKE_SOURCE_DIR}")
endif()

find_package(Threads REQUIRED)

include(ExternalProject)
include(version.cmake)

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
    CMAKE_ARGS ${ExternalProjectCMakeArgs} -DSTATIC=ON -DARCH=x86-64 -DHAVE_EVP_MD_CTX_NEW=YES -DENABLE_SYSLOG=${ENABLE_SYSLOG} -DCMAKE_ROOT_SOURCE_DIR="\"${CMAKE_ROOT_SOURCE_DIR}\""
    )

set(CMAKE_CXX_STANDARD 17)

add_definitions(-DGN_ENABLE_EVENTFD=1 -DMG_USE_READ_WRITE)

if(STATIC_LINK)
    set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")
    set(CMAKE_SHARED_LINKER_FLAGS "-static-libgcc -static-libstdc++")
    set(CMAKE_MODULE_LINKER_FLAGS "-static-libgcc -static-libstdc++")
endif(STATIC_LINK)

set(GRAFT_INCLUDE_DIRS
    ${PROJECT_SOURCE_DIR}/modules/libr3/include
    ${PROJECT_SOURCE_DIR}/include
    )

set(CRYPTONODE_INCLUDE_DIRS
    ${PROJECT_SOURCE_DIR}/modules/cryptonode/contrib/epee/include
    ${PROJECT_SOURCE_DIR}/modules/cryptonode/external/rapidjson/include
    ${PROJECT_SOURCE_DIR}/modules/cryptonode/external/easylogging++
    ${PROJECT_SOURCE_DIR}/modules/cryptonode/external
    ${PROJECT_SOURCE_DIR}/modules/cryptonode/src
    )

set(CRYPTONODE_LIB_DIRS
    ${PROJECT_BINARY_DIR}/BUILD/cryptonode/contrib/epee/src
    ${PROJECT_BINARY_DIR}/BUILD/cryptonode/external/easylogging++
    ${PROJECT_BINARY_DIR}/BUILD/cryptonode/external/unbound
    ${PROJECT_BINARY_DIR}/BUILD/cryptonode/external/db_drivers/liblmdb
    ${PROJECT_BINARY_DIR}/BUILD/cryptonode/external/randomx
    ${PROJECT_BINARY_DIR}/BUILD/cryptonode/lib
    ${PROJECT_BINARY_DIR}/BUILD/cryptonode/src/blocks
    ${PROJECT_BINARY_DIR}/BUILD/cryptonode/src/blockchain_db
    ${PROJECT_BINARY_DIR}/BUILD/cryptonode/src/common
    ${PROJECT_BINARY_DIR}/BUILD/cryptonode/src/crypto
    ${PROJECT_BINARY_DIR}/BUILD/cryptonode/src/cryptonote_basic
    ${PROJECT_BINARY_DIR}/BUILD/cryptonode/src/cryptonote_core
    ${PROJECT_BINARY_DIR}/BUILD/cryptonode/src/mnemonics
    ${PROJECT_BINARY_DIR}/BUILD/cryptonode/src/ringct
    ${PROJECT_BINARY_DIR}/BUILD/cryptonode/src/wallet
    ${PROJECT_BINARY_DIR}/BUILD/cryptonode/src/utils
    ${PROJECT_BINARY_DIR}/BUILD/cryptonode/src/multisig
    ${PROJECT_BINARY_DIR}/BUILD/cryptonode/src/device
    ${PROJECT_BINARY_DIR}/BUILD/cryptonode/src/checkpoints

    )

set(CRYPTONODE_LIBS
    easylogging
    wallet
    utils
    mnemonics
    epee
    cryptonote_core
    cryptonote_basic
    cncrypto
    multisig
    ringct
    ringct_basic
    common
    unbound
    device
    sodium
    checkpoints
    lmdb
    randomx
    blocks
    blockchain_db
    )

if(STATIC_LINK)
    set(COMMON_LIBS
        libssl.a
        libcrypto.a
        libreadline.a
        )
else(STATIC_LINK)
    set(COMMON_LIBS
        ssl
        crypto
        readline
        )
endif(STATIC_LINK)

if(STATIC_LINK)
    set(Boost_USE_STATIC_LIBS ON)
    #set(Boost_USE_MULTITHREADED ON)
    set(Boost_USE_STATIC_RUNTIME ON)
endif(STATIC_LINK)

find_package(Boost 1.65 QUIET REQUIRED COMPONENTS system chrono filesystem program_options serialization thread regex)

if(NOT Boost_FOUND)
    message(FATAL_ERROR "Could not find Boost libraries, please make sure you have installed Boost or libboost-all-dev (1.65) or the equivalent")
elseif(Boost_FOUND)
    message(STATUS "Found Boost Version: ${Boost_VERSION}")
endif()

include_directories(SYSTEM ${Boost_INCLUDE_DIRS})
# cryptonode dependencies
# TODO: modify cryptonode CMakeLists.txt so it installs all required headers/libs
include_directories("${CMAKE_BINARY_DIR}/version" ${CRYPTONODE_INCLUDE_DIRS})
link_directories(${CRYPTONODE_LIB_DIRS})

### graftlet_lib
add_library(graftlet_lib STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lib/graft/GraftletLoader.cpp
    )

target_include_directories(graftlet_lib PRIVATE
    ${GRAFT_INCLUDE_DIRS}
    )

if(UNIX)
    target_link_libraries(graftlet_lib PRIVATE
        dl
        )
endif(UNIX)

set_target_properties(graftlet_lib PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/BUILD/graftlets
    )

set(GRAFTLETS_OUTPUT_DIR ${PROJECT_BINARY_DIR}/graftlets)
add_subdirectory(graftlets ${PROJECT_BINARY_DIR}/BUILD/graftlets)

### graft library
add_library(graft STATIC
    ${PROJECT_SOURCE_DIR}/src/lib/graft/common/utils.cpp
    ${PROJECT_SOURCE_DIR}/src/lib/graft/backtrace.cpp
    ${PROJECT_SOURCE_DIR}/src/lib/graft/blacklist.cpp
    ${PROJECT_SOURCE_DIR}/src/lib/graft/connection.cpp
    ${PROJECT_SOURCE_DIR}/src/lib/graft/context.cpp
    ${PROJECT_SOURCE_DIR}/src/lib/graft/inout.cpp
    ${PROJECT_SOURCE_DIR}/src/lib/graft/log.cpp
    ${PROJECT_SOURCE_DIR}/src/lib/graft/mongoosex.cpp
    ${PROJECT_SOURCE_DIR}/src/lib/graft/router.cpp
    ${PROJECT_SOURCE_DIR}/src/lib/graft/task.cpp
    ${PROJECT_SOURCE_DIR}/modules/mongoose/mongoose.c
    ${PROJECT_SOURCE_DIR}/src/supernode/server.cpp
    ${PROJECT_SOURCE_DIR}/src/supernode/supernode.cpp
    ${PROJECT_SOURCE_DIR}/src/lib/graft/sys_info.cpp
    ${PROJECT_SOURCE_DIR}/src/lib/graft/sys_info_request.cpp
    )

target_include_directories(graft PRIVATE
    ${PROJECT_SOURCE_DIR}/modules/mongoose
    ${GRAFT_INCLUDE_DIRS}
    )

if(STATIC_LINK)
    set(PCRE_LIB libpcre.a)
else(STATIC_LINK)
    set(PCRE_LIB pcre)
endif(STATIC_LINK)

target_link_libraries(graft PRIVATE
    ${PROJECT_BINARY_DIR}/BUILD/lib/libr3.a
    ${PROJECT_BINARY_DIR}/BUILD/lib/liblib3rdparty.a
    graftlet_lib
    easylogging
    ${PCRE_LIB}
    ${CMAKE_THREAD_LIBS_INIT}
    ${Boost_PROGRAM_OPTIONS_LIBRARY}
    ${Boost_FILESYSTEM_LIBRARY}
    )

set_target_properties(graft PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/BUILD/libgraft
    )

target_compile_definitions(graft PRIVATE MG_ENABLE_COAP=1 -DMONERO_DEFAULT_LOG_CATEGORY="supernode")
if(ENABLE_SYSLOG)
    target_compile_definitions(graft PRIVATE -DELPP_SYSLOG)
endif()

set_target_properties(graft PROPERTIES LINK_FLAGS "-Wl,-E -rdynamic")

add_dependencies(graft version libr3 cryptonode graftlet_lib)

### requests_common library
add_library(requests_common STATIC
    ${PROJECT_SOURCE_DIR}/src/lib/graft/requests.cpp
    ${PROJECT_SOURCE_DIR}/src/lib/graft/requesttools.cpp
    ${PROJECT_SOURCE_DIR}/src/lib/graft/requests/requestdefines.cpp
    ${PROJECT_SOURCE_DIR}/src/lib/graft/requests/health_check.cpp
    )

target_include_directories(requests_common PRIVATE
    ${GRAFT_INCLUDE_DIRS}
    )

target_link_libraries(requests_common PRIVATE
    graft
    )

set_target_properties(requests_common PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/BUILD/requests_common
    )

if(ENABLE_SYSLOG)
    target_compile_definitions(requests_common PRIVATE -DELPP_SYSLOG)
endif()

add_dependencies(requests_common graft cryptonode)

### supernode_common library
add_library(supernode_common STATIC
    ${PROJECT_SOURCE_DIR}/src/supernode/requestdefines.cpp
    ${PROJECT_SOURCE_DIR}/src/supernode/requests.cpp
    ${PROJECT_SOURCE_DIR}/src/supernode/requests/authorize_rta_tx.cpp
    ${PROJECT_SOURCE_DIR}/src/supernode/requests/debug.cpp
    ${PROJECT_SOURCE_DIR}/src/supernode/requests/forward.cpp
    ${PROJECT_SOURCE_DIR}/src/supernode/requests/get_info.cpp
    ${PROJECT_SOURCE_DIR}/src/supernode/requests/pay.cpp
    ${PROJECT_SOURCE_DIR}/src/supernode/requests/pay_status.cpp
    ${PROJECT_SOURCE_DIR}/src/supernode/requests/reject_pay.cpp
    ${PROJECT_SOURCE_DIR}/src/supernode/requests/reject_sale.cpp
    ${PROJECT_SOURCE_DIR}/src/supernode/requests/sale_details.cpp
    ${PROJECT_SOURCE_DIR}/src/supernode/requests/sale.cpp
    ${PROJECT_SOURCE_DIR}/src/supernode/requests/sale_status.cpp
    ${PROJECT_SOURCE_DIR}/src/supernode/requests/send_raw_tx.cpp
    ${PROJECT_SOURCE_DIR}/src/supernode/requests/send_supernode_announce.cpp
    ${PROJECT_SOURCE_DIR}/src/supernode/requests/send_supernode_stakes.cpp
    ${PROJECT_SOURCE_DIR}/src/supernode/requests/send_transfer.cpp
    ${PROJECT_SOURCE_DIR}/src/supernode/requests/blockchain_based_list.cpp
    ${PROJECT_SOURCE_DIR}/src/rta/DaemonRpcClient.cpp
    ${PROJECT_SOURCE_DIR}/src/rta/fullsupernodelist.cpp
    ${PROJECT_SOURCE_DIR}/src/rta/supernode.cpp
    )

target_include_directories(supernode_common PRIVATE
    ${GRAFT_INCLUDE_DIRS}
    )

  target_link_libraries(supernode_common PUBLIC
    ${CRYPTONODE_LIBS}
    ${CMAKE_THREAD_LIBS_INIT}
    ${Boost_PROGRAM_OPTIONS_LIBRARY}
    ${Boost_CHRONO_LIBRARY}
    ${Boost_FILESYSTEM_LIBRARY}
    ${Boost_SYSTEM_LIBRARY}
    ${Boost_SERIALIZATION_LIBRARY}
    ${Boost_THREAD_LIBRARY}
    ${Boost_REGEX_LIBRARY}
    ${COMMON_LIBS}
    requests_common
    graft
    )

set_target_properties(supernode_common PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/BUILD/supernode_common
    )

target_compile_definitions(supernode_common PRIVATE MG_ENABLE_COAP=1 -DMONERO_DEFAULT_LOG_CATEGORY="supernode")
if(ENABLE_SYSLOG)
    target_compile_definitions(supernode_common PRIVATE -DELPP_SYSLOG)
endif()

add_dependencies(supernode_common requests_common graft cryptonode)

### supernode
add_executable(supernode
    ${PROJECT_SOURCE_DIR}/src/supernode/main.cpp
    )

set_target_properties(supernode PROPERTIES LINK_FLAGS "-Wl,-E")

target_include_directories(supernode PRIVATE
    ${GRAFT_INCLUDE_DIRS}
    )

target_link_libraries(supernode PRIVATE
    graft
    supernode_common
    )

add_dependencies(supernode graft supernode_common)

### wallet_server
add_executable(wallet_server
    ${PROJECT_SOURCE_DIR}/src/walletnode/main.cpp
    ${PROJECT_SOURCE_DIR}/src/walletnode/requests/wallet_requests.cpp
    ${PROJECT_SOURCE_DIR}/src/walletnode/server.cpp
    ${PROJECT_SOURCE_DIR}/src/walletnode/wallet_manager.cpp
    )

target_include_directories(wallet_server PRIVATE
    ${GRAFT_INCLUDE_DIRS}
    )

target_link_libraries(wallet_server PRIVATE
  #${CRYPTONODE_LIBS}
    #${CMAKE_THREAD_LIBS_INIT}
   ${COMMON_LIBS}
    graft
    requests_common
    supernode_common
    )

add_dependencies(wallet_server graft requests_common)

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

        add_executable(supernode_test
            ${PROJECT_SOURCE_DIR}/test/upstream_test.cpp
            ${PROJECT_SOURCE_DIR}/test/blacklist_test.cpp
            ${PROJECT_SOURCE_DIR}/test/graft_server_test.cpp
            ${PROJECT_SOURCE_DIR}/test/graftlets_test.cpp
            ${PROJECT_SOURCE_DIR}/test/thread_pool_test.cpp
            ${PROJECT_SOURCE_DIR}/test/json_rpc_test.cpp
            ${PROJECT_SOURCE_DIR}/test/json_test.cpp
            ${PROJECT_SOURCE_DIR}/test/cryptonode_handlers_test.cpp
            ${PROJECT_SOURCE_DIR}/test/rta_classes_test.cpp
            ${PROJECT_SOURCE_DIR}/test/sys_info.cpp
            ${PROJECT_SOURCE_DIR}/test/strand_test.cpp
            ${PROJECT_SOURCE_DIR}/test/main.cpp
        )

        target_include_directories(supernode_test PRIVATE
            ${PROJECT_SOURCE_DIR}/modules/mongoose
            ${GRAFT_INCLUDE_DIRS}
            ${GT_INCLUDE_DIRS}
            ${CRYPTONODE_INCLUDES}
        )

        target_link_libraries(supernode_test PRIVATE
            ${GT_LIBS}
            graft
            supernode_common
            )


        target_compile_definitions(supernode_test PRIVATE MG_ENABLE_COAP=1)
        add_dependencies(supernode_test graft supernode_common googletest)
        set_target_properties(supernode_test PROPERTIES LINK_FLAGS "-Wl,-E")
        if(ENABLE_SYSLOG)
            target_compile_definitions(supernode_test PRIVATE -DELPP_SYSLOG)
        endif()

        if(NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/test_wallet.keys)
            add_custom_command(
                TARGET supernode_test POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_SOURCE_DIR}/data/test_wallet.keys
                ${CMAKE_CURRENT_BINARY_DIR}/test_wallet.keys)
        endif()

endif (OPT_BUILD_TESTS)

# copy config file to build directory
if(NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/config.ini)
    add_custom_command(
        TARGET supernode POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_SOURCE_DIR}/data/*
        ${CMAKE_CURRENT_BINARY_DIR}/)
endif()

