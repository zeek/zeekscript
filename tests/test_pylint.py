#! /usr/bin/env python
"""This runs pylint on the zeek-script wrapper and the zeekscript package. It
exits non-zero if pylint identifies any hard errors, and zero otherwise
(including when pylint isn't available). These aren't really unit tests, but we
use the unittest infrastructure for convenient test-skipping functionality.
"""
import os
import sys
import unittest

try:
    import pylint.lint
except ImportError:
    pass

TESTS = os.path.dirname(os.path.realpath(__file__))
ROOT = os.path.normpath(os.path.join(TESTS, '..'))
RCFILE = os.path.join(ROOT, '.pylintrc')

class TestPylint(unittest.TestCase):

    def _run(self, args):
        try:
            # The easiest way to get the return code out of a pylint run
            # seems to be allowing it to try to exit and catch its SystemExit.
            pylint.lint.Run(args)
        except SystemExit as err:
            return err.code == 0

    @unittest.skipIf('pylint.lint' not in sys.modules, 'Pylint not available')
    def test_zeekscript(self):
        self.assertTrue(self._run(['--rcfile=' + RCFILE, '-E', os.path.join(ROOT, 'zeekscript')]))

    @unittest.skipIf('pylint.lint' not in sys.modules, 'Pylint not available')
    def test_zeek_script(self):
        self.assertTrue(self._run(['--rcfile=' + RCFILE, '-E', os.path.join(ROOT, 'zeek-script')]))

    @unittest.skipIf('pylint.lint' not in sys.modules, 'Pylint not available')
    def test_zeek_format(self):
        self.assertTrue(self._run(['--rcfile=' + RCFILE, '-E', os.path.join(ROOT, 'zeek-format')]))


def test():
    """Entry point for testing this module.

    Returns True if successful, False otherwise.
    """
    res = unittest.main(sys.modules[__name__], verbosity=0, exit=False)
    # This is how unittest.main() implements the exit code itself:
    return res.result.wasSuccessful()

if __name__ == '__main__':
    sys.exit(not test())
