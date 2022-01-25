#! /usr/bin/env python
import io
import os
import sys
import unittest

TESTS = os.path.dirname(os.path.realpath(__file__))
ROOT = os.path.normpath(os.path.join(TESTS, '..'))
DATA = os.path.normpath(os.path.join(TESTS, 'data'))

# Add the tree's root folder to the module searchpath so we find zeekscript via
# it. This allows tests to run without package installation. (We do need a
# package build though, so the .so bindings library gets created.)
sys.path.append(ROOT)

import zeekscript

class TestFormatting(unittest.TestCase):

    def test_file_formatting(self):
        script = zeekscript.Script(os.path.join(DATA, 'test1.zeek'))
        script.parse()

        buf = io.BytesIO()
        script.format(buf)

        with open(os.path.join(DATA, 'test1.zeek.out'), 'rb') as hdl:
            result_wanted = hdl.read()
        result_is = buf.getvalue()

        self.assertEqual(result_wanted, result_is)

    def test_parse_error(self):
        script = zeekscript.Script(os.path.join(DATA, 'test2.zeek'))
        with self.assertRaises(zeekscript.ParserError) as cmgr:
            script.parse()
        self.assertEqual(str(cmgr.exception), 'cannot parse line 2, col 4: ")"')

def test():
    """Entry point for testing this module.

    Returns True if successful, False otherwise.
    """
    res = unittest.main(sys.modules[__name__], verbosity=0, exit=False)
    # This is how unittest.main() implements the exit code itself:
    return res.result.wasSuccessful()

if __name__ == '__main__':
    sys.exit(not test())
