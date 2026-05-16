from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, OpaqueFunction
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def _get_launch_value(context, name):
    return LaunchConfiguration(name).perform(context).strip()


def _set_if_present(context, overrides, launch_name, parameter_name=None, cast=str):
    value = _get_launch_value(context, launch_name)
    if not value:
        return

    key = parameter_name or launch_name
    if cast is bool:
        normalized = value.lower()
        if normalized in ("1", "true", "yes", "on"):
            overrides[key] = True
        elif normalized in ("0", "false", "no", "off"):
            overrides[key] = False
        else:
            raise ValueError(f"{launch_name} must be a boolean value")
    else:
        overrides[key] = cast(value)


def _create_nodes(context):
    config_file = _get_launch_value(context, "config_file")

    network_camera_overrides = {
        "topic": "/camera/image_raw",
        "frame_id": "network_camera",
    }
    _set_if_present(context, network_camera_overrides, "stream_url")
    _set_if_present(context, network_camera_overrides, "fps", cast=float)
    _set_if_present(context, network_camera_overrides, "image_width", cast=int)
    _set_if_present(context, network_camera_overrides, "image_height", cast=int)
    _set_if_present(context, network_camera_overrides, "reconnect_interval", cast=float)

    tcp_client_overrides = {
        "topic_name": "/camera/image_raw",
    }
    _set_if_present(context, tcp_client_overrides, "server_host", "host")
    _set_if_present(context, tcp_client_overrides, "server_port", "port", int)
    _set_if_present(context, tcp_client_overrides, "output_path")
    _set_if_present(context, tcp_client_overrides, "depth_output_path")
    _set_if_present(context, tcp_client_overrides, "max_request_fps", cast=float)

    network_camera_node = Node(
        package="image_publisher_pkg",
        executable="network_camera_node",
        name="network_camera_node",
        output="screen",
        parameters=[config_file, network_camera_overrides],
    )

    tcp_client_node = Node(
        package="image_publisher_pkg",
        executable="ros2_tcp_client_node",
        name="ros2_tcp_client_node",
        output="screen",
        parameters=[config_file, tcp_client_overrides],
    )

    return [network_camera_node, tcp_client_node]


def generate_launch_description():
    config_file_arg = DeclareLaunchArgument(
        "config_file",
        default_value=PathJoinSubstitution(
            [
                FindPackageShare("image_publisher_pkg"),
                "config",
                "network_camera_depth_visualization.yaml",
            ]
        ),
        description="YAML parameter file used as the base configuration",
    )

    stream_url_arg = DeclareLaunchArgument(
        "stream_url",
        default_value="",
        description="Optional HTTP MJPEG or RTSP network camera stream URL override",
    )

    image_width_arg = DeclareLaunchArgument(
        "image_width",
        default_value="",
        description="Optional published image width override",
    )

    image_height_arg = DeclareLaunchArgument(
        "image_height",
        default_value="",
        description="Optional published image height override",
    )

    fps_arg = DeclareLaunchArgument(
        "fps",
        default_value="",
        description="Optional network camera publish rate override",
    )

    reconnect_interval_arg = DeclareLaunchArgument(
        "reconnect_interval",
        default_value="",
        description="Optional reconnect interval override",
    )

    server_host_arg = DeclareLaunchArgument(
        "server_host",
        default_value="",
        description="Optional Muduo image_server host override",
    )

    server_port_arg = DeclareLaunchArgument(
        "server_port",
        default_value="",
        description="Optional Muduo image_server port override",
    )

    max_request_fps_arg = DeclareLaunchArgument(
        "max_request_fps",
        default_value="",
        description="Optional maximum TCP inference request rate override",
    )

    output_path_arg = DeclareLaunchArgument(
        "output_path",
        default_value="",
        description="Optional debug path for the latest processed image",
    )

    depth_output_path_arg = DeclareLaunchArgument(
        "depth_output_path",
        default_value="",
        description="Optional debug path for the latest depth visualization image",
    )

    return LaunchDescription(
        [
            config_file_arg,
            stream_url_arg,
            image_width_arg,
            image_height_arg,
            fps_arg,
            reconnect_interval_arg,
            server_host_arg,
            server_port_arg,
            max_request_fps_arg,
            output_path_arg,
            depth_output_path_arg,
            OpaqueFunction(function=_create_nodes),
        ]
    )
