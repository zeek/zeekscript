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

    def test_event_type_parameter_alignment(self):
        """Event type parameters should align to after the opening paren."""
        code = b'global classification: event(o: string, label: string, conf: level, sources: set[string], caller: string);'
        result = self._format(code).decode()
        lines = result.splitlines()
        # Should wrap and align continuation to after 'event('
        self.assertGreater(len(lines), 1)
        # Find the column where 'o:' starts on line 1
        line1 = lines[0]
        o_col = line1.index('o:')
        # Continuation should align to same column
        line2 = lines[1]
        # Find first non-space char in line2
        first_char_col = len(line2) - len(line2.lstrip())
        self.assertEqual(first_char_col, o_col)

    def test_event_statement_argument_alignment(self):
        """Event statement arguments should align to after the opening paren."""
        code = b"function observation() { if ( foo ) event Confidence::observation_evt(o, label, conf, source, caller); }"
        result = self._format(code).decode()
        # Should not have MISINDENTATION marker
        self.assertNotIn("MISINDENTATION", result)
        # Verify arguments wrap with proper alignment
        lines = result.splitlines()
        # Find line with event call and continuation
        event_line = None
        cont_line = None
        for i, line in enumerate(lines):
            if "event Confidence::observation_evt" in line:
                event_line = line
                if i + 1 < len(lines):
                    cont_line = lines[i + 1]
                break
        self.assertIsNotNone(event_line)
        self.assertIsNotNone(cont_line)
        # Continuation should align to after '('
        paren_col = event_line.index("(") + 1
        first_char_col = len(cont_line) - len(cont_line.lstrip())
        self.assertEqual(first_char_col, paren_col)

    def test_in_and_not_in_operator_alignment(self):
        """The in and !in operators should align continuations like other binary operators."""
        # Test !in
        code1 = b'return aws_main_domain in subj && amzaws_wildcard !in subj && apiaws_wildcard !in subj;'
        result1 = self._format(code1).decode()
        self.assertNotIn("MISINDENTATION", result1)
        # Test in
        code2 = b'return some_very_long_variable_name in some_other_very_long_variable_name && another_thing in yet_another_thing;'
        result2 = self._format(code2).decode()
        self.assertNotIn("MISINDENTATION", result2)

    def test_end_of_line_comment_stays_on_line(self):
        """End-of-line comments should stay with the statement even on long lines."""
        # This tests that deeply nested statements with end-of-line comments
        # don't get the comment pushed to a new line
        code = b'''function process_conn(c: connection)
    {
    if ( foo )
        {
        if ( bar )
            {
            if ( bletch )
                report_processing("Observed dual stack endpoint"); #@ NOT-TESTED
            }
        }
    }'''
        result = self._format(code).decode()
        # Should not have MISINDENTATION marker
        self.assertNotIn("MISINDENTATION", result)
        # The comment should be on the same line as the statement
        self.assertIn('endpoint"); #@ NOT-TESTED', result)

    def test_boolean_op_preferred_over_arithmetic(self):
        """Line breaks should prefer && and || over arithmetic operators like -."""
        # This tests that when a line needs breaking, && is chosen over -
        code = b'''function get_connection_vpc_id(c: connection): string
    {
    if ( c?$tunnel && |c$tunnel| >= 2 &&
         c$tunnel[|c$tunnel| - 1]$tunnel_type == Tunnel::VXLAN )
        print "yep";
    }'''
        result = self._format(code).decode()
        # The break should be after && not inside the array index expression
        # Check that "- 1]" stays together (not split across lines)
        self.assertIn("- 1]", result)
        # Check that && is at end of a line (not start of next line)
        lines = result.splitlines()
        found_and_at_end = any(line.rstrip().endswith("&&") for line in lines)
        self.assertTrue(found_and_at_end, "Expected && at end of line")

    def test_attribute_keeps_equals_together(self):
        """Attributes like &default=expr should not break at the = sign."""
        code = b'global some_table: table[string] of count &default=some_long_default_expression_here &redef;'
        result = self._format(code).decode()
        # The &default=... should stay together
        self.assertIn("&default=some_long", result)
        # Should not have = at end of line or start of continuation
        lines = result.splitlines()
        for line in lines:
            stripped = line.rstrip()
            # = should not be at end of line (breaking after =)
            self.assertFalse(stripped.endswith("="), f"Line should not end with =: {line}")

    def test_compound_conditional_breaks_at_boolean_op(self):
        """Long compound conditionals should break at && or || operators."""
        code = b'if ( first_condition && second_condition && third_condition && some_object$long_field > another_object$other_field ) do_something();'
        result = self._format(code).decode()
        lines = result.splitlines()
        # Should have a line ending with &&
        found_and_at_end = any(line.rstrip().endswith("&&") for line in lines)
        self.assertTrue(found_and_at_end, "Expected && at end of line when breaking")
        # The > comparison should stay together, not be used as break point
        self.assertIn("long_field >", result)

    def test_nested_function_call_alignment(self):
        """Nested function calls should align correctly when outer call wraps."""
        code = b'local filter = Log::Filter($name="conn-app", $path="conn_app", $include=set("id.orig_h", "id.orig_p", "id.resp_h", "id.resp_p", "app"), $policy=conn_apps_only);'
        result = self._format(code).decode()
        # Should not have MISINDENTATION marker
        self.assertNotIn("MISINDENTATION", result)
        lines = result.splitlines()
        # Find lines with Log::Filter( and set( continuations
        outer_line = None
        set_line = None
        set_cont_line = None
        for i, line in enumerate(lines):
            if "Log::Filter(" in line:
                outer_line = line
            if "$include=set(" in line:
                set_line = line
                if i + 1 < len(lines):
                    set_cont_line = lines[i + 1]
        self.assertIsNotNone(outer_line)
        self.assertIsNotNone(set_line)
        self.assertIsNotNone(set_cont_line)
        # Verify nested set() continuation aligns to after set(
        set_paren_col = set_line.index("set(") + 4
        set_cont_col = len(set_cont_line) - len(set_cont_line.lstrip())
        self.assertEqual(set_cont_col, set_paren_col)

    def test_constructor_formatting_ignores_input_newlines(self):
        """Constructor calls should format the same regardless of input whitespace."""
        # Single-line input
        code1 = b'local x = set("a", "b", "c", "d", "e");'
        # Multi-line input (same semantically)
        code2 = b'local x = set("a",\n"b",\n"c",\n"d",\n"e");'

        result1 = self._format(code1)
        result2 = self._format(code2)
        # Both should produce identical output
        self.assertEqual(result1, result2)

    def test_large_constructor_uses_one_per_line(self):
        """Constructors >80 chars should use one-per-line format."""
        # Long set (>80 chars) - should be one-per-line regardless of input
        code1 = b'local x = set(1.0.0.2/31, 1.1.1.2/31, 3.7.176.123/32, 5.1.66.255/32, 5.2.75.75/32, 5.45.107.88/32, 8.8.4.4/32);'
        code2 = b'local x = set(1.0.0.2/31,\n1.1.1.2/31,\n3.7.176.123/32,\n5.1.66.255/32,\n5.2.75.75/32,\n5.45.107.88/32,\n8.8.4.4/32);'

        result1 = self._format(code1).decode()
        result2 = self._format(code2).decode()

        # Both should produce identical output
        self.assertEqual(result1, result2)
        # Should have multiple lines (one-per-line format)
        self.assertGreater(len(result1.splitlines()), 1)
        # Each element should be on its own line
        self.assertIn("1.0.0.2/31,\n", result1)

    def test_multiline_element_aligns_next_element(self):
        """When an element spans multiple lines, next element starts on new aligned line."""
        code = b'local filter = Log::Filter($name="conn-app", $path="conn_app", $include=set("id.orig_h", "id.orig_p", "id.resp_h", "id.resp_p", "app"), $policy=conn_apps_only);'
        result = self._format(code).decode()
        lines = result.splitlines()
        # Find lines with $include and $policy
        include_line = None
        policy_line = None
        for i, line in enumerate(lines):
            if "$include=" in line:
                include_line = i
            if "$policy=" in line:
                policy_line = i
        self.assertIsNotNone(include_line)
        self.assertIsNotNone(policy_line)
        # $policy should be on a different line than where $include starts
        self.assertGreater(policy_line, include_line)
        # $policy should be aligned with $include (same indentation)
        include_indent = len(lines[include_line]) - len(lines[include_line].lstrip())
        policy_indent = len(lines[policy_line]) - len(lines[policy_line].lstrip())
        self.assertEqual(policy_indent, include_indent)

    def test_multiline_element_aligns_next_element_indented(self):
        """Same as above but inside an indented block - indentation shouldn't change behavior."""
        code = b'event zeek_init() { local filter = Log::Filter($name="conn-app", $path="conn_app", $include=set("id.orig_h", "id.orig_p", "id.resp_h", "id.resp_p", "app"), $policy=conn_apps_only); }'
        result = self._format(code).decode()
        lines = result.splitlines()
        # Find lines with $include and $policy
        include_line = None
        policy_line = None
        for i, line in enumerate(lines):
            if "$include=" in line:
                include_line = i
            if "$policy=" in line:
                policy_line = i
        self.assertIsNotNone(include_line)
        self.assertIsNotNone(policy_line)
        # $policy should be on a different line than where $include starts
        self.assertGreater(policy_line, include_line)
        # $policy should be aligned with $include (same indentation)
        include_indent = len(lines[include_line]) - len(lines[include_line].lstrip())
        policy_indent = len(lines[policy_line]) - len(lines[policy_line].lstrip())
        self.assertEqual(policy_indent, include_indent)

    def test_standalone_comment_after_function(self):
        """Comments on their own line after a function should not be indented."""
        code = b'''#@ BEGIN
function foo()
    {
    bar();
    }
#@ END
'''
        result = self._format(code).decode()
        lines = result.splitlines()
        # Find the #@ END comment
        end_line = None
        for line in lines:
            if "#@ END" in line:
                end_line = line
                break
        self.assertIsNotNone(end_line)
        # Should be at column 0 (no indentation)
        self.assertFalse(end_line.startswith("\t"))
        self.assertFalse(end_line.startswith(" "))

    def test_record_args_alignment_ignores_source_newlines(self):
        """Record-style arguments should format consistently regardless of source whitespace."""
        # Single-line input
        code1 = b'local filter = Log::Filter($name="conn-app", $path="conn_app", $include=set("id.orig_h", "id.orig_p", "id.resp_h", "id.resp_p", "app"), $policy=conn_apps_only);'
        # Multi-line input (same semantically)
        code2 = b'''local filter = Log::Filter($name="conn-app", $path="conn_app",
                               $include=set("id.orig_h", "id.orig_p",
                                            "id.resp_h", "id.resp_p",
                                            "app"), $policy=conn_apps_only);'''

        result1 = self._format(code1)
        result2 = self._format(code2)
        # Both should produce identical output
        self.assertEqual(result1, result2)
        # $policy should be on its own line (not after closing paren of set)
        decoded = result1.decode()
        self.assertIn("),\n", decoded)  # Newline after set's closing paren
        self.assertIn("$policy=", decoded)


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
