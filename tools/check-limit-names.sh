#!/bin/sh
# Fails when the registry of limit names (tools/limit-names.txt) and the
# members of GLECMA_Limits disagree, or when a name already committed has been
# removed, renamed or moved.
#
# Usage: check-limit-names.sh [root]
set -u
root=${1:-.}
registry=$root/tools/limit-names.txt
header=$root/include/ghoti.io/lang-ecmascript/limits.h
fail() { printf 'check-limit-names: %s\n' "$1" >&2; exit 1; }

[ -f "$registry" ] || fail "$registry is missing"
[ -f "$header" ] || fail "$header is missing"

names=$(grep -v '^#' "$registry" | grep -v '^[[:space:]]*$')
[ -n "$names" ] || fail "the registry holds no names, so this gate checks nothing"

# The members of the struct, less the size-first header.
members=$(awk '/^typedef struct GLECMA_Limits/{on=1;next} /^} GLECMA_Limits;/{on=0}
  on && /;/ { line=$0; sub(/\/\/\/<.*/,"",line); sub(/;.*/,"",line); n=split(line,a,/[ \t*]+/); print a[n] }' "$header" \
  | grep -v '^size$' | grep -v '^reserved$')
[ -n "$members" ] || fail "found no members in GLECMA_Limits; the pattern has rotted"

for n in $names; do
  printf '%s\n' "$members" | grep -qx "$n" || fail "registered name '$n' is not a member of GLECMA_Limits (removed or renamed)"
done
for m in $members; do
  printf '%s\n' "$names" | grep -qx "$m" || fail "member '$m' of GLECMA_Limits is not in the registry"
done

# Append-only against the committed registry, when there is one to compare.
if git -C "$root" rev-parse --git-dir >/dev/null 2>&1 &&
    git -C "$root" cat-file -e HEAD:tools/limit-names.txt 2>/dev/null; then
  old=$(git -C "$root" show HEAD:tools/limit-names.txt | grep -v '^#' | grep -v '^[[:space:]]*$')
  i=0
  for o in $old; do
    i=$((i + 1))
    now=$(printf '%s\n' "$names" | sed -n "${i}p")
    [ "$now" = "$o" ] || fail "committed name '$o' (entry $i) is now '${now:-<missing>}': the registry is append-only"
  done
fi
printf 'check-limit-names: %s names, all members registered\n' "$(printf '%s\n' "$names" | wc -l)"
