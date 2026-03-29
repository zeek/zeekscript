#!/bin/sh
# Generate .rep files for all test input .zeek files using emit_ast.py.
# Re-run after changing the emitter to regenerate all representations.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
EMITTER="$SCRIPT_DIR/../zeekscript/emit_ast.py"
FMT_DIR="$SCRIPT_DIR/formatting"

count=0
errors=0

for f in "$FMT_DIR"/test*.zeek; do
    # Skip .fmt.zeek files — we only want the input files
    case "$f" in *.fmt.zeek) continue;; esac

    rep="${f%.zeek}.rep"
    if python3.11 "$EMITTER" "$f" > "$rep" 2>/dev/null; then
        count=$((count + 1))
    else
        echo "FAIL: $f" >&2
        rm -f "$rep"
        errors=$((errors + 1))
    fi
done

echo "Generated $count .rep files ($errors failures)"
