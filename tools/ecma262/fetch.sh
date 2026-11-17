#!/bin/sh
#
# Fetch ECMA-262's source text (tc39/ecma262's spec.html) at the 16th and the
# 17th edition's commits into third_party/ecma262/<commit>/spec.html, checking
# each against its pinned SHA-256. They are the inputs of the clause
# concordance (documentation/clause-concordance.tsv), which `make-concordance.py`
# builds and `make check-clauses` rebuilds and compares.
#
# The commits are the ones the tags `es2025` and `es2026` of
# github.com/tc39/ecma262 name (checked with `git ls-remote` on 2026-11-17).
# A tag can be moved; a commit cannot, so the commit is what is pinned.
# `es2026` is a release branch cut from main on 2026-03-31 (its merge base
# with main), so its text is main's of that day plus one editorial commit.
#
# Licence: the specification text is Ecma International's, under its
# copyright and software licence (the repository's LICENSE.md); only the
# concordance, which holds clause numbers and titles, is published here.
#
# Usage:  tools/ecma262/fetch.sh
#
# Copyright 2026 by Corey Pennycuff

set -eu

root=$(cd "$(dirname "$0")/../.." && pwd)
sums="$root/tools/ecma262/SHA256SUMS"

sha256() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | awk '{print $1}'
  else
    shasum -a 256 "$1" | awk '{print $1}'
  fi
}

# commit edition
while read -r commit edition; do
  case $commit in ''|\#*) continue ;; esac
  dest="$root/third_party/ecma262/$commit"
  want=$(awk -v c="$commit" '$2 == c {print $1}' "$sums")
  [ -n "$want" ] || { printf 'fetch.sh: %s has no digest in tools/ecma262/SHA256SUMS\n' "$commit" >&2; exit 1; }
  if [ -s "$dest/spec.html" ] && [ "$(sha256 "$dest/spec.html")" = "$want" ]; then
    printf 'have    ecma262 %s (%s)\n' "$commit" "$edition"
    continue
  fi
  printf 'fetch   ecma262 %s (%s)\n' "$commit" "$edition"
  mkdir -p "$dest"
  curl --fail --silent --show-error --location \
      --output "$dest/spec.html.partial" \
      "https://raw.githubusercontent.com/tc39/ecma262/$commit/spec.html"
  got=$(sha256 "$dest/spec.html.partial")
  if [ "$got" != "$want" ]; then
    rm -f "$dest/spec.html.partial"
    printf 'fetch.sh: spec.html at %s has digest %s, not the pinned %s\n' "$commit" "$got" "$want" >&2
    exit 1
  fi
  mv "$dest/spec.html.partial" "$dest/spec.html"
done <"$root/tools/ecma262/EDITIONS"

printf '\nECMA-262 sources are in %s/third_party/ecma262\n' "$root"
