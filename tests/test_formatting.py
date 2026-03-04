#!/usr/bin/env python
"""Formatting-related tests."""

import io
import os
import pathlib
import sys
import textwrap
import unittest

# Sets up sys.path and provides helpers
import testutils as tu

# This imports the tree-local zeekscript
from testutils import zeekscript


class TestFormatting(unittest.TestCase):
    def _format(self, content):
        script = zeekscript.Script(io.BytesIO(content))

        self.assertTrue(script.parse())
        self.assertFalse(script.has_error())

        buf = io.BytesIO()
        script.format(buf)

        return buf.getvalue()

    def test_interval(self):
        self.assertEqual(self._format(b"1 sec;").rstrip(), b"1 sec;")
        self.assertEqual(self._format(b"1min;").rstrip(), b"1 min;")
        self.assertEqual(self._format(b"3.5  hrs;").rstrip(), b"3.5 hrs;")

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
            self._format(code.encode("UTF-8")).decode().splitlines(),
            expected.splitlines(),
        )

    def test_init_lists(self):
        # Square brackets should have no spaces after [ and before ]
        self.assertEqual(self._format(b"[ ];").rstrip(), b"[];")
        self.assertEqual(self._format(b"[ 1 ];").rstrip(), b"[1];")
        self.assertEqual(self._format(b"[ 1 , 2 ];").rstrip(), b"[1, 2];")

        # Trailing comma
        self.assertEqual(self._format(b"[ 1 , ];").rstrip(), b"[1, ];")
        self.assertEqual(self._format(b"[ 1 , 2 , ];").rstrip(), b"[1, 2, ];")

        self.assertEqual(self._format(b"local x = {};").rstrip(), b"local x = set();")
        self.assertEqual(
            self._format(b'global x = { [ 1 ] = "one" };').rstrip(),
            b'global x = table([1] = "one");',
        )

        # Test complex nodes
        code = textwrap.dedent("""\
            global x = [1, # Comment one
            2 # Comment two
            ];
            """)

        expected = textwrap.dedent("""\
            global x = [
            \t1, # Comment one
            \t2 # Comment two
            ];
            """)

        # We split out lines here to work around different line endings on Windows.
        self.assertEqual(
            self._format(code.encode("UTF-8")).decode().splitlines(),
            expected.splitlines(),
        )

    def test_no_split_on_field_access(self):
        """Don't split on $ or ?$ operators - keep expr$field together."""
        # The $ operator should not be a break point
        code = b"if ( ssl_cache_intermediate_ca && result$result_string == \"ok\" && result?$chain_certs && |result$chain_certs| > 2 ) print \"x\";"
        result = self._format(code).decode()
        # Verify $result_string stays together
        self.assertIn("result$result_string", result)
        # Verify ?$chain_certs stays together (not split as "result\n?$chain_certs")
        self.assertIn("result?$chain_certs", result)
        self.assertNotIn("result\n", result.replace("result$", "").replace("result?", ""))

    def test_assignment_line_breaking(self):
        """Assignment breaks should not create unnecessary splits."""
        # When RHS needs breaking anyway, don't also break at =
        code = b"age_d = interval_to_double(network_time() - cert$not_valid_before) / 86400.0;"
        result = self._format(code).decode()
        # Should keep "age_d = interval_to_double(network_time() -" on one line
        # Not break as "age_d =\n    interval_to_double..."
        self.assertIn("age_d = interval_to_double(network_time() -", result)

    def test_assignment_break_when_rhs_fits(self):
        """Assignment can break at = when RHS fits on continuation line."""
        # += should get proper indentation, not MISINDENTATION
        code = b"another_very_long_access_to_some_member += yetanotherveryveryveryverylongthing[0];"
        result = self._format(code).decode()
        # Should not have MISINDENTATION marker
        self.assertNotIn("MISINDENTATION", result)

    def test_event_handler_parameter_wrapping(self):
        """Event handler parameters should wrap at commas when line is too long."""
        code = b'event ssl_encrypted_data(c: connection, is_orig: bool, record_version: count, content_type: count, length: count) &group="doh-generic" { }'
        result = self._format(code).decode()
        lines = result.splitlines()
        # Parameters should wrap - not all on one line
        # First line should have some parameters, second line should have more
        self.assertTrue(
            any("content_type:" in line for line in lines[1:]),
            "Parameters should wrap to multiple lines"
        )
        # The &group attribute should be on its own line or with closing params
        self.assertTrue(
            any("&group" in line for line in lines),
            "Attribute should be present"
        )

    def test_assignment_with_nested_function_alignment(self):
        """Assignment RHS with function call should align arguments correctly."""
        code = b"recently_validated_certs[chain_id] = X509::Result($result=result$result, $result_string=result$result_string);"
        result = self._format(code).decode()
        # Should not have MISINDENTATION
        self.assertNotIn("MISINDENTATION", result)
        # Both $result and $result_string should be present (not split on $)
        self.assertIn("$result=result$result", result)
        self.assertIn("$result_string=result$result_string", result)

    def test_comments_count_toward_line_length(self):
        """Regular comments should count toward line length."""
        # This line is ~127 chars with the comment - should trigger breaking
        code = b"age_d = interval_to_double(network_time() - cert$not_valid_before) / 86400.0; # This is a comment that makes the line very long"
        result = self._format(code).decode()
        # Should have been broken into multiple lines
        self.assertGreater(len(result.splitlines()), 1)

    def test_annotation_comments_excluded_from_line_length(self):
        """Comments starting with #@ should not count toward line length."""
        # Same line but with #@ annotation - should NOT trigger breaking
        code = b"age_d = interval_to_double(network_time() - cert$not_valid_before) / 86400.0; #@ annotation"
        result = self._format(code).decode()
        # Should stay on one line (the code part fits, annotation excluded)
        lines = [l for l in result.splitlines() if l.strip()]
        self.assertEqual(len(lines), 1)

    def test_skip_pointless_line_breaks(self):
        """Don't break if continuation would be as long as original."""
        # Breaking at ( would shift content but not reduce max line length
        code = b'print fmt("This is a really long string argument that cannot be broken up into smaller pieces");'
        result = self._format(code).decode()
        # Should stay on one line - breaking wouldn't help
        lines = [l for l in result.splitlines() if l.strip()]
        self.assertEqual(len(lines), 1)

    def test_keep_fmt_string_together(self):
        """Keep fmt("string") together, break at comma after string."""
        code = b'local x = [$msg=fmt("Host uses protocol version %s which is lower than the safe minimum %s", host_string, minimum_string)];'
        result = self._format(code).decode()
        # Should NOT break at fmt( - instead break at comma after string
        # Line 1: ...fmt("...string...",
        # Line 2: host_string, minimum_string)...
        self.assertIn('fmt("Host uses protocol', result)
        self.assertNotIn('fmt(\n', result)
        self.assertNotIn('fmt(\t', result)

    def test_record_fields_combine_when_fit(self):
        """Record fields should combine on one line when they fit under 80 chars."""
        # This tests that newlines aren't counted in line length (off-by-one fix).
        # With indentation, the alignment column changes and the last two fields
        # fit exactly at 80 chars - but only if newlines are excluded from length.
        code = b'''function f() {
\tif ( x )
\t\tInput::add_event([$source=cert_hygiene_sni_wl_source, $name=cert_hygiene_sni_wl_name, $fields=CertHygieneSNIWL, $mode=Input::REREAD, $want_record=F, $ev=cert_hygiene_sni_wl_add]);
}'''
        result = self._format(code).decode()
        # $want_record=F and $ev should be on the same line since they fit at 80 chars
        self.assertIn('$want_record=F, $ev=cert_hygiene_sni_wl_add', result)

    def test_enum_single_line_preserved(self):
        """Enum on single line in source should stay on single line if it fits."""
        code = b'type level: enum { NOT = 0, LOW = 1, MED = 2, HIGH = 3 };'
        result = self._format(code).decode()
        # Should stay on one line
        lines = [l for l in result.splitlines() if l.strip()]
        self.assertEqual(len(lines), 1)
        self.assertIn('{ NOT = 0, LOW = 1, MED = 2, HIGH = 3 }', result)


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
        _, error = self._format(
            tu.normalize(
                """type foo: record {
	a: count; ##< A field
	b count; ##< A broken field
	c: count; ##< Another field, better not skipped!
	d: count; ##< Ditto.
};
"""
            )
        )

        # TODO(bbannier): The way we currently format this is not idempotent,
        # so only check that we return the expected error. This is okay since
        # we do not format files with errors anyway.
        assert error == (
            "type foo: record {",
            0,
            'cannot parse line 0, col 10: "record {\n\ta: count; ##< A field\n\tb"',
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

    def test_file_formatting(self):
        given = tu.SAMPLE_UNFORMATTED.encode("utf-8")
        # Swap line endings for something not native to the platform:
        if zeekscript.Formatter.NL == b"\n":
            # Turn everything to \r\n, even if mixed
            given = given.replace(b"\r\n", b"\n")
            given = given.replace(b"\n", b"\r\n")
        else:
            given = given.replace(b"\r\n", b"\n")

        buf = io.BytesIO(given)
        script = zeekscript.Script(buf)
        script.parse()

        buf = io.BytesIO()
        script.format(buf)

        given = buf.getvalue()

        expected = tu.SAMPLE_FORMATTED.encode("utf-8")
        self.assertEqual(expected, given)


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
