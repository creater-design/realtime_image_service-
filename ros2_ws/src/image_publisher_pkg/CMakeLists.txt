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
find_package(cv_bridge REQUIRED)

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

# 设置外部项目路径
set(RIS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../..")

# 包含目录  
include_directories(
  include
  ${OpenCV_INCLUDE_DIRS}
  ${RIS_ROOT}/include
)

# 添加可执行文件
add_executable(image_file_publisher 
  src/image_file_publisher.cpp
)

ament_target_dependencies(
  image_file_publisher
  rclcpp
  sensor_msgs
  std_msgs
  cv_bridge
)

target_link_libraries(
  image_file_publisher
  ${OpenCV_LIBS}
)
# ROS2 依赖绑定
ament_target_dependencies(
  image_file_publisher
  rclcpp
  sensor_msgs
  std_msgs
  cv_bridge
)

# OpenCV 依赖绑定
target_link_libraries(
  image_file_publisher
  ${OpenCV_LIBS}
)

add_executable(ros2_tcp_client_node
  src/ros2_tcp_client_node.cpp
  ${RIS_ROOT}/src/client/tcp_image_client.cpp
  ${RIS_ROOT}/src/common/image_codec.cpp
  ${RIS_ROOT}/src/common/protocol.cpp
)

ament_target_dependencies(
  ros2_tcp_client_node
  rclcpp
  sensor_msgs
  std_msgs
  cv_bridge
)

target_link_libraries(
  image_file_publisher
  ${OpenCV_LIBS}
)

install(
  TARGETS
    image_file_publisher
    ros2_tcp_client_node
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY launch DESTINATION share/${PROJECT_NAME})
ament_package()
