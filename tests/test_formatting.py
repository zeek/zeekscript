#! /usr/bin/env python
import io
import os
import pathlib
import sys
import unittest

TESTS = os.path.dirname(os.path.realpath(__file__))
ROOT = os.path.normpath(os.path.join(TESTS, '..'))
DATA = os.path.normpath(os.path.join(TESTS, 'data'))

# Prepend the tree's root folder to the module searchpath so we find zeekscript
# via it. This allows tests to run without package installation. (We do need a
# package build though, so the .so bindings library gets created.)
sys.path.insert(0, ROOT)

import zeekscript

class TestFormatting(unittest.TestCase):

    def _get_formatted_and_baseline(self, filename):
        script = zeekscript.Script(os.path.join(DATA, filename))
        script.parse()

        buf = io.BytesIO()
        script.format(buf)

        with open(os.path.join(DATA, filename + '.out'), 'rb') as hdl:
            result_wanted = hdl.read()
        result_is = buf.getvalue()
        return result_wanted, result_is

    def test_file_formatting(self):
        result_wanted, result_is = self._get_formatted_and_baseline('test1.zeek')
        self.assertEqual(result_wanted, result_is)

    def test_parse_error(self):
        script = zeekscript.Script(os.path.join(DATA, 'test2.zeek'))
        with self.assertRaises(zeekscript.ParserError) as cmgr:
            script.parse()
        self.assertEqual(str(cmgr.exception), 'cannot parse line 2, col 4: ")"')

    def test_dosfile_formatting(self):
        result_wanted, result_is = self._get_formatted_and_baseline('test3.zeek')
        self.assertEqual(result_wanted, result_is)


class TestScriptConstruction(unittest.TestCase):
    DATA = 'event zeek_init() { }'
    TMPFILE = 'tmp.zeek'

    def test_file(self):
        # tempfile.NamedTemporaryFile doesn't seem to support reading while
        # still existing on some platforms, so going manual here:
        try:
            with open(self.TMPFILE, 'w') as hdl:
                hdl.write(self.DATA)
            script = zeekscript.Script(self.TMPFILE)
            script.parse()
        finally:
            os.unlink(self.TMPFILE)

    def test_path(self):
        try:
            with open(self.TMPFILE, 'w') as hdl:
                hdl.write(self.DATA)
            script = zeekscript.Script(pathlib.Path(hdl.name))
            script.parse()
        finally:
            os.unlink(self.TMPFILE)

    def test_stdin(self):
        try:
            oldstdin = sys.stdin
            sys.stdin = io.StringIO(self.DATA)
            script = zeekscript.Script('-')
            script.parse()
        finally:
            sys.stdin = oldstdin

    def test_text_fileobj(self):
        obj = io.StringIO(self.DATA)
        script = zeekscript.Script(obj)
        script.parse()

    def test_bytes_fileobj(self):
        obj = io.BytesIO(self.DATA.encode('UTF-8'))
        script = zeekscript.Script(obj)
        script.parse()


def test():
    """Entry point for testing this module.

    Returns True if successful, False otherwise.
    """
    res = unittest.main(sys.modules[__name__], verbosity=0, exit=False)
    # This is how unittest.main() implements the exit code itself:
    return res.result.wasSuccessful()

if __name__ == '__main__':
    sys.exit(not test())