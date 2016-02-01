macro(join_paths var dir)
    foreach(_file ${ARGN})
        get_filename_component(_full_path ${_file} ABSOLUTE BASE_DIR ${dir})
        list(APPEND ${var} ${_full_path})
    endforeach()
endmacro()


# thrift related stuff
find_package(PkgConfig REQUIRED)

pkg_check_modules(THRIFT REQUIRED thrift)
pkg_get_variable(THRIFT_EXEC_PREFIX thrift exec_prefix)
find_program(THRIFT_BIN thrift ${THRIFT_EXEC_PREFIX}/bin)
foreach(lib ${THRIFT_LIBRARIES})
    find_library(lib_full_path ${lib} PATHS ${THRIFT_LIBRARY_DIRS} NO_DEFAULT_PATH)
    list(APPEND _THRIFT_LIBRARIES ${lib_full_path})
endforeach()

macro(add_thrift_library lib_name)
    set(THRIFT_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/thrift)
    file(MAKE_DIRECTORY ${THRIFT_OUTPUT_DIR})

    set(TIMESTAMP_FILE ${THRIFT_OUTPUT_DIR}/_${lib_name}_generated_timestamp)

    foreach(src ${ARGN})
        set(_src ${CMAKE_CURRENT_SOURCE_DIR}/${src})
        if(${_src} IS_NEWER_THAN ${TIMESTAMP_FILE})
            message(STATUS "Generating thrift code for ${src}")
            execute_process(
                COMMAND ${THRIFT_BIN} -out ${THRIFT_OUTPUT_DIR} --gen cpp ${_src})
            set(GENERATED 1)
        endif()
    endforeach()

    if(GENERATED)
        string(TIMESTAMP GENERATE_TIME UTC)
        file(WRITE ${TIMESTAMP_FILE}
            "Source code for thrift library ${lib_name} was generated at ${GENERATE_TIME}.\n")
    endif()

    file(GLOB THRIFT_SKELETON_FILES ${THRIFT_OUTPUT_DIR}/*.skeleton.cpp)
    if(THRIFT_SKELETON_FILES)
        file(REMOVE ${THRIFT_SKELETON_FILES})
    endif()

    file(GLOB THRIFT_GENERATED_SRCS ${THRIFT_OUTPUT_DIR}/*.cpp)
    add_library(${lib_name} STATIC ${THRIFT_GENERATED_SRCS})
    set_target_properties(${lib_name} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_link_libraries(${lib_name} INTERFACE
        ${_THRIFT_LIBRARIES})
    target_compile_options(${lib_name} INTERFACE
        ${THRIFT_CFLAGS_OTHER})
    target_include_directories(${lib_name} INTERFACE
        ${THRIFT_INCLUDE_DIRS} ${THRIFT_OUTPUT_DIR})
endmacro()


# GTest/GMock
if(BUILD_TESTING)
    include(ExternalProject)
    ExternalProject_Add(googletest
      GIT_REPOSITORY    https://github.com/google/googletest.git
      GIT_TAG           82b11b8cfcca464c2ac74b623d04e74452e74f32
      SOURCE_DIR        "${CMAKE_BINARY_DIR}/googletest-src"
      BINARY_DIR        "${CMAKE_BINARY_DIR}/googletest-build"
      INSTALL_COMMAND   ""
    )

    ExternalProject_Get_Property(googletest BINARY_DIR SOURCE_DIR)
    set(GMOCK_BIN_DIR ${BINARY_DIR}/googlemock)
    set(GMOCK_SRC_DIR ${SOURCE_DIR}/googlemock)
    set(GTEST_BIN_DIR ${GMOCK_BIN_DIR}/gtest)
    set(GTEST_SRC_DIR ${SOURCE_DIR}/googletest)
    add_library(gmock UNKNOWN IMPORTED)
    add_library(gmock_main UNKNOWN IMPORTED)
    set(GMOCK_LIB
        ${GMOCK_BIN_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}gmock${CMAKE_STATIC_LIBRARY_SUFFIX})
    set(GMOCK_MAIN_LIB
        ${GMOCK_BIN_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}gmock_main${CMAKE_STATIC_LIBRARY_SUFFIX})
    set_target_properties(gmock PROPERTIES IMPORTED_LOCATION ${GMOCK_LIB})
    set_target_properties(gmock_main PROPERTIES IMPORTED_LOCATION ${GMOCK_MAIN_LIB})
    add_dependencies(gmock googletest)
    add_dependencies(gmock_main googletest)

    macro(add_gtest_target target srcs)
        add_executable(${target} ${srcs})
        target_link_libraries(${target} gmock gmock_main)
        target_include_directories(${target} SYSTEM
            PUBLIC ${GMOCK_SRC_DIR}/include
            PUBLIC ${GTEST_SRC_DIR}/include)
        add_test(NAME ${target} COMMAND ${target})
    endmacro()
else()
    macro(add_gtest_target target srcs)
        message(STATUS "Test target: ${target} skipped")
    endmacro()
endif()
