#!/usr/bin/env bash
# T039: Memory testing documentation for macOS/Apple Silicon
# 
# LIMITATION: AddressSanitizer (ASan) on macOS has compatibility issues with
# dynamically loaded GStreamer plugins. The GStreamer core libraries must also
# be built with ASan for proper operation, which is not the case with Homebrew.
#
# MEMORY VALIDATION STRATEGY:
# 1. Unit tests with explicit refcount assertions (unit_test_no_refcount_critical)
# 2. CTest memory test target runs refcount-focused tests
# 3. Linux CI with Valgrind for leak detection (see .github/workflows/valgrind.yml)
# 4. Manual testing: Rebuild GStreamer with ASan, or use macOS Instruments/leaks tool
#
# For immediate leak testing, use Linux with test_leaks_valgrind.sh (T039-OPTIONAL-2)

set -euo pipefail

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build/Debug"

echo -e "${YELLOW}[MEMORY] Memory Validation Test Suite (macOS)${NC}"
echo "[MEMORY] Platform: $(uname -s) $(uname -m)"
echo ""
echo -e "${YELLOW}[MEMORY] ASan Limitation on macOS:${NC}"
echo "[MEMORY] GStreamer plugins require ASan-built core libraries (not available via Homebrew)"
echo "[MEMORY] Using refcount-focused unit tests instead for local validation"
echo "[MEMORY] For full leak detection, see:"
echo "[MEMORY]   - tests/memory/test_leaks_valgrind.sh (Linux)"
echo "[MEMORY]   - .github/workflows/valgrind.yml (CI)"
echo ""

# Build without ASan (standard Debug build)
echo "[MEMORY] Step 1/2: Ensuring standard Debug build..."
cd "${PROJECT_ROOT}"

if [ ! -f "${BUILD_DIR}/CMakeCache.txt" ]; then
  echo "[MEMORY] Building project..."
  conan install . --build=missing --settings=build_type=Debug
  cmake --preset=conan-debug
  cmake --build --preset=conan-debug --parallel 6
else
  echo "[MEMORY] Using existing Debug build"
fi

# Run refcount-critical tests
echo "[MEMORY] Step 2/2: Running refcount validation tests..."
echo ""

# Tests that validate proper reference counting and cleanup
TESTS=(
  "unit_test_no_refcount_critical"
  "unit_test_rearm_sequence"
  "unit_test_flush_seek_reset"
)

export GST_PLUGIN_PATH="${BUILD_DIR}/gstprerecordloop:${GST_PLUGIN_PATH:-}"

TEST_COUNT=${#TESTS[@]}
PASSED=0
FAILED=0

for test_name in "${TESTS[@]}"; do
  echo "[MEMORY] Running: ${test_name}"
  
  if "${BUILD_DIR}/tests/${test_name}" > /dev/null 2>&1; then
    echo -e "${GREEN}[MEMORY] ✓ ${test_name} passed${NC}"
    PASSED=$((PASSED + 1))
  else
    echo -e "${RED}[MEMORY] ✗ ${test_name} failed${NC}"
    # Run again with output for debugging
    "${BUILD_DIR}/tests/${test_name}" || true
    FAILED=$((FAILED + 1))
  fi
done

# Summary
echo ""
echo "[MEMORY] ========================================="
echo "[MEMORY] Refcount Test Results: ${PASSED}/${TEST_COUNT} passed"

if [ ${FAILED} -gt 0 ]; then
  echo -e "${RED}[MEMORY] ❌ Refcount tests failed${NC}"
  echo "[MEMORY] Tests validate proper mini-object refcounting and cleanup"
  exit 1
else
  echo -e "${GREEN}[MEMORY] ✓ ALL REFCOUNT TESTS PASSED${NC}"
  echo "[MEMORY] No critical refcount issues detected"
  echo ""
  echo -e "${YELLOW}[MEMORY] Next Steps for Full Memory Validation:${NC}"
  echo "[MEMORY] 1. Run on Linux: bash tests/memory/test_leaks_valgrind.sh"
  echo "[MEMORY] 2. Check CI: .github/workflows/valgrind.yml (automated on PR)"
  echo "[MEMORY] 3. Manual: Use macOS Instruments/leaks tool for deep analysis"
  exit 0
fi
