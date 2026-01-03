#!/usr/bin/env bash
set -euo pipefail

KEEP_STATE=0

usage() {
  echo "Usage: $0 [-k|--keep|--no-delete] [<absolute/path/to/rpmb_state.bin>]" >&2
  echo "" >&2
  echo "Default state file: \$(pwd -P)/rpmb_state.bin" >&2
  echo "  -k, --keep, --no-delete   Do not delete the state file before/after the run" >&2
}

# --- parse optional flags (must come before state file) ---
while [[ $# -gt 0 ]]; do
  case "$1" in
    --keep|--no-delete|-k)
      KEEP_STATE=1
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    --)
      shift
      break
      ;;
    -*)
      echo "ERROR: Unknown option: $1" >&2
      usage
      exit 2
      ;;
    *)
      break
      ;;
  esac
done

WORKING_DIRECTORY="$(pwd -P)"

# --- default state file if not provided ---
STATE_FILE="${1:-$WORKING_DIRECTORY/rpmb_state.bin}"

# --- ensure root (re-run via sudo if needed) ---
if [[ "${EUID}" -ne 0 ]]; then
  if [[ "$KEEP_STATE" -eq 1 ]]; then
    exec sudo -- "$0" -k "$STATE_FILE"
  else
    exec sudo -- "$0" "$STATE_FILE"
  fi
fi

# --- validate state file argument ---
if [[ $# -gt 1 ]]; then
  echo "ERROR: Too many arguments." >&2
  usage
  exit 2
fi

if [[ "${STATE_FILE:0:1}" != "/" ]]; then
  echo "ERROR: State file path must be absolute, got: $STATE_FILE" >&2
  exit 2
fi

SCRIPT_PATH="$(cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P)"
BIN="$SCRIPT_PATH/build/rpmbd"

# --- ensure binary exists ---
if [[ ! -x "$BIN" ]]; then
  echo "ERROR: Binary not found or not executable: $BIN" >&2
  exit 3
fi

# --- delete state file before start unless KEEP_STATE ---
if [[ "$KEEP_STATE" -eq 0 && -f "$STATE_FILE" ]]; then
  rm -f -- "$STATE_FILE"
fi

# --- delete state file after run unless KEEP_STATE ---
cleanup() {
  if [[ "$KEEP_STATE" -eq 0 ]]; then
    rm -f -- "$STATE_FILE"
  fi
}
trap cleanup EXIT

# --- run rpmbd (NO exec, so trap works) ---
"$BIN" --state-file "$STATE_FILE"
