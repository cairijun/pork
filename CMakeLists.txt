cmake_minimum_required(VERSION 3.4)
project(Pork C CXX)

set(CMAKE_CXX_STANDARD 11)

include(utils.cmake)


set(THRIFT_LIB ${CMAKE_PROJECT_NAME}-thrift)
file(GLOB THRIFT_LIB_SRCS src/thrift/*.thrift)

set(WORKER_LIB ${CMAKE_PROJECT_NAME}-worker)
file(GLOB WORKER_LIB_SRCS src/worker/*.cc)

set(BROKER_EXE ${CMAKE_PROJECT_NAME}-broker)
file(GLOB BROKER_EXE_SRCS src/broker/*.cc)

add_thrift_library(${THRIFT_LIB} ${THRIFT_LIB_SRCS})

add_library(${WORKER_LIB} SHARED ${WORKER_LIB_SRCS})
target_link_libraries(${WORKER_LIB}
    ${THRIFT_LIB})

add_executable(${BROKER_EXE} ${BROKER_EXE_SRCS})
target_link_libraries(${BROKER_EXE}
    ${THRIFT_LIB})

install(TARGETS ${WORKER_LIB} ${BROKER_EXE}
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib)
