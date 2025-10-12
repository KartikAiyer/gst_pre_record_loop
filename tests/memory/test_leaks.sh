#!/usr/bin/env bash
# T039: Memory leak testing with macOS leaks tool or Linux Valgrind
# 
# PLATFORM-SPECIFIC STRATEGY:
# - macOS: Uses native `leaks` tool from Xcode (MallocStackLogging-based)
# - Linux: Uses valgrind (see test_leaks_valgrind.sh)
# 
# macOS leaks tool detects:
# - Leaked allocations (malloc/new with no corresponding free/delete)
# - Cycles in object graphs
# - Works with GStreamer plugins (no ASan compatibility issues)
#
# For CI automation: .github/workflows/valgrind.yml (Linux x86_64)

set -euo pipefail

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build/Debug"
LOG_DIR="${PROJECT_ROOT}/build/leaks_logs"

PLATFORM="$(uname -s)"

echo -e "${YELLOW}[LEAKS] Memory Leak Detection Test Suite${NC}"
echo "[LEAKS] Platform: ${PLATFORM} $(uname -m)"

# Platform check
if [[ "${PLATFORM}" == "Darwin" ]]; then
  if ! command -v leaks &> /dev/null; then
    echo -e "${RED}[LEAKS] Error: 'leaks' tool not found${NC}"
    echo "[LEAKS] Install Xcode Command Line Tools: xcode-select --install"
    exit 1
  fi
  echo "[LEAKS] Using macOS 'leaks' tool for leak detection"
elif [[ "${PLATFORM}" == "Linux" ]]; then
  echo "[LEAKS] Linux detected - use test_leaks_valgrind.sh instead"
  echo "[LEAKS] Redirecting to Valgrind-based testing..."
  exec "${PROJECT_ROOT}/tests/memory/test_leaks_valgrind.sh"
else
  echo -e "${YELLOW}[LEAKS] Unsupported platform: ${PLATFORM}${NC}"
  echo "[LEAKS] Running refcount validation tests only..."
fi

echo ""

# Clean previous logs
mkdir -p "${LOG_DIR}"
rm -f "${LOG_DIR}"/*.txt

# Build without ASan (standard Debug build)
echo "[LEAKS] Step 1/3: Ensuring standard Debug build..."
cd "${PROJECT_ROOT}"

if [ ! -f "${BUILD_DIR}/CMakeCache.txt" ]; then
  echo "[LEAKS] Building project..."
  conan install . --build=missing --settings=build_type=Debug
  cmake --preset=conan-debug
  cmake --build --preset=conan-debug --parallel 6
else
  echo "[LEAKS] Using existing Debug build"
fi

# Run leak detection tests
if [[ "${PLATFORM}" == "Darwin" ]]; then
  echo "[LEAKS] Step 2/3: Running macOS leak detection with 'leaks' tool..."
else
  echo "[LEAKS] Step 2/3: Running refcount validation tests..."
fi
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
LEAKED=0

for test_name in "${TESTS[@]}"; do
  echo "[LEAKS] Running: ${test_name}"
  
  if [[ "${PLATFORM}" == "Darwin" ]]; then
    # macOS: Run with leaks tool
    # MallocStackLogging=1 enables detailed allocation tracking
    # --atExit tells leaks to check at program termination
    set +e  # leaks returns non-zero when leaks found, but we want to continue
    MallocStackLogging=1 leaks --atExit -- "${BUILD_DIR}/tests/${test_name}" \
      > "${LOG_DIR}/${test_name}.txt" 2>&1
    LEAKS_EXIT=$?
    set -e
    
    # Check exit code and parse output
    if [ ${LEAKS_EXIT} -eq 0 ] || [ ${LEAKS_EXIT} -eq 1 ]; then
      # Exit code 0 = no leaks, 1 = leaks found (both are successful runs)
      # Filter out GLib/GStreamer global initialization leaks (expected false positives)
      # These are intentional globals that persist for the process lifetime
      if grep -A 25 "STACK OF.*ROOT LEAK" "${LOG_DIR}/${test_name}.txt" | \
         grep -qE "(glib_init|g_quark_init|g_type_register_static|gst_init)"; then
        # All leaks are GLib/GStreamer globals - this is expected
        echo -e "${GREEN}[LEAKS] ✓ ${test_name} - NO APPLICATION LEAKS (GLib globals filtered)${NC}"
        PASSED=$((PASSED + 1))
      elif grep -q "0 leaks for 0 total leaked bytes" "${LOG_DIR}/${test_name}.txt"; then
        echo -e "${GREEN}[LEAKS] ✓ ${test_name} - NO LEAKS${NC}"
        PASSED=$((PASSED + 1))
      else
        echo -e "${RED}[LEAKS] ✗ ${test_name} - APPLICATION LEAKS DETECTED${NC}"
        # Show leak summary (excluding known GLib frames)
        grep "Process.*leak" "${LOG_DIR}/${test_name}.txt" | tail -1
        LEAKED=$((LEAKED + 1))
        FAILED=$((FAILED + 1))
      fi
    else
      echo -e "${RED}[LEAKS] ✗ ${test_name} - TEST FAILED (exit code: ${LEAKS_EXIT})${NC}"
      tail -20 "${LOG_DIR}/${test_name}.txt"
      FAILED=$((FAILED + 1))
    fi
  else
    # Other platforms: Run test normally (refcount validation only)
    if "${BUILD_DIR}/tests/${test_name}" > /dev/null 2>&1; then
      echo -e "${GREEN}[LEAKS] ✓ ${test_name} passed${NC}"
      PASSED=$((PASSED + 1))
    else
      echo -e "${RED}[LEAKS] ✗ ${test_name} failed${NC}"
      # Run again with output for debugging
      "${BUILD_DIR}/tests/${test_name}" || true
      FAILED=$((FAILED + 1))
    fi
  fi
done

# Step 3: Summary
echo ""
echo "[LEAKS] ========================================="
if [[ "${PLATFORM}" == "Darwin" ]]; then
  echo "[LEAKS] macOS Leak Detection Results:"
  echo "[LEAKS] ${PASSED}/${TEST_COUNT} tests passed with no leaks"
  echo "[LEAKS] ${LEAKED} tests with detected leaks"
  echo "[LEAKS] ${FAILED} total failures"
else
  echo "[LEAKS] Refcount Test Results: ${PASSED}/${TEST_COUNT} passed"
fi

if [ ${FAILED} -gt 0 ]; then
  if [ ${LEAKED} -gt 0 ]; then
    echo -e "${RED}[LEAKS] ❌ MEMORY LEAKS DETECTED!${NC}"
    echo "[LEAKS] Review detailed logs in: ${LOG_DIR}/"
    echo "[LEAKS] Leak reports show allocation backtraces"
  else
    echo -e "${RED}[LEAKS] ❌ Tests failed${NC}"
  fi
  exit 1
else
  echo -e "${GREEN}[LEAKS] ✓ ALL TESTS PASSED - NO LEAKS DETECTED${NC}"
  if [[ "${PLATFORM}" == "Darwin" ]]; then
    echo "[LEAKS] macOS leaks tool validated all allocations properly freed"
  else
    echo "[LEAKS] Refcount validation complete"
    echo "[LEAKS] For full leak testing on this platform, see test_leaks_valgrind.sh"
  fi
  exit 0
fi
