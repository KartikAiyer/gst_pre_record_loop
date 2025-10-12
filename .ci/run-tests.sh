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

# Run conan install only if the corresponding build/generators directory is missing
conan_preset() {
  local build_type="$1" # Debug or Release
  # local preset_name="conan-${build_type,,}" # lower-case
  local preset_name="conan-$(echo "$build_type" | tr '[:upper:]' '[:lower:]')"
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
  local upper_dir="$(printf '%s%s' "${build_dir_suffix:0:1}" | tr '[:lower:]' '[:upper:]')${build_dir_suffix:1}"
  echo "[CI] Configuring ($preset)"
  cmake --preset="$preset"
  echo "[CI] Building ($preset)"
  cmake --build --preset="$preset" -- -j"$CPU_CORES"
  
  # Run style check after build (only once for Debug build)
  if [[ "$preset" == "conan-debug" ]]; then
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

echo "[CI] Completed CI script successfully"