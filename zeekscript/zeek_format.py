#!/usr/bin/env python
"""
This is a Zeek script formatter. It intentionally accepts no formatting
arguments. It writes any errors during processing to stderr. When errors arise,
it writes out unchanged input.
"""

# https://pypi.org/project/argcomplete/#global-completion
# PYTHON_ARGCOMPLETE_OK

import argparse
import sys

import argcomplete

import zeekscript

# ---- Helper functions --------------------------------------------------------


def create_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="A Zeek script formatter")

    zeekscript.add_version_arg(parser)
    zeekscript.add_format_cmd(parser)

    argcomplete.autocomplete(parser)

    return parser


def zeek_format() -> int:
    parser = create_parser()
    args = parser.parse_args()

    if args.version:
        print(zeekscript.__version__)
        return 0

    try:
        result: int = args.run_cmd(args)
        return result
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    sys.exit(zeek_format())
