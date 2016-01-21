cmake_minimum_required(VERSION 3.4)
project(Pork C CXX)

set(CMAKE_CXX_STANDARD 11)

include(utils.cmake)


# Zookeeper
find_library(ZooKeeper_LIB zookeeper_mt)
find_path(ZooKeeper_INCLUDE_DIR NAMES zookeeper.h zookeeper/zookeeper.h)
if(ZooKeeper_LIB STREQUAL "ZooKeeper_LIB-NOTFOUND")
    message(SEND_ERROR "Failed to find ZooKeeper library!")
else()
    message(STATUS "Found ZooKeeper library: ${ZooKeeper_LIB}")
endif()
if(ZooKeeper_INCLUDE_DIR STREQUAL "ZooKeeper_INCLUDE_DIR-NOTFOUND")
    message(SEND_ERROR "Failed to find ZooKeeper header files")
else()
    message(STATUS "Found ZooKeeper header files directory: ${ZooKeeper_INCLUDE_DIR}")
endif()

# Boost
set(Boost_USE_MULTITHREADED ON)
find_package(Boost 1.53.0 REQUIRED
    COMPONENTS chrono system thread)

set(${CMAKE_PROJECT_NAME}_INCLUDE_DIR src/include)

include_directories(
    ${ZooKeeper_INCLUDE_DIR}
    ${Boost_INCLUDE_DIR}
    ${${CMAKE_PROJECT_NAME}_INCLUDE_DIR})


set(THRIFT_LIB ${CMAKE_PROJECT_NAME}-thrift)
file(GLOB THRIFT_LIB_SRCS src/thrift/*.thrift)

set(WORKER_LIB ${CMAKE_PROJECT_NAME}-worker)
file(GLOB WORKER_LIB_SRCS src/worker/*.cc)

set(BROKER_EXE ${CMAKE_PROJECT_NAME}-broker)
file(GLOB BROKER_EXE_SRCS src/broker/*.cc)

add_thrift_library(${THRIFT_LIB} ${THRIFT_LIB_SRCS})

add_library(${WORKER_LIB} SHARED ${WORKER_LIB_SRCS})
target_link_libraries(${WORKER_LIB}
    ${THRIFT_LIB}
    ${ZooKeeper_LIB})

add_executable(${BROKER_EXE} ${BROKER_EXE_SRCS})
target_link_libraries(${BROKER_EXE}
    ${Boost_LIBRARIES}
    ${THRIFT_LIB}
    ${ZooKeeper_LIB})

install(TARGETS ${WORKER_LIB} ${BROKER_EXE}
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib)
