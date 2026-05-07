from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue


def generate_launch_description():
    video_device_arg = DeclareLaunchArgument(
        "video_device",
        default_value="/dev/video0",
        description="V4L2 camera device path",
    )

    image_width_arg = DeclareLaunchArgument(
        "image_width",
        default_value="160",
        description="Camera image width",
    )

    image_height_arg = DeclareLaunchArgument(
        "image_height",
        default_value="120",
        description="Camera image height",
    )

    framerate_arg = DeclareLaunchArgument(
        "framerate",
        default_value="30.0",
        description="Camera frame rate",
    )

    pixel_format_arg = DeclareLaunchArgument(
        "pixel_format",
        default_value="mjpeg2rgb",
        description="usb_cam pixel format",
    )

    camera_topic_arg = DeclareLaunchArgument(
        "camera_topic",
        default_value="/camera/image_raw",
        description="ROS image topic published by the camera",
    )

    server_host_arg = DeclareLaunchArgument(
        "server_host",
        default_value="127.0.0.1",
        description="Muduo image_server host",
    )

    server_port_arg = DeclareLaunchArgument(
        "server_port",
        default_value="9999",
        description="Muduo image_server port",
    )

    output_path_arg = DeclareLaunchArgument(
        "output_path",
        default_value="/tmp/ros2_camera_result.jpg",
        description="Debug path for the latest processed image",
    )

    depth_output_path_arg = DeclareLaunchArgument(
        "depth_output_path",
        default_value="/tmp/ros2_camera_depth.jpg",
        description="Debug path for the latest depth visualization image",
    )

    max_request_fps_arg = DeclareLaunchArgument(
        "max_request_fps",
        default_value="2.0",
        description="Maximum TCP inference request rate",
    )

    low_speed_arg = DeclareLaunchArgument(
        "low_speed",
        default_value="0.20",
        description="Linear speed used when risk_level is low",
    )

    medium_speed_arg = DeclareLaunchArgument(
        "medium_speed",
        default_value="0.05",
        description="Linear speed used when risk_level is medium",
    )

    stop_on_unknown_arg = DeclareLaunchArgument(
        "stop_on_unknown",
        default_value="true",
        description="Stop when obstacle_result is missing or malformed",
    )

    camera_node = Node(
        package="usb_cam",
        executable="usb_cam_node_exe",
        name="usb_cam",
        output="screen",
        parameters=[
            {
                "video_device": LaunchConfiguration("video_device"),
                "image_width": ParameterValue(
                    LaunchConfiguration("image_width"),
                    value_type=int,
                ),
                "image_height": ParameterValue(
                    LaunchConfiguration("image_height"),
                    value_type=int,
                ),
                "framerate": ParameterValue(
                    LaunchConfiguration("framerate"),
                    value_type=float,
                ),
                "pixel_format": LaunchConfiguration("pixel_format"),
            }
        ],
        remappings=[
            ("/image_raw", LaunchConfiguration("camera_topic")),
        ],
    )

    tcp_client_node = Node(
        package="image_publisher_pkg",
        executable="ros2_tcp_client_node",
        name="ros2_tcp_client_node",
        output="screen",
        parameters=[
            {
                "host": LaunchConfiguration("server_host"),
                "port": ParameterValue(
                    LaunchConfiguration("server_port"),
                    value_type=int,
                ),
                "topic_name": LaunchConfiguration("camera_topic"),
                "output_path": LaunchConfiguration("output_path"),
                "depth_output_path": LaunchConfiguration("depth_output_path"),
                "max_request_fps": ParameterValue(
                    LaunchConfiguration("max_request_fps"),
                    value_type=float,
                ),
            }
        ],
    )

    safety_controller_node = Node(
        package="image_publisher_pkg",
        executable="safety_controller_node",
        name="safety_controller_node",
        output="screen",
        parameters=[
            {
                "low_speed": ParameterValue(
                    LaunchConfiguration("low_speed"),
                    value_type=float,
                ),
                "medium_speed": ParameterValue(
                    LaunchConfiguration("medium_speed"),
                    value_type=float,
                ),
                "stop_on_unknown": ParameterValue(
                    LaunchConfiguration("stop_on_unknown"),
                    value_type=bool,
                ),
            }
        ],
    )

    return LaunchDescription(
        [
            video_device_arg,
            image_width_arg,
            image_height_arg,
            framerate_arg,
            pixel_format_arg,
            camera_topic_arg,
            server_host_arg,
            server_port_arg,
            output_path_arg,
            depth_output_path_arg,
            max_request_fps_arg,
            low_speed_arg,
            medium_speed_arg,
            stop_on_unknown_arg,
            camera_node,
            tcp_client_node,
            safety_controller_node,
        ]
    )
