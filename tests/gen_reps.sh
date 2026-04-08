#!/bin/sh
# Generate .rep files for all test input .zeek files using emit_ast.py.
# Re-run after changing the emitter to regenerate all representations.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
EMITTER="$SCRIPT_DIR/../zeekscript/emit_ast.py"
FORMATTER="$SCRIPT_DIR/../src/clz-format"
FMT_DIR="$SCRIPT_DIR/formatting"

count=0
errors=0

for f in "$FMT_DIR"/test*.zeek; do
    # Skip .fmt.zeek files — we only want the input files
    case "$f" in *.fmt.zeek) continue;; esac

    rep="${f%.zeek}.rep"
    if python3.11 "$EMITTER" "$f" > "$rep" 2>/dev/null; then
        # Normalize through a parse-dump cycle so the .rep
        # matches the parser's internal representation
        # (e.g., pre-comment push-down into BODY).
        if [ -x "$FORMATTER" ]; then
            "$FORMATTER" --dump "$rep" > "$rep.tmp" 2>/dev/null \
                && mv "$rep.tmp" "$rep"
        fi
        count=$((count + 1))
    else
        echo "FAIL: $f" >&2
        rm -f "$rep"
        errors=$((errors + 1))
    fi
done

echo "Generated $count .rep files ($errors failures)"
