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
  -v            Verbose: trace each file being processed
  -H, --help    Show this help message and exit
EOF
    exit 0
}

# ---- Option parsing ----
INPLACE=false
RECURSIVE=false
VERBOSE=false

while [ $# -gt 0 ]; do
    case "$1" in
        -i)        INPLACE=true ;;
        -r)        RECURSIVE=true ;;
        -v)        VERBOSE=true ;;
        -H|--help) usage ;;
        --)        shift; break ;;
        -*)        echo "zeek-fmt.sh: unknown option: $1" >&2; exit 255 ;;
        *)         break ;;
    esac
    shift
done

# ---- Helpers ----

# Run the emitter; on failure, report the file and return 1.
emit_rep() {
    if ! "$PYTHON" "$EMITTER" "$1" > "$2" 2>"$3"; then
        sed "s|^|$1: |" "$3" >&2
        return 1
    fi
}

format_inplace() {
    FILE="$1"
    $VERBOSE && echo "$FILE" >&2
    REP="${FILE}.rep.$$"
    TMP="${FILE}.fmt.$$"
    ERR="${FILE}.err.$$"
    if ! emit_rep "$FILE" "$REP" "$ERR"; then
        rm -f "$REP" "$TMP" "$ERR"
        return 255
    fi
    if "$FORMATTER" "$REP" > "$TMP" 2>"$ERR"; then
        mv "$TMP" "$FILE"
    else
        sed "s|^|$FILE: |" "$ERR" >&2
        rm -f "$TMP"
        rm -f "$REP" "$ERR"
        return 255
    fi
    rm -f "$REP" "$ERR"
}

# ---- Main ----
INPUT="${1:--}"

if $RECURSIVE; then
    if [ "$INPUT" = "-" ] || [ ! -d "$INPUT" ]; then
        echo "zeek-fmt.sh: -r requires a directory argument" >&2
        exit 255
    fi
    ERRORS=0
    while IFS= read -r f; do
        format_inplace "$f" || ERRORS=$((ERRORS + 1))
    done <<EOF
$(find "$INPUT" -name '*.zeek' -type f)
EOF
    [ $ERRORS -gt 0 ] && exit 255
    exit 0
elif $INPLACE; then
    if [ "$INPUT" = "-" ]; then
        echo "zeek-fmt.sh: -i requires a file argument" >&2
        exit 255
    fi
    format_inplace "$INPUT"
else
    REP=$(mktemp)
    ERR=$(mktemp)
    if ! emit_rep "$INPUT" "$REP" "$ERR"; then
        rm -f "$REP" "$ERR"
        exit 255
    fi
    if ! "$FORMATTER" "$REP" 2>"$ERR"; then
        sed "s|^|$INPUT: |" "$ERR" >&2
        rm -f "$REP" "$ERR"
        exit 255
    fi
    rm -f "$REP" "$ERR"
fi
