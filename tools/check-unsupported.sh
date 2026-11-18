#!/bin/sh
#
# Fail if GLECMA_ERR_UNSUPPORTED is returned from a place that is not a feature
# the build may lack.
#
# While the parser was being built, work not yet built answered
# GLECMA_ERR_UNSUPPORTED, which never passes a negative test of the conformance
# suite. It no longer may: the result means a feature the build does not define
# (a name in a feature set the build lacks, answered by the parse and tokenizer
# entries) or an operation a kind of stream cannot do (a seek or a size on a
# callback stream), and nothing else. So the source files that mention it are
# named here, one by one, with the reason, and a file that mentions it and is
# not named fails; so does a named file that no longer mentions it (a stale
# row says a path is there that is not).
#
# Usage: check-unsupported.sh <root>   (the library root)
#
# Fails on an empty population: a tree with no source measured nothing.

set -eu

ROOT="${1:?usage: check-unsupported.sh <root>}"
cd "$ROOT"

# file: why it may say GLECMA_ERR_UNSUPPORTED
allowed="src/core/core.c src/parse/entry.c src/lex/lexer.c src/lex/lexer.h src/source/stream.c"
# (core.c: the name of the result and the conversion of runtime-core's own;
#  entry.c, lexer.c and lexer.h: a feature set naming a feature the build
#  lacks; stream.c: seek and size on a callback stream.)

total=0
found=
for file in $(find src -type f \( -name '*.c' -o -name '*.h' \) | sort); do
  total=$((total + 1))
  if grep -q 'GLECMA_ERR_UNSUPPORTED' "$file"; then
    found="$found $file"
    case " $allowed " in
      *" $file "*) ;;
      *)
        printf 'check-unsupported: %s mentions GLECMA_ERR_UNSUPPORTED, and it is not a place that can mean a feature the build lacks or an operation a stream cannot do\n' "$file" >&2
        exit 1 ;;
    esac
  fi
done
[ "$total" -gt 0 ] || { printf 'check-unsupported: no source under src/; this gate is measuring nothing\n' >&2; exit 1; }
for file in $allowed; do
  case " $found " in
    *" $file "*) ;;
    *)
      printf 'check-unsupported: %s is allowed to say GLECMA_ERR_UNSUPPORTED and no longer does; remove it from the list\n' "$file" >&2
      exit 1 ;;
  esac
done
printf 'check-unsupported: %d source files, %d name GLECMA_ERR_UNSUPPORTED, each for a feature the build lacks or a stream operation\n' \
  "$total" "$(printf '%s\n' $found | wc -l)"
