"""This module provides reusable command line parsers and tooling."""

import argparse
import io
import os
import sys
import traceback

from .error import Error, ParserError
from .output import print_error
from .script import Script

FILE_HELP = (
    'Use "-" to specify stdin as a filename. Omitting '
    "filenames entirely implies reading from stdin."
)


def cmd_format(args):
    """This function implements Zeek script formatting for the command line.

    It determines input and output streams, parses each input into a Script
    object, applies formatting, and writes out the result.

    Returns 0 in case of success, 1 in case of any errors -- this includes
    formatter-internal errors as well as any problems encountered during
    parsing. Encountered problems are written to stderr.
    """
    if args.recursive and not args.inplace:
        print_error("error: recursive file processing requires --inplace")
        return 1

    if not args.scripts:
        args.scripts = ["-"]

    scripts = []  # Final list of Zeek scripts to format.

    for fname in args.scripts:
        if fname == "-":
            if args.inplace:
                print_error(
                    "warning: cannot use --inplace when reading from stdin, skipping it"
                )
            else:
                scripts.append(fname)

        elif os.path.isdir(fname):
            if args.recursive:  # implies --inplace
                for dirpath, _, filenames in os.walk(fname):
                    names = [n for n in filenames if n.endswith(".zeek")]
                    names = [os.path.join(dirpath, n) for n in names]
                    scripts.extend(names)
            else:
                print_error(
                    f'warning: "{fname}" is a directory but --recursive not set, skipping it'
                )

        elif os.path.isfile(fname):
            scripts.append(fname)

        else:
            print_error(f'warning: skipping "{fname}"; not a supported file type')

    def do_write(source):
        with open(ofname, "wb") if ofname else sys.stdout.buffer as ostream:
            ostream.write(source)

    if len(scripts) > 1 and not args.inplace:
        print_error("error: processing multiple files requires --inplace")
        return 1

    errs = 0

    for fname in scripts:
        script = Script(fname)
        ofname = fname if args.inplace else None

        try:
            if not script.parse():
                errs += 1
                _, _, msg = script.get_error()
                if len(scripts) > 1:
                    print_error(f"{fname}: {msg}")
                else:
                    print_error(msg)
        except Error as err:
            print_error("parsing error: " + str(err))
            do_write(script.source)
            return 1
        except Exception as err:
            print_error("internal error: " + str(err))
            traceback.print_exc(file=sys.stderr)
            do_write(script.source)
            return 1

        buf = io.BytesIO()

        try:
            script.format(buf, not args.no_linebreaks)
        except Exception as err:
            print_error("internal error: " + str(err))
            traceback.print_exc(file=sys.stderr)
            do_write(script.source)
            return 1

        # Write out the complete, reformatted source.
        do_write(buf.getvalue())

    if args.inplace:
        print(
            f"{len(scripts)} file{'' if len(scripts) == 1 else 's'} processed, "
            f"{errs} error{'' if errs == 1 else 's'}"
        )

    return int(errs > 0)


def cmd_parse(args):
    """This function implements Zeek-script parsing for the commandline.

    It takes a single input file provided via the command line, parses it, and
    prints the parse tree to stdout according to the provided flags.

    Returns 0 when successful, 1 when a hard parse error came up that prevented
    building a parse tree, and 2 when the resulting parse tree has erroneous
    nodes.
    """
    script = Script(args.script or "-")

    try:
        script.parse()
    except ParserError as err:
        if not args.quiet:
            print_error("parsing error: " + str(err))
        return 1
    except Error as err:
        if not args.quiet:
            print_error("error: " + str(err))
        return 1

    if not args.quiet:
        script.write_tree(include_cst=args.concrete)

    if script.has_error():
        if not args.quiet:
            _, _, msg = script.get_error()
            print_error(f"parse tree has problems: {msg}")
        return 2

    return 0


def add_version_arg(parser):
    parser.add_argument(
        "--version", "-v", action="store_true", help="show version and exit"
    )


def add_format_cmd(parser):
    """This adds a Zeek script formatting CLI interface to the given argparse
    parser. It registers the cmd_format() callback as the parser's run_cmd
    default."""
    parser.set_defaults(run_cmd=cmd_format)
    parser.add_argument(
        "--inplace",
        "-i",
        action="store_true",
        help="change provided files instead of writing to stdout",
    )
    parser.add_argument(
        "--recursive",
        "-r",
        action="store_true",
        help="process *.zeek files recursively when provided directories "
        "instead of files. Requires --inplace.",
    )
    parser.add_argument("--no-linebreaks", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument(
        "scripts",
        metavar="FILES",
        nargs="*",
        help="Zeek script(s) to process. " + FILE_HELP,
    )


def add_parse_cmd(parser):
    """This adds a Zeek script parser CLI interface to the given argparse parser. It
    registers the cmd_parse() callback as the parser's run_cmd default."""
    parser.set_defaults(run_cmd=cmd_parse)
    parser.add_argument(
        "--concrete",
        "-c",
        action="store_true",
        help="report concrete syntax tree (CST) instead of AST",
    )
    parser.add_argument(
        "script", metavar="FILE", nargs="?", help="Zeek script to parse. " + FILE_HELP
    )
    parser.add_argument(
        "--quiet",
        "-q",
        action="store_true",
        help="suppress output and just return success or failure",
    )
