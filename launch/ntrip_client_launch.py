from launch import LaunchDescription
from launch.actions import (
    DeclareLaunchArgument,
    EmitEvent,
    LogInfo,
    RegisterEventHandler,
    SetEnvironmentVariable,
)
from launch.substitutions import LaunchConfiguration
from launch.conditions import IfCondition
from launch.event_handlers import OnProcessStart
from launch_ros.actions import LifecycleNode
from launch_ros.event_handlers import OnStateTransition
from launch_ros.events.lifecycle import ChangeState
from launch.events import matches_action
from lifecycle_msgs.msg import Transition


def generate_launch_description():
    # ------------------------------------------------------------------ #
    # Declare launch arguments
    # ------------------------------------------------------------------ #
    declared_args = [
        DeclareLaunchArgument('namespace',             default_value=''),
        DeclareLaunchArgument('node_name',             default_value='ntrip_client'),
        DeclareLaunchArgument('debug',                 default_value='false'),
        DeclareLaunchArgument('host',                  default_value='20.185.11.35'),
        DeclareLaunchArgument('port',                  default_value='2101'),
        DeclareLaunchArgument('mountpoint',            default_value='VRS_RTCM3'),
        DeclareLaunchArgument('ntrip_version',         default_value='None'),
        DeclareLaunchArgument('authenticate',          default_value='True'),
        DeclareLaunchArgument('username',              default_value='user'),
        DeclareLaunchArgument('password',              default_value='pass'),
        DeclareLaunchArgument('ssl',                   default_value='False'),
        DeclareLaunchArgument('cert',                  default_value='None'),
        DeclareLaunchArgument('key',                   default_value='None'),
        DeclareLaunchArgument('ca_cert',               default_value='None'),
        DeclareLaunchArgument('rtcm_message_package',  default_value='rtcm_msgs'),
        DeclareLaunchArgument('rtcm_frame_id',         default_value='odom'),
        DeclareLaunchArgument('nmea_max_length',       default_value='128'),
        DeclareLaunchArgument('nmea_min_length',       default_value='3'),
        DeclareLaunchArgument('rtcm_timeout_seconds',  default_value='4'),
        DeclareLaunchArgument('reconnect_attempt_max', default_value='10'),
        DeclareLaunchArgument('reconnect_attempt_wait_seconds', default_value='5'),
        # Set false so launch drives the transitions (see handlers below).
        DeclareLaunchArgument('auto_configure',        default_value='true'),
        DeclareLaunchArgument('auto_activate',         default_value='true'),
    ]

    # ------------------------------------------------------------------ #
    # Lifecycle node
    # ------------------------------------------------------------------ #
    ntrip_client_node = LifecycleNode(
        name=LaunchConfiguration('node_name'),
        namespace=LaunchConfiguration('namespace'),
        package='ntrip_client_cpp',
        executable='ntrip_ros',
        output='screen',
        emulate_tty=True,
        parameters=[
            {
                # --- Connection ---
                'host':        LaunchConfiguration('host'),
                'port':        LaunchConfiguration('port'),
                'mountpoint':  LaunchConfiguration('mountpoint'),
                'ntrip_version': LaunchConfiguration('ntrip_version'),

                # --- Auth ---
                'authenticate': LaunchConfiguration('authenticate'),
                'username':     LaunchConfiguration('username'),
                'password':     LaunchConfiguration('password'),

                # --- SSL ---
                'ssl':     LaunchConfiguration('ssl'),
                'cert':    LaunchConfiguration('cert'),
                'key':     LaunchConfiguration('key'),
                'ca_cert': LaunchConfiguration('ca_cert'),

                # --- Frame / NMEA tuning ---
                'rtcm_frame_id':  LaunchConfiguration('rtcm_frame_id'),
                'nmea_max_length': LaunchConfiguration('nmea_max_length'),
                'nmea_min_length': LaunchConfiguration('nmea_min_length'),

                # --- RTCM ---
                'rtcm_message_package': LaunchConfiguration('rtcm_message_package'),
                'rtcm_timeout_seconds': LaunchConfiguration('rtcm_timeout_seconds'),

                # --- Reconnect ---
                'reconnect_attempt_max':          LaunchConfiguration('reconnect_attempt_max'),
                'reconnect_attempt_wait_seconds': LaunchConfiguration('reconnect_attempt_wait_seconds'),

                # --- Debug / lifecycle ---
                'debug':          LaunchConfiguration('debug'),
                'auto_configure': LaunchConfiguration('auto_configure'),
                'auto_activate':  LaunchConfiguration('auto_activate'),
            }
        ],
      # Uncomment to remap topics (e.g. NMEA from a GPS driver):
      # remappings=[('nmea', '/gx5/nmea/sentence')],
    )

    # ------------------------------------------------------------------ #
    # Drive the lifecycle: configure then activate automatically
    # ------------------------------------------------------------------ #
    # Step 1: as soon as the node reaches 'unconfigured', call CONFIGURE.
    configure_event = RegisterEventHandler(
        event_handler=OnProcessStart(
            target_action=ntrip_client_node,
            on_start=[
                # LogInfo(msg='[lifecycle] Configuring ntrip_client...'),
                EmitEvent(event=ChangeState(
                    lifecycle_node_matcher=matches_action(ntrip_client_node),
                    transition_id=Transition.TRANSITION_CONFIGURE,
                )),
            ],
        ),
        condition=IfCondition(LaunchConfiguration('auto_configure')),
    )

    # Step 2: once 'inactive', call ACTIVATE.
    activate_event = RegisterEventHandler(
        event_handler=OnStateTransition(
            target_lifecycle_node=ntrip_client_node,
            start_state='configuring',
            goal_state='inactive',
            entities=[
                # LogInfo(msg='[lifecycle] Activating ntrip_client...'),
                EmitEvent(event=ChangeState(
                    lifecycle_node_matcher=matches_action(ntrip_client_node),
                    transition_id=Transition.TRANSITION_ACTIVATE,
                )),
            ],
        ),
        condition=IfCondition(LaunchConfiguration('auto_activate')),
    )

    # ------------------------------------------------------------------ #
    # Assemble
    # ------------------------------------------------------------------ #
    return LaunchDescription([
        *declared_args,

        # Export the debug env var (the C++ node also accepts `debug:=true`).
        SetEnvironmentVariable(
            name='NTRIP_CLIENT_DEBUG',
            value=LaunchConfiguration('debug'),
        ),

        ntrip_client_node,
        configure_event,
        activate_event,
    ])