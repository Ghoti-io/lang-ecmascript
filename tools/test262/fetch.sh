#!/bin/sh
#
# Fetch the whole tc39/test262 repository at the pinned commit into
# third_party/test262/<commit>/, and check every file against the committed
# list of checksums.
#
# The suite is not committed here: it is somebody else's corpus, reproducible
# from a commit and a URL, and a copy in this repository would be a snapshot
# that quietly stops being the thing every other engine is measured against.
# What is committed is the commit (tools/test262/TEST262_COMMIT) and the
# SHA-256 of every file the commit holds (tools/test262/SHA256SUMS), so that a
# published score names the corpus it was measured over and a changed, a
# missing or an extra file is noticed, not absorbed.
#
# How the commit was chosen is written in documentation/design.md ("The
# test262 pin"). Moving it is deliberate: new tests that fail are findings,
# not regressions. To move it, change TEST262_COMMIT, run this script with
# --write-sums, and review the difference between the old and new lists.
#
# Licence: test262 is under the BSD 3-clause licence (its LICENSE file, which
# is part of the fetched tree); each test carries its own copyright header.
# Nothing of it is copied into a repository of ours.
#
# `suite/install.sh --test` runs this first, with the network. `make test`
# does not fetch: a gate that fetched would pass in a place with a network and
# fail without, so a missing corpus fails and names this script.
#
# Usage:  tools/test262/fetch.sh [--write-sums]
#
# Copyright 2026 by Corey Pennycuff

set -eu

root=$(cd "$(dirname "$0")/../.." && pwd)
commit=$(cat "$root/tools/test262/TEST262_COMMIT")
dest="$root/third_party/test262/$commit"
write_sums=no
[ "${1:-}" = "--write-sums" ] && write_sums=yes

case $commit in
  *[!0-9a-f]*|'') printf 'fetch.sh: TEST262_COMMIT is not a full commit id\n' >&2; exit 1 ;;
esac
[ ${#commit} -eq 40 ] || { printf 'fetch.sh: TEST262_COMMIT is not 40 characters\n' >&2; exit 1; }

if [ -d "$dest" ] && [ "$write_sums" = no ]; then
  printf 'have    third_party/test262/%s\n' "$commit"
else
  printf 'fetch   third_party/test262/%s\n' "$commit"
  work="$dest.partial"
  rm -rf "$work"
  mkdir -p "$work"
  # --fail so that an HTML error page never lands on disk looking like data.
  curl --fail --silent --show-error --location \
      --output "$work/archive.tar.gz" \
      "https://codeload.github.com/tc39/test262/tar.gz/$commit"
  mkdir "$work/tree"
  tar -xzf "$work/archive.tar.gz" -C "$work/tree" --strip-components=1 --no-same-owner
  if [ "$write_sums" = yes ]; then
    python3 "$root/tools/check-test262.py" sums --root "$work/tree" \
        --sums "$root/tools/test262/SHA256SUMS"
  fi
  python3 "$root/tools/check-test262.py" integrity --root "$work/tree" \
      --sums "$root/tools/test262/SHA256SUMS" ||
      { rm -rf "$work"; printf 'fetch.sh: the fetched tree does not match SHA256SUMS\n' >&2; exit 1; }
  rm -rf "$dest"
  mv "$work/tree" "$dest"
  rm -rf "$work"
fi

printf '\ntest262 %s is in %s\n' "$commit" "$dest"
