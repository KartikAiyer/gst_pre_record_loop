#!/usr/bin/env bash
# T039-OPTIONAL-2: Memory leak testing with Valgrind for Linux x86_64
# Requires: valgrind, GStreamer 1.0 development packages
# Platform: Linux only (valgrind not available on Apple Silicon macOS)
# For macOS ASan testing, see test_leaks.sh

set -euo pipefail

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build/Debug"
LOG_DIR="${PROJECT_ROOT}/build/valgrind_logs"
SUPPRESSIONS_FILE="${PROJECT_ROOT}/tests/memory/gstreamer.supp"

# Platform check
if [[ "$(uname -s)" == "Darwin" ]]; then
  echo -e "${RED}[VALGRIND] Error: Valgrind not available on macOS (especially Apple Silicon)${NC}"
  echo "[VALGRIND] Use test_leaks.sh (ASan-based) instead for macOS testing"
  exit 1
fi

echo -e "${YELLOW}[VALGRIND] Memory Leak Testing with Valgrind${NC}"
echo "[VALGRIND] Platform: $(uname -s) $(uname -m)"

# Check if valgrind is installed
if ! command -v valgrind &> /dev/null; then
  echo -e "${RED}[VALGRIND] Error: valgrind not found${NC}"
  echo "[VALGRIND] Install with: sudo apt-get install valgrind  # Ubuntu/Debian"
  echo "[VALGRIND]           or: sudo dnf install valgrind      # Fedora/RHEL"
  exit 1
fi

# Clean previous logs
mkdir -p "${LOG_DIR}"
rm -f "${LOG_DIR}"/*.txt

# Step 1: Build (standard Debug build, no ASan needed)
echo "[VALGRIND] Step 1/3: Ensuring Debug build exists..."
cd "${PROJECT_ROOT}"

if [ ! -f "${BUILD_DIR}/CMakeCache.txt" ]; then
  echo "[VALGRIND] Building project..."
  conan install . --build=missing --settings=build_type=Debug
  cmake --preset=conan-debug
  cmake --build --preset=conan-debug --parallel 6
else
  echo "[VALGRIND] Using existing Debug build"
fi

# Step 2: Create/check suppressions file
echo "[VALGRIND] Step 2/3: Configuring Valgrind suppressions..."

if [ ! -f "${SUPPRESSIONS_FILE}" ]; then
  echo "[VALGRIND] Creating GStreamer suppressions file..."
  cat > "${SUPPRESSIONS_FILE}" << 'EOF'
# GStreamer Valgrind suppressions
# GStreamer plugins may have intentional "leaks" for cached global state
# See: https://gitlab.freedesktop.org/gstreamer/gstreamer/-/blob/main/gstreamer.supp

{
   gstreamer-registry-cache
   Memcheck:Leak
   ...
   fun:gst_registry_*
}

{
   gstreamer-plugin-loader
   Memcheck:Leak
   ...
   fun:gst_plugin_*
}

{
   glib-type-system
   Memcheck:Leak
   ...
   fun:g_type_*
}

{
   glib-quark-table
   Memcheck:Leak
   ...
   fun:g_quark_*
}
EOF
  echo "[VALGRIND] Created ${SUPPRESSIONS_FILE}"
else
  echo "[VALGRIND] Using existing suppressions: ${SUPPRESSIONS_FILE}"
fi

# Step 3: Run tests under Valgrind
echo "[VALGRIND] Step 3/3: Running leak detection tests..."

# Valgrind options:
# --leak-check=full                      : detailed leak information
# --show-leak-kinds=definite,indirect    : show only definite and indirect leaks
# --errors-for-leak-kinds=definite,indirect : only error on definite/indirect (not "possibly lost")
# --track-origins=yes                    : track origin of uninitialized values
# --error-exitcode=1                     : exit with code 1 if errors found
# --suppressions=file                    : suppress known GStreamer "leaks"
# --gen-suppressions=no                  : don't generate suppression patterns (set to 'all' for debugging)

VALGRIND_OPTS=(
  --leak-check=full
  --show-leak-kinds=definite,indirect
  --errors-for-leak-kinds=definite,indirect
  --track-origins=yes
  --error-exitcode=1
  --suppressions="${SUPPRESSIONS_FILE}"
  --log-file="${LOG_DIR}/valgrind_%p.txt"
  --verbose
)

# Tests to run (focus on state transitions with complex refcounting)
TESTS=(
  "unit_test_rearm_sequence"
  "unit_test_flush_seek_reset"
  "unit_test_no_refcount_critical"
)

TEST_COUNT=${#TESTS[@]}
PASSED=0
FAILED=0

for test_name in "${TESTS[@]}"; do
  echo ""
  echo "[VALGRIND] Running: ${test_name}"
  
  # Set GST_PLUGIN_PATH for test
  export GST_PLUGIN_PATH="${BUILD_DIR}/gstprerecordloop:${GST_PLUGIN_PATH:-}"
  
  # Run test under Valgrind
  if valgrind "${VALGRIND_OPTS[@]}" \
     "${BUILD_DIR}/tests/${test_name}" > "${LOG_DIR}/${test_name}_stdout.txt" 2>&1; then
    echo -e "${GREEN}[VALGRIND] ✓ ${test_name} passed (no leaks)${NC}"
    PASSED=$((PASSED + 1))
  else
    echo -e "${RED}[VALGRIND] ✗ ${test_name} failed or leaked${NC}"
    echo "[VALGRIND] See: ${LOG_DIR}/valgrind_*.txt and ${LOG_DIR}/${test_name}_stdout.txt"
    FAILED=$((FAILED + 1))
  fi
done

# Step 4: Analyze results
echo ""
echo "[VALGRIND] ========================================="
echo "[VALGRIND] Analyzing results..."
echo "[VALGRIND] ========================================="

LEAK_DETECTED=0

for log_file in "${LOG_DIR}"/valgrind_*.txt; do
  if [ -f "${log_file}" ]; then
    # Check for definite leaks (ignore "still reachable" from GStreamer globals)
    # Use || true to prevent script exit if grep fails (files without LEAK SUMMARY)
    DEFINITE_LEAKS=$(grep "definitely lost:" "${log_file}" 2>/dev/null | awk '{print $4}' | sed 's/,//g' || echo "0")
    INDIRECT_LEAKS=$(grep "indirectly lost:" "${log_file}" 2>/dev/null | awk '{print $4}' | sed 's/,//g' || echo "0")
    
    if [ "${DEFINITE_LEAKS:-0}" -gt 0 ] || [ "${INDIRECT_LEAKS:-0}" -gt 0 ]; then
      LEAK_DETECTED=1
      echo -e "${RED}[VALGRIND] ❌ Memory leak in: ${log_file}${NC}"
      echo "[VALGRIND] Definitely lost: ${DEFINITE_LEAKS:-0} bytes"
      echo "[VALGRIND] Indirectly lost: ${INDIRECT_LEAKS:-0} bytes"
      echo ""
      grep -A 20 "LEAK SUMMARY" "${log_file}" || true
    fi
  fi
done

# Step 5: Summary
echo ""
echo "[VALGRIND] ========================================="
echo "[VALGRIND] Test Results: ${PASSED}/${TEST_COUNT} passed, ${FAILED} failed"

if [ ${LEAK_DETECTED} -eq 1 ]; then
  echo -e "${RED}[VALGRIND] ❌ MEMORY LEAKS DETECTED!${NC}"
  echo "[VALGRIND] Review detailed logs in: ${LOG_DIR}/"
  echo "[VALGRIND] To generate suppressions: valgrind --gen-suppressions=all ..."
  exit 1
elif [ ${FAILED} -gt 0 ]; then
  echo -e "${YELLOW}[VALGRIND] ⚠ Tests failed but no leaks detected${NC}"
  exit 1
else
  echo -e "${GREEN}[VALGRIND] ✓ NO MEMORY LEAKS DETECTED${NC}"
  echo "[VALGRIND] All tests passed with clean memory usage"
  exit 0
fi
