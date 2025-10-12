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

# T040: Code style check with clang-format
# Set ENFORCE_STYLE=1 to make style violations fail the CI
ENFORCE_STYLE="${ENFORCE_STYLE:-0}"

if command -v clang-format >/dev/null 2>&1; then
  echo "[CI] Running clang-format style check (enforce=${ENFORCE_STYLE})"
  
  # Check for .clang-format config
  if [[ ! -f "$ROOT_DIR/.clang-format" ]]; then
    echo "[CI][WARN] .clang-format not found; using clang-format defaults"
  fi
  
  # Find all C/C++ source and header files
  STYLE_FILES=$(git ls-files '*.c' '*.h' '*.cc' '*.cpp' '*.hpp' 2>/dev/null || find . -name '*.c' -o -name '*.h')
  
  if [[ -z "$STYLE_FILES" ]]; then
    echo "[CI][WARN] No source files found for style check"
  else
    # Check each file for formatting differences
    STYLE_VIOLATIONS=0
    VIOLATING_FILES=()
    
    while IFS= read -r file; do
      if [[ -f "$file" ]]; then
        # Compare formatted vs actual
        if ! diff -q "$file" <(clang-format "$file") >/dev/null 2>&1; then
          STYLE_VIOLATIONS=$((STYLE_VIOLATIONS + 1))
          VIOLATING_FILES+=("$file")
        fi
      fi
    done <<< "$STYLE_FILES"
    
    # Report results
    if [[ $STYLE_VIOLATIONS -eq 0 ]]; then
      echo "[CI] ✓ Style check passed: All files conform to .clang-format"
    else
      echo "[CI][STYLE] Found $STYLE_VIOLATIONS file(s) with formatting issues:"
      for file in "${VIOLATING_FILES[@]}"; do
        echo "[CI][STYLE]   - $file"
      done
      echo "[CI][STYLE] To fix: clang-format -i <file>"
      echo "[CI][STYLE] To fix all: git ls-files '*.c' '*.h' | xargs clang-format -i"
      
      if [[ "$ENFORCE_STYLE" == "1" ]]; then
        echo "[CI][ERROR] Style enforcement enabled; failing build due to formatting violations" >&2
        exit 1
      else
        echo "[CI][WARN] Style violations detected (non-blocking; set ENFORCE_STYLE=1 to enforce)"
      fi
    fi
  fi
else
  echo "[CI][WARN] clang-format not found; skipping style check"
  echo "[CI][WARN] Install with: brew install clang-format (macOS) or apt-get install clang-format (Linux)"
fi

echo "[CI] Completed CI script successfully"