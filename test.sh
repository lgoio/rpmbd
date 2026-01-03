#!/usr/bin/env bash
set -euo pipefail

WORKING_DIRECTORY="$(pwd -P)"

CLEAN_ALL=0
if [[ "${1:-}" == "--clean-all" ]]; then
  CLEAN_ALL=1
  shift
fi

# --- ensure root (re-run via sudo if needed) ---
if [[ "${EUID}" -ne 0 ]]; then
  exec sudo -- "$0" ${CLEAN_ALL:+--clean-all} "$WORKING_DIRECTORY"
fi

# --- guard / usage ---
if [[ $# -ne 1 ]]; then
  echo "Usage: $0 [--clean-all] <working-directory>" >&2
  exit 2
fi

cd "$1"

DEV="/dev/mmcblk2rpmb"

# --- cleanup (also on errors) ---
cleanup() {
  rm -f out.bin
  if [[ "$CLEAN_ALL" -eq 1 ]]; then
    rm -f key.bin data.bin
  fi
}
trap cleanup EXIT

# -------------------------------------------------
# Create key.bin / data.bin if missing
# -------------------------------------------------
if [[ ! -f key.bin ]]; then
  echo "[init] key.bin missing -> creating random 32-byte RPMB key"
  dd if=/dev/urandom of=key.bin bs=32 count=1 status=none
fi

if [[ ! -f data.bin ]]; then
  echo "[init] data.bin missing -> creating random 256-byte data block"
  dd if=/dev/urandom of=data.bin bs=256 count=1 status=none
fi

# -------------------------------------------------
# RPMB Test Sequence
# -------------------------------------------------
echo "[1] Program key"
mmc rpmb write-key "$DEV" key.bin

echo "[2] Write block 0"
mmc rpmb write-block "$DEV" 0 data.bin key.bin

echo "[3] Read counter"
mmc rpmb read-counter "$DEV"

echo "[4] Read back block 0 -> out.bin"
rm -f out.bin
mmc rpmb read-block "$DEV" 0 1 out.bin key.bin

echo "[5] Compare output"
md5sum out.bin data.bin

echo "[6] Write block 0 again"
mmc rpmb write-block "$DEV" 0 data.bin key.bin

echo "[7] Read counter again"
mmc rpmb read-counter "$DEV"

echo "[8] Read back block 0 -> out.bin"
rm -f out.bin
mmc rpmb read-block "$DEV" 0 1 out.bin key.bin

echo "[9] Compare output again"
md5sum out.bin data.bin

echo "Done."
