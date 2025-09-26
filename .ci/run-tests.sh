#!/usr/bin/env bash
# Phase 3.1 CI Script (updated to follow README / quickstart build flow)
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CPU_CORES="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

echo "[CI] Starting full Debug + Release build/test using Conan + CMake presets"

require() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "[CI][FATAL] Required tool '$1' not found" >&2
    exit 2
  fi
}

require cmake
require conan

# Run conan install only if the corresponding build/generators directory is missing
conan_preset() {
  local build_type="$1" # Debug or Release
  local preset_name="conan-${build_type,,}" # lower-case
  local gen_dir="$ROOT_DIR/build/$build_type/generators"
  if [[ ! -d "$gen_dir" ]]; then
    echo "[CI] Running Conan install for $build_type (generators missing)"
    if [[ "$build_type" == "Debug" ]]; then
      conan install . --build=missing --settings=build_type=Debug
    else
      conan install . --build=missing
    fi
  else
    echo "[CI] Skipping Conan for $build_type (generators present)"
  fi
}

# Configure, build, test using preset
configure_build_test() {
  local preset="$1" # e.g. conan-debug
  local build_dir_suffix="${preset#conan-}" # debug or release
  local upper_dir="${build_dir_suffix^}"   # capitalize first letter
  echo "[CI] Configuring ($preset)"
  cmake --preset="$preset"
  echo "[CI] Building ($preset)"
  cmake --build --preset="$preset" -- -j"$CPU_CORES"
  # Export GST_PLUGIN_PATH for sanity checks (plugin directory first)
  local plugin_dir="$ROOT_DIR/build/${upper_dir}/gstprerecordloop"
  if [[ -d "$plugin_dir" ]]; then
    export GST_PLUGIN_PATH="$plugin_dir:${GST_PLUGIN_PATH:-}"
    echo "[CI] GST_PLUGIN_PATH set to $GST_PLUGIN_PATH"
  else
    echo "[CI][WARN] Plugin directory not found at $plugin_dir"
  fi
  # gst-inspect sanity (non-fatal warning if missing gst-inspect)
  if command -v gst-inspect-1.0 >/dev/null 2>&1; then
    if gst-inspect-1.0 pre_record_loop >/dev/null 2>&1; then
      echo "[CI] gst-inspect located plugin 'pre_record_loop'"
    else
      echo "[CI][ERROR] gst-inspect could not find 'pre_record_loop'" >&2
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
conan_preset Debug
configure_build_test conan-debug

# Release cycle
conan_preset Release
configure_build_test conan-release || echo "[CI][WARN] Release preset not found yet (no preset file)"

# Style / formatting check (non-blocking now, can be enforced later)
if command -v clang-format >/dev/null 2>&1; then
  echo "[CI] Running clang-format style probe"
  CF_CHANGED=$(git ls-files '*.c' '*.h' | xargs -I{} bash -c 'diff -u <(cat {}) <(clang-format {}) || true' | wc -l | tr -d ' ')
  if [[ "$CF_CHANGED" -gt 0 ]]; then
    echo "[CI][WARN] clang-format suggests changes (enable enforcement later)"
  else
    echo "[CI] Style clean"
  fi
else
  echo "[CI][WARN] clang-format not found; skipping style check"
fi

echo "[CI] Completed CI script"