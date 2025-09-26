# Changelog

All notable changes to this project will be documented in this file.

The format is inspired by Keep a Changelog and follows semantic versioning.

## [Unreleased]
### Added
- Project scaffolding and initial GStreamer prerecord loop element skeleton.
- Debug categories: `pre_record_loop`, `pre_record_loop_dataflow` (T003).
- Test directory structure: `tests/unit`, `tests/integration`, `tests/perf`, `tests/memory` (T001).
- Test build integration with gst-check placeholder (T002).

### Changed
- Renamed GST_DEBUG categories from `prerecloop` / `prerecloop_dataflow` to `pre_record_loop` / `pre_record_loop_dataflow`.

### Removed
- (Pending T008) Buffer list handling paths slated for removal.

## [0.1.0] - YYYY-MM-DD
- Tag to be created after core feature tests & implementation (Phase 3.3) are stable.

