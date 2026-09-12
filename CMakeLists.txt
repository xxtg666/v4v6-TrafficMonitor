cmake_minimum_required(VERSION 3.20)
project(TrafficMonitorIpv4Ipv6 LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "TrafficMonitor plugins must be built for Windows (Win32 or x64). Use a Windows host or a MinGW Windows toolchain.")
endif()

enable_language(RC)

add_library(TrafficMonitorIpv4Ipv6 SHARED
  src/Ipv4Ipv6TrafficPlugin.cpp
  src/DisplayOptions.cpp
  src/DisplayOptions.rc
)
target_compile_features(TrafficMonitorIpv4Ipv6 PRIVATE cxx_std_17)
target_include_directories(TrafficMonitorIpv4Ipv6 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(TrafficMonitorIpv4Ipv6 PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(TrafficMonitorIpv4Ipv6 PRIVATE iphlpapi ws2_32 user32 gdi32)
if(MINGW)
  # Keep the plugin self-contained when it is built in the Codespace.
  target_link_options(TrafficMonitorIpv4Ipv6 PRIVATE -static-libgcc -static-libstdc++)
endif()
set_target_properties(TrafficMonitorIpv4Ipv6 PROPERTIES PREFIX "" OUTPUT_NAME "TrafficMonitorIpv4Ipv6")

option(V4V6_BUILD_TESTS "Build Windows drawing and persistence regression tests" OFF)
if(V4V6_BUILD_TESTS)
  enable_testing()
  add_executable(windows_regression tests/windows_regression.cpp src/DisplayOptions.cpp src/DisplayOptions.rc)
  target_compile_features(windows_regression PRIVATE cxx_std_17)
  target_include_directories(windows_regression PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(windows_regression PRIVATE iphlpapi ws2_32 user32 gdi32)
  if(MSVC)
    target_compile_options(windows_regression PRIVATE /UNDEBUG)
  else()
    target_compile_options(windows_regression PRIVATE -UNDEBUG)
  endif()
  if(MINGW)
    target_link_options(windows_regression PRIVATE -static-libgcc -static-libstdc++)
  endif()
  add_test(NAME windows_regression COMMAND windows_regression)
endif()
