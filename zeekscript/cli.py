"""This module provides reusable command line parsers and tooling."""
import argparse
import io
import sys
import traceback

from .error import Error, ParserError
from .script import Script
from .output import print_error

FILE_HELP = ('Use "-" to specify stdin as a filename. Omitting '
             'filenames entirely implies reading from stdin.')

def cmd_format(args):
    """This function implements Zeek script formatting for the command line. It
    determines input and output streams, parses each input into a Script object,
    applies formattinge, and writes out the result."""

    if not args.scripts:
        args.scripts = ['-']

    def do_write(source):
        with open(ofname, 'w') if ofname else sys.stdout as ostream:
            ostream.write(source.decode('UTF-8'))

    for fname in args.scripts:
        inplace = args.inplace
        if fname == '-' and inplace:
            print_error('warning: ignoring --inplace when reading from stdin')
            inplace = False

        script = Script(fname)
        ofname = fname if inplace else None

        try:
            script.parse()
        except Error as err:
            print_error('parsing error: ' + str(err))
            do_write(script.source)
            return 1
        except Exception as err:
            print_error('internal error: ' + str(err))
            traceback.print_exc(file=sys.stderr)
            do_write(script.source)
            return 1

        buf = io.BytesIO()

        try:
            script.format(buf, not args.no_linebreaks)
        except Exception as err:
            print_error('internal error: ' + str(err))
            traceback.print_exc(file=sys.stderr)
            do_write(script.source)
            return 1

        # Write out the complete, reformatted source.
        do_write(buf.getvalue())

    return 0


def cmd_parse(args):
    """This function implements Zeek-script parsing for the commandline.

    It takes a single input file provided via the command line, parses it, and
    prints the parse tree to stdout according to the provided flags.

    Returns 0 when successful, 1 when a hard parse error came up that prevented
    building a parse tree, and 2 when the resulting parse tree has erroneous
    nodes.
    """
    script = Script(args.script or '-')

    try:
        script.parse()
    except ParserError as err:
        print_error('parsing error: ' + str(err))
        return 1
    except Error as err:
        print_error('error: ' + str(err))
        return 1

    script.write_tree(include_cst=args.concrete)

    if script.has_error():
        _, _, msg = script.get_error()
        print_error('parse tree has problems: %s' % msg)
        return 2

    return 0


def add_format_cmd(parser):
    """This adds a Zeek script formatting CLI interface to the given argparse
    parser. It registers the cmd_format() callback as the parser's run_cmd
    default."""
    parser.set_defaults(run_cmd=cmd_format)
    parser.add_argument(
        '--inplace', '-i', action='store_true',
        help='change provided files instead of writing to stdout')
    parser.add_argument(
        '--no-linebreaks', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument(
        'scripts', metavar='FILES', nargs='*',
        help='Zeek script(s) to process. ' + FILE_HELP)


def add_parse_cmd(parser):
    """This adds a Zeek script parser CLI interface to the given argparse parser. It
    registers the cmd_parse() callback as the parser's run_cmd default."""
    parser.set_defaults(run_cmd=cmd_parse)
    parser.add_argument(
        '--concrete', '-c', action='store_true',
        help='report concrete syntax tree (CST) instead of AST')
    parser.add_argument(
        'script', metavar='FILE', nargs='?',
        help='Zeek script to parse. ' + FILE_HELP)
