#!/usr/bin/env bash
# Clone the AOSP sources from repos.json, then hand off to patch-source.sh for the
# in-place source fixups. Runs identically in CI and in `docker run`.
#
#   TAG       AOSP source tag/branch to clone (default: master)
#   ROOTDIR   checkout root holding repos.json / patches/ (default: cwd)
#   TARGET    target triple, forwarded to patch-source.sh (optional here)
set -euo pipefail

ROOTDIR="${ROOTDIR:-$PWD}"
TAG="${TAG:-master}"
SCRIPT_DIR="$(cd -- "$(dirname -- "$0")" && pwd)"
cd "$ROOTDIR"

log() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }

# --- clone every repo from repos.json (shallow, detached) -------------------
# Parallel: only frameworks/base is slow (measured 68 s of a 166 s serial
# total on 2026-10-15); the other 39 clone in 1-12 s each, so serializing them
# just stacks handshake latency. Bounded parallelism (CLONE_JOBS, default 8)
# drops the wall time toward the slowest single repo. xargs exits non-zero if
# any clone fails, which aborts the script under `set -e` -- and patch-source.sh
# only runs after this pipeline finishes, so it never sees a partial tree.
log "Cloning AOSP sources @ $TAG"
export TAG
jq -r '.[] | .path, .url' repos.json \
  | xargs -P "${CLONE_JOBS:-8}" -n 2 bash -c '
      path="$1"; url="$2"; t0=$SECONDS
      if [ -d "$path" ]; then
        printf "\033[1;34m==>\033[0m exists: %s\n" "$path"
      else
        printf "\033[1;34m==>\033[0m clone:  %s\n" "$path"
        git clone -c advice.detachedHead=false --depth 1 --branch "$TAG" "$url" "$path" \
          && printf "\033[1;34m==>\033[0m cloned: %s (%ss)\n" "$path" "$((SECONDS - t0))"
      fi
    ' _

# --- in-place source fixups -------------------------------------------------
# FETCH_ONLY=1 stops right after the clones, so CI can cache the pristine tree
# and still run the fixups on every build (patch-source.sh is not idempotent).
if [ -n "${FETCH_ONLY:-}" ]; then
  log "FETCH_ONLY set: stopping before source fixups"
  exit 0
fi
TARGET="${TARGET:-}" ROOTDIR="$ROOTDIR" "$SCRIPT_DIR/patch-source.sh"

log "Sources ready under $ROOTDIR/src"
