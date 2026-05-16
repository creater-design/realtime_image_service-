cmake_minimum_required(VERSION 3.8)
project(image_publisher_pkg)

# 如果当前 C++ 编译器是 GCC 或 Clang，就给整个项目加上编译警告选项：只有 GCC 或 Clang 才生效这些选项
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# 查找第三方依赖
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(cv_bridge REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs videoio)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  # the following line skips the linter which checks for copyrights
  # comment the line when a copyright and license is added to all source files
  set(ament_cmake_copyright_FOUND TRUE)
  # the following line skips cpplint (only works in a git repo)
  # comment the line when this package is in a git repo and when
  # a copyright and license is added to all source files
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

# 设置项目根目录
set(RIS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../..")

# 生成可执行文件，由 下面所有 .cpp 文件编译成 .o 文件
add_executable(ros2_tcp_client_node
  src/ros2_tcp_client_node.cpp
  ${RIS_ROOT}/src/client/tcp_image_client.cpp
  ${RIS_ROOT}/src/common/image_codec.cpp
  ${RIS_ROOT}/src/common/protocol.cpp
  ${RIS_ROOT}/src/common/result_payload.cpp
)

# 编译的时候，如果遇到 #include，应该去哪些文件夹里找头文件
target_include_directories(ros2_tcp_client_node PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${RIS_ROOT}/include
  ${OpenCV_INCLUDE_DIRS}
)

# 告诉 ROS2 构建系统：这个节点用到了哪些 ROS2 包
ament_target_dependencies(ros2_tcp_client_node
  rclcpp
  sensor_msgs
  std_msgs
  cv_bridge
)

# 告诉编译器：最后生成程序时，要把 OpenCV 的库也链接进来。
target_link_libraries(ros2_tcp_client_node
  ${OpenCV_LIBS}
)

# 添加可执行文件 network_camera_node
add_executable(network_camera_node
  src/network_camera_node.cpp
)

target_include_directories(network_camera_node PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

ament_target_dependencies(
  network_camera_node
  rclcpp
  sensor_msgs
  cv_bridge
)

target_link_libraries(
  network_camera_node
  ${OpenCV_LIBS}
)

install(
  TARGETS
    ros2_tcp_client_node
    network_camera_node
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY launch config DESTINATION share/${PROJECT_NAME})
ament_package()
