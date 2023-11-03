#!/usr/bin/env python
"""Formatting-related tests."""
import io
import os
import pathlib
import sys
import unittest
import textwrap

# Sets up sys.path and provides helpers
import testutils as tu

# This imports the tree-local zeekscript
from testutils import zeekscript


class TestFormatting(unittest.TestCase):
    def _get_input_and_baseline(self, filename):
        with open(os.path.join(tu.DATA, filename), "rb") as hdl:
            input_ = hdl.read()

        with open(os.path.join(tu.DATA, filename + ".out"), "rb") as hdl:
            output = hdl.read()

        return input_, output

    def _format(self, content):
        script = zeekscript.Script(io.BytesIO(content))

        self.assertTrue(script.parse())
        self.assertFalse(script.has_error())

        buf = io.BytesIO()
        script.format(buf)

        return buf.getvalue()

    def test_file_formatting(self):
        input_, baseline = self._get_input_and_baseline("test1.zeek")

        # Format the input data and compare to baseline:
        result1 = self._format(input_)
        self.assertEqual(baseline, result1)

        # Format the result again. There should be no change.
        result2 = self._format(result1)
        self.assertEqual(baseline, result2)

    def test_index_slice(self):
        # Use compact layout if neither side is a constant or id.
        self.assertEqual(self._format(b"xs[  0   :1  ];").rstrip(), b"xs[0:1];")

        # # If either side is unspecified do not use use compact layout, but do
        # not add additional whitespace if both sides are omitted.
        self.assertEqual(self._format(b"xs[  0   :  ];").rstrip(), b"xs[0:];")
        self.assertEqual(self._format(b"xs[  : 1 ];").rstrip(), b"xs[:1];")
        self.assertEqual(self._format(b"data[1-1:];").rstrip(), b"data[1 - 1 :];")
        self.assertEqual(self._format(b"data[:1-1];").rstrip(), b"data[: 1 - 1];")
        self.assertEqual(self._format(b"data[   :   ];").rstrip(), b"data[:];")

        # Else surround `:` with spaces.
        self.assertEqual(self._format(b"data[1-1:1];").rstrip(), b"data[1 - 1 : 1];")
        self.assertEqual(self._format(b"data[1 :1-1];").rstrip(), b"data[1 : 1 - 1];")
        self.assertEqual(self._format(b"data[f(): ];").rstrip(), b"data[f() :];")

    def test_format_comment_separator(self):
        """Validates that we preserve the separator before a comment. This is a
        regression test for #62."""

        self.assertEqual(
            self._format(b"global x = 42;   # Inline.").rstrip(),
            b"global x = 42; # Inline.",
        )

        code = textwrap.dedent(
            """\
               event zeek_init() {}
               # Comment on next line.
            """
        )

        expected = textwrap.dedent(
            """\
            event zeek_init()
            \t{ }
            # Comment on next line.
            """
        )

        # We split out lines here to work around different line endings on Windows.
        self.assertEqual(
            self._format(code.encode()).decode().splitlines(), expected.splitlines()
        )


class TestFormattingErrors(unittest.TestCase):
    def _format(self, content):
        script = zeekscript.Script(io.BytesIO(content))

        # Everything passed in has a syntax error, so parsing should always
        # fail, and has_error() should be True.
        self.assertFalse(script.parse())
        self.assertTrue(script.has_error())

        buf = io.BytesIO()
        script.format(buf)

        return buf.getvalue(), script.get_error()

    # pylint: disable-next=invalid-name
    def assertFormatting(self, input_, baseline, error_baseline):
        # Verify formatting and reported error
        result1, error1 = self._format(tu.normalize(input_))
        self.assertEqual(tu.normalize(baseline), result1)
        self.assertEqual(error_baseline, error1)

        # Format again. There should be no change to the formatting.
        result2, _ = self._format(result1)
        self.assertEqual(tu.normalize(baseline), result2)

    def test_start_error(self):
        self.assertFormatting(
            "xxx  function foo() { }",
            """xxx function foo()
	{ }
""",
            ("xxx  function foo() { }", 0, 'cannot parse line 0, col 0: "xxx"'),
        )

    def test_mid_error(self):
        self.assertFormatting(
            """module Foo;

function foo) { print  "hi" ; }
""",
            """module Foo;

function foo) {
print "hi";
}
""",
            (
                'function foo) { print  "hi" ; }',
                2,
                'cannot parse line 2, col 0: "function foo)"',
            ),
        )

    def test_mid_record_error(self):
        self.assertFormatting(
            """type foo: record {
	a: count; ##< A field
	b count; ##< A broken field
	c: count; ##< Another field, better not skipped!
	d: count; ##< Ditto.
};
""",
            """type foo: record {
	a: count; ##< A field
	b count; ##< A broken field
	c: count; ##< Another field, better not skipped!
	d: count; ##< Ditto.
};
""",
            (
                "\tb count; ##< A broken field",
                2,
                'cannot parse line 2, col 1: "b count;"',
            ),
        )

    def test_single_char_mid_error(self):
        # In this example, the error node spans just a single character:
        self.assertFormatting(
            "event zeek_init() { foo)(); }",
            """event zeek_init()
	{
	foo ) ();
	}
""",
            ("event zeek_init() { foo)(); }", 0, 'cannot parse line 0, col 23: ")"'),
        )

    def test_tail_error_no_nl(self):
        self.assertFormatting(
            "function foo( )  { if (",
            "function foo()  { if (\n",
            (
                "function foo( )  { if (",
                0,
                'cannot parse line 0, col 0: "function foo( )  { if ("',
            ),
        )

    def test_tail_error_nl(self):
        self.assertFormatting(
            "function foo( ) { if (\n",
            "function foo() { if (\n",
            (
                "function foo( ) { if (",
                0,
                'cannot parse line 0, col 0: "function foo( ) { if ("',
            ),
        )


class TestNewlineFormatting(unittest.TestCase):
    # This test verifies correct processing when line endings in the input
    # differ from that normally used by the platform.

    def _get_formatted_and_baseline(self, filename):
        # Swap line endings for something not native to the platform:
        with open(os.path.join(tu.DATA, filename), "rb") as hdl:
            data = hdl.read()
            if zeekscript.Formatter.NL == b"\n":
                # Turn everything to \r\n, even if mixed
                data = data.replace(b"\r\n", b"\n")
                data = data.replace(b"\n", b"\r\n")
            else:
                data = data.replace(b"\r\n", b"\n")

        buf = io.BytesIO(data)

        script = zeekscript.Script(buf)
        script.parse()

        buf = io.BytesIO()
        script.format(buf)

        with open(os.path.join(tu.DATA, filename + ".out"), "rb") as hdl:
            result_wanted = hdl.read()

        result_is = buf.getvalue()
        return result_wanted, result_is

    def test_file_formatting(self):
        result_wanted, result_is = self._get_formatted_and_baseline("test1.zeek")
        self.assertEqual(result_wanted, result_is)


class TestScriptConstruction(unittest.TestCase):
    DATA = "event zeek_init() { }"
    TMPFILE = "tmp.zeek"

    def test_file(self):
        # tempfile.NamedTemporaryFile doesn't seem to support reading while
        # still existing on some platforms, so going manual here:
        try:
            with open(self.TMPFILE, "w", encoding="utf-8") as hdl:
                hdl.write(self.DATA)
            script = zeekscript.Script(self.TMPFILE)
            script.parse()
        finally:
            os.unlink(self.TMPFILE)

    def test_path(self):
        try:
            with open(self.TMPFILE, "w", encoding="utf-8") as hdl:
                hdl.write(self.DATA)
            script = zeekscript.Script(pathlib.Path(hdl.name))
            script.parse()
        finally:
            os.unlink(self.TMPFILE)

    def test_stdin(self):
        oldstdin = sys.stdin

        try:
            sys.stdin = io.StringIO(self.DATA)
            script = zeekscript.Script("-")
            script.parse()
        finally:
            sys.stdin = oldstdin

    def test_text_fileobj(self):
        obj = io.StringIO(self.DATA)
        script = zeekscript.Script(obj)
        script.parse()

    def test_bytes_fileobj(self):
        obj = io.BytesIO(self.DATA.encode("UTF-8"))
        script = zeekscript.Script(obj)
        script.parse()
