from launch import LaunchDescription # 导入 LaunchDescription 类
from launch.actions import DeclareLaunchArgument # 导入 DeclareLaunchArgument 类
from launch.substitutions import LaunchConfiguration # 导入 LaunchConfiguration 类
from launch_ros.actions import Node # 导入 Node 类
from launch_ros.parameter_descriptions import ParameterValue # 导入 ParameterValue 类

def generate_launch_description():
    # =========================
    # Launch arguments
    # =========================
    image_path_arg = DeclareLaunchArgument(
        'image_path',
        default_value="sample/test.jpg",
        description='Path to the image file to be published'
    )
    
    topic_name_arg = DeclareLaunchArgument(
        'topic_name',
        default_value="image_topic",
        description='Topic name to publish the image'
    )
    server_host_arg = DeclareLaunchArgument(
        'server_host',
        default_value="127.0.0.1",
        description='Hostname of the server'
    )
    server_port_arg = DeclareLaunchArgument(
        'server_port',
        default_value="9999",
        description='Port number of the server'
    )
    publish_interval_ms_arg = DeclareLaunchArgument(
        'publish_interval_ms',
        default_value="1000",
        description='Interval between image publishes in milliseconds'
    )
    output_path_arg = DeclareLaunchArgument(
        'output_path',
        default_value="samples/ros2_result.jpg",
        description='Path to the output directory'
    )

    # =========================
    # Nodes
    # =========================
    image_file_publisher_node = Node(
        package='image_publisher_pkg',
        executable='image_file_publisher',
        name='image_file_publisher',
        output='screen',
        parameters=[
            {
                'image_path': LaunchConfiguration('image_path'),
                'topic_name': LaunchConfiguration('topic_name'),
                'publish_interval_ms': ParameterValue(
                    LaunchConfiguration('publish_interval_ms'),
                    value_type=int
                )
            }
        ]
    )

    ros2_tcp_client_node = Node(
        package='image_publisher_pkg',
        executable='ros2_tcp_client_node',
        name='ros2_tcp_client_node',
        output='screen',
        parameters=[
            {
                'host': LaunchConfiguration('server_host'),
                'port': ParameterValue(
                    LaunchConfiguration('server_port'),
                    value_type=int
                ),
                'topic_name': LaunchConfiguration('topic_name'),      
                'output_path': LaunchConfiguration('output_path'),
            }
        ]
    )

    return LaunchDescription([
        image_path_arg,
        topic_name_arg,
        server_host_arg,
        server_port_arg,
        publish_interval_ms_arg,
        output_path_arg,
        image_file_publisher_node,
        ros2_tcp_client_node,
    ])
