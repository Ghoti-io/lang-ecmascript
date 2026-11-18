#!/bin/sh
#
# Fetch the real-world corpus (tools/corpus/CORPUS.tsv) into
# third_party/corpus/, and check every file against the hashes the table pins.
#
# The corpus is other people's software, under licenses that let it be
# redistributed (MIT, ISC, Apache-2.0); none of it is copied into a repository
# of ours. What is committed is the table: the exact package version and the
# SHA-256 of its tarball, of each file read and of each license file. A gate
# that needs the corpus never fetches (it would pass with a network and fail
# without); it checks the hashes and, when the files are missing, names this
# script.
#
# Usage:  tools/corpus/fetch.sh
#
# Copyright 2026 by Corey Pennycuff
set -eu
root=$(cd "$(dirname "$0")/../.." && pwd)
exec python3 -I "$root/tools/corpus/corpus.py" fetch --root "$root/third_party/corpus"
