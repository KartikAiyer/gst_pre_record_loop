cmake_minimum_required(VERSION 3.15)
project(gstprerecordloop C CXX)

file(GLOB sources src/*.c)
file(GLOB headers inc/gstprerecordloop/*.h)

add_library(gstprerecordloop MODULE "${sources}")
target_link_libraries(gstprerecordloop PRIVATE PkgConfig::gstreamer)
target_compile_features(gstprerecordloop PRIVATE c_std_11 cxx_std_20)
target_include_directories(gstprerecordloop PUBLIC inc)
