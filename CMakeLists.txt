cmake_minimum_required(VERSION 3.8)
project(ntrip_client_cpp)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    Debug Release RelWithDebInfo MinSizeRel)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(std_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(nmea_msgs REQUIRED)
find_package(rtcm_msgs REQUIRED)
find_package(OpenSSL REQUIRED)

# ---------------- Core library (no ROS deps) ----------------
add_library(ntrip_core
  src/nmea_parser.cpp
  src/rtcm_parser.cpp
  src/ntrip_base.cpp
  src/ntrip_client.cpp
  src/ntrip_serial_device.cpp
)
target_include_directories(ntrip_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(ntrip_core PUBLIC OpenSSL::SSL OpenSSL::Crypto)
set_target_properties(ntrip_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------- ROS node base ----------------
add_library(ntrip_ros_base
  src/ntrip_ros_base.cpp
)
target_include_directories(ntrip_ros_base PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(ntrip_ros_base PUBLIC
  ntrip_core
  rclcpp::rclcpp
  rclcpp_lifecycle::rclcpp_lifecycle
  ${std_msgs_TARGETS}
  ${sensor_msgs_TARGETS}
  ${nmea_msgs_TARGETS}
  ${rtcm_msgs_TARGETS}
)
set_target_properties(ntrip_ros_base PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------- Executables ----------------
add_executable(ntrip_ros src/nodes/ntrip_ros.cpp)
target_link_libraries(ntrip_ros ntrip_ros_base)

add_executable(ntrip_serial_device_ros src/nodes/ntrip_serial_device_ros.cpp)
target_link_libraries(ntrip_serial_device_ros ntrip_ros_base)

install(TARGETS
  ntrip_ros
  ntrip_serial_device_ros
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY launch
  DESTINATION share/${PROJECT_NAME}
)

ament_export_include_directories(include)
ament_export_libraries(ntrip_core ntrip_ros_base)
ament_package()