cmake_minimum_required(VERSION 3.20)
project(TrafficMonitorIpv4Ipv6 LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "TrafficMonitor plugins must be built for Windows (Win32 or x64). Use a Windows host or a MinGW Windows toolchain.")
endif()

add_library(TrafficMonitorIpv4Ipv6 SHARED
  src/Ipv4Ipv6TrafficPlugin.cpp
)
target_compile_features(TrafficMonitorIpv4Ipv6 PRIVATE cxx_std_17)
target_include_directories(TrafficMonitorIpv4Ipv6 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(TrafficMonitorIpv4Ipv6 PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(TrafficMonitorIpv4Ipv6 PRIVATE iphlpapi ws2_32 user32 gdi32)
set_target_properties(TrafficMonitorIpv4Ipv6 PROPERTIES PREFIX "" OUTPUT_NAME "TrafficMonitorIpv4Ipv6")
