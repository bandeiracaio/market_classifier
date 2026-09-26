#!/usr/bin/env bash
# Measure compressed and uncompressed sizes of WASM build artifacts.
# Run from the repository root after a successful wasm-release build.
#
# Usage: bash tools/measure-bundle.sh

set -euo pipefail

WASM_DIR="apps/web/static/wasm"
BUILD_DIR="apps/web/build"

echo "=== WASM artifacts (${WASM_DIR}) ==="
if [[ -d "${WASM_DIR}" ]] && ls "${WASM_DIR}"/*.js 2>/dev/null; then
    for f in "${WASM_DIR}"/market_classifier.js "${WASM_DIR}"/market_classifier.wasm; do
        if [[ -f "${f}" ]]; then
            raw=$(du -b "${f}" | cut -f1)
            gz=$(gzip -c -9 "${f}" | wc -c)
            printf "  %-50s  raw: %8d B  gzip-9: %8d B\n" "$(basename "${f}")" "${raw}" "${gz}"
        fi
    done
else
    echo "  No WASM artifacts found. Run: cmake --build --preset wasm-release"
fi

echo ""
echo "=== SvelteKit web build (${BUILD_DIR}) ==="
if [[ -d "${BUILD_DIR}" ]]; then
    total_raw=0
    total_gz=0
    while IFS= read -r -d '' f; do
        raw=$(du -b "${f}" | cut -f1)
        gz=$(gzip -c -9 "${f}" | wc -c)
        total_raw=$((total_raw + raw))
        total_gz=$((total_gz + gz))
    done < <(find "${BUILD_DIR}" -type f \( -name '*.js' -o -name '*.css' -o -name '*.html' -o -name '*.wasm' \) -print0)
    printf "  Total JS+CSS+HTML+WASM raw:   %8d B  (%d KB)\n" "${total_raw}" "$((total_raw / 1024))"
    printf "  Total JS+CSS+HTML+WASM gzip:  %8d B  (%d KB)\n" "${total_gz}" "$((total_gz / 1024))"

    WARN_BYTES=$((12 * 1024 * 1024))
    if (( total_gz > WARN_BYTES )); then
        echo ""
        echo "  WARNING: compressed payload ${total_gz} B exceeds 12 MB hard warning from SPECIFICATION.md §18."
        exit 1
    fi
else
    echo "  No web build found. Run: pnpm build (from apps/web/)"
fi
