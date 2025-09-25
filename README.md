# Introduction

This project implements a GStreamer plugin featuring a ring buffer filter for encoded video capture. The filter addresses a common requirement in event-driven recording applications: capturing video data that occurred before an event was detected.

## How it works

The filter operates as a ring buffer, continuously caching encoded video frames. When an event is triggered, it transitions to pass-through mode, first sending the cached pre-event data downstream, followed by real-time incoming frames.

## Sample Pipeline

┌────────┐      ┌───────┐        ┌─────────┐        ┌──────────┐       ┌───────┐     ┌────────┐
│ VidSrc ┼─────►│ xh264 ┼───────►│h264Parse┼───────►│prerecloop┼──────►│  Mux  ┼────►│filesink│
│        │      │       │        │         │        │          │       │       │     │        │
└────────┘      └───────┘        └─────────┘        └──────────┘       └───────┘     └────────┘

The idea is that the prerecloop will buffer video frames until an event is published after which it will push buffered frames and incoming frames 
downstream to the file sink.

# Notes

The filter is GOP aware. i.e it will always start at a key frame and when it drops frames, it will drop an entire GOP.

# Test Application

At this point there are no unit tests, however I have created a test application that uses my filter in a pipeline. This is found in `testapp/src/main.cc`.
I should probably have some unit tests to introspect and verify performance of the pre record loop at a more granular level. 

# Building the Code

The code base uses Conan as a package manager. It also expects that Gstreamer libraries are installed using homebrew. It uses pkgconfig to find it in 
the CMakeLists.txt file.

```cmake
# Define the path to your GStreamer pkgconfig directory
set(GSTREAMER_PKGCONFIG_DIR "/opt/homebrew/Cellar/gstreamer/1.26.2/lib/pkgconfig") # Make sure to use the correct version
# Define the path to your glib pkgconfig directory
set(GLIB_PKGCONFIG_DIR "/opt/homebrew/Cellar/glib/2.84.3/lib/pkgconfig") # Make sure to use the correct glib version
```

## A Fresh build.

This uses conan, and so when starting from a fresh build (i.e `./build` doesn't exist) we will need to invoke conan. 
You can invoke either a release or debug build. Conan is setup to generate CMakePresets for configuration and build that can then subsequently be used to 
configure and build the code.

#### Debug

```sh
conan install . --build=missing --settings=build_type=Debug
```

#### Release
 
```sh
conan install . --build=missing
```
## Configure and build

This will configure and build all targets in the project.

### Configure

#### Debug

```sh
cmake --preset=conan-debug
```

#### Release

```sh
cmake --preset=conan-release
```

### Build

#### Debug

```sh
cmake --build --preset=conan-debug
```

#### Release

```sh
cmake --build --preset=conan-release
```

# Running the test app from the Build directory

For brevity I will just consider the Debug version. The same shouls apply for a release version with the folders slightly modified.

### Running without the relevant GStreamer logs.

This will just run the test app (it currently just outputs 450 frames using the test source element from gstreamer) without additional gstreamer logging
```sh
build/Debug/testapp/prerec.app/Contents/MacOS/prerec
```

If you want to get Gstreamer logging with the prerecloop module logging at high granularity run this instead

```sh
GST_DEBUG=*:4,prerecloop:7,prerecloop_dataflow=7 build/Debug/testapp/prerec.app/Contents/MacOS/prerec
```