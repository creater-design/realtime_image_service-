cmake_minimum_required(VERSION 3.18)

project(realtime_image_service LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(MUDUO_ROOT "/home/wzq/Project/muduo" CACHE PATH "Muduo source/include root")
set(MUDUO_BUILD_ROOT "/home/wzq/Project/build/release-cpp11" CACHE PATH "Muduo build root")

find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)
find_package(Threads REQUIRED)
find_package(yaml-cpp REQUIRED)

find_path(MUDUO_INCLUDE_DIR
  NAMES muduo/net/TcpServer.h
  PATHS
    ${MUDUO_ROOT}
    /usr/include
    /usr/local/include
)

find_library(MUDUO_NET_LIBRARY
  NAMES muduo_net
  PATHS
    ${MUDUO_BUILD_ROOT}/lib
    /usr/lib
    /usr/local/lib
    /usr/lib/x86_64-linux-gnu
)

find_library(MUDUO_BASE_LIBRARY
  NAMES muduo_base
  PATHS
    ${MUDUO_BUILD_ROOT}/lib
    /usr/lib
    /usr/local/lib
    /usr/lib/x86_64-linux-gnu
)

if(NOT MUDUO_INCLUDE_DIR)
  message(FATAL_ERROR "Muduo headers not found. Set MUDUO_ROOT or install muduo.")
endif()

if(NOT MUDUO_NET_LIBRARY OR NOT MUDUO_BASE_LIBRARY)
  message(FATAL_ERROR "Muduo libraries not found. Set MUDUO_BUILD_ROOT or install muduo.")
endif()

message(STATUS "Muduo include: ${MUDUO_INCLUDE_DIR}")
message(STATUS "muduo_net: ${MUDUO_NET_LIBRARY}")
message(STATUS "muduo_base: ${MUDUO_BASE_LIBRARY}")

add_library(realtime_common
  src/common/protocol.cpp
  src/common/image_codec.cpp
  src/common/depth_estimator.cpp
  src/common/result_json.cpp
  src/common/result_payload.cpp
)

target_include_directories(realtime_common
  PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(realtime_common
  PUBLIC
    ${OpenCV_LIBS}
)

add_executable(image_server
  src/server/image_server_config.cpp
  src/server/image_tcp_server.cpp
  src/server/server_main.cpp
)

target_include_directories(image_server
  PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${MUDUO_INCLUDE_DIR}
)

target_link_libraries(image_server
  PRIVATE
    realtime_common
    ${MUDUO_NET_LIBRARY}
    ${MUDUO_BASE_LIBRARY}
    yaml-cpp
    Threads::Threads
)

install(
  TARGETS
    image_server
    realtime_common
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)

install(
  DIRECTORY include/
  DESTINATION include
)

install(
  DIRECTORY config/
  DESTINATION config
)
