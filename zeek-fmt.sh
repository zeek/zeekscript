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
  -H, --help    Show this help message and exit
EOF
    exit 0
}

# ---- Option parsing ----
while [ $# -gt 0 ]; do
    case "$1" in
        -H|--help) usage ;;
        --)        shift; break ;;
        -*)        echo "zeek-fmt.sh: unknown option: $1" >&2; exit 1 ;;
        *)         break ;;
    esac
    shift
done

# ---- Main ----
INPUT="${1:--}"

"$PYTHON" "$EMITTER" "$INPUT" | "$FORMATTER" /dev/stdin
