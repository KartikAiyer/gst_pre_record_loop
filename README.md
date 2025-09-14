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