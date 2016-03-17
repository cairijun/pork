cmake_minimum_required(VERSION 3.4)
project(Pork C CXX)

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_STANDARD 11)

set(CMAKE_THREAD_PREFER_PTHREAD ON)
find_package(Threads REQUIRED)

include(CTest)
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
    COMPONENTS chrono system thread log program_options)

set(${CMAKE_PROJECT_NAME}_INCLUDE_DIR src/include)

include_directories(
    ${ZooKeeper_INCLUDE_DIR}
    ${Boost_INCLUDE_DIR}
    ${${CMAKE_PROJECT_NAME}_INCLUDE_DIR})


set(THRIFT_LIB ${CMAKE_PROJECT_NAME}-thrift)
join_paths(THRIFT_LIB_SRCS src/thrift proto.thrift internal.thrift)

set(WORKER_LIB ${CMAKE_PROJECT_NAME}-worker)
join_paths(WORKER_LIB_SRCS src/worker worker.cc)

set(BROKER_EXE ${CMAKE_PROJECT_NAME}-broker)
set(BROKER_LIB ${BROKER_EXE}-lib)
join_paths(BROKER_LIB_SRCS src/broker
    message_queue.cc
    broker_handler.cc
    broker_internal.cc)

add_thrift_library(${THRIFT_LIB} ${THRIFT_LIB_SRCS})

add_library(${WORKER_LIB} STATIC ${WORKER_LIB_SRCS})
target_link_libraries(${WORKER_LIB}
    Threads::Threads
    ${Boost_LIBRARIES}
    ${THRIFT_LIB}
    ${ZooKeeper_LIB})

add_library(${BROKER_LIB} STATIC ${BROKER_LIB_SRCS})
target_link_libraries(${BROKER_LIB}
    Threads::Threads
    ${Boost_LIBRARIES}
    ${THRIFT_LIB}
    ${ZooKeeper_LIB})

add_executable(${BROKER_EXE} src/broker/broker.cc)
target_link_libraries(${BROKER_EXE} ${BROKER_LIB})

install(TARGETS ${WORKER_LIB} ${BROKER_EXE}
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib)


add_subdirectory(tests)
