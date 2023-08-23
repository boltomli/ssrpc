cmake_minimum_required(VERSION 3.15)
set(PROJECT_NAME ssrpc)
project(${PROJECT_NAME} CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fsanitize=undefined")

set(DEFAULT_CMAKE_BUILD_TYPE "Release")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "No build type selected, default to ${DEFAULT_CMAKE_BUILD_TYPE}")
  set(CMAKE_BUILD_TYPE "${DEFAULT_CMAKE_BUILD_TYPE}" CACHE STRING "Build type (default ${DEFAULT_CMAKE_BUILD_TYPE})" FORCE)
endif()

set(CMAKE_THREAD_LIBS_INIT "-lpthread")
set(CMAKE_HAVE_THREADS_LIBRARY 1)
set(CMAKE_USE_WIN32_THREADS_INIT 0)
set(CMAKE_USE_PTHREADS_INIT 1)
set(THREADS_PREFER_PTHREAD_FLAG ON)

set(_GRPC_CPP_PLUGIN_EXECUTABLE /usr/bin/grpc_cpp_plugin)
set(_PROTOBUF_PROTOC /usr/bin/protoc)
set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/proto")
set(OUT_DIR ${PROTO_DIR}/generated)
if (NOT EXISTS ${OUT_DIR})
    file(MAKE_DIRECTORY ${OUT_DIR})
endif ()
FILE(GLOB PROTO_FILES "${PROTO_DIR}/*.proto")

FOREACH (PROTO_FILE_PATH ${PROTO_FILES})
  execute_process(
    COMMAND ${_PROTOBUF_PROTOC}
    --grpc_out=${OUT_DIR}
    -I=${PROTO_DIR}
    --cpp_out=${OUT_DIR}
    --plugin=protoc-gen-grpc=${_GRPC_CPP_PLUGIN_EXECUTABLE}
    ${PROTO_FILE_PATH}
  )
  execute_process(
    COMMAND srpc_generator
    protobuf ${PROTO_FILE_PATH} ${OUT_DIR}
  )
ENDFOREACH ()

find_library(SNAPPY_LIB snappy)
find_library(LZ4_LIB lz4)
find_library(WORKFLOW_LIB workflow)
find_library(SRPC_LIB srpc)

find_package(audiofile REQUIRED)
find_package(gflags REQUIRED)
find_package(glog REQUIRED)
find_package(protobuf REQUIRED)
find_package(SoundTouch REQUIRED)

add_executable(${PROJECT_NAME} main.cpp ${PROTO_DIR}/generated/sound.pb.cc)
target_link_libraries(${PROJECT_NAME}
${SNAPPY_LIB}
${LZ4_LIB}
${WORKFLOW_LIB}
${SRPC_LIB}
audiofile::audiofile
  gflags::gflags
  glog::glog
  protobuf::libprotobuf
  soundtouch::soundtouch
)
