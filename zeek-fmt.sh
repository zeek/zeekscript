#!/bin/sh
# zeek-fmt.sh - format Zeek scripts via emitter + clz-format pipeline

# ---- Configurable paths ----
PYTHON=python3.11
EMITTER=/Users/vern/zeek/zeekscript/zeekscript/emit_ast.py
FORMATTER=/Users/vern/warehouse/zeek/zeekscript/src/clz-format

# ---- Usage ----
usage() {
    cat <<'EOF'
Usage: zeek-fmt.sh [OPTIONS] [FILE]

Format a Zeek script.  Reads from FILE or stdin if omitted.

Options:
  -i            Format FILE in-place (requires a file argument)
  -H, --help    Show this help message and exit
EOF
    exit 0
}

# ---- Option parsing ----
INPLACE=false

while [ $# -gt 0 ]; do
    case "$1" in
        -i)        INPLACE=true ;;
        -H|--help) usage ;;
        --)        shift; break ;;
        -*)        echo "zeek-fmt.sh: unknown option: $1" >&2; exit 1 ;;
        *)         break ;;
    esac
    shift
done

# ---- Main ----
INPUT="${1:--}"

if $INPLACE; then
    if [ "$INPUT" = "-" ]; then
        echo "zeek-fmt.sh: -i requires a file argument" >&2
        exit 1
    fi
    TMP="${INPUT}.fmt.$$"
    if "$PYTHON" "$EMITTER" "$INPUT" | "$FORMATTER" /dev/stdin > "$TMP"; then
        mv "$TMP" "$INPUT"
    else
        rm -f "$TMP"
        exit 1
    fi
else
    "$PYTHON" "$EMITTER" "$INPUT" | "$FORMATTER" /dev/stdin
fi
