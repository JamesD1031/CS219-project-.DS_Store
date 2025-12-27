#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT_DIR/build/MiniFileExplorer"
TEST_DIR="$ROOT_DIR/.tmp_minifileexplorer_smoke_$$"

cleanup() {
  if [[ -d "$TEST_DIR" ]]; then
    rm -f "$TEST_DIR"/note.txt "$TEST_DIR"/new_note.txt "$TEST_DIR"/big.bin "$TEST_DIR"/small.bin || true
    rm -f "$TEST_DIR"/backup/note.txt || true
    rmdir "$TEST_DIR"/backup 2>/dev/null || true
    rm -f "$TEST_DIR"/data_file || true
    rmdir "$TEST_DIR"/data 2>/dev/null || true
    rmdir "$TEST_DIR" 2>/dev/null || true
  fi
}
trap cleanup EXIT

echo "[smoke] build"
make -C "$ROOT_DIR" >/dev/null

mkdir -p "$TEST_DIR"

python3 - <<PY
from pathlib import Path
root = Path(r"$TEST_DIR")
(root / "big.bin").write_bytes(b"0" * 2000)
(root / "small.bin").write_bytes(b"0")
PY

touch -t 202001010000 "$TEST_DIR/big.bin" || true
touch -t 202101010000 "$TEST_DIR/small.bin" || true

echo "[smoke] ls -s ordering"
OUT_LS_S="$(printf "ls -s\nexit\n" | "$BIN" "$TEST_DIR")"
echo "$OUT_LS_S" | grep -F "Name" >/dev/null
LINE_BIG="$(echo "$OUT_LS_S" | grep -n "^big\\.bin" | head -n1 | cut -d: -f1)"
LINE_SMALL="$(echo "$OUT_LS_S" | grep -n "^small\\.bin" | head -n1 | cut -d: -f1)"
if [[ -z "${LINE_BIG}" || -z "${LINE_SMALL}" ]]; then
  echo "[smoke][fail] ls -s missing big.bin/small.bin"
  exit 1
fi
if (( LINE_BIG > LINE_SMALL )); then
  echo "[smoke][fail] ls -s order unexpected (big should be before small)"
  exit 1
fi

echo "[smoke] ls -t ordering"
OUT_LS_T="$(printf "ls -t\nexit\n" | "$BIN" "$TEST_DIR")"
LINE_BIG="$(echo "$OUT_LS_T" | grep -n "^big\\.bin" | head -n1 | cut -d: -f1)"
LINE_SMALL="$(echo "$OUT_LS_T" | grep -n "^small\\.bin" | head -n1 | cut -d: -f1)"
if [[ -z "${LINE_BIG}" || -z "${LINE_SMALL}" ]]; then
  echo "[smoke][fail] ls -t missing big.bin/small.bin"
  exit 1
fi
if (( LINE_SMALL > LINE_BIG )); then
  echo "[smoke][fail] ls -t order unexpected (small should be before big)"
  exit 1
fi

echo "[smoke] core commands"
OUT_MAIN="$(
  printf "help\nstat\nmkdir data\nmkdir data\ntouch note.txt\ntouch note.txt\nls\nstat note.txt\nsearch note\nmkdir backup\ncp note.txt backup/\ncp note.txt backup/\nn\ncp note.txt backup/\ny\nmv note.txt new_note.txt\ndu backup\nrm new_note.txt\nn\nrm new_note.txt\ny\nrmdir no_such\nexit\n" | "$BIN" "$TEST_DIR"
)"

echo "$OUT_MAIN" | grep -F "Supported commands:" >/dev/null
echo "$OUT_MAIN" | grep -F "Missing target: Please enter'stat [name]'" >/dev/null
echo "$OUT_MAIN" | grep -F "Directory already exists: data" >/dev/null
echo "$OUT_MAIN" | grep -F "File already exists: note.txt" >/dev/null
echo "$OUT_MAIN" | grep -F "Search results for 'note'" >/dev/null
echo "$OUT_MAIN" | grep -F "File exists in target: Overwrite? (y/n)" >/dev/null
echo "$OUT_MAIN" | grep -F "Are you sure to delete new_note.txt? (y/n)" >/dev/null
echo "$OUT_MAIN" | grep -F "Directory not found: no_such" >/dev/null
echo "$OUT_MAIN" | grep -F "MiniFileExplorer closed successfully" >/dev/null

echo "[smoke] OK"

