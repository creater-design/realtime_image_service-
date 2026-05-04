cmake_minimum_required(VERSION 3.18)

project(realtime_image_service LANGUAGES CXX)

# ============================================================
# 1. C++ 基础设置
# ============================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 如果用户没有指定 Debug / Release，默认用 Release
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

message(STATUS "Project name: ${PROJECT_NAME}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ compiler: ${CMAKE_CXX_COMPILER}")

# ============================================================
# 2. 编译选项开关
# ============================================================

# 是否编译 CUDA 图像处理模块
# 开启：cmake -S . -B build -DBUILD_WITH_CUDA=ON
# 关闭：cmake -S . -B build -DBUILD_WITH_CUDA=OFF
option(BUILD_WITH_CUDA "Build CUDA image processor" ON)

# Muduo 路径
# 这里给默认值，但是也可以用 -D 从命令行覆盖
set(MUDUO_ROOT "/home/wzq/Project/muduo" CACHE PATH "Muduo source root")
set(MUDUO_BUILD_ROOT "/home/wzq/Project/build/release-cpp11" CACHE PATH "Muduo build root containing lib/")

# CUDA 宿主编译器
# CUDA 有时候不兼容系统默认 g++，可以手动指定 g++-10
# 例如：
# cmake -S . -B build -DBUILD_WITH_CUDA=ON -DCUDA_HOST_COMPILER_OVERRIDE=/usr/bin/g++-10
set(CUDA_HOST_COMPILER_OVERRIDE "" CACHE FILEPATH "Optional host compiler for nvcc, e.g. /usr/bin/g++-10")

# ============================================================
# 3. 查找第三方依赖
# ============================================================

find_package(OpenCV REQUIRED)
find_package(Threads REQUIRED)

message(STATUS "OpenCV version: ${OpenCV_VERSION}")
message(STATUS "OpenCV include dirs: ${OpenCV_INCLUDE_DIRS}")
message(STATUS "OpenCV libs: ${OpenCV_LIBS}")

# ============================================================
# 4. 查找 Muduo 头文件和库
# ============================================================

# 找 Muduo 头文件
# 目标是找到 muduo/net/TcpServer.h
find_path(MUDUO_INCLUDE_DIR
  NAMES muduo/net/TcpServer.h
  PATHS
    ${MUDUO_ROOT}
    /usr/include
    /usr/local/include
  PATH_SUFFIXES
    .
)

# 找 muduo_net 库
# 目标是找到 libmuduo_net.a 或 libmuduo_net.so
find_library(MUDUO_NET_LIBRARY
  NAMES muduo_net
  PATHS
    ${MUDUO_BUILD_ROOT}
    /usr/lib
    /usr/local/lib
    /usr/lib/x86_64-linux-gnu
  PATH_SUFFIXES
    lib
)

# 找 muduo_base 库
# 目标是找到 libmuduo_base.a 或 libmuduo_base.so
find_library(MUDUO_BASE_LIBRARY
  NAMES muduo_base
  PATHS
    ${MUDUO_BUILD_ROOT}
    /usr/lib
    /usr/local/lib
    /usr/lib/x86_64-linux-gnu
  PATH_SUFFIXES
    lib
)

# 如果 Muduo 没找到，直接报错
if(NOT MUDUO_INCLUDE_DIR)
  message(FATAL_ERROR "Muduo include directory not found. Please check MUDUO_ROOT.")
endif()

if(NOT MUDUO_NET_LIBRARY)
  message(FATAL_ERROR "muduo_net library not found. Please check MUDUO_BUILD_ROOT.")
endif()

if(NOT MUDUO_BASE_LIBRARY)
  message(FATAL_ERROR "muduo_base library not found. Please check MUDUO_BUILD_ROOT.")
endif()

message(STATUS "Muduo include dir: ${MUDUO_INCLUDE_DIR}")
message(STATUS "Muduo net library: ${MUDUO_NET_LIBRARY}")
message(STATUS "Muduo base library: ${MUDUO_BASE_LIBRARY}")

# ============================================================
# 5. CUDA 可选编译
# ============================================================

set(CUDA_SOURCES)

if(BUILD_WITH_CUDA)

  if(CUDA_HOST_COMPILER_OVERRIDE)
    set(CMAKE_CUDA_HOST_COMPILER "${CUDA_HOST_COMPILER_OVERRIDE}")
    message(STATUS "CUDA host compiler override: ${CMAKE_CUDA_HOST_COMPILER}")
  endif()

  enable_language(CUDA)

  set(CMAKE_CUDA_STANDARD 14)
  set(CMAKE_CUDA_STANDARD_REQUIRED ON)

  find_package(CUDAToolkit REQUIRED)

  list(APPEND CUDA_SOURCES
    cuda/sobel_processor.cu
  )

  message(STATUS "CUDA enabled")
  message(STATUS "CUDA compiler: ${CMAKE_CUDA_COMPILER}")

else()

  message(STATUS "CUDA disabled, build CPU version only")

endif()

# ============================================================
# 6. 公共源文件
# ============================================================

set(COMMON_SOURCES
  src/common/protocol.cpp
  src/common/image_codec.cpp
  src/common/image_processor.cpp
)

# ============================================================
# 7. 编译公共库 realtime_common
# ============================================================

add_library(realtime_common
  ${COMMON_SOURCES}
  ${CUDA_SOURCES}
)

# 公共库的头文件路径
target_include_directories(realtime_common
  PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${OpenCV_INCLUDE_DIRS}
)

# 公共库链接 OpenCV
target_link_libraries(realtime_common
  PUBLIC
    ${OpenCV_LIBS}
)

# 公共库使用 C++17
target_compile_features(realtime_common
  PUBLIC
    cxx_std_17
)

# 如果是 GCC 或 Clang，打开常用警告
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(realtime_common
    PRIVATE
      -Wall
      -Wextra
      -Wpedantic
  )
endif()

# CUDA 相关设置
if(BUILD_WITH_CUDA)

  set_target_properties(realtime_common PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
    POSITION_INDEPENDENT_CODE ON
  )

  # 只给 CUDA 文件添加这个 nvcc 参数
  target_compile_options(realtime_common
    PRIVATE
      $<$<COMPILE_LANGUAGE:CUDA>:--allow-unsupported-compiler>
  )

  # 给 C++ 代码定义宏：RIS_ENABLE_CUDA=1
  target_compile_definitions(realtime_common
    PUBLIC
      RIS_ENABLE_CUDA=1
  )

  # 链接 CUDA runtime
  target_link_libraries(realtime_common
    PUBLIC
      CUDA::cudart
  )

else()

  # 给 C++ 代码定义宏：RIS_ENABLE_CUDA=0
  target_compile_definitions(realtime_common
    PUBLIC
      RIS_ENABLE_CUDA=0
  )

endif()

# ============================================================
# 8. 编译服务端 image_server
# ============================================================

add_executable(image_server
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
    Threads::Threads
)

target_compile_features(image_server
  PRIVATE
    cxx_std_17
)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(image_server
    PRIVATE
      -Wall
      -Wextra
      -Wpedantic
  )
endif()

# ============================================================
# 9. 编译客户端 image_client
# ============================================================

add_executable(image_client
  src/client/tcp_image_client.cpp
  src/client/client_main.cpp
)

target_include_directories(image_client
  PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(image_client
  PRIVATE
    realtime_common
    Threads::Threads
)

target_compile_features(image_client
  PRIVATE
    cxx_std_17
)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(image_client
    PRIVATE
      -Wall
      -Wextra
      -Wpedantic
  )
endif()

# ============================================================
# 10. 安装规则，可选
# ============================================================

install(
  TARGETS
    image_server
    image_client
    realtime_common
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)

install(
  DIRECTORY include/
  DESTINATION include
)