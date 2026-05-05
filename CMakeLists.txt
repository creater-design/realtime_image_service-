cmake_minimum_required(VERSION 3.18)

# 项目名称
project(realtime_image_service LANGUAGES CXX)

# ============================================================
# 1. 设置 C++ 标准
# ============================================================

# 使用 C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 默认使用 Release 编译
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ============================================================
# 2. 一些可修改的开关和路径
# ============================================================

# 是否使用 CUDA
# ON  = 编译 CUDA 版本
# OFF = 只编译 CPU 版本
option(BUILD_WITH_CUDA "Use CUDA processor" ON)

# Muduo 的路径
# 你自己的 muduo 源码路径
set(MUDUO_ROOT "/home/wzq/Project/muduo")

# 你自己的 muduo 编译结果路径
set(MUDUO_BUILD_ROOT "/home/wzq/Project/build/release-cpp11")

# 如果 CUDA 不兼容默认 g++，可以手动指定 g++-10
# 不需要就保持空
set(CUDA_HOST_COMPILER_OVERRIDE "")

# ============================================================
# 3. 查找 OpenCV 和线程库
# ============================================================

find_package(OpenCV REQUIRED)
find_package(Threads REQUIRED)

# ============================================================
# 4. 查找 Muduo
# ============================================================

# 找 muduo 头文件
find_path(MUDUO_INCLUDE_DIR
  NAMES muduo/net/TcpServer.h
  PATHS
    ${MUDUO_ROOT}
    /usr/include
    /usr/local/include
)

# 找 muduo_net 库
find_library(MUDUO_NET_LIBRARY
  NAMES muduo_net
  PATHS
    ${MUDUO_BUILD_ROOT}/lib
    /usr/lib
    /usr/local/lib
    /usr/lib/x86_64-linux-gnu
)

# 找 muduo_base 库
find_library(MUDUO_BASE_LIBRARY
  NAMES muduo_base
  PATHS
    ${MUDUO_BUILD_ROOT}/lib
    /usr/lib
    /usr/local/lib
    /usr/lib/x86_64-linux-gnu
)

# 如果 Muduo 没找到，就直接报错
if(NOT MUDUO_INCLUDE_DIR)
  message(FATAL_ERROR "找不到 Muduo 头文件，请检查 MUDUO_ROOT")
endif()

if(NOT MUDUO_NET_LIBRARY)
  message(FATAL_ERROR "找不到 muduo_net 库，请检查 MUDUO_BUILD_ROOT")
endif()

if(NOT MUDUO_BASE_LIBRARY)
  message(FATAL_ERROR "找不到 muduo_base 库，请检查 MUDUO_BUILD_ROOT")
endif()

# 打印找到的路径，方便检查
message(STATUS "Muduo include: ${MUDUO_INCLUDE_DIR}")
message(STATUS "muduo_net: ${MUDUO_NET_LIBRARY}")
message(STATUS "muduo_base: ${MUDUO_BASE_LIBRARY}")

# ============================================================
# 5. CUDA 可选部分
# ============================================================

# 默认 CUDA 源文件为空
set(CUDA_SOURCES)

if(BUILD_WITH_CUDA)

  # 如果用户指定了 CUDA 使用的 g++，就使用它
  if(CUDA_HOST_COMPILER_OVERRIDE)
    set(CMAKE_CUDA_HOST_COMPILER ${CUDA_HOST_COMPILER_OVERRIDE})
  endif()

  # 启用 CUDA
  enable_language(CUDA)

  # CUDA 代码按 C++14 编译
  set(CMAKE_CUDA_STANDARD 14)
  set(CMAKE_CUDA_STANDARD_REQUIRED ON)

  # 查找 CUDA 工具包
  find_package(CUDAToolkit REQUIRED)

  # 加入 CUDA 源文件
  set(CUDA_SOURCES
    cuda/sobel_processor.cu
  )

  message(STATUS "CUDA enabled")

else()

  message(STATUS "CUDA disabled")

endif()

# ============================================================
# 6. 公共代码编译成一个库
# ============================================================

# 这些文件是 server 和 client 都会用到的公共代码
set(COMMON_SOURCES
  src/common/protocol.cpp
  src/common/image_codec.cpp
  src/common/image_processor.cpp
  src/common/obstacle_risk_analyzer.cpp
  src/common/result_json.cpp
  src/common/result_payload.cpp
)

# 编译公共库
add_library(realtime_common
  ${COMMON_SOURCES}
  ${CUDA_SOURCES}
)

# 给公共库添加头文件路径
target_include_directories(realtime_common
  PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${OpenCV_INCLUDE_DIRS}
)

# 公共库需要 OpenCV
target_link_libraries(realtime_common
  PUBLIC
    ${OpenCV_LIBS}
)

# 如果使用 CUDA，给代码加 RIS_ENABLE_CUDA=1
# 如果不使用 CUDA，给代码加 RIS_ENABLE_CUDA=0
if(BUILD_WITH_CUDA)

  target_compile_definitions(realtime_common
    PUBLIC
      RIS_ENABLE_CUDA=1
  )

  target_link_libraries(realtime_common
    PUBLIC
      CUDA::cudart
  )

else()

  target_compile_definitions(realtime_common
    PUBLIC
      RIS_ENABLE_CUDA=0
  )

endif()

# ============================================================
# 7. 编译服务端 image_server
# ============================================================

add_executable(image_server
  src/server/image_tcp_server.cpp
  src/server/server_main.cpp
)

# 服务端需要项目头文件和 Muduo 头文件
target_include_directories(image_server
  PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${MUDUO_INCLUDE_DIR}
)

# 服务端链接公共库、Muduo、线程库
target_link_libraries(image_server
  PRIVATE
    realtime_common
    ${MUDUO_NET_LIBRARY}
    ${MUDUO_BASE_LIBRARY}
    Threads::Threads
)

# ============================================================
# 8. 编译客户端 image_client
# ============================================================

add_executable(image_client
  src/client/tcp_image_client.cpp
  src/client/client_main.cpp
)

# 客户端需要项目头文件
target_include_directories(image_client
  PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

# 客户端链接公共库和线程库
target_link_libraries(image_client
  PRIVATE
    realtime_common
    Threads::Threads
)

# ============================================================
# 9. 安装
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