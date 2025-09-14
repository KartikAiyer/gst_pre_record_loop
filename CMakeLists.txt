cmake_minimum_required(VERSION 3.27)
project(prerec C CXX)

# Get the existing PKG_CONFIG_PATH environment variable
# If it's not set, it will be an empty string
set(CURRENT_PKG_CONFIG_PATH "$ENV{PKG_CONFIG_PATH}")

# Define the path to your GStreamer pkgconfig directory
set(GSTREAMER_PKGCONFIG_DIR "/opt/homebrew/Cellar/gstreamer/1.26.2/lib/pkgconfig") # Make sure to use the correct version
# Define the path to your glib pkgconfig directory
set(GLIB_PKGCONFIG_DIR "/opt/homebrew/Cellar/glib/2.84.3/lib/pkgconfig") # Make sure to use the correct glib version

set(CUMULATIVE_PATH "${GSTREAMER_PKGCONFIG_DIR}:${GLIB_PKGCONFIG_DIR}")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 23)

# Append the GStreamer path to PKG_CONFIG_PATH
# Use a semicolon (;) as a separator for multiple paths
if (NOT CURRENT_PKG_CONFIG_PATH STREQUAL "")
  set(NEW_PKG_CONFIG_PATH "${CURRENT_PKG_CONFIG_PATH}:${CUMULATIVE_PATH}")
else()
  set(NEW_PKG_CONFIG_PATH "${CUMULATIVE_PATH}")
endif()

# Set the PKG_CONFIG_PATH environment variable for the current CMake process
set(ENV{PKG_CONFIG_PATH} "${NEW_PKG_CONFIG_PATH}")
message("PKG_CONFIG_PATH is set to: ${NEW_PKG_CONFIG_PATH}")

# Find Package Config
find_package(PkgConfig REQUIRED)
# Find GStreamer
#find_package(gstreamer REQUIRED)
pkg_check_modules(gstreamer REQUIRED IMPORTED_TARGET gstreamer-1.0)
find_package(spdlog REQUIRED)
find_package(uvw REQUIRED)

add_subdirectory(gstprerecordloop)
add_subdirectory(testapp)
