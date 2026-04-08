#!/bin/sh
# zeek-fmt.sh - format Zeek scripts via emitter + clz-format pipeline

# ---- Configurable paths ----
PYTHON=python3.11
EMITTER=/Users/vern/zeek/zeekscript/zeekscript/emit_ast.py
FORMATTER=/Users/vern/warehouse/zeek/zeekscript/src/clz-format

# ---- Usage ----
usage() {
    cat <<'EOF'
Usage: zeek-fmt.sh [OPTIONS] [FILE|DIR]

Format a Zeek script.  Reads from FILE or stdin if omitted.

Options:
  -i            Format FILE in-place (requires a file argument)
  -r            Recursively format all .zeek files under DIR in-place
  -H, --help    Show this help message and exit
EOF
    exit 0
}

# ---- Option parsing ----
INPLACE=false
RECURSIVE=false

while [ $# -gt 0 ]; do
    case "$1" in
        -i)        INPLACE=true ;;
        -r)        RECURSIVE=true ;;
        -H|--help) usage ;;
        --)        shift; break ;;
        -*)        echo "zeek-fmt.sh: unknown option: $1" >&2; exit 1 ;;
        *)         break ;;
    esac
    shift
done

# ---- Helpers ----
format_inplace() {
    FILE="$1"
    TMP="${FILE}.fmt.$$"
    if "$PYTHON" "$EMITTER" "$FILE" | "$FORMATTER" /dev/stdin > "$TMP"; then
        mv "$TMP" "$FILE"
    else
        echo "zeek-fmt.sh: failed: $FILE" >&2
        rm -f "$TMP"
        return 1
    fi
}

# ---- Main ----
INPUT="${1:--}"

if $RECURSIVE; then
    if [ "$INPUT" = "-" ] || [ ! -d "$INPUT" ]; then
        echo "zeek-fmt.sh: -r requires a directory argument" >&2
        exit 1
    fi
    ERRORS=0
    find "$INPUT" -name '*.zeek' -type f | while read -r f; do
        format_inplace "$f" || ERRORS=$((ERRORS + 1))
    done
    exit $ERRORS
elif $INPLACE; then
    if [ "$INPUT" = "-" ]; then
        echo "zeek-fmt.sh: -i requires a file argument" >&2
        exit 1
    fi
    format_inplace "$INPUT"
else
    "$PYTHON" "$EMITTER" "$INPUT" | "$FORMATTER" /dev/stdin
fi
