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
        if(${src} IS_NEWER_THAN ${TIMESTAMP_FILE})
            message(STATUS "Generating thrift code for ${src}")
            execute_process(
                COMMAND ${THRIFT_BIN} -out ${THRIFT_OUTPUT_DIR} --gen cpp ${src})
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
