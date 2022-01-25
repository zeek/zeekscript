#! /usr/bin/env python
"""
This runs pylint on the zeek-script wrapper and the zeekscript package.  It
exits non-zero if pylint identifies any hard errors, and zero otherwise
(including when pylint isn't available).
"""
import os
import sys

try:
    import pylint.lint
except ImportError:
    print('Skipping pylint tests, need pylint installation.', file=sys.stderr)
    sys.exit(0)

TESTS = os.path.dirname(os.path.realpath(__file__))
ROOT = os.path.normpath(os.path.join(TESTS, '..'))
RCFILE = os.path.join(ROOT, '.pylintrc')

def run(args):
    try:
        # The easiest way to get the return code out of a pylint run
        # seems to allow it to try to exit and catch its SystemExit.
        pylint.lint.Run(args)
    except SystemExit as err:
        print('pylint {}: {}'.format(
            ' '.join(args), 'OK' if err.code == 0 else 'FAILURE'))
        return err.code == 0

def test():
    """Entry point for testing this module.

    Returns True if successful, False otherwise.
    """
    return all((
        run(['--rcfile=' + RCFILE, '-E', os.path.join(ROOT, 'zeekscript')]),
        run(['--rcfile=' + RCFILE, '-E', os.path.join(ROOT, 'zeek-script')]),
    ))

if __name__ == '__main__':
    sys.exit(not test())
