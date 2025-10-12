
file(GLOB sources src/*.cc src/*.c)
add_executable(prerec "${sources}")

# Link with GStreamer
target_link_libraries(prerec PRIVATE PkgConfig::gstreamer spdlog::spdlog)
target_compile_features(prerec PRIVATE cxx_std_20)
target_include_directories(prerec PRIVATE inc)

# Set macOS bundle properties if on macOS
if(APPLE)
  set_target_properties(prerec PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
  )
  
  # Add debugging
  target_compile_definitions(prerec PRIVATE GST_DEBUG=3)
endif()
