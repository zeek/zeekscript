#!/usr/bin/env python
"""
This is a tool for processing Zeek scripts. It currently supports formatting
scripts according to codified rules (no optionas at all atm), and showing a
parse tree for the script.
"""

# https://pypi.org/project/argcomplete/#global-completion
# PYTHON_ARGCOMPLETE_OK
import argparse
import sys

import argcomplete

import zeekscript

# ---- Helper functions --------------------------------------------------------


def create_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="A Zeek script analyzer")

    zeekscript.add_version_arg(parser)

    command_parser = parser.add_subparsers(
        title="commands",
        dest="command",
        help="See `%(prog)s <command> -h` for per-command usage info.",
    )

    sub_parser = command_parser.add_parser("format", help="Format/indent Zeek scripts")
    zeekscript.add_format_cmd(sub_parser)

    sub_parser = command_parser.add_parser(
        "parse",
        help="Show Zeek script parse tree with parser metadata.",
        epilog="Exits with 0 on success, 1 on a hard parse error that "
        "does not yield a parse tree, and 2 when parsing succeeds but "
        "the parse tree contains erroneous nodes.",
    )
    zeekscript.add_parse_cmd(sub_parser)

    argcomplete.autocomplete(parser)

    return parser


def zeek_script() -> int:
    parser = create_parser()
    args = parser.parse_args()

    if args.version:
        print(zeekscript.__version__)
        return 0

    if not args.command:
        zeekscript.print_error(
            "error: please provide a command to execute. See --help."
        )
        return 1

    try:
        result: int = args.run_cmd(args)
        return result
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    sys.exit(zeek_script())
