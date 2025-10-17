cmake_minimum_required(VERSION 3.27)
project(prerec C CXX)

# Option: BUILD_GTK_DOC (Phase 3.1 T007) - disabled by default
option(BUILD_GTK_DOC "Build gtk-doc API reference (requires gtk-doc tools)" OFF)
option(PREREC_ENABLE_LIFE_DIAG "Enable prerecordloop lifecycle/sticky diagnostics (ref/push tracking)" OFF)
option(ENABLE_ASAN "Enable AddressSanitizer for leak/memory error detection (macOS/Linux)" OFF)

# Get the existing PKG_CONFIG_PATH environment variable
# If it's not set, it will be an empty string
set(CURRENT_PKG_CONFIG_PATH "$ENV{PKG_CONFIG_PATH}")

# Auto-detect GStreamer installation paths (macOS Homebrew vs Linux Linuxbrew)
if(APPLE)
  # macOS Homebrew paths
  set(GSTREAMER_PKGCONFIG_DIR "/opt/homebrew/Cellar/gstreamer/1.26.2/lib/pkgconfig")
  set(GLIB_PKGCONFIG_DIR "/opt/homebrew/Cellar/glib/2.84.3/lib/pkgconfig")
elseif(UNIX)
  # Linux Linuxbrew paths - try to find actual version
  execute_process(
    COMMAND pkg-config --variable=prefix gstreamer-1.0
    OUTPUT_VARIABLE GSTREAMER_PREFIX
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if(GSTREAMER_PREFIX)
    set(GSTREAMER_PKGCONFIG_DIR "${GSTREAMER_PREFIX}/lib/pkgconfig")
    # Try to get glib prefix from GStreamer's dependency
    execute_process(
      COMMAND pkg-config --variable=prefix glib-2.0
      OUTPUT_VARIABLE GLIB_PREFIX
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    if(GLIB_PREFIX)
      set(GLIB_PKGCONFIG_DIR "${GLIB_PREFIX}/lib/pkgconfig")
    endif()
  else()
    # Fallback to common Linuxbrew path
    set(GSTREAMER_PKGCONFIG_DIR "/home/linuxbrew/.linuxbrew/lib/pkgconfig")
    set(GLIB_PKGCONFIG_DIR "/home/linuxbrew/.linuxbrew/lib/pkgconfig")
  endif()
endif()

set(CUMULATIVE_PATH "${GSTREAMER_PKGCONFIG_DIR}:${GLIB_PKGCONFIG_DIR}")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 23)

# AddressSanitizer configuration (T039)
if(ENABLE_ASAN)
  message(STATUS "AddressSanitizer enabled for leak/memory error detection")
  add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address)
endif()

# Enable testing framework before adding subdirectories that register tests
enable_testing()

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
# Find GStreamer (require 1.26+ for GstVecDeque support)
pkg_check_modules(gstreamer REQUIRED IMPORTED_TARGET gstreamer-1.0>=1.26)
if(gstreamer_VERSION VERSION_LESS "1.26")
  message(FATAL_ERROR "GStreamer 1.26 or newer required (found ${gstreamer_VERSION}). GstVecDeque API required.")
endif()
message(STATUS "Found GStreamer ${gstreamer_VERSION}")
find_package(spdlog REQUIRED)

add_subdirectory(gstprerecordloop)
add_subdirectory(testapp)
add_subdirectory(tests)

if(BUILD_GTK_DOC)
  message(STATUS "gtk-doc build enabled (stub) - no targets yet")
  # Placeholder for future gtk-doc integration
endif()

