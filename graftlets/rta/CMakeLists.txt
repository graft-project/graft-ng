cmake_minimum_required(VERSION 3.5)

project(graftlet_rta)

set (CMAKE_CXX_STANDARD 17)

if(NOT ("${GRAFTLETS_OUTPUT_DIR}" EQUAL ""))
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${GRAFTLETS_OUTPUT_DIR}")
endif(NOT ("${GRAFTLETS_OUTPUT_DIR}" EQUAL ""))

find_package(Boost 1.65 QUIET REQUIRED COMPONENTS system filesystem program_options serialization thread regex)

if(NOT Boost_FOUND)
    die("Could not find Boost libraries, please make sure you have installed Boost or libboost-all-dev (1.65) or the equivalent")
elseif(Boost_FOUND)
    message(STATUS "Found Boost Version: ${Boost_VERSION}")
endif()

include_directories(SYSTEM ${Boost_INCLUDE_DIRS})

######## Graftlets

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/requests/include
    ${PROJECT_SOURCE_DIR}/../../modules/libr3/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(graftlet_rta SHARED
    RTA.cpp
    requests/presale.cpp
    requests/sale.cpp
    requests/common.cpp
    requests/storepaymentdata.cpp
    requests/paymentdatarequest.cpp
    requests/paymentdataresponse.cpp
    requests/getpaymentdata.cpp
    requests/pay.cpp
)
set_target_properties(graftlet_walletAddress PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/supernode
)

