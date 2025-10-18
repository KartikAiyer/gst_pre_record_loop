#!/usr/bin/env bash
# Phase 3.1 CI Script (updated to follow README / quickstart build flow)
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CPU_CORES="$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"

echo "[CI] Starting full Debug + Release build/test using CMake presets"

# Note: GStreamer plugin cache is managed by GStreamer itself
# On fresh Linux runs (caching disabled), cache will be built from scratch
# On cached macOS runs, we use GST_REGISTRY to force fresh cache with our plugin

require() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "[CI][FATAL] Required tool '$1' not found" >&2
    exit 2
  fi
}

require cmake

# T040: Code style check function - can be called independently
check_code_style() {
  # Set ENFORCE_STYLE=1 to make style violations fail the CI
  local enforce="${ENFORCE_STYLE:-0}"
  
  if ! command -v clang-format >/dev/null 2>&1; then
    echo "[CI][WARN] clang-format not found; skipping style check"
    echo "[CI][WARN] Install with: brew install clang-format (macOS) or apt-get install clang-format (Linux)"
    return 0
  fi

  echo "[CI] Running clang-format style check (enforce=${enforce})"
  
  # Check for .clang-format config
  if [[ ! -f "$ROOT_DIR/.clang-format" ]]; then
    echo "[CI][WARN] .clang-format not found; using clang-format defaults"
  fi
  
  # Find all C/C++ source and header files
  local style_files
  style_files=$(git ls-files '*.c' '*.h' '*.cc' '*.cpp' '*.hpp' 2>/dev/null || find . -name '*.c' -o -name '*.h')
  
  if [[ -z "$style_files" ]]; then
    echo "[CI][WARN] No source files found for style check"
    return 0
  fi
  
  # Check each file for formatting differences
  local violations=0
  local violating_files=()
  
  while IFS= read -r file; do
    if [[ -f "$file" ]]; then
      # Compare formatted vs actual
      if ! diff -q "$file" <(clang-format "$file") >/dev/null 2>&1; then
        violations=$((violations + 1))
        violating_files+=("$file")
      fi
    fi
  done <<< "$style_files"
  
  # Report results
  if [[ $violations -eq 0 ]]; then
    echo "[CI] ✓ Style check passed: All files conform to .clang-format"
    return 0
  else
    echo "[CI][STYLE] Found $violations file(s) with formatting issues:"
    for file in "${violating_files[@]}"; do
      echo "[CI][STYLE]   - $file"
    done
    echo "[CI][STYLE] To fix: clang-format -i <file>"
    echo "[CI][STYLE] To fix all: git ls-files '*.c' '*.h' | xargs clang-format -i"
    
    if [[ "$enforce" == "1" ]]; then
      echo "[CI][ERROR] Style enforcement enabled; failing build due to formatting violations" >&2
      return 1
    else
      echo "[CI][WARN] Style violations detected (non-blocking; set ENFORCE_STYLE=1 to enforce)"
      return 0
    fi
  fi
}

# Configure, build, test using preset
configure_build_test() {
  local preset="$1" # e.g. debug or release
  local build_dir_suffix="${preset}" # debug or release
  local upper_dir="$(printf '%s%s' "${build_dir_suffix:0:1}" | tr '[:lower:]' '[:upper:]')${build_dir_suffix:1}"
  echo "[CI] Configuring ($preset)"
  cmake --preset="$preset"
  echo "[CI] Building ($preset)"
  cmake --build --preset="$preset" --parallel "$CPU_CORES"
  
  # Run style check after build (only once for Debug build)
  if [[ "$preset" == "debug" ]]; then
    check_code_style || return 1
  fi
  
  # Export GST_PLUGIN_PATH for sanity checks (plugin directory first)
  local plugin_dir="$ROOT_DIR/build/${upper_dir}/gstprerecordloop"
  if [[ -d "$plugin_dir" ]]; then
    export GST_PLUGIN_PATH="$plugin_dir:${GST_PLUGIN_PATH:-}"
    echo "[CI] GST_PLUGIN_PATH set to $GST_PLUGIN_PATH"
  else
    echo "[CI][WARN] Plugin directory not found at $plugin_dir"
  fi

  if [[ -n "${CI_DEBUG:-}" ]]; then
    echo "[DEBUG]: PATH=${PATH}"
    echo "[DEBUG]: which gst-inspect-1.0: $(which gst-inspect-1.0 2>/dev/null || echo 'not in PATH')"
    echo "[DEBUG]: find gst-inspect-1.0: $(find /home/linuxbrew/.linuxbrew/ -iname gst-inspect-1.0 2>/dev/null | head -1 || echo 'not found')"
    if [[ -d /home/linuxbrew/.linuxbrew/Cellar/gstreamer ]]; then
      echo "[DEBUG]: find gstreamer bin: $(find /home/linuxbrew/.linuxbrew/Cellar/gstreamer -type d -name bin 2>/dev/null | head -1)"
    else
      echo "[DEBUG]: find gstreamer bin: directory not yet cached"
    fi
    echo "[DEBUG]: brew info gstreamer: $(brew info gstreamer 2>/dev/null || echo 'brew not available')"
  fi
  
  # gst-inspect sanity (non-fatal warning if missing gst-inspect)
  # Determine gst-inspect-1.0 path (Homebrew may not be in PATH)
  # IMPORTANT: Use dynamic path finding instead of hardcoding versions
  local gst_inspect_cmd=""
  if command -v gst-inspect-1.0 >/dev/null 2>&1; then
    gst_inspect_cmd="gst-inspect-1.0"
    echo "[DEBUG]: Found gst-inspect-1.0 in PATH: $gst_inspect_cmd"
  # Fallback: search Cellar for any gst-inspect-1.0 (works with any version)
  elif gst_inspect_path=$(find /home/linuxbrew/.linuxbrew/Cellar/gstreamer -name "gst-inspect-1.0" -type f 2>/dev/null | head -1); [ -n "$gst_inspect_path" ]; then
    gst_inspect_cmd="$gst_inspect_path"
    echo "[DEBUG]: Found gst-inspect-1.0 via Linux Cellar search: $gst_inspect_cmd"
  elif gst_inspect_path=$(find /opt/homebrew/Cellar/gstreamer -name "gst-inspect-1.0" -type f 2>/dev/null | head -1); [ -n "$gst_inspect_path" ]; then
    gst_inspect_cmd="$gst_inspect_path"
    echo "[DEBUG]: Found gst-inspect-1.0 via macOS Cellar search: $gst_inspect_cmd"
  else
    echo "[DEBUG]: gst-inspect-1.0 not found anywhere"
    gst_inspect_cmd=""
  fi
  
  if [ -n "$gst_inspect_cmd" ]; then
    # Add diagnostics before attempting gst-inspect
    echo "[CI] Plugin directory: $plugin_dir"
    if [[ -d "$plugin_dir" ]]; then
      echo "[CI] Plugin directory contents:"
      ls -lh "$plugin_dir" || true
    else
      echo "[CI][ERROR] Plugin directory does not exist" >&2
      return 1
    fi
    
    # Try to inspect the element
    if "$gst_inspect_cmd" pre_record_loop >/dev/null 2>&1; then
      echo "[CI] ✓ gst-inspect located element 'pre_record_loop'"
    else
      echo "[CI][ERROR] gst-inspect could not find 'pre_record_loop'" >&2
      echo "[CI][DEBUG] GST_PLUGIN_PATH=$GST_PLUGIN_PATH"
      echo "[CI][DEBUG] LD_LIBRARY_PATH=${LD_LIBRARY_PATH:-<not set>}"
      echo "[CI][DEBUG] Attempting direct plugin inspection:"
      "$gst_inspect_cmd" prerecordloop 2>&1 | head -30 || true
      echo "[CI][DEBUG] GStreamer debug output:"
      GST_DEBUG=3 "$gst_inspect_cmd" pre_record_loop 2>&1 | head -30 || true
      return 1
    fi
  else
    echo "[CI][WARN] gst-inspect-1.0 not available; skipping plugin registration check"
  fi
  echo "[CI] Testing ($preset)"
  # ctest preset may exist only after configure; fallback to manual path
  if ctest --list-presets 2>/dev/null | grep -q "$preset"; then
    ctest --preset "$preset" --output-on-failure --timeout 120
  else
    ctest --test-dir "$ROOT_DIR/build/$upper_dir" --output-on-failure --timeout 120
  fi
  # Enforce minimum test count gate (increase as real tests are added)
  local reported_tests
  reported_tests=$(ctest --show-only=json-v1 --test-dir "$ROOT_DIR/build/$upper_dir" 2>/dev/null | grep -c '"name"') || true
  if [[ ${reported_tests:-0} -lt 1 ]]; then
    echo "[CI][ERROR] Expected at least 1 test; found $reported_tests" >&2
    return 1
  fi
}

# Debug cycle
configure_build_test debug

# Release cycle
configure_build_test release

echo "[CI] Completed CI script successfully"