#!/bin/sh
# Applies each planted defect in tools/planted/registry.txt to a scratch copy
# of the tree and requires its gate to fail. Before the first plant of each
# gate it runs that gate on the unmodified copy and requires it to pass, so a
# gate that fails for some other reason proves nothing.
#
# Usage: check-planted.sh [--only ID] ; PREFIX and PKG_CONFIG_PATH as for make.
set -u
only=
if [ "${1:-}" = "--only" ]; then only=$2; fi
root=$(cd "$(dirname "$0")/.." && pwd)
registry=$root/tools/planted/registry.txt
fail() { printf 'check-planted: %s\n' "$1" >&2; exit 1; }
[ -f "$registry" ] || fail "$registry is missing"
[ -n "${PREFIX:-}" ] || fail "PREFIX is not set (it is the prefix the dependencies are installed in)"

scratch=$(mktemp -d "${TMPDIR:-/tmp}/glecma-planted.XXXXXX") || fail "no scratch directory"
trap 'rm -r "$scratch"' EXIT INT TERM

fresh_copy() {
  work=$scratch/tree
  rm -rf "$work"
  mkdir -p "$work"
  (cd "$root" && git ls-files -z --cached --others --exclude-standard | xargs -0 cp --parents -t "$work")
  # Reuse the built objects so a plant rebuilds only what it touches.
  [ -d "$root/build" ] && cp -a "$root/build" "$work/build"
}

controlled=" "
total=0
while IFS='|' read -r id name defect patch gate; do
  case $id in ''|\#*) continue ;; esac
  [ -z "$only" ] || [ "$only" = "$id" ] || continue
  total=$((total + 1))
  fresh_copy
  case $controlled in
    *" $gate "*) ;;
    *)
      (cd "$work" && make "$gate" PREFIX="$PREFIX" >"$scratch/control.log" 2>&1) ||
        { tail -20 "$scratch/control.log" >&2; fail "gate '$gate' fails on the unmodified tree, so it proves nothing"; }
      controlled="$controlled$gate "
      fresh_copy ;;
  esac
  (cd "$work" && patch -p1 -s <"$root/tools/planted/$patch") || fail "$id ($name): the patch does not apply"
  if (cd "$work" && make "$gate" PREFIX="$PREFIX" >"$scratch/plant.log" 2>&1); then
    fail "$id ($name): gate '$gate' PASSED with the defect planted: $defect"
  fi
  printf 'check-planted: %s %s: gate %s failed, as it must\n' "$id" "$name" "$gate"
done <"$registry"
[ "$total" -gt 0 ] || fail "no planted defect ran"
printf 'check-planted: %s defects, every gate failed on its own\n' "$total"
