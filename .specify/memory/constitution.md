<!--
Sync Impact Report:
- Version change: Initial → 1.0.0
- Constitution created for GStreamer Pre-Record Loop Plugin
- Added sections: Core Principles (5), GStreamer Compliance, Development Standards
- Templates requiring updates: ✅ constitution created / ⚠ dependent templates need review
- Follow-up TODOs: None - all placeholders filled
-->

# GStreamer Pre-Record Loop Plugin Constitution

## Core Principles

### I. Thread-Safe GStreamer Integration (NON-NEGOTIABLE)
All code MUST be thread-safe following GStreamer's threading model; Use proper GStreamer locking mechanisms (GMutex, GCond) for shared state; Buffer processing MUST handle concurrent access between sink and source pads; No direct memory access without proper synchronization primitives.

**Rationale**: GStreamer pipelines run in multiple threads, and the ring buffer implementation requires careful coordination between buffering and playback operations.

### II. GOP-Aware Frame Processing
Buffer management MUST respect GOP (Group of Pictures) boundaries; Always start playback from keyframes; When dropping frames due to buffer overflow, drop complete GOPs, never partial ones; Maintain GOP ID tracking for consistent stream integrity.

**Rationale**: Video streams require GOP-aware processing to ensure decodable output. Partial GOPs result in corrupted video streams.

### III. Zero-Copy Performance Architecture
Minimize buffer copies through GStreamer's reference counting; Use gst_buffer_ref/unref for efficient memory management; Implement efficient ring buffer using GstVecDeque; Avoid unnecessary memory allocations in the critical data path.

**Rationale**: Real-time video processing demands optimal performance. Extra memory copies introduce latency and reduce throughput.

### IV. GStreamer API Compliance
Strictly follow GStreamer plugin development guidelines; Implement proper caps negotiation for upstream/downstream elements; Handle all required GStreamer events (EOS, FLUSH, SEEK) appropriately; Provide configurable properties using GObject property system.

**Rationale**: Compliance ensures the plugin integrates seamlessly with any GStreamer pipeline and maintains compatibility across GStreamer versions.

### V. Event-Driven State Management
Implement clear state transitions between BUFFERING and PASS_THROUGH modes; Handle external trigger events for mode switching; Ensure proper synchronization when transitioning states; Maintain buffer continuity during state changes with appropriate DISCONT flags.

**Rationale**: The plugin's core functionality depends on reliable state management responding to external events while maintaining stream continuity.

## GStreamer Compliance

### Plugin Registration and Metadata
Plugin MUST register with appropriate metadata (author, description, version); Element factory MUST define proper pad templates with supported caps; Properties MUST be documented and use appropriate GParamFlags; Plugin MUST handle dynamic property changes safely.

### Pipeline Integration Standards
Support all standard GStreamer pipeline operations (PLAYING, PAUSED, READY, NULL states); Handle upstream events (caps, segments, flush) and forward appropriately downstream; Implement proper latency reporting for live sources; Support seeking operations where applicable.

## Development Standards

### Code Quality and Safety
Use GStreamer debug categories for structured logging; All memory allocations MUST be paired with proper cleanup; Handle all error conditions gracefully with appropriate GstFlowReturn codes; Follow GStreamer coding style guidelines for C code.

### Testing Requirements
Unit tests MUST cover all state transitions and edge cases; Pipeline tests MUST verify integration with common GStreamer elements; Performance benchmarks required for buffer operations; Memory leak testing mandatory using Valgrind or similar tools.

### Documentation Standards
All public APIs documented using GTK-Doc format; Configuration properties documented with examples; Pipeline usage examples provided in README; Code comments explain complex threading and synchronization logic.

## Governance

This constitution supersedes all other development practices for the GStreamer Pre-Record Loop Plugin. All code changes MUST comply with these principles. Amendments require justification, impact analysis, and verification that dependent templates remain consistent. Breaking changes require MAJOR version increments. Any complexity introduced must be justified against performance or compliance requirements.

**Version**: 1.0.0 | **Ratified**: 2025-09-24 | **Last Amended**: 2025-09-24