# ntrip_client_cpp

## Description

A ROS 2 C++ node that communicates with an NTRIP caster to receive RTCM
corrections and publish them on a ROS topic. It also supports virtual NTRIP
servers by subscribing to NMEA messages (or `NavSatFix` messages) and
forwarding them to the caster.

This package is a C++ port of the original Python
[`ntrip_client`](https://github.com/LORD-MicroStrain/ntrip_client) package.
The launch files, parameter names, topics, and behaviour are kept
compatible so existing configurations continue to work.

Two executables are provided:

* **`ntrip_ros`** — connects to a remote NTRIP caster over TCP (with optional
  TLS), subscribes to `/nmea` and `/fix`, and publishes `/rtcm`.
* **`ntrip_serial_device_ros`** — connects to a local NTRIP device over a
  serial port, and publishes `/rtcm`.

Both are implemented as **ROS 2 lifecycle nodes**.

## Build Instructions

### Build from source

1. Install ROS 2 (Humble) and create a workspace.

2. Move the `ntrip_client_cpp` package into your workspace's `src` directory.

3. Install dependencies:

   ```bash
   rosdep install --from-paths ~/your_workspace/src --ignore-src -r -y
   sudo apt install libssl-dev    # required for TLS support
   ```

4. Ensure the `rtcm_msgs` package is available in the workspace.

5. Build the workspace:

   ```bash
   cd ~/your_workspace
   colcon build --packages-select ntrip_client_cpp
   source ~/your_workspace/install/setup.bash
   ```

   The `source` command must be run in each new terminal before launching a
   node.

> **Note:** Unlike the Python package, `ntrip_client_cpp` only supports
> `rtcm_msgs/Message`. The `mavros_msgs/RTCM` message type is not available
> in the C++ port.

## Connecting to a remote NTRIP caster

Use this when you have access to an NTRIP caster or server over the network.

```bash
ros2 launch ntrip_client_cpp ntrip_client_launch.py
```

### Launch arguments

| Argument | Type | Default | Description |
|---|---|---|---|
| `host` | string | `20.185.11.35` | Hostname or IP of the NTRIP server. |
| `port` | int | `2101` | Port to connect to on the server. |
| `mountpoint` | string | `VRS_RTCM3` | Mountpoint on the NTRIP server. |
| `ntrip_version` | string | `'None'` | Value for the `Ntrip-Version` header. Use `'None'` to omit. |
| `authenticate` | bool | `true` | If `true`, `username` and `password` must be provided. |
| `username` | string | `user` | Username for authentication. |
| `password` | string | `pass` | Password for authentication. |
| `ssl` | bool | `false` | Connect with TLS. `cert`, `key`, `ca_cert` only apply if `true`. |
| `cert` | string | `'None'` | Path to a client certificate (PEM). |
| `key` | string | `'None'` | Path to the private key (PEM). |
| `ca_cert` | string | `'None'` | Path to a CA bundle for self-signed / custom chains. |
| `rtcm_frame_id` | string | `odom` | `frame_id` attached to published RTCM messages. |
| `rtcm_message_package` | string | `rtcm_msgs` | RTCM message type. Only `rtcm_msgs` is supported. |
| `rtcm_timeout_seconds` | int | `4` | Reconnect if no RTCM is received for this long. |
| `nmea_max_length` | int | `128` | Maximum accepted NMEA sentence length. |
| `nmea_min_length` | int | `3` | Minimum accepted NMEA sentence length. |
| `reconnect_attempt_max` | int | `10` | Max reconnect attempts before giving up. |
| `reconnect_attempt_wait_seconds` | int | `5` | Delay between reconnect attempts. |
| `debug` | bool | `false` | Enable debug-level logging. |
| `auto_configure` | bool | `false` | If `false`, launch handlers drive lifecycle transitions. |
| `auto_activate` | bool | `false` | If `false`, launch handlers drive lifecycle transitions. |

### Example

```bash
ros2 launch ntrip_client_cpp ntrip_client_launch.py \
  host:=www.geodetic.gov.hk \
  port:=2101 \
  mountpoint:=VRS32G \
  username:=your_username \
  password:=your_password \
  debug:=true
```

## Connecting to a local NTRIP device

Use this when you do not have internet access but have a local NTRIP device
such as the [MicroStrain 3DM-RTK](https://www.microstrain.com/inertial-sensors/3dm-rtk).

```bash
ros2 launch ntrip_client_cpp ntrip_serial_device_launch.py
```

### Launch arguments

| Argument | Type | Default | Description |
|---|---|---|---|
| `port` | string | `/dev/ttyACM0` | Serial port the device is connected on. |
| `baudrate` | int | `115200` | Baud rate. |
| `rtcm_frame_id` | string | `odom` | `frame_id` attached to published RTCM messages. |
| `rtcm_message_package` | string | `rtcm_msgs` | RTCM message type. Only `rtcm_msgs` is supported. |
| `nmea_max_length` | int | `128` | Maximum accepted NMEA sentence length. |
| `nmea_min_length` | int | `3` | Minimum accepted NMEA sentence length. |
| `reconnect_attempt_max` | int | `10` | Max reconnect attempts before giving up. |
| `reconnect_attempt_wait_seconds` | int | `5` | Delay between reconnect attempts. |
| `debug` | bool | `false` | Enable debug-level logging. |
| `auto_configure` | bool | `false` | If `false`, launch handlers drive lifecycle transitions. |
| `auto_activate` | bool | `false` | If `false`, launch handlers drive lifecycle transitions. |

> Make sure your user has permission to open the serial port:
> ```bash
> sudo usermod -aG dialout $USER   # then log out and back in
> ```

## Lifecycle

Both nodes are implemented as `rclcpp_lifecycle::LifecycleNode`s.

* **`ntrip_ros`** — node name `ntrip_client`
* **`ntrip_serial_device_ros`** — node name `ntrip_serial_device`

### Transitions

| Transition | Effect |
|---|---|
| `configure` | Declares parameters, creates publishers and subscriptions. Does **not** open the connection. |
| `activate` | Opens the socket / serial port and starts the RTCM publish timer. |
| `deactivate` | Stops the timer, deactivates the publisher, closes the connection. |
| `cleanup` | Destroys publishers, subscriptions, and timers. |
| `shutdown` | Deactivate + cleanup, leaves the node in the `finalized` state. |

The lifecycle interface is particularly useful when a serial device is not
plugged in at start-up: `on_activate` fails, the node stays in `inactive`,
and you can retry with `ros2 lifecycle set ... activate` once the device is
available — without restarting the process.

## Topics

| Topic | Type | Direction | Description |
|---|---|---|---|
| `/rtcm` | `rtcm_msgs/Message` | published | RTCM corrections received from the caster or serial device. |
| `/nmea` | `nmea_msgs/Sentence` | subscribed | NMEA sentences forwarded to the caster. Required for virtual NTRIP servers and serial devices. |
| `/fix` | `sensor_msgs/NavSatFix` | subscribed | Alternative to `/nmea`: converts a `NavSatFix` to a `$GPGGA` sentence before forwarding. |

**`/rtcm`** — publishes the RTCM corrections received from the server.
Messages can be consumed by drivers such as
[`microstrain_inertial_driver`](https://github.com/LORD-MicroStrain/microstrain_inertial).

**`/nmea`** — subscribes to NMEA sentence messages and forwards them
verbatim to the NTRIP server. Always needed when using a virtual NTRIP
server or a serial NTRIP device.

**`/fix`** — serves the same purpose as `/nmea`, but accepts a
`sensor_msgs/NavSatFix` and builds a `$GPGGA` sentence internally.

### Remapping

To subscribe to NMEA on a different topic, add a remapping in the launch
file or on the command line:

```bash
ros2 run ntrip_client_cpp ntrip_ros --ros-args \
  -r nmea:=/gx5/nmea/sentence
```

## SSL / TLS notes

* Requires `libssl-dev` at build time and OpenSSL at runtime.
* The client verifies the server certificate against the system CA store by
  default. Set `ca_cert` to override.
* For client-certificate authentication, set `ssl:=true` and provide both
  `cert` and `key`.
* The `ntrip_version` header is often required by casters that enforce
  NTRIP 2.0. Use `ntrip_version:=Ntrip/2.0` if your caster rejects the
  default HTTP/1.0 request.

## Differences from the Python package

| Feature | Python `ntrip_client` | C++ `ntrip_client_cpp` |
|---|---|---|
| Language | Python 3 | C++17 |
| Build system | `ament_python` | `ament_cmake` |
| Executables | `ntrip_ros.py`, `ntrip_serial_device_ros.py` | `ntrip_ros`, `ntrip_serial_device_ros` |
| RTCM message types | `mavros_msgs/RTCM`, `rtcm_msgs/Message` | `rtcm_msgs/Message` only |
| Socket/TLS | Python `socket`, `ssl` | POSIX sockets + OpenSSL |
| Serial | `pyserial` | POSIX `termios` |
| Lifecycle | plain `rclpy.Node` | `rclcpp_lifecycle::LifecycleNode` |
| Node names / topics / parameters | unchanged | unchanged |

The C++ port preserves the Python package's quirks where they matter for
wire compatibility (HTTP request format, NTRIP headers, NMEA assembly) but
fixes latent issues such as an uninitialized reconnect counter in the base
class.

## License

`ntrip_client_cpp` is released under the MIT License — see the
[`LICENSE`](LICENSE) file.

This package is a C++ port of the Python
[`ntrip_client`](https://github.com/LORD-MicroStrain/ntrip_client) package,
which is also MIT licensed:

- Copyright (c) 2024 MicroStrain by HBK — original Python `ntrip_client`
- Copyright (c) 2026 HKCLR — C++ port

Both copyright notices are retained in the `LICENSE` file, as required by
the MIT License.